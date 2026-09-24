#include "DeadAirGuard.h"
#include "AutoDJ.h"
#include "../audio/AudioEngine.h"
#include "../library/MusicLibrary.h"
#include "../library/Playlist.h"
#include "../generator/PlaylistGenerator.h"

DeadAirGuard::DeadAirGuard(AudioEngine& engine, AutoDJ& adj, Playlist& pl, MusicLibrary& lib)
    : audioEngine(engine), autoDJ(adj), playlist(pl), library(lib)
{
    startTimer(500);
}

DeadAirGuard::~DeadAirGuard() {
    stopTimer();
}

void DeadAirGuard::setEnabled(bool shouldBeEnabled) {
    enabled = shouldBeEnabled;
    if (!enabled) alarmActive = false;
}

DeadAirGuard::Action DeadAirGuard::decide(const Situation& s) {
    if (!s.enabled || !s.everOnAir || s.silenceSeconds < s.threshold) return Action::None;

    // En manual, o con la locución abierta, el silencio puede ser intencional.
    if (!s.autoMode || s.talkover) return Action::Alarm;

    // Un deck suena pero lo que entrega es silencio: el problema es la pista.
    for (int i = 0; i < 2; ++i)
        if (s.deckPlaying[i] && s.deckSilenceSeconds[i] >= s.threshold)
            return i == 0 ? Action::SkipDeckA : Action::SkipDeckB;

    // Suena algo que no llega al aire (fader cerrado, ruteo a CUE): no se arregla solo.
    if (s.deckPlaying[0] || s.deckPlaying[1]) return Action::Alarm;

    return s.queueEmpty ? Action::RefillAndResume : Action::Resume;
}

void DeadAirGuard::timerCallback() {
    auto& mixer = audioEngine.getMixer();
    Deck* decks[2] = { &audioEngine.getDeck(0), &audioEngine.getDeck(1) };

    Situation s;
    s.enabled = enabled;
    s.autoMode = autoDJ.isEnabled();
    s.talkover = mixer.isTalkoverActive();
    s.silenceSeconds = mixer.getMainSilenceSeconds();
    s.threshold = thresholdSeconds;
    s.queueEmpty = playlist.isEmpty();
    for (int i = 0; i < 2; ++i) {
        s.deckPlaying[i] = decks[i]->isPlaying();
        s.deckSilenceSeconds[i] = mixer.getDeckSilenceSeconds(i);
        if (s.deckPlaying[i] && decks[i]->isRoutedToMain()) everOnAir = true;
    }
    s.everOnAir = everOnAir;

    const auto action = decide(s);
    alarmActive = action != Action::None;
    if (action == Action::None || action == Action::Alarm) return;

    // Después de actuar se espera otro umbral antes de volver a intentarlo.
    const auto now = juce::Time::getMillisecondCounter();
    if (lastActionMs != 0 && now - lastActionMs < (juce::uint32) (thresholdSeconds * 1000.0)) return;
    lastActionMs = now;

    const auto silence = juce::String(s.silenceSeconds, 0) + " s";
    switch (action) {
        case Action::SkipDeckA:
        case Action::SkipDeckB: {
            const int index = action == Action::SkipDeckA ? 0 : 1;
            juce::Logger::writeToLog("DeadAirGuard: " + silence + " of silence, deck " + juce::String(index + 1)
                                     + " is playing silence, skipping");
            decks[index]->startSkip();
            break;
        }
        case Action::Resume:
            juce::Logger::writeToLog("DeadAirGuard: " + silence + " of silence, resuming the queue");
            autoDJ.resumePlayback();
            break;
        case Action::RefillAndResume:
            juce::Logger::writeToLog("DeadAirGuard: " + silence + " of silence and empty queue, playing emergency music");
            if (refillFromEmergency()) autoDJ.resumePlayback();
            break;
        default:
            break;
    }
}

bool DeadAirGuard::refillFromEmergency() {
    // Cualquier música es mejor que el silencio: sin separación por última reproducción.
    GeneratorSettings settings;
    settings.lastPlayedSeparation = 0;
    settings.categorySeparation = 0;
    settings.targetDurationVal = 60;
    settings.insertionMode = InsertionMode::AddToEnd;

    const auto categories = library.getCategories();
    PlaylistGenerator generator(library);

    for (const auto& c : categories) {
        if (juce::String(c.name).equalsIgnoreCase(emergencyCategory)) {
            settings.includedCategories = { c.id };
            if (generator.generate(settings, playlist).success) return true;
            break;
        }
    }

    // Sin categoría de emergencia (o vacía): toda la música, también la que no tiene
    // categoría. El generador deja fuera identificaciones, cuñas y separadores.
    settings.includedCategories.clear();
    return generator.generate(settings, playlist).success;
}
