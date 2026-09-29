#include "OnlineIdentifier.h"
#include "Chromaprint.h"
#include "OnlineMetadata.h"

OnlineIdentifier::OnlineIdentifier(MusicLibrary& lib, Transport t)
    : juce::Thread("Online Identifier"), library(lib), transport(t ? std::move(t) : Transport(&HttpTransport::send))
{
    formats.registerBasicFormats();
    startThread(juce::Thread::Priority::low);
}

OnlineIdentifier::~OnlineIdentifier() {
    signalThreadShouldExit();
    notify();
    stopThread(20000);  // un pedido en curso termina por su tiempo de espera
}

//==============================================================================
// Ajustes

bool OnlineIdentifier::isEnabled() {
    return library.withDatabase([](Database& db) { return db.getSetting("online.enabled") == "1"; });
}

void OnlineIdentifier::setEnabled(bool enabled) {
    library.withDatabase([enabled](Database& db) { return db.setSetting("online.enabled", enabled ? "1" : "0"); });
    if (enabled) enqueuePending();
    sendChangeMessage();
}

juce::String OnlineIdentifier::getApiKey() {
    return juce::String::fromUTF8(library.withDatabase([](Database& db) { return db.getSetting("online.acoustidKey"); }).c_str());
}

void OnlineIdentifier::setApiKey(const juce::String& key) {
    const auto value = key.trim().toStdString();
    library.withDatabase([&value](Database& db) { return db.setSetting("online.acoustidKey", value); });
    if (isEnabled()) enqueuePending();
    sendChangeMessage();
}

bool OnlineIdentifier::isChromaprintAvailable() {
    return Chromaprint::isAvailable();
}

bool OnlineIdentifier::canSearch() {
    return getApiKey().isNotEmpty() && isChromaprintAvailable();
}

//==============================================================================
// Fila

void OnlineIdentifier::enqueuePending() {
    if (!isEnabled() || !canSearch()) return;
    const auto ids = library.withDatabase([](Database& db) { return db.getTracksNeedingOnlineLookup(); });
    {
        const juce::ScopedLock sl(lock);
        for (int id : ids)
            if (std::find(queue.begin(), queue.end(), id) == queue.end()) queue.push_back(id);
        status.queued = (int) queue.size();
    }
    notify();
}

void OnlineIdentifier::enqueue(const std::vector<int>& trackIds) {
    {
        const juce::ScopedLock sl(lock);
        for (auto it = trackIds.rbegin(); it != trackIds.rend(); ++it) {
            const auto found = std::find(queue.begin(), queue.end(), *it);
            if (found != queue.end()) queue.erase(found);
            queue.push_front(*it);
        }
        status.queued = (int) queue.size();
    }
    notify();
    sendChangeMessage();
}

OnlineIdentifier::Status OnlineIdentifier::getStatus() const {
    const juce::ScopedLock sl(lock);
    return status;
}

void OnlineIdentifier::run() {
    // Al abrir, lo que quedó pendiente de otras sesiones.
    enqueuePending();

    while (!threadShouldExit()) {
        int id = -1;
        {
            const juce::ScopedLock sl(lock);
            if (!queue.empty()) {
                id = queue.front();
                queue.pop_front();
            }
            status.queued = (int) queue.size();
            status.working = id != -1;
        }
        if (id == -1) {
            sendChangeMessage();
            wait(-1);
            continue;
        }
        identify(id);
        sendChangeMessage();
    }
}

//==============================================================================
// Una pista

void OnlineIdentifier::waitTurn(juce::uint32& last, int intervalMs) {
    const auto now = juce::Time::getMillisecondCounter();
    if (last != 0 && now - last < (juce::uint32) intervalMs) wait(intervalMs - (int) (now - last));
    last = juce::Time::getMillisecondCounter();
}

OnlineIdentifier::Response OnlineIdentifier::send(Request request) {
    request.headers = "User-Agent: " + OnlineMetadata::userAgent() + (request.headers.isNotEmpty() ? "\r\n" + request.headers : juce::String());
    return transport(request);
}

OnlineIdentifier::Outcome OnlineIdentifier::save(int trackId, const Database::OnlineMatch& match) {
    library.withDatabase([&](Database& db) { return db.saveOnlineMatch(trackId, match); });
    const juce::ScopedLock sl(lock);
    switch (match.status) {
        case Database::OnlineStatus::Found:    ++status.found; return Outcome::Found;
        case Database::OnlineStatus::NotFound: ++status.notFound; return Outcome::NotFound;
        default:
            ++status.errors;
            status.lastError = juce::String::fromUTF8(match.error.c_str());
            return Outcome::Error;
    }
}

OnlineIdentifier::Outcome OnlineIdentifier::identify(int trackId) {
    const auto key = getApiKey();
    if (key.isEmpty() || !isChromaprintAvailable()) return Outcome::Skipped;

    const auto track = library.getTrackById(trackId);
    const juce::File file(juce::String::fromUTF8(track.path.c_str()));
    if (track.id == -1 || !file.existsAsFile()) return Outcome::Skipped;  // se intenta en otra sesión

    Database::OnlineMatch failed;
    failed.status = Database::OnlineStatus::Error;

    // 1. La huella de los dos primeros minutos.
    Chromaprint::Result print;
    if (std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file)); reader != nullptr)
        print = Chromaprint::compute(*reader, [this] { return threadShouldExit(); });
    if (threadShouldExit()) return Outcome::Skipped;
    if (print.fingerprint.isEmpty()) {
        failed.error = "fingerprint";
        return save(trackId, failed);
    }

    // 2. AcoustID: qué grabación es.
    waitTurn(lastAcoustId, 350);
    const auto lookup = send({ OnlineMetadata::acoustIdUrl(), OnlineMetadata::acoustIdBody(key, print.fingerprint, print.duration),
                               "Content-Type: application/x-www-form-urlencoded" });
    if (lookup.status == 0) {
        failed.error = "offline";
        return save(trackId, failed);
    }
    juce::String error;
    const auto candidates = OnlineMetadata::parseAcoustId(lookup.body, error);
    if (error.isNotEmpty()) {
        failed.error = error.toStdString();
        return save(trackId, failed);
    }
    const auto* best = OnlineMetadata::best(candidates, track.duration);
    if (best == nullptr || best->score < minimumScore) {
        Database::OnlineMatch none;
        none.status = Database::OnlineStatus::NotFound;
        return save(trackId, none);
    }

    // 3. MusicBrainz: sus datos completos. Si no responde, alcanza con lo de AcoustID.
    auto match = OnlineMetadata::fromCandidate(*best);
    waitTurn(lastMusicBrainz, 1100);
    const auto recording = send({ OnlineMetadata::recordingUrl(best->recordingId), {}, "Accept: application/json" });
    if (recording.status == 200) {
        auto detailed = match;
        if (OnlineMetadata::parseRecording(recording.body, detailed)) {
            detailed.status = Database::OnlineStatus::Found;
            detailed.score = match.score;
            match = detailed;
        }
    }
    return save(trackId, match);
}
