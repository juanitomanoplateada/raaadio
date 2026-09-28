#include "BackupService.h"
#include "StationSettings.h"
#include "../library/MusicLibrary.h"

class BackupService::Worker : public juce::Thread {
public:
    Worker(BackupService& o, Job j) : juce::Thread("Station Backup"), owner(o), job(std::move(j)) {}
    void run() override { owner.runJob(job, *this); }

private:
    BackupService& owner;
    Job job;
};

BackupService::BackupService(MusicLibrary& lib, StationConfig& st) : library(lib), station(st) {
    stationId = juce::String(library.withDatabase([](Database& db) { return db.getStationId(); }));
}

BackupService::~BackupService() {
    stopTimer();
    cancelPendingUpdate();
    if (worker != nullptr) {
        worker->signalThreadShouldExit();  // la copia de la música se corta entre tramos
        worker->stopThread(30000);
    }
}

void BackupService::start() {
    startTimer(15 * 1000);  // el primer vistazo, cuando la emisora ya arrancó
}

juce::Time BackupService::lastScheduledTime(juce::Time now, int hour) {
    const juce::Time today(now.getYear(), now.getMonth(), now.getDayOfMonth(), juce::jlimit(0, 23, hour), 0, 0, 0, true);
    return now >= today ? today : today - juce::RelativeTime::days(1);
}

bool BackupService::isDue(juce::Time now, int hour, juce::Time lastBackup) {
    return lastBackup < lastScheduledTime(now, hour);
}

juce::File BackupService::getExternalStationFolder() const {
    const auto folder = station.get().backupExternalFolder;
    return folder.isEmpty() ? juce::File() : PersistenceManager::getExternalStationFolder(juce::File(folder), stationId);
}

BackupService::Status BackupService::getStatus() const {
    const juce::ScopedLock sl(lock);
    return status;
}

void BackupService::setStep(const juce::String& step) {
    {
        const juce::ScopedLock sl(lock);
        status.step = step;
    }
    triggerAsyncUpdate();
}

void BackupService::timerCallback() {
    if (getTimerInterval() != 60 * 1000) startTimer(60 * 1000);
    if (worker != nullptr && worker->isThreadRunning()) return;

    const auto s = station.get();
    if (!s.backupEnabled) return;

    Job job;
    job.mirrorLibrary = s.backupMirrorLibrary;
    job.externalStationFolder = getExternalStationFolder();
    if (job.externalStationFolder != juce::File()) job.externalRoot = juce::File(s.backupExternalFolder);

    const auto now = juce::Time::getCurrentTime();
    const auto latestLocal = PersistenceManager::getLatestBackupTime();
    if (isDue(now, s.backupHour, latestLocal)) {
        launch(job);
        return;
    }

    // Sin respaldo nuevo que hacer, pero el externo quedó pendiente (o es la primera vez en
    // esta sesión): se completa, sin insistir más de una vez cada media hora.
    if (job.externalStationFolder == juce::File()) return;
    bool externalPending;
    {
        const juce::ScopedLock sl(lock);
        externalPending = (!status.externalAttempted || status.lastExternalOk < latestLocal)
                       && (lastExternalAttempt == juce::Time()
                           || now - lastExternalAttempt >= juce::RelativeTime::minutes(externalRetryMinutes));
    }
    if (externalPending) {
        job.makeLocalBackup = false;
        launch(job);
    }
}

void BackupService::runNow() {
    if (worker != nullptr && worker->isThreadRunning()) return;
    const auto s = station.get();
    Job job;
    job.mirrorLibrary = s.backupMirrorLibrary;
    job.externalStationFolder = getExternalStationFolder();
    if (job.externalStationFolder != juce::File()) job.externalRoot = juce::File(s.backupExternalFolder);
    launch(job);
}

