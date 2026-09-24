#pragma once
#include <JuceHeader.h>

class MasterLimiter {
public:
    MasterLimiter() : sampleRate(44100.0) {}

    void prepare(double newSampleRate) {
        sampleRate = newSampleRate;
        releaseCoeff = std::exp(-1.0 / (sampleRate * 0.1));
        currentGain = 1.0f;
    }

    void process(juce::AudioBuffer<float>& buffer) {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        const float threshold = 1.0f; 

        auto* channelData = buffer.getArrayOfWritePointers();

        for (int i = 0; i < numSamples; ++i) {
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
    float currentGain = 1.0f;
    float releaseCoeff = 0.999f;
};
