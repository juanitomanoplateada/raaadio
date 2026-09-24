#include "PersistenceManager.h"

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

juce::File PersistenceManager::getDatabaseFolder()   { return getLabRoot().getChildFile("Database"); }
juce::File PersistenceManager::getConfigFolder()     { return getLabRoot().getChildFile("Config"); }
juce::File PersistenceManager::getLibraryFolder()    { return getLabRoot().getChildFile("Library"); }
juce::File PersistenceManager::getRecordingsFolder() { return getLabRoot().getChildFile("Recordings"); }
juce::File PersistenceManager::getTemplatesFolder()  { return getLabRoot().getChildFile("Templates"); }
juce::File PersistenceManager::getBackupsFolder()    { return getLabRoot().getChildFile("Backups"); }
juce::File PersistenceManager::getLogsFolder()       { return getLabRoot().getChildFile("Logs"); }
juce::File PersistenceManager::getPendingRestoreFolder() { return getLabRoot().getChildFile("RestorePending"); }

//==============================================================================
// Respaldos

// Lo que se respalda: la base de datos (copia consistente), los ajustes y las plantillas.
// La música y las grabaciones no: pesan mucho y no cambian.
juce::Result PersistenceManager::writeStationData(const juce::File& target, const DatabaseSnapshot& snapshotDatabase)
{
    if (!target.createDirectory())
        return juce::Result::fail(TRANS("backup_err_folder") + "\n" + target.getFullPathName());

    if (!snapshotDatabase || !snapshotDatabase(target.getChildFile("raaadio.db")))
        return juce::Result::fail(TRANS("backup_err_database"));

    for (const auto& folder : { getConfigFolder(), getTemplatesFolder() })
        if (folder.isDirectory() && !folder.copyDirectoryTo(target.getChildFile(folder.getFileName())))
            return juce::Result::fail(TRANS("backup_err_settings"));

    return juce::Result::ok();
}

namespace
{
    struct BackupFolder { juce::File folder; juce::Time time; };

    // Respaldos en Backups/AAAA/MM/DD/HH_MM_SS (con "_2", "_3"... si hubo dos en el mismo
    // segundo), del más reciente al más antiguo. Otras carpetas se ignoran.
    std::vector<BackupFolder> listBackups(const juce::File& backupsRoot)
    {
        auto digits = [](const juce::String& s, int length) {
            return s.length() == length && s.containsOnly("0123456789");
        };

        std::vector<BackupFolder> list;
        for (const auto& folder : backupsRoot.findChildFiles(juce::File::findDirectories, true))
        {
            const auto parts = juce::StringArray::fromTokens(
                folder.getRelativePathFrom(backupsRoot).replaceCharacter('\\', '/'), "/", "");
            if (parts.size() != 4 || !digits(parts[0], 4) || !digits(parts[1], 2) || !digits(parts[2], 2))
                continue;

            const auto clock = parts[3].substring(0, 8);
            if (clock.length() != 8 || clock[2] != '_' || clock[5] != '_'
                || !clock.removeCharacters("_").containsOnly("0123456789"))
                continue;

            list.push_back({ folder, juce::Time(parts[0].getIntValue(), parts[1].getIntValue() - 1, parts[2].getIntValue(),
                                                clock.substring(0, 2).getIntValue(), clock.substring(3, 5).getIntValue(),
                                                clock.substring(6, 8).getIntValue(), 0, true) });
        }

        std::sort(list.begin(), list.end(), [](const BackupFolder& a, const BackupFolder& b) { return a.time > b.time; });
        return list;
    }
}

juce::Result PersistenceManager::createBackup(const DatabaseSnapshot& snapshotDatabase, juce::File* createdFolder)
{
    const auto now = juce::Time::getCurrentTime();
    auto folder = getBackupsFolder().getChildFile(now.formatted("%Y/%m/%d/%H_%M_%S"));
    for (int n = 2; folder.exists(); ++n)  // dos respaldos en el mismo segundo
        folder = folder.getSiblingFile(now.formatted("%H_%M_%S") + "_" + juce::String(n));

    const auto result = writeStationData(folder, snapshotDatabase);
    if (result.failed())
    {
        folder.deleteRecursively();
        return result;
    }

    if (createdFolder != nullptr) *createdFolder = folder;
    pruneBackups(now);
    return juce::Result::ok();
}

void PersistenceManager::pruneBackups(juce::Time now)
{
    const auto root = getBackupsFolder();
    const auto backups = listBackups(root);
    const auto oldest = now - juce::RelativeTime::days(backupKeepDays);

    for (size_t i = backupKeepAtLeast; i < backups.size(); ++i)
    {
        if (backups[i].time >= oldest) continue;
        backups[i].folder.deleteRecursively();

        // Las carpetas de día, mes y año que quedaron vacías.
        for (auto parent = backups[i].folder.getParentDirectory();
             parent != root && parent.isAChildOf(root)
                 && parent.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0;
             parent = parent.getParentDirectory())
            parent.deleteFile();
    }
}

juce::Time PersistenceManager::getLatestBackupTime()
{
    const auto backups = listBackups(getBackupsFolder());
    return backups.empty() ? juce::Time() : backups.front().time;
}

