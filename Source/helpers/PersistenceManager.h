#pragma once

#include <JuceHeader.h>

class PersistenceManager
{
public:
    // Laboratorio: la carpeta de la emisora (base de datos, música, grabaciones y
    // respaldos). Se decide una vez por ejecución; cambiarla rige desde el próximo inicio,
    // para que toda la app use siempre la misma carpeta.
    static juce::File getLabRoot();
    static juce::File getConfiguredLabRoot();  // la que se usará al próximo inicio
    static void setLabRoot(const juce::File& newRoot);
    static bool isLabRootConfigured();
    static juce::File labFolderFor(const juce::File& chosenFolder);  // chosen/raaadio_lab, o chosen si ya lo es
    static bool containsStation(const juce::File& labRoot);          // tiene Database/raaadio.db

    // Usa otra carpeta como laboratorio solo durante esta ejecución, sin tocar la
    // configuración guardada. Lo usan las pruebas y el modo de captura de UI para no leer
    // ni modificar los datos reales de la emisora.
    static void setLabRootOverride(const juce::File& temporaryRoot);
    static bool isLabRootOverridden();

    // Specific Subfolders
    static juce::File getDatabaseFolder();
    static juce::File getConfigFolder();     // solo versiones anteriores: su contenido pasa a la base
    static juce::File getLibraryFolder();
    static juce::File getRecordingsFolder();
    static juce::File getAirLogFolder();     // Recordings/AirLog: el registro de lo emitido
    static juce::File getTemplatesFolder();  // solo versiones anteriores
    static juce::File getBackupsFolder();
    static juce::File getLogsFolder();

    // Carpetas que cambiaron de nombre (Recordings/Registro pasó a Recordings/AirLog).
    static void migrateLegacyFolders();

    //==========================================================================
    // Respaldos

    // Escribe en target una copia consistente de la base de datos abierta
    // (MusicLibrary::backupDatabaseTo). Copiar el archivo mientras la app escribe en él
    // puede dejar la copia a medias.
    using DatabaseSnapshot = std::function<bool(const juce::File& target)>;

    // Un respaldo es una carpeta AAAA/MM/DD/HH_MM_SS con raaadio.db (toda la emisora: música
    // catalogada, pauta, cola, botonera, ajustes y plantillas) y manifest.json (versión,
    // fecha, resultado de la revisión de integridad y SHA-256 de cada archivo). Los de
    // versiones anteriores traen además Config/ y Templates/, y no tienen manifiesto.
    struct BackupInfo { juce::File folder; juce::Time time; };
    static std::vector<BackupInfo> listBackups(const juce::File& backupsRoot);  // el más reciente primero

    // Crea un respaldo en Backups/. La copia se revisa (PRAGMA quick_check) antes de darla
    // por buena; con mustBeHealthy en false (la copia de seguridad antes de restaurar, que
    // puede ser de una base dañada) se guarda igual, marcada como no verificada.
    static juce::Result createBackup(const DatabaseSnapshot& snapshotDatabase, juce::File* createdFolder = nullptr,
                                     bool mustBeHealthy = true);

    // Comprueba un respaldo: que los archivos coincidan con el manifiesto y que la base esté
    // sana. Los de versiones anteriores (sin manifiesto) solo pasan la revisión de la base.
    static juce::Result verifyBackup(const juce::File& backupFolder);
    static juce::File findLatestVerifiedBackup(const juce::File& backupsRoot);

    // Retención escalonada: siempre los 10 más recientes, y además el último de cada día de
    // las dos últimas semanas, de cada semana de los dos últimos meses y de cada mes del
    // último año. Lo que no es un respaldo de RAAADIO no se toca.
    static constexpr int keepRecent = 10, keepDays = 14, keepWeeks = 8, keepMonths = 12;
    static std::vector<size_t> backupsToPrune(const std::vector<juce::Time>& newestFirst, juce::Time now);
    static void pruneBackups(const juce::File& backupsRoot, juce::Time now);
    static void pruneBackups(juce::Time now) { pruneBackups(getBackupsFolder(), now); }
    static juce::Time getLatestBackupTime(const juce::File& backupsRoot);
    static juce::Time getLatestBackupTime() { return getLatestBackupTime(getBackupsFolder()); }

