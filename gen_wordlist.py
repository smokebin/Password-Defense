"""
Download a breach-derived password corpus and generate a C++ header with two
embedded, sorted, newline-separated blobs used by the offline strength check:

  * g_common_pw_blob   - exact common/breached passwords (lowercased)
  * g_common_word_blob - dictionary base words / names (lowercased, alpha-only)

Both are sorted by unsigned-byte order so utility.cpp can binary-search them.

Usage:   python gen_wordlist.py
Output:  tools/common_passwords_data.h

Source: danielmiessler/SecLists (MIT). The default list is the frequency-ranked
"xato top-100k", which already subsumes the rockyou top entries. To scale up,
swap LIST_URL for one of the alternatives below and re-run -- but note the
generated header is ~6x the source text size (top-1M => a ~50MB header that
chokes IntelliSense and bloats the repo). top-100k => ~4.5MB header, on par with
the existing icons/mdi_font_data.h.
"""
import os
import sys
import urllib.request

# ----------------------------------------------------------------------------
# CONFIG
# ----------------------------------------------------------------------------
# Frequency-ranked, breach-derived, includes rockyou. ~100k entries / ~780KB.
LIST_URL = "https://raw.githubusercontent.com/danielmiessler/SecLists/master/Passwords/Common-Credentials/xato-net-10-million-passwords-100000.txt"
# Alternatives (uncomment ONE to change coverage; mind the header-size note above):
#   ...xato-net-10-million-passwords-1000000.txt          # top-1M  (~50MB header)
#   .../Passwords/Leaked-Databases/rockyou-75.txt         # pure rockyou (~59k)

MIN_LEN = 3      # drop 1-2 char noise; too short to be a meaningful "word"
MAX_LEN = 40     # drop pathological long lines
MAX_ENTRIES = None  # None = use every entry in the source list

OUT_PATH = os.path.join(os.path.dirname(__file__), "tools", "common_passwords_data.h")
CACHE_PATH = os.path.join(os.path.dirname(__file__), "_wordlist_tmp.txt")

# ----------------------------------------------------------------------------
# Curated supplements copied from the original tools/utility.cpp lists, so the
# new data-driven check never regresses anything the hand-written sets caught.
# ----------------------------------------------------------------------------
OLD_COMMON_PW = """
123456 password 12345678 qwerty 123456789 12345 1234 111111 1234567 dragon
123123 baseball abc123 football monkey letmein shadow master 666666 qwertyuiop
123321 mustang 1234567890 michael 654321 superman 1qaz2wsx 7777777 121212 000000
qazwsx 123qwe killer trustno1 jordan jennifer zxcvbnm asdfgh hunter buster soccer
harley batman andrew tigger sunshine iloveyou 2000 charlie robert thomas hockey
ranger daniel starwars klaster 112233 george computer michelle jessica pepper
1111 zxcvbn 555555 11111111 131313 freedom 777777 pass maggie 159753 aaaaaa ginger
princess joshua cheese amanda summer love ashley nicole chelsea biteme matthew
access yankees 987654321 dallas austin thunder taylor matrix william corvette
hello martin heather secret merlin diamond 1234qwer gfhjkm hammer silver 222222
88888888 anthony justin test bailey q1w2e3r4t5 patrick internet scooter orange
golfer cookie richard samantha banana abcdef letmein1 1q2w3e4r welcome welcome1
p@ssw0rd passw0rd password1 password123 admin admin123 root toor login abc123456
qwerty123 iloveu changeme password12 password2 qwerty1 qwerty12 dragon1 monkey1
shadow1 master1 jordan23
""".split()

