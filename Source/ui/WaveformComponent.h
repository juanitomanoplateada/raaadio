#pragma once
#include <JuceHeader.h>

class WaveformComponent : public juce::Component,
                          public juce::ChangeListener,
                          public juce::SettableTooltipClient
{
public:
    WaveformComponent(juce::AudioThumbnail& thumbnail);
    ~WaveformComponent() override;

    void paint(juce::Graphics& g) override;
    void lookAndFeelChanged() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    void setPosition(double fraction);
    void setScalingFactor(float scale) { scalingFactor = scale; isImageValid = false; repaint(); }
    std::function<void(double)> onPositionChanged;

    // Si devuelve true, un clic no mueve la reproducción salvo que se mantenga Shift
    // (protege al deck que está al aire de un clic accidental).
    std::function<bool()> isSeekLocked;

    // Segundos finales que se marcan como zona de salida.
    void setEndWarningSeconds(double seconds) { endWarningSeconds = seconds; repaint(); }

    // Puntos de entrada y salida, en segundos (outSeconds 0 = final del archivo). Lo que
    // queda fuera no sale al aire y se dibuja atenuado.
    void setCueRegion(double inSeconds, double outSeconds) { cueInSeconds = inSeconds; cueOutSeconds = outSeconds; repaint(); }

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void resized() override;

private:
    bool canSeek(const juce::MouseEvent& e) const;

    juce::AudioThumbnail& thumbnail;
    double currentPosition = 0.0;
    float scalingFactor = 1.0f;
    double endWarningSeconds = 15.0;
    double cueInSeconds = 0.0;
    double cueOutSeconds = 0.0;

    juce::Image waveformImage;
    bool isImageValid = false;
};
