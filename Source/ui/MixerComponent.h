#pragma once
#include <JuceHeader.h>
#include "../audio/Mixer.h"
#include "../audio/Deck.h"
#include "../autodj/AutoDJ.h"

class MixerComponent : public juce::Component, public juce::Timer {
public:
    MixerComponent(Mixer& mixer, Deck& dA, Deck& dB, AutoDJ& adj);
    ~MixerComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void lookAndFeelChanged() override;

private:
    struct MeterState {
        float rms[2] = { 0.0f, 0.0f };
        float peak[2] = { 0.0f, 0.0f };
    };

    void drawMeter(juce::Graphics& g, juce::Rectangle<float> area, const MeterState& meter);
    void drawFaderLabels(juce::Graphics& g, juce::Slider& fader, juce::Rectangle<int> readout);
    static float levelToProportion(float gain);

    Mixer& mixer;
    AutoDJ& autoDJ;
    Deck& deckA;
    Deck& deckB;

    juce::Slider crossfader;
    juce::Slider volSliderA;
    juce::Slider volSliderB;

    juce::TextButton airA, cueA, airB, cueB;
    juce::TextButton talkoverButton;
    juce::TextButton autoDjButton;

    MeterState meterA, meterB, meterMaster;

    juce::Rectangle<int> titleA, titleB, titleMaster;
    juce::Rectangle<int> meterAreaA, meterAreaB, meterAreaMaster, scaleArea;
    juce::Rectangle<int> readoutA, readoutB, crossfaderArea;
};
