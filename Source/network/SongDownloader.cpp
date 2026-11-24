#include "SongDownloader.h"
#include "LinkDownload.h"
#include "OnlineMetadata.h"
#include "../library/AudioConverter.h"

namespace {
    const std::string pausedUntilKey = "downloads.pausedUntil";
    const std::string pauseReasonKey = "downloads.pauseReason";
    const std::string lastUpdateKey = "downloads.lastUpdate";
    const std::string acknowledgedKey = "downloads.acknowledged";
    const std::string linksAcknowledgedKey = "downloads.linksAcknowledged";

    // Lo último que yt-dlp dijo de un error, sin el prefijo.
    juce::String lastError(const juce::StringArray& lines) {
        for (int i = lines.size(); --i >= 0;)
            if (lines[i].startsWith("ERROR:")) return lines[i].fromFirstOccurrenceOf("ERROR:", false, false).trim();
        return lines.isEmpty() ? juce::String() : lines[lines.size() - 1].trim();
    }
}

SongDownloader::SongDownloader(MusicLibrary& lib) : SongDownloader(lib, Services{}) {}

SongDownloader::SongDownloader(MusicLibrary& lib, Services s)
    : juce::Thread("Song Downloader"), library(lib), services(std::move(s))
{
    if (!services.transport) services.transport = &HttpTransport::send;
    if (!services.fetch) services.fetch = &HttpTransport::downloadToFile;
    if (!services.run) services.run = [](const juce::StringArray& c, const ProcessRunner::LineCallback& l, const ProcessRunner::StopCheck& s) {
        return ProcessRunner::run(c, l, s);
    };
    if (services.toolsFolder == juce::File()) services.toolsFolder = DownloadTools::defaultFolder();
    if (services.workFolder == juce::File())
        services.workFolder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("RAAADIO-descargas");
    if (!services.pathVariable.has_value()) services.pathVariable = juce::SystemStats::getEnvironmentVariable("PATH", {});

    findTools();
    // Una pausa pedida por YouTube sigue aunque se haya cerrado el programa.
    const auto until = getSetting(pausedUntilKey).getLargeIntValue();
    if (until > juce::Time::currentTimeMillis()) {
        status.pausedUntil = juce::Time(until);
        status.pauseReason = SongDownload::problemFromKey(getSetting(pauseReasonKey));
    }
    startThread(juce::Thread::Priority::low);
}

SongDownloader::~SongDownloader() {
    signalThreadShouldExit();  // la descarga en curso se cancela (ProcessRunner cierra yt-dlp y FFmpeg)
    notify();
    stopThread(20000);
}

//==============================================================================
// Ajustes y herramientas

juce::String SongDownloader::getSetting(const std::string& key) {
    return juce::String::fromUTF8(library.withDatabase([&key](Database& db) { return db.getSetting(key); }).c_str());
}

void SongDownloader::setSetting(const std::string& key, const juce::String& value) {
    const auto v = value.toStdString();
    library.withDatabase([&](Database& db) { return db.setSetting(key, v); });
}

bool SongDownloader::isAcknowledged(bool links) { return getSetting(links ? linksAcknowledgedKey : acknowledgedKey) == "1"; }
void SongDownloader::acknowledge(bool links) { setSetting(links ? linksAcknowledgedKey : acknowledgedKey, "1"); }

void SongDownloader::findTools() {
    const auto found = DownloadTools::find(services.toolsFolder, *services.pathVariable);
    const juce::ScopedLock sl(lock);
    tools = found;
    status.toolsReady = found.ready();
}

DownloadTools::Paths SongDownloader::getTools() const {
    const juce::ScopedLock sl(lock);
    return tools;
}

std::vector<DownloadTools::Asset> SongDownloader::getMissingTools() const {
    return DownloadTools::missing(getTools(), services.toolsFolder);
}

