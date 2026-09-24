#include <JuceHeader.h>
#include "../library/MusicLibrary.h"
#include "Mixer.h"
#include "Deck.h"

// Por debajo de esto (-50 dBFS de pico) un bloque cuenta como silencio.
static constexpr float silenceThreshold = 0.00316f;

Mixer::Mixer() {
    juce::Logger::writeToLog("Mixer::Mixer ctor entry");
    for(int d=0; d<2; ++d) {
        for(int c=0; c<2; ++c) {
            rmsLevels[d][c] = 0.0f;
            peakLevels[d][c] = 0.0f;
        }
    }
}

void Mixer::addDeck(Deck* deck) {
    decks.push_back(deck);
}

void Mixer::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    tempBuffer.setSize(2, samplesPerBlockExpected);
    
    gainA.reset(sampleRate, 0.02); 
    gainB.reset(sampleRate, 0.02);
    xfade.reset(sampleRate, 0.02);
    duckingGain.reset(sampleRate, 0.4); 
    
    gainA.setCurrentAndTargetValue(1.0f); 
    gainB.setCurrentAndTargetValue(1.0f); 
    xfade.setCurrentAndTargetValue(0.5f);
    masterGain.reset(sampleRate, 0.05);
    masterGain.setCurrentAndTargetValue(1.0f);
    
    masterLimiter.prepare(sampleRate);
    
    juce::Logger::writeToLog("Mixer::prepareToPlay: Preparing Decks...");
    
    cueFifo.setTotalSize(8192);
    cueFifo.reset(); 
    cueFifoBuffer.clear();
    
    for (auto* deck : decks) {
        if (deck) deck->prepareToPlay(samplesPerBlockExpected, sampleRate);
    }

    if (cartSource != nullptr) cartSource->prepareToPlay(samplesPerBlockExpected, sampleRate);
    cartBuffer.setSize(2, samplesPerBlockExpected);

    mainSilentSamples = 0;
    deckSilentSamples[0] = 0;
    deckSilentSamples[1] = 0;
    preparedSampleRate = sampleRate;
    for (auto& tap : taps)
        if (auto* t = tap.load()) t->prepare(sampleRate);
}

void Mixer::addOutputTap(OutputTap* tap) {
    for (auto& slot : taps) {
        if (slot.load() == nullptr) {
            if (preparedSampleRate.load() > 0.0) tap->prepare(preparedSampleRate.load());
            slot = tap;
            return;
        }
    }
    jassertfalse;  // más consumidores que lugares
}

void Mixer::removeOutputTap(OutputTap* tap) {
    for (auto& slot : taps)
        if (slot.load() == tap) slot = nullptr;
}

double Mixer::getMainSilenceSeconds() const {
    const double rate = preparedSampleRate.load();
    return rate > 0.0 ? (double) mainSilentSamples.load() / rate : 0.0;
}

double Mixer::getDeckSilenceSeconds(int deckIndex) const {
    const double rate = preparedSampleRate.load();
    if (rate <= 0.0 || deckIndex < 0 || deckIndex > 1) return 0.0;
    return (double) deckSilentSamples[deckIndex].load() / rate;
}

void Mixer::releaseResources() {
    tempBuffer.setSize(0, 0);
}

void Mixer::setChannelVolume(int channel, float levelDb) {
    float gain = (levelDb <= -60.0f) ? 0.0f : juce::Decibels::decibelsToGain(levelDb);
    if (channel == 0) {
        volA_target = gain;
    } else {
        volB_target = gain;
    }
}

void Mixer::setCrossfader(float value) {
    xfade_target = value; 
}

void Mixer::setTalkover(bool active) {
    duckingTarget = active ? 0.2f : 1.0f;
}

