#include "StreamSocket.h"
#include <cstring>
#include <vector>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #define SECURITY_WIN32
 #include <windows.h>
 #include <sspi.h>
 #include <schannel.h>
 #pragma comment(lib, "secur32.lib")
#endif

struct StreamSocket::Tls {
    std::vector<char> incoming;  // recibido cifrado, todavía sin descifrar
    std::vector<char> plain;     // descifrado, todavía sin entregar
    size_t plainPos = 0;

   #if JUCE_WINDOWS
    CredHandle credentials {};
    CtxtHandle context {};
    bool haveCredentials = false, haveContext = false;
    SecPkgContext_StreamSizes sizes {};

    ~Tls() {
        if (haveContext) DeleteSecurityContext(&context);
        if (haveCredentials) FreeCredentialsHandle(&credentials);
    }
   #endif
};

#if JUCE_WINDOWS
namespace {
    // Los motivos más comunes, con el nombre que traen en la documentación de Windows.
    juce::String describe(SECURITY_STATUS status) {
        const auto code = "0x" + juce::String::toHexString((int) status).toUpperCase();
        switch (status) {
            case SEC_E_UNTRUSTED_ROOT:      return "certificate not trusted (SEC_E_UNTRUSTED_ROOT)";
            case SEC_E_WRONG_PRINCIPAL:     return "certificate is for another name (SEC_E_WRONG_PRINCIPAL)";
            case SEC_E_CERT_EXPIRED:        return "certificate expired (SEC_E_CERT_EXPIRED)";
            case SEC_E_ILLEGAL_MESSAGE:     return "the port does not answer TLS (SEC_E_ILLEGAL_MESSAGE)";
            case SEC_E_ALGORITHM_MISMATCH:  return "no TLS version in common (SEC_E_ALGORITHM_MISMATCH)";
            case SEC_I_RENEGOTIATE:         return "the server asked to renegotiate (SEC_I_RENEGOTIATE)";
            default:                        return code;
        }
    }
}
#endif

StreamSocket::StreamSocket() = default;

StreamSocket::~StreamSocket() {
    close();
}

bool StreamSocket::connect(const juce::String& host, int port, bool useTls, int timeoutMs) {
    secure.reset();
    tls = useTls;
    lastError = {};
    if (!socket.connect(host, port, timeoutMs)) return false;
    if (!useTls) return true;

   #if JUCE_WINDOWS
    secure = std::make_unique<Tls>();
    if (handshake(host, timeoutMs)) return true;
   #else
    lastError = "TLS is not available on this system yet";
   #endif
    socket.close();
    return false;
}

void StreamSocket::close() {
    socket.close();
}