void SongDownloader::installTools(const juce::StringArray& only) {
    {
        const juce::ScopedLock sl(lock);
        if (status.installing) return;
        status.installing = true;
        status.installError = {};
        status.installErrorStep = {};
        status.installProgress = 0.0;
        installOnly = only;
    }
    installRequested = true;
    notify();
    sendChangeMessage();
}

void SongDownloader::runInstall() {
    auto fail = [this](const juce::String& error, const juce::String& step) {
        {
            const juce::ScopedLock sl(lock);
            status.installing = false;
            status.installError = error;
            status.installErrorStep = step;
        }
        sendChangeMessage();
    };

    juce::StringArray only;
    { const juce::ScopedLock sl(lock); only = installOnly; }
    auto wanted = [&only](const DownloadTools::Asset& asset) { return only.isEmpty() || only.contains(asset.name); };

    services.toolsFolder.createDirectory();
    for (const auto& asset : DownloadTools::missing(getTools(), services.toolsFolder)) {
        if (!wanted(asset)) continue;
        {
            const juce::ScopedLock sl(lock);
            status.installStep = asset.name;
            status.installProgress = 0.0;
        }
        sendChangeMessage();

        // El SHA-256 publicado junto al programa: si no coincide, no se usa.
        HttpTransport::Request request { asset.checksumUrl, {}, "User-Agent: " + OnlineMetadata::userAgent() };
        const auto sums = services.transport(request);
        const auto expected = sums.status == 200 ? DownloadTools::findSha256(sums.body, asset.checksumName) : juce::String();
        if (expected.isEmpty()) return fail(sums.status == 200 ? "checksum" : "download", asset.name);

        const auto temp = services.toolsFolder.getChildFile("descargando-" + asset.url.fromLastOccurrenceOf("/", false, false));
        double shown = -2.0;
        const bool fetched = services.fetch(asset.url, temp, [this, &shown](double progress) {
            if (std::abs(progress - shown) >= 0.01) {
                shown = progress;
                { const juce::ScopedLock sl(lock); status.installProgress = progress; }
                sendChangeMessage();
            }
            return !threadShouldExit();
        });
        if (!fetched || threadShouldExit()) {
            temp.deleteFile();
            return fail(threadShouldExit() ? juce::String() : juce::String("download"), asset.name);
        }
        if (juce::SHA256(temp).toHexString().toLowerCase() != expected) {
            temp.deleteFile();
            return fail("checksum", asset.name);
        }

        bool placed;
        if (asset.zip) {
            placed = DownloadTools::extract(temp, asset) > 0;
            temp.deleteFile();
        } else {
            asset.destination.deleteFile();
            placed = temp.moveFileTo(asset.destination);
        }
        if (!placed) return fail("extract", asset.name);
    }

    findTools();
    bool stillMissing = false;
    for (const auto& asset : DownloadTools::missing(getTools(), services.toolsFolder))
        stillMissing = stillMissing || (!only.isEmpty() && wanted(asset));
    {
        const juce::ScopedLock sl(lock);
        status.installing = false;
        status.installStep = {};
        if (only.isEmpty() ? !status.toolsReady : stillMissing) status.installError = "extract";
    }
    sendChangeMessage();
    notify();  // si había canciones esperando las herramientas
}

void SongDownloader::maybeUpdateYtDlp() {
    // YouTube cambia seguido y un yt-dlp viejo deja de funcionar: una vez al día se actualiza
    // (solo el de RAAADIO; el que el usuario instaló por su cuenta lo maneja él).
    const auto t = getTools();
    if (!t.managedYtDlp) return;
    const auto last = getSetting(lastUpdateKey).getLargeIntValue();
    const auto now = juce::Time::currentTimeMillis();
    if (last > 0 && now - last < 24LL * 60 * 60 * 1000) return;
    setSetting(lastUpdateKey, juce::String(now));
    services.run(SongDownload::updateArguments(t), [](const juce::String&) {}, [this] { return threadShouldExit(); });
}

//==============================================================================
// Fila

SongDownloader::Job* SongDownloader::job(int jobId) {
    for (auto& j : jobs)
        if (j.id == jobId) return &j;
    return nullptr;
}