void Mixer::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    bufferToFill.clearActiveBufferRegion(); 
    
    int numSamples = bufferToFill.numSamples; 

    cueInternalBuffer.setSize(2, numSamples, false, false, true);
    cueInternalBuffer.clear(); 
    
    xfade.setTargetValue(xfade_target.load());
    duckingGain.setTargetValue(duckingTarget.load());
    
    xfade.skip(numSamples); 
    float x = xfade.getCurrentValue(); 
    
    float xfadeA = std::cos(x * juce::MathConstants<float>::halfPi);
    float xfadeB = std::sin(x * juce::MathConstants<float>::halfPi);
    
    gainA.setTargetValue(volA_target.load() * xfadeA);
    gainB.setTargetValue(volB_target.load() * xfadeB);
    
    float startDuck = duckingGain.getCurrentValue();
    duckingGain.skip(numSamples);
    float endDuck = duckingGain.getCurrentValue();
    
    for (int i=0; i<decks.size(); ++i) {
        auto* deck = decks[i];
        if (!deck) continue;
        
        tempBuffer.setSize(2, numSamples, false, false, true);
        tempBuffer.clear();
        
        juce::AudioSourceChannelInfo deckInfo(&tempBuffer, 0, numSamples);
        deck->getNextAudioBlock(deckInfo);

        // Lo que entrega el deck antes del fader: si suena y esto es silencio, el problema
        // es la pista y no el fader (protección contra silencio).
        if (i < 2) {
            const float deckPeak = juce::jmax(tempBuffer.getMagnitude(0, 0, numSamples), tempBuffer.getMagnitude(1, 0, numSamples));
            if (deckPeak > silenceThreshold) deckSilentSamples[i] = 0;
            else deckSilentSamples[i] += numSamples;
        }

        // Preescucha antes del fader (PFL): se escucha la pista normalizada tal como sale
        // del deck, sin importar el volumen del canal, el crossfader ni la locución.
        if (deck->isRoutedToCue()) {
            for (int ch = 0; ch < juce::jmin(cueInternalBuffer.getNumChannels(), tempBuffer.getNumChannels()); ++ch)
                cueInternalBuffer.addFrom(ch, 0, tempBuffer, ch, 0, numSamples);
        }

        float currentStart, currentEnd;
        if (i == 0) {
            currentStart = gainA.getCurrentValue();
            gainA.skip(numSamples);
            currentEnd = gainA.getCurrentValue();
        } else {
            currentStart = gainB.getCurrentValue();
            gainB.skip(numSamples); 
            currentEnd = gainB.getCurrentValue();
        }
        
        currentStart *= startDuck;
        currentEnd *= endDuck;
        
        tempBuffer.applyGainRamp(0, numSamples, currentStart, currentEnd);
        
        bool toMain = deck->isRoutedToMain();

        float rmsL = tempBuffer.getRMSLevel(0, 0, numSamples);
        float rmsR = tempBuffer.getRMSLevel(1, 0, numSamples);
        float peakL = tempBuffer.getMagnitude(0, 0, numSamples);
        float peakR = tempBuffer.getMagnitude(1, 0, numSamples);
        
        rmsLevels[i][0].store(rmsL);
        rmsLevels[i][1].store(rmsR);
        
        float currentPL = peakLevels[i][0].load();
        float currentPR = peakLevels[i][1].load();
        peakLevels[i][0].store(std::max(currentPL, peakL));
        peakLevels[i][1].store(std::max(currentPR, peakR));

        if (toMain) {
            for (int ch=0; ch<bufferToFill.buffer->getNumChannels(); ++ch) {
                if (ch < tempBuffer.getNumChannels()) {
                     bufferToFill.buffer->addFrom(ch, bufferToFill.startSample,
                                                  tempBuffer, ch, 0, numSamples);
                }
            }
        }
    }
    
    if (cueFifoBuffer.getNumChannels() < 2) cueFifoBuffer.setSize(2, 8192); 
    
    int space = cueFifo.getFreeSpace();
    
    if (space >= numSamples) {
        int start1, size1, start2, size2;
        cueFifo.prepareToWrite(numSamples, start1, size1, start2, size2);
        
        if (size1 > 0) {
            for (int ch=0; ch<2; ++ch) 
                cueFifoBuffer.copyFrom(ch, start1, cueInternalBuffer, ch, 0, size1);
        }
        if (size2 > 0) {
            for (int ch=0; ch<2; ++ch)
                cueFifoBuffer.copyFrom(ch, start2, cueInternalBuffer, ch, size1, size2);
        }
        cueFifo.finishedWrite(size1 + size2);
    }

    // Botonera: directo al aire, sin fader ni locución.
    if (cartSource != nullptr) {
        cartBuffer.setSize(2, numSamples, false, false, true);
        cartBuffer.clear();
        juce::AudioSourceChannelInfo cartInfo(&cartBuffer, 0, numSamples);
        cartSource->getNextAudioBlock(cartInfo);
        for (int ch = 0; ch < juce::jmin(2, bufferToFill.buffer->getNumChannels()); ++ch)
            bufferToFill.buffer->addFrom(ch, bufferToFill.startSample, cartBuffer, ch, 0, numSamples);
    }

    bufferToFill.buffer->applyGainRamp(bufferToFill.startSample, numSamples,
                                       masterGain.getCurrentValue(), masterGain.getNextValue());

    masterLimiter.process(*bufferToFill.buffer);

    float mainPeak = 0.0f;
    for (int ch = 0; ch < juce::jmin(2, bufferToFill.buffer->getNumChannels()); ++ch) {
        masterRmsLevels[ch].store(bufferToFill.buffer->getRMSLevel(ch, bufferToFill.startSample, numSamples));
        const float peak = bufferToFill.buffer->getMagnitude(ch, bufferToFill.startSample, numSamples);
        mainPeak = juce::jmax(mainPeak, peak);
        if (peak > masterPeakLevels[ch].load())
            masterPeakLevels[ch].store(peak);
    }
    if (mainPeak > silenceThreshold) mainSilentSamples = 0;
    else mainSilentSamples += numSamples;

    for (auto& tap : taps)
        if (auto* t = tap.load()) t->pushBlock(*bufferToFill.buffer, bufferToFill.startSample, numSamples);
}

