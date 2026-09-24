#include "NowPlayingPublisher.h"
#include "StreamEncoder.h"
#include "ApiServer.h"
#include "../audio/Deck.h"
#include "../autodj/OnAir.h"
#include "../autodj/QueueTiming.h"
#include "../library/Playlist.h"
#include "../library/MusicLibrary.h"

namespace {
    constexpr int heartbeatMs = 60 * 1000;
    constexpr int upcomingLookahead = 30;  // filas de la cola que se miran para hallar las siguientes canciones
    constexpr int historyLookahead = 40;
}

//==============================================================================
// Envía el JSON más reciente; si llegan varios mientras uno está en camino, solo sale el
// último. Nunca más de uno por segundo.
class NowPlayingPublisher::Poster : private juce::Thread {
public:
    Poster() : juce::Thread("Now Playing Push") { startThread(); }

    ~Poster() override {
        signalThreadShouldExit();
        {
            const juce::ScopedLock sl(lock);
            if (active != nullptr) active->cancel();
        }
        notify();
        stopThread(4000);
    }

    void post(const juce::String& url, const juce::String& json) {
        {
            const juce::ScopedLock sl(lock);
            pendingUrl = url;
            pendingJson = json;
            hasPending = true;
        }
        notify();
    }

    PushStatus getStatus() const {
        const juce::ScopedLock sl(lock);
        return status;
    }

private:
    void run() override {
        while (!threadShouldExit()) {
            juce::String url, json;
            {
                const juce::ScopedLock sl(lock);
                if (hasPending) {
                    url = pendingUrl;
                    json = pendingJson;
                    hasPending = false;
                }
            }
            if (url.isEmpty()) {
                wait(-1);
                continue;
            }
            send(url, json);
            wait(1000);
        }
    }

    void send(const juce::String& url, const juce::String& json) {
        juce::WebInputStream stream(juce::URL(url).withPOSTData(json), true);
        stream.withExtraHeaders("Content-Type: application/json; charset=utf-8\r\nUser-Agent: RAAADIO/"
                                + juce::String(ProjectInfo::versionString))
              .withConnectionTimeout(10000)
              .withNumRedirectsToFollow(3);
        {
            const juce::ScopedLock sl(lock);
            active = &stream;
        }
        const bool connected = !threadShouldExit() && stream.connect(nullptr);
        const int code = connected ? stream.getStatusCode() : 0;
        {
            const juce::ScopedLock sl(lock);
            active = nullptr;
            status.attempted = true;
            status.ok = code >= 200 && code < 300;
            status.httpStatus = code;
            status.time = juce::Time::getCurrentTime();
        }
        if (code < 200 || code >= 300)
            juce::Logger::writeToLog("NowPlaying: push to " + url + " failed (HTTP " + juce::String(code) + ")");
    }

    mutable juce::CriticalSection lock;
    juce::String pendingUrl, pendingJson;
    bool hasPending = false;
    juce::WebInputStream* active = nullptr;
    PushStatus status;
};

//==============================================================================
NowPlayingPublisher::NowPlayingPublisher(Deck& a, Deck& b, Playlist& p, HistoryManager& h, MusicLibrary& l,
                                         StreamEncoder& s, ApiServer& api)
    : deckA(a), deckB(b), playlist(p), history(h), library(l), streamEncoder(s), apiServer(api),
      poster(std::make_unique<Poster>())
{
    history.addChangeListener(this);
    rebuild();
    startTimer(1000);
}

NowPlayingPublisher::~NowPlayingPublisher() {
    stopTimer();
    cancelPendingUpdate();
    history.removeChangeListener(this);
}

void NowPlayingPublisher::setStationName(const juce::String& name) {
    const auto trimmed = name.trim();
    const auto newName = trimmed.isNotEmpty() ? trimmed : juce::String("RAAADIO");
    if (newName == stationName) return;
    stationName = newName;
    refresh();
}

void NowPlayingPublisher::setPushUrl(const juce::String& url) {
    const auto newUrl = isValidPushUrl(url.trim()) ? url.trim() : juce::String();
    if (newUrl == pushUrl) return;
    pushUrl = newUrl;
    refresh();
}

