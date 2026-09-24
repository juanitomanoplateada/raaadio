#pragma once
#include <JuceHeader.h>
#include "Playlist.h"

class MusicLibrary;

// Guarda la cola de reproducción en disco (Config/queue.json) poco después de cada
// cambio y la recupera al iniciar, para que un cierre inesperado no deje la emisora
// sin programación. Se guardan los id de las pistas: al restaurar se leen de nuevo de
// la biblioteca, así los datos y las rutas quedan al día.
class PlaylistStore : private juce::Timer
{
public:
    PlaylistStore(Playlist& playlist, MusicLibrary& library, const juce::File& file);
    ~PlaylistStore() override;

    // Carga la cola guardada al final de la actual. Devuelve cuántas pistas recuperó.
    int restore();

    // Escribe la cola si cambió desde la última vez.
    void saveIfChanged();

private:
    void timerCallback() override;

    Playlist& playlist;
    MusicLibrary& library;
    juce::File file;
    uint64_t savedVersion = 0;

    JUCE_DECLARE_NON_COPYABLE(PlaylistStore)
};
