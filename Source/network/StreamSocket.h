#pragma once
#include <JuceHeader.h>

// Conexión TCP con el servidor de transmisión, sin cifrar o cifrada con TLS (como HTTPS).
//
// TLS sirve para dos cosas: que la clave no viaje a la vista y que un firewall de la red
// no lea el audio. Sin cifrar, los firewalls que revisan el tráfico web (FortiGate, por
// ejemplo) toman el MP3 por peticiones HTTP y, cuando unos bytes al azar parecen una
// dirección inválida, cortan la conexión con su propia página de "bloqueado".
//
// En Windows el cifrado es el de Schannel, el del sistema: valida el certificado y el
// nombre del servidor con los certificados raíz de Windows. En otros sistemas, por ahora,
// conectar con TLS falla con un mensaje claro.
//
// Escribir bloquea hasta que todo salió. close() se puede llamar desde otro hilo para
// destrabar una escritura en una red lenta; lo demás es de un solo hilo.
class StreamSocket {
public:
    StreamSocket();
    ~StreamSocket();

    // Si falla la conexión TCP, getLastError() queda vacío; si falla el saludo TLS, dice por qué.
    bool connect(const juce::String& host, int port, bool useTls, int timeoutMs);
    juce::String getLastError() const { return lastError; }

    bool writeAll(const void* data, int numBytes);

    // Lo que haya llegado, hasta maxBytes: > 0 bytes leídos; 0 si en timeoutMs no llegó
    // nada (con TLS, también si llegó un registro a medias); -1 si el servidor cerró o hubo
    // un error.
    int readSome(void* dest, int maxBytes, int timeoutMs);

    void close();

private:
    bool handshake(const juce::String& host, int timeoutMs);
    int rawRead(void* dest, int maxBytes, int timeoutMs);
    bool rawWriteAll(const void* data, int numBytes);

    juce::StreamingSocket socket;
    bool tls = false;
    juce::String lastError;

    struct Tls;
    std::unique_ptr<Tls> secure;

    JUCE_DECLARE_NON_COPYABLE(StreamSocket)
};
