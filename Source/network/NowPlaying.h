#pragma once
#include <JuceHeader.h>
#include <optional>
#include "../database/Database.h"
#include "../library/HistoryManager.h"

// Lo que sonó, lo que suena y lo que viene, tal como se publica: el título del stream y un
// JSON para la página web de la emisora, una pantalla en cabina u OBS. Solo cuenta la
// música: durante una cuña, una identificación o un separador, los oyentes ven el nombre
// de la emisora, y esas piezas no aparecen entre las anteriores ni las siguientes.
namespace NowPlaying {

struct Item {
    juce::String artist, title;
    double duration = 0.0;  // al aire, en segundos
    juce::Time time;        // cuándo empezó (actual, anteriores) o empezará (siguientes); 0 = sin hora
};

enum class State { Off, Music, Break };  // nada al aire; una canción; una pieza de la emisora

struct Snapshot {
    juce::String station;
    State state = State::Off;
    Item current;            // solo con State::Music
    juce::Time currentEnds;  // cuándo entra lo siguiente; 0 si no se sabe
    std::vector<Item> previous;  // la más reciente primero
    std::vector<Item> next;      // en el orden en que sonarán

    // Para los reproductores: artista y título de la canción, o solo el nombre de la emisora.
    juce::String streamArtist() const { return state == State::Music ? current.artist : juce::String(); }
    juce::String streamTitle() const { return state == State::Music ? current.title : station; }
};

struct Airing {
    TrackInfo track;
    juce::Time started;
    double secondsToNext = -1.0;  // negativo si no se sabe
};

struct Upcoming {
    TrackInfo track;
    double secondsFromNow = -1.0;  // negativo si no se sabe (no suena nada)
};

struct Inputs {
    juce::String station;
    juce::Time now;
    std::optional<Airing> onAir;
    std::vector<HistoryEntry> history;  // la más reciente primero
    std::vector<Upcoming> upcoming;     // en el orden en que sonarán
    std::function<bool(const TrackInfo&)> isMusic;  // sin ella, todo cuenta como música
    int maxPrevious = 5;
    int maxNext = 3;
};

Snapshot build(const Inputs& inputs);

// { station, state: "music" | "break" | "off", updated, now: {…} | null, previous: […], next: […] }
// Cada pista: artist, title, text ("Artista - Título"), duration (s) y su hora en ISO 8601:
// started y ends (now), played (previous) o eta (next; null si no se sabe).
juce::var toVar(const Snapshot& snapshot, juce::Time generatedAt);
juce::String toJson(const Snapshot& snapshot, juce::Time generatedAt);

}
