#include "PersistenceManager.h"
#include "../database/Database.h"

static juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "raaadio";
    options.filenameSuffix = ".settings";
    options.folderName = "raaadio";
    return options;
}

juce::File PersistenceManager::getDefaultLabRoot()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("raaadio");
}

static juce::File labRootOverride;
static juce::File sessionLabRoot;  // la carpeta de esta ejecución, leída una sola vez

void PersistenceManager::setLabRootOverride(const juce::File& temporaryRoot)
{
    labRootOverride = temporaryRoot;
}

bool PersistenceManager::isLabRootOverridden()
{
    return labRootOverride != juce::File();
}

juce::File PersistenceManager::getLabRoot()
{
    if (labRootOverride != juce::File())
        return labRootOverride;

    // Cambiar la carpeta en la configuración no mueve la emisora en marcha: la base de
    // datos ya está abierta en esta. La nueva rige desde el próximo inicio.
    if (sessionLabRoot == juce::File())
        sessionLabRoot = getConfiguredLabRoot();
    return sessionLabRoot;
}

juce::File PersistenceManager::getConfiguredLabRoot()
{
    if (labRootOverride != juce::File())  // pruebas y capturas: la configuración real no cuenta
        return labRootOverride;

    juce::ApplicationProperties appProperties;
    appProperties.setStorageParameters(settingsOptions());

    if (auto* props = appProperties.getUserSettings())
    {
        const juce::String savedPath = props->getValue("lab.root", "");
        if (savedPath.isNotEmpty())
            return labFolderFor(juce::File(savedPath));
    }

    return getDefaultLabRoot();
}

juce::File PersistenceManager::labFolderFor(const juce::File& chosenFolder)
{
    return chosenFolder.getFileName().equalsIgnoreCase("raaadio_lab") ? chosenFolder
                                                                      : chosenFolder.getChildFile("raaadio_lab");
}

bool PersistenceManager::containsStation(const juce::File& labRoot)
{
    return labRoot.getChildFile("Database/raaadio.db").existsAsFile();
}

void PersistenceManager::setLabRoot(const juce::File& newRoot)
{
    juce::ApplicationProperties appProperties;
    appProperties.setStorageParameters(settingsOptions());

    if (auto* props = appProperties.getUserSettings())
    {
        props->setValue("lab.root", labFolderFor(newRoot).getFullPathName());
        props->saveIfNeeded();
    }
}

bool PersistenceManager::isLabRootConfigured()
{
    if (labRootOverride != juce::File())
        return true;

    juce::ApplicationProperties appProperties;
    appProperties.setStorageParameters(settingsOptions());

    if (auto* props = appProperties.getUserSettings())
        return props->getValue("lab.root", "").isNotEmpty();

    return false;
}

bool PersistenceManager::legacyAutoBackupSetting()
{
    if (labRootOverride != juce::File())
        return true;

    juce::ApplicationProperties appProperties;
    appProperties.setStorageParameters(settingsOptions());
    if (auto* props = appProperties.getUserSettings())
        return props->getBoolValue("autobackup.enabled", true);
    return true;
}

juce::File PersistenceManager::getDatabaseFolder()   { return getLabRoot().getChildFile("Database"); }
juce::File PersistenceManager::getConfigFolder()     { return getLabRoot().getChildFile("Config"); }
juce::File PersistenceManager::getLibraryFolder()    { return getLabRoot().getChildFile("Library"); }
juce::File PersistenceManager::getRecordingsFolder() { return getLabRoot().getChildFile("Recordings"); }
juce::File PersistenceManager::getAirLogFolder()     { return getRecordingsFolder().getChildFile("AirLog"); }
juce::File PersistenceManager::getTemplatesFolder()  { return getLabRoot().getChildFile("Templates"); }
juce::File PersistenceManager::getBackupsFolder()    { return getLabRoot().getChildFile("Backups"); }
juce::File PersistenceManager::getLogsFolder()       { return getLabRoot().getChildFile("Logs"); }
juce::File PersistenceManager::getPendingRestoreFolder() { return getLabRoot().getChildFile("RestorePending"); }

