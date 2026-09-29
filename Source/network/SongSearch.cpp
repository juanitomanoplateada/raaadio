#include "SongSearch.h"
#include "../library/TrackNaming.h"
#include <algorithm>
#include <set>

namespace SongSearch {

namespace {
    // Versiones de cliente de la página de YouTube (las mismas que usa yt-dlp). YouTube acepta
    // versiones viejas por mucho tiempo; si un día las rechaza, la búsqueda avisa del error.
    constexpr const char* musicClientVersion = "1.20260707.12.00";
    constexpr const char* webClientVersion = "2.20260708.00.00";
    constexpr const char* musicSongsFilter = "EgWKAQIIAWoKEAkQBRAKEAMQBA==";  // solo canciones
    constexpr const char* webVideosFilter = "EgIQAQ==";                       // solo videos

    juce::String escape(const juce::String& text) { return juce::URL::addEscapeChars(text.trim(), true); }

    // Texto de un campo de YouTube: {"simpleText": "..."} o {"runs": [{"text": "..."}]}.
    juce::String runsText(const juce::var& text) {
        if (text.hasProperty("simpleText")) return text["simpleText"].toString();
        juce::String result;
        if (const auto* runs = text["runs"].getArray())
            for (const auto& run : *runs) result << run["text"].toString();
        return result;
    }

    // Todos los objetos con esa clave, a cualquier profundidad (sin entrar en los encontrados).
    void collect(const juce::var& v, const juce::Identifier& key, juce::Array<juce::var>& found) {
        if (const auto* object = v.getDynamicObject()) {
            for (const auto& property : object->getProperties()) {
                if (property.name == key) found.add(property.value);
                else collect(property.value, key, found);
            }
        } else if (const auto* array = v.getArray()) {
            for (const auto& item : *array) collect(item, key, found);
        }
    }

    Video::Kind kindOf(const juce::String& musicVideoType) {
        if (musicVideoType == "MUSIC_VIDEO_TYPE_ATV") return Video::Kind::OfficialAudio;
        if (musicVideoType == "MUSIC_VIDEO_TYPE_OMV") return Video::Kind::OfficialVideo;
        if (musicVideoType == "MUSIC_VIDEO_TYPE_UGC") return Video::Kind::UserUpload;
        return Video::Kind::Unknown;
    }

    HttpTransport::Request innerTube(const juce::String& host, const juce::String& clientName, int clientNumber,
                                     const juce::String& version, const juce::String& query, const juce::String& params,
                                     const juce::String& language) {
        auto* client = new juce::DynamicObject();
        client->setProperty("clientName", clientName);
        client->setProperty("clientVersion", version);
        client->setProperty("hl", language.isNotEmpty() ? language : juce::String("es"));
        auto* context = new juce::DynamicObject();
        context->setProperty("client", juce::var(client));
        auto* root = new juce::DynamicObject();
        root->setProperty("context", juce::var(context));
        root->setProperty("query", query.trim());
        root->setProperty("params", params);

        HttpTransport::Request r;
        r.url = "https://" + host + "/youtubei/v1/search?prettyPrint=false";
        r.body = juce::JSON::toString(juce::var(root), true);
        r.headers = "Content-Type: application/json\r\nX-YouTube-Client-Name: " + juce::String(clientNumber)
                  + "\r\nX-YouTube-Client-Version: " + version + "\r\nOrigin: https://" + host;
        return r;
    }

    //==========================================================================
    // Comparar

    // Palabras que no distinguen una canción de otra.
    const std::set<juce::String> stopWords = { "el", "la", "los", "las", "lo", "un", "una", "de", "del", "y", "e", "a",
                                               "al", "en", "con", "the", "of", "and", "feat", "ft", "featuring" };
    // Palabras de una versión distinta de la misma canción. Las de otro intérprete (karaoke,
    // cover, homenaje...) pesan más.
    const std::set<juce::String> versionWords = { "vivo", "live", "directo", "concierto", "remix", "acustico", "acustica",
                                                  "acoustic", "unplugged", "sped", "slowed", "8d" };
    const std::set<juce::String> otherPerformerWords = { "karaoke", "instrumental", "cover", "tributo", "homenaje", "tribute" };

    std::set<juce::String> words(const juce::String& text) {
        std::set<juce::String> result;
        for (const auto& w : juce::StringArray::fromTokens(TrackNaming::key(text), " ", ""))
            if (w.isNotEmpty() && stopWords.count(w) == 0) result.insert(w);
        return result;
    }