int SongDownloader::enqueue(const SongSearch::Track& track, const SongSearch::Video& video) {
    if (!SongDownload::isValidVideoId(video.id)) return 0;
    int id = 0;
    {
        const juce::ScopedLock sl(lock);
        for (const auto& j : jobs)
            if (j.kind == Kind::Song && !j.isFinished() && (j.track.id == track.id || j.video.id == video.id)) return 0;
        Job j;
        j.id = id = nextJobId++;
        j.track = track;
        j.video = video;
        jobs.push_back(j);
    }
    notify();
    sendChangeMessage();
    return id;
}

int SongDownloader::enqueueLink(const juce::String& pasted) {
    const auto url = LinkDownload::normalise(pasted);
    if (url.isEmpty()) return 0;
    int id = 0;
    {
        const juce::ScopedLock sl(lock);
        for (const auto& j : jobs)
            if (j.kind == Kind::Temporary && !j.isFinished() && j.url == url) return 0;
        Job j;
        j.id = id = nextJobId++;
        j.kind = Kind::Temporary;
        j.url = url;
        j.site = LinkDownload::siteName(url);
        jobs.push_back(j);
    }
    notify();
    sendChangeMessage();
    return id;
}

void SongDownloader::cancel(int jobId) {
    {
        const juce::ScopedLock sl(lock);
        auto* j = job(jobId);
        if (j == nullptr || j->isFinished()) return;
        if (j->state == State::Queued) j->state = State::Cancelled;  // la que corre se cancela en su hilo
        else cancelRequested.insert(jobId);
    }
    notify();
    sendChangeMessage();
}

void SongDownloader::retry(int jobId) {
    {
        const juce::ScopedLock sl(lock);
        auto* j = job(jobId);
        if (j == nullptr || (j->state != State::Failed && j->state != State::Cancelled)) return;
        j->state = State::Queued;
        j->progress = 0.0;
        j->problem = SongDownload::Problem::None;
        j->detail = {};
    }
    notify();
    sendChangeMessage();
}

void SongDownloader::clearFinished() {
    {
        const juce::ScopedLock sl(lock);
        jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [](const Job& j) { return j.isFinished(); }), jobs.end());
    }
    sendChangeMessage();
}

std::vector<SongDownloader::Job> SongDownloader::getJobs() const {
    const juce::ScopedLock sl(lock);
    return jobs;
}

std::vector<SongDownloader::Job> SongDownloader::getJobs(Kind kind) const {
    const juce::ScopedLock sl(lock);
    std::vector<Job> result;
    for (const auto& j : jobs)
        if (j.kind == kind) result.push_back(j);
    return result;
}

std::optional<SongDownloader::Job> SongDownloader::jobForTrack(juce::int64 deezerId) const {
    const juce::ScopedLock sl(lock);
    for (auto it = jobs.rbegin(); it != jobs.rend(); ++it)
        if (it->kind == Kind::Song && it->track.id == deezerId) return *it;
    return std::nullopt;
}

SongDownloader::Status SongDownloader::getStatus() const {
    const juce::ScopedLock sl(lock);
    auto s = status;
    s.queued = s.finished = 0;
    for (const auto& j : jobs) {
        if (j.kind != Kind::Song) continue;
        if (j.state == State::Queued) ++s.queued;
        if (j.isFinished()) ++s.finished;
    }
    if (s.pausedUntil <= juce::Time::getCurrentTime()) s.pausedUntil = juce::Time();
    return s;
}

void SongDownloader::setState(int jobId, State state, double progress) {
    {
        const juce::ScopedLock sl(lock);
        if (auto* j = job(jobId)) {
            j->state = state;
            if (progress > -2.0) j->progress = progress;
        }
    }
    sendChangeMessage();
}

bool SongDownloader::isCancelRequested(int jobId) const {
    const juce::ScopedLock sl(lock);
    return cancelRequested.count(jobId) > 0;
}