static bool copyContents(const juce::File& from, const juce::File& to, const juce::StringArray& skipNames = {});

void PersistenceManager::migrateLegacyFolders()
{
    const auto legacyAirLog = getRecordingsFolder().getChildFile("Registro");
    const auto airLog = getAirLogFolder();
    if (legacyAirLog.isDirectory() && !airLog.exists())
    {
        const bool moved = legacyAirLog.moveFileTo(airLog);
        juce::Logger::writeToLog("PersistenceManager: Recordings/Registro -> Recordings/AirLog "
                                 + juce::String(moved ? "done" : "failed"));
    }
}

//==============================================================================
// Respaldos

namespace
{
    constexpr int manifestFormat = 1;

    bool isSQLiteDatabase(const juce::File& file)
    {
        juce::FileInputStream in(file);
        char header[16] = {};
        return in.openedOk() && in.read(header, 16) == 16 && std::memcmp(header, "SQLite format 3", 16) == 0;
    }

    // Abre una copia de la base solo para revisarla. "ok" si está sana.
    juce::String checkDatabaseFile(const juce::File& file, int* schemaVersion = nullptr)
    {
        if (!isSQLiteDatabase(file))
            return "not a SQLite database";
        Database db;
        if (!db.open(file.getFullPathName().toStdString()))
            return "cannot open";
        const auto result = juce::String(db.quickCheck());
        if (schemaVersion != nullptr) *schemaVersion = db.getSchemaVersion();
        return result;
    }

    juce::String sha256Of(const juce::File& file)
    {
        return juce::SHA256(file).toHexString();
    }

    // Copia un archivo sin cargar el disco de la emisora: en tramos de 1 MB con una pausa
    // corta, a un .partial que se renombra al final. Con expectedHash, la copia se lee de
    // nuevo y se compara; si no, se compara el tamaño.
    bool copyFileVerified(const juce::File& from, const juce::File& to, const juce::String& expectedHash,
                          const std::function<bool()>& keepGoing, juce::int64& bytesCopied)
    {
        const auto partial = to.getSiblingFile(to.getFileName() + ".partial");
        partial.deleteFile();
        if (!to.getParentDirectory().createDirectory().wasOk())
            return false;

        bool written = false;
        {
            juce::FileInputStream in(from);
            juce::FileOutputStream out(partial);
            if (in.openedOk() && out.openedOk())
            {
                constexpr int chunk = 1 << 20;
                juce::HeapBlock<char> buffer(chunk);
                written = true;
                for (;;)
                {
                    if (keepGoing && !keepGoing()) { written = false; break; }
                    const int n = in.read(buffer.get(), chunk);
                    if (n <= 0) break;
                    if (!out.write(buffer.get(), (size_t) n)) { written = false; break; }
                    bytesCopied += n;
                    juce::Thread::sleep(20);  // como mucho unos 50 MB/s: los decks leen del mismo disco
                }
                out.flush();
                written = written && out.getStatus().wasOk();
            }
        }
        if (!written)
        {
            partial.deleteFile();  // ya cerrado: no queda una copia a medias
            return false;
        }

        const bool same = partial.getSize() == from.getSize()
                       && (expectedHash.isEmpty() || sha256Of(partial).equalsIgnoreCase(expectedHash));
        if (!same || !partial.moveFileTo(to))
        {
            partial.deleteFile();
            return false;
        }
        return true;
    }

    juce::var readManifest(const juce::File& backupFolder)
    {
        const auto file = backupFolder.getChildFile("manifest.json");
        return file.existsAsFile() ? juce::JSON::parse(file.loadFileAsString()) : juce::var();
    }
}