    // El título sin lo que va entre paréntesis o corchetes ("(En Vivo)", "[Remastered]").
    juce::String withoutBrackets(const juce::String& text) {
        juce::String result;
        int depth = 0;
        for (auto p = text.getCharPointer(); !p.isEmpty(); ++p) {
            const auto c = *p;
            if (c == '(' || c == '[') ++depth;
            else if ((c == ')' || c == ']') && depth > 0) --depth;
            else if (depth == 0) result << juce::String::charToString(c);
        }
        return result.trim().isNotEmpty() ? result.trim() : text.trim();
    }

    int common(const std::set<juce::String>& a, const std::set<juce::String>& b) {
        int n = 0;
        for (const auto& w : a) n += b.count(w) > 0 ? 1 : 0;
        return n;
    }

    // Parecido de dos nombres de artista: 1 si uno contiene al otro ("Juan Luis Guerra" y
    // "Juan Luis Guerra 4.40") o son el mismo mal escrito.
    double artistSimilarity(const juce::String& a, const juce::String& b) {
        const auto keyA = TrackNaming::key(a), keyB = TrackNaming::key(b);
        if (keyA.isEmpty() || keyB.isEmpty()) return 0.0;
        if (keyA == keyB || TrackNaming::similarKeys(keyA, keyB)) return 1.0;
        const auto wa = words(a), wb = words(b);
        if (wa.empty() || wb.empty()) return 0.0;
        return (double) common(wa, wb) / (double) juce::jmin(wa.size(), wb.size());
    }

    double durationSimilarity(int a, int b) {
        if (a <= 0 || b <= 0) return 0.5;
        const int diff = std::abs(a - b);
        return diff <= 2 ? 1.0 : diff <= 6 ? 0.9 : diff <= 12 ? 0.6 : diff <= 25 ? 0.3 : 0.0;
    }

