#include "DownloadTools.h"

namespace DownloadTools {

namespace {
    const char* const ytDlpRelease = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/";
    const char* const ffmpegRelease = "https://github.com/yt-dlp/FFmpeg-Builds/releases/download/latest/";
    const char* const denoRelease = "https://github.com/denoland/deno/releases/latest/download/";
    const char* const ffmpegZip = "ffmpeg-master-latest-win64-gpl-shared.zip";  // compartida: la mitad de grande
    const char* const denoZip = "deno-x86_64-pc-windows-msvc.zip";

    // El primer programa con ese nombre en las carpetas del PATH.
    juce::File onPath(const juce::String& pathVariable, const juce::String& name) {
       #if JUCE_WINDOWS
        const juce::String separator(";");
       #else
        const juce::String separator(":");
       #endif
        for (auto dir : juce::StringArray::fromTokens(pathVariable, separator, "\"")) {
            dir = dir.trim().unquoted();
            if (dir.isEmpty() || !juce::File::isAbsolutePath(dir)) continue;
            const auto candidate = juce::File(dir).getChildFile(Paths::exe(name));
            if (candidate.existsAsFile()) return candidate;
        }
        return {};
    }

    bool isHex64(const juce::String& s) {
        return s.length() == 64 && s.containsOnly("0123456789abcdefABCDEF");
    }
}

juce::String Paths::exe(const juce::String& name) {
   #if JUCE_WINDOWS
    return name + ".exe";
   #else
    return name;
   #endif
}

juce::File defaultFolder() {
   #if JUCE_WINDOWS
    return juce::File::getSpecialLocation(juce::File::windowsLocalAppData).getChildFile("RAAADIO").getChildFile("Herramientas");
   #else
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("RAAADIO").getChildFile("Herramientas");
   #endif
}

Paths find(const juce::File& folder, const juce::String& pathVariable) {
    Paths p;
    const auto ownYtDlp = folder.getChildFile(Paths::exe("yt-dlp"));
    p.managedYtDlp = ownYtDlp.existsAsFile();
    p.ytDlp = p.managedYtDlp ? ownYtDlp : onPath(pathVariable, "yt-dlp");

    const auto ownFfmpeg = folder.getChildFile("ffmpeg");
    if (ownFfmpeg.getChildFile(Paths::exe("ffmpeg")).existsAsFile()) p.ffmpegFolder = ownFfmpeg;
    else if (const auto ffmpeg = onPath(pathVariable, "ffmpeg"); ffmpeg.existsAsFile()) p.ffmpegFolder = ffmpeg.getParentDirectory();

    // Deno es el que yt-dlp recomienda; Node también sirve.
    const auto ownDeno = folder.getChildFile("deno").getChildFile(Paths::exe("deno"));
    if (ownDeno.existsAsFile()) { p.jsRuntime = ownDeno; p.jsRuntimeName = "deno"; }
    else if (const auto deno = onPath(pathVariable, "deno"); deno.existsAsFile()) { p.jsRuntime = deno; p.jsRuntimeName = "deno"; }
    else if (const auto node = onPath(pathVariable, "node"); node.existsAsFile()) { p.jsRuntime = node; p.jsRuntimeName = "node"; }
    return p;
}

std::vector<Asset> missing(const Paths& found, const juce::File& folder) {
    std::vector<Asset> assets;
    if (!found.ytDlp.existsAsFile()) {
        Asset a;
        a.name = "yt-dlp";
        a.url = juce::String(ytDlpRelease) + "yt-dlp.exe";
        a.checksumUrl = juce::String(ytDlpRelease) + "SHA2-256SUMS";
        a.checksumName = "yt-dlp.exe";
        a.destination = folder.getChildFile(Paths::exe("yt-dlp"));
        a.approxMegabytes = 17;
        assets.push_back(a);
    }
    if (!found.ffmpegFolder.getChildFile(Paths::exe("ffmpeg")).existsAsFile()) {
        Asset a;
        a.name = ffmpegName;
        a.url = juce::String(ffmpegRelease) + ffmpegZip;
        a.checksumUrl = juce::String(ffmpegRelease) + "checksums.sha256";
        a.checksumName = ffmpegZip;
        a.zip = true;
        a.zipFolder = "bin/";
        a.skip = { "ffplay.exe" };
        a.destination = folder.getChildFile("ffmpeg");
        a.approxMegabytes = 83;
        assets.push_back(a);
    }
    if (!found.jsRuntime.existsAsFile()) {
        Asset a;
        a.name = "Deno";
        a.url = juce::String(denoRelease) + denoZip;
        a.checksumUrl = juce::String(denoRelease) + denoZip + ".sha256sum";
        a.checksumName = denoZip;
        a.zip = true;
        a.destination = folder.getChildFile("deno");
        a.approxMegabytes = 41;
        assets.push_back(a);
    }
    return assets;
}

int totalMegabytes(const std::vector<Asset>& assets) {
    int total = 0;
    for (const auto& a : assets) total += a.approxMegabytes;
    return total;
}

juce::String findSha256(const juce::String& checksums, const juce::String& fileName) {
    juce::String loose;  // "Hash : ..." sin nombre de archivo en la misma línea
    for (const auto& rawLine : juce::StringArray::fromLines(checksums)) {
        const auto line = rawLine.trim();
        const auto tokens = juce::StringArray::fromTokens(line, " \t:*", "");
        juce::String hex;
        for (const auto& t : tokens)
            if (isHex64(t)) hex = t;
        if (hex.isEmpty()) continue;
        // "<hex>  archivo" o "<hex> *archivo": el nombre exacto, no uno que lo contenga.
        for (const auto& t : tokens)
            if (t == fileName) return hex.toLowerCase();
        if (line.startsWithIgnoreCase("hash")) loose = hex.toLowerCase();
    }
    return loose;
}

int extract(const juce::File& zipFile, const Asset& asset) {
    juce::ZipFile zip(zipFile);
    if (zip.getNumEntries() == 0) return 0;
    asset.destination.createDirectory();
    int extracted = 0;
    for (int i = 0; i < zip.getNumEntries(); ++i) {
        const auto* entry = zip.getEntry(i);
        if (entry == nullptr) continue;
        const auto path = entry->filename.replaceCharacter('\\', '/');
        if (path.endsWithChar('/')) continue;  // carpeta
        const auto name = path.fromLastOccurrenceOf("/", false, false);
        if (name.isEmpty() || asset.skip.contains(name, true)) continue;
        if (asset.zipFolder.isNotEmpty() && !path.containsIgnoreCase("/" + asset.zipFolder) && !path.startsWithIgnoreCase(asset.zipFolder))
            continue;
        std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(i));
        if (in == nullptr) return 0;
        const auto target = asset.destination.getChildFile(name);
        target.deleteFile();
        juce::FileOutputStream out(target);
        if (!out.openedOk() || out.writeFromInputStream(*in, -1) < 0) return 0;
        out.flush();
        ++extracted;
    }
    return extracted;
}

}
