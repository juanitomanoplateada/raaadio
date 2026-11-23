#include "AutoDJ.h"
#include "../library/MusicLibrary.h"

AutoDJ::AutoDJ(AudioEngine& engine, Playlist& pl) 
    : audioEngine(engine), playlist(pl)
{
    juce::Logger::writeToLog("AutoDJ::AutoDJ ctor");
    state = AutoPlaying;
    startTimer(50);  // la siguiente entra a lo sumo 50 ms después del punto de mezcla
}

AutoDJ::~AutoDJ() {
    stopTimer();
}

void AutoDJ::setEnabled(bool enabled) {
    if (enabled) {
        state = AutoPlaying;
    } else {
        state = Manual;
        recoveryPending = false;
    }
}

bool AutoDJ::isEnabled() const {
    return state == AutoPlaying;
}

void AutoDJ::timerCallback() {
    processDecks();
}

void AutoDJ::processDecks() {
    Deck& d1 = audioEngine.getDeck(0);
    Deck& d2 = audioEngine.getDeck(1);
    
    checkDeckFinish(d1, d2);
    checkDeckFinish(d2, d1);
    recoverIfStalled(d1, d2);
    prioritiseUpcomingAnalysis();
}

// Si la biblioteca todavía tiene pistas sin analizar (importadas con una versión anterior),
// las próximas de la cola pasan primero: así llegan al deck con su análisis hecho.
void AutoDJ::prioritiseUpcomingAnalysis() {
    const auto modified = playlist.getLastModified();
    if (modified == lastQueueSeen) return;
    lastQueueSeen = modified;

    auto upcoming = playlist.getQueue();
    if (upcoming.size() > 3) upcoming.resize(3);
    audioEngine.getDeck(0).getLibrary().prioritiseAnalysis(upcoming);
}

void AutoDJ::recoverIfStalled(Deck& d1, Deck& d2) {
    if (!recoveryPending || state == Manual) return;

    if (d1.isPlaying() || d2.isPlaying()) {
        recoveryPending = false;
        return;
    }

    // Solo reintenta si hay algo que reproducir: un deck ya cargado o pistas en la lista.
    Deck& target = d1.isLoaded() ? d1 : (d2.isLoaded() ? d2 : d1);
    if (!target.isLoaded() && playlist.isEmpty()) return;

    juce::Logger::writeToLog("AutoDJ: Recovering from silence on Deck " + juce::String(target.getId()));
    if (triggerNextDeck(target)) {
        recoveryPending = false;
    }
}

void AutoDJ::checkDeckFinish(Deck& activeDeck, Deck& otherDeck) {
    if (state == Manual) {
        if (activeDeck.isLoaded() && activeDeck.hasFinished()) {
             juce::Logger::writeToLog("AutoDJ: Manual Mode - Deck Fin. Unloading.");
             activeDeck.unload();
        }
        return;
    }

    // Llegó al punto de mezcla: entra la siguiente y lo que queda de esta (el final que ya
    // se desvanece) baja con un fundido. Un deck que terminó y aún no se descargó no se
    // vuelve a poner al aire.
    if (activeDeck.isLoaded() && activeDeck.isPlaying() && activeDeck.isReadyToMix()
        && !otherDeck.isPlaying() && !otherDeck.hasFinished()) {
        juce::Logger::writeToLog("AutoDJ: Mix Triggered by Deck " + juce::String(activeDeck.getId()));
        if (triggerNextDeck(otherDeck)) activeDeck.startMixFade();
    }

    if (activeDeck.isLoaded() && activeDeck.hasFinished()) {
        juce::Logger::writeToLog("AutoDJ: Deck " + juce::String(activeDeck.getId()) + " Finished. Unloading.");
        activeDeck.unload();

        if (!otherDeck.isPlaying()) {
             if (!triggerNextDeck(otherDeck)) {
                 recoveryPending = true;
             }
        } else if (otherDeck.isFadingOut()) {
            // Una pista corta terminó antes que el fundido de la anterior: la programación
            // sigue en este deck, sin esperar a que el otro se apague.
            if (!triggerNextDeck(activeDeck)) recoveryPending = true;
        }
    }
}

bool AutoDJ::resumePlayback() {
    Deck& d1 = audioEngine.getDeck(0);
    Deck& d2 = audioEngine.getDeck(1);
    if (d1.isPlaying() || d2.isPlaying()) return true;

    Deck& target = d1.isLoaded() ? d1 : (d2.isLoaded() ? d2 : d1);
    target.setRouting(true, target.isRoutedToCue());
    return triggerNextDeck(target);
}

bool AutoDJ::triggerNextDeck(Deck& targetDeck) {
    if (targetDeck.isLoaded()) {
        juce::Logger::writeToLog("AutoDJ: Playing pre-loaded Deck " + juce::String(targetDeck.getId()));
        targetDeck.play();
        return targetDeck.isPlaying();
    }

    // Salta archivos faltantes o ilegibles en lugar de quedarse en silencio.
    while (!playlist.isEmpty()) {
        TrackInfo t = playlist.popNext();
        juce::File f(t.path);

        if (!f.existsAsFile()) {
            juce::Logger::writeToLog("AutoDJ: File not found, skipping: " + f.getFullPathName());
            continue;
        }

        juce::Logger::writeToLog("AutoDJ: Loading from Playlist to Deck " + juce::String(targetDeck.getId()));
        targetDeck.loadTrack(f);

        if (!targetDeck.isLoaded()) {
            juce::Logger::writeToLog("AutoDJ: Could not read file, skipping: " + f.getFullPathName());
            continue;
        }

        targetDeck.play();
        return targetDeck.isPlaying();
    }

    juce::Logger::writeToLog("AutoDJ: Playlist Empty - Enqueue tracks to continue.");
    return false;
}
