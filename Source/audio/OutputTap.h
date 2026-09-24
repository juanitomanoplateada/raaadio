#pragma once
#include <JuceHeader.h>

// Recibe la salida al aire tal como se emite (después del limitador): la grabación, el
// registro por hora y la transmisión por internet. pushBlock() se llama desde el hilo de
// audio, así que no puede bloquear: copia a un FIFO y escribe desde su propio hilo.
class OutputTap {
public:
    virtual ~OutputTap() = default;
    virtual void prepare(double sampleRate) = 0;
    virtual void pushBlock(const juce::AudioBuffer<float>& buffer, int startSample, int numSamples) = 0;
};
