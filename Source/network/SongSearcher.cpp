#include "SongSearcher.h"
#include "OnlineMetadata.h"

SongSearcher::SongSearcher(HttpTransport::Transport t, const juce::String& lang)
    : juce::Thread("Song Searcher"), transport(t ? std::move(t) : HttpTransport::Transport(&HttpTransport::send)),
      language(lang.isNotEmpty() ? lang : juce::String("es"))
{
    startThread(juce::Thread::Priority::low);
}

SongSearcher::~SongSearcher() {
    signalThreadShouldExit();
    notify();
    stopThread(20000);  // un pedido en curso termina por su tiempo de espera
}

void SongSearcher::search(const juce::String& query) {
    {
        const juce::ScopedLock sl(lock);
        pendingQuery = query.trim();
        results.working = true;
    }
    notify();
    sendChangeMessage();
}

SongSearcher::Results SongSearcher::getResults() const {
    const juce::ScopedLock sl(lock);
    return results;
}

void SongSearcher::resolve(const SongSearch::Track& track) {
    {
        const juce::ScopedLock sl(lock);
        const auto it = links.find(track.id);
        if (it != links.end() && it->second.state != Links::State::Error) return;
        Links pending;
        pending.track = track;
        links[track.id] = pending;
        pendingTrack = track;  // la última elegida: las anteriores que esperaban ya no importan
    }
    notify();
    sendChangeMessage();
}

bool SongSearcher::getLinks(juce::int64 trackId, Links& out) const {
    const juce::ScopedLock sl(lock);
    const auto it = links.find(trackId);
    if (it == links.end()) return false;
    out = it->second;
    return true;
}

void SongSearcher::run() {
    while (!threadShouldExit()) {
        std::optional<juce::String> query;
        std::optional<SongSearch::Track> track;
        {
            const juce::ScopedLock sl(lock);
            // Primero la búsqueda: lo que se ve en la lista importa más que el detalle.
            if (pendingQuery.has_value()) std::swap(query, pendingQuery);
            else if (pendingTrack.has_value()) std::swap(track, pendingTrack);
        }
        if (query.has_value()) runSearch(*query);
        else if (track.has_value()) runResolve(*track);
        else wait(-1);
    }
}

HttpTransport::Response SongSearcher::send(HttpTransport::Request request) {
    request.headers = "User-Agent: " + OnlineMetadata::userAgent() + (request.headers.isNotEmpty() ? "\r\n" + request.headers : juce::String());
    return transport(request);
}

juce::String SongSearcher::describe(const HttpTransport::Response& response) {
    return response.status == 0 ? juce::String("offline") : "http " + juce::String(response.status);
}

void SongSearcher::runSearch(const juce::String& query) {
    Results found;
    found.query = query;
    if (query.isNotEmpty()) {
        const auto response = send({ SongSearch::deezerSearchUrl(query), {}, "Accept: application/json" });
        if (response.status != 200) {
            found.error = describe(response);
        } else {
            juce::String error;
            found.tracks = SongSearch::parseDeezerSearch(response.body, error);
            found.error = error;
        }
    }

    {
        const juce::ScopedLock sl(lock);
        found.generation = results.generation + 1;
        found.working = pendingQuery.has_value();  // ya hay otra en espera
        results = std::move(found);
    }
    sendChangeMessage();
}

bool SongSearcher::askYouTube(const HttpTransport::Request& request, bool music, std::vector<SongSearch::Video>& videos,
                              juce::String& error) {
    if (youTubeBlockedUntil != 0 && juce::Time::getMillisecondCounter() < youTubeBlockedUntil) {
        error = "youtube_wait";
        return false;
    }

    // Pausa desde el pedido anterior (notify() despierta antes: se vuelve a esperar lo que falta).
    const int interval = youTubeInterval.load();
    while (lastYouTube != 0 && !threadShouldExit()) {
        const auto elapsed = (int) (juce::Time::getMillisecondCounter() - lastYouTube);
        if (elapsed >= interval) break;
        wait(interval - elapsed);
    }
    if (threadShouldExit()) return false;

    const auto response = send(request);
    lastYouTube = juce::Time::getMillisecondCounter();
    if (response.status == 429) {
        youTubeBlockedUntil = lastYouTube + 15 * 60 * 1000;
        error = "youtube_wait";
        return false;
    }
    if (response.status != 200) {
        error = describe(response);
        return false;
    }
    auto found = music ? SongSearch::parseYouTubeMusicSearch(response.body) : SongSearch::parseYouTubeSearch(response.body);
    videos.insert(videos.end(), found.begin(), found.end());
    return true;
}

void SongSearcher::runResolve(SongSearch::Track track) {
    Links result;

    // 1. Deezer: año e invitados (si no responde, alcanza con lo de la búsqueda).
    const auto detail = send({ SongSearch::deezerTrackUrl(track.id), {}, "Accept: application/json" });
    if (detail.status == 200) SongSearch::parseDeezerTrack(detail.body, track);

    // 2. La carátula.
    if (track.coverUrl.isNotEmpty() && !threadShouldExit()) {
        HttpTransport::Request coverRequest { track.coverUrl, {}, {} };
        coverRequest.binary = true;
        const auto cover = send(coverRequest);
        if (cover.status == 200 && cover.data.getSize() > 0)
            result.cover = juce::ImageFileFormat::loadFrom(cover.data.getData(), cover.data.getSize());
    }

    // 3. YouTube Music (el audio oficial de las disqueras); si allá no coincide, YouTube.
    const auto query = SongSearch::youTubeQuery(track);
    std::vector<SongSearch::Video> videos;
    juce::String error;
    const bool musicOk = askYouTube(SongSearch::youTubeMusicSearch(query, language), true, videos, error);
    SongSearch::rank(track, videos);
    if ((videos.empty() || videos.front().score < SongSearch::goodMatch) && error != "youtube_wait" && !threadShouldExit()) {
        juce::String webError;
        if (askYouTube(SongSearch::youTubeSearch(query, language), false, videos, webError)) error = {};
        else if (!musicOk || webError == "youtube_wait") error = webError;
        SongSearch::rank(track, videos);
    }
    if (videos.size() > (size_t) maxVideos) videos.resize((size_t) maxVideos);

    result.track = track;
    result.videos = std::move(videos);
    result.error = error;
    result.state = result.videos.empty() && error.isNotEmpty() ? Links::State::Error : Links::State::Done;
    {
        const juce::ScopedLock sl(lock);
        links[track.id] = result;
    }
    sendChangeMessage();
}
