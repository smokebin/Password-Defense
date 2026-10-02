// kdbx_export.cpp
// KDBX4 write-only export (AES-256-CBC, Argon2id, ChaCha20 inner stream)
//
// Binary layout:
//   [Outer Header]   signatures + TLV fields
//   [Post-Header]    SHA256(header) + HMAC-SHA256(header)
//   [HMAC Blocks]    block 0 (data) + block 1 (terminator)
//   [Encrypted]      AES-256-CBC(inner_header + XML)

#include "kdbx_export.h"
#include "../crypto/aes_cipher.h"
#include "../../tools/utility.h"
#include "../../tools/totp.h"

#include <sodium.h>
#include <cstring>
#include <map>
#include <sstream>

namespace kdbx_export {

static const uint32_t KDBX_SIG1 = 0x9AA2D903;
static const uint32_t KDBX_SIG2 = 0xB54BFB67;
static const uint16_t KDBX_VER_MINOR = 0x0001;
static const uint16_t KDBX_VER_MAJOR = 0x0004;

// AES-256-CBC UUID: {31C1F0E1-77F1-4602-851F-A27D8DB01B4A}
static const uint8_t AES256_UUID[16] = {
    0x31,0xC1,0xF0,0xE1, 0x77,0xF1, 0x46,0x02,
    0x85,0x1F, 0xA2,0x7D,0x8D,0xB0,0x1B,0x4A
};

// Argon2id UUID: {9E298B19-56DB-4773-B23D-FC3EC6F0A1E6}
static const uint8_t ARGON2ID_UUID[16] = {
    0x9E,0x29,0x8B,0x19, 0x56,0xDB, 0x47,0x73,
    0xB2,0x3D, 0xFC,0x3E,0xC6,0xF0,0xA1,0xE6
};

static void write_u8(std::vector<uint8_t>& buf, uint8_t v)
{
    buf.push_back(v);
}

static void write_u16_le(std::vector<uint8_t>& buf, uint16_t v)
{
    buf.push_back((uint8_t)(v & 0xFF));
    buf.push_back((uint8_t)((v >> 8) & 0xFF));
}

static void write_u32_le(std::vector<uint8_t>& buf, uint32_t v)
{
    buf.push_back((uint8_t)(v & 0xFF));
    buf.push_back((uint8_t)((v >> 8) & 0xFF));
    buf.push_back((uint8_t)((v >> 16) & 0xFF));
    buf.push_back((uint8_t)((v >> 24) & 0xFF));
}

static void write_u64_le(std::vector<uint8_t>& buf, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        buf.push_back((uint8_t)((v >> (i * 8)) & 0xFF));
}

static void write_bytes(std::vector<uint8_t>& buf, const uint8_t* data, size_t len)
{
    buf.insert(buf.end(), data, data + len);
}

static std::vector<uint8_t> build_kdf_parameters(const uint8_t salt[16])
{
    std::vector<uint8_t> vd;

    write_u16_le(vd, 0x0100); // version

    auto write_entry = [&](uint8_t type, const char* key, const uint8_t* val, uint32_t val_len) {
        write_u8(vd, type);
        uint32_t key_len = (uint32_t)strlen(key);
        write_u32_le(vd, key_len);
        write_bytes(vd, (const uint8_t*)key, key_len);
        write_u32_le(vd, val_len);
        write_bytes(vd, val, val_len);
    };

    // $UUID = Argon2id (type 0x42 = byte array)
    write_entry(0x42, "$UUID", ARGON2ID_UUID, 16);

    // S = salt (type 0x42 = byte array)
    write_entry(0x42, "S", salt, 16);

    // I = iterations (type 0x05 = UInt64)
    { uint64_t v = 2; write_entry(0x05, "I", (const uint8_t*)&v, 8); }

    // M = memory in bytes (type 0x05 = UInt64)
    { uint64_t v = 67108864; write_entry(0x05, "M", (const uint8_t*)&v, 8); }

    // P = parallelism (type 0x04 = UInt32)
    { uint32_t v = 1; write_entry(0x04, "P", (const uint8_t*)&v, 4); }

    // V = version (type 0x04 = UInt32) — 0x13 = Argon2 v1.3
    { uint32_t v = 0x13; write_entry(0x04, "V", (const uint8_t*)&v, 4); }

    // Terminator
    write_u8(vd, 0x00);

    return vd;
}

static std::vector<uint8_t> build_outer_header(
    const uint8_t master_seed[32],
    const uint8_t iv[16],
    const uint8_t salt[16])
{
    std::vector<uint8_t> hdr;

    write_u32_le(hdr, KDBX_SIG1);
    write_u32_le(hdr, KDBX_SIG2);
    write_u16_le(hdr, KDBX_VER_MINOR);
    write_u16_le(hdr, KDBX_VER_MAJOR);

    // TLV: ID(1) + size(4 LE) + data
    auto write_tlv = [&](uint8_t id, const uint8_t* data, uint32_t len) {
        write_u8(hdr, id);
        write_u32_le(hdr, len);
        if (len > 0)
            write_bytes(hdr, data, len);
    };

    // 0x02 CipherId (16 bytes)
    write_tlv(0x02, AES256_UUID, 16);

    // 0x03 CompressionFlags (4 bytes) — 0 = none
    { uint32_t v = 0; write_tlv(0x03, (const uint8_t*)&v, 4); }

    // 0x04 MasterSeed (32 bytes)
    write_tlv(0x04, master_seed, 32);

    // 0x07 EncryptionIV (16 bytes)
    write_tlv(0x07, iv, 16);

    // 0x0B KdfParameters (VariantDictionary)
    auto kdf_params = build_kdf_parameters(salt);
    write_tlv(0x0B, kdf_params.data(), (uint32_t)kdf_params.size());

    // 0x00 EndOfHeader (size=4, data=0x0D0A0D0A)
    { uint8_t end_data[4] = { 0x0D, 0x0A, 0x0D, 0x0A };
      write_tlv(0x00, end_data, 4); }

    return hdr;
}

struct DerivedKeys {
    uint8_t cipher_key[32];     // AES-256 key
    uint8_t hmac_base_key[64];  // For block HMAC computation
    bool ok = false;
};

static DerivedKeys derive_kdbx_keys(
    const std::string& password,
    const uint8_t master_seed[32],
    const uint8_t salt[16])
{
    DerivedKeys keys{};

    // composite_key = SHA256(SHA256(password))
    uint8_t pw_hash[32];
    crypto_hash_sha256(pw_hash, (const uint8_t*)password.data(), password.size());
    uint8_t composite_key[32];
    crypto_hash_sha256(composite_key, pw_hash, 32);
    sodium_memzero(pw_hash, 32);

    // transformed_key = Argon2id(composite_key, salt, I=2, M=64MB, P=1)
    uint8_t transformed_key[32];
    int rc = crypto_pwhash(
        transformed_key, 32,
        (const char*)composite_key, 32,
        salt,
        2,           // iterations
        67108864ULL, // 64 MB
        crypto_pwhash_ALG_ARGON2ID13);

    sodium_memzero(composite_key, 32);

    if (rc != 0) {
        return keys; // ok = false
    }

    // cipher_key = SHA256(master_seed || transformed_key)
    crypto_hash_sha256_state sha_state;
    crypto_hash_sha256_init(&sha_state);
    crypto_hash_sha256_update(&sha_state, master_seed, 32);
    crypto_hash_sha256_update(&sha_state, transformed_key, 32);
    crypto_hash_sha256_final(&sha_state, keys.cipher_key);

    // hmac_base_key = SHA512(master_seed || transformed_key || 0x01)
    crypto_hash_sha512_state sha512_state;
    crypto_hash_sha512_init(&sha512_state);
    crypto_hash_sha512_update(&sha512_state, master_seed, 32);
    crypto_hash_sha512_update(&sha512_state, transformed_key, 32);
    uint8_t one = 0x01;
    crypto_hash_sha512_update(&sha512_state, &one, 1);
    crypto_hash_sha512_final(&sha512_state, keys.hmac_base_key);

    sodium_memzero(transformed_key, 32);

    keys.ok = true;
    return keys;
}

static void derive_block_hmac_key(uint64_t block_index, const uint8_t hmac_base_key[64], uint8_t out[64])
{
    // SHA512(block_index_8LE || hmac_base_key)
    crypto_hash_sha512_state st;
    crypto_hash_sha512_init(&st);
    uint8_t idx_le[8];
    for (int i = 0; i < 8; i++)
        idx_le[i] = (uint8_t)((block_index >> (i * 8)) & 0xFF);
    crypto_hash_sha512_update(&st, idx_le, 8);
    crypto_hash_sha512_update(&st, hmac_base_key, 64);
    crypto_hash_sha512_final(&st, out);
}

struct HeaderVerification {
    uint8_t sha256[32];
    uint8_t hmac[32];
};

static HeaderVerification compute_header_verification(
    const std::vector<uint8_t>& header,
    const uint8_t hmac_base_key[64])
{
    HeaderVerification hv;

    crypto_hash_sha256(hv.sha256, header.data(), header.size());

    // block_index = 0xFFFF... is the KDBX4 convention for the header block
    uint8_t header_block_key[64];
    derive_block_hmac_key(0xFFFFFFFFFFFFFFFFULL, hmac_base_key, header_block_key);

    crypto_auth_hmacsha256_state hmac_state;
    crypto_auth_hmacsha256_init(&hmac_state, header_block_key, 64);
    crypto_auth_hmacsha256_update(&hmac_state, header.data(), header.size());
    crypto_auth_hmacsha256_final(&hmac_state, hv.hmac);

    sodium_memzero(header_block_key, 64);

    return hv;
}

static std::vector<uint8_t> build_inner_header(const uint8_t inner_stream_key[32])
{
    std::vector<uint8_t> ih;

    // 0x01 InnerRandomStreamId = 3 (ChaCha20)
    write_u8(ih, 0x01);
    write_u32_le(ih, 4);
    { uint32_t v = 3; write_bytes(ih, (const uint8_t*)&v, 4); }

    // 0x02 InnerRandomStreamKey (32 bytes)
    write_u8(ih, 0x02);
    write_u32_le(ih, 32);
    write_bytes(ih, inner_stream_key, 32);

    // 0x00 EndOfInnerHeader (size=0)
    write_u8(ih, 0x00);
    write_u32_le(ih, 0);

    return ih;
}

static std::string xml_escape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c; break;
        }
    }
    return out;
}