CURATED_WORDS = """
james john robert michael david richard joseph thomas charles christopher daniel
matthew anthony mark donald steven paul andrew joshua kenneth kevin brian george
timothy ronald edward jason jeffrey ryan jacob gary nicholas eric jonathan stephen
larry justin scott brandon benjamin samuel raymond gregory frank alexander patrick
jack dennis jerry tyler aaron jose nathan henry peter adam douglas zachary walter
kyle harold carl jeremy roger keith gerald sean austin albert arthur lawrence terry
jesse dylan bryan joe jordan billy bruce gabriel mary patricia jennifer linda
barbara elizabeth susan jessica sarah karen lisa nancy betty margaret sandra ashley
dorothy kimberly emily donna michelle carol amanda melissa deborah stephanie rebecca
sharon laura cynthia kathleen amy angela shirley anna brenda pamela emma nicole helen
samantha katherine christine debra rachel carolyn janet catherine maria heather diane
ruth julie olivia joyce virginia victoria kelly lauren christina joan evelyn judith
megan andrea cheryl hannah jacqueline martha gloria teresa ann sara madison frances
kathryn janice jean abigail alice judy sophia grace denise amber doris marilyn
danielle beverly isabella theresa diana natalie brittany charlotte marie kayla alexis
lori maggie charlie bailey buster ginger princess cookie shadow tiger buddy ranger
harley lucky sammy max rocky tucker bear molly oscar winston casey bentley password
dragon master monkey letmein football baseball soccer hockey basketball tennis
cricket batman superman spider pokemon naruto gaming gamer player winner loser hello
welcome goodbye secret private access control freedom liberty justice power energy
thunder lightning silver golden diamond crystal phantom ghost ninja samurai warrior
knight prince queen king summer winter spring autumn sunshine rainbow starlight
heaven angel devil demon killer hunter sniper hacker cyber matrix system network
server computer laptop mobile apple google amazon facebook twitter orange banana
cherry mango strawberry blueberry chocolate vanilla coffee butter cheese pepper
mustard ketchup pizza burger taco sushi love lover forever trust faith hope dream
""".split()


def is_ascii(s):
    return all(ord(c) < 128 for c in s)


def clean(raw_lines):
    """Lowercase, ASCII-only, length-bounded, deduped."""
    out = set()
    for line in raw_lines:
        w = line.strip()
        if not w or not is_ascii(w):
            continue
        w = w.lower()
        if MIN_LEN <= len(w) <= MAX_LEN:
            out.add(w)
    return out


def bytes_to_c_array(data):
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    return "\n".join(lines)


def emit_blob(f, name, words):
    """Write a sorted '\\n'-joined blob + its length + entry count."""
    words = sorted(words, key=lambda w: w.encode("ascii"))  # unsigned-byte order
    blob = "\n".join(words).encode("ascii")
    f.write(f"// {name}: {len(words)} entries, {len(blob)} bytes\n")
    f.write(f"static const unsigned char {name}_blob[] = {{\n")
    f.write(bytes_to_c_array(blob))
    f.write("\n};\n")
    f.write(f"static const unsigned long {name}_blob_len = {len(blob)}UL;\n")
    f.write(f"static const unsigned long {name}_count = {len(words)}UL;\n\n")
    return len(words)


def main():
    # --- fetch (cached) ---
    if os.path.exists(CACHE_PATH) and os.path.getsize(CACHE_PATH) > 1000:
        print(f"[cached] {CACHE_PATH}")
        with open(CACHE_PATH, "rb") as fh:
            data = fh.read()
    else:
        print(f"Downloading {LIST_URL} ...")
        req = urllib.request.Request(LIST_URL, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=60) as resp:
            data = resp.read()
        with open(CACHE_PATH, "wb") as fh:
            fh.write(data)
        print(f"  OK ({len(data)} bytes)")

    raw = data.decode("latin-1").splitlines()
    if MAX_ENTRIES:
        raw = raw[:MAX_ENTRIES]

    # --- common passwords: cleaned source + curated supplement ---
    pw_set = clean(raw)
    pw_set |= clean(OLD_COMMON_PW)

    # --- dictionary words: alpha-only entries from the corpus + curated words ---
    word_set = {w for w in pw_set if w.isalpha()}
    word_set |= clean(CURATED_WORDS)
    word_set = {w for w in word_set if w.isalpha() and len(w) >= MIN_LEN}

    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    with open(OUT_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write("// Auto-generated by gen_wordlist.py - do not edit.\n")
        f.write(f"// Source: {LIST_URL}\n")
        f.write("// SecLists is MIT-licensed. Blobs are sorted by unsigned-byte order\n")
        f.write("// for binary search (see tools/utility.cpp).\n")
        f.write("#pragma once\n\n")
        n_pw = emit_blob(f, "g_common_pw", pw_set)
        n_wd = emit_blob(f, "g_common_word", word_set)

    print(f"Generated {OUT_PATH}")
    print(f"  common passwords: {n_pw}")
    print(f"  dictionary words: {n_wd}")


if __name__ == "__main__":
    main()
