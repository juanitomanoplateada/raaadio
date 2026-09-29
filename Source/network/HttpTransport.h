#pragma once
#include <JuceHeader.h>

// Un pedido HTTP y su respuesta, para los servicios en línea (AcoustID, MusicBrainz, Deezer,
// YouTube). Quien los usa recibe un Transport: en la aplicación, send(); en las pruebas, uno
// que responde con datos de ejemplo sin conectarse.
namespace HttpTransport {

struct Request {
    juce::String url, body;  // body vacío: GET; si no, POST
    juce::String headers;
    bool binary = false;     // la respuesta es un archivo (una carátula), no texto
};

struct Response {
    int status = 0;          // 0: sin conexión
    juce::String body;
    juce::MemoryBlock data;  // si el pedido era binario
};

using Transport = std::function<Response(const Request&)>;

// El pedido de verdad (bloquea hasta que responde o pasan 15 s sin conexión).
Response send(const Request& request);

// Bajar un archivo grande (un programa) directo al disco. progress recibe el avance de 0 a 1
// (-1 si el servidor no dice el tamaño) y devuelve false para cancelar. Si falla o se cancela,
// target no queda a medias.
using Progress = std::function<bool(double)>;
using FileFetcher = std::function<bool(const juce::String& url, const juce::File& target, const Progress& progress)>;
bool downloadToFile(const juce::String& url, const juce::File& target, const Progress& progress);

}