// Lo que se respalda: la copia consistente de la base, que ya lleva todo el estado de la
// emisora. La música y las grabaciones no: pesan mucho y van por la copia externa.
juce::Result PersistenceManager::writeBackupContents(const juce::File& target, const DatabaseSnapshot& snapshotDatabase,
                                                      bool mustBeHealthy)
{
    if (!target.createDirectory())
        return juce::Result::fail(TRANS("backup_err_folder") + "\n" + target.getFullPathName());

    const auto database = target.getChildFile("raaadio.db");
    if (!snapshotDatabase || !snapshotDatabase(database))
        return juce::Result::fail(TRANS("backup_err_database"));

    int schemaVersion = 0;
    const auto integrity = checkDatabaseFile(database, &schemaVersion);
    if (mustBeHealthy && integrity != "ok")
        return juce::Result::fail(TRANS("backup_err_damaged") + "\n" + integrity);

    auto* entry = new juce::DynamicObject();
    entry->setProperty("path", "raaadio.db");
    entry->setProperty("size", database.getSize());
    entry->setProperty("sha256", sha256Of(database));

    auto* manifest = new juce::DynamicObject();
    manifest->setProperty("format", manifestFormat);
    manifest->setProperty("app", "RAAADIO");
    manifest->setProperty("appVersion", ProjectInfo::versionString);
    manifest->setProperty("created", juce::Time::getCurrentTime().toISO8601(true));
    manifest->setProperty("schemaVersion", schemaVersion);
    manifest->setProperty("integrity", integrity);
    manifest->setProperty("files", juce::Array<juce::var> { juce::var(entry) });

    if (!target.getChildFile("manifest.json").replaceWithText(juce::JSON::toString(juce::var(manifest))))
        return juce::Result::fail(TRANS("backup_err_folder") + "\n" + target.getFullPathName());
    return juce::Result::ok();
}

std::vector<PersistenceManager::BackupInfo> PersistenceManager::listBackups(const juce::File& backupsRoot)
{
    auto digits = [](const juce::String& s, int length) {
        return s.length() == length && s.containsOnly("0123456789");
    };

    // Backups/AAAA/MM/DD/HH_MM_SS (con "_2", "_3"... si hubo dos en el mismo segundo). Otras
    // carpetas (una copia a medias .partial, una carpeta del usuario) se ignoran.
    std::vector<BackupInfo> list;
    if (!backupsRoot.isDirectory()) return list;
    for (const auto& folder : backupsRoot.findChildFiles(juce::File::findDirectories, true))
    {
        const auto parts = juce::StringArray::fromTokens(
            folder.getRelativePathFrom(backupsRoot).replaceCharacter('\\', '/'), "/", "");
        if (parts.size() != 4 || !digits(parts[0], 4) || !digits(parts[1], 2) || !digits(parts[2], 2))
            continue;

        const auto clock = parts[3].substring(0, 8);
        const auto rest = parts[3].substring(8);
        if (clock.length() != 8 || clock[2] != '_' || clock[5] != '_'
            || !clock.removeCharacters("_").containsOnly("0123456789")
            || !(rest.isEmpty() || (rest.startsWithChar('_') && rest.substring(1).containsOnly("0123456789"))))
            continue;

        list.push_back({ folder, juce::Time(parts[0].getIntValue(), parts[1].getIntValue() - 1, parts[2].getIntValue(),
                                            clock.substring(0, 2).getIntValue(), clock.substring(3, 5).getIntValue(),
                                            clock.substring(6, 8).getIntValue(), 0, true) });
    }

    std::sort(list.begin(), list.end(), [](const BackupInfo& a, const BackupInfo& b) {
        return a.time != b.time ? a.time > b.time : a.folder.getFileName() > b.folder.getFileName();
    });
    return list;
}