void Mixer::readCueBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    if (cueFifoBuffer.getNumChannels() < 2) {
        bufferToFill.clearActiveBufferRegion();
        return;
    }

    int numSamples = bufferToFill.numSamples;
    int ready = cueFifo.getNumReady();
    
    int maxLatency = numSamples * 2; 
    if (maxLatency < 512) maxLatency = 512; 

    if (ready > maxLatency) {
        int toSkip = ready - numSamples; 
        
        int s1, sz1, s2, sz2;
        cueFifo.prepareToRead(toSkip, s1, sz1, s2, sz2);
        cueFifo.finishedRead(sz1 + sz2);
    }

    if (ready < numSamples) {
        bufferToFill.clearActiveBufferRegion();
        return;
    }
    
    int start1, size1, start2, size2;
    cueFifo.prepareToRead(numSamples, start1, size1, start2, size2);
    
    if (size1 > 0) {
        for (int ch=0; ch<bufferToFill.buffer->getNumChannels(); ++ch)
            bufferToFill.buffer->copyFrom(ch, bufferToFill.startSample, cueFifoBuffer, ch % 2, start1, size1);
    }
    if (size2 > 0) {
        for (int ch=0; ch<bufferToFill.buffer->getNumChannels(); ++ch)
            bufferToFill.buffer->copyFrom(ch, bufferToFill.startSample + size1, cueFifoBuffer, ch % 2, start2, size2);
    }
    
    cueFifo.finishedRead(size1 + size2);
}

float Mixer::getRMS(int deckId, int channel) const {
    if (deckId < 0 || deckId > 1) return 0.0f;
    return rmsLevels[deckId][channel].load();
}

float Mixer::getPeak(int deckId, int channel) {
    if (deckId < 0 || deckId > 1) return 0.0f;
    float val = peakLevels[deckId][channel].exchange(0.0f);
    return val;
}

float Mixer::getMasterRMS(int channel) const {
    if (channel < 0 || channel > 1) return 0.0f;
    return masterRmsLevels[channel].load();
}

float Mixer::getMasterPeak(int channel) {
    if (channel < 0 || channel > 1) return 0.0f;
    return masterPeakLevels[channel].exchange(0.0f);
}
