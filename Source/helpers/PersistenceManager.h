#pragma once

#include <JuceHeader.h>

class PersistenceManager
{
public:
    // Laboratorio: la carpeta de la emisora (base de datos, ajustes, música, grabaciones y
    // respaldos). Se decide una vez por ejecución; cambiarla rige desde el próximo inicio,
    // para que toda la app use siempre la misma carpeta.
    static juce::File getLabRoot();
    static juce::File getConfiguredLabRoot();  // la que se usará al próximo inicio
    static void setLabRoot(const juce::File& newRoot);
    static bool isLabRootConfigured();
    static juce::File labFolderFor(const juce::File& chosenFolder);  // chosen/raaadio_lab, o chosen si ya lo es
    static bool containsStation(const juce::File& labRoot);          // tiene Database/raaadio.db

    // Usa otra carpeta como laboratorio solo durante esta ejecución, sin tocar la
    // configuración guardada. Lo usa el modo de captura de UI para no leer ni
    // modificar los datos reales de la emisora.
    static void setLabRootOverride(const juce::File& temporaryRoot);

    // Specific Subfolders
    static juce::File getDatabaseFolder();
    static juce::File getConfigFolder();
    static juce::File getLibraryFolder();
    static juce::File getRecordingsFolder();
    static juce::File getTemplatesFolder();
    static juce::File getBackupsFolder();
    static juce::File getLogsFolder();

    // Escribe en target una copia consistente de la base de datos abierta
    // (MusicLibrary::backupDatabaseTo). Copiar el archivo mientras la app escribe en él
    // puede dejar la copia a medias.
    using DatabaseSnapshot = std::function<bool(const juce::File& target)>;

    // Respaldos: base de datos, ajustes (Config) y plantillas en Backups/AAAA/MM/DD/HH_MM_SS.
    // Después de cada uno se borran los de más de backupKeepDays días, dejando siempre los
    // backupKeepAtLeast más recientes.
    static constexpr int backupKeepDays = 30;
    static constexpr int backupKeepAtLeast = 10;
    static juce::Result createBackup(const DatabaseSnapshot& snapshotDatabase, juce::File* createdFolder = nullptr);
    static void pruneBackups(juce::Time now);
    static juce::Time getLatestBackupTime();  // Time() si no hay ninguno

    // Respaldo diario al abrir la emisora: activado salvo que el usuario lo apague.
    static bool isAutoBackupEnabled();
    static void setAutoBackupEnabled(bool enabled);
    static void performAutoBackupIfNeeded(const DatabaseSnapshot& snapshotDatabase);

    // Exportar: lo mismo que un respaldo, en otra carpeta (para llevar los datos a otro equipo).
    static juce::Result exportPersistence(const juce::File& targetDirectory, const DatabaseSnapshot& snapshotDatabase,
                                          juce::File* createdFolder = nullptr);

    // Restaurar un respaldo o importar una exportación. La app en marcha tiene la base de
    // datos abierta y guarda sus ajustes al cerrar, así que no se puede escribir encima:
    // los datos se preparan en RestorePending y se aplican al próximo inicio, antes de abrir
    // nada y después de respaldar lo que había.
    static juce::Result stageRestore(const juce::File& sourceDirectory);
    static bool hasPendingRestore();
    static juce::Result applyPendingRestore();  // al iniciar, antes de abrir la base de datos

    // Mover la emisora a otro disco: copia el laboratorio en uso a target (una carpeta vacía
    // o que no existe). keepGoing recibe el avance (0..1) y devuelve false para cancelar;
    // lo que no se pudo copiar (una grabación en curso) queda en skipped.
    static juce::int64 getLabSizeToCopy();
    static juce::Result copyLabTo(const juce::File& target, const DatabaseSnapshot& snapshotDatabase,
                                  const std::function<bool(double progress)>& keepGoing, juce::StringArray& skipped);

private:
    static juce::File getDefaultLabRoot();
    static juce::File getPendingRestoreFolder();
    static juce::Result writeStationData(const juce::File& target, const DatabaseSnapshot& snapshotDatabase);
    static juce::Array<juce::File> labFilesToCopy();
};