void SongDownloader::pauseFor(SongDownload::Problem problem) {
    const auto until = juce::Time::getCurrentTime() + juce::RelativeTime::minutes(SongDownload::pauseMinutes(problem));
    setSetting(pausedUntilKey, juce::String(until.toMilliseconds()));
    setSetting(pauseReasonKey, SongDownload::problemKey(problem));
    const juce::ScopedLock sl(lock);
    status.pausedUntil = until;
    status.pauseReason = problem;
}

void SongDownloader::recentStarts(juce::Array<juce::Time>& youtube, juce::Time& latestAny) {
    const auto rows = library.withDatabase([](Database& db) { return db.getDownloadStartsSince(60); });
    youtube.clear();
    latestAny = juce::Time();
    for (const auto& r : rows) {
        const auto started = parseLocalTimestamp(r.startedAt);
        if (LinkDownload::isYouTube(juce::String::fromUTF8(r.videoId.c_str()))) youtube.add(started);
        if (started > latestAny) latestAny = started;
    }
}

int SongDownloader::pickNextJob(int& waitSeconds) {
    struct Candidate { int id; bool youtube; };
    std::vector<Candidate> queued;
    juce::Time pausedUntil;
    {
        const juce::ScopedLock sl(lock);
        for (const auto& j : jobs)
            if (j.state == State::Queued)
                queued.push_back({ j.id, j.kind == Kind::Song || LinkDownload::isYouTube(j.url) });
        pausedUntil = status.pausedUntil;
    }
    waitSeconds = 0;
    if (queued.empty()) return 0;

    // YouTube: pausa pedida, pausa desde su última descarga y tope por hora. Los demás sitios:
    // solo la pausa desde la última descarga, de donde sea (un saludo no espera a que YouTube
    // vuelva a dejar bajar canciones).
    const auto now = juce::Time::getCurrentTime();
    juce::Array<juce::Time> youtubeStarts;
    juce::Time latestAny;
    recentStarts(youtubeStarts, latestAny);
    const int gap = services.minGapSeconds + jitterSeconds;
    juce::Array<juce::Time> latestOnly;
    if (latestAny.toMilliseconds() > 0) latestOnly.add(latestAny);

    int soonest = std::numeric_limits<int>::max();
    for (const auto& c : queued) {
        int wait;
        if (c.youtube)
            wait = now < pausedUntil ? (int) std::ceil((pausedUntil - now).inSeconds())
                                     : SongDownload::secondsToWait(youtubeStarts, now, gap);
        else
            wait = SongDownload::secondsToWait(latestOnly, now, gap);
        if (wait <= 0) return c.id;
        soonest = juce::jmin(soonest, wait);
    }
    waitSeconds = soonest;
    return 0;
}

void SongDownloader::run() {
    services.workFolder.deleteRecursively();  // lo que quedó de una sesión anterior

    while (!threadShouldExit()) {
        if (installRequested.exchange(false)) {
            runInstall();
            continue;
        }

        {
            const juce::ScopedLock sl(lock);
            status.nextStart = juce::Time();
        }

        // El ritmo: si YouTube pidió esperar, lo de YouTube espera (sin fallar); y entre una
        // descarga y otra, una pausa (con algo de azar) y el tope por hora.
        int waitSeconds = 0;
        const int next = pickNextJob(waitSeconds);
        if (next == 0) {
            if (waitSeconds <= 0) {
                wait(-1);  // nada en la fila
                continue;
            }
            const auto now = juce::Time::getCurrentTime();
            juce::Time pausedUntil;
            {
                const juce::ScopedLock sl(lock);
                pausedUntil = status.pausedUntil;
                if (now >= pausedUntil) status.nextStart = now + juce::RelativeTime::seconds(waitSeconds);
            }
            sendChangeMessage();
            // De a un minuto como mucho: un enlace nuevo, o el fin de la pausa, se atienden pronto.
            wait(juce::jmin(60000, waitSeconds * 1000 + 100));
            continue;
        }

        if (!getTools().ready()) {
            finishJob(next, -1, State::Failed, SongDownload::Problem::Tools, {}, -1);
            continue;
        }
        maybeUpdateYtDlp();
        if (threadShouldExit()) break;
        jitterSeconds = services.maxJitterSeconds > 0 ? random.nextInt(services.maxJitterSeconds + 1) : 0;
        bool link;
        {
            const juce::ScopedLock sl(lock);
            const auto* j = job(next);
            link = j != nullptr && j->kind == Kind::Temporary;
        }
        if (link) runLinkJob(next);
        else runJob(next);
    }
}