static std::string random_kdbx_uuid()
{
    uint8_t buf[16];
    randombytes_buf(buf, 16);
    std::string raw((const char*)buf, 16);
    return helpers::b64_encode(raw);
}

static std::string unix_ms_to_kdbx_time(int64_t ms)
{
    if (ms <= 0) ms = helpers::now_unix_ms();
    time_t secs = (time_t)(ms / 1000);
    struct tm t;
#ifdef _WIN32
    gmtime_s(&t, &secs);
#else
    gmtime_r(&secs, &t);
#endif
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
        t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
        t.tm_hour, t.tm_min, t.tm_sec);
    return std::string(buf);
}

static std::string build_xml(
    const std::vector<Credential>& creds,
    const uint8_t inner_stream_key[32])
{
    // SHA512(inner_stream_key): first 32 = ChaCha20 key, next 12 = nonce
    uint8_t stream_hash[64];
    crypto_hash_sha512(stream_hash, inner_stream_key, 32);
    uint8_t chacha_key[32];
    uint8_t chacha_nonce[12];
    memcpy(chacha_key, stream_hash, 32);
    memcpy(chacha_nonce, stream_hash + 32, 12);
    sodium_memzero(stream_hash, 64);

    // pre-generate keystream for all passwords in one shot
    size_t total_pw_bytes = 0;
    for (const auto& c : creds) {
        if (!c.is_deleted())
            total_pw_bytes += c.password.size();
    }

    std::vector<uint8_t> keystream_buf;
    if (total_pw_bytes > 0) {
        keystream_buf.resize(total_pw_bytes);
        crypto_stream_chacha20_ietf(keystream_buf.data(), total_pw_bytes, chacha_nonce, chacha_key);
    }
    size_t keystream_used = 0;

    auto protect_password = [&](const std::string& pw) -> std::string {
        if (pw.empty()) return "";
        size_t len = pw.size();
        std::string xored(len, '\0');
        for (size_t i = 0; i < len; i++)
            xored[i] = (char)((uint8_t)pw[i] ^ keystream_buf[keystream_used + i]);
        keystream_used += len;
        return helpers::b64_encode(xored);
    };

    // "" = ungrouped, placed directly under Root
    std::map<std::string, std::vector<const Credential*>> groups;
    for (const auto& c : creds) {
        if (c.is_deleted()) continue;
        groups[c.group].push_back(&c);
    }

    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        << "<KeePassFile>\n"
        << "\t<Meta>\n"
        << "\t\t<Generator>Password Defense</Generator>\n"
        << "\t\t<DatabaseName>Export</DatabaseName>\n"
        << "\t\t<MemoryProtection>\n"
        << "\t\t\t<ProtectPassword>True</ProtectPassword>\n"
        << "\t\t</MemoryProtection>\n"
        << "\t</Meta>\n"
        << "\t<Root>\n"
        << "\t\t<Group>\n"
        << "\t\t\t<UUID>" << random_kdbx_uuid() << "</UUID>\n"
        << "\t\t\t<Name>Root</Name>\n";

    auto write_entry = [&](const Credential* c) {
        xml << "\t\t\t\t<Entry>\n"
            << "\t\t\t\t\t<UUID>" << random_kdbx_uuid() << "</UUID>\n"
            << "\t\t\t\t\t<String><Key>Title</Key><Value>" << xml_escape(c->title) << "</Value></String>\n"
            << "\t\t\t\t\t<String><Key>UserName</Key><Value>" << xml_escape(c->user) << "</Value></String>\n"
            << "\t\t\t\t\t<String><Key>Password</Key><Value Protected=\"True\">"
                << protect_password(c->password) << "</Value></String>\n"
            << "\t\t\t\t\t<String><Key>URL</Key><Value>" << xml_escape(c->website) << "</Value></String>\n"
            << "\t\t\t\t\t<String><Key>Notes</Key><Value>" << xml_escape(c->notes) << "</Value></String>\n";

        if (!c->email.empty()) {
            xml << "\t\t\t\t\t<String><Key>Email</Key><Value>" << xml_escape(c->email) << "</Value></String>\n";
        }

        if (c->is_favorite) {
            xml << "\t\t\t\t\t<String><Key>_pd_favorite</Key><Value>True</Value></String>\n";
        }
        if (c->is_pinned) {
            xml << "\t\t\t\t\t<String><Key>_pd_pinned</Key><Value>True</Value></String>\n";
        }

        // "otp" key with Protected="True" is the KeePassXC convention
        if (!c->totp_secret.empty()) {
            const std::string& account = !c->user.empty() ? c->user : c->email;
            std::string uri = totp::generate_otpauth_uri(c->totp_secret, c->website, account);
            if (!uri.empty()) {
                xml << "\t\t\t\t\t<String><Key>otp</Key><Value Protected=\"True\">"
                    << protect_password(uri) << "</Value></String>\n";
            }
        }

        xml << "\t\t\t\t\t<Times>\n"
            << "\t\t\t\t\t\t<CreationTime>" << unix_ms_to_kdbx_time(c->created_at_ms) << "</CreationTime>\n"
            << "\t\t\t\t\t\t<LastModificationTime>" << unix_ms_to_kdbx_time(c->updated_at_ms) << "</LastModificationTime>\n"
            << "\t\t\t\t\t</Times>\n";

        // KeePassXC surfaces History sub-entries as password history
        if (!c->password_history.empty()) {
            xml << "\t\t\t\t\t<History>\n";
            for (const auto& h : c->password_history) {
                xml << "\t\t\t\t\t\t<Entry>\n"
                    << "\t\t\t\t\t\t\t<UUID>" << random_kdbx_uuid() << "</UUID>\n"
                    << "\t\t\t\t\t\t\t<String><Key>Title</Key><Value>" << xml_escape(c->title) << "</Value></String>\n"
                    << "\t\t\t\t\t\t\t<String><Key>UserName</Key><Value>" << xml_escape(c->user) << "</Value></String>\n"
                    << "\t\t\t\t\t\t\t<String><Key>Password</Key><Value Protected=\"True\">"
                        << protect_password(h.password) << "</Value></String>\n"
                    << "\t\t\t\t\t\t\t<Times>\n"
                    << "\t\t\t\t\t\t\t\t<LastModificationTime>" << unix_ms_to_kdbx_time(h.changed_at_ms) << "</LastModificationTime>\n"
                    << "\t\t\t\t\t\t\t</Times>\n"
                    << "\t\t\t\t\t\t</Entry>\n";
            }
            xml << "\t\t\t\t\t</History>\n";
        }

        xml << "\t\t\t\t</Entry>\n";
    };

    if (groups.count("")) {
        for (const auto* c : groups[""]) {
            write_entry(c);
        }
    }

    for (const auto& [group_name, group_creds] : groups) {
        if (group_name.empty()) continue;

        xml << "\t\t\t<Group>\n"
            << "\t\t\t\t<UUID>" << random_kdbx_uuid() << "</UUID>\n"
            << "\t\t\t\t<Name>" << xml_escape(group_name) << "</Name>\n";

        for (const auto* c : group_creds) {
            write_entry(c);
        }

        xml << "\t\t\t</Group>\n";
    }

    xml << "\t\t</Group>\n"
        << "\t</Root>\n"
        << "</KeePassFile>";

    sodium_memzero(chacha_key, 32);
    sodium_memzero(chacha_nonce, 12);
    if (!keystream_buf.empty())
        sodium_memzero(keystream_buf.data(), keystream_buf.size());

    return xml.str();
}

