#include "PreviewPlayer.h"

PreviewPlayer::PreviewPlayer(juce::AudioFormatManager& f) : formats(f) {
    readAhead.startThread(juce::Thread::Priority::low);
}

PreviewPlayer::~PreviewPlayer() {
    transport.setSource(nullptr);
    reader.reset();
    readAhead.stopThread(2000);
}

bool PreviewPlayer::play(const TrackInfo& track, bool fromCueIn) {
    stop();

    auto* formatReader = formats.createReaderFor(juce::File(juce::String::fromUTF8(track.path.c_str())));
    if (formatReader == nullptr) return false;

    const double sourceRate = formatReader->sampleRate;
    auto newReader = std::make_unique<juce::AudioFormatReaderSource>(formatReader, true);
    transport.setSource(newReader.get(), 32768, &readAhead, sourceRate, 2);
    reader = std::move(newReader);

    // Desde el primer tercio de lo que suena al aire (o desde el punto de entrada).
    const double length = transport.getLengthInSeconds();
    const double cueIn = juce::jlimit(0.0, length, track.cueIn);
    const double end = track.cueOut > cueIn && track.cueOut < length ? track.cueOut : length;
    transport.setPosition(fromCueIn ? cueIn : cueIn + (end - cueIn) / 3.0);
    transport.setGain(track.normalizationGain > 0.0 ? (float) track.normalizationGain : 1.0f);
    transport.start();
    trackId = track.id;
    return true;
}

void PreviewPlayer::stop() {
    transport.stop();
    transport.setSource(nullptr);
    reader.reset();
    trackId = -1;
}

bool PreviewPlayer::isPlaying() const {
    return transport.isPlaying();
}

void PreviewPlayer::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    transport.prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void PreviewPlayer::releaseResources() {
    transport.releaseResources();
}

void PreviewPlayer::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    transport.getNextAudioBlock(bufferToFill);
}
