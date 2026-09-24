#pragma once
#include <JuceHeader.h>
#include "../audio/AudioEngine.h"
#include "../autodj/AutoDJ.h"
#include "../audio/RecordingManager.h"
#include "RecordingPanel.h"
#include "../autodj/DeadAirGuard.h"
#include "../network/StreamEncoder.h"

// Barra superior: marca, estado global de la emisora (al aire / fuera del aire,
// automático / manual, silencio al aire, transmisión y qué suena), grabación, reloj y
// acceso a configuración.
class StatusBar : public juce::Component, public juce::TooltipClient, private juce::Timer
{
public:
    StatusBar(AudioEngine& engine, AutoDJ& autoDJ, RecordingManager& recordingManager,
              DeadAirGuard& deadAirGuard, StreamEncoder& streamEncoder);
    ~StatusBar() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    std::function<void()> onSettingsRequested;

    juce::String getTooltip() override;  // motivo del error de transmisión

private:
    void timerCallback() override;
    void refreshState();

    AudioEngine& audioEngine;
    AutoDJ& autoDJ;
    DeadAirGuard& deadAirGuard;
    StreamEncoder& streamEncoder;

    RecordingPanel recordingPanel;
    juce::TextButton settingsButton;
    juce::Image logo;

    bool onAir = false;
    bool autoMode = false;
    bool deadAir = false;
    bool blinkOn = true;
    StreamEncoder::Status streamStatus = StreamEncoder::Status::Off;
    juce::Rectangle<float> streamChipArea;
    juce::String nowPlaying;
    juce::String clockText;
    juce::String dateText;

    juce::Rectangle<int> brandArea, stateArea, clockArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};
