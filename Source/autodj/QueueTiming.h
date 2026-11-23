#pragma once
#include <JuceHeader.h>
#include "../database/Database.h"

class Deck;

// Cuándo sonará cada cosa, tal como encadena AutoDJ las pistas: la siguiente entra en el
// punto de mezcla de la que suena (Deck::mixPointFor). Lo usan la cola (hora de inicio de
// cada fila) y la pauta (dónde insertar una tanda para que salga a su hora).
namespace QueueTiming {
    // Segundos entre el inicio de una pista y el de la siguiente.
    double secondsUntilNext(const TrackInfo& track);

    // Segundos hasta que el deck que suena dé paso a la siguiente pista.
    double secondsUntilNext(const Deck& playingDeck);

    // Segundos hasta que empiece la primera pista de la cola: lo que falta en los decks,
    // más un deck cargado y detenido, que sale antes que la cola. Negativo si nada suena.
    double secondsUntilQueueStarts(const Deck& deckA, const Deck& deckB);

    // Inicio de cada pista de la cola, en segundos desde el inicio de la primera.
    std::vector<double> startOffsets(const std::vector<TrackInfo>& queue);

    // Posición de la cola (0 = antes de la primera pista) en la que algo insertado empieza
    // lo más cerca posible de target, sin cortar canciones.
    int findInsertionIndex(const std::vector<TrackInfo>& queue, juce::Time queueStart, juce::Time target);
}
