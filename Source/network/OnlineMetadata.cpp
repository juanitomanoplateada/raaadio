#include "OnlineMetadata.h"

namespace OnlineMetadata {

namespace {
    std::string text(const juce::var& v) { return v.toString().toStdString(); }

    void addUnique(std::vector<std::string>& list, const std::string& name) {
        if (!name.empty() && std::find(list.begin(), list.end(), name) == list.end()) list.push_back(name);
    }

    // Compositores y autores de la letra de una lista de relaciones de MusicBrainz.
    void collectWriters(const juce::var& relations, std::vector<std::string>& composers) {
        if (const auto* list = relations.getArray()) {
            for (const auto& relation : *list) {
                const auto type = relation["type"].toString();
                if (type == "composer" || type == "lyricist" || type == "writer")
                    addUnique(composers, text(relation["artist"]["name"]));
            }
        }
    }
}

juce::String acoustIdUrl() {
    return "https://api.acoustid.org/v2/lookup";
}

juce::String acoustIdBody(const juce::String& apiKey, const juce::String& fingerprint, int durationSeconds) {
    return "client="+ juce::URL::addEscapeChars(apiKey.trim(), true) + "&meta=recordings&duration="
         + juce::String(durationSeconds) + "&fingerprint=" + juce::URL::addEscapeChars(fingerprint, true);
}

std::vector<Candidate> parseAcoustId(const juce::String& json, juce::String& error) {
    std::vector<Candidate> candidates;
    error = {};
    const auto root = juce::JSON::parse(json);
    if (!root.isObject()) {
        error = "invalid response";
        return candidates;
    }
    if (root["status"].toString() != "ok") {
        error = root["error"]["message"].toString();
        if (error.isEmpty()) error = "error";
        return candidates;
    }
    if (const auto* results = root["results"].getArray()) {
        for (const auto& result : *results) {
            const double score = (double) result.getProperty("score", 0.0);
            if (const auto* recordings = result["recordings"].getArray()) {
                for (const auto& recording : *recordings) {
                    Candidate c;
                    c.score = score;
                    c.recordingId = text(recording["id"]);
                    c.title = text(recording["title"]);
                    c.duration = (double) recording.getProperty("duration", 0.0);
                    if (const auto* artists = recording["artists"].getArray())
                        for (const auto& artist : *artists) c.artists.push_back(text(artist["name"]));
                    if (!c.recordingId.empty()) candidates.push_back(std::move(c));
                }
            }
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
    return candidates;
}

const Candidate* best(const std::vector<Candidate>& candidates, double durationSeconds) {
    const Candidate* chosen = nullptr;
    for (const auto& c : candidates) {
        if (c.title.empty()) continue;  // grabación sin datos en MusicBrainz
        if (chosen == nullptr || c.score > chosen->score + 0.02) {
            chosen = &c;
            continue;
        }
        // Puntajes casi iguales: la que dura más parecido al archivo.
        if (c.score >= chosen->score - 0.02 && c.duration > 0.0
            && (chosen->duration <= 0.0 || std::abs(c.duration - durationSeconds) < std::abs(chosen->duration - durationSeconds)))
            chosen = &c;
    }
    return chosen;
}

juce::String recordingUrl(const std::string& recordingId) {
    return "https://musicbrainz.org/ws/2/recording/" + juce::URL::addEscapeChars(juce::String(recordingId), false)
         + "?inc=artist-credits+isrcs+artist-rels+work-rels+work-level-rels&fmt=json";
}

juce::String userAgent() {
    return "RAAADIO/" + juce::String(ProjectInfo::versionString) + " ( https://github.com/juanitomanoplateada/raaadio )";
}

bool parseRecording(const juce::String& json, Database::OnlineMatch& match) {
    const auto root = juce::JSON::parse(json);
    if (!root.isObject() || root["title"].toString().isEmpty()) return false;

    match.title = text(root["title"]);
    if (root.hasProperty("id")) match.recordingId = text(root["id"]);

    // Crédito: el primero es el intérprete principal; los demás, invitados.
    match.artist.clear();
    match.featured.clear();
    if (const auto* credits = root["artist-credit"].getArray()) {
        for (const auto& credit : *credits) {
            auto name = text(credit["name"]);
            if (name.empty()) name = text(credit["artist"]["name"]);
            if (match.artist.empty()) match.artist = name;
            else addUnique(match.featured, name);
        }
    }

    const auto date = root["first-release-date"].toString();
    match.year = date.length() >= 4 ? date.substring(0, 4).getIntValue() : 0;

    match.isrc.clear();
    if (const auto* isrcs = root["isrcs"].getArray())
        if (!isrcs->isEmpty()) match.isrc = text((*isrcs)[0]);

    // Compositores: de la obra que interpreta la grabación (o de la grabación misma).
    match.composers.clear();
    collectWriters(root["relations"], match.composers);
    if (const auto* relations = root["relations"].getArray())
        for (const auto& relation : *relations)
            if (relation["type"].toString() == "performance") collectWriters(relation["work"]["relations"], match.composers);
    return !match.artist.empty();
}

Database::OnlineMatch fromCandidate(const Candidate& candidate) {
    Database::OnlineMatch m;
    m.status = Database::OnlineStatus::Found;
    m.score = candidate.score;
    m.recordingId = candidate.recordingId;
    m.title = candidate.title;
    if (!candidate.artists.empty()) m.artist = candidate.artists.front();
    for (size_t i = 1; i < candidate.artists.size(); ++i) m.featured.push_back(candidate.artists[i]);
    return m;
}

TrackNaming::Proposal toProposal(const Database::OnlineMatch& match, const TrackNaming::Context& context) {
    // El título pasa por lo mismo que unas etiquetas: se separa la versión ("(En vivo)") y
    // algún invitado escrito en él.
    TrackNaming::Source source;
    source.tagArtist = juce::String::fromUTF8(match.artist.c_str());
    source.tagTitle = juce::String::fromUTF8(match.title.c_str());
    auto p = TrackNaming::propose(source, TrackNaming::Orientation::ArtistTitle, context);

    // El artista de MusicBrainz es uno solo aunque su nombre lleve "y" ("Sandy y Papo"): no se
    // separa. Los invitados vienen aparte en el crédito.
    if (p.featuredGuessed) p.featured.clear();
    const auto* knownArtist = context.findKnown(TrackNaming::key(source.tagArtist));
    p.artist = knownArtist != nullptr ? knownArtist->name : source.tagArtist.trim();

    for (const auto& f : match.featured) {
        const auto name = juce::String::fromUTF8(f.c_str());
        const auto* known = context.findKnown(TrackNaming::key(name));
        p.featured.addIfNotAlreadyThere(known != nullptr ? known->name : name);
    }
    p.composers.clear();
    for (const auto& c : match.composers) {
        const auto name = juce::String::fromUTF8(c.c_str());
        const auto* known = context.findKnown(TrackNaming::key(name));
        p.composers.addIfNotAlreadyThere(known != nullptr ? known->name : name);
    }
    p.year = match.year;
    p.featuredGuessed = false;
    p.joinedArtist = {};
    return p;
}

}
