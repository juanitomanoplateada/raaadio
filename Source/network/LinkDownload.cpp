#include "LinkDownload.h"

namespace LinkDownload {

namespace {
    // El dominio del enlace, en minúsculas y sin "www.", "m."...: "instagram.com".
    juce::String hostOf(const juce::String& url) {
        auto host = url.fromFirstOccurrenceOf("://", false, false);
        for (auto stop : { "/", "?", "#" }) host = host.upToFirstOccurrenceOf(stop, false, false);
        host = host.fromLastOccurrenceOf("@", false, false).upToFirstOccurrenceOf(":", false, false).toLowerCase();
        for (const juce::String prefix : { "www.", "m.", "mobile.", "web.", "vm.", "music." })
            if (host.startsWith(prefix)) host = host.substring(prefix.length());
        return host;
    }

    bool hostIs(const juce::String& host, const juce::String& domain) {
        return host == domain || host.endsWith("." + domain);
    }

    // Un texto JSON (lo que imprime "%(campo)j") o el texto tal cual; "NA" es que yt-dlp no lo sabe.
    juce::String decodePrinted(const juce::String& text) {
        auto value = text.trim();
        if (value.startsWithChar('"')) {
            // fromString acepta un texto suelto (parse solo objetos y listas).
            const auto parsed = juce::JSON::fromString(value);
            if (parsed.isString()) value = parsed.toString();
        }
        return value == "NA" || value == "null" ? juce::String() : value.trim();
    }

    juce::String printed(const juce::String& line, const juce::String& marker) {
        const auto text = line.trim();
        return text.startsWith(marker) ? decodePrinted(text.substring(marker.length())) : juce::String();
    }

