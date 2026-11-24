#pragma once
#include <JuceHeader.h>
#include <optional>
#include <set>
#include "SongDownload.h"
#include "HttpTransport.h"
#include "../helpers/ProcessRunner.h"
#include "../library/MusicLibrary.h"

// Descarga en su propio hilo las canciones que el operador elige en la búsqueda en internet:
// yt-dlp baja el audio del video, FFmpeg lo pasa a MP3, se le escriben las etiquetas con los
// datos de Deezer y entra a la biblioteca por revisar (Library/_Entrada), en un lote propio.
// También baja los enlaces que se pegan en la pestaña Temporales (Facebook, Instagram, X...;
// LinkDownload): el audio, a MP3 con AudioConverter, entra como audio temporal.
// También instala las herramientas (verificando su SHA-256) y actualiza yt-dlp una vez al día.
//
// De a una descarga, con pausa entre una y otra. Lo de YouTube lleva además un tope por hora
// (SongDownload) y, si YouTube pide esperar, se detiene (la pausa se guarda: sigue aunque se
// cierre el programa) y espera su turno en vez de fallar; los enlaces de otros sitios siguen
// mientras tanto. Cerrar la ventana de búsqueda no detiene nada; cerrar el programa cancela la
// descarga en curso.
class SongDownloader : public juce::ChangeBroadcaster, private juce::Thread {
public:
    using Runner = std::function<int(const juce::StringArray& command, const ProcessRunner::LineCallback& onLine,
                                     const ProcessRunner::StopCheck& shouldStop)>;
    // Lo que se puede reemplazar en las pruebas (por omisión, lo de verdad).
    struct Services {
        HttpTransport::Transport transport;  // listas de checksums
        HttpTransport::FileFetcher fetch;    // los programas
        Runner run;                          // yt-dlp
        juce::File toolsFolder;              // DownloadTools::defaultFolder()
        juce::File workFolder;               // carpeta temporal del sistema
        std::optional<juce::String> pathVariable;  // el PATH del sistema
        int minGapSeconds = SongDownload::secondsBetween;
        int maxJitterSeconds = 10;           // más una espera al azar, para no parecer un reloj
    };

    // Dos constructores en vez de "Services services = {}": Services tiene inicializadores de
    // miembros y Clang (clang-tidy) no deja usarlos como argumento por omisión dentro de la clase.
    explicit SongDownloader(MusicLibrary& library);
    explicit SongDownloader(MusicLibrary& library, Services services);
    ~SongDownloader() override;

    //==========================================================================
    // Herramientas
    DownloadTools::Paths getTools() const;
    std::vector<DownloadTools::Asset> getMissingTools() const;
    // En el hilo: ver getStatus(). only: los nombres de lo que se instala (vacío: todo lo que
    // falta); la importación pide solo FFmpeg.
    void installTools(const juce::StringArray& only = {});

    // El aviso sobre derechos y términos de uso, aceptado una vez por emisora: uno para las
    // canciones y otro para los enlaces de redes (Kind se declara más abajo).
    bool isAcknowledged(bool links = false);
    void acknowledge(bool links = false);

    //==========================================================================
    // Fila
    enum class State { Queued, Downloading, Importing, Done, Duplicate, Failed, Cancelled };
    // Song: una canción de la búsqueda (va a la revisión). Temporary: un enlace pegado en la
    // pestaña Temporales (va a los audios temporales).
    enum class Kind { Song, Temporary };
    struct Job {
        int id = 0;
        Kind kind = Kind::Song;
        SongSearch::Track track;
        SongSearch::Video video;
        juce::String url, site;   // Temporary: el enlace y "Instagram", "Facebook"...
        juce::String title;       // Temporary: lo que dice yt-dlp, cuando empieza a bajar
        State state = State::Queued;
        double progress = 0.0;  // de la descarga, 0 a 1 (en 1 se está convirtiendo a MP3)
        SongDownload::Problem problem = SongDownload::Problem::None;
        juce::String detail;    // el error de yt-dlp, tal cual
        int trackId = -1;       // la pista en la biblioteca, al terminar
        bool isFinished() const { return state == State::Done || state == State::Duplicate || state == State::Failed || state == State::Cancelled; }
    };
    // Agrega una canción. 0 si el id del video no es válido o esa canción ya está en la fila.
    int enqueue(const SongSearch::Track& track, const SongSearch::Video& video);
    // Agrega un enlace para los audios temporales (lo pegado se limpia con
    // LinkDownload::normalise). 0 si no es un enlace o ya está en la fila.
    int enqueueLink(const juce::String& pasted);
    void cancel(int jobId);
    void retry(int jobId);
    void clearFinished();
    std::vector<Job> getJobs() const;
    std::vector<Job> getJobs(Kind kind) const;
    std::optional<Job> jobForTrack(juce::int64 deezerId) const;  // la canción más reciente

    struct Status {
        bool installing = false;
        juce::String installStep;      // "yt-dlp", "FFmpeg", "Deno"
        double installProgress = 0.0;  // -1: no se sabe
        juce::String installError;     // "download", "checksum", "extract" (o vacío)
        juce::String installErrorStep;
        bool toolsReady = false;
        juce::Time pausedUntil;        // YouTube pidió esperar; 0 si no
        SongDownload::Problem pauseReason = SongDownload::Problem::None;
        juce::Time nextStart;          // la siguiente espera su turno hasta entonces; 0 si no
        int queued = 0, finished = 0;  // canciones (los enlaces se cuentan con getJobs(Kind::Temporary))
        int activeJob = 0;             // la que se está bajando o importando
    };
    Status getStatus() const;

private:
    MusicLibrary& library;
    Services services;

    mutable juce::CriticalSection lock;
    std::vector<Job> jobs;
    std::set<int> cancelRequested;
    int nextJobId = 1;
    Status status;
    DownloadTools::Paths tools;
    std::atomic<bool> installRequested { false };
    juce::StringArray installOnly;
    int jitterSeconds = 0;
    juce::Random random;

    void run() override;
    void runInstall();
    void runJob(int jobId);
    void runLinkJob(int jobId);
    void maybeUpdateYtDlp();
    void findTools();
    // La siguiente que puede empezar ya (0 si ninguna) y, si no, cuántos segundos falta para
    // la primera que podrá.
    int pickNextJob(int& waitSeconds);

    Job* job(int jobId);  // con lock tomado
    void setState(int jobId, State state, double progress = -2.0);
    bool isCancelRequested(int jobId) const;
    void finishJob(int jobId, int recordId, State state, SongDownload::Problem problem, const juce::String& detail, int trackId);
    void pauseFor(SongDownload::Problem problem);
    // Los comienzos de la última hora: los de YouTube y el último de cualquier sitio.
    void recentStarts(juce::Array<juce::Time>& youtube, juce::Time& latestAny);
    juce::String getSetting(const std::string& key);
    void setSetting(const std::string& key, const juce::String& value);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongDownloader)
};
