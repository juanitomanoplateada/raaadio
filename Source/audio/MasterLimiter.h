#pragma once
#include <JuceHeader.h>

// Limitador de pico: ataque instantáneo (ninguna muestra pasa del techo) y 100 ms de
// recuperación. En la salida al aire el techo es 0 dBFS; la transmisión por internet usa
// otro con -1 dB, porque al comprimir en MP3 u Ogg aparecen picos por encima del original.
class MasterLimiter {
public:
    explicit MasterLimiter(float ceilingDb = 0.0f) : sampleRate(44100.0), threshold(juce::Decibels::decibelsToGain(ceilingDb)) {}

    void prepare(double newSampleRate) {
        sampleRate = newSampleRate;
        releaseCoeff = std::exp(-1.0 / (sampleRate * 0.1));
        currentGain = 1.0f;
    }

    float getCeiling() const { return threshold; }

    void process(juce::AudioBuffer<float>& buffer) {
        process(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), 0, buffer.getNumSamples());
    }

    void process(float* const* channelData, int numChannels, int startSample, int numSamples) {
        for (int i = startSample; i < startSample + numSamples; ++i) {
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch) {
                peak = std::max(peak, std::abs(channelData[ch][i]));
            }

            float targetGain = 1.0f;
            if (peak > threshold) {
                targetGain = threshold / peak;
            }

            if (targetGain < currentGain) {
                currentGain = targetGain; 
            } else {
                currentGain = currentGain * releaseCoeff + targetGain * (1.0f - releaseCoeff);
            }

            for (int ch = 0; ch < numChannels; ++ch) {
                channelData[ch][i] *= currentGain;
            }
        }
    }

private:
    double sampleRate;
    float threshold;
    float currentGain = 1.0f;
    float releaseCoeff = 0.999f;
};