    bool containsAny(const juce::String& text, std::initializer_list<const char*> needles) {
        for (auto n : needles)
            if (text.contains(n)) return true;
        return false;
    }
}

juce::String normalise(const juce::String& pasted) {
    auto text = pasted.trim();

    // El primer enlace del texto ("Mira esto https://..."); sin esquema, la primera palabra.
    const int at = text.indexOfIgnoreCase("http");
    int start = -1;
    for (int i = at; i >= 0; i = text.indexOfIgnoreCase(i + 1, "http")) {
        const auto rest = text.substring(i).toLowerCase();
        if (rest.startsWith("http://") || rest.startsWith("https://")) { start = i; break; }
    }
    auto url = start >= 0 ? text.substring(start) : text;
    url = url.upToFirstOccurrenceOf(" ", false, false).upToFirstOccurrenceOf("\n", false, false)
             .upToFirstOccurrenceOf("\t", false, false).upToFirstOccurrenceOf("\r", false, false).trim();
    // Lo que suele venir pegado: signos de puntuación, comillas (también « »), paréntesis.
    const auto trailing = juce::String::fromUTF8(".,;:!?)>]\"'\xc2\xbb");
    const auto leading = juce::String::fromUTF8("<([\"'\xc2\xab");
    while (url.isNotEmpty() && trailing.containsChar(url.getLastCharacter()))
        url = url.dropLastCharacters(1);
    while (url.isNotEmpty() && leading.containsChar(url[0]))
        url = url.substring(1);

    const auto lower = url.toLowerCase();
    if (!lower.startsWith("http://") && !lower.startsWith("https://")) {
        // "www.facebook.com/..." o "instagram.com/reel/...": un dominio, con o sin ruta. Con
        // ":" sería otro esquema ("file:", "javascript:").
        if (url.containsChar(':') || !url.upToFirstOccurrenceOf("/", false, false).containsChar('.'))
            return {};
        url = "https://" + url;
    }

    if (url.length() > 2048) return {};
    for (auto c : url)
        if (c <= 0x20 || c == 0x7f || c == '"' || c == '\\' || c == '<' || c == '>' || c == '`') return {};

    const auto host = hostOf(url);
    if (host.isEmpty() || !host.containsChar('.') || host.startsWithChar('.') || host.endsWithChar('.')
        || !host.containsOnly("abcdefghijklmnopqrstuvwxyz0123456789.-"))
        return {};
    return url;
}

juce::String siteName(const juce::String& url) {
    const auto host = hostOf(url);
    if (hostIs(host, "facebook.com") || hostIs(host, "fb.watch") || hostIs(host, "fb.com")) return "Facebook";
    if (hostIs(host, "instagram.com") || hostIs(host, "instagr.am")) return "Instagram";
    if (hostIs(host, "x.com") || hostIs(host, "twitter.com")) return "X";
    if (hostIs(host, "tiktok.com")) return "TikTok";
    if (hostIs(host, "youtube.com") || hostIs(host, "youtu.be")) return "YouTube";
    if (hostIs(host, "soundcloud.com")) return "SoundCloud";
    if (hostIs(host, "vimeo.com")) return "Vimeo";
    return host;
}

bool isYouTube(const juce::String& urlOrVideoId) {
    if (SongDownload::isValidVideoId(urlOrVideoId)) return true;
    return urlOrVideoId.containsIgnoreCase("://") && siteName(urlOrVideoId) == "YouTube";
}

juce::StringArray arguments(const DownloadTools::Paths& tools, const juce::String& url, const juce::File& outputFolder) {
    juce::StringArray args { tools.ytDlp.getFullPathName(),
        "--no-config", "--no-playlist", "--playlist-items", "1", "--no-cache-dir", "--no-mtime", "--encoding", "utf-8",
        "--ffmpeg-location", tools.ffmpegFolder.getFullPathName(),
        // Solo el audio; si el sitio no lo da aparte, el video (FFmpeg le saca el audio).
        "-f", "bestaudio/best",
        // Una transmisión en vivo no termina nunca.
        "--match-filter", "!is_live",
        // Pocos reintentos, como con YouTube: insistir es lo que hace que un sitio bloquee.
        "--retries", "3", "--fragment-retries", "3", "--extractor-retries", "1", "--socket-timeout", "30",
        "--sleep-requests", "1",
        "--newline", "--progress", "--progress-template", "download:[raaadio] %(progress._percent_str)s",
        "--print", "before_dl:[raaadio-title] %(title)j",
        "--print", "before_dl:[raaadio-author] %(uploader,channel,creator,uploader_id)j",
        "--print", "after_move:filepath",
        "-o", outputFolder.getChildFile("%(id).60s.%(ext)s").getFullPathName() };
    if (tools.jsRuntime.existsAsFile()) {
        args.add("--js-runtimes");
        args.add(tools.jsRuntimeName + ":" + tools.jsRuntime.getFullPathName());
    }
    args.add("--");
    args.add(url);
    return args;
}

juce::String parseTitle(const juce::String& line) { return printed(line, "[raaadio-title]"); }
juce::String parseAuthor(const juce::String& line) { return printed(line, "[raaadio-author]"); }

juce::String parseFinalPath(const juce::String& line) {
    const auto text = line.trim();
    if (!juce::File::isAbsolutePath(text) || text.startsWithChar('[')) return {};
    return juce::File(text).getFileExtension().isNotEmpty() ? text : juce::String();
}

SongDownload::Problem classify(const juce::String& output) {
    using P = SongDownload::Problem;
    const auto text = output.toLowerCase();

    // YouTube: lo de siempre va primero (su "inicia sesión para confirmar que no eres un robot"
    // no es una cuenta que falte).
    if (text.contains("not a bot")) return P::BotCheck;
    if (containsAny(text, { "confirm your age", "age-restricted", "inappropriate for some users", "18 years old" }))
        return P::AgeRestricted;
    // El filtro "!is_live" deja fuera lo que está en vivo; también lo que todavía no empieza.
    if (containsAny(text, { "does not pass filter", "live event will begin", "premieres in" }))
        return P::Live;
    if (text.contains("ffmpeg") && (text.contains("not found") || text.contains("not installed"))) return P::Tools;
    if (text.contains("unsupported url")) return P::Unsupported;
    if (containsAny(text, { "no video could be found", "no video formats found", "there is no video", "there's no video",
                            "does not contain a video", "no media found", "requested format is not available",
                            "no video in this" }))
        return P::NoAudio;
    // Instagram dice "rate-limit reached or login required" casi siempre que falta una sesión;
    // Facebook, "cannot parse data" cuando el video no es público.
    if (containsAny(text, { "login required", "log in", "logged in", "logged-in", "login page", "sign in", "--cookies",
                            "account credentials", "registered users", "authentication", "private", "cannot parse data" }))
        return P::LoginRequired;
    if (containsAny(text, { "http error 404", "http error 410" })) return P::Unavailable;
    return SongDownload::classify(output);
}

juce::String cleanTitle(const juce::String& title, const juce::String& author, const juce::String& site) {
    // Una línea, sin enlaces (los tuits terminan con uno) ni espacios repetidos.
    juce::StringArray words;
    words.addTokens(title.replaceCharacters("\r\n\t", "   "), " ", "");
    juce::StringArray kept;
    for (const auto& w : words)
        if (w.isNotEmpty() && !w.startsWithIgnoreCase("http://") && !w.startsWithIgnoreCase("https://")) kept.add(w);
    auto clean = kept.joinIntoString(" ").trim();
    while (clean.endsWithChar('-') || clean.endsWithChar(':') || clean.endsWithChar('|'))
        clean = clean.dropLastCharacters(1).trimEnd();

    constexpr int maxLength = 90;
    if (clean.length() > maxLength) {
        auto cut = clean.substring(0, maxLength);
        const int space = cut.lastIndexOfChar(' ');
        if (space > maxLength / 2) cut = cut.substring(0, space);
        clean = cut.trimEnd() + juce::String::charToString((juce::juce_wchar) 0x2026);  // "…"
    }
    if (clean.isEmpty()) clean = author.trim();
    if (clean.isEmpty()) clean = site.trim();
    return clean;
}

}
