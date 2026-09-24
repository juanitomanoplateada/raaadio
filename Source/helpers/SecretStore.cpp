#include "SecretStore.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <wincrypt.h>
 #if JUCE_MSVC
  #pragma comment(lib, "crypt32.lib")
 #endif
#endif

namespace SecretStore {

#if JUCE_WINDOWS
// Entropía propia: otra aplicación del mismo usuario necesita conocerla para descifrar.
static DATA_BLOB entropy()
{
    static const char text[] = "RAAADIO station secret";
    return { (DWORD) (sizeof(text) - 1), (BYTE*) text };
}
#endif

juce::String protect(const juce::String& secret)
{
    if (secret.isEmpty()) return {};

   #if JUCE_WINDOWS
    const auto* utf8 = secret.toRawUTF8();
    DATA_BLOB in { (DWORD) secret.getNumBytesAsUTF8(), (BYTE*) utf8 };
    auto extra = entropy();
    DATA_BLOB out {};
    if (CryptProtectData(&in, L"RAAADIO", &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        const auto encoded = "dpapi:" + juce::Base64::toBase64(out.pbData, out.cbData);
        LocalFree(out.pbData);
        return encoded;
    }
   #endif
    return "plain:" + secret;
}

bool unprotect(const juce::String& stored, juce::String& secret)
{
    secret = {};
    if (stored.isEmpty()) return true;

    if (stored.startsWith("plain:")) {
        secret = stored.substring(6);
        return true;
    }

   #if JUCE_WINDOWS
    if (stored.startsWith("dpapi:")) {
        juce::MemoryOutputStream decoded;
        if (!juce::Base64::convertFromBase64(decoded, stored.substring(6)) || decoded.getDataSize() == 0)
            return false;

        DATA_BLOB in { (DWORD) decoded.getDataSize(), (BYTE*) decoded.getData() };
        auto extra = entropy();
        DATA_BLOB out {};
        if (!CryptUnprotectData(&in, nullptr, &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
            return false;
        secret = juce::String::fromUTF8((const char*) out.pbData, (int) out.cbData);
        SecureZeroMemory(out.pbData, out.cbData);
        LocalFree(out.pbData);
        return true;
    }
   #endif
    return false;
}

}
