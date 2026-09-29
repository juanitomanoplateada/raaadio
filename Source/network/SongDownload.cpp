#include "SongDownload.h"
#include "../library/MusicLibrary.h"

namespace SongDownload {

bool isValidVideoId(const juce::String& id) {
    return id.length() == 11 && id.containsOnly("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_");
}

juce::StringArray arguments(const DownloadTools::Paths& tools, const juce::String& videoId, const juce::File& outputFolder) {
    juce::StringArray args { tools.ytDlp.getFullPathName(),
        "--no-config", "--no-playlist", "--no-cache-dir", "--no-mtime", "--encoding", "utf-8",
        "--ffmpeg-location", tools.ffmpegFolder.getFullPathName(),
        "-f", "bestaudio/best", "-x", "--audio-format", "mp3", "--audio-quality", "0",
        // Pocos reintentos: repetir pedidos fallidos es justo lo que hace que YouTube bloquee.
        "--retries", "3", "--fragment-retries", "3", "--extractor-retries", "1", "--socket-timeout", "30",
        "--sleep-requests", "1",
        "--newline", "--progress", "--progress-template", "download:[raaadio] %(progress._percent_str)s",
        "--print", "after_move:filepath",
        "-o", outputFolder.getChildFile("%(id)s.%(ext)s").getFullPathName() };
    if (tools.jsRuntime.existsAsFile()) {
        args.add("--js-runtimes");
        args.add(tools.jsRuntimeName + ":" + tools.jsRuntime.getFullPathName());
    }
    args.add("https://www.youtube.com/watch?v=" + videoId);
    return args;
}

juce::StringArray updateArguments(const DownloadTools::Paths& tools) {
    return { tools.ytDlp.getFullPathName(), "--no-config", "-U" };
}

double parseProgress(const juce::String& line) {
    const auto text = line.trim();
    if (!text.startsWith("[raaadio]")) return -1.0;
    const auto number = text.fromFirstOccurrenceOf("]", false, false).upToFirstOccurrenceOf("%", false, false).trim();
    if (number.isEmpty() || !number.containsOnly("0123456789.")) return -1.0;
    return juce::jlimit(0.0, 1.0, number.getDoubleValue() / 100.0);
}

juce::String parseFinalPath(const juce::String& line) {
    const auto text = line.trim();
    return juce::File::isAbsolutePath(text) && text.endsWithIgnoreCase(".mp3") ? text : juce::String();
}

Problem classify(const juce::String& output) {
    const auto text = output.toLowerCase();
    // "Video unavailable. This content isn't available, try again later": primero el límite.
    if (text.contains("rate-limit") || text.contains("rate limit") || text.contains("try again later")
        || text.contains("http error 429") || text.contains("too many requests"))
        return Problem::RateLimited;
    if (text.contains("not a bot")) return Problem::BotCheck;
    if (text.contains("confirm your age") || text.contains("age-restricted") || text.contains("inappropriate for some users"))
        return Problem::AgeRestricted;
    if (text.contains("in your country") || text.contains("geo restrict") || text.contains("geo-restrict"))
        return Problem::Region;
    if (text.contains("video unavailable") || text.contains("private video") || text.contains("has been removed")
        || text.contains("no longer available") || text.contains("is not available") || text.contains("members-only"))
        return Problem::Unavailable;
    if (text.contains("ffmpeg") && (text.contains("not found") || text.contains("not installed")))
        return Problem::Tools;
    if (text.contains("unable to download") || text.contains("getaddrinfo") || text.contains("timed out")
        || text.contains("connection") || text.contains("urlopen error") || text.contains("http error 5"))
        return Problem::Network;
    return text.contains("error") ? Problem::Other : Problem::None;
}

int pauseMinutes(Problem problem) {
    switch (problem) {
        case Problem::RateLimited: return 60;       // YouTube dice "hasta una hora"
        case Problem::BotCheck:    return 6 * 60;   // la marca de la conexión dura horas
        default:                   return 0;
    }
}

juce::String problemKey(Problem problem) {
    switch (problem) {
        case Problem::None:          return {};
        case Problem::RateLimited:   return "rate_limited";
        case Problem::BotCheck:      return "bot_check";
        case Problem::Unavailable:   return "unavailable";
        case Problem::AgeRestricted: return "age_restricted";
        case Problem::Region:        return "region";
        case Problem::Network:       return "network";
        case Problem::Tools:         return "tools";
        case Problem::Other:         return "other";
    }
    return "other";
}

Problem problemFromKey(const juce::String& key) {
    for (auto p : { Problem::RateLimited, Problem::BotCheck, Problem::Unavailable, Problem::AgeRestricted,
                    Problem::Region, Problem::Network, Problem::Tools, Problem::Other })
        if (problemKey(p) == key) return p;
    return Problem::None;
}

juce::String fileName(const SongSearch::Track& track) {
    const auto title = track.title.isNotEmpty() ? track.title : track.titleShort;
    return MusicLibrary::makeSafeFileName(track.artist.isNotEmpty() ? track.artist + " - " + title : title);
}

TagWriter::Tags tagsFor(const SongSearch::Track& track, const juce::String& videoId) {
    TagWriter::Tags t;
    t.artist = track.featured.isEmpty() ? track.artist : track.artist + " feat. " + track.featured.joinIntoString(", ");
    t.title = track.title.isNotEmpty() ? track.title : track.titleShort;
    t.album = track.album;
    t.year = track.year;
    t.isrc = track.isrc;
    t.source = "https://www.youtube.com/watch?v=" + videoId;
    return t;
}

int secondsToWait(const juce::Array<juce::Time>& recentStarts, juce::Time now, int minGapSeconds) {
    const auto hourAgo = now - juce::RelativeTime::hours(1);
    juce::Array<juce::Time> inHour;
    juce::Time latest;
    for (const auto& t : recentStarts) {
        if (t > hourAgo) inHour.add(t);
        if (t > latest) latest = t;
    }
    int wait = 0;
    if (latest.toMilliseconds() > 0)
        wait = juce::jmax(wait, (int) std::ceil(minGapSeconds - (now - latest).inSeconds()));
    if (inHour.size() >= perHour) {
        // Hasta que la más vieja de las que cuentan salga de la hora.
        std::sort(inHour.begin(), inHour.end());
        const auto freeAt = inHour[inHour.size() - perHour] + juce::RelativeTime::hours(1);
        wait = juce::jmax(wait, (int) std::ceil((freeAt - now).inSeconds()));
    }
    return juce::jmax(0, wait);
}

}
