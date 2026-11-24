#pragma once
#include <JuceHeader.h>
#include <set>

class MusicLibrary;

// Lo que impide borrar un audio temporal: que esté cargado en un deck (suene o no, el deck tiene
// el archivo abierto y está por salir al aire), que suene en la botonera o que esté en la cola.
// Lógica pura: la interfaz junta lo que está pasando (los decks, la botonera, la cola).
namespace TemporaryAudio {

struct Usage {
    std::set<int> onDecks;       // cargados en un deck
    std::set<int> playingCarts;  // sonando en la botonera
    std::set<int> queued;        // en la cola de programación
};

enum class Busy { No, OnDeck, InCart, Queued };
Busy whyBusy(int trackId, const Usage& usage);

// "Vaciar": borra de verdad (archivo y registro) todos los temporales que no están en uso. Los
// que sí, se quedan y se cuentan en busy; failed: los que no se pudieron borrar (un archivo
// abierto por otro programa).
struct EmptyResult { int deleted = 0, busy = 0, failed = 0; };
EmptyResult emptyAll(MusicLibrary& library, const Usage& usage);

}
