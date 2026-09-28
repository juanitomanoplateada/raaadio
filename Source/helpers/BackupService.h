#pragma once
#include <JuceHeader.h>
#include "PersistenceManager.h"

class MusicLibrary;
class StationConfig;

// Respaldos automáticos mientras la emisora está al aire, en otro hilo y con prioridad baja
// para no competir con los decks por el disco:
//  1. Todos los días a la hora configurada (y al abrir, si esa hora pasó sin respaldo), una
//     copia verificada de la base en Backups/.
//  2. Si hay destino externo, esa copia también allá, y la música que falte.
//  3. Si el destino externo no estaba (un USB desconectado), se reintenta cada 30 minutos.
class BackupService : public juce::ChangeBroadcaster, private juce::Timer, private juce::AsyncUpdater {
public:
    BackupService(MusicLibrary& library, StationConfig& station);
    ~BackupService() override;

    void start();   // revisa cada minuto si toca
    void runNow();  // "Respaldar ahora": copia local, externa y música

    struct Status {
        bool running = false;
        juce::String step;          // lo que está haciendo, para la interfaz
        juce::Time lastRun;         // último intento (local o externo)
        bool lastOk = true;
        juce::String lastError;     // del último respaldo local
        bool externalAttempted = false;
        bool externalOk = true;
        juce::Time lastExternalOk;
        juce::String externalError;
        PersistenceManager::MirrorReport mirror;
    };
    Status getStatus() const;

    // La carpeta de esta emisora en el destino externo (vacía si no hay destino).
    juce::File getExternalStationFolder() const;

    // La hora programada más reciente que ya pasó, y si con el último respaldo hecho a
    // lastBackup toca otro.
    static juce::Time lastScheduledTime(juce::Time now, int hour);
    static bool isDue(juce::Time now, int hour, juce::Time lastBackup);

    static constexpr int externalRetryMinutes = 30;

private:
    struct Job {
        bool makeLocalBackup = true;
        juce::File externalStationFolder;  // vacía: sin destino externo
        juce::File externalRoot;           // la carpeta que eligió el usuario
        bool mirrorLibrary = true;
    };
    class Worker;

    void timerCallback() override;
    void handleAsyncUpdate() override { sendChangeMessage(); }
    void launch(const Job& job);
    void runJob(const Job& job, juce::Thread& thread);  // en el hilo del respaldo
    void setStep(const juce::String& step);

    MusicLibrary& library;
    StationConfig& station;
    juce::String stationId;

    mutable juce::CriticalSection lock;
    Status status;
    juce::Time lastExternalAttempt;
    std::unique_ptr<Worker> worker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BackupService)
};