bool StreamSocket::rawWriteAll(const void* data, int numBytes) {
    const auto* bytes = static_cast<const char*>(data);
    for (int sent = 0; sent < numBytes;) {
        const int n = socket.write(bytes + sent, numBytes - sent);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

int StreamSocket::rawRead(void* dest, int maxBytes, int timeoutMs) {
    const int ready = socket.waitUntilReady(true, timeoutMs);
    if (ready <= 0) return ready;
    const int n = socket.read(dest, maxBytes, false);
    // En Windows JUCE deja el socket en el modo de la última lectura. En modo no bloqueante,
    // un envío que encuentra la red atascada un instante fallaría (WSAEWOULDBLOCK) en vez de
    // esperar, y se tomaría por una conexión perdida: se vuelve a bloqueante sin leer nada.
    char none = 0;
    socket.read(&none, 0, true);
    return n > 0 ? n : -1;  // anunciaba datos y no había nada: el servidor cerró
}

bool StreamSocket::writeAll(const void* data, int numBytes) {
    if (!tls) return rawWriteAll(data, numBytes);
   #if JUCE_WINDOWS
    if (secure == nullptr) return false;
    auto& t = *secure;
    const auto* bytes = static_cast<const char*>(data);
    std::vector<char> record;
    for (int done = 0; done < numBytes;) {
        const int chunk = juce::jmin(numBytes - done, (int) t.sizes.cbMaximumMessage);
        record.resize(t.sizes.cbHeader + (size_t) chunk + t.sizes.cbTrailer);
        std::memcpy(record.data() + t.sizes.cbHeader, bytes + done, (size_t) chunk);
        SecBuffer buffers[4] {
            { t.sizes.cbHeader, SECBUFFER_STREAM_HEADER, record.data() },
            { (unsigned long) chunk, SECBUFFER_DATA, record.data() + t.sizes.cbHeader },
            { t.sizes.cbTrailer, SECBUFFER_STREAM_TRAILER, record.data() + t.sizes.cbHeader + chunk },
            { 0, SECBUFFER_EMPTY, nullptr },
        };
        SecBufferDesc desc { SECBUFFER_VERSION, 4, buffers };
        if (EncryptMessage(&t.context, 0, &desc, 0) != SEC_E_OK) return false;
        // Cabecera, datos y cola van seguidos en record; la cola puede salir más corta.
        if (!rawWriteAll(record.data(), (int) (buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer))) return false;
        done += chunk;
    }
    return true;
   #else
    return false;
   #endif
}

int StreamSocket::readSome(void* dest, int maxBytes, int timeoutMs) {
    if (!tls) return rawRead(dest, maxBytes, timeoutMs);
   #if JUCE_WINDOWS
    if (secure == nullptr) return -1;
    auto& t = *secure;
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax(0, timeoutMs);
    for (;;) {
        if (t.plainPos < t.plain.size()) {
            const int n = juce::jmin(maxBytes, (int) (t.plain.size() - t.plainPos));
            std::memcpy(dest, t.plain.data() + t.plainPos, (size_t) n);
            t.plainPos += (size_t) n;
            if (t.plainPos == t.plain.size()) {
                t.plain.clear();
                t.plainPos = 0;
            }
            return n;
        }

        if (!t.incoming.empty()) {
            SecBuffer buffers[4] {
                { (unsigned long) t.incoming.size(), SECBUFFER_DATA, t.incoming.data() },
                { 0, SECBUFFER_EMPTY, nullptr }, { 0, SECBUFFER_EMPTY, nullptr }, { 0, SECBUFFER_EMPTY, nullptr },
            };
            SecBufferDesc desc { SECBUFFER_VERSION, 4, buffers };
            const auto status = DecryptMessage(&t.context, &desc, 0, nullptr);
            if (status == SEC_E_OK) {
                // Se descifra en el lugar: primero se copia lo descifrado, después se quita
                // de incoming lo usado (queda lo que sobró, el comienzo del registro siguiente).
                size_t extra = 0;
                for (const auto& b : buffers) {
                    if (b.BufferType == SECBUFFER_DATA)
                        t.plain.insert(t.plain.end(), static_cast<char*>(b.pvBuffer), static_cast<char*>(b.pvBuffer) + b.cbBuffer);
                    else if (b.BufferType == SECBUFFER_EXTRA)
                        extra = b.cbBuffer;
                }
                t.incoming.erase(t.incoming.begin(), t.incoming.end() - (std::ptrdiff_t) extra);
                continue;
            }
            if (status == SEC_I_CONTEXT_EXPIRED) return -1;  // el servidor cerró la sesión cifrada
            if (status != SEC_E_INCOMPLETE_MESSAGE) {
                lastError = describe(status);
                return -1;
            }
        }

        const int remaining = (int) (deadline - juce::Time::getMillisecondCounter());
        char buffer[8192];
        const int n = rawRead(buffer, (int) sizeof(buffer), juce::jmax(0, juce::jmin(remaining, timeoutMs)));
        if (n <= 0) return n;  // 0: nada todavía (o un registro a medias); -1: cerrado
        t.incoming.insert(t.incoming.end(), buffer, buffer + n);
    }
   #else
    juce::ignoreUnused(dest, maxBytes, timeoutMs);
    return -1;
   #endif
}

bool StreamSocket::handshake(const juce::String& host, int timeoutMs) {
   #if JUCE_WINDOWS
    auto& t = *secure;

    SCHANNEL_CRED cred {};
    cred.dwVersion = SCHANNEL_CRED_VERSION;
    // TLS 1.2: con 1.3, Schannel entrega después del saludo mensajes del servidor (los
    // "tickets") que habría que procesar aparte. Los servidores actuales aceptan 1.2.
    cred.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
    // Schannel valida el certificado y que sea de este servidor con las raíces de Windows.
    cred.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
    auto status = AcquireCredentialsHandleW(nullptr, const_cast<LPWSTR>(UNISP_NAME_W), SECPKG_CRED_OUTBOUND, nullptr,
                                            &cred, nullptr, nullptr, &t.credentials, nullptr);
    if (status != SEC_E_OK) {
        lastError = describe(status);
        return false;
    }
    t.haveCredentials = true;

    std::wstring target(host.toWideCharPointer());
    const DWORD request = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY
                        | ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
    DWORD returned = 0;

    // Envía el mensaje que preparó Schannel (si hay) y lo libera.
    auto sendToken = [this](SecBuffer& token) {
        const bool ok = token.cbBuffer == 0 || rawWriteAll(token.pvBuffer, (int) token.cbBuffer);
        if (token.pvBuffer != nullptr) FreeContextBuffer(token.pvBuffer);
        token = { 0, SECBUFFER_TOKEN, nullptr };
        return ok;
    };

    SecBuffer outToken { 0, SECBUFFER_TOKEN, nullptr };
    SecBufferDesc outDesc { SECBUFFER_VERSION, 1, &outToken };
    status = InitializeSecurityContextW(&t.credentials, nullptr, target.data(), request, 0, 0, nullptr, 0,
                                        &t.context, &outDesc, &returned, nullptr);
    if (status != SEC_I_CONTINUE_NEEDED) {
        lastError = describe(status);
        return false;
    }
    t.haveContext = true;
    if (!sendToken(outToken)) {
        lastError = "could not send the TLS greeting";
        return false;
    }

    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    auto& in = t.incoming;
    bool needMore = true;
    for (;;) {
        if (needMore) {
            const int remaining = (int) (deadline - juce::Time::getMillisecondCounter());
            char buffer[8192];
            const int n = remaining > 0 ? rawRead(buffer, (int) sizeof(buffer), remaining) : 0;
            if (n <= 0) {
                lastError = n == 0 ? "the server did not answer the TLS greeting" : "the server closed the connection during the TLS greeting";
                return false;
            }
            in.insert(in.end(), buffer, buffer + n);
        }

        SecBuffer inBuffers[2] {
            { (unsigned long) in.size(), SECBUFFER_TOKEN, in.data() },
            { 0, SECBUFFER_EMPTY, nullptr },
        };
        SecBufferDesc inDesc { SECBUFFER_VERSION, 2, inBuffers };
        status = InitializeSecurityContextW(&t.credentials, &t.context, target.data(), request, 0, 0, &inDesc, 0,
                                            nullptr, &outDesc, &returned, nullptr);

        if (status == SEC_E_INCOMPLETE_MESSAGE) {  // falta parte de lo que manda el servidor
            if (outToken.pvBuffer != nullptr) FreeContextBuffer(outToken.pvBuffer);
            outToken = { 0, SECBUFFER_TOKEN, nullptr };
            needMore = true;
            continue;
        }
        if (status != SEC_E_OK && status != SEC_I_CONTINUE_NEEDED) {
            sendToken(outToken);  // si Schannel preparó una alerta para el servidor, que la reciba
            lastError = describe(status);
            return false;
        }
        if (!sendToken(outToken)) {
            lastError = "could not send the TLS greeting";
            return false;
        }
        // Lo que sobró es el comienzo del mensaje siguiente (o, al terminar, datos ya cifrados).
        if (inBuffers[1].BufferType == SECBUFFER_EXTRA)
            in.erase(in.begin(), in.end() - (std::ptrdiff_t) inBuffers[1].cbBuffer);
        else
            in.clear();
        if (status == SEC_E_OK) break;
        needMore = in.empty();
    }

    status = QueryContextAttributesW(&t.context, SECPKG_ATTR_STREAM_SIZES, &t.sizes);
    if (status != SEC_E_OK) {
        lastError = describe(status);
        return false;
    }
    return true;
   #else
    juce::ignoreUnused(host, timeoutMs);
    return false;
   #endif
}