bool PersistenceManager::isAutoBackupEnabled()
{
    juce::ApplicationProperties appProperties;
    appProperties.setStorageParameters(settingsOptions());

    if (auto* props = appProperties.getUserSettings())
        return props->getBoolValue("autobackup.enabled", true);

    return true;
}

void PersistenceManager::setAutoBackupEnabled(bool enabled)
{
    juce::ApplicationProperties appProperties;
    appProperties.setStorageParameters(settingsOptions());

    if (auto* props = appProperties.getUserSettings())
    {
        props->setValue("autobackup.enabled", enabled);
        props->saveIfNeeded();
    }
}

void PersistenceManager::performAutoBackupIfNeeded(const DatabaseSnapshot& snapshotDatabase)
{
    if (!isAutoBackupEnabled())
        return;

    juce::ApplicationProperties appProperties;
    appProperties.setStorageParameters(settingsOptions());

    if (auto* props = appProperties.getUserSettings())
    {
        const juce::String today = juce::Time::getCurrentTime().formatted("%Y-%m-%d");
        if (today == props->getValue("autobackup.last_date", ""))
            return;

        const auto res = createBackup(snapshotDatabase);
        if (res.wasOk())
        {
            props->setValue("autobackup.last_date", today);
            props->saveIfNeeded();
            juce::Logger::writeToLog("Daily Auto-Backup completed successfully.");
        }
        else
        {
            juce::Logger::writeToLog("Daily Auto-Backup failed: " + res.getErrorMessage());
        }
    }
}

juce::Result PersistenceManager::exportPersistence(const juce::File& targetDirectory, const DatabaseSnapshot& snapshotDatabase,
                                                   juce::File* createdFolder)
{
    if (!targetDirectory.isDirectory())
        return juce::Result::fail(TRANS("restore_err_folder"));

    const auto folder = targetDirectory.getNonexistentChildFile(
        "RAAADIO_Export_" + juce::Time::getCurrentTime().formatted("%Y-%m-%d_%H%M%S"), {}, false);

    const auto result = writeStationData(folder, snapshotDatabase);
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

static bool isSQLiteDatabase(const juce::File& file)
{
    juce::FileInputStream in(file);
    char header[16] = {};
    return in.openedOk() && in.read(header, 16) == 16 && std::memcmp(header, "SQLite format 3", 16) == 0;
}

// Copia los archivos de una carpeta (y sus subcarpetas) sobre otra, reemplazando los que
// ya existan y dejando los demás. Los de skipNames no se copian.
static bool copyContents(const juce::File& from, const juce::File& to, const juce::StringArray& skipNames = {})
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

    if (!database.existsAsFile())
        return juce::Result::fail(TRANS("restore_err_nothing"));
    if (!isSQLiteDatabase(database))
        return juce::Result::fail(TRANS("restore_err_invalid_db"));

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

juce::Result PersistenceManager::applyPendingRestore()
{
    const auto pending = getPendingRestoreFolder();
    if (!pending.getChildFile("raaadio.db").existsAsFile())
    {
        pending.deleteRecursively();  // restos de una preparación que no terminó
        return juce::Result::ok();
    }

    // Primero, un respaldo de lo que hay. La base todavía no está abierta: basta el archivo.
    const auto database = getDatabaseFolder().getChildFile("raaadio.db");
    const auto backup = createBackup([database](const juce::File& target) {
        return !database.existsAsFile() || database.copyFileTo(target);
    });
    if (backup.failed())
        return juce::Result::fail(TRANS("restore_err_backup") + ": " + backup.getErrorMessage());

    // Un diario de SQLite que quedara de la base anterior se aplicaría sobre la restaurada.
    for (auto* suffix : { "-journal", "-wal", "-shm" })
        database.getSiblingFile(database.getFileName() + suffix).deleteFile();

    getDatabaseFolder().createDirectory();
    if (!pending.getChildFile("raaadio.db").copyFileTo(database))
        return juce::Result::fail(TRANS("restore_err_database"));

    // Ajustes y plantillas del respaldo. La cola y las tandas ya insertadas son del momento:
    // se quedan como estaban.
    copyContents(pending.getChildFile("Config"), getConfigFolder(),
                 juce::StringArray::fromTokens("queue.json|ad_breaks_done.json", "|", ""));
    copyContents(pending.getChildFile("Templates"), getTemplatesFolder());

    pending.deleteRecursively();
    return juce::Result::ok();
}

//==============================================================================
// Mover la emisora

juce::Array<juce::File> PersistenceManager::labFilesToCopy()
{
    // La base de datos va aparte (copia consistente); el registro de la app se está
    // escribiendo y no hace falta; una restauración pendiente es de esta carpeta.
    const auto source = getLabRoot();
    juce::Array<juce::File> files;
    for (const auto& file : source.findChildFiles(juce::File::findFiles, true))
    {
        if (file.getParentDirectory() == getDatabaseFolder() && file.getFileName().startsWith("raaadio.db")) continue;
        if (file.isAChildOf(getLogsFolder()) || file.isAChildOf(getPendingRestoreFolder())) continue;
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

    for (auto* folder : { "Config", "Library", "Recordings", "Templates", "Backups", "Logs" })
        target.getChildFile(folder).createDirectory();

    if (keepGoing) keepGoing(1.0);
    return juce::Result::ok();
}