juce::Result PersistenceManager::createBackup(const DatabaseSnapshot& snapshotDatabase, juce::File* createdFolder, bool mustBeHealthy)
{
    const auto now = juce::Time::getCurrentTime();
    auto folder = getBackupsFolder().getChildFile(now.formatted("%Y/%m/%d/%H_%M_%S"));
    for (int n = 2; folder.exists(); ++n)  // dos respaldos en el mismo segundo
        folder = folder.getSiblingFile(now.formatted("%H_%M_%S") + "_" + juce::String(n));

    const auto result = writeBackupContents(folder, snapshotDatabase, mustBeHealthy);
    if (result.failed())
    {
        folder.deleteRecursively();
        return result;
    }

    if (createdFolder != nullptr) *createdFolder = folder;
    pruneBackups(now);
    return juce::Result::ok();
}

juce::Result PersistenceManager::verifyBackup(const juce::File& folder)
{
    const auto database = folder.getChildFile("raaadio.db");
    if (!database.existsAsFile())
        return juce::Result::fail(TRANS("restore_err_nothing"));
    if (!isSQLiteDatabase(database))
        return juce::Result::fail(TRANS("restore_err_invalid_db"));

    const auto manifest = readManifest(folder);
    if (manifest.isObject())
    {
        if (manifest["integrity"].toString() != "ok")
            return juce::Result::fail(TRANS("backup_err_unverified"));
        if (const auto* files = manifest["files"].getArray())
        {
            for (const auto& entry : *files)
            {
                const auto file = folder.getChildFile(entry["path"].toString());
                if (!file.existsAsFile() || file.getSize() != (juce::int64) entry["size"]
                    || !sha256Of(file).equalsIgnoreCase(entry["sha256"].toString()))
                    return juce::Result::fail(TRANS("backup_err_mismatch").replace("%1", entry["path"].toString()));
            }
        }
    }

    const auto integrity = checkDatabaseFile(database);
    if (integrity != "ok")
        return juce::Result::fail(TRANS("backup_err_damaged") + "\n" + integrity);
    return juce::Result::ok();
}

juce::File PersistenceManager::findLatestVerifiedBackup(const juce::File& backupsRoot)
{
    for (const auto& backup : listBackups(backupsRoot))
        if (verifyBackup(backup.folder).wasOk())
            return backup.folder;
    return {};
}

std::vector<size_t> PersistenceManager::backupsToPrune(const std::vector<juce::Time>& newestFirst, juce::Time now)
{
    std::vector<bool> keep(newestFirst.size(), false);
    for (size_t i = 0; i < newestFirst.size() && i < (size_t) keepRecent; ++i)
        keep[i] = true;

    // El más reciente de cada día, semana (desde el lunes) y mes dentro de su ventana.
    auto keepNewestPer = [&](juce::RelativeTime window, const std::function<juce::String(juce::Time)>& period) {
        std::set<juce::String> seen;
        for (size_t i = 0; i < newestFirst.size(); ++i)
        {
            const auto t = newestFirst[i];
            if (t < now - window) continue;
            if (seen.insert(period(t)).second) keep[i] = true;
        }
    };
    keepNewestPer(juce::RelativeTime::days(keepDays), [](juce::Time t) { return t.formatted("%Y-%m-%d"); });
    keepNewestPer(juce::RelativeTime::weeks(keepWeeks), [](juce::Time t) {
        return (t - juce::RelativeTime::days((t.getDayOfWeek() + 6) % 7)).formatted("%Y-%m-%d");
    });
    keepNewestPer(juce::RelativeTime::days(keepMonths * 31), [](juce::Time t) { return t.formatted("%Y-%m"); });

    std::vector<size_t> prune;
    for (size_t i = 0; i < keep.size(); ++i)
        if (!keep[i]) prune.push_back(i);
    return prune;
}

void PersistenceManager::pruneBackups(const juce::File& root, juce::Time now)
{
    const auto backups = listBackups(root);
    std::vector<juce::Time> times;
    for (const auto& b : backups) times.push_back(b.time);

    for (const auto index : backupsToPrune(times, now))
    {
        const auto& folder = backups[index].folder;
        folder.deleteRecursively();

        // Las carpetas de día, mes y año que quedaron vacías.
        for (auto parent = folder.getParentDirectory();
             parent != root && parent.isAChildOf(root)
                 && parent.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0;
             parent = parent.getParentDirectory())
            parent.deleteFile();
    }
}

