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
// También instala las herramientas (verificando su SHA-256) y actualiza yt-dlp una vez al día.
//
// De a una canción, con pausa entre una y otra y un tope por hora (SongDownload). Si YouTube
// pide esperar, la fila se detiene (la pausa se guarda: sigue aunque se cierre el programa) y la
// canción espera su turno en vez de fallar. Cerrar la ventana de búsqueda no detiene nada; cerrar
// el programa cancela la descarga en curso.
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

    explicit SongDownloader(MusicLibrary& library, Services services = {});
    ~SongDownloader() override;

    //==========================================================================
    // Herramientas
    DownloadTools::Paths getTools() const;
    std::vector<DownloadTools::Asset> getMissingTools() const;
    void installTools();  // en el hilo: ver getStatus()

    // El aviso sobre derechos y términos de uso, aceptado una vez por emisora.
    bool isAcknowledged();
    void acknowledge();

    //==========================================================================
    // Fila
    enum class State { Queued, Downloading, Importing, Done, Duplicate, Failed, Cancelled };
    struct Job {
        int id = 0;
        SongSearch::Track track;
        SongSearch::Video video;
        State state = State::Queued;
        double progress = 0.0;  // de la descarga, 0 a 1 (en 1 se está convirtiendo a MP3)
        SongDownload::Problem problem = SongDownload::Problem::None;
        juce::String detail;    // el error de yt-dlp, tal cual
        int trackId = -1;       // la pista en la biblioteca, al terminar
        bool isFinished() const { return state == State::Done || state == State::Duplicate || state == State::Failed || state == State::Cancelled; }
    };
    // Agrega una canción. 0 si el id del video no es válido o esa canción ya está en la fila.
    int enqueue(const SongSearch::Track& track, const SongSearch::Video& video);
    void cancel(int jobId);
    void retry(int jobId);
    void clearFinished();
    std::vector<Job> getJobs() const;
    std::optional<Job> jobForTrack(juce::int64 deezerId) const;  // la más reciente

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
        int queued = 0, finished = 0;
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
    int jitterSeconds = 0;
    juce::Random random;

    void run() override;
    void runInstall();
    void runJob(int jobId);
    void maybeUpdateYtDlp();
    void findTools();

    Job* job(int jobId);  // con lock tomado
    void setState(int jobId, State state, double progress = -2.0);
    bool isCancelRequested(int jobId) const;
    void finishJob(int jobId, int recordId, State state, SongDownload::Problem problem, const juce::String& detail, int trackId);
    void pauseFor(SongDownload::Problem problem);
    juce::Array<juce::Time> recentStarts();
    juce::String getSetting(const std::string& key);
    void setSetting(const std::string& key, const juce::String& value);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongDownloader)
};
