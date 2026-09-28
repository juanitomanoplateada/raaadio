#pragma once
#include <JuceHeader.h>

// Huella de Chromaprint: la que entiende AcoustID para decir qué grabación es un archivo. Usa
// chromaprint.dll (LGPL), que va junto al ejecutable y se carga la primera vez que se pide;
// sin ella todo funciona menos la búsqueda en línea. No tiene que ver con AcousticFingerprint,
// que es la huella propia para reconocer duplicados sin internet.
namespace Chromaprint {

bool isAvailable();
juce::String libraryVersion();  // vacío si no está

struct Result {
    juce::String fingerprint;  // comprimida y en base64, como la pide AcoustID; vacía si falló
    int duration = 0;          // segundos de todo el archivo
};

// Huella de los primeros dos minutos (lo que usa AcoustID).
Result compute(juce::AudioFormatReader& reader, const std::function<bool()>& shouldAbort = {});

}
