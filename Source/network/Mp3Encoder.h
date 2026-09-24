#pragma once
#include <JuceHeader.h>

// Codificador MP3 para la transmisión, sobre LAME. La biblioteca (libmp3lame.dll, LGPL) no
// se enlaza con RAAADIO: se carga al abrir el primer codificador, desde la carpeta del
// ejecutable. Sin ella RAAADIO funciona igual; solo no puede transmitir en MP3.
class Mp3Encoder {
public:
    Mp3Encoder() = default;
    ~Mp3Encoder();

    // Tasas de bits que se ofrecen (kbps), en orden, y la recomendada.
    static const juce::Array<int>& bitrates();
    static constexpr int defaultBitrate = 128;

    // Si se encontró libmp3lame.dll, y su versión ("3.100"); vacía si no está.
    static bool isAvailable();
    static juce::String libraryVersion();

    // Estéreo, tasa fija (CBR), calidad alta. Una tasa de muestreo que MP3 no admite se
    // convierte a 44,1 kHz. Devuelve false si falta la biblioteca o LAME rechaza los ajustes.
    bool open(double sampleRate, int bitrateKbps);
    bool isOpen() const { return lame != nullptr; }
    int getOutputSampleRate() const { return outputRate; }

    // Codifica numSamples muestras de cada canal (valores de -1 a 1) y agrega a out los
    // bytes MP3 que salgan (LAME entrega por cuadros: a veces nada, a veces varios).
    bool encode(const float* left, const float* right, int numSamples, juce::MemoryBlock& out);

    // Lo que LAME retiene todavía (el final del último cuadro). Después hay que volver a abrir.
    void flush(juce::MemoryBlock& out);
    void close();

    // Tasa de muestreo de salida para una de entrada: la misma si MP3 la admite.
    static int outputRateFor(double sampleRate);

private:
    void* lame = nullptr;  // lame_global_flags*
    int outputRate = 44100;
    juce::HeapBlock<unsigned char> buffer;
    int bufferSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Mp3Encoder)
};
