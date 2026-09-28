#pragma once
#include <JuceHeader.h>

class Deck;

#include "MasterLimiter.h"
#include "OutputTap.h"

class Mixer : public juce::AudioSource {
public:
    Mixer();
    
    void addDeck(Deck* deck);
    
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    
    void setCrossfader(float value); 
    void setChannelVolume(int channel, float levelDb); 
    
    float getRMS(int deckId, int channel) const;
    float getPeak(int deckId, int channel);

    // Nivel de la salida al aire (después del limitador): lo que realmente se emite.
    float getMasterRMS(int channel) const;
    float getMasterPeak(int channel);
    
    void setTalkover(bool active);
    bool isTalkoverActive() const { return duckingTarget.load() < 1.0f; }

    // Protección contra silencio: segundos seguidos sin sonido (por debajo de -50 dBFS) en
    // la salida al aire, y en lo que entrega cada deck antes de su fader.
    double getMainSilenceSeconds() const;
    double getDeckSilenceSeconds(int deckIndex) const;

    // Botonera: se suma a la salida al aire, sin pasar por los faders ni la locución.
    void setCartSource(juce::AudioSource* source) { cartSource = source; }
    // Preescucha de la revisión: solo a la salida de cue (se fija antes de abrir el audio).
    void setPreviewSource(juce::AudioSource* source) { previewSource = source; }

private:
    std::vector<Deck*> decks;
    
    juce::AudioBuffer<float> tempBuffer;
    
    juce::LinearSmoothedValue<float> gainA { 1.0f };
    juce::LinearSmoothedValue<float> gainB { 1.0f };
    
    juce::LinearSmoothedValue<float> xfade { 0.5f };
    
    juce::LinearSmoothedValue<float> duckingGain { 1.0f };

    // Valores que fija la interfaz y lee el hilo de audio en cada bloque.
    std::atomic<float> duckingTarget { 1.0f };
    std::atomic<float> volA_target { 1.0f };
    std::atomic<float> volB_target { 1.0f };
    std::atomic<float> xfade_target { 0.5f };
    
    std::atomic<float> rmsLevels[2][2];
    std::atomic<float> peakLevels[2][2];
    std::atomic<float> masterRmsLevels[2] { {0.0f}, {0.0f} };
    std::atomic<float> masterPeakLevels[2] { {0.0f}, {0.0f} };
    
    juce::AbstractFifo cueFifo { 8192 }; 
    juce::AudioBuffer<float> cueInternalBuffer; 
    juce::AudioBuffer<float> cueFifoBuffer; 
    
public:
    void readCueBlock(const juce::AudioSourceChannelInfo& bufferToFill);

    // Grabación, registro y transmisión. Se agregan antes de abrir el dispositivo de audio
    // y se quitan después de cerrarlo.
    void addOutputTap(OutputTap* tap);
    void removeOutputTap(OutputTap* tap);

private:
    juce::LinearSmoothedValue<float> masterGain { 1.0f };
    MasterLimiter masterLimiter;

    static constexpr int maxTaps = 4;
    std::atomic<OutputTap*> taps[maxTaps] { {nullptr}, {nullptr}, {nullptr}, {nullptr} };
    std::atomic<double> preparedSampleRate { 0.0 };

    juce::AudioSource* cartSource = nullptr;
    juce::AudioBuffer<float> cartBuffer;
    juce::AudioSource* previewSource = nullptr;
    juce::AudioBuffer<float> previewBuffer;

    std::atomic<juce::int64> mainSilentSamples { 0 };
    std::atomic<juce::int64> deckSilentSamples[2] { {0}, {0} };
};
