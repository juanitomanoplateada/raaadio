#pragma once
#include <JuceHeader.h>

// Ajustes de emisión, guardados en la base de datos de la emisora (así viajan con ella y
// salen en cada respaldo): protección contra silencio, registro de lo emitido por hora,
// transmisión a un servidor Icecast, publicación de lo que suena y respaldos.
struct StationSettings {
    // Protección contra silencio
    bool deadAirEnabled = true;
    int deadAirSeconds = 20;
    juce::String emergencyCategory = "EMERGENCIA";

    // Registro de lo emitido: un archivo por hora
    bool airLogEnabled = false;
    int airLogRetentionDays = 30;

    // Transmisión por internet (Icecast: Zeno.fm, un servidor propio...)
    enum class StreamFormat { OggVorbis, Mp3 };
    bool streamEnabled = false;
    juce::String streamHost;
    int streamPort = 8000;
    juce::String streamMount = "/radio";
    juce::String streamUser = "source";
    juce::String streamPassword;  // en el archivo va cifrada (SecretStore)
    juce::String streamName = "RAAADIO";
    StreamFormat streamFormat = StreamFormat::Mp3;  // los archivos de versiones anteriores quedan en Ogg
    int streamQuality = 4;    // Ogg Vorbis: índice de calidad (4 ≈ 128 kbps)
    int streamBitrate = 128;  // MP3: kbps, tasa fija
    // Datos de la emisora que el servidor muestra en su directorio y en los reproductores.
    juce::String streamGenre;
    juce::String streamDescription;
    juce::String streamWebsite;
    bool streamPublic = false;  // pedir que la liste en directorios públicos (Icecast)
    bool streamTls = false;     // conexión cifrada con el servidor (ver StreamSocket)
    // Segundos que se retrasa el título en MP3, para que cambie cuando el oyente oye el
    // cambio de canción y no antes. En Ogg el título viaja con el audio y no hace falta.
    int streamMetadataDelay = 0;

    // Lo que suena (anterior, actual y siguientes) en JSON: siempre en el puerto local, y
    // además se envía a esta dirección (POST) en cada cambio, si hay una.
    juce::String nowPlayingPushUrl;

    // Respaldos: uno diario a backupHour (y al abrir, si se pasó la hora sin respaldar),
    // copiado también a backupExternalFolder (otro disco, un USB, una carpeta de OneDrive o
    // Google Drive) junto con la música si backupMirrorLibrary.
    bool backupEnabled = true;
    int backupHour = 3;
    juce::String backupExternalFolder;
    bool backupMirrorLibrary = true;

    // La clave se cifró en otro equipo o con otro usuario de Windows (la emisora se movió):
    // hay que volver a escribirla. Mientras tanto se conserva tal cual en el archivo.
    bool isStreamPasswordUnreadable() const { return streamPassword.isEmpty() && unreadablePassword.isNotEmpty(); }
    juce::String unreadablePassword;  // lo guardado que no se pudo descifrar

    juce::var toVar() const;
    static StationSettings fromVar(const juce::var& v);
};

class MusicLibrary;

// Dueño de los ajustes: los carga de la base, los guarda y avisa cuando cambian. Sin
// biblioteca (nullptr) solo viven en memoria.
class StationConfig : public juce::ChangeBroadcaster {
public:
    explicit StationConfig(MusicLibrary* library);

    const StationSettings& get() const { return settings; }
    void set(const StationSettings& newSettings);  // guarda y avisa

private:
    void save();

    MusicLibrary* library;
    StationSettings settings;
};