juce::Time PersistenceManager::getLatestBackupTime(const juce::File& backupsRoot)
{
    const auto backups = listBackups(backupsRoot);
    return backups.empty() ? juce::Time() : backups.front().time;
}

//==============================================================================
// Destino externo

juce::File PersistenceManager::getExternalStationFolder(const juce::File& externalRoot, const juce::String& stationId)
{
    return externalRoot.getChildFile("RAAADIO-" + stationId);
}

juce::Result PersistenceManager::copyBackupTo(const juce::File& backupFolder, const juce::File& localBackupsRoot,
                                              const juce::File& targetBackupsRoot)
{
    const auto destination = targetBackupsRoot.getChildFile(backupFolder.getRelativePathFrom(localBackupsRoot));
    if (destination.isDirectory() && verifyBackup(destination).wasOk())
        return juce::Result::ok();

    // Se copia a una carpeta .partial y se renombra al final: el respaldo aparece completo o
    // no aparece (un USB que se desconecta a mitad no deja un respaldo roto con nombre válido).
    const auto partial = destination.getSiblingFile(destination.getFileName() + ".partial");
    partial.deleteRecursively();
    destination.deleteRecursively();

    const auto manifest = readManifest(backupFolder);
    auto hashFor = [&manifest](const juce::String& relative) {
        if (const auto* files = manifest["files"].getArray())
            for (const auto& entry : *files)
                if (entry["path"].toString() == relative) return entry["sha256"].toString();
        return juce::String();
    };

    juce::int64 bytes = 0;
    for (const auto& file : backupFolder.findChildFiles(juce::File::findFiles, true))
    {
        const auto relative = file.getRelativePathFrom(backupFolder).replaceCharacter('\\', '/');
        if (!copyFileVerified(file, partial.getChildFile(relative), hashFor(relative), {}, bytes))
        {
            partial.deleteRecursively();
            return juce::Result::fail(TRANS("backup_err_external_copy") + "\n" + destination.getFullPathName());
        }
    }
    if (!partial.moveFileTo(destination))
    {
        partial.deleteRecursively();
        return juce::Result::fail(TRANS("backup_err_external_copy") + "\n" + destination.getFullPathName());
    }
    return verifyBackup(destination);
}

PersistenceManager::MirrorReport PersistenceManager::mirrorFolder(const juce::File& source, const juce::File& target,
                                                                  const std::function<juce::String(const juce::File&)>& expectedHash,
                                                                  const std::function<bool()>& keepGoing)
{
    MirrorReport report;
    if (!source.isDirectory()) return report;
    if (!target.createDirectory().wasOk())
    {
        report.error = TRANS("backup_err_external_missing");
        return report;
    }

    // Primero lo que falta, para saber si cabe.
    juce::Array<juce::File> pending;
    juce::int64 needed = 0;
    for (const auto& file : source.findChildFiles(juce::File::findFiles, true))
    {
        if (file.getFileName().endsWith(".partial") || file.isHidden()) continue;
        const auto copy = target.getChildFile(file.getRelativePathFrom(source));
        if (copy.existsAsFile() && copy.getSize() == file.getSize())
        {
            ++report.unchanged;
            continue;
        }
        pending.add(file);
        needed += file.getSize();
    }

    const auto available = target.getBytesFreeOnVolume();
    if (needed > 0 && available > 0 && available < needed + (64 << 20))
    {
        report.error = TRANS("backup_err_external_space").replace("%1", juce::File::descriptionOfSizeInBytes(needed))
                                                          .replace("%2", juce::File::descriptionOfSizeInBytes(available));
        report.failed = pending.size();
        return report;
    }

    for (const auto& file : pending)
    {
        if (keepGoing && !keepGoing())
        {
            report.cancelled = true;
            break;
        }
        const auto relative = file.getRelativePathFrom(source);
        const auto hash = expectedHash ? expectedHash(file) : juce::String();
        if (copyFileVerified(file, target.getChildFile(relative), hash, keepGoing, report.bytesCopied))
            ++report.copied;
        else if (keepGoing && !keepGoing())
            report.cancelled = true;
        else
        {
            ++report.failed;
            if (report.failures.size() < 20) report.failures.add(relative);
        }
    }
    return report;
}

