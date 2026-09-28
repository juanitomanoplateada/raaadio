#pragma once
#include <JuceHeader.h>

// Alertas de la emisora que no son del aire en sí pero pueden terminar afectándolo: el disco
// de la emisora casi lleno y los respaldos atrasados o fallidos. La barra superior muestra
// la más grave; el detalle, al pasar el mouse.
namespace StationHealth {

enum class Level { Ok, Warning, Critical };

struct Alert {
    Level level = Level::Ok;
    juce::String text;         // corto, para la barra
    juce::StringArray details; // todas las alertas, explicadas
};

// Menos de 1 GB libre es crítico (la base y el registro del aire pueden dejar de guardarse);
// menos de 5 GB o del 5 % del disco, aviso.
constexpr juce::int64 criticalFreeBytes = (juce::int64) 1 << 30;
constexpr juce::int64 warningFreeBytes = (juce::int64) 5 << 30;
Level diskLevel(juce::int64 freeBytes, juce::int64 totalBytes);

// Un respaldo de más de 36 horas es un respaldo atrasado (el diario no se hizo).
constexpr int backupOverdueHours = 36;

struct Inputs {
    juce::Time now;
    juce::int64 diskFree = -1, diskTotal = -1;  // -1 = no se sabe
    bool backupEnabled = true;
    juce::Time latestBackup;
    bool lastBackupOk = true;
    juce::String lastBackupError;
    bool externalConfigured = false;
    bool externalAttempted = false;
    bool externalOk = true;
    juce::String externalError;
};

Alert evaluate(const Inputs& inputs);

}
