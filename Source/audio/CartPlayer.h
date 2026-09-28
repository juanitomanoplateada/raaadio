#pragma once
#include <JuceHeader.h>
#include "../database/Database.h"

class MusicLibrary;

// Botonera de jingles (cartwall): 12 cartuchos que suenan directo al aire con F1-F12 o
// un clic, por encima de lo que esté sonando. Cada uno usa la ganancia y los puntos de
// entrada/salida del análisis de su pista. Lo asignado se guarda en la base de datos (con
// rememberAssignments en false, no se lee ni se guarda: una botonera de prueba).
class CartPlayer : public juce::AudioSource, public juce::ChangeBroadcaster, private juce::Timer {
public:
    static constexpr int numCarts = 12;

    CartPlayer(MusicLibrary& library, juce::AudioFormatManager& formats, bool rememberAssignments = true);
    ~CartPlayer() override;

    bool assign(int slot, const TrackInfo& track);  // false si el archivo no se puede leer
    // Asigna una pista de la biblioteca; si le falta el análisis (ganancia, puntos de
    // entrada y salida), lo hace antes. Las cortinillas son cortas: tarda poco.
    bool assignFromLibrary(int slot, TrackInfo track);
    void clear(int slot);
    void clearAll();
    TrackInfo getTrack(int slot) const;
    bool isAssigned(int slot) const;

    void toggle(int slot);  // suena desde el punto de entrada, o se detiene si ya sonaba
    void stopAll();
    bool isPlaying(int slot) const;
    double getRemainingSeconds(int slot) const;
    double getProgress(int slot) const;  // 0..1

    std::function<void(const TrackInfo&)> onCartPlayed;  // para el historial

    // Como en los decks, cada cartucho se lee en otro hilo por adelantado. Espera, hasta
    // timeoutMs, a que los que suenan tengan las próximas numSamples muestras en memoria.
    bool waitUntilBuffered(int numSamples, int timeoutMs);

    // AudioSource (el mezclador lo suma a la salida al aire)
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

private:
    struct Cart {
        TrackInfo track;
        std::unique_ptr<juce::AudioFormatReaderSource> reader;
        std::unique_ptr<juce::BufferingAudioSource> buffered;  // lee reader por adelantado
        juce::AudioTransportSource transport;
        double cueIn = 0.0, cueOut = 0.0;
    };

    void timerCallback() override;
    void saveState() const;
    void loadState();
    bool validSlot(int slot) const { return slot >= 0 && slot < numCarts; }
    bool waitForCart(Cart& cart, int numSamples, int timeoutMs);

    MusicLibrary& library;
    juce::AudioFormatManager& formats;
    const bool rememberAssignments;
    juce::TimeSliceThread readAheadThread { "Cartwall read-ahead" };
    std::atomic<bool> prepared { false };
    std::array<Cart, numCarts> carts;
    juce::MixerAudioSource mixer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CartPlayer)
};