//==============================================================================
// Salud de la base

PersistenceManager::DatabaseHealth PersistenceManager::checkDatabase(const juce::File& databaseFile)
{
    DatabaseHealth health;
    if (!databaseFile.existsAsFile()) return health;  // emisora nueva

    const auto problem = checkDatabaseFile(databaseFile);
    if (problem == "ok") return health;

    // Una base ocupada no está dañada: no se toca.
    if (problem.containsIgnoreCase("locked") || problem.containsIgnoreCase("busy"))
    {
        juce::Logger::writeToLog("PersistenceManager: database busy at startup: " + problem);
        return health;
    }

    health.damaged = true;
    health.problem = problem;
    juce::Logger::writeToLog("PersistenceManager: DATABASE DAMAGED: " + problem);

    const auto backup = findLatestVerifiedBackup(getBackupsFolder());
    if (backup == juce::File())
        return health;  // sin respaldo verificado: se sigue con la que hay y se avisa

    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    health.damagedCopy = databaseFile.getSiblingFile(databaseFile.getFileName() + ".damaged-" + stamp);
    const auto incoming = databaseFile.getSiblingFile(databaseFile.getFileName() + ".restoring");
    if (!backup.getChildFile("raaadio.db").copyFileTo(incoming) || !databaseFile.moveFileTo(health.damagedCopy))
    {
        incoming.deleteFile();
        return health;
    }
    for (auto* suffix : { "-journal", "-wal", "-shm" })
        databaseFile.getSiblingFile(databaseFile.getFileName() + suffix)
            .moveFileTo(health.damagedCopy.getSiblingFile(health.damagedCopy.getFileName() + suffix));

    if (!incoming.moveFileTo(databaseFile))
    {
        health.damagedCopy.copyFileTo(databaseFile);  // se deja todo como estaba
        return health;
    }

    // Un respaldo de una versión anterior trae la pauta, los ajustes y las plantillas en
    // archivos: se ponen donde la base (del esquema anterior) los importa al abrirse.
    if (backup.getChildFile("Config").isDirectory())
        copyContents(backup.getChildFile("Config"), getConfigFolder(),
                     juce::StringArray::fromTokens("queue.json|ad_breaks_done.json", "|", ""));
    if (backup.getChildFile("Templates").isDirectory())
        copyContents(backup.getChildFile("Templates"), getTemplatesFolder());

    health.recovered = true;
    health.restoredFrom = backup;
    juce::Logger::writeToLog("PersistenceManager: database restored from " + backup.getFullPathName()
                             + "; damaged copy kept as " + health.damagedCopy.getFileName());
    return health;
}

//==============================================================================