    bool hasAny(const std::set<juce::String>& w, const std::set<juce::String>& list) {
        for (const auto& x : w)
            if (list.count(x) > 0) return true;
        return false;
    }
}

//==============================================================================
// Deezer

juce::String deezerSearchUrl(const juce::String& query, int limit) {
    return "https://api.deezer.com/search/track?q=" + escape(query) + "&limit=" + juce::String(limit);
}

juce::String deezerTrackUrl(juce::int64 id) {
    return "https://api.deezer.com/track/" + juce::String(id);
}

std::vector<Track> parseDeezerSearch(const juce::String& json, juce::String& error) {
    std::vector<Track> tracks;
    error = {};
    const auto root = juce::JSON::parse(json);
    if (!root.isObject()) {
        error = "invalid response";
        return tracks;
    }
    if (root["error"].isObject()) {
        error = root["error"]["message"].toString();
        if (error.isEmpty()) error = "error";
        return tracks;
    }
    if (const auto* data = root["data"].getArray()) {
        for (const auto& item : *data) {
            if (item["type"].toString().isNotEmpty() && item["type"].toString() != "track") continue;
            Track t;
            t.id = (juce::int64) item["id"];
            t.title = item["title"].toString().trim();
            t.titleShort = item["title_short"].toString().trim();
            if (t.titleShort.isEmpty()) t.titleShort = t.title;
            t.version = item["title_version"].toString().trim();
            t.artist = item["artist"]["name"].toString().trim();
            t.album = item["album"]["title"].toString().trim();
            t.coverUrl = item["album"]["cover_medium"].toString();
            t.isrc = item["isrc"].toString();
            t.link = item["link"].toString();
            t.duration = (int) item["duration"];
            t.rank = (int) item["rank"];
            t.explicitLyrics = (bool) item["explicit_lyrics"];
            if (t.id != 0 && t.title.isNotEmpty()) tracks.push_back(std::move(t));
        }
    }
    return tracks;
}

bool parseDeezerTrack(const juce::String& json, Track& track) {
    const auto root = juce::JSON::parse(json);
    if (!root.isObject() || root["error"].isObject() || root["title"].toString().isEmpty()) return false;

    const auto date = root["release_date"].toString();
    if (date.length() >= 4 && date.substring(0, 4).getIntValue() > 1900) track.year = date.substring(0, 4).getIntValue();
    if (track.isrc.isEmpty()) track.isrc = root["isrc"].toString();
    if (track.duration <= 0) track.duration = (int) root["duration"];

    track.featured.clear();
    const auto mainKey = TrackNaming::key(track.artist);
    if (const auto* contributors = root["contributors"].getArray()) {
        for (const auto& c : *contributors) {
            const auto name = c["name"].toString().trim();
            if (name.isNotEmpty() && TrackNaming::key(name) != mainKey && c["role"].toString() != "Main")
                track.featured.addIfNotAlreadyThere(name);
        }
    }
    return true;
}

//==============================================================================
// YouTube

HttpTransport::Request youTubeMusicSearch(const juce::String& query, const juce::String& language) {
    return innerTube("music.youtube.com", "WEB_REMIX", 67, musicClientVersion, query, musicSongsFilter, language);
}

HttpTransport::Request youTubeSearch(const juce::String& query, const juce::String& language) {
    return innerTube("www.youtube.com", "WEB", 1, webClientVersion, query, webVideosFilter, language);
}

int parseDuration(const juce::String& text) {
    const auto parts = juce::StringArray::fromTokens(text.trim(), ":", "");
    if (parts.size() < 2 || parts.size() > 3) return 0;
    int total = 0;
    for (const auto& p : parts) {
        if (p.isEmpty() || !p.containsOnly("0123456789")) return 0;
        total = total * 60 + p.getIntValue();
    }
    return total;
}

std::vector<Video> parseYouTubeMusicSearch(const juce::String& json) {
    std::vector<Video> videos;
    juce::Array<juce::var> items;
    collect(juce::JSON::parse(json), "musicResponsiveListItemRenderer", items);

    for (const auto& item : items) {
        const auto* columns = item["flexColumns"].getArray();
        if (columns == nullptr || columns->isEmpty()) continue;
        auto column = [columns](int i) {
            return i < columns->size() ? (*columns)[i]["musicResponsiveListItemFlexColumnRenderer"]["text"] : juce::var();
        };

        Video v;
        v.fromMusic = true;
        const auto titleText = column(0);
        v.title = runsText(titleText).trim();
        v.id = item["playlistItemData"]["videoId"].toString();
        juce::var watch;
        if (const auto* runs = titleText["runs"].getArray(); runs != nullptr && !runs->isEmpty())
            watch = (*runs)[0]["navigationEndpoint"]["watchEndpoint"];
        if (v.id.isEmpty()) v.id = watch["videoId"].toString();
        if (v.id.isEmpty() || v.title.isEmpty()) continue;  // un álbum o un artista, no una canción
        v.kind = kindOf(watch["watchEndpointMusicSupportedConfigs"]["watchEndpointMusicConfig"]["musicVideoType"].toString());

        // Segunda columna: "Artista y Otro • Álbum • 4:09", en partes separadas por " • ".
        juce::StringArray groups;
        std::vector<juce::String> groupPages;  // tipo de página de la parte (artista, álbum)
        juce::String current, currentPage;
        if (const auto* runs = column(1)["runs"].getArray()) {
            for (const auto& run : *runs) {
                const auto text = run["text"].toString();
                if (text.trim() == juce::String::fromUTF8("\xe2\x80\xa2")) {
                    groups.add(current.trim());
                    groupPages.push_back(currentPage);
                    current = {};
                    currentPage = {};
                    continue;
                }
                current << text;
                const auto page = run["navigationEndpoint"]["browseEndpoint"]["browseEndpointContextSupportedConfigs"]
                                     ["browseEndpointContextMusicConfig"]["pageType"].toString();
                if (page.isNotEmpty() && currentPage.isEmpty()) currentPage = page;
            }
        }
        if (current.trim().isNotEmpty()) {
            groups.add(current.trim());
            groupPages.push_back(currentPage);
        }
        for (int i = 0; i < groups.size(); ++i) {
            const auto& page = groupPages[(size_t) i];
            if (const int seconds = parseDuration(groups[i]); seconds > 0) v.duration = seconds;
            else if (page.contains("ALBUM")) v.album = groups[i];
            else if (v.artists.isEmpty() && (page.contains("ARTIST") || i == 0)) v.artists = groups[i];
        }
        v.views = runsText(column(2)).trim();
        videos.push_back(std::move(v));
    }
    return videos;
}

std::vector<Video> parseYouTubeSearch(const juce::String& json) {
    std::vector<Video> videos;
    juce::Array<juce::var> items;
    collect(juce::JSON::parse(json), "videoRenderer", items);

    for (const auto& item : items) {
        Video v;
        v.id = item["videoId"].toString();
        v.title = runsText(item["title"]).trim();
        if (v.id.isEmpty() || v.title.isEmpty()) continue;
        v.channel = runsText(item["ownerText"]).trim();
        v.artists = v.channel;
        v.duration = parseDuration(runsText(item["lengthText"]));
        v.views = runsText(item["viewCountText"]).trim();

        // Canal "Artista - Topic" (o "- Tema"): el audio que publica la disquera.
        if (v.channel.endsWith(" - Topic") || v.channel.endsWith(" - Tema")) {
            v.kind = Video::Kind::OfficialAudio;
            v.artists = v.channel.upToLastOccurrenceOf(" - ", false, false);
        } else if (const auto* badges = item["ownerBadges"].getArray()) {
            for (const auto& b : *badges)
                if (b["metadataBadgeRenderer"]["style"].toString() == "BADGE_STYLE_TYPE_VERIFIED_ARTIST")
                    v.kind = Video::Kind::OfficialVideo;
        }
        videos.push_back(std::move(v));
    }
    return videos;
}

juce::String youTubeUrl(const juce::String& videoId) { return "https://www.youtube.com/watch?v=" + videoId; }
juce::String youTubeMusicUrl(const juce::String& videoId) { return "https://music.youtube.com/watch?v=" + videoId; }

juce::String youTubeQuery(const Track& track) {
    auto query = track.artist + " " + track.titleShort;
    // Una versión que cambia la grabación ("En Vivo") se busca; "Remastered", no.
    if (const auto v = words(track.version); hasAny(v, versionWords) || hasAny(v, otherPerformerWords))
        query << " " << withoutBrackets(track.version.removeCharacters("()[]"));
    return query.trim();
}

juce::String spotifySearchUrl(const Track& track) {
    return "https://open.spotify.com/search/" + escape(track.artist + " " + track.titleShort);
}

juce::String youTubeSearchPageUrl(const Track& track) {
    return "https://www.youtube.com/results?search_query=" + escape(youTubeQuery(track));
}

//==============================================================================
// Comparar

double matchScore(const Track& track, const Video& video) {
    const auto wanted = words(track.titleShort.isNotEmpty() ? withoutBrackets(track.titleShort) : withoutBrackets(track.title));
    if (wanted.empty()) return 0.0;

    // Título: en YouTube Music es el título solo; en YouTube, el del video, que suele llevar
    // también el artista y "(Video Oficial)": basta que contenga el buscado.
    double title;
    if (video.fromMusic) {
        const auto found = words(withoutBrackets(video.title));
        title = found.empty() ? 0.0 : 2.0 * common(wanted, found) / (double) (wanted.size() + found.size());
    } else {
        title = (double) common(wanted, words(video.title)) / (double) wanted.size();
    }

    // Artista: el de la canción, o en el título del video ("Juan Luis Guerra - Burbujas...").
    double artist = artistSimilarity(track.artist, video.artists);
    if (!video.fromMusic && artist < 1.0) {
        const auto a = words(track.artist);
        if (!a.empty()) artist = juce::jmax(artist, (double) common(a, words(video.title)) / (double) a.size());
    }

    auto score = 0.4 * title + 0.4 * artist + 0.2 * durationSimilarity(track.duration, video.duration);
    if (video.kind == Video::Kind::OfficialAudio) score += 0.04;
    else if (video.kind == Video::Kind::OfficialVideo) score += 0.02;
    else if (video.kind == Video::Kind::UserUpload) score -= 0.04;

    // Otra versión: en vivo, remix... o de otro intérprete (karaoke, cover).
    const auto trackWords = words(track.title + " " + track.version);
    const auto videoWords = words(video.title);
    if (hasAny(videoWords, otherPerformerWords) && !hasAny(trackWords, otherPerformerWords)) score -= 0.6;
    else if (hasAny(videoWords, versionWords) && !hasAny(trackWords, versionWords)) score -= 0.35;
    else if (hasAny(trackWords, versionWords) && !hasAny(videoWords, versionWords)) score -= 0.2;

    // Otro título es otra canción, aunque sea del mismo artista y dure lo mismo.
    if (title < 0.5) score = juce::jmin(score, 0.2);
    return juce::jlimit(0.0, 1.0, score);
}

void rank(const Track& track, std::vector<Video>& videos) {
    for (auto& v : videos) v.score = matchScore(track, v);
    videos.erase(std::remove_if(videos.begin(), videos.end(), [](const Video& v) { return v.score < otherSong; }), videos.end());
    std::stable_sort(videos.begin(), videos.end(), [](const Video& a, const Video& b) {
        if (std::abs(a.score - b.score) > 0.001) return a.score > b.score;
        return a.fromMusic && !b.fromMusic;  // a igual puntaje, el audio de YouTube Music
    });
}

int findInLibrary(const Track& track, const std::vector<Database::KeyRow>& library) {
    const auto title = words(withoutBrackets(track.titleShort.isNotEmpty() ? track.titleShort : track.title));
    if (title.empty() || track.artist.isEmpty()) return -1;
    for (const auto& row : library) {
        const auto key = juce::String::fromUTF8(row.key.c_str());
        const auto artistKey = key.upToFirstOccurrenceOf("|", false, false);
        const auto titleKey = key.fromFirstOccurrenceOf("|", false, false);
        if (words(titleKey) == title && artistSimilarity(track.artist, artistKey) >= 0.99) return row.id;
    }
    return -1;
}

}