bool NowPlayingPublisher::isValidPushUrl(const juce::String& url) {
    return (url.startsWithIgnoreCase("http://") || url.startsWithIgnoreCase("https://")) && url.length() > 10
           && !url.containsAnyOf(" \r\n\t");
}

void NowPlayingPublisher::refresh() {
    triggerAsyncUpdate();
}

NowPlayingPublisher::PushStatus NowPlayingPublisher::getPushStatus() const {
    return poster->getStatus();
}

juce::String NowPlayingPublisher::signature() const {
    // Lo que, si cambia, cambia lo publicado: lo cargado y lo que suena en cada deck, la
    // cola y el historial (que avisa por su cuenta).
    juce::String s;
    for (const Deck* deck : { &deckA, &deckB }) {
        s << (deck->isLoaded() ? juce::String(deck->getCurrentTrack().path) : juce::String()) << '|'
          << (int) deck->isPlaying() << (int) deck->isRoutedToMain() << (int) deck->hasFinished() << '|';
    }
    s << (juce::int64) playlist.getLastModified();
    return s;
}

void NowPlayingPublisher::timerCallback() {
    const bool heartbeat = pushUrl.isNotEmpty() && juce::Time::getMillisecondCounter() - lastRebuild >= (juce::uint32) heartbeatMs;
    if (heartbeat || signature() != lastSignature) rebuild();
}

void NowPlayingPublisher::rebuild() {
    lastSignature = signature();
    lastRebuild = juce::Time::getMillisecondCounter();

    NowPlaying::Inputs in;
    in.station = stationName;
    in.now = juce::Time::getCurrentTime();

    if (const auto* deck = OnAir::nowPlaying(deckA, deckB)) {
        const double speed = deck->getSpeed() > 0.01 ? deck->getSpeed() : 1.0;
        const double elapsed = juce::jmax(0.0, deck->getCurrentPosition() - deck->getCueIn()) / speed;
        in.onAir = NowPlaying::Airing { deck->getCurrentTrack(), in.now - juce::RelativeTime(elapsed),
                                        QueueTiming::secondsUntilNext(*deck) };
    }

    // Lo que viene: primero un deck cargado y detenido (sale antes que la cola), después
    // la cola, con las horas que calcula la cola misma.
    const double untilQueue = QueueTiming::secondsUntilQueueStarts(deckA, deckB);
    double untilDeck = -1.0;
    if (deckA.isPlaying() || deckB.isPlaying())
        untilDeck = juce::jmax(deckA.isPlaying() ? QueueTiming::secondsUntilNext(deckA) : 0.0,
                               deckB.isPlaying() ? QueueTiming::secondsUntilNext(deckB) : 0.0);
    for (Deck* deck : { &deckA, &deckB }) {
        if (deck->isLoaded() && !deck->isPlaying() && !deck->hasFinished()) {
            in.upcoming.push_back({ deck->getCurrentTrack(), untilDeck });
            if (untilDeck >= 0.0) untilDeck += QueueTiming::secondsUntilNext(*deck);
        }
    }
    auto queue = playlist.getQueue();
    if ((int) queue.size() > upcomingLookahead) queue.resize((size_t) upcomingLookahead);
    const auto offsets = QueueTiming::startOffsets(queue);
    for (size_t i = 0; i < queue.size(); ++i)
        in.upcoming.push_back({ queue[i], untilQueue >= 0.0 ? untilQueue + offsets[i] : -1.0 });

    in.history = history.getHistory();
    if ((int) in.history.size() > historyLookahead) in.history.resize((size_t) historyLookahead);

    // Una consulta: las pistas de identificaciones, cuñas y separadores no son música.
    const auto roles = library.getSystemRolesByTrack();
    in.isMusic = [&roles](const TrackInfo& track) { return track.id == -1 || roles.find(track.id) == roles.end(); };

    const auto snapshot = NowPlaying::build(in);
    const auto json = NowPlaying::toJson(snapshot, in.now);

    streamEncoder.setNowPlaying(snapshot.streamArtist(), snapshot.streamTitle());
    apiServer.update(StreamEncoder::songText(snapshot.streamArtist(), snapshot.streamTitle()), json);
    if (pushUrl.isNotEmpty()) poster->post(pushUrl, json);
}