void SongDownloader::finishJob(int jobId, int recordId, State state, SongDownload::Problem problem, const juce::String& detail, int trackId) {
    {
        const juce::ScopedLock sl(lock);
        if (auto* j = job(jobId)) {
            j->state = state;
            j->problem = problem;
            j->detail = detail;
            j->trackId = trackId;
            if (state == State::Done || state == State::Duplicate) j->progress = 1.0;
        }
        cancelRequested.erase(jobId);
        if (status.activeJob == jobId) status.activeJob = 0;
    }
    if (recordId > 0) {
        const auto dbStatus = state == State::Done ? Database::DownloadStatus::Done
                            : state == State::Duplicate ? Database::DownloadStatus::Duplicate
                            : state == State::Cancelled ? Database::DownloadStatus::Cancelled : Database::DownloadStatus::Failed;
        const auto error = (SongDownload::problemKey(problem) + (detail.isNotEmpty() ? ": " + detail : juce::String())).toStdString();
        library.withDatabase([&](Database& db) { return db.finishDownload(recordId, dbStatus, trackId, error); });
    }
    sendChangeMessage();
}

void SongDownloader::runJob(int jobId) {
    Job current;
    {
        const juce::ScopedLock sl(lock);
        auto* j = job(jobId);
        if (j == nullptr) return;
        j->state = State::Downloading;
        j->progress = 0.0;
        current = *j;
        status.activeJob = jobId;
    }
    sendChangeMessage();

    const auto videoId = current.video.id;
    const int recordId = library.withDatabase([&](Database& db) {
        return db.startDownload(videoId.toStdString(), (long long) current.track.id, current.track.artist.toStdString(),
                                current.track.title.toStdString());
    });

    const auto folder = services.workFolder.getChildFile(juce::String(jobId));
    folder.deleteRecursively();
    folder.createDirectory();

    juce::String finalPath;
    juce::StringArray output;  // las últimas líneas, para saber qué falló
    double shown = 0.0;
    const int code = services.run(SongDownload::arguments(getTools(), videoId, folder), [&](const juce::String& line) {
        const auto progress = SongDownload::parseProgress(line);
        if (progress >= 0.0) {
            if (progress - shown >= 0.01 || (progress >= 1.0 && shown < 1.0)) {
                shown = progress;
                setState(jobId, State::Downloading, progress);
            }
            return;
        }
        if (const auto path = SongDownload::parseFinalPath(line); path.isNotEmpty()) finalPath = path;
        output.add(line);
        if (output.size() > 40) output.remove(0);
    }, [this, jobId] { return threadShouldExit() || isCancelRequested(jobId); });

    auto cleanUp = [&folder] { folder.deleteRecursively(); };

    if (code == ProcessRunner::stopped) {
        cleanUp();
        finishJob(jobId, recordId, State::Cancelled, SongDownload::Problem::None, {}, -1);
        return;
    }

    auto mp3 = finalPath.isNotEmpty() ? juce::File(finalPath) : folder.getChildFile(videoId + ".mp3");
    if (code != 0 || !mp3.existsAsFile()) {
        cleanUp();
        const auto text = output.joinIntoString("\n");
        auto problem = code == ProcessRunner::couldNotStart ? SongDownload::Problem::Tools
                     : code == ProcessRunner::timedOut ? SongDownload::Problem::Network
                     : SongDownload::classify(text);
        if (problem == SongDownload::Problem::None) problem = SongDownload::Problem::Other;

        if (SongDownload::pauseMinutes(problem) > 0) {
            // YouTube pidió esperar: se para la fila y esta canción vuelve a esperar su turno.
            pauseFor(problem);
            finishJob(jobId, recordId, State::Queued, problem, lastError(output), -1);
            return;
        }
        finishJob(jobId, recordId, State::Failed, problem, lastError(output), -1);
        return;
    }

    // Etiquetas con los datos de Deezer y el nombre con el que entra a la biblioteca.
    setState(jobId, State::Importing, 1.0);
    TagWriter::writeMp3(mp3, SongDownload::tagsFor(current.track, videoId));
    const auto named = folder.getChildFile(SongDownload::fileName(current.track) + ".mp3");
    if (mp3 != named && mp3.moveFileTo(named)) mp3 = named;

    const auto source = "YouTube: " + current.track.artist + " - " + current.track.title;
    const auto imported = library.importDownloaded(mp3, source, current.track.isrc);
    cleanUp();

    switch (imported.result) {
        case MusicLibrary::ImportResult::Added:
            finishJob(jobId, recordId, State::Done, SongDownload::Problem::None, {}, imported.trackId);
            break;
        case MusicLibrary::ImportResult::Duplicate:
            finishJob(jobId, recordId, State::Duplicate, SongDownload::Problem::None, {}, -1);
            break;
        case MusicLibrary::ImportResult::Failed:
        case MusicLibrary::ImportResult::NeedsFfmpeg:  // un MP3: no pasa
            finishJob(jobId, recordId, State::Failed, SongDownload::Problem::Other, "import", -1);
            break;
    }
}