static std::vector<uint8_t> build_hmac_block_stream(
    const std::vector<uint8_t>& encrypted_payload,
    const uint8_t hmac_base_key[64])
{
    std::vector<uint8_t> blocks;

    { // block 0: HMAC(32) + size(4 LE) + data
        uint8_t block_key[64];
        derive_block_hmac_key(0, hmac_base_key, block_key);

        // HMAC-SHA256(block_index_8LE || size_4LE || data)
        uint8_t block_hmac[32];
        crypto_auth_hmacsha256_state hmac_st;
        crypto_auth_hmacsha256_init(&hmac_st, block_key, 64);

        uint8_t idx_le[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        crypto_auth_hmacsha256_update(&hmac_st, idx_le, 8);

        uint32_t data_size = (uint32_t)encrypted_payload.size();
        uint8_t size_le[4];
        size_le[0] = (uint8_t)(data_size & 0xFF);
        size_le[1] = (uint8_t)((data_size >> 8) & 0xFF);
        size_le[2] = (uint8_t)((data_size >> 16) & 0xFF);
        size_le[3] = (uint8_t)((data_size >> 24) & 0xFF);
        crypto_auth_hmacsha256_update(&hmac_st, size_le, 4);

        crypto_auth_hmacsha256_update(&hmac_st, encrypted_payload.data(), encrypted_payload.size());
        crypto_auth_hmacsha256_final(&hmac_st, block_hmac);

        sodium_memzero(block_key, 64);

        write_bytes(blocks, block_hmac, 32);
        write_u32_le(blocks, data_size);
        write_bytes(blocks, encrypted_payload.data(), encrypted_payload.size());
    }

    { // block 1 (terminator): HMAC(32) + size=0
        uint8_t block_key[64];
        derive_block_hmac_key(1, hmac_base_key, block_key);

        uint8_t block_hmac[32];
        crypto_auth_hmacsha256_state hmac_st;
        crypto_auth_hmacsha256_init(&hmac_st, block_key, 64);

        uint8_t idx_le[8] = { 1, 0, 0, 0, 0, 0, 0, 0 };
        crypto_auth_hmacsha256_update(&hmac_st, idx_le, 8);

        uint8_t size_le[4] = { 0, 0, 0, 0 };
        crypto_auth_hmacsha256_update(&hmac_st, size_le, 4);

        crypto_auth_hmacsha256_final(&hmac_st, block_hmac);

        sodium_memzero(block_key, 64);

        write_bytes(blocks, block_hmac, 32);
        write_u32_le(blocks, 0);
    }

    return blocks;
}

ExportResult export_kdbx(
    const std::vector<Credential>& creds,
    const std::string& password,
    const char* file_path)
{
    ExportResult res;

    uint8_t master_seed[32];
    uint8_t enc_iv[16];
    uint8_t kdf_salt[16];
    uint8_t inner_stream_key[32];

    randombytes_buf(master_seed, 32);
    randombytes_buf(enc_iv, 16);
    randombytes_buf(kdf_salt, 16);
    randombytes_buf(inner_stream_key, 32);

    auto header = build_outer_header(master_seed, enc_iv, kdf_salt);

    auto keys = derive_kdbx_keys(password, master_seed, kdf_salt);
    if (!keys.ok) {
        res.error = "Key derivation failed (insufficient memory?)";
        return res;
    }

    auto hv = compute_header_verification(header, keys.hmac_base_key);

    auto inner_hdr = build_inner_header(inner_stream_key);
    std::string xml = build_xml(creds, inner_stream_key);
    sodium_memzero(inner_stream_key, 32);

    std::vector<uint8_t> plaintext;
    plaintext.reserve(inner_hdr.size() + xml.size());
    plaintext.insert(plaintext.end(), inner_hdr.begin(), inner_hdr.end());
    plaintext.insert(plaintext.end(), (uint8_t*)xml.data(), (uint8_t*)xml.data() + xml.size());
    sodium_memzero(xml.data(), xml.size());

    // PKCS7 pad + AES-256-CBC
    std::vector<unsigned char> padded = helpers::add_padding(
        std::vector<unsigned char>(plaintext.begin(), plaintext.end()));
    sodium_memzero(plaintext.data(), plaintext.size());

    AES aes(AESKeyLength::AES_256);
    std::vector<unsigned char> aes_key(keys.cipher_key, keys.cipher_key + 32);
    std::vector<unsigned char> aes_iv(enc_iv, enc_iv + 16);
    auto encrypted = aes.EncryptCBC(padded, aes_key, aes_iv);

    sodium_memzero(aes_key.data(), aes_key.size());
    sodium_memzero(padded.data(), padded.size());

    if (encrypted.empty()) {
        sodium_memzero(keys.cipher_key, 32);
        sodium_memzero(keys.hmac_base_key, 64);
        res.error = "Encryption failed";
        return res;
    }

    auto hmac_blocks = build_hmac_block_stream(
        std::vector<uint8_t>(encrypted.begin(), encrypted.end()),
        keys.hmac_base_key);

    sodium_memzero(keys.cipher_key, 32);
    sodium_memzero(keys.hmac_base_key, 64);

    std::vector<unsigned char> output;
    output.reserve(header.size() + 32 + 32 + hmac_blocks.size());
    output.insert(output.end(), header.begin(), header.end());
    output.insert(output.end(), hv.sha256, hv.sha256 + 32);
    output.insert(output.end(), hv.hmac, hv.hmac + 32);
    output.insert(output.end(), hmac_blocks.begin(), hmac_blocks.end());

    if (!helpers::bytes_to_file(file_path, output)) {
        res.error = "Could not write file";
        return res;
    }

    int count = 0;
    for (const auto& c : creds) {
        if (!c.is_deleted()) count++;
    }

    res.ok = true;
    res.count = count;
    return res;
}

} // namespace kdbx_export