    // Destino externo (otro disco, un USB o una carpeta sincronizada con la nube): cada
    // emisora usa su propia carpeta RAAADIO-<id>, con Backups/ y Library/ adentro.
    static juce::File getExternalStationFolder(const juce::File& externalRoot, const juce::String& stationId);

    // Copia un respaldo local al mismo lugar bajo targetBackupsRoot (si ya está y verifica,
    // no hace nada) y comprueba la copia. Aparece completo o no aparece.
    static juce::Result copyBackupTo(const juce::File& backupFolder, const juce::File& localBackupsRoot,
                                     const juce::File& targetBackupsRoot);

    // Copia de la música: agrega lo nuevo y lo que cambió de tamaño, verificando cada copia
    // con el SHA-256 que la base ya guarda de la pista (o con el tamaño). Nunca borra: un
    // error en la emisora no se propaga a la copia.
    struct MirrorReport {
        int copied = 0, unchanged = 0, failed = 0;
        juce::int64 bytesCopied = 0;
        juce::StringArray failures;
        bool cancelled = false;
        juce::String error;  // sin espacio, destino inaccesible...
    };
    static MirrorReport mirrorFolder(const juce::File& source, const juce::File& target,
                                     const std::function<juce::String(const juce::File&)>& expectedHash,
                                     const std::function<bool()>& keepGoing);

    // Al abrir la emisora, antes de abrir la base: si está dañada y hay un respaldo
    // verificado, la dañada se aparta (raaadio.db.damaged-FECHA) y se pone la del respaldo.
    struct DatabaseHealth {
        bool damaged = false, recovered = false;
        juce::String problem;
        juce::File restoredFrom, damagedCopy;
    };
    static DatabaseHealth checkDatabase(const juce::File& databaseFile);

    // Exportar: lo mismo que un respaldo, en otra carpeta (para llevar los datos a otro equipo).
    static juce::Result exportPersistence(const juce::File& targetDirectory, const DatabaseSnapshot& snapshotDatabase,
                                          juce::File* createdFolder = nullptr);

    // Restaurar un respaldo o importar una exportación. La app en marcha tiene la base de
    // datos abierta, así que no se puede escribir encima: los datos se preparan en
    // RestorePending y se aplican al próximo inicio, antes de abrir nada y después de
    // respaldar lo que había (previousDatabase: esa copia, para traer de ella la cola).
    static juce::Result stageRestore(const juce::File& sourceDirectory);
    static bool hasPendingRestore();
    static juce::Result applyPendingRestore(juce::File* previousDatabase = nullptr);

    // Mover la emisora a otro disco: copia el laboratorio en uso a target (una carpeta vacía
    // o que no existe). keepGoing recibe el avance (0..1) y devuelve false para cancelar;
    // lo que no se pudo copiar (una grabación en curso) queda en skipped.
    static juce::int64 getLabSizeToCopy();
    static juce::Result copyLabTo(const juce::File& target, const DatabaseSnapshot& snapshotDatabase,
                                  const std::function<bool(double progress)>& keepGoing, juce::StringArray& skipped);

    // Versiones anteriores guardaban en la configuración de Windows si el respaldo
    // automático estaba activo; ahora es un ajuste de la emisora y esto solo lo migra.
    static bool legacyAutoBackupSetting();

private:
    static juce::File getDefaultLabRoot();
    static juce::File getPendingRestoreFolder();
    static juce::Result writeBackupContents(const juce::File& target, const DatabaseSnapshot& snapshotDatabase, bool mustBeHealthy);
    static juce::Array<juce::File> labFilesToCopy();
};
