#pragma once
#include <JuceHeader.h>

// Ajustes de emisión que se guardan en Config/station.json (dentro del laboratorio, así
// viajan con la emisora): protección contra silencio, registro de lo emitido por hora y
// transmisión a un servidor Icecast.
struct StationSettings {
    // Protección contra silencio
    bool deadAirEnabled = true;
    int deadAirSeconds = 20;
    juce::String emergencyCategory = "EMERGENCIA";

    // Registro de lo emitido: un archivo por hora
    bool airLogEnabled = false;
    int airLogRetentionDays = 30;

    // Transmisión por internet (Icecast, Ogg Vorbis)
    bool streamEnabled = false;
    juce::String streamHost;
    int streamPort = 8000;
    juce::String streamMount = "/radio.ogg";
    juce::String streamUser = "source";
    juce::String streamPassword;  // en el archivo va cifrada (SecretStore)
    juce::String streamName = "RAAADIO";
    int streamQuality = 4;  // índice de calidad Ogg Vorbis (4 ≈ 128 kbps)

    // La clave se cifró en otro equipo o con otro usuario de Windows (la emisora se movió):
    // hay que volver a escribirla. Mientras tanto se conserva tal cual en el archivo.
    bool isStreamPasswordUnreadable() const { return streamPassword.isEmpty() && unreadablePassword.isNotEmpty(); }
    juce::String unreadablePassword;  // lo guardado que no se pudo descifrar

    juce::var toVar() const;
    static StationSettings fromVar(const juce::var& v);
};

// Dueño de los ajustes: los carga, los guarda y avisa cuando cambian.
class StationConfig : public juce::ChangeBroadcaster {
public:
    explicit StationConfig(juce::File file = {});

    const StationSettings& get() const { return settings; }
    void set(const StationSettings& newSettings);  // guarda y avisa

private:
    void save();

    juce::File file;
    StationSettings settings;
};
