#include "CartPlayer.h"
#include "../library/MusicLibrary.h"
#include "Deck.h"

CartPlayer::CartPlayer(MusicLibrary& lib, juce::AudioFormatManager& fm, juce::File file)
    : library(lib), formats(fm), stateFile(std::move(file))
{
    readAheadThread.startThread(juce::Thread::Priority::high);
    for (auto& cart : carts)
        mixer.addInputSource(&cart.transport, false);
    loadState();
}

CartPlayer::~CartPlayer() {
    stopTimer();
    mixer.removeAllInputs();
    for (auto& cart : carts) {
        cart.transport.setSource(nullptr);
        cart.buffered.reset();
        cart.reader.reset();
    }
    readAheadThread.stopThread(2000);
}

bool CartPlayer::assign(int slot, const TrackInfo& track) {
    if (!validSlot(slot)) return false;

    auto* reader = formats.createReaderFor(juce::File(track.path));
    if (reader == nullptr) return false;

    auto& cart = carts[(size_t) slot];
    cart.transport.stop();
    auto source = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
    auto buffered = std::make_unique<juce::BufferingAudioSource>(source.get(), readAheadThread, false,
                                                                 Deck::readAheadSamples, 2);
    cart.transport.setSource(buffered.get(), 0, nullptr, reader->sampleRate);
    cart.transport.setGain(track.normalizationGain > 0.0 ? (float) track.normalizationGain : 1.0f);
    cart.buffered = std::move(buffered);  // la vieja usa al lector viejo: se suelta primero
    cart.reader = std::move(source);
    cart.track = track;

    const double length = cart.transport.getLengthInSeconds();
    cart.cueIn = juce::jlimit(0.0, length, track.cueIn);
    cart.cueOut = (track.cueOut > cart.cueIn && track.cueOut < length) ? track.cueOut : length;
    cart.transport.setPosition(cart.cueIn);  // el comienzo queda en memoria para el disparo

    saveState();
    sendChangeMessage();
    return true;
}

bool CartPlayer::assignFromLibrary(int slot, TrackInfo track) {
    if (track.id != -1 && MusicLibrary::needsAnalysis(track))
        track = library.ensureAnalysed(track);
    return assign(slot, track);
}

void CartPlayer::clear(int slot) {
    if (!validSlot(slot)) return;
    auto& cart = carts[(size_t) slot];
    cart.transport.stop();
    cart.transport.setSource(nullptr);
    cart.buffered.reset();
    cart.reader.reset();
    cart.track = TrackInfo {};
    cart.track.id = -1;
    saveState();
    sendChangeMessage();
}

void CartPlayer::clearAll() {
    for (int slot = 0; slot < numCarts; ++slot)
        clear(slot);
}

TrackInfo CartPlayer::getTrack(int slot) const {
    return validSlot(slot) ? carts[(size_t) slot].track : TrackInfo {};
}

bool CartPlayer::isAssigned(int slot) const {
    return validSlot(slot) && carts[(size_t) slot].reader != nullptr;
}

void CartPlayer::toggle(int slot) {
    if (!isAssigned(slot)) return;
    auto& cart = carts[(size_t) slot];

    if (cart.transport.isPlaying()) {
        cart.transport.stop();
        cart.transport.setPosition(cart.cueIn);  // listo para el próximo disparo
    } else {
        cart.transport.setPosition(cart.cueIn);
        waitForCart(cart, 2048, 250);
        cart.transport.start();
        if (onCartPlayed) onCartPlayed(cart.track);
        startTimer(50);
    }
    sendChangeMessage();
}

void CartPlayer::stopAll() {
    for (auto& cart : carts) {
        cart.transport.stop();
        cart.transport.setPosition(cart.cueIn);
    }
    sendChangeMessage();
}

bool CartPlayer::waitForCart(Cart& cart, int numSamples, int timeoutMs) {
    if (cart.buffered == nullptr || !prepared) return false;
    const juce::AudioSourceChannelInfo next(nullptr, 0, numSamples);
    return cart.buffered->waitForNextAudioBlockReady(next, (juce::uint32) timeoutMs);
}

bool CartPlayer::waitUntilBuffered(int numSamples, int timeoutMs) {
    bool ready = true;
    for (auto& cart : carts)
        if (cart.transport.isPlaying())
            ready = waitForCart(cart, numSamples, timeoutMs) && ready;
    return ready;
}

bool CartPlayer::isPlaying(int slot) const {
    return isAssigned(slot) && carts[(size_t) slot].transport.isPlaying();
}

double CartPlayer::getRemainingSeconds(int slot) const {
    if (!isAssigned(slot)) return 0.0;
    const auto& cart = carts[(size_t) slot];
    const double from = isPlaying(slot) ? cart.transport.getCurrentPosition() : cart.cueIn;
    return juce::jmax(0.0, cart.cueOut - from);
}

double CartPlayer::getProgress(int slot) const {
    if (!isPlaying(slot)) return 0.0;
    const auto& cart = carts[(size_t) slot];
    const double length = cart.cueOut - cart.cueIn;
    return length > 0.0 ? juce::jlimit(0.0, 1.0, (cart.transport.getCurrentPosition() - cart.cueIn) / length) : 0.0;
}

void CartPlayer::timerCallback() {
    // Cada cartucho termina en su punto de salida (lo que sigue es silencio).
    bool anyPlaying = false;
    for (auto& cart : carts) {
        if (!cart.transport.isPlaying()) continue;
        if (cart.transport.getCurrentPosition() >= cart.cueOut || cart.transport.hasStreamFinished()) {
            cart.transport.stop();
            cart.transport.setPosition(cart.cueIn);
        } else
            anyPlaying = true;
    }
    if (!anyPlaying) stopTimer();
    sendChangeMessage();
}

void CartPlayer::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    mixer.prepareToPlay(samplesPerBlockExpected, sampleRate);
    prepared = true;
}

void CartPlayer::releaseResources() {
    prepared = false;
    mixer.releaseResources();
}

void CartPlayer::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    mixer.getNextAudioBlock(bufferToFill);
}

void CartPlayer::saveState() const {
    if (stateFile == juce::File()) return;
    juce::Array<juce::var> ids;
    for (const auto& cart : carts)
        ids.add(cart.reader != nullptr ? cart.track.id : -1);
    stateFile.getParentDirectory().createDirectory();
    stateFile.replaceWithText(juce::JSON::toString(juce::var(ids)));
}

void CartPlayer::loadState() {
    for (auto& cart : carts) cart.track.id = -1;

    const auto json = juce::JSON::parse(stateFile);
    const auto* ids = json.getArray();
    if (ids == nullptr) return;

    for (int slot = 0; slot < juce::jmin(numCarts, ids->size()); ++slot) {
        const int id = (*ids)[slot];
        if (id < 0) continue;
        const auto track = library.getTrackById(id);
        if (track.id != -1) assign(slot, track);  // una pista borrada deja el cartucho libre
    }
}
