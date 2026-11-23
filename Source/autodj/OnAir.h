#pragma once
#include <JuceHeader.h>
#include "../audio/Deck.h"

// Lo que oyen los oyentes, y cómo poner una pista al aire "ya" sin cortar en seco.
namespace OnAir {

// Suena y va a la salida principal. Una pista en preescucha (solo CUE) no está al aire.
bool isOnAir(const Deck& deck);

// El deck que oyen los oyentes: entre los que están al aire, el que arrancó último
// (durante una mezcla suenan los dos). nullptr si no hay nada al aire.
const Deck* nowPlaying(const Deck& a, const Deck& b);

// Reproducir ahora: la pista va al deck que no suena (A si ninguno suena; si suenan los
// dos, el que está en preescucha), sale al aire y lo que sonaba al aire baja con un
// fundido de 2 s. Nunca corta en seco lo que oyen los oyentes. Devuelve false si el
// archivo no se pudo leer (entonces no cambia nada al aire).
bool playNow(Deck& a, Deck& b, const juce::File& file);

// Lo mismo desde la interfaz: si hay algo al aire, pregunta antes. beforeStart corre al
// confirmar (por ejemplo, para sacar la pista de la cola); si devuelve false, no se hace nada.
void requestPlayNow(Deck& a, Deck& b, const TrackInfo& track, const std::function<bool()>& beforeStart = {});

}