void SongDownloader::runLinkJob(int jobId) {
    Job current;
    {
        const juce::ScopedLock sl(lock);
        auto* j = job(jobId);
        if (j == nullptr) return;
        j->state = State::Downloading;
        j->progress = 0.0;
        current = *j;
        status.activeJob = jobId;
    }
    sendChangeMessage();

    const auto url = current.url;
    const bool youtube = LinkDownload::isYouTube(url);
    const int recordId = library.withDatabase([&](Database& db) {
        return db.startDownload(url.toStdString(), 0, current.site.toStdString(), {});
    });

    const auto folder = services.workFolder.getChildFile(juce::String(jobId));
    folder.deleteRecursively();
    folder.createDirectory();
    auto cleanUp = [&folder] { folder.deleteRecursively(); };
    auto stopRequested = [this, jobId] { return threadShouldExit() || isCancelRequested(jobId); };

    juce::String finalPath, title, author;
    juce::StringArray output;  // las últimas líneas, para saber qué falló
    double shown = 0.0;
    const auto tools = getTools();
    const int code = services.run(LinkDownload::arguments(tools, url, folder), [&](const juce::String& line) {
        const auto progress = SongDownload::parseProgress(line);
        if (progress >= 0.0) {
            // En 1 se está convirtiendo: eso se avisa al empezar FFmpeg.
            if (progress - shown >= 0.01 && progress < 1.0) {
                shown = progress;
                setState(jobId, State::Downloading, progress);
            }
            return;
        }
        if (const auto t = LinkDownload::parseTitle(line); t.isNotEmpty()) {
            title = t;
            {
                const juce::ScopedLock sl(lock);
                if (auto* j = job(jobId)) j->title = LinkDownload::cleanTitle(title, {}, current.site);
            }
            sendChangeMessage();
            return;
        }
        if (const auto a = LinkDownload::parseAuthor(line); a.isNotEmpty()) { author = a; return; }
        if (const auto path = LinkDownload::parseFinalPath(line); path.isNotEmpty()) { finalPath = path; return; }
        output.add(line);
        if (output.size() > 40) output.remove(0);
    }, stopRequested);

    if (code == ProcessRunner::stopped) {
        cleanUp();
        finishJob(jobId, recordId, State::Cancelled, SongDownload::Problem::None, {}, -1);
        return;
    }

    // Lo bajado: la ruta que imprimió yt-dlp o, si no la dijo, el único archivo de la carpeta.
    juce::File downloaded(finalPath);
    if (finalPath.isEmpty() || !downloaded.existsAsFile()) {
        downloaded = juce::File();
        for (const auto& f : folder.findChildFiles(juce::File::findFiles, false))
            if (!f.hasFileExtension("part;ytdl;tmp")) downloaded = f;
    }
    if (code != 0 || !downloaded.existsAsFile()) {
        cleanUp();
        const auto text = output.joinIntoString("\n");
        auto problem = code == ProcessRunner::couldNotStart ? SongDownload::Problem::Tools
                     : code == ProcessRunner::timedOut ? SongDownload::Problem::Network
                     : LinkDownload::classify(text);
        // Código 0 sin archivo: yt-dlp lo saltó (el filtro de lo que está en vivo).
        if (problem == SongDownload::Problem::None) problem = code == 0 ? SongDownload::Problem::NoAudio : SongDownload::Problem::Other;

        if (youtube && SongDownload::pauseMinutes(problem) > 0) {
            // YouTube pidió esperar: como con las canciones, la fila de YouTube se detiene y el
            // enlace vuelve a esperar su turno. Otro sitio que pide esperar falla: se reintenta a mano.
            pauseFor(problem);
            finishJob(jobId, recordId, State::Queued, problem, lastError(output), -1);
            return;
        }
        finishJob(jobId, recordId, State::Failed, problem, lastError(output), -1);
        return;
    }

    // A MP3 con FFmpeg (AudioConverter: el mismo de la importación), salvo que ya lo sea.
    setState(jobId, State::Downloading, 1.0);
    const auto name = LinkDownload::cleanTitle(title, author, current.site);
    auto mp3 = folder.getChildFile(MusicLibrary::makeSafeFileName(name) + ".mp3");
    if (downloaded.hasFileExtension("mp3")) {
        if (downloaded != mp3 && !downloaded.moveFileTo(mp3)) mp3 = downloaded;
    } else {
        AudioConverter::Services convert;
        convert.findFfmpeg = [tools] {
            const auto ffmpeg = tools.ffmpegFolder.getChildFile(DownloadTools::Paths::exe("ffmpeg"));
            return ffmpeg.existsAsFile() ? ffmpeg : juce::File();
        };
        convert.run = services.run;
        const auto result = AudioConverter(convert).toMp3(downloaded, mp3, stopRequested);
        if (result != AudioConverter::Result::Converted) {
            cleanUp();
            if (result == AudioConverter::Result::Cancelled)
                finishJob(jobId, recordId, State::Cancelled, SongDownload::Problem::None, {}, -1);
            else if (result == AudioConverter::Result::Unavailable)
                finishJob(jobId, recordId, State::Failed, SongDownload::Problem::Tools, {}, -1);
            else  // FFmpeg no encontró un audio que sacar
                finishJob(jobId, recordId, State::Failed, SongDownload::Problem::NoAudio, "convert", -1);
            return;
        }
    }

    // Etiquetas (título, autor y el enlace de origen) y a los audios temporales.
    setState(jobId, State::Importing, 1.0);
    TagWriter::Tags tags;
    tags.title = name;
    tags.artist = author;
    tags.source = url;
    TagWriter::writeMp3(mp3, tags);
    const auto imported = library.importTemporaryDownload(mp3, name, author);
    cleanUp();

    switch (imported.result) {
        case MusicLibrary::ImportResult::Added:
            finishJob(jobId, recordId, State::Done, SongDownload::Problem::None, {}, imported.trackId);
            break;
        case MusicLibrary::ImportResult::Duplicate:
            finishJob(jobId, recordId, State::Duplicate, SongDownload::Problem::None, {}, imported.trackId);
            break;
        case MusicLibrary::ImportResult::Failed:
        case MusicLibrary::ImportResult::NeedsFfmpeg:  // un MP3: no pasa
            finishJob(jobId, recordId, State::Failed, SongDownload::Problem::Other, "import", -1);
            break;
    }
}
