#pragma once
#include <JuceHeader.h>

class AudioEngine;
class AutoDJ;
class Playlist;
class MusicLibrary;

// Protección contra silencio. Vigila la salida al aire y, si queda en silencio más de
// thresholdSeconds con la emisora en automático:
// - si un deck suena pero su pista es silencio, la salta;
// - si nada suena, reanuda la cola, y si está vacía la llena con la categoría de
//   emergencia (o con música de toda la biblioteca si esa categoría no tiene pistas).
// En manual, o con la locución abierta, solo avisa: el silencio puede ser intencional.
// No actúa hasta que algo haya salido al aire en esta sesión.
class DeadAirGuard : private juce::Timer {
public:
    DeadAirGuard(AudioEngine& engine, AutoDJ& autoDJ, Playlist& playlist, MusicLibrary& library);
    ~DeadAirGuard() override;

    void setEnabled(bool enabled);
    void setThresholdSeconds(double seconds) { thresholdSeconds = seconds; }
    void setEmergencyCategory(const juce::String& name) { emergencyCategory = name; }

    // true mientras la salida al aire lleva en silencio más que el umbral.
    bool isAlarmActive() const { return alarmActive; }

    enum class Action { None, Alarm, SkipDeckA, SkipDeckB, Resume, RefillAndResume };

    struct Situation {
        bool enabled = true;
        bool everOnAir = false;       // ya salió algo al aire en esta sesión
        bool autoMode = true;
        bool talkover = false;
        double silenceSeconds = 0.0;  // salida al aire
        double threshold = 20.0;
        bool deckPlaying[2] { false, false };
        double deckSilenceSeconds[2] { 0.0, 0.0 };  // antes del fader
        bool queueEmpty = false;
    };
    static Action decide(const Situation& situation);

private:
    void timerCallback() override;
    bool refillFromEmergency();

    AudioEngine& audioEngine;
    AutoDJ& autoDJ;
    Playlist& playlist;
    MusicLibrary& library;

    bool enabled = true;
    double thresholdSeconds = 20.0;
    juce::String emergencyCategory = "EMERGENCIA";
    bool everOnAir = false;
    bool alarmActive = false;
    juce::uint32 lastActionMs = 0;
};
