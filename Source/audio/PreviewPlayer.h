#pragma once
#include <JuceHeader.h>
#include "../database/Database.h"

// Preescucha de la revisión y de los audios temporales: suena solo por la salida de cue (PFL),
// nunca al aire. Una canción empieza en el primer tercio (donde ya se oye la voz, para confirmar
// que es la que dice su nombre); un mensaje, desde su punto de entrada. Con la ganancia del
// análisis, como sonaría en el deck.
class PreviewPlayer : public juce::AudioSource {
public:
    explicit PreviewPlayer(juce::AudioFormatManager& formats);
    ~PreviewPlayer() override;

    // false si el archivo no se puede leer.
    bool play(const TrackInfo& track, bool fromCueIn = false);
    void stop();
    bool isPlaying() const;
    int getTrackId() const { return trackId.load(); }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

private:
    juce::AudioFormatManager& formats;
    juce::TimeSliceThread readAhead { "Preview read-ahead" };
    std::unique_ptr<juce::AudioFormatReaderSource> reader;
    juce::AudioTransportSource transport;
    std::atomic<int> trackId { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreviewPlayer)
};
