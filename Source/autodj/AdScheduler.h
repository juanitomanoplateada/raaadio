#pragma once
#include <JuceHeader.h>
#include <set>
#include "../database/Database.h"

class MusicLibrary;
class AdvertisingManager;
class Playlist;
class Deck;

// Tandas publicitarias de la pauta.
namespace AdBreaks {
    // "HH:MM" del día de day.
    juce::Time timeOfSlot(const juce::String& slot, juce::Time day);

    // Tanda de una franja: una identificación de la emisora al azar y las cuñas de los
    // números programados a esa hora. Vacía si a esa hora no hay cuñas programadas.
    std::vector<TrackInfo> buildBreak(MusicLibrary& library, const AdvertisingManager& ads,
                                      const juce::String& slot, juce::Random& random);
}

// Pauta automática: con la opción activada en la pestaña de publicidad, la tanda de cada
// franja se inserta sola en la cola, en el punto donde empieza más cerca de su hora. Si
// la cola se vacía o se reemplaza (generador) antes de que la tanda suene, se vuelve a
// insertar en la cola nueva.
class AdScheduler : private juce::Timer {
public:
    AdScheduler(MusicLibrary& library, AdvertisingManager& ads, Playlist& playlist,
                Deck& deckA, Deck& deckB);
    ~AdScheduler() override;

    // Se inserta con esta anticipación (la cola ya suele llegar hasta esa hora); si la
    // hora pasó hace menos de lateMinutes (la emisora estaba cerrada), todavía se inserta.
    static constexpr int lookaheadMinutes = 20;
    static constexpr int lateMinutes = 10;

    // Tandas que tocan en now, de las franjas programadas: entre now - lateMinutes y
    // now + lookaheadMinutes, y que no estén en done (claves de keyFor). En orden.
    static std::vector<juce::Time> dueBreaks(const juce::StringArray& slots, juce::Time now,
                                             const std::set<juce::String>& done);
    static juce::String keyFor(juce::Time breakTime);  // "AAAA-MM-DD HH:MM"

    // Lo llama el temporizador cada 15 s; público para las pruebas.
    void checkNow(juce::Time now);

private:
    void timerCallback() override { checkNow(juce::Time::getCurrentTime()); }
    void loadState();
    void saveState(juce::Time now);

    MusicLibrary& library;
    AdvertisingManager& ads;
    Playlist& playlist;
    Deck& deckA;
    Deck& deckB;
    std::set<juce::String> done;  // tandas ya insertadas (en la base: sobrevive a un reinicio)
    juce::Random random;
};
