#include "SongDownloader.h"
#include "OnlineMetadata.h"

namespace {
    const std::string pausedUntilKey = "downloads.pausedUntil";
    const std::string pauseReasonKey = "downloads.pauseReason";
    const std::string lastUpdateKey = "downloads.lastUpdate";
    const std::string acknowledgedKey = "downloads.acknowledged";

    // Lo último que yt-dlp dijo de un error, sin el prefijo.
    juce::String lastError(const juce::StringArray& lines) {
        for (int i = lines.size(); --i >= 0;)
            if (lines[i].startsWith("ERROR:")) return lines[i].fromFirstOccurrenceOf("ERROR:", false, false).trim();
        return lines.isEmpty() ? juce::String() : lines[lines.size() - 1].trim();
    }
}

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

bool SongDownloader::isAcknowledged() { return getSetting(acknowledgedKey) == "1"; }
void SongDownloader::acknowledge() { setSetting(acknowledgedKey, "1"); }

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

void SongDownloader::installTools() {
    {
        const juce::ScopedLock sl(lock);
        if (status.installing) return;
        status.installing = true;
        status.installError = {};
        status.installErrorStep = {};
        status.installProgress = 0.0;
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

    services.toolsFolder.createDirectory();
    for (const auto& asset : DownloadTools::missing(getTools(), services.toolsFolder)) {
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
    {
        const juce::ScopedLock sl(lock);
        status.installing = false;
        status.installStep = {};
        if (!status.toolsReady) status.installError = "extract";
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
            if (!j.isFinished() && (j.track.id == track.id || j.video.id == video.id)) return 0;
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

std::optional<SongDownloader::Job> SongDownloader::jobForTrack(juce::int64 deezerId) const {
    const juce::ScopedLock sl(lock);
    for (auto it = jobs.rbegin(); it != jobs.rend(); ++it)
        if (it->track.id == deezerId) return *it;
    return std::nullopt;
}

SongDownloader::Status SongDownloader::getStatus() const {
    const juce::ScopedLock sl(lock);
    auto s = status;
    s.queued = s.finished = 0;
    for (const auto& j : jobs) {
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

juce::Array<juce::Time> SongDownloader::recentStarts() {
    const auto rows = library.withDatabase([](Database& db) { return db.getDownloadStartsSince(60); });
    juce::Array<juce::Time> starts;
    for (const auto& r : rows) starts.add(parseLocalTimestamp(r));
    return starts;
}

void SongDownloader::run() {
    services.workFolder.deleteRecursively();  // lo que quedó de una sesión anterior

    while (!threadShouldExit()) {
        if (installRequested.exchange(false)) {
            runInstall();
            continue;
        }

        int next = 0;
        {
            const juce::ScopedLock sl(lock);
            for (const auto& j : jobs)
                if (j.state == State::Queued) { next = j.id; break; }
            status.nextStart = juce::Time();
        }
        if (next == 0) {
            wait(-1);
            continue;
        }

        // YouTube pidió esperar: se espera (sin fallar la canción).
        const auto now = juce::Time::getCurrentTime();
        juce::Time pausedUntil;
        { const juce::ScopedLock sl(lock); pausedUntil = status.pausedUntil; }
        if (now < pausedUntil) {
            sendChangeMessage();
            wait((int) juce::jmin<juce::int64>(60000, (pausedUntil - now).inMilliseconds() + 100));
            continue;
        }

        // El ritmo: pausa desde la anterior (con algo de azar) y tope por hora.
        const int waitSeconds = SongDownload::secondsToWait(recentStarts(), now, services.minGapSeconds + jitterSeconds);
        if (waitSeconds > 0) {
            { const juce::ScopedLock sl(lock); status.nextStart = now + juce::RelativeTime::seconds(waitSeconds); }
            sendChangeMessage();
            wait(waitSeconds * 1000);
            continue;
        }

        if (!getTools().ready()) {
            finishJob(next, -1, State::Failed, SongDownload::Problem::Tools, {}, -1);
            continue;
        }
        maybeUpdateYtDlp();
        if (threadShouldExit()) break;
        jitterSeconds = services.maxJitterSeconds > 0 ? random.nextInt(services.maxJitterSeconds + 1) : 0;
        runJob(next);
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
            finishJob(jobId, recordId, State::Failed, SongDownload::Problem::Other, "import", -1);
            break;
    }
}