void BackupService::launch(const Job& job) {
    {
        const juce::ScopedLock sl(lock);
        status.running = true;
        status.step = TRANS(job.makeLocalBackup ? "backup_step_local" : "backup_step_external");
    }
    worker = std::make_unique<Worker>(*this, job);
    worker->startThread(juce::Thread::Priority::low);
    sendChangeMessage();
}

void BackupService::runJob(const Job& job, juce::Thread& thread) {
    const auto now = juce::Time::getCurrentTime();
    const auto localRoot = PersistenceManager::getBackupsFolder();

    if (job.makeLocalBackup) {
        const auto result = PersistenceManager::createBackup([this](const juce::File& target) { return library.backupDatabaseTo(target); });
        juce::Logger::writeToLog("BackupService: local backup " + (result.wasOk() ? juce::String("ok") : result.getErrorMessage()));
        const juce::ScopedLock sl(lock);
        status.lastOk = result.wasOk();
        status.lastError = result.getErrorMessage();
    }
    {
        const juce::ScopedLock sl(lock);
        status.lastRun = now;
    }

    if (job.externalStationFolder != juce::File() && !thread.threadShouldExit()) {
        {
            const juce::ScopedLock sl(lock);
            lastExternalAttempt = now;
        }
        juce::String error;
        PersistenceManager::MirrorReport mirror;

        if (!job.externalRoot.isDirectory()) {
            error = TRANS("backup_err_external_missing") + "\n" + job.externalRoot.getFullPathName();
        } else if (job.externalRoot == PersistenceManager::getLabRoot() || job.externalRoot.isAChildOf(PersistenceManager::getLabRoot())) {
            error = TRANS("backup_err_external_inside");
        } else {
            setStep(TRANS("backup_step_external"));
            const auto externalBackups = job.externalStationFolder.getChildFile("Backups");
            const auto latest = PersistenceManager::findLatestVerifiedBackup(localRoot);
            if (latest != juce::File()) {
                const auto copied = PersistenceManager::copyBackupTo(latest, localRoot, externalBackups);
                if (copied.failed()) error = copied.getErrorMessage();
                else PersistenceManager::pruneBackups(externalBackups, now);
            }

            if (error.isEmpty() && job.mirrorLibrary && !thread.threadShouldExit()) {
                setStep(TRANS("backup_step_music"));
                // Cada copia se compara con el SHA-256 que la base guarda de la pista.
                std::unordered_map<std::string, std::string> hashes;
                for (const auto& track : library.search("", ""))
                    if (!track.fileHash.empty())
                        hashes[juce::File(juce::String(track.path)).getFullPathName().toLowerCase().toStdString()] = track.fileHash;

                mirror = PersistenceManager::mirrorFolder(
                    PersistenceManager::getLibraryFolder(), job.externalStationFolder.getChildFile("Library"),
                    [&hashes](const juce::File& file) {
                        const auto it = hashes.find(file.getFullPathName().toLowerCase().toStdString());
                        return it == hashes.end() ? juce::String() : juce::String(it->second);
                    },
                    [&thread] { return !thread.threadShouldExit(); });

                if (mirror.error.isNotEmpty())
                    error = mirror.error;
                else if (mirror.failed > 0)
                    error = TRANS("backup_err_mirror_failed").replace("%1", juce::String(mirror.failed))
                            + "\n" + mirror.failures.joinIntoString(", ");
                juce::Logger::writeToLog("BackupService: music copy: " + juce::String(mirror.copied) + " copied, "
                                         + juce::String(mirror.unchanged) + " already there, " + juce::String(mirror.failed) + " failed");
            }
        }

        if (error.isNotEmpty())
            juce::Logger::writeToLog("BackupService: external backup failed: " + error);

        const juce::ScopedLock sl(lock);
        status.externalAttempted = true;
        status.externalOk = error.isEmpty() && !mirror.cancelled;
        status.externalError = error;
        status.mirror = mirror;
        if (status.externalOk) status.lastExternalOk = now;
    }

    {
        const juce::ScopedLock sl(lock);
        status.running = false;
        status.step = {};
    }
    triggerAsyncUpdate();
}