juce::Result PersistenceManager::exportPersistence(const juce::File& targetDirectory, const DatabaseSnapshot& snapshotDatabase,
                                                   juce::File* createdFolder)
{
    if (!targetDirectory.isDirectory())
        return juce::Result::fail(TRANS("restore_err_folder"));

    const auto folder = targetDirectory.getNonexistentChildFile(
        "RAAADIO_Export_" + juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H%M%S"), {}, false);

    const auto result = writeBackupContents(folder, snapshotDatabase, true);
    if (result.failed())
    {
        folder.deleteRecursively();
        return result;
    }

    if (createdFolder != nullptr) *createdFolder = folder;
    return juce::Result::ok();
}

//==============================================================================
// Restaurar

// Copia los archivos de una carpeta (y sus subcarpetas) sobre otra, reemplazando los que
// ya existan y dejando los demás. Los de skipNames no se copian.
static bool copyContents(const juce::File& from, const juce::File& to, const juce::StringArray& skipNames)
{
    bool ok = true;
    for (const auto& file : from.findChildFiles(juce::File::findFiles, true))
    {
        if (skipNames.contains(file.getFileName())) continue;
        const auto target = to.getChildFile(file.getRelativePathFrom(from));
        ok = target.getParentDirectory().createDirectory().wasOk() && file.copyFileTo(target) && ok;
    }
    return ok;
}

juce::Result PersistenceManager::stageRestore(const juce::File& sourceDirectory)
{
    if (!sourceDirectory.isDirectory())
        return juce::Result::fail(TRANS("restore_err_folder"));

    const auto database = sourceDirectory.getChildFile("raaadio.db");
    const auto config = sourceDirectory.getChildFile("Config");
    const auto templates = sourceDirectory.getChildFile("Templates");
    const auto legacyAds = sourceDirectory.getChildFile("ads_data.json");  // respaldos de versiones anteriores

    // Solo un respaldo que pasa la revisión: archivos iguales al manifiesto y base sana.
    const auto verified = verifyBackup(sourceDirectory);
    if (verified.failed())
        return verified;

    // Una restauración pendiente anterior se reemplaza por esta.
    const auto pending = getPendingRestoreFolder();
    pending.deleteRecursively();

    bool ok = pending.createDirectory().wasOk() && database.copyFileTo(pending.getChildFile("raaadio.db"));
    if (config.isDirectory())
        ok = ok && copyContents(config, pending.getChildFile("Config"));
    else if (legacyAds.existsAsFile())
        ok = ok && pending.getChildFile("Config").createDirectory().wasOk()
                && legacyAds.copyFileTo(pending.getChildFile("Config/ads_data.json"));
    if (templates.isDirectory())
        ok = ok && copyContents(templates, pending.getChildFile("Templates"));

    if (!ok)
    {
        pending.deleteRecursively();
        return juce::Result::fail(TRANS("restore_err_copy"));
    }
    return juce::Result::ok();
}

bool PersistenceManager::hasPendingRestore()
{
    return getPendingRestoreFolder().getChildFile("raaadio.db").existsAsFile();
}

juce::Result PersistenceManager::applyPendingRestore(juce::File* previousDatabase)
{
    const auto pending = getPendingRestoreFolder();
    if (!pending.getChildFile("raaadio.db").existsAsFile())
    {
        pending.deleteRecursively();  // restos de una preparación que no terminó
        return juce::Result::ok();
    }

    // Primero, un respaldo de lo que hay (aunque esté dañada: por eso se restaura). La
    // base todavía no está abierta: basta el archivo.
    const auto database = getDatabaseFolder().getChildFile("raaadio.db");
    juce::File saved;
    if (database.existsAsFile())
    {
        const auto backup = createBackup([database](const juce::File& target) { return database.copyFileTo(target); },
                                         &saved, false);
        if (backup.failed())
            return juce::Result::fail(TRANS("restore_err_backup") + ": " + backup.getErrorMessage());
    }

    // La restaurada se copia al lado y se pone en su lugar al final: si la copia falla, la
    // base anterior sigue intacta.
    getDatabaseFolder().createDirectory();
    const auto incoming = database.getSiblingFile(database.getFileName() + ".restoring");
    if (!pending.getChildFile("raaadio.db").copyFileTo(incoming))
    {
        incoming.deleteFile();
        return juce::Result::fail(TRANS("restore_err_database"));
    }

    // Un diario de SQLite que quedara de la base anterior se aplicaría sobre la restaurada.
    for (auto* suffix : { "-journal", "-wal", "-shm" })
        database.getSiblingFile(database.getFileName() + suffix).deleteFile();

    if (!incoming.moveFileTo(database))
    {
        incoming.deleteFile();
        return juce::Result::fail(TRANS("restore_err_database"));
    }

    // Respaldos de versiones anteriores: ajustes y plantillas en archivos, que la base
    // restaurada (del esquema anterior) importa al abrirse. La cola y las tandas ya
    // insertadas son del momento: se traen de la base anterior (previousDatabase).
    copyContents(pending.getChildFile("Config"), getConfigFolder(),
                 juce::StringArray::fromTokens("queue.json|ad_breaks_done.json", "|", ""));
    copyContents(pending.getChildFile("Templates"), getTemplatesFolder());

    if (previousDatabase != nullptr && saved != juce::File())
        *previousDatabase = saved.getChildFile("raaadio.db");

    pending.deleteRecursively();
    return juce::Result::ok();
}

//==============================================================================
// Mover la emisora

juce::Array<juce::File> PersistenceManager::labFilesToCopy()
{
    // La base de datos va aparte (copia consistente); el registro de la app se está
    // escribiendo y no hace falta; una restauración pendiente es de esta carpeta, y el
    // candado es de este equipo.
    const auto source = getLabRoot();
    juce::Array<juce::File> files;
    for (const auto& file : source.findChildFiles(juce::File::findFiles, true))
    {
        if (file.getParentDirectory() == getDatabaseFolder() && file.getFileName().startsWith("raaadio.db")) continue;
        if (file.isAChildOf(getLogsFolder()) || file.isAChildOf(getPendingRestoreFolder())) continue;
        if (file.getParentDirectory() == source && file.getFileName() == "raaadio.lock") continue;
        files.add(file);
    }
    return files;
}

juce::int64 PersistenceManager::getLabSizeToCopy()
{
    juce::int64 total = getDatabaseFolder().getChildFile("raaadio.db").getSize();
    for (const auto& file : labFilesToCopy())
        total += file.getSize();
    return total;
}

juce::Result PersistenceManager::copyLabTo(const juce::File& target, const DatabaseSnapshot& snapshotDatabase,
                                           const std::function<bool(double)>& keepGoing, juce::StringArray& skipped)
{
    const auto source = getLabRoot();
    if (target == source || target.isAChildOf(source) || source.isAChildOf(target))
        return juce::Result::fail(TRANS("lab_err_inside"));
    if (target.existsAsFile()
        || (target.isDirectory() && target.getNumberOfChildFiles(juce::File::findFilesAndDirectories) > 0))
        return juce::Result::fail(TRANS("lab_err_not_empty").replace("%1", target.getFullPathName()));

    // Si algo sale mal se borra lo copiado: la carpeta estaba vacía o no existía.
    auto abandon = [&target](const juce::String& message) {
        target.deleteRecursively();
        return juce::Result::fail(message);
    };

    const auto files = labFilesToCopy();
    juce::int64 total = 0, done = 0;
    for (const auto& file : files) total += file.getSize();

    if (!target.getChildFile("Database").createDirectory().wasOk())
        return abandon(TRANS("backup_err_folder") + "\n" + target.getFullPathName());
    if (!snapshotDatabase || !snapshotDatabase(target.getChildFile("Database/raaadio.db")))
        return abandon(TRANS("backup_err_database"));

    for (const auto& file : files)
    {
        if (keepGoing && !keepGoing(total > 0 ? (double) done / (double) total : 1.0))
            return abandon(TRANS("lab_err_cancelled"));

        const auto relative = file.getRelativePathFrom(source);
        const auto destination = target.getChildFile(relative);
        if (!destination.getParentDirectory().createDirectory().wasOk() || !file.copyFileTo(destination))
            skipped.add(relative);
        done += file.getSize();
    }

    for (auto* folder : { "Library", "Recordings", "Backups", "Logs" })
        target.getChildFile(folder).createDirectory();

    if (keepGoing) keepGoing(1.0);
    return juce::Result::ok();
}
