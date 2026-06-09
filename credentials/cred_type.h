// cred_type.h
#pragma once

enum class CredType : int { Password = 0, CreditCard = 1, Identity = 2, SecureNote = 3 };

inline const char* CredTypeLabel(CredType t)
{
    switch (t) {
    case CredType::CreditCard: return "Credit Card";
    case CredType::Identity:   return "Identity";
    case CredType::SecureNote: return "Secure Note";
    default:                   return "Password";
    }
}
