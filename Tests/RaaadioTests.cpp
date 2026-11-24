// Pruebas de RAAADIO: persistencia, mezclador, análisis de audio, etiquetas e importación.
// Se ejecutan con:  build\RAAADIO_Tests_artefacts\Release\RAAADIO_Tests.exe  (o ctest)

#include <JuceHeader.h>
#include "../Source/database/Database.h"
#include "../Source/library/MusicLibrary.h"
#include "../Source/library/Playlist.h"
#include "../Source/library/PlaylistStore.h"
#include "../Source/library/HistoryManager.h"
#include "../Source/helpers/PersistenceManager.h"
#include "../Source/audio/Deck.h"
#include "../Source/audio/Mixer.h"
#include "../Source/audio/TrackAnalyzer.h"
#include "../Source/library/TagReader.h"
#include "../Source/library/TrackNaming.h"
#include "../Source/library/ImportReview.h"
#include "../Source/audio/AcousticFingerprint.h"
#include "../Source/network/Chromaprint.h"
#include "../Source/network/OnlineMetadata.h"
#include "../Source/network/OnlineIdentifier.h"
#include "../Source/network/SongSearch.h"
#include "../Source/network/SongSearcher.h"
#include "../Source/network/DownloadTools.h"
#include "../Source/network/SongDownload.h"
#include "../Source/network/SongDownloader.h"
#include "../Source/network/LinkDownload.h"
#include "../Source/library/TemporaryAudio.h"
#include "../Source/helpers/ProcessRunner.h"
#include "../Source/library/TagWriter.h"
#include "../Source/library/AudioConverter.h"
#include "../Source/library/AdvertisingManager.h"
#include "../Source/generator/PlaylistGenerator.h"
#include "../Source/autodj/QueueTiming.h"
#include "../Source/autodj/AdScheduler.h"
#include "../Source/autodj/OnAir.h"
#include "../Source/autodj/DeadAirGuard.h"
#include "../Source/autodj/AutoDJ.h"
#include "../Source/audio/AudioEngine.h"
#include "../Source/audio/AirLogRecorder.h"
#include "../Source/audio/RecordingManager.h"
#include "../Source/audio/CartPlayer.h"
#include "../Source/network/StreamEncoder.h"
#include "../Source/network/Mp3Encoder.h"
#include "../Source/network/NowPlaying.h"
#include "../Source/network/NowPlayingPublisher.h"
#include "../Source/network/ApiServer.h"
#include "../Source/helpers/StationSettings.h"
#include "../Source/helpers/SecretStore.h"
#include "../Source/helpers/BackupService.h"
#include "../Source/helpers/StationHealth.h"
#include "../Source/helpers/StationLock.h"
#include "../Source/generator/TemplateManager.h"
#include "../Source/ui/DragFormats.h"
#include "../Source/helpers/UiSnapshot.h"
#include "../Source/helpers/LanguageManager.h"
#include "../Source/helpers/ThemeManager.h"
#include "../Source/helpers/RaaadioLookAndFeel.h"
#include "../Source/MainComponent.h"
#include "../Source/ui/ImportReviewView.h"
#include "../Source/ui/CreditsEditor.h"
#include "../Source/ui/FileChoosers.h"
#include "../Source/ui/AudioSettingsDialog.h"
#include "../Source/ui/PlaylistComponent.h"
#include "../Source/ui/TrackDetailsDialog.h"
#include "../Source/ui/PlaylistGeneratorDialog.h"
#include "../Source/ui/LibraryComponent.h"
#include "../Source/ui/LibraryView.h"
#include "../Source/ui/CategorySidebarComponent.h"
#include "../Source/ui/AdvancedImportDialog.h"
#include "../Source/ui/AdsListView.h"
#include "../Source/ui/ScheduleView.h"
#include "../Source/ui/CartwallView.h"
#include "../Source/ui/DeckComponent.h"
#include "../Source/ui/WaveformComponent.h"
#include "../Source/ui/MixerComponent.h"
#include "../Source/ui/RecordingPanel.h"
#include "../Source/ui/StatusBar.h"
#include "../Source/ui/HistoryView.h"
#include "../Source/ui/ArtistsView.h"
#include "../Source/ui/OnlineSearchView.h"
#include "../Source/ui/TemporariesView.h"
#include "../Source/helpers/AppSettings.h"
#include "../Source/helpers/External.h"
#include <thread>

// Despacha el siguiente mensaje de la cola del sistema (false si no hay). Es la función que
// usa el bucle de mensajes de JUCE (juce_events); las pruebas la llaman para atender
// temporizadores y avisos sin abrir un bucle modal.
namespace juce::detail { bool dispatchNextMessageOnSystemQueue(bool returnIfNoPendingMessages); }

void installCrashReport();  // CrashReport.cpp: si las pruebas se caen, dice dónde
#include <mutex>

namespace
{
    // Carpeta temporal que se borra al terminar la prueba.
    struct TempLab
    {
        TempLab() : root(juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("raaadio_test_" + juce::String(juce::Random::getSystemRandom().nextInt64())))
        {
            root.createDirectory();
        }
        ~TempLab() { root.deleteRecursively(); }

        juce::File touch(const juce::String& relativePath) const
        {
            auto f = root.getChildFile(relativePath);
            f.getParentDirectory().createDirectory();
            f.replaceWithText("x");
            return f;
        }

        juce::File root;
    };

    TrackInfo makeTrack(const juce::String& path, const juce::String& artist = "ARTISTA", const juce::String& title = "TITULO")
    {
        TrackInfo t {};
        t.path = path.toStdString();
        t.artist = artist.toStdString();
        t.title = title.toStdString();
        t.duration = 180.0;
        t.normalizationGain = 1.0;
        return t;
    }

    std::string rawPathOf(const juce::File& dbFile, int id)
    {
        // Una conexión sin carpeta base devuelve la ruta tal como está guardada.
        Database raw;
        raw.open(dbFile.getFullPathName().toStdString());
        return raw.getTrackById(id).path;
    }

   #if JUCE_WINDOWS
    const juce::String oldLabRoot = "Z:\\old_lab";
   #else
    const juce::String oldLabRoot = "/nonexistent/old_lab";
   #endif

    // Audio de prueba: signal(t), con t en segundos, igual en los dos canales.
    using Signal = std::function<float(double)>;

    float tone(double t, float amplitude, double frequency = 1000.0)
    {
        return amplitude * (float) std::sin(juce::MathConstants<double>::twoPi * frequency * t);
    }

    // Tono de 440 Hz a -6 dBFS en los tramos [inicio, fin) indicados; silencio digital fuera.
    Signal toneBetween(std::initializer_list<std::pair<double, double>> ranges)
    {
        std::vector<std::pair<double, double>> r(ranges);
        return [r](double t) {
            for (const auto& [from, to] : r)
                if (t >= from && t < to) return tone(t, 0.5f, 440.0);
            return 0.0f;
        };
    }

    // Como el final de muchas canciones: tono de 440 Hz a -6 dBFS hasta fadeStart y un
    // fundido de 6.25 dB/s (50 dB en 8 s) hasta el silencio.
    Signal toneWithFadeOut(double fadeStart)
    {
        return [fadeStart](double t) {
            if (t < fadeStart) return tone(t, 0.5f, 440.0);
            if (t >= fadeStart + 8.0) return 0.0f;
            return tone(t, 0.5f * juce::Decibels::decibelsToGain(-6.25f * (float) (t - fadeStart)), 440.0);
        };
    }

    // Una persona hablando, grabada con el teléfono: sílabas de 0.25 s a -6 dBFS con pausas
    // de 0.15 s hasta 11.85 s, una última sílaba 10 dB más suave en 12.0-12.3 s y un zumbido
    // de fondo a -40 dBFS de principio a fin.
    float voiceWithHum(double t)
    {
        float speech = 0.0f;
        if (t < 11.85 && std::fmod(t, 0.4) < 0.25) speech = tone(t, 0.5f, 440.0);
        else if (t >= 12.0 && t < 12.3) speech = tone(t, 0.16f, 440.0);
        return speech + tone(t, 0.01f, 120.0);
    }

    juce::File writeAudio(juce::AudioFormat& format, const juce::File& file, double seconds, const Signal& signal,
                          const juce::StringPairArray& metadata = {}, int bits = 24, int quality = 0)
    {
        const double sampleRate = 44100.0;
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        auto stream = file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), sampleRate, 2, bits, metadata, quality));
        if (writer == nullptr) return {};
        (void) stream.release();  // ahora es del writer

        const int total = (int) std::lround(seconds * sampleRate);
        juce::AudioBuffer<float> block(2, 4096);
        for (int pos = 0; pos < total; pos += 4096) {
            const int n = std::min(4096, total - pos);
            for (int i = 0; i < n; ++i) {
                const float v = signal((pos + i) / sampleRate);
                block.setSample(0, i, v);
                block.setSample(1, i, v);
            }
            writer->writeFromAudioSampleBuffer(block, 0, n);
        }
        return file;
    }

    juce::File writeWav(const juce::File& file, double seconds, const Signal& signal, const juce::StringPairArray& metadata = {})
    {
        juce::WavAudioFormat wav;
        return writeAudio(wav, file, seconds, signal, metadata);
    }

    TrackAnalysis analyseFile(const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        return reader != nullptr ? TrackAnalyzer::analyse(*reader) : TrackAnalysis {};
    }

    // Hace sonar el mezclador (y sus decks) durante los segundos indicados. Los decks y la
    // botonera leen el archivo en otro hilo: al aire el audio va a tiempo real y ese hilo va
    // segundos por delante, pero aquí los bloques se piden mucho más rápido, así que antes
    // de cada uno se espera a que lo que suena esté en memoria.
    void pump(Mixer& mixer, double seconds, std::initializer_list<Deck*> decks = {}, CartPlayer* carts = nullptr)
    {
        juce::AudioBuffer<float> out(2, 512);
        for (int i = 0; i < (int) (seconds * 44100.0 / 512.0); ++i) {
            for (auto* deck : decks)
                if (deck->isPlaying()) deck->waitUntilBuffered(1024, 1000);
            if (carts != nullptr) carts->waitUntilBuffered(1024, 1000);
            out.clear();
            juce::AudioSourceChannelInfo info(&out, 0, 512);
            mixer.getNextAudioBlock(info);
        }
    }

    // Atiende la cola de mensajes durante unos milisegundos, como el bucle de la aplicación.
    // No se usa runDispatchLoopUntil porque exige JUCE_MODAL_LOOPS_PERMITTED, y con esa opción
    // JUCE muestra algunos avisos de forma síncrona: las pruebas ya no se comportarían como la
    // aplicación, que la tiene apagada.
    void dispatchMessagesFor(int milliseconds)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) milliseconds;
        do
        {
            while (juce::detail::dispatchNextMessageOnSystemQueue(true)) {}
            juce::Thread::sleep(1);
        } while (juce::Time::getMillisecondCounter() < end);
    }

    // Procesa los mensajes pendientes (temporizadores, avisos asíncronos) hasta que done()
    // o hasta que pase el tiempo. Las pruebas corren en el hilo de mensajes.
    bool pumpMessagesUntil(const std::function<bool()>& done, int timeoutMs = 30000)
    {
        const auto start = juce::Time::getMillisecondCounter();
        while (!done())
        {
            if ((int) (juce::Time::getMillisecondCounter() - start) > timeoutMs) return false;
            dispatchMessagesFor(20);
        }
        return true;
    }

    // Contesta el diálogo modal que esté abierto (un AlertWindow de JUCE; 1 = Aceptar) y
    // espera a que su respuesta se procese.
    bool answerModal(int result, int timeoutMs = 5000)
    {
        juce::Component* modal = nullptr;
        pumpMessagesUntil([&] { modal = juce::Component::getCurrentlyModalComponent(); return modal != nullptr; }, timeoutMs);
        if (modal == nullptr) return false;
        juce::Component::SafePointer<juce::Component> watched(modal);
        modal->exitModalState(result);
        const bool closed = pumpMessagesUntil([&] { return watched == nullptr; }, timeoutMs);
        dispatchMessagesFor(20);
        return closed;
    }

    // Una respuesta HTTP completa. claimedLength: el Content-Length que se anuncia (para
    // simular una conexión que se corta antes de tiempo).
    juce::String httpResponse(int status, const juce::String& body, int claimedLength = -1)
    {
        return "HTTP/1.1 " + juce::String(status) + (status == 200 ? " OK" : " Not Found") + "\r\n"
             + "Connection: close\r\nContent-Type: text/plain; charset=utf-8\r\n"
             + "Content-Length: " + juce::String(claimedLength >= 0 ? claimedLength : (int) body.getNumBytesAsUTF8())
             + "\r\n\r\n" + body;
    }

    // Un servidor HTTP mínimo en 127.0.0.1, para probar los pedidos de verdad sin internet.
    class LocalHttpServer : private juce::Thread
    {
    public:
        using Handler = std::function<juce::String(const juce::String& method, const juce::String& path, const juce::String& body)>;

        explicit LocalHttpServer(Handler h) : juce::Thread("Local HTTP server"), handler(std::move(h))
        {
            listener.createListener(0, "127.0.0.1");
            startThread();
        }
        ~LocalHttpServer() override
        {
            signalThreadShouldExit();
            listener.close();
            stopThread(3000);
        }
        juce::String url(const juce::String& path) const
        {
            return "http://127.0.0.1:" + juce::String(listener.getBoundPort()) + path;
        }

    private:
        void run() override
        {
            while (!threadShouldExit())
            {
                std::unique_ptr<juce::StreamingSocket> client(listener.waitForNextConnection());
                if (client == nullptr) break;
                juce::MemoryBlock received;
                juce::String text;
                int headerEnd = -1;
                char buffer[4096];
                while (client->waitUntilReady(true, 2000) == 1)
                {
                    const int n = client->read(buffer, (int) sizeof(buffer), false);
                    if (n <= 0) break;
                    received.append(buffer, (size_t) n);
                    text = received.toString();
                    headerEnd = text.indexOf("\r\n\r\n");
                    const int length = text.fromFirstOccurrenceOf("Content-Length:", false, true).getIntValue();
                    if (headerEnd >= 0 && (int) received.getSize() >= headerEnd + 4 + length) break;
                }
                if (headerEnd < 0) continue;
                const auto requestLine = text.upToFirstOccurrenceOf("\r\n", false, false);
                const auto method = requestLine.upToFirstOccurrenceOf(" ", false, false);
                const auto path = requestLine.fromFirstOccurrenceOf(" ", false, false).upToFirstOccurrenceOf(" ", false, false);
                const auto response = handler(method, path, text.substring(headerEnd + 4));
                client->write(response.toRawUTF8(), (int) response.getNumBytesAsUTF8());
                client->close();
            }
        }

        Handler handler;
        juce::StreamingSocket listener;
    };
}

//==============================================================================
class PortablePathTests : public juce::UnitTest
{
public:
    PortablePathTests() : juce::UnitTest("Rutas portables", "RAAADIO") {}

    void runTest() override
    {
        beginTest("Una base en una carpeta que no existe no se abre (y se avisa)");
        {
            TempLab lab;
            Database db;
            expect(!db.open(lab.root.getChildFile("NO EXISTE/raaadio.db").getFullPathName().toStdString()));
        }

        beginTest("Las pistas del laboratorio se guardan con ruta relativa y se leen absolutas");
        {
            TempLab lab;
            const auto dbFile = lab.root.getChildFile("Database/raaadio.db");
            dbFile.getParentDirectory().createDirectory();

            const auto song = lab.touch("Library/SALSA/GRUPO NICHE/GRUPO NICHE - UNA AVENTURA.ogg");
            Database db;
            db.open(dbFile.getFullPathName().toStdString());
            db.initSchema();
            db.setPathBase(lab.root);
            db.addTrack(makeTrack(song.getFullPathName()));

            const auto found = db.getTrackByPath(song.getFullPathName().toStdString());
            expect(found.id != -1, "la pista se encuentra por su ruta absoluta");
            expect(juce::File(found.path) == song, "la ruta devuelta es absoluta");
            expectEquals(juce::String(rawPathOf(dbFile, found.id)),
                         juce::String("Library/SALSA/GRUPO NICHE/GRUPO NICHE - UNA AVENTURA.ogg"));
        }

        beginTest("La base de datos sirve si el laboratorio se copia a otro lugar");
        {
            TempLab original, copy;
            const auto dbFile = original.root.getChildFile("raaadio.db");
            const auto song = original.touch("Library/BOLERO/X - Y.ogg");
            int id = -1;
            {
                Database db;
                db.open(dbFile.getFullPathName().toStdString());
                db.initSchema();
                db.setPathBase(original.root);
                db.addTrack(makeTrack(song.getFullPathName()));
                id = db.getTrackByPath(song.getFullPathName().toStdString()).id;
            }

            // Se copia el laboratorio completo (como al cambiar la USB de letra de unidad).
            expect(original.root.copyDirectoryTo(copy.root));
            Database moved;
            moved.open(copy.root.getChildFile("raaadio.db").getFullPathName().toStdString());
            moved.setPathBase(copy.root);
            const auto t = moved.getTrackById(id);
            expect(juce::File(t.path) == copy.root.getChildFile("Library/BOLERO/X - Y.ogg"));
            expect(juce::File(t.path).existsAsFile(), "el archivo existe en la nueva ubicación");
        }

        beginTest("Los archivos fuera del laboratorio conservan su ruta absoluta");
        {
            TempLab lab, elsewhere;
            const auto external = elsewhere.touch("musica/externa.mp3");
            Database db;
            db.open(lab.root.getChildFile("raaadio.db").getFullPathName().toStdString());
            db.initSchema();
            db.setPathBase(lab.root);
            db.addTrack(makeTrack(external.getFullPathName()));

            const auto t = db.getTrackByPath(external.getFullPathName().toStdString());
            expect(t.id != -1);
            expect(juce::File(t.path) == external);
            expect(juce::File::isAbsolutePath(juce::String(rawPathOf(lab.root.getChildFile("raaadio.db"), t.id))));
        }

        beginTest("La migración convierte rutas viejas, incluso de un laboratorio que cambió de unidad");
        {
            TempLab lab, elsewhere;
            const auto dbFile = lab.root.getChildFile("raaadio.db");
            const auto inside = lab.touch("Library/SALSA/A - B.ogg");
            lab.touch("Library/VALLENATO/C - D.ogg");
            const auto external = elsewhere.touch("E - F.mp3");
            const auto movedPath = oldLabRoot + juce::File::getSeparatorString() + "Library"
                                 + juce::File::getSeparatorString() + "VALLENATO"
                                 + juce::File::getSeparatorString() + "C - D.ogg";
            const auto lostPath = oldLabRoot + juce::File::getSeparatorString() + "otra" + juce::File::getSeparatorString() + "G - H.ogg";

            // Base de datos como la dejaba la versión anterior: todo con rutas absolutas.
            {
                Database legacy;
                legacy.open(dbFile.getFullPathName().toStdString());
                legacy.initSchema();
                legacy.addTrack(makeTrack(inside.getFullPathName(), "A", "B"));
                legacy.addTrack(makeTrack(movedPath, "C", "D"));
                legacy.addTrack(makeTrack(external.getFullPathName(), "E", "F"));
                legacy.addTrack(makeTrack(lostPath, "G", "H"));
            }

            Database db;
            db.open(dbFile.getFullPathName().toStdString());
            db.initSchema();
            db.setPathBase(lab.root);
            int backups = 0;
            auto backup = [&backups] { ++backups; };
            expectEquals(db.migrateToRelativePaths(backup), 2);
            expectEquals(db.migrateToRelativePaths(backup), 0, "la segunda vez no queda nada por migrar");
            expectEquals(backups, 1, "se respalda una sola vez, y solo si hay algo que migrar");

            auto rawPaths = [&] {
                juce::StringArray paths;
                for (int id = 1; id <= 4; ++id) paths.add(juce::String(rawPathOf(dbFile, id)));
                return paths;
            }();
            expectEquals(rawPaths[0], juce::String("Library/SALSA/A - B.ogg"));
            expectEquals(rawPaths[1], juce::String("Library/VALLENATO/C - D.ogg"));
            expectEquals(rawPaths[2], external.getFullPathName(), "lo externo no se toca");
            expectEquals(rawPaths[3], lostPath, "lo que no se encuentra no se toca");

            expect(juce::File(db.getTrackById(2).path).existsAsFile(), "la pista del laboratorio movido se recupera");
        }
    }
};

//==============================================================================
class PlayLogTests : public juce::UnitTest
{
public:
    PlayLogTests() : juce::UnitTest("Registro de emisión", "RAAADIO") {}

    void runTest() override
    {
        beginTest("Se guarda y se lee de la más reciente a la más antigua");
        TempLab lab;
        Database db;
        db.open(lab.root.getChildFile("raaadio.db").getFullPathName().toStdString());
        db.initSchema();

        for (int i = 1; i <= 3; ++i)
        {
            PlayLogEntry e;
            e.playedAt = "2026-09-22 10:0" + std::to_string(i) + ":00";
            e.trackId = (i == 2) ? -1 : i;
            e.artist = "ARTISTA " + std::to_string(i);
            e.title = juce::String::fromUTF8("CANCIÓN ").toStdString() + std::to_string(i);
            e.duration = 100.0 + i;
            e.deck = "Deck A";
            expect(db.addPlayLogEntry(e));
        }

        const auto recent = db.getRecentPlayLog(2);
        expectEquals((int) recent.size(), 2);
        expectEquals(juce::String(recent[0].playedAt), juce::String("2026-09-22 10:03:00"));
        expectEquals(recent[1].trackId, -1, "una pista sin id se guarda como NULL y vuelve como -1");
        expectEquals(juce::String::fromUTF8(recent[0].title.c_str()), juce::String::fromUTF8("CANCIÓN 3"));
        expectWithinAbsoluteError(recent[0].duration, 103.0, 0.001);
    }
};

//==============================================================================
class PlaylistTests : public juce::UnitTest
{
public:
    PlaylistTests() : juce::UnitTest("Cola de reproducción", "RAAADIO") {}

    void runTest() override
    {
        auto makeQueue = [](Playlist& p) {
            std::vector<TrackInfo> tracks;
            for (int i = 0; i < 5; ++i)
            {
                auto t = makeTrack("/m/" + juce::String(i) + ".ogg");
                t.id = i;
                tracks.push_back(t);
            }
            p.insertTracks(tracks, 0);
        };
        auto ids = [](const Playlist& p) {
            juce::String s;
            for (const auto& t : p.getQueue()) s << t.id;
            return s;
        };

        beginTest("Mover filas verificando que siguen siendo las mismas");
        {
            Playlist p;
            makeQueue(p);
            expect(p.moveTracks({ { 3, "/m/3.ogg" }, { 4, "/m/4.ogg" } }, 1));
            expectEquals(ids(p), juce::String("03412"));

            // Si la cola cambió (AutoDJ sacó una pista), una referencia vieja no mueve nada.
            p.popNext();
            expect(!p.moveTracks({ { 0, "/m/0.ogg" } }, 3));
            expectEquals(ids(p), juce::String("3412"));
        }

        beginTest("Subir y bajar un tramo de filas, y nada si la cola cambió");
        {
            Playlist p;
            makeQueue(p);
            // Subir las filas 2-3: se insertan antes de la 1. Bajarlas: antes de la 5 (última + 2).
            expect(p.moveTracks({ { 2, "/m/2.ogg" }, { 3, "/m/3.ogg" } }, 1));
            expectEquals(ids(p), juce::String("02314"));
            expect(p.moveTracks({ { 1, "/m/2.ogg" }, { 2, "/m/3.ogg" } }, 4));
            expectEquals(ids(p), juce::String("01234"));

            // AutoDJ sacó la primera: la fila que el operador veía en la posición 1 ya no es esa.
            p.popNext();
            expect(!p.moveTracks({ { 1, "/m/1.ogg" } }, 3));
            expectEquals(ids(p), juce::String("1234"));

            Playlist empty;
            expect(!empty.moveTracks({ { 0, "/m/0.ogg" } }, 2), "con la cola vacía no hace nada");
        }

        beginTest("Quitar solo lo que coincide y vaciar");
        {
            Playlist p;
            makeQueue(p);
            expectEquals(p.removeTracksIfMatching({ { 1, "/m/1.ogg" }, { 2, "/m/OTRA.ogg" } }), 1);
            expectEquals(ids(p), juce::String("0234"));

            const auto before = p.getLastModified();
            p.clear();
            expect(p.isEmpty());
            expect(p.getLastModified() != before);
        }
    }
};

//==============================================================================
// Biblioteca real en un laboratorio temporal, para la cola persistente y el historial.
struct LibraryFixture
{
    LibraryFixture()
    {
        PersistenceManager::setLabRootOverride(lab.root);
        PersistenceManager::getDatabaseFolder().createDirectory();
        PersistenceManager::getLibraryFolder().createDirectory();
        library.initialise(PersistenceManager::getDatabaseFolder().getChildFile("raaadio.db"));
        // Sin el FFmpeg del equipo: las pruebas de conversión ponen el suyo.
        library.setConverter({ [] { return juce::File(); }, {} });
    }

    TrackInfo add(const juce::String& relativePath, const juce::String& title)
    {
        const auto file = lab.touch(relativePath);
        library.getDatabase().addTrack(makeTrack(file.getFullPathName(), "ARTISTA", title));
        return library.getTrackByPath(file.getFullPathName());
    }

    TempLab lab;
    MusicLibrary library;
};

class PersistenceTests : public juce::UnitTest
{
public:
    PersistenceTests() : juce::UnitTest("Cola e historial persistentes", "RAAADIO") {}

    void runTest() override
    {
        beginTest("La cola se guarda y se restaura en el mismo orden");
        {
            LibraryFixture fx;
            const auto a = fx.add("Library/A.ogg", "A");
            const auto b = fx.add("Library/B.ogg", "B");
            const auto c = fx.add("Library/C.ogg", "C");
            {
                Playlist p;
                PlaylistStore store(p, fx.library);
                auto inBreak = b;
                inBreak.breakKey = "2026-09-22 12:00";
                p.insertTracks({ c, a, inBreak }, 0);
                store.saveIfChanged();
            }
            expectEquals((int) fx.library.getDatabase().loadQueue().size(), 3, "la cola queda en la base de datos");

            // Entre el cierre y el reinicio se borra una pista: se omite sin romper la cola.
            fx.library.deleteTrack(a.id, false);

            Playlist restored;
            PlaylistStore store(restored, fx.library);
            expectEquals(store.restore(), 2);
            auto q = restored.getQueue();
            expectEquals((int) q.size(), 2);
            expectEquals(juce::String(q[0].title), juce::String("C"));
            expectEquals(juce::String(q[1].title), juce::String("B"));
            expect(juce::File(q[0].path).existsAsFile(), "las rutas restauradas son válidas");
            expect(q[0].breakKey.empty());
            expectEquals(juce::String(q[1].breakKey), juce::String("2026-09-22 12:00"),
                         "la pieza de una tanda sigue marcada: la pauta sabe si se pierde");
        }

        beginTest("El historial sobrevive a un reinicio");
        {
            LibraryFixture fx;
            const auto a = fx.add("Library/A.ogg", "PRIMERA");
            const auto b = fx.add("Library/B.ogg", "SEGUNDA");
            {
                HistoryManager history;
                history.initialise(fx.library);
                history.addEntry(a, "Deck A");
                history.addEntry(b, "Deck B");
            }

            HistoryManager reopened;
            reopened.initialise(fx.library);
            const auto entries = reopened.getHistory();
            expectEquals((int) entries.size(), 2);
            expectEquals(juce::String(entries[0].track.title), juce::String("SEGUNDA"));
            expectEquals(entries[0].deckName, juce::String("Deck B"));
            expectEquals(entries[1].track.id, a.id);
            expect(entries[0].timestamp.toMilliseconds() > 0, "la hora se recupera");
        }
    }
};

//==============================================================================
class MixerCueTests : public juce::UnitTest
{
public:
    MixerCueTests() : juce::UnitTest("Preescucha (CUE)", "RAAADIO") {}

    void runTest() override
    {
        beginTest("CUE suena antes del fader, con la normalización aplicada una sola vez");

        LibraryFixture fx;

        // Seno de 440 Hz con amplitud 0.5 (RMS ~0.354), registrado con ganancia de normalización 2.
        const auto wav = fx.lab.root.getChildFile("Library/tono.wav");
        {
            juce::WavAudioFormat format;
            auto stream = wav.createOutputStream();
            std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), 44100.0, 2, 16, {}, 0));
            expect(writer != nullptr);
            (void) stream.release();  // ahora es del writer
            juce::AudioBuffer<float> tone(2, 44100 * 2);
            for (int i = 0; i < tone.getNumSamples(); ++i)
                for (int ch = 0; ch < 2; ++ch)
                    tone.setSample(ch, i, 0.5f * std::sin(juce::MathConstants<float>::twoPi * 440.0f * (float) i / 44100.0f));
            writer->writeFromAudioSampleBuffer(tone, 0, tone.getNumSamples());
        }
        auto info = makeTrack(wav.getFullPathName(), "PRUEBA", "TONO");
        info.normalizationGain = 2.0;
        info.analysisVersion = TrackAnalyzer::version;  // ya analizada: el deck usa esta ganancia
        fx.library.getDatabase().addTrack(info);

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        juce::AudioThumbnailCache cache(4);
        Deck deckA(1, fx.library, formats, cache);
        Deck deckB(2, fx.library, formats, cache);

        Mixer mixer;
        mixer.addDeck(&deckA);
        mixer.addDeck(&deckB);
        mixer.prepareToPlay(512, 44100.0);

        bool logged = false;
        deckB.onTrackLog = [&logged](const TrackInfo&) { logged = true; };

        deckB.loadTrack(wav);
        deckB.setRouting(false, true);      // solo preescucha
        mixer.setChannelVolume(1, -60.0f);  // fader cerrado
        deckB.play();
        expect(!logged, "la preescucha no cuenta como emisión");

        juce::AudioBuffer<float> mainOut(2, 512), cueOut(2, 512);
        double cueRms = 0.0;
        float mainPeak = 0.0f;
        int measured = 0;
        for (int i = 0; i < 40; ++i)
        {
            deckB.waitUntilBuffered(1024, 1000);
            mainOut.clear();
            juce::AudioSourceChannelInfo mainInfo(&mainOut, 0, 512);
            mixer.getNextAudioBlock(mainInfo);

            cueOut.clear();
            juce::AudioSourceChannelInfo cueInfo(&cueOut, 0, 512);
            mixer.readCueBlock(cueInfo);

            if (i >= 5)
            {
                cueRms += cueOut.getRMSLevel(0, 0, 512);
                mainPeak = juce::jmax(mainPeak, mainOut.getMagnitude(0, 512));
                ++measured;
            }
        }
        cueRms /= measured;

        // 0.5 de amplitud x 2 de normalización = 1.0 → RMS 0.707. Con la ganancia aplicada
        // dos veces serían 1.41; con la señal después del fader cerrado, 0.
        expectWithinAbsoluteError(cueRms, 0.7071, 0.03);
        expect(mainPeak < 1.0e-4f, "lo que no está al aire no sale por la salida principal");

        deckB.setRouting(true, true);
        expect(logged, "al pasar al aire se registra la emisión");
    }
};

//==============================================================================
class TrackAnalyzerTests : public juce::UnitTest
{
public:
    TrackAnalyzerTests() : juce::UnitTest("Análisis de audio", "RAAADIO") {}

    void runTest() override
    {
        beginTest("Sonoridad EBU R 128: un seno de 1 kHz a -23 dBFS mide -23 LUFS");
        {
            TempLab lab;
            const float amplitude = juce::Decibels::decibelsToGain(-23.0f);
            const auto a = analyseFile(writeWav(lab.root.getChildFile("ebu.wav"), 20.0,
                                                [amplitude](double t) { return tone(t, amplitude); }));
            expect(a.valid);
            expectWithinAbsoluteError(a.loudnessLufs, -23.0, 0.1);
            expectWithinAbsoluteError(a.truePeakDb, -23.0, 0.3);
            expectWithinAbsoluteError(a.lengthSeconds, 20.0, 0.001);
        }

        beginTest("Pico real: un pico que cae entre dos muestras se mide igual");
        {
            // Seno de fs/4 desfasado 45°: todas las muestras valen ±0.354 (-9 dBFS), pero la
            // onda llega a 0.5 (-6 dBTP) entre ellas.
            TempLab lab;
            const auto a = analyseFile(writeWav(lab.root.getChildFile("entre_muestras.wav"), 5.0, [](double t) {
                return 0.5f * (float) std::sin(juce::MathConstants<double>::twoPi * 11025.0 * t + juce::MathConstants<double>::pi / 4.0);
            }));
            expectWithinAbsoluteError(a.truePeakDb, -6.02, 0.3);
        }

        beginTest("Dos pistas que la normalización por pico dejaba 8 dB aparte quedan al mismo volumen");
        {
            TempLab lab;
            const auto loud = analyseFile(writeWav(lab.root.getChildFile("fuerte.wav"), 10.0,
                                                   [](double t) { return tone(t, 0.5f); }));
            const auto soft = analyseFile(writeWav(lab.root.getChildFile("suave.wav"), 10.0,
                                                   [](double t) { return tone(t, 0.1f); }));
            auto onAir = [](const TrackAnalysis& a) {
                return a.loudnessLufs + juce::Decibels::gainToDecibels(TrackAnalyzer::gainFor(a.loudnessLufs, a.truePeakDb));
            };
            expectWithinAbsoluteError(onAir(loud), TrackAnalyzer::targetLufs, 0.1);
            expectWithinAbsoluteError(onAir(soft), TrackAnalyzer::targetLufs, 0.1);
        }

        beginTest("Ganancia: se sube solo hasta el techo de pico, y nunca se baja una pista suave");
        {
            auto gainDb = [](double lufs, double peak) {
                return juce::Decibels::gainToDecibels(TrackAnalyzer::gainFor(lufs, peak));
            };
            expectWithinAbsoluteError(gainDb(-8.0, -0.1), -6.0, 0.01, "una pista fuerte se baja a -14 LUFS");
            expectWithinAbsoluteError(gainDb(-22.0, -10.0), 8.0, 0.01, "una suave se sube");
            expectWithinAbsoluteError(gainDb(-20.0, -2.0), 1.0, 0.01, "hasta que su pico llega a -1 dBTP");
            expectWithinAbsoluteError(gainDb(-20.0, 0.5), 0.0, 0.01, "con picos ya altos se deja igual");
            expectWithinAbsoluteError(gainDb(-45.0, -30.0), 12.0, 0.01, "tope de +12 dB");
            expectEquals(TrackAnalyzer::gainFor(-70.0, -70.0), 1.0, "un archivo en silencio no se toca");
        }

        beginTest("Puntos de entrada y salida; una pausa de 2 s al 80 % de la canción no la corta");
        {
            // 1.5 s de silencio, tono hasta 40 s con una pausa en 32-34 s (el detector anterior
            // saltaba ahí a la siguiente pista), y 3 s de silencio al final.
            TempLab lab;
            const auto a = analyseFile(writeWav(lab.root.getChildFile("pausa.wav"), 43.0,
                                                toneBetween({ { 1.5, 32.0 }, { 34.0, 40.0 } })));
            expect(a.valid);
            expectWithinAbsoluteError(a.cueIn, 1.48, 0.03);
            expectWithinAbsoluteError(a.cueOut, 40.02, 0.03);
        }

        const double w = 0.02;
        auto levels = [w](std::initializer_list<std::pair<double, float>> segments) {
            std::vector<float> result;
            for (const auto& [seconds, levelDb] : segments)
                result.insert(result.end(), (size_t) std::lround(seconds / w),
                              levelDb <= -100.0f ? 0.0f : juce::Decibels::decibelsToGain(levelDb));
            return result;
        };
        double cueIn = -1.0, cueOut = -1.0;

        beginTest("Una pista oculta tras un silencio largo no deja la radio muda");
        TrackAnalyzer::findCuePoints(levels({ { 35.0, -12.0f }, { 20.0, -120.0f }, { 10.0, -12.0f } }), w, 65.0, cueIn, cueOut);
        expectWithinAbsoluteError(cueIn, 0.0, 0.001);
        expectWithinAbsoluteError(cueOut, 35.02, 0.001, "la pista termina donde empieza el silencio");

        beginTest("Un pasaje muy suave no se confunde con silencio");
        TrackAnalyzer::findCuePoints(levels({ { 40.0, -12.0f }, { 10.0, -55.0f }, { 10.0, -12.0f } }), w, 60.0, cueIn, cueOut);
        expectWithinAbsoluteError(cueOut, 60.0, 0.001);

        beginTest("Los primeros 30 s de sonido se protegen");
        TrackAnalyzer::findCuePoints(levels({ { 2.0, -12.0f }, { 10.0, -120.0f }, { 180.0, -12.0f } }), w, 192.0, cueIn, cueOut);
        expectWithinAbsoluteError(cueOut, 192.0, 0.001, "una entrada hablada seguida de silencio no corta la canción");

        beginTest("Un archivo en silencio no se recorta");
        TrackAnalyzer::findCuePoints(levels({ { 10.0, -120.0f } }), w, 10.0, cueIn, cueOut);
        expectWithinAbsoluteError(cueIn, 0.0, 0.001);
        expectWithinAbsoluteError(cueOut, 10.0, 0.001);

        beginTest("Punto de mezcla: lo que termina en seco no se pisa; un fundido se pisa cuando ya bajó");
        {
            expectWithinAbsoluteError(TrackAnalyzer::findMixPoint(levels({ { 30.0, -10.0f } }), w, -10.0, 0.0, 30.0),
                                      30.0, 0.001, "termina en seco: la siguiente entra al final");

            // 30 s a -10 dB y un fundido de 50 dB en 8 s: baja de -28 dB a los 2.88 s.
            auto fade = levels({ { 30.0, -10.0f } });
            for (int k = 0; k < 400; ++k)
                fade.push_back(juce::Decibels::decibelsToGain(-10.0f - 50.0f * (float) k / 400.0f));
            const double mix = TrackAnalyzer::findMixPoint(fade, w, -10.0, 0.0, 38.0);
            expect(mix > 32.88 && mix < 33.2, "entra cuando el fundido ya bajó 18 dB: " + juce::String(mix, 3));

            expectWithinAbsoluteError(TrackAnalyzer::findMixPoint(levels({ { 60.0, -10.0f }, { 20.0, -35.0f } }), w, -10.0, 0.0, 80.0),
                                      70.0, 0.001, "una salida larga y suave se pisa como mucho 10 s antes del final");
            expectWithinAbsoluteError(TrackAnalyzer::findMixPoint(levels({ { 10.0, -120.0f } }), w, -70.0, 0.0, 10.0),
                                      10.0, 0.001, "un archivo en silencio no se adelanta");
        }

        beginTest("Una voz con ruido de fondo: la siguiente entra después de su última palabra, aunque sea más suave");
        {
            TempLab lab;
            const auto a = analyseFile(writeWav(lab.root.getChildFile("voz.wav"), 15.0, voiceWithHum));
            expect(a.valid);
            expectWithinAbsoluteError(a.cueOut, 15.0, 0.03, "el zumbido sigue hasta el final del archivo");
            expect(a.mixPoint >= 12.3 && a.mixPoint < 12.6, "la última sílaba termina a los 12.3 s: " + juce::String(a.mixPoint, 3));
        }
    }
};

//==============================================================================
// Etiquetas armadas byte a byte, como las escriben los programas.
namespace
{
    using Bytes = std::vector<juce::uint8>;

    Bytes latin1Text(const Bytes& text) { Bytes b { 0x00 }; b.insert(b.end(), text.begin(), text.end()); return b; }

    Bytes utf8Text(const juce::String& text)
    {
        Bytes b { 0x03 };
        auto utf8 = text.toRawUTF8();
        b.insert(b.end(), utf8, utf8 + strlen(utf8));
        return b;
    }

    Bytes utf16Text(const juce::String& text)  // con BOM, orden de Windows
    {
        Bytes b { 0x01, 0xFF, 0xFE };
        for (auto c : text) {
            b.push_back((juce::uint8) (c & 0xff));
            b.push_back((juce::uint8) ((c >> 8) & 0xff));
        }
        return b;
    }

    struct Id3Builder
    {
        explicit Id3Builder(int v) : major(v) {}

        void frame(const char* id, const Bytes& content, bool plainSize = false)
        {
            body.insert(body.end(), id, id + (major == 2 ? 3 : 4));
            const auto n = (juce::uint32) content.size();
            if (major == 2) {
                body.insert(body.end(), { (juce::uint8) (n >> 16), (juce::uint8) (n >> 8), (juce::uint8) n });
            } else {
                if (major == 3 || plainSize)
                    body.insert(body.end(), { (juce::uint8) (n >> 24), (juce::uint8) (n >> 16), (juce::uint8) (n >> 8), (juce::uint8) n });
                else
                    body.insert(body.end(), { (juce::uint8) ((n >> 21) & 0x7f), (juce::uint8) ((n >> 14) & 0x7f),
                                              (juce::uint8) ((n >> 7) & 0x7f), (juce::uint8) (n & 0x7f) });
                body.insert(body.end(), { 0x00, 0x00 });
            }
            body.insert(body.end(), content.begin(), content.end());
        }

        // unsynchronise: inserta 0x00 después de cada 0xFF (bandera 0x80 de la cabecera).
        Bytes build(bool unsynchronise = false) const
        {
            Bytes b = body;
            b.insert(b.end(), 32, 0x00);  // relleno
            if (unsynchronise) {
                Bytes u;
                for (auto byte : b) { u.push_back(byte); if (byte == 0xFF) u.push_back(0x00); }
                b = u;
            }
            const auto n = (juce::uint32) b.size();
            Bytes tag { 'I', 'D', '3', (juce::uint8) major, 0x00, (juce::uint8) (unsynchronise ? 0x80 : 0x00),
                        (juce::uint8) ((n >> 21) & 0x7f), (juce::uint8) ((n >> 14) & 0x7f),
                        (juce::uint8) ((n >> 7) & 0x7f), (juce::uint8) (n & 0x7f) };
            tag.insert(tag.end(), b.begin(), b.end());
            return tag;
        }

        int major;
        Bytes body;
    };

    Bytes vorbisComment(std::initializer_list<const char*> entries)
    {
        Bytes b;
        auto le32 = [&b](juce::uint32 v) { for (int i = 0; i < 4; ++i) b.push_back((juce::uint8) (v >> (8 * i))); };
        const char* vendor = "RAAADIO test";
        le32((juce::uint32) strlen(vendor));
        b.insert(b.end(), vendor, vendor + strlen(vendor));
        le32((juce::uint32) entries.size());
        for (auto* e : entries) {
            le32((juce::uint32) strlen(e));
            b.insert(b.end(), e, e + strlen(e));
        }
        return b;
    }

    juce::String utf8(const char* s) { return juce::String::fromUTF8(s); }
}

class TagReaderTests : public juce::UnitTest
{
public:
    TagReaderTests() : juce::UnitTest("Etiquetas", "RAAADIO") {}

    void runTest() override
    {
        beginTest("ID3v2.3: título en UTF-16 y artista en Latin-1");
        {
            Id3Builder id3(3);
            id3.frame("TIT2", utf16Text(utf8("Canción de prueba")));
            id3.frame("TPE1", latin1Text({ 0xD1, 'a', 'n', 'd', 0xFA, ' ', 0x93, 'X', 0x94 }));  // Ñandú “X”
            const auto tag = id3.build();
            size_t length = 0;
            const auto tags = TagReader::parseId3v2(tag.data(), tag.size(), &length);
            expectEquals(tags.title, utf8("Canción de prueba"));
            expectEquals(tags.artist, utf8("Ñandú \xe2\x80\x9cX\xe2\x80\x9d"));
            expectEquals((int) length, (int) tag.size());
        }

        beginTest("ID3v2.3 con desincronización");
        {
            Id3Builder id3(3);
            id3.frame("TIT2", utf16Text(utf8("Él")));  // la marca FF FE queda como FF 00 FE
            const auto tag = id3.build(true);
            expectEquals(TagReader::parseId3v2(tag.data(), tag.size()).title, utf8("Él"));
        }

        beginTest("ID3v2.4: UTF-8, tamaños syncsafe y el tamaño sin syncsafe de iTunes");
        {
            const auto longTitle = utf8("Canción larga ") + juce::String::repeatedString("x", 200);
            Id3Builder id3(4);
            id3.frame("TIT2", utf8Text(longTitle), true);  // tamaño "normal", como iTunes
            id3.frame("TPE1", utf8Text(utf8("Héctor Lavoe")));
            const auto tag = id3.build();
            const auto tags = TagReader::parseId3v2(tag.data(), tag.size());
            expectEquals(tags.title, longTitle);
            expectEquals(tags.artist, utf8("Héctor Lavoe"));
        }

        beginTest("ID3v2.2, ID3v1 y artista del álbum como respaldo");
        {
            Id3Builder id3(2);
            id3.frame("TT2", latin1Text({ 'O', 'j', 'a', 'l', 0xE1 }));
            id3.frame("TP2", latin1Text({ 'J', 'L', 'G' }));
            const auto tag = id3.build();
            const auto tags = TagReader::parseId3v2(tag.data(), tag.size());
            expectEquals(tags.title, utf8("Ojalá"));
            expectEquals(tags.artist, juce::String("JLG"));

            Bytes v1(128, 0);
            memcpy(v1.data(), "TAG", 3);
            memcpy(v1.data() + 3, "La Gota Fr\xed" "a", 12);
            memcpy(v1.data() + 33, "Carlos Vives", 12);
            const auto old = TagReader::parseId3v1(v1.data(), v1.size());
            expectEquals(old.title, utf8("La Gota Fría"));
            expectEquals(old.artist, juce::String("Carlos Vives"));
        }

        beginTest("FLAC: comentario Vorbis después de una carátula grande");
        {
            Bytes flac { 'f', 'L', 'a', 'C', 0x00, 0x00, 0x00, 34 };
            flac.insert(flac.end(), 34, 0x00);  // STREAMINFO
            flac.insert(flac.end(), { 0x06, 0x00, 0x13, 0x88 });  // PICTURE de 5000 bytes
            flac.insert(flac.end(), 5000, 0xAB);
            const auto comment = vorbisComment({ "title=Caf\xc3\xa9 con leche", "ARTIST=Ni\xc3\xb1o", "ARTIST=Otro" });
            const auto n = (juce::uint32) comment.size();
            flac.insert(flac.end(), { 0x84, (juce::uint8) (n >> 16), (juce::uint8) (n >> 8), (juce::uint8) n });
            flac.insert(flac.end(), comment.begin(), comment.end());

            const auto tags = TagReader::parseFlac(flac.data(), flac.size());
            expectEquals(tags.title, utf8("Café con leche"));
            expectEquals(tags.artist, utf8("Niño, Otro"));
        }

        beginTest("Ogg Vorbis: las tildes se leen bien");
        {
            TempLab lab;
            juce::OggVorbisAudioFormat ogg;
            juce::StringPairArray metadata;
            metadata.set(juce::OggVorbisAudioFormat::id3title, utf8("Canción del niño"));
            metadata.set(juce::OggVorbisAudioFormat::id3artist, utf8("Guayacán Orquesta"));
            const auto file = writeAudio(ogg, lab.root.getChildFile("x.ogg"), 1.0,
                                         [](double t) { return tone(t, 0.3f); }, metadata, 16, 4);
            const auto tags = TagReader::read(file);
            expectEquals(tags.title, utf8("Canción del niño"));
            expectEquals(tags.artist, utf8("Guayacán Orquesta"));
        }

        beginTest("Los datos de relleno de los programas de copia se ignoran");
        {
            TempLab lab;
            Id3Builder id3(3);
            id3.frame("TIT2", latin1Text({ 'T', 'r', 'a', 'c', 'k', ' ', '0', '3' }));
            id3.frame("TPE1", utf16Text("Unknown Artist"));
            auto bytes = id3.build();
            bytes.insert(bytes.end(), 2000, 0x55);  // "audio"
            const auto file = lab.root.getChildFile("x.mp3");
            file.replaceWithData(bytes.data(), bytes.size());
            expect(TagReader::read(file).isEmpty());
        }
    }
};

//==============================================================================
class TrackNamingTests : public juce::UnitTest
{
public:
    TrackNamingTests() : juce::UnitTest("Nombres de canciones", "RAAADIO") {}

    TrackNaming::Source file(const char* name, const char* group = "MERENGUE")
    {
        TrackNaming::Source s;
        s.fileName = utf8(name);
        s.group = group;
        return s;
    }

    void runTest() override
    {
        using namespace TrackNaming;

        beginTest("Claves: mayúsculas, tildes y \"&\" no cambian el nombre");
        expectEquals(key(utf8("Rubby Pérez")), key("RUBBY PEREZ"));
        expectEquals(key("Sandy & Papo"), key("SANDY Y PAPO"));
        expectEquals(key(utf8("Oscar D'León")), juce::String("oscar dleon"));
        expectEquals(matchKey(utf8("Rubby Pérez"), "Buscando Tus Besos"), matchKey("RUBBY PEREZ", "BUSCANDO TUS BESOS"));

        beginTest("Tipo Título para lo que viene en mayúsculas; lo demás queda igual");
        expectEquals(smartCase("POCHY Y SU COCOBAND"), juce::String("Pochy y su Cocoband"));
        expectEquals(smartCase("LOS OCHO DE COLOMBIA"), juce::String("Los Ocho de Colombia"));
        expectEquals(smartCase(utf8("AY AY CARIÑO")), utf8("Ay Ay Cariño"));
        expectEquals(smartCase("DJ PEPE"), juce::String("DJ Pepe"));
        expectEquals(smartCase("ROCKY II"), juce::String("Rocky II"));
        expectEquals(smartCase(utf8("La Gota Fría")), utf8("La Gota Fría"));
        expectEquals(smartCase("la camisa negra"), juce::String("La Camisa Negra"));

        beginTest("Nombres limpios: copias repetidas, número de pista, video oficial y direcciones web");
        expectEquals(cleanFileName("LA FLACA - POCHY Y SU COCOBAND (ALFONSO VASQUEZ)_521167018"),
                     juce::String("LA FLACA - POCHY Y SU COCOBAND (ALFONSO VASQUEZ)"));
        expectEquals(cleanFileName(utf8("01 - Carlos Vives - La Gota Fría (Video Oficial)")), utf8("Carlos Vives - La Gota Fría"));
        expectEquals(cleanFileName("Juanes_-_La_Camisa_Negra"), juce::String("Juanes - La Camisa Negra"));
        expectEquals(cleanFileName("www.musica.com - Artista - Tema [HD]"), juce::String("Artista - Tema"));
        expectEquals(cleanFileName("50 Cent - In Da Club"), juce::String("50 Cent - In Da Club"));
        expectEquals(cleanFileName("07 Bachata Rosa"), juce::String("Bachata Rosa"));

        const Context empty({}, {}, { "MERENGUE" }, {});

        beginTest("Título – Artista: compositores entre paréntesis, año, género e invitados");
        {
            auto p = propose(file("LA FLACA - POCHY Y SU COCOBAND (ALFONSO VASQUEZ)"), Orientation::TitleArtist, empty);
            expectEquals(p.artist, juce::String("Pochy y su Cocoband"));
            expectEquals(p.title, juce::String("La Flaca"));
            expectEquals(p.composers.joinIntoString("|"), juce::String("Alfonso Vasquez"));

            p = propose(file("AMARTE A TI - SIN FRONTERAS - MERENGUE (D. FREIGBERG - W. A. RENZON) 1997"), Orientation::TitleArtist, empty);
            expectEquals(p.artist, juce::String("Sin Fronteras"));
            expectEquals(p.title, juce::String("Amarte a Ti"));
            expectEquals(p.year, 1997);
            expectEquals(p.composers.joinIntoString("|"), juce::String("D. Freigberg|W. A. Renzon"));
            expectEquals(p.genreRemoved, juce::String("MERENGUE"));

            p = propose(file("LA GRAN MANZANA - FRENESI ORQUESTA, CANTA EL FANTASMA"), Orientation::TitleArtist, empty);
            expectEquals(p.artist, juce::String("Frenesi Orquesta"));
            expectEquals(p.featured.joinIntoString("|"), juce::String("El Fantasma"));
            expectEquals(p.credit(), juce::String("Frenesi Orquesta feat. El Fantasma"));
            expect(!p.featuredGuessed, "\"canta\" dice explícitamente quién es el invitado");

            p = propose(file("DAME TU QUERER - LOS OCHO DE COLOMBIA, CANTA EDER ORTEGA MERENGUE (D. EN D.)"), Orientation::TitleArtist, empty);
            expectEquals(p.artist, juce::String("Los Ocho de Colombia"));
            expectEquals(p.featured.joinIntoString("|"), juce::String("Eder Ortega"));
            expectEquals(p.title, juce::String("Dame Tu Querer"));

            p = propose(file("ESA MORENA - LOS HERMANOS ROSARIO - 1997"), Orientation::TitleArtist, empty);
            expectEquals(p.artist, juce::String("Los Hermanos Rosario"));
            expectEquals(p.title, juce::String("Esa Morena"));
            expectEquals(p.year, 1997);
        }

        beginTest("El formato de cada carpeta sale de los artistas que se repiten con títulos distintos");
        {
            std::vector<Source> batch;
            for (auto* name : { "VOLVERE - WILFRIDO VARGAS", "CUPIDO ME FLECHO - WILFRIDO VARGAS",
                                "EL BAILE DEL PERRITO - WILFRIDO VARGAS (WILFRIDO VARGAS)", "EL PADRINO - FULANITO",
                                "CALLATE - FULANITO_521290686", "GUALLANDO - FULANITO", "HOLA - LOS HERMANOS ROSARIO",
                                "ESA MORENA - LOS HERMANOS ROSARIO - 1997", "LA DUEÑA DEL SWING - LOS HERMANOS ROSARIO",
                                "DEMASIADO NIÑA - EDDY HERRERA_521204329", "A DORMIR JUNTITOS - EDDY HERRERA Y LIZ",
                                "LA BAILADORA - EDDY HERRERA (ALFONSO VALLEJO)", "LA MOVIDA - SANDY Y PAPO",
                                "LA HORA DE BAILAR - SANDY Y PAPO", "EL VIRAO - LOS CANTANTES DE RAMON ORLANDO" })
                batch.push_back(file(name));
            for (auto* name : { "LOS HERMANOS ROSARIO - ESA MORENA", "RUBBY PEREZ - BUSCANDO TUS BESOS",
                                "JUAN LUIS GUERRA - OJALA QUE LLUEVA CAFE" })
                batch.push_back(file(name, "GENERAL"));

            const Context context({}, {}, { "MERENGUE" }, batch);
            const auto formats = detectFormats(batch, context);
            const auto merengue = formats.at("MERENGUE");
            expect(merengue.orientation == Orientation::TitleArtist, "la carpeta de merengue viene al revés");
            expect(merengue.verified());
            expect(merengue.confirming >= 10, "lo confirman los artistas repetidos: " + juce::String(merengue.confirming));
            expectEquals(merengue.contradicting, 0);
            expect(formats.at("GENERAL").orientation == Orientation::ArtistTitle, "la carpeta general viene bien");

            auto p = propose(file("A DORMIR JUNTITOS - EDDY HERRERA Y LIZ"), Orientation::TitleArtist, context);
            expectEquals(p.artist, juce::String("Eddy Herrera"));
            expectEquals(p.featured.joinIntoString("|"), juce::String("Liz"));
            expect(p.featuredGuessed, "se separó porque Eddy Herrera aparece solo en otros archivos");
            expectEquals(p.joinedArtist, juce::String("Eddy Herrera y Liz"));

            p = propose(file("LA MOVIDA - SANDY Y PAPO"), Orientation::TitleArtist, context);
            expectEquals(p.artist, juce::String("Sandy y Papo"));
            expect(p.featured.isEmpty(), "un dúo que nadie conoce por separado queda entero");
        }

        beginTest("Artistas conocidos: se escriben como en la biblioteca, con alias y parecidos");
        {
            std::map<juce::String, KnownArtist> known {
                { key("Juan Luis Guerra"), { "Juan Luis Guerra", 12 } },
                { key("Los Hermanos Rosario"), { "Los Hermanos Rosario", 6 } },
                { key(utf8("Rubby Pérez")), { utf8("Rubby Pérez"), 3 } },
            };
            const Context context(known, { { key("Hnos Rosario"), key("Los Hermanos Rosario") } }, {}, {});
            expectEquals(propose(file("RUBBY PEREZ - BUSCANDO TUS BESOS", "GENERAL"), Orientation::ArtistTitle, context).artist,
                         utf8("Rubby Pérez"));
            expectEquals(propose(file("HNOS ROSARIO - HOLA", "GENERAL"), Orientation::ArtistTitle, context).artist,
                         juce::String("Los Hermanos Rosario"));
            expectEquals(context.closestKnown("Hermanos Rosario"), juce::String("Los Hermanos Rosario"));
            expectEquals(context.closestKnown("Juan Luis Guera"), juce::String("Juan Luis Guerra"));
            expectEquals(context.closestKnown("Juan Gabriel"), juce::String());
            expectEquals((int) context.suggestions("juan", 5).size(), 1);
        }

        beginTest("Etiquetas: invitados y versión; y lo que no se debe quitar");
        {
            Source tagged;
            tagged.fileName = "pista 07";
            tagged.tagArtist = "Carlos Vives feat. Shakira";
            tagged.tagTitle = "La Bicicleta (Remix)";
            auto p = propose(tagged, Orientation::TitleArtist, empty);
            expectEquals(p.artist, juce::String("Carlos Vives"));
            expectEquals(p.featured.joinIntoString("|"), juce::String("Shakira"));
            expectEquals(p.title, juce::String("La Bicicleta"));
            expectEquals(p.version, juce::String("Remix"));
            expectEquals(p.fullTitle(), juce::String("La Bicicleta (Remix)"));

            p = propose(file("PRINCE - 1999", "GENERAL"), Orientation::ArtistTitle, empty);
            expectEquals(p.artist, juce::String("Prince"));
            expectEquals(p.title, juce::String("1999"));
            expectEquals(p.year, 0);

            const Context salsa({}, {}, { "SALSA" }, {});
            p = propose(file("GRUPO NICHE - SALSA", "GENERAL"), Orientation::ArtistTitle, salsa);
            expectEquals(p.title, juce::String("Salsa"));

            p = propose(file("MARC ANTHONY - TUYA (MAS QUE TUYA)", "GENERAL"), Orientation::ArtistTitle, empty);
            expectEquals(p.title, juce::String("Tuya (Mas Que Tuya)"));
            expect(p.composers.isEmpty());
        }

        beginTest("Casos de la biblioteca real: \"vi\" no es número romano, siglas con puntos y artistas pegados");
        {
            auto p = propose(file("LOS VI PASAR - LA BANDA NOCTURNA - TECNO MERENGUE (AUGUSTO ZAPATA) 1992"), Orientation::TitleArtist, empty);
            expectEquals(p.title, juce::String("Los Vi Pasar"));
            expectEquals(p.genreRemoved, juce::String("TECNO MERENGUE"));
            p = propose(file("SOY CHIQUITO - SANTI Y SUS DUENDES (D.R.A.)"), Orientation::TitleArtist, empty);
            expectEquals(p.composers.joinIntoString("|"), juce::String("D.R.A."));
            p = propose(file("TE QUIERO - MONCHI CAPRICHO."), Orientation::TitleArtist, empty);
            expectEquals(p.artist, juce::String("Monchi Capricho"));
            p = propose(file("SUAVECITO - PROYECCION T.L.F."), Orientation::TitleArtist, empty);
            expectEquals(p.artist, juce::String("Proyeccion T.L.F."));

            std::vector<Source> batch;
            for (auto* name : { "VOLVERE - WILFRIDO VARGAS", "CUPIDO ME FLECHO - WILFRIDO VARGAS", "EL JARDINERO - WILFRIDO VARGAS",
                                "BACHATA MERENGUE - WILFRIDOVARGAS, CANTA MICKEY TAVERAS (MARINO PEREZ)" })
                batch.push_back(file(name));
            const Context context({}, {}, { "MERENGUE" }, batch);
            p = propose(batch.back(), Orientation::TitleArtist, context);
            expectEquals(p.artist, juce::String("Wilfridovargas"));
            expectEquals(context.closestKnown(p.artist), juce::String("Wilfrido Vargas"),
                         "la forma que más se repite en el lote, aunque la biblioteca esté vacía");
            expectEquals(context.closestKnown("Wilfrido Vargas"), juce::String(), "la forma más repetida no se corrige a sí misma");
        }

        beginTest("En pantalla, en mayúsculas si la emisora lo prefiere; lo guardado no cambia");
        setDisplayUppercase(true);
        expectEquals(display(utf8("Cariño")), utf8("CARIÑO"));
        setDisplayUppercase(false);
        expectEquals(display(utf8("Cariño")), utf8("Cariño"));
    }
};

//==============================================================================
class ImportTests : public juce::UnitTest
{
public:
    ImportTests() : juce::UnitTest("Importación", "RAAADIO") {}

    void runTest() override
    {
        LibraryFixture fx;
        TempLab outside;
        fx.library.addCategory("VALLENATO");
        fx.library.addCategory("EXITOS");

        // Un segundo de silencio y 19 de tono; las etiquetas no coinciden con el nombre.
        juce::StringPairArray info;
        info.set(juce::WavAudioFormat::riffInfoTitle, utf8("La Gota Fría"));
        info.set(juce::WavAudioFormat::riffInfoArtist, "Carlos Vives");
        const auto source = writeWav(outside.root.getChildFile("pista 07.wav"), 20.0, toneBetween({ { 1.0, 20.0 } }), info);

        beginTest("Se copia el archivo original, con artista y título de sus etiquetas, y se analiza");
        expect(fx.library.processAndAddTrack(source, "VALLENATO") == MusicLibrary::ImportResult::Added);
        auto tracks = fx.library.search("", "VALLENATO");
        expectEquals((int) tracks.size(), 1);
        const auto track = tracks.empty() ? TrackInfo {} : tracks.front();
        const juce::File copied(track.path);
        expectEquals(juce::String(track.artist), juce::String("Carlos Vives"));
        expectEquals(juce::String::fromUTF8(track.title.c_str()), utf8("La Gota Fría"));
        expect(track.isPending(), "la música entra por revisar");
        expectEquals(copied.getFileName(), juce::String("pista 07.wav"));
        expect(copied.isAChildOf(fx.lab.root.getChildFile("Library/_Entrada")), "espera en _Entrada hasta que se confirme");
        expect(copied.hasIdenticalContentTo(source), "el archivo es idéntico al original, sin recodificar");
        expectEquals(track.analysisVersion, TrackAnalyzer::version);
        expectWithinAbsoluteError(track.cueIn, 0.98, 0.03);
        expectWithinAbsoluteError(track.loudnessLufs, analyseFile(source).loudnessLufs, 0.01);
        expect(track.loudnessLufs > -8.0 && track.loudnessLufs < -5.0, "un tono a -6 dBFS mide unos -6.5 LUFS");
        expectWithinAbsoluteError(track.normalizationGain,
                                  TrackAnalyzer::gainFor(track.loudnessLufs, track.truePeakDb), 1.0e-6);
        expectEquals(juce::String(track.fileHash), juce::SHA256(source).toHexString());

        beginTest("El mismo archivo no se importa dos veces; en otra categoría, se suma a ella");
        expect(fx.library.processAndAddTrack(source, "VALLENATO") == MusicLibrary::ImportResult::Duplicate);
        expect(fx.library.processAndAddTrack(source, "EXITOS") == MusicLibrary::ImportResult::Duplicate);
        expectEquals((int) fx.library.search("", "").size(), 1);
        expectEquals((int) fx.library.getTrackCategories(track.id).size(), 2);

        beginTest("La misma canción en otro formato se reconoce por artista, título y duración");
        {
            juce::FlacAudioFormat flac;
            const auto other = writeAudio(flac, outside.root.getChildFile(utf8("Carlos Vives - La Gota Fría.flac")), 20.5,
                                          toneBetween({ { 1.0, 20.0 } }), {}, 16);
            expect(fx.library.processAndAddTrack(other) == MusicLibrary::ImportResult::Duplicate);

            const auto different = writeAudio(flac, outside.root.getChildFile("Carlos Vives - Otra.flac"), 20.0,
                                              toneBetween({ { 1.0, 20.0 } }), {}, 16);
            expect(fx.library.processAndAddTrack(different) == MusicLibrary::ImportResult::Added);
            expectEquals((int) fx.library.search("", "").size(), 2);
        }

        beginTest("La cuña de este mes, con el mismo nombre y duración que la anterior, no se toma por duplicada");
        {
            const juce::String spots(fx.library.getSystemCategory(CategoryRole::Spots).name);
            const auto september = writeWav(outside.root.getChildFile("septiembre/Ferreteria - Cuna 30.wav"), 30.0,
                                            toneBetween({ { 0.0, 30.0 } }));
            const auto october = writeWav(outside.root.getChildFile("octubre/Ferreteria - Cuna 30.wav"), 30.0,
                                          [](double t) { return tone(t, 0.3f, 880.0); });
            expect(fx.library.processAndAddTrack(september, spots) == MusicLibrary::ImportResult::Added);
            expect(fx.library.processAndAddTrack(october, spots) == MusicLibrary::ImportResult::Added,
                   "es otra grabación: la cuña nueva");
            expect(fx.library.processAndAddTrack(october, spots) == MusicLibrary::ImportResult::Duplicate,
                   "el mismo archivo sí se reconoce");
            expectEquals((int) fx.library.getTracksWithRole(CategoryRole::Spots).size(), 2);
        }

        beginTest("Un archivo que no es audio se rechaza");
        {
            const auto text = outside.root.getChildFile("notas.wav");
            text.replaceWithText("no soy audio");
            expect(fx.library.processAndAddTrack(text) == MusicLibrary::ImportResult::Failed);
        }

        beginTest("Las pistas de versiones anteriores se analizan en segundo plano");
        {
            const auto legacyFile = writeWav(fx.lab.root.getChildFile("Library/GENERAL/VIEJA.wav"), 12.0,
                                             toneBetween({ { 2.0, 12.0 } }));
            fx.library.getDatabase().addTrack(makeTrack(legacyFile.getFullPathName(), "VIEJA", "VIEJA"));
            expect(MusicLibrary::needsAnalysis(fx.library.getTrackByPath(legacyFile.getFullPathName())));

            fx.library.startBackgroundAnalysis();
            TrackInfo legacy;
            for (int i = 0; i < 200; ++i) {
                legacy = fx.library.getTrackByPath(legacyFile.getFullPathName());
                if (!MusicLibrary::needsAnalysis(legacy)) break;
                juce::Thread::sleep(50);
            }
            fx.library.stopBackgroundAnalysis();

            expectEquals(legacy.analysisVersion, TrackAnalyzer::version);
            expectWithinAbsoluteError(legacy.cueIn, 1.98, 0.03);
            expect(!legacy.fileHash.empty(), "el hash queda guardado para detectar duplicados");
        }
    }
};

//==============================================================================
class ImportReviewTests : public juce::UnitTest
{
public:
    ImportReviewTests() : juce::UnitTest("Revisión de lo importado", "RAAADIO") {}

    // Un archivo corto con un tono propio (así cada uno es distinto para el hash).
    static juce::File song(const juce::File& folder, const char* name, int index, double seconds = 2.0)
    {
        return writeWav(folder.getChildFile(utf8(name) + ".wav"), seconds,
                        [index](double t) { return tone(t, 0.3f, 300.0 + 37.0 * index); });
    }

    static void importAndWait(MusicLibrary& library, const juce::File& folder, const juce::String& category)
    {
        library.importFiles({ folder }, category);
        for (int i = 0; i < 600 && library.isImporting(); ++i) juce::Thread::sleep(20);
    }

    static int rowNamed(const ImportReview& review, const char* sourceName)
    {
        const auto& items = review.getItems();
        for (int i = 0; i < (int) items.size(); ++i)
            if (juce::String::fromUTF8(items[(size_t) i].source.name.c_str()) == utf8(sourceName)) return i;
        return -1;
    }

    static TrackInfo trackTitled(MusicLibrary& library, const juce::String& title)
    {
        for (const auto& t : library.search("", ""))
            if (juce::String::fromUTF8(t.title.c_str()) == title) return t;
        TrackInfo none {};
        none.id = -1;
        return none;
    }

    static int generatorCandidates(MusicLibrary& library)
    {
        std::vector<int> music;
        for (const auto& c : library.getCategories())
            if (!c.isSystem()) music.push_back(c.id);
        GeneratorSettings settings;
        return (int) PlaylistGenerator::selectCandidates(library.search("", ""), library.getAllTrackCategories(), music,
                                                         settings, juce::Time::getCurrentTime()).size();
    }

    void runTest() override
    {
        using ImportReview = ::ImportReview;

        LibraryFixture fx;
        TempLab outside;
        fx.library.addCategory("MERENGUE");
        fx.library.addCategory("EXITOS");

        // Una carpeta como la de la emisora: "TÍTULO - ARTISTA (compositores)", sin etiquetas.
        const auto merengue = outside.root.getChildFile("MERENGUE");
        const char* names[] = { "VOLVERE - WILFRIDO VARGAS", "CUPIDO ME FLECHO - WILFRIDO VARGAS", "EL PADRINO - FULANITO",
                                "GUALLANDO - FULANITO", "LA FLACA - POCHY Y SU COCOBAND (ALFONSO VASQUEZ)",
                                "A DORMIR JUNTITOS - EDDY HERRERA Y LIZ", "DEMASIADO NIÑA - EDDY HERRERA",
                                "LA BAILADORA - EDDY HERRERA (ALFONSO VALLEJO)" };
        int index = 0;
        for (auto* name : names) song(merengue, name, index++);
        importAndWait(fx.library, merengue, "MERENGUE");

        beginTest("La música importada espera por revisar: AutoDJ y el generador no la usan");
        expectEquals(fx.library.getPendingCount(), 8);
        expectEquals(generatorCandidates(fx.library), 0);

        ImportReview review(fx.library);
        review.load(ImportReview::Scope::NewImports);

        beginTest("La revisión lee la carpeta completa: título – artista, compositores e invitados");
        {
            expectEquals((int) review.getGroups().size(), 1);
            const auto& group = review.getGroups().front();
            expect(group.format.orientation == TrackNaming::Orientation::TitleArtist, "se detecta que la carpeta va al revés");
            expect(group.format.verified());
            expectEquals(group.name, juce::String("MERENGUE"));

            const int flaca = rowNamed(review, "LA FLACA - POCHY Y SU COCOBAND (ALFONSO VASQUEZ)");
            expect(flaca >= 0);
            if (flaca >= 0) {
                const auto& item = review.getItems()[(size_t) flaca];
                expectEquals(item.proposal.artist, juce::String("Pochy y su Cocoband"));
                expectEquals(item.proposal.title, juce::String("La Flaca"));
                expectEquals(item.proposal.composers.joinIntoString("|"), juce::String("Alfonso Vasquez"));
                expect(item.state == ImportReview::State::Ready);
            }
            const int juntitos = rowNamed(review, "A DORMIR JUNTITOS - EDDY HERRERA Y LIZ");
            expect(juntitos >= 0 && review.getItems()[(size_t) juntitos].state == ImportReview::State::Check,
                   "\"y Liz\" puede ser una invitada o parte del nombre: se pregunta");
            expectEquals((int) review.readyRows().size(), 7);
        }

        beginTest("Confirmar ordena el archivo en Musica/<artista> y guarda los créditos");
        {
            const auto outcome = review.confirm(review.readyRows());
            expectEquals(outcome.confirmed, 7);
            expectEquals(outcome.failed, 0);

            const auto volvere = trackTitled(fx.library, "Volvere");
            expect(volvere.id != -1);
            expect(!volvere.isPending());
            expect(!volvere.confirmedAt.empty());
            expectEquals(juce::File(volvere.path).getRelativePathFrom(fx.lab.root).replaceCharacter('\\', '/'),
                         juce::String("Library/Musica/Wilfrido Vargas/Wilfrido Vargas - Volvere.wav"));

            const auto flaca = trackTitled(fx.library, "La Flaca");
            const auto credits = fx.library.getTrackCredits(flaca);
            expectEquals(credits.artist, juce::String("Pochy y su Cocoband"));
            expectEquals(credits.composers.joinIntoString("|"), juce::String("Alfonso Vasquez"));

            expectEquals(fx.library.getPendingCount(), 1);
            expectEquals(generatorCandidates(fx.library), 7);
            expectEquals((int) review.getItems().size(), 1);  // la revisión se recarga sola
        }

        beginTest("Lo que el operador corrige queda guardado aunque cierre la revisión");
        {
            review.resolveFeatured(0, true);
            expect(review.getItems().front().state == ImportReview::State::Ready);

            ImportReview again(fx.library);
            again.load(ImportReview::Scope::NewImports);
            expect(again.getItems().size() == 1 && again.getItems().front().manual);
            expect(again.getItems().front().state == ImportReview::State::Ready);
            expectEquals(again.getItems().front().proposal.credit(), juce::String("Eddy Herrera feat. Liz"));
            expectEquals(again.confirm(again.readyRows()).confirmed, 1);
            expect(!fx.lab.root.getChildFile("Library/_Entrada").exists(), "la carpeta de entrada vacía se quita");
        }

        beginTest("Un artista conocido orienta la importación: otra copia de la canción se reconoce al importarla");
        {
            const auto exitos = outside.root.getChildFile("EXITOS");
            song(exitos, "VOLVERE - WILFRIDO VARGAS", 40);
            importAndWait(fx.library, exitos, "EXITOS");

            expectEquals(fx.library.getLastImportSummary().duplicates, 1);
            expectEquals(fx.library.getPendingCount(), 0);
            const auto original = trackTitled(fx.library, "Volvere");
            expectEquals((int) fx.library.getTrackCategories(original.id).size(), 2);
        }

        beginTest("Si el operador corrige el artista, la próxima vez se reconoce solo");
        {
            const auto typo = outside.root.getChildFile("TYPO");
            song(typo, "TE QUIERO - WILFRIDO VARGAZ", 50);
            song(typo, "LA LOCA - WILFRIDO VARGAS", 51);
            importAndWait(fx.library, typo, "MERENGUE");

            review.load(ImportReview::Scope::NewImports);
            const int row = rowNamed(review, "TE QUIERO - WILFRIDO VARGAZ");
            expect(row >= 0 && review.getItems()[(size_t) row].has(ImportReview::Issue::DidYouMean));
            if (row >= 0) {
                expectEquals(review.getItems()[(size_t) row].didYouMean, juce::String("Wilfrido Vargas"));
                review.acceptDidYouMean(row);
            }
            expectEquals(review.confirm(review.readyRows()).confirmed, 2);
            const auto context = fx.library.makeNamingContext({});
            const auto* known = context.findKnown(TrackNaming::key("WILFRIDO VARGAZ"));
            expect(known != nullptr && known->name == "Wilfrido Vargas");
        }

        beginTest("Editar una canción confirmada la cambia de lugar y avisa a la cola");
        {
            const auto volvere = trackTitled(fx.library, "Volvere");
            int relocatedId = -1;
            juce::String relocatedPath;
            fx.library.onTrackRelocated = [&](int id, const juce::String& path) { relocatedId = id; relocatedPath = path; };

            auto values = fx.library.getTrackCredits(volvere);
            values.title = utf8("Volveré");
            values.version = "En Vivo";
            const auto result = fx.library.confirmTrack(volvere.id, values);
            expect(result.ok && result.fileMoved);
            const auto edited = fx.library.getTrackById(volvere.id);
            expectEquals(juce::String::fromUTF8(edited.title.c_str()), utf8("Volveré (En Vivo)"));
            expectEquals(juce::File(edited.path).getFileName(), utf8("Wilfrido Vargas - Volveré (En Vivo).wav"));
            expect(juce::File(edited.path).existsAsFile());
            expectEquals(relocatedId, volvere.id);
            expectEquals(relocatedPath, juce::String::fromUTF8(edited.path.c_str()));
            expect(fx.library.search("volvere", "").size() == 1, "se encuentra sin la tilde");
            fx.library.onTrackRelocated = nullptr;
        }

        beginTest("Un respaldo de antes de ordenar la música: los archivos se encuentran por su huella");
        {
            const auto volvere = trackTitled(fx.library, utf8("Volveré (En Vivo)"));
            const auto ordered = juce::String::fromUTF8(volvere.path.c_str());
            // Así queda la base restaurada: con la ruta que tenía el archivo antes de confirmarlo.
            fx.library.getDatabase().setTrackPath(volvere.id, fx.lab.root.getChildFile("Library/_Entrada/1/VOLVERE - WILFRIDO VARGAS.wav")
                                                                  .getFullPathName().toStdString());
            expectEquals(fx.library.repairMissingFiles(), 1);
            expectEquals(juce::String::fromUTF8(fx.library.getTrackById(volvere.id).path.c_str()), ordered);
            expectEquals(fx.library.repairMissingFiles(), 0, "no queda nada por buscar");
        }

        beginTest("La última forma en que se confirma un artista es la que se sugiere después");
        {
            const auto padrino = trackTitled(fx.library, "El Padrino");
            auto values = fx.library.getTrackCredits(padrino);
            values.artist = "FULANITO";
            expect(fx.library.confirmTrack(padrino.id, values).ok);
            const auto context = fx.library.makeNamingContext({});
            const auto* known = context.findKnown(TrackNaming::key("Fulanito"));
            expect(known != nullptr && known->name == "FULANITO");
        }

        beginTest("Música de versiones anteriores: sigue sonando y se puede sanear");
        {
            LibraryFixture old;
            old.library.addCategory("MERENGUE");
            // Como la dejaba la importación anterior: artista y título al revés y en mayúsculas.
            const char* legacy[][2] = { { "VOLVERE", "WILFRIDO VARGAS" }, { "CUPIDO ME FLECHO", "WILFRIDO VARGAS" },
                                        { "EL PADRINO", "FULANITO" }, { "GUALLANDO", "FULANITO" },
                                        { "HOLA", "LOS HERMANOS ROSARIO" }, { "ESA MORENA", "LOS HERMANOS ROSARIO - 1997" } };
            int merengueId = -1;
            for (const auto& c : old.library.getCategories())
                if (juce::String(c.name) == "MERENGUE") merengueId = c.id;

            // La misma "Esa Morena", ya bien escrita, en GENERAL: la de MERENGUE es una copia.
            const auto general = song(old.lab.root.getChildFile("Library/GENERAL/LOS HERMANOS ROSARIO"),
                                      "LOS HERMANOS ROSARIO - ESA MORENA", 90);
            old.library.getDatabase().addTrack(makeTrack(general.getFullPathName(), "LOS HERMANOS ROSARIO", "ESA MORENA"));
            const int generalId = old.library.getTrackByPath(general.getFullPathName()).id;

            for (const auto& l : legacy) {
                const auto file = song(old.lab.root.getChildFile("Library/MERENGUE").getChildFile(l[0]),
                                       (juce::String(l[0]) + " - " + l[1]).toRawUTF8(), index++);
                old.library.getDatabase().addTrack(makeTrack(file.getFullPathName(), l[0], l[1]));
                old.library.setTrackCategories(old.library.getTrackByPath(file.getFullPathName()).id, { merengueId });
            }

            expectEquals(old.library.getUnreviewedCount(), 7);
            expectEquals(generatorCandidates(old.library), 7);

            ImportReview sanitize(old.library);
            sanitize.load(ImportReview::Scope::Library);
            expectEquals((int) sanitize.getGroups().size(), 2);
            const auto* merengueGroup = sanitize.findGroup(old.lab.root.getChildFile("Library/MERENGUE").getFullPathName());
            expect(merengueGroup != nullptr && merengueGroup->format.orientation == TrackNaming::Orientation::TitleArtist);
            const auto* generalGroup = sanitize.findGroup(old.lab.root.getChildFile("Library/GENERAL").getFullPathName());
            expect(generalGroup != nullptr && generalGroup->format.orientation == TrackNaming::Orientation::ArtistTitle);

            const int morena = rowNamed(sanitize, "ESA MORENA - LOS HERMANOS ROSARIO - 1997");
            expect(morena >= 0);
            if (morena >= 0) {
                const auto& item = sanitize.getItems()[(size_t) morena];
                expectEquals(item.proposal.artist, juce::String("Los Hermanos Rosario"));
                expectEquals(item.proposal.year, 1997);
                expect(item.state == ImportReview::State::Duplicate, "es la misma que la de GENERAL");
                expectEquals(item.duplicateOf, generalId);

                const juce::File copyFile(item.track.path);
                expectEquals(sanitize.discard({ morena }), 1);
                expect(!copyFile.exists());
                expectEquals(old.lab.root.getChildFile("Library/_Descartadas").findChildFiles(juce::File::findFiles, true).size(), 1);
                expectEquals((int) old.library.getTrackCategories(generalId).size(), 1);
            }

            expectEquals(sanitize.confirm(sanitize.readyRows()).confirmed, 6);
            expectEquals(old.library.getUnreviewedCount(), 0);
            expect(!old.lab.root.getChildFile("Library/MERENGUE").exists(), "las carpetas viejas vacías se quitan");
            expect(old.lab.root.getChildFile("Library/Musica/Fulanito/Fulanito - El Padrino.wav").existsAsFile());
        }

        beginTest("Una base de la versión anterior se migra una vez y se sigue viendo en mayúsculas");
        {
            LibraryFixture older;
            older.library.getDatabase().addTrack(makeTrack(older.lab.touch("Library/GENERAL/x.mp3").getFullPathName(),
                                                           "RUBBY PEREZ", "BUSCANDO TUS BESOS"));
            const auto dbFile = PersistenceManager::getDatabaseFolder().getChildFile("raaadio.db");
            sqlite3* raw = nullptr;
            sqlite3_open(dbFile.getFullPathName().toRawUTF8(), &raw);
            sqlite3_exec(raw, "UPDATE tracks SET match_key = ''; DELETE FROM settings WHERE key = 'library.displayUppercase'; "
                              "PRAGMA user_version = 2;", nullptr, nullptr, nullptr);
            sqlite3_close(raw);

            TrackNaming::setDisplayUppercase(false);
            MusicLibrary reopened;
            reopened.initialise(dbFile);
            expect(reopened.isDisplayUppercase(), "lo guardado estaba en mayúsculas: se sigue viendo igual");
            expectEquals(reopened.getDatabase().getSchemaVersion(), 3);
            expect(reopened.search(utf8("rubby pérez"), "").size() == 1, "la clave permite buscar sin mayúsculas");
            reopened.setDisplayUppercase(false);
        }

        beginTest("Si cambia el objetivo de sonoridad, las ganancias se recalculan sin reanalizar");
        {
            LibraryFixture older;
            auto& db = older.library.getDatabase();
            auto loud = makeTrack(older.lab.touch("Library/GENERAL/fuerte.mp3").getFullPathName(), "ARTISTA", "FUERTE");
            loud.loudnessLufs = -8.0;
            loud.truePeakDb = -0.1;
            loud.analysisVersion = TrackAnalyzer::version;
            loud.normalizationGain = juce::Decibels::decibelsToGain(-8.0);  // calculada para -16 LUFS
            db.addTrack(loud);
            auto legacy = makeTrack(older.lab.touch("Library/GENERAL/vieja.mp3").getFullPathName(), "ARTISTA", "VIEJA");
            legacy.normalizationGain = 1.5;  // sin analizar: queda hasta que el análisis la reemplace
            db.addTrack(legacy);
            db.setSetting("library.normalizationTarget", "-16.0");

            MusicLibrary reopened;
            reopened.initialise(PersistenceManager::getDatabaseFolder().getChildFile("raaadio.db"));
            expectWithinAbsoluteError(reopened.getTrackByPath(juce::String(loud.path)).normalizationGain,
                                      TrackAnalyzer::gainFor(-8.0, -0.1), 1.0e-9, "la analizada toma el objetivo nuevo");
            expectWithinAbsoluteError(reopened.getTrackByPath(juce::String(legacy.path)).normalizationGain, 1.5, 1.0e-9,
                                      "la que no tiene sonoridad medida no se toca");
            expectEquals(juce::String(reopened.getDatabase().getSetting("library.normalizationTarget")),
                         juce::String(TrackAnalyzer::targetLufs, 1), "se anota el objetivo aplicado");
        }
    }
};

//==============================================================================
namespace
{
    // "Música" de prueba: dos voces con armónicos que cambian de nota cada 0,1 a 0,5 s y un
    // golpe de ruido cada tanto, siempre igual para la misma semilla. pad: silencio al inicio.
    constexpr double musicRate = 44100.0;

    Signal music(int seed, double seconds, double pad = 0.0, float gain = 1.0f)
    {
        auto samples = std::make_shared<std::vector<float>>((size_t) (seconds * musicRate), 0.0f);
        juce::Random random(seed);
        for (int voice = 0; voice < 2; ++voice) {
            double t = 0.0;
            while (t < seconds) {
                const double length = 0.1 + 0.4 * random.nextDouble();
                const double hz = 110.0 * std::pow(2.0, random.nextInt(36) / 12.0);
                const float amp = 0.12f + 0.08f * random.nextFloat();
                const auto from = (size_t) (t * musicRate), to = juce::jmin(samples->size(), (size_t) ((t + length) * musicRate));
                for (size_t i = from; i < to; ++i) {
                    const double local = (double) (i - from) / musicRate;
                    const float envelope = (float) juce::jmin(1.0, local * 40.0) * (float) std::exp(-local * 3.0);
                    const double phase = juce::MathConstants<double>::twoPi * hz * (double) i / musicRate;
                    (*samples)[i] += amp * envelope * (float) (std::sin(phase) + 0.5 * std::sin(2.0 * phase) + 0.25 * std::sin(3.0 * phase));
                }
                t += length;
            }
        }
        double burst = 0.3;  // ráfagas de ruido de 50 ms a intervalos al azar
        while (burst < seconds) {
            for (size_t i = (size_t) (burst * musicRate); i < juce::jmin(samples->size(), (size_t) ((burst + 0.05) * musicRate)); ++i)
                (*samples)[i] += 0.2f * (random.nextFloat() * 2.0f - 1.0f);
            burst += 0.4 + 0.6 * random.nextDouble();
        }

        return [samples, pad, gain](double t) {
            const auto i = (long) ((t - pad) * musicRate);
            return i >= 0 && i < (long) samples->size() ? gain * (*samples)[(size_t) i] : 0.0f;
        };
    }

    std::vector<uint32_t> fingerprintOf(const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr) return {};
        const auto analysis = TrackAnalyzer::analyse(*reader);
        const double air = analysis.cueOut > analysis.cueIn ? analysis.cueOut - analysis.cueIn : analysis.lengthSeconds;
        return AcousticFingerprint::compute(*reader, analysis.cueIn, air);
    }
}

class AcousticFingerprintTests : public juce::UnitTest
{
public:
    AcousticFingerprintTests() : juce::UnitTest("Huella acústica", "RAAADIO") {}

    void runTest() override
    {
        TempLab lab;
        juce::OggVorbisAudioFormat ogg;

        beginTest("La misma grabación en OGG, 4 dB más baja y con otro comienzo, se reconoce; otra canción no");
        {
            const auto original = fingerprintOf(writeWav(lab.root.getChildFile("original.wav"), 60.0, music(7, 60.0)));
            const auto copy = fingerprintOf(writeAudio(ogg, lab.root.getChildFile("copia.ogg"), 60.3, music(7, 60.0, 0.3, 0.63f), {}, 16, 1));
            const auto other = fingerprintOf(writeWav(lab.root.getChildFile("otra.wav"), 60.0, music(8, 60.0)));

            expect(original.size() > 300, "unos 10 segundos de huella: " + juce::String((int) original.size()));
            const double same = AcousticFingerprint::similarity(original, copy);
            const double different = AcousticFingerprint::similarity(original, other);
            logMessage("  misma grabación: " + juce::String(same, 3) + ", otra canción: " + juce::String(different, 3));
            expect(same >= AcousticFingerprint::sameRecording, "la copia se parece " + juce::String(same, 3));
            expect(different < AcousticFingerprint::sameRecording - 0.1, "otra canción se parece " + juce::String(different, 3));
        }

        beginTest("Un tono o un silencio no tienen huella: no se confunden entre sí");
        {
            const auto toneA = fingerprintOf(writeWav(lab.root.getChildFile("tono_a.wav"), 50.0, [](double t) { return tone(t, 0.3f, 440.0); }));
            const auto toneB = fingerprintOf(writeWav(lab.root.getChildFile("tono_b.wav"), 50.0, [](double t) { return tone(t, 0.3f, 523.0); }));
            expect(toneA.empty() && toneB.empty());
            expectEquals(AcousticFingerprint::similarity(toneA, toneB), 0.0);
        }
    }
};

//==============================================================================
class CatalogToolsTests : public juce::UnitTest
{
public:
    CatalogToolsTests() : juce::UnitTest("Artistas, búsqueda y lotes", "RAAADIO") {}

    static TrackInfo titled(MusicLibrary& library, const juce::String& title)
    {
        for (const auto& t : library.search("", ""))
            if (juce::String::fromUTF8(t.title.c_str()) == title) return t;
        TrackInfo none {};
        none.id = -1;
        return none;
    }

    static void importAndWait(MusicLibrary& library, const juce::File& folder, const juce::String& category)
    {
        library.importFiles({ folder }, category);
        for (int i = 0; i < 600 && library.isImporting(); ++i) juce::Thread::sleep(20);
    }

    static int artistId(MusicLibrary& library, const juce::String& name)
    {
        for (const auto& a : library.getArtists())
            if (juce::String::fromUTF8(a.name.c_str()) == name) return a.id;
        return -1;
    }

    void runTest() override
    {
        LibraryFixture fx;
        TempLab outside;
        fx.library.addCategory("MERENGUE");

        // Un lote con créditos: invitada, compositor y un artista escrito de dos formas.
        const auto folder = outside.root.getChildFile("MERENGUE");
        const char* names[] = { "VOLVERE - WILFRIDO VARGAS", "CUPIDO ME FLECHO - WILFRIDO VARGAS", "EL JARDINERO - WILFRIDO VARGAS",
                                "LA FLACA - POCHY Y SU COCOBAND (ALFONSO VASQUEZ)", "DEMASIADO NIÑA - EDDY HERRERA",
                                "LA BAILADORA - EDDY HERRERA", "A DORMIR JUNTITOS - EDDY HERRERA Y LIZ",
                                "BACHATA MERENGUE - WILFRIDOVARGAS, CANTA MICKEY TAVERAS" };
        int index = 0;
        for (auto* name : names)
            writeWav(folder.getChildFile(utf8(name) + ".wav"), 2.0, [index](double t) { return tone(t, 0.3f, 250.0 + 41.0 * index); }), ++index;
        importAndWait(fx.library, folder, "MERENGUE");
        {
            ImportReview review(fx.library);
            review.load(ImportReview::Scope::NewImports);
            for (int i = 0; i < (int) review.getItems().size(); ++i) {
                const auto& item = review.getItems()[(size_t) i];
                if (item.has(ImportReview::Issue::FeaturedGuess)) review.resolveFeatured(i, true);
                // "Wilfridovargas" se deja escrito así, para fusionarlo después.
                if (item.has(ImportReview::Issue::DidYouMean)) review.edit(i, item.proposal);
            }
            expectEquals(review.confirm(review.readyRows()).confirmed, 8);
        }

        beginTest("Búsqueda sin tildes ni mayúsculas, por créditos, por el nombre original y por partes de palabras");
        {
            expectEquals((int) fx.library.search("volvere wilfrido", "").size(), 1);
            expectEquals((int) fx.library.search(utf8("NIÑA"), "").size(), 1);
            expectEquals((int) fx.library.search("nina", "").size(), 1);
            expectEquals((int) fx.library.search("vasquez", "").size(), 1, "por el compositor");
            expectEquals((int) fx.library.search("liz", "").size(), 1, "por la invitada");
            expectEquals((int) fx.library.search("wilf", "").size(), 4, "por el comienzo de una palabra (también \"Wilfridovargas\")");
            expectEquals((int) fx.library.search("mickey taveras", "MERENGUE").size(), 1, "con categoría");
            expectEquals((int) fx.library.search("nada que ver", "").size(), 0);
        }

        beginTest("La separación de artistas cuenta al intérprete aunque venga con invitados");
        {
            PlaylistGenerator::Pool pool;
            pool.performersOf = fx.library.getPerformerKeys();
            for (const auto& t : fx.library.search("", "")) pool.candidates.push_back(t);
            const auto juntitos = titled(fx.library, "A Dormir Juntitos");
            const auto keys = PlaylistGenerator::performerKeys(juntitos, pool);
            expect(keys.size() == 2 && keys[0] == "eddy herrera" && keys[1] == "liz");

            GeneratorSettings settings;
            settings.artistSeparation = 1;
            settings.titleSeparation = 0;
            settings.categorySeparation = 0;
            settings.lastPlayedSeparation = 0;
            settings.targetDurationVal = 1;
            juce::Random random(3);
            juce::String error;
            bool separated = true;
            for (int round = 0; round < 20; ++round) {
                const auto list = PlaylistGenerator::buildSequence(settings, pool, random, error);
                for (size_t i = 1; i < list.size(); ++i) {
                    const auto a = PlaylistGenerator::performerKeys(list[i - 1], pool);
                    const auto b = PlaylistGenerator::performerKeys(list[i], pool);
                    separated &= a.front() != b.front();
                }
            }
            expect(separated, "\"Eddy Herrera\" y \"Eddy Herrera feat. Liz\" nunca suenan seguidas");

            // Sin créditos (música de versiones anteriores), del artista guardado.
            TrackInfo legacy {};
            legacy.id = 999;
            legacy.artist = "Eddy Herrera feat. Liz & Mickey Taveras";
            const auto legacyKeys = PlaylistGenerator::performerKeys(legacy, pool);
            expectEquals((int) legacyKeys.size(), 3);
        }

        beginTest("Renombrar un artista cambia sus canciones y sus archivos, y el nombre anterior queda como alias");
        {
            const int eddy = artistId(fx.library, "Eddy Herrera");
            expect(eddy != -1);
            int relocated = 0;
            fx.library.onTrackRelocated = [&relocated](int, const juce::String&) { ++relocated; };
            const auto result = fx.library.renameArtist(eddy, utf8("Eddy Herrera Ortíz"));
            fx.library.onTrackRelocated = nullptr;
            expect(result.ok);
            expectEquals(result.tracksUpdated, 3);
            expectEquals(relocated, 3);

            const auto juntitos = titled(fx.library, "A Dormir Juntitos");
            expectEquals(juce::String::fromUTF8(juntitos.artist.c_str()), utf8("Eddy Herrera Ortíz feat. Liz"));
            expectEquals(juce::File(juntitos.path).getRelativePathFrom(fx.lab.root).replaceCharacter('\\', '/'),
                         utf8("Library/Musica/Eddy Herrera Ortíz/Eddy Herrera Ortíz feat. Liz - A Dormir Juntitos.wav"));
            expect(juce::File(juntitos.path).existsAsFile());
            expect(!fx.lab.root.getChildFile("Library/Musica/Eddy Herrera").exists(), "la carpeta vieja vacía se quita");
            expectEquals((int) fx.library.search("ortiz", "").size(), 3);

            const auto* known = fx.library.makeNamingContext({}).findKnown(TrackNaming::key("Eddy Herrera"));
            expect(known != nullptr && known->name == utf8("Eddy Herrera Ortíz"), "una importación futura con el nombre viejo lo reconoce");
        }

        beginTest("Renombrar con el nombre de otro artista avisa; fusionarlos junta sus canciones");
        {
            const int typo = artistId(fx.library, "Wilfridovargas");
            const int wilfrido = artistId(fx.library, "Wilfrido Vargas");
            expect(typo != -1 && wilfrido != -1);

            const auto conflict = fx.library.renameArtist(typo, "Wilfrido Vargas");
            expect(!conflict.ok);
            expectEquals(conflict.conflictId, wilfrido);

            const auto merged = fx.library.mergeArtists(typo, wilfrido);
            expect(merged.ok);
            expectEquals(merged.tracksUpdated, 1);
            expectEquals(artistId(fx.library, "Wilfridovargas"), -1);
            const auto bachata = titled(fx.library, "Bachata Merengue");
            expectEquals(juce::String::fromUTF8(bachata.artist.c_str()), juce::String("Wilfrido Vargas feat. Mickey Taveras"));
            expect(juce::File(bachata.path).isAChildOf(fx.lab.root.getChildFile("Library/Musica/Wilfrido Vargas")));
            expectEquals((int) fx.library.getTracksOfArtist(wilfrido).size(), 4);

            bool hasAlias = false;
            for (const auto& a : fx.library.getArtists())
                if (a.id == wilfrido)
                    for (const auto& alias : a.aliases) hasAlias |= alias == "wilfridovargas";
            expect(hasAlias, "la forma equivocada queda como alias");
            expect(fx.library.removeArtistAlias("wilfridovargas"));
        }

        beginTest("Deshacer una importación saca lo que sigue por revisar; lo confirmado se queda");
        {
            const auto more = outside.root.getChildFile("MAS");
            for (int i = 0; i < 3; ++i)
                writeWav(more.getChildFile("NUEVA " + juce::String(i) + " - GRUPO NUEVO.wav"), 2.0,
                         [i](double t) { return tone(t, 0.3f, 700.0 + 37.0 * i); });
            importAndWait(fx.library, more, "MERENGUE");
            expectEquals(fx.library.getPendingCount(), 3);

            {
                ImportReview review(fx.library);
                review.load(ImportReview::Scope::NewImports);
                expectEquals(review.confirm({ 0 }).confirmed, 1);
            }
            const auto batches = fx.library.getImportBatches();
            expect(!batches.empty() && batches.front().pending == 2 && batches.front().confirmed == 1);
            expectEquals(fx.library.undoImportBatch(batches.front().id), 2);
            expectEquals(fx.library.getPendingCount(), 0);
            expectEquals((int) fx.library.search("grupo nuevo", "").size(), 1, "la confirmada se queda");
            expect(fx.lab.root.getChildFile("Library/_Descartadas").findChildFiles(juce::File::findFiles, true).size() == 2,
                   "los archivos no se borran");
        }

        beginTest("La misma grabación con otro nombre y en otro formato se marca como duplicada por su sonido");
        {
            const auto copies = outside.root.getChildFile("COPIAS");
            juce::OggVorbisAudioFormat ogg;
            writeWav(copies.getChildFile("LA LLAVE DE MI CORAZON - JUAN LUIS GUERRA.wav"), 60.0, music(21, 60.0));
            writeAudio(ogg, copies.getChildFile("pista 03 sin nombre.ogg"), 60.2, music(21, 60.0, 0.2, 0.7f), {}, 16, 1);
            writeWav(copies.getChildFile("OTRA COSA - JUAN LUIS GUERRA.wav"), 60.0, music(22, 60.0));
            importAndWait(fx.library, copies, "MERENGUE");

            ImportReview review(fx.library);
            review.load(ImportReview::Scope::NewImports);
            expectEquals((int) review.getItems().size(), 3);
            int bySound = 0, llave = -1, pista = -1;
            for (const auto& item : review.getItems()) {
                const auto name = juce::String::fromUTF8(item.source.name.c_str());
                if (name.startsWith("LA LLAVE")) llave = item.track.id;
                if (name.startsWith("pista")) pista = item.track.id;
            }
            for (const auto& item : review.getItems()) {
                if (!item.duplicateBySound) continue;
                ++bySound;
                expect(item.state == ImportReview::State::Duplicate);
                expect((item.track.id == pista && item.duplicateOf == llave) || (item.track.id == llave && item.duplicateOf == pista),
                       "son las dos grabaciones iguales: " + item.duplicateText);
            }
            expectEquals(bySound, 1, "solo una de las dos iguales se marca como copia; la otra canción no");
        }
    }
};

//==============================================================================
class OnlineIdentityTests : public juce::UnitTest
{
public:
    OnlineIdentityTests() : juce::UnitTest("Identificación en línea", "RAAADIO") {}

    // Respuestas de ejemplo con la forma de las de AcoustID y MusicBrainz.
    static juce::String acoustIdFound()
    {
        return R"json({"status": "ok", "results": [
            {"id": "a1", "score": 0.97, "recordings": [
                {"id": "rec-1", "title": "Volveré", "duration": 248, "artists": [{"id": "x", "name": "Wilfrido Vargas"}]},
                {"id": "rec-2", "title": "Volveré (en vivo)", "duration": 301, "artists": [{"name": "Wilfrido Vargas"}]}]},
            {"id": "a2", "score": 0.41, "recordings": [{"id": "rec-3", "title": "Otra", "artists": [{"name": "Otro"}]}]}]})json";
    }

    static juce::String musicBrainzRecording()
    {
        return juce::String::fromUTF8(R"json({"id": "rec-1", "title": "Volveré", "first-release-date": "1986-05-01",
            "isrcs": ["USABC8600001"],
            "artist-credit": [
                {"name": "Wilfrido Vargas", "joinphrase": " feat. ", "artist": {"name": "Wilfrido Vargas"}},
                {"name": "Las Chicas del Can", "joinphrase": "", "artist": {"name": "Las Chicas del Can"}}],
            "relations": [{"type": "performance", "target-type": "work", "work": {"title": "Volveré", "relations": [
                {"type": "composer", "artist": {"name": "Wilfrido Vargas"}},
                {"type": "lyricist", "artist": {"name": "Ramón Orlando"}}]}}]})json");
    }

    struct FakeServices {
        std::vector<OnlineIdentifier::Request> requests;
        OnlineIdentifier::Response acoustId { 200, acoustIdFound() };
        OnlineIdentifier::Response musicBrainz { 200, musicBrainzRecording() };

        OnlineIdentifier::Transport transport()
        {
            return [this](const OnlineIdentifier::Request& r) {
                requests.push_back(r);
                return r.url.contains("acoustid") ? acoustId : musicBrainz;
            };
        }
    };

    static int importOne(LibraryFixture& fx, const juce::File& file)
    {
        fx.library.processAndAddTrack(file, "MERENGUE");
        return fx.library.getTrackByPath(fx.lab.root.getChildFile("Library/_Entrada/0").getChildFile(file.getFileName()).getFullPathName()).id;
    }

    static const ImportReview::Item* itemFor(const ImportReview& review, int trackId, int& row)
    {
        for (row = 0; row < (int) review.getItems().size(); ++row)
            if (review.getItems()[(size_t) row].track.id == trackId) return &review.getItems()[(size_t) row];
        return nullptr;
    }

    void runTest() override
    {
        TempLab outside;

        beginTest("Chromaprint: la huella de AcoustID sale de la DLL y es siempre la misma");
        {
            expect(Chromaprint::isAvailable(), "chromaprint.dll está junto al ejecutable");
            const auto file = writeWav(outside.root.getChildFile("huella.wav"), 60.0, music(31, 60.0));
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> first(formats.createReaderFor(file)), second(formats.createReaderFor(file));
            const auto a = Chromaprint::compute(*first), b = Chromaprint::compute(*second);
            expect(a.fingerprint.startsWith("AQA"), a.fingerprint.substring(0, 12));
            expectEquals(a.duration, 60);
            expectEquals(a.fingerprint, b.fingerprint);
            expect(Chromaprint::libraryVersion().startsWith("1.5"));
        }

        beginTest("AcoustID y MusicBrainz: candidatos, invitados, año, compositores e ISRC");
        {
            juce::String error;
            const auto candidates = OnlineMetadata::parseAcoustId(acoustIdFound(), error);
            expect(error.isEmpty());
            expectEquals((int) candidates.size(), 3);
            const auto* best = OnlineMetadata::best(candidates, 250.0);
            expect(best != nullptr && best->recordingId == "rec-1", "la más segura y de duración más parecida");

            OnlineMetadata::parseAcoustId(R"json({"status": "error", "error": {"code": 4, "message": "invalid API key"}})json", error);
            expectEquals(error, juce::String("invalid API key"));

            Database::OnlineMatch match;
            expect(OnlineMetadata::parseRecording(musicBrainzRecording(), match));
            expectEquals(juce::String::fromUTF8(match.artist.c_str()), juce::String("Wilfrido Vargas"));
            expect(match.featured.size() == 1 && match.featured[0] == "Las Chicas del Can");
            expectEquals(match.year, 1986);
            expectEquals(juce::String(match.isrc), juce::String("USABC8600001"));
            expect(match.composers.size() == 2 && juce::String::fromUTF8(match.composers[1].c_str()) == utf8("Ramón Orlando"));

            const auto p = OnlineMetadata::toProposal(match, TrackNaming::Context());
            expectEquals(p.credit(), juce::String("Wilfrido Vargas feat. Las Chicas del Can"));
            expectEquals(p.title, utf8("Volveré"));

            // Un artista de MusicBrainz no se separa aunque se parezca a un dúo, y la versión se aparta.
            Database::OnlineMatch duo;
            duo.artist = "Sandy & Papo";
            duo.title = "La Movida (en vivo)";
            const TrackNaming::Context knowsSandy({ { "sandy", { "Sandy", 3 } } }, {}, {}, {});
            const auto q = OnlineMetadata::toProposal(duo, knowsSandy);
            expectEquals(q.artist, juce::String("Sandy & Papo"));
            expect(q.featured.isEmpty());
            expectEquals(q.title, juce::String("La Movida"));
            expectEquals(q.version, juce::String("En Vivo"));
        }

        LibraryFixture fx;
        fx.library.addCategory("MERENGUE");
        FakeServices services;
        OnlineIdentifier identifier(fx.library, services.transport());
        identifier.setApiKey("clave123");

        beginTest("Una canción sin nombre se identifica en línea; la revisión lo sugiere y al confirmar guarda su identificador");
        {
            const int id = importOne(fx, writeWav(outside.root.getChildFile("pista 03.wav"), 60.0, music(31, 60.0)));
            expect(id != -1);
            expect(identifier.identify(id) == OnlineIdentifier::Outcome::Found);

            expectEquals((int) services.requests.size(), 2);
            const auto& lookup = services.requests[0];
            expect(lookup.body.contains("client=clave123") && lookup.body.contains("duration=60") && lookup.body.contains("fingerprint=AQA"),
                   "a AcoustID solo va la huella y la duración");
            expect(lookup.headers.contains("User-Agent: RAAADIO/"));
            expect(services.requests[1].url.contains("recording/rec-1") && services.requests[1].url.contains("inc="));
            expect(services.requests[1].body.isEmpty(), "a MusicBrainz, GET");

            ImportReview review(fx.library);
            review.load(ImportReview::Scope::NewImports);
            int row = -1;
            const auto* item = itemFor(review, id, row);
            expect(item != nullptr && item->has(ImportReview::Issue::OnlineDiffers));
            expect(item != nullptr && item->state == ImportReview::State::Check, "se sugiere, no se aplica sola");
            expectEquals((int) review.onlineRows(true).size(), 1);

            review.acceptOnline(review.onlineRows(true));
            item = itemFor(review, id, row);
            expect(item != nullptr && item->state == ImportReview::State::Ready);
            if (item != nullptr) {
                expectEquals(item->proposal.credit(), juce::String("Wilfrido Vargas feat. Las Chicas del Can"));
                expectEquals(item->proposal.year, 1986);
                expectEquals(item->proposal.composers.size(), 2);
            }
            expectEquals(review.confirm({ row }).confirmed, 1);

            const auto ids = fx.library.getDatabase().getOnlineIds(id);
            expectEquals(juce::String(ids.first), juce::String("rec-1"));
            expectEquals(juce::String(ids.second), juce::String("USABC8600001"));
            expect(juce::File(fx.library.getTrackById(id).path).isAChildOf(fx.lab.root.getChildFile("Library/Musica/Wilfrido Vargas")));
        }

        beginTest("Si coincide con lo propuesto, confirma el formato y completa año y compositores");
        {
            services.requests.clear();
            const int id = importOne(fx, writeWav(outside.root.getChildFile("WILFRIDO VARGAS - VOLVERE.wav"), 45.0, music(32, 45.0)));
            expect(identifier.identify(id) == OnlineIdentifier::Outcome::Found);

            ImportReview review(fx.library);
            review.load(ImportReview::Scope::NewImports);
            int row = -1;
            const auto* item = itemFor(review, id, row);
            expect(item != nullptr && item->onlineAgrees);
            expect(item != nullptr && item->state == ImportReview::State::Ready, "la carpeta de un solo archivo ya no pide elegir el formato");
            if (item != nullptr) {
                expectEquals(item->proposal.year, 1986);
                expectEquals(item->proposal.composers.joinIntoString("|"), utf8("Wilfrido Vargas|Ramón Orlando"));
                expectEquals(item->proposal.title, utf8("Volveré"), "la tilde que el nombre del archivo había perdido");
            }
            expectEquals(TrackNaming::withAccentsFrom("VOLVERE", utf8("Volveré")), utf8("VOLVERÉ"), "con las mayúsculas propias");
            expectEquals(TrackNaming::withAccentsFrom("Volvere", "Otra"), juce::String("Volvere"));
        }

        beginTest("Errores: una clave inválida o sin conexión quedan para reintentar; lo desconocido no se vuelve a buscar");
        {
            const int invalid = importOne(fx, writeWav(outside.root.getChildFile("otra 1.wav"), 40.0, music(33, 40.0)));
            services.acoustId = { 200, R"json({"status": "error", "error": {"code": 4, "message": "invalid API key"}})json" };
            expect(identifier.identify(invalid) == OnlineIdentifier::Outcome::Error);
            expectEquals(identifier.getStatus().lastError, juce::String("invalid API key"));

            const int offline = importOne(fx, writeWav(outside.root.getChildFile("otra 2.wav"), 41.0, music(34, 41.0)));
            services.acoustId = { 0, {} };
            expect(identifier.identify(offline) == OnlineIdentifier::Outcome::Error);

            const int unknown = importOne(fx, writeWav(outside.root.getChildFile("otra 3.wav"), 42.0, music(35, 42.0)));
            services.acoustId = { 200, R"json({"status": "ok", "results": []})json" };
            expect(identifier.identify(unknown) == OnlineIdentifier::Outcome::NotFound);

            const auto again = fx.library.getDatabase().getTracksNeedingOnlineLookup();
            auto has = [&again](int id) { return std::find(again.begin(), again.end(), id) != again.end(); };
            expect(has(invalid) && has(offline), "los errores se reintentan en otra sesión");
            expect(!has(unknown), "lo que AcoustID no conoce no se vuelve a preguntar");
        }

        beginTest("Sin clave no se busca nada");
        {
            identifier.setApiKey({});
            const int id = importOne(fx, writeWav(outside.root.getChildFile("otra 4.wav"), 43.0, music(36, 43.0)));
            const auto before = services.requests.size();
            expect(identifier.identify(id) == OnlineIdentifier::Outcome::Skipped);
            expectEquals(services.requests.size(), before);
        }
    }
};

//==============================================================================
class SongSearchTests : public juce::UnitTest
{
public:
    SongSearchTests() : juce::UnitTest("Buscar canciones en internet", "RAAADIO") {}

    // Respuestas con la forma de las de Deezer y YouTube (recortadas de respuestas reales).
    static juce::String deezerSearch()
    {
        return juce::String::fromUTF8(R"json({"data": [
            {"id": 1857061907, "type": "track", "title": "Burbujas de Amor", "title_short": "Burbujas de Amor", "title_version": "",
             "isrc": "USJ3V1862529", "link": "https://www.deezer.com/track/1857061907", "duration": 250, "rank": 552338,
             "explicit_lyrics": false, "artist": {"id": 1, "name": "Juan Luis Guerra"},
             "album": {"title": "Grandes Éxitos", "cover_medium": "https://cdn-images.dzcdn.net/cover/250x250.jpg"}},
            {"id": 3530820701, "type": "track", "title": "Burbujas de Amor (2025 Remastered)", "title_short": "Burbujas de Amor",
             "title_version": "(2025 Remastered)", "duration": 249, "artist": {"name": "Juan Luis Guerra"}, "album": {"title": "Grandes Éxitos (2025 Remastered)"}},
            {"id": 0, "type": "track", "title": "Sin id"}],
            "total": 3})json");
    }

    static juce::String deezerTrack()
    {
        return R"json({"id": 1, "title": "Frio, Frio", "release_date": "2012-11-06", "isrc": "USX1", "duration": 236,
            "contributors": [{"name": "Juan Luis Guerra", "role": "Main"}, {"name": "Romeo Santos", "role": "Featured"}]})json";
    }

    // Una canción de YouTube Music: título; "Artista • Álbum • 4:09"; reproducciones.
    static std::string musicItem(const std::string& id, const std::string& title, const std::string& artists,
                                 const std::string& album, const std::string& duration,
                                 const std::string& type = "MUSIC_VIDEO_TYPE_ATV")
    {
        auto page = [](const std::string& t) {
            return R"(, "navigationEndpoint": {"browseEndpoint": {"browseEndpointContextSupportedConfigs": {"browseEndpointContextMusicConfig": {"pageType": ")" + t + "\"}}}}";
        };
        return R"({"musicResponsiveListItemRenderer": {"playlistItemData": {"videoId": ")" + id + R"("}, "flexColumns": [
            {"musicResponsiveListItemFlexColumnRenderer": {"text": {"runs": [{"text": ")" + title
            + R"(", "navigationEndpoint": {"watchEndpoint": {"videoId": ")" + id
            + R"(", "watchEndpointMusicSupportedConfigs": {"watchEndpointMusicConfig": {"musicVideoType": ")" + type + R"("}}}}}]}}},
            {"musicResponsiveListItemFlexColumnRenderer": {"text": {"runs": [{"text": ")" + artists + "\"" + page("MUSIC_PAGE_TYPE_ARTIST")
            + R"(}, {"text": " • "}, {"text": ")" + album + "\"" + page("MUSIC_PAGE_TYPE_ALBUM")
            + R"(}, {"text": " • "}, {"text": ")" + duration + R"("}]}}},
            {"musicResponsiveListItemFlexColumnRenderer": {"text": {"runs": [{"text": "267 M reproducciones"}]}}}]}})";
    }

    static juce::String musicSearch(const std::vector<std::string>& items)
    {
        std::string joined;
        for (const auto& i : items) joined += (joined.empty() ? "" : ", ") + i;
        // Un artista entre las canciones (sin videoId): no es una canción y se salta.
        joined += R"(, {"musicResponsiveListItemRenderer": {"flexColumns": [{"musicResponsiveListItemFlexColumnRenderer": {"text": {"runs": [{"text": "Juan Luis Guerra"}]}}}]}})";
        return juce::String::fromUTF8((R"({"contents": {"tabbedSearchResultsRenderer": {"tabs": [{"tabRenderer": {"content": {"sectionListRenderer": {"contents": [{"musicShelfRenderer": {"contents": [)"
                                       + joined + "]}}]}}}}]}}}").c_str());
    }

    static std::string webItem(const std::string& id, const std::string& title, const std::string& channel,
                               const std::string& length, const std::string& badge = "")
    {
        return R"({"videoRenderer": {"videoId": ")" + id + R"(", "title": {"runs": [{"text": ")" + title
             + R"("}]}, "ownerText": {"runs": [{"text": ")" + channel + R"("}]}, "lengthText": {"simpleText": ")" + length
             + R"("}, "viewCountText": {"simpleText": "31.385.024 visualizaciones"}, "ownerBadges": [)"
             + (badge.empty() ? std::string() : R"({"metadataBadgeRenderer": {"style": ")" + badge + "\"}}") + "]}}";
    }

    static juce::String webSearch(const std::vector<std::string>& items)
    {
        std::string joined;
        for (const auto& i : items) joined += (joined.empty() ? "" : ", ") + i;
        return juce::String::fromUTF8((R"({"contents": {"twoColumnSearchResultsRenderer": {"primaryContents": {"sectionListRenderer": {"contents": [{"itemSectionRenderer": {"contents": [)"
                                       + joined + "]}}]}}}}}").c_str());
    }

    static SongSearch::Track track(const char* artist, const char* title, int duration, const char* version = "")
    {
        SongSearch::Track t;
        t.id = 7;
        t.artist = juce::String::fromUTF8(artist);
        t.titleShort = juce::String::fromUTF8(title);
        t.version = juce::String::fromUTF8(version);
        t.title = (t.titleShort + " " + t.version).trim();
        t.duration = duration;
        return t;
    }

    static SongSearch::Video video(const char* title, const char* artists, int duration, bool music = true,
                                   SongSearch::Video::Kind kind = SongSearch::Video::Kind::OfficialAudio)
    {
        SongSearch::Video v;
        v.id = "x";
        v.title = juce::String::fromUTF8(title);
        v.artists = juce::String::fromUTF8(artists);
        v.duration = duration;
        v.fromMusic = music;
        v.kind = kind;
        return v;
    }

    // Servicios de mentira: responden según la dirección y guardan lo que se pidió.
    struct FakeServices {
        std::mutex mutex;
        std::vector<HttpTransport::Request> requests;
        HttpTransport::Response deezer { 200, deezerSearch(), {} };
        HttpTransport::Response detail { 200, deezerTrack(), {} };
        HttpTransport::Response music { 200, musicSearch({ musicItem("v0ckuv1xBm0", "Burbujas De Amor", "Juan Luis Guerra 4.40", "Romance Rosa", "4:09") }), {} };
        HttpTransport::Response web { 200, webSearch({}), {} };

        HttpTransport::Transport transport()
        {
            return [this](const HttpTransport::Request& r) {
                std::lock_guard<std::mutex> guard(mutex);
                requests.push_back(r);
                if (r.url.contains("api.deezer.com/search")) return deezer;
                if (r.url.contains("api.deezer.com/track")) return detail;
                if (r.url.contains("music.youtube.com")) return music;
                if (r.url.contains("www.youtube.com")) return web;
                return HttpTransport::Response { 404, {}, {} };
            };
        }

        // Cambiar una respuesta mientras el hilo de la búsqueda puede estar pidiendo.
        void set(HttpTransport::Response& target, HttpTransport::Response value)
        {
            std::lock_guard<std::mutex> guard(mutex);
            target = std::move(value);
        }

        int count(const juce::String& host)
        {
            std::lock_guard<std::mutex> guard(mutex);
            int n = 0;
            for (const auto& r : requests) n += r.url.contains(host) ? 1 : 0;
            return n;
        }
    };

    template <typename Predicate>
    static bool waitFor(Predicate done, int timeoutMs = 5000)
    {
        const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (!done()) {
            if (juce::Time::getMillisecondCounter() > until) return false;
            juce::Thread::sleep(5);
        }
        return true;
    }

    static SongSearcher::Links linksWhenDone(SongSearcher& searcher, juce::int64 id, int timeoutMs = 5000)
    {
        SongSearcher::Links links;
        waitFor([&] { return searcher.getLinks(id, links) && links.state != SongSearcher::Links::State::Pending; }, timeoutMs);
        return links;
    }

    // Un servidor HTTP de una sola respuesta en 127.0.0.1: devuelve lo que recibió.
    static juce::String receiveOneRequest(juce::StreamingSocket& server)
    {
        std::unique_ptr<juce::StreamingSocket> connection(server.waitForNextConnection());
        if (connection == nullptr) return {};
        juce::MemoryBlock received;
        char buffer[4096];
        for (;;) {
            const juce::String text(static_cast<const char*>(received.getData()), received.getSize());
            const int headerEnd = text.indexOf("\r\n\r\n");
            if (headerEnd >= 0) {
                const auto length = text.substring(0, headerEnd).fromFirstOccurrenceOf("content-length:", false, true)
                                        .upToFirstOccurrenceOf("\r\n", false, false).trim().getIntValue();
                if ((int) received.getSize() >= headerEnd + 4 + length) break;
            }
            if (connection->waitUntilReady(true, 5000) != 1) break;
            const int n = connection->read(buffer, (int) sizeof(buffer), false);
            if (n <= 0) break;
            received.append(buffer, (size_t) n);
        }
        const juce::String reply("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}");
        connection->write(reply.toRawUTF8(), (int) reply.getNumBytesAsUTF8());
        return juce::String::fromUTF8(static_cast<const char*>(received.getData()), (int) received.getSize());
    }

    void runTest() override
    {
        beginTest("HTTP: un POST lleva el cuerpo tal cual y deja los parámetros en la dirección");
        {
            // JUCE pasaba "?prettyPrint=false" al cuerpo y YouTube respondía 400 (JSON inválido).
            juce::StreamingSocket server;
            expect(server.createListener(0, "127.0.0.1"));
            HttpTransport::Response response;
            const auto json = juce::String(R"({"query": "x"})");
            std::thread client([&] {
                response = HttpTransport::send({ "http://127.0.0.1:" + juce::String(server.getBoundPort()) + "/search?prettyPrint=false",
                                                 json, "Content-Type: application/json" });
            });
            const auto request = receiveOneRequest(server);
            client.join();
            expectEquals(response.status, 200);
            expect(request.startsWith("POST /search?prettyPrint=false HTTP/1."), request.upToFirstOccurrenceOf("\r\n", false, false));
            expectEquals(request.fromFirstOccurrenceOf("\r\n\r\n", false, false), json);
            expect(request.contains("Content-Type: application/json"));
        }

        beginTest("Deezer: resultados con versión, ISRC y carátula; el error del servicio se lee");
        {
            juce::String error;
            const auto tracks = SongSearch::parseDeezerSearch(deezerSearch(), error);
            expect(error.isEmpty());
            expectEquals((int) tracks.size(), 2, "la fila sin id se salta");
            if (tracks.size() != 2) return;
            expect(tracks[0].id == 1857061907LL);
            expectEquals(tracks[0].artist, juce::String("Juan Luis Guerra"));
            expectEquals(tracks[0].album, juce::String::fromUTF8("Grandes Éxitos"));
            expectEquals(tracks[0].isrc, juce::String("USJ3V1862529"));
            expectEquals(tracks[0].duration, 250);
            expect(tracks[0].coverUrl.endsWith("250x250.jpg"));
            expectEquals(tracks[1].titleShort, juce::String("Burbujas de Amor"));
            expectEquals(tracks[1].version, juce::String("(2025 Remastered)"));
            expect(tracks[1].id == 3530820701LL, "los id de Deezer no caben en un int");

            SongSearch::parseDeezerSearch(R"json({"error": {"type": "Exception", "message": "Quota limit exceeded", "code": 4}})json", error);
            expectEquals(error, juce::String("Quota limit exceeded"));
            SongSearch::parseDeezerSearch("<html>", error);
            expect(error.isNotEmpty());

            expect(SongSearch::deezerSearchUrl(juce::String::fromUTF8("Ojalá que llueva café")).startsWith("https://api.deezer.com/search/track?q=Ojal%C3%A1%20que"));
        }

        beginTest("Deezer: el detalle da el año y los invitados");
        {
            auto t = track("Juan Luis Guerra", "Frio, Frio", 0);
            expect(SongSearch::parseDeezerTrack(deezerTrack(), t));
            expectEquals(t.year, 2012);
            expectEquals(t.featured.joinIntoString(","), juce::String("Romeo Santos"));
            expectEquals(t.duration, 236);
            expect(!SongSearch::parseDeezerTrack(R"json({"error": {"message": "no data"}})json", t));
        }

        beginTest("YouTube Music: canciones con artista, álbum, duración y tipo; lo que no es canción se salta");
        {
            const auto videos = SongSearch::parseYouTubeMusicSearch(musicSearch({
                musicItem("v0ckuv1xBm0", "Burbujas De Amor", "Juan Luis Guerra 4.40", "Romance Rosa", "4:09"),
                musicItem("cEoJWnTXyLY", "A Palo Limpio", "Kinito Mendez y Enerolisa Nuñez", "1", "4:53", "MUSIC_VIDEO_TYPE_OMV"),
            }));
            expectEquals((int) videos.size(), 2);
            if (videos.size() != 2) return;
            expectEquals(videos[0].id, juce::String("v0ckuv1xBm0"));
            expectEquals(videos[0].title, juce::String("Burbujas De Amor"));
            expectEquals(videos[0].artists, juce::String("Juan Luis Guerra 4.40"));
            expectEquals(videos[0].album, juce::String("Romance Rosa"));
            expectEquals(videos[0].duration, 249);
            expect(videos[0].kind == SongSearch::Video::Kind::OfficialAudio && videos[0].fromMusic);
            expectEquals(videos[0].views, juce::String("267 M reproducciones"));
            expectEquals(videos[1].artists, juce::String::fromUTF8("Kinito Mendez y Enerolisa Nuñez"));
            expect(videos[1].kind == SongSearch::Video::Kind::OfficialVideo);
            expect(SongSearch::parseYouTubeMusicSearch("not json").empty());
        }

        beginTest("YouTube: videos con canal, duración y si el canal es del artista");
        {
            const auto videos = SongSearch::parseYouTubeSearch(webSearch({
                webItem("v0ckuv1xBm0", "Burbujas De Amor", "Juan Luis Guerra", "4:09", "BADGE_STYLE_TYPE_VERIFIED_ARTIST"),
                webItem("abc", "Burbujas De Amor", "Juan Luis Guerra 4.40 - Topic", "4:10"),
                webItem("5iXOnXazxkk", "JUAN LUIS GUERRA SUS MEJORES EXITOS", "Voces Gruperas", "2:00:47"),
            }));
            expectEquals((int) videos.size(), 3);
            if (videos.size() != 3) return;
            expect(videos[0].kind == SongSearch::Video::Kind::OfficialVideo && !videos[0].fromMusic);
            expect(videos[1].kind == SongSearch::Video::Kind::OfficialAudio);
            expectEquals(videos[1].artists, juce::String("Juan Luis Guerra 4.40"));
            expectEquals(videos[2].duration, 7247);
            expect(videos[2].kind == SongSearch::Video::Kind::Unknown);
        }

        beginTest("Pedidos a YouTube: la búsqueda, el filtro de canciones y el cliente van en el JSON");
        {
            const auto music = SongSearch::youTubeMusicSearch(juce::String::fromUTF8("Juan Luis Guerra Ojalá \"café\""), "es");
            expectEquals(music.url, juce::String("https://music.youtube.com/youtubei/v1/search?prettyPrint=false"));
            const auto body = juce::JSON::parse(music.body);
            expectEquals(body["query"].toString(), juce::String::fromUTF8("Juan Luis Guerra Ojalá \"café\""));
            expectEquals(body["context"]["client"]["clientName"].toString(), juce::String("WEB_REMIX"));
            expectEquals(body["context"]["client"]["hl"].toString(), juce::String("es"));
            expect(body["params"].toString().isNotEmpty());
            expect(music.headers.contains("Content-Type: application/json") && music.headers.contains("X-YouTube-Client-Name: 67"));

            const auto web = SongSearch::youTubeSearch("x", "en");
            expect(web.url.startsWith("https://www.youtube.com/youtubei/v1/search"));
            expectEquals(juce::JSON::parse(web.body)["context"]["client"]["clientName"].toString(), juce::String("WEB"));

            expectEquals(SongSearch::parseDuration("4:09"), 249);
            expectEquals(SongSearch::parseDuration("1:02:03"), 3723);
            expectEquals(SongSearch::parseDuration("EN VIVO"), 0);
            expectEquals(SongSearch::parseDuration("4"), 0);
            expectEquals(SongSearch::youTubeUrl("v0ckuv1xBm0"), juce::String("https://www.youtube.com/watch?v=v0ckuv1xBm0"));

            // En vivo se busca como tal; "Remastered" no cambia la grabación.
            expectEquals(SongSearch::youTubeQuery(track("Juan Luis Guerra", "Frio, Frio", 236, "(En Vivo)")), juce::String("Juan Luis Guerra Frio, Frio En Vivo"));
            expectEquals(SongSearch::youTubeQuery(track("Juan Luis Guerra", "Burbujas de Amor", 249, "(2025 Remastered)")), juce::String("Juan Luis Guerra Burbujas de Amor"));
        }

        beginTest("Comparar: el audio oficial coincide; covers, en vivo, karaoke y recopilaciones quedan abajo");
        {
            const auto burbujas = track("Juan Luis Guerra", "Burbujas de Amor", 250);
            auto score = [&](const SongSearch::Video& v) { return SongSearch::matchScore(burbujas, v); };

            expect(score(video("Burbujas De Amor", "Juan Luis Guerra 4.40", 249)) >= SongSearch::goodMatch, "el audio oficial");
            expect(score(video("Burbujas de amor", "Caracol Televisión y Catalexia", 195)) < SongSearch::possibleMatch, "otro intérprete");
            expect(score(video("Burbujas de Amor", "Banda Sinaloense Los Recoditos", 273)) < SongSearch::possibleMatch, "otro intérprete, duración parecida");
            expect(score(video("Burbujas de Amor (Karaoke Version)", "Juan Luis Guerra", 250)) < SongSearch::possibleMatch, "karaoke");
            const double live = score(video("Burbujas de Amor (En Vivo)", "Juan Luis Guerra 4.40", 262));
            expect(live < SongSearch::goodMatch && live >= SongSearch::possibleMatch, "en vivo: parecida, no la misma");

            // YouTube: el título lleva el artista y "(Video Oficial)"; las recopilaciones no coinciden.
            expect(score(video("Juan Luis Guerra 4-40 - Burbujas De Amor (Video Oficial)", "Karen Records", 251, false,
                               SongSearch::Video::Kind::Unknown)) >= SongSearch::goodMatch);
            expect(score(video("JUAN LUIS GUERRA SUS MEJORES EXITOS 2026 LAS 30 MEJORES CANCIONES", "Voces Gruperas", 7247, false,
                               SongSearch::Video::Kind::Unknown)) < SongSearch::possibleMatch);

            // Un artículo de más y 5 s de diferencia (otra edición del mismo disco) siguen coincidiendo.
            expect(SongSearch::matchScore(track("Kinito Méndez", "El Suero De Amor", 279), video("Suero de Amor", "Kinito Mendez", 284))
                   >= SongSearch::goodMatch);

            std::vector<SongSearch::Video> videos {
                video("Burbujas de amor", "Caracol Televisión y Catalexia", 195),
                video("Burbujas de Amor (En Vivo)", "Juan Luis Guerra 4.40", 262),
                video("Burbujas De Amor", "Juan Luis Guerra 4.40", 249),
            };
            videos[2].id = "best";
            SongSearch::rank(burbujas, videos);
            expectEquals(videos[0].id, juce::String("best"));
            expect(videos[0].score >= videos[1].score && videos[1].score >= videos[2].score);

            // Otra canción del mismo artista, con la misma duración, no es "parecida": no se muestra
            // (pasó con la búsqueda real de YouTube Music).
            std::vector<SongSearch::Video> others { video("La Bilirrubina", "Juan Luis Guerra 4.40", 246),
                                                    video("Como Abeja al Panal", "Juan Luis Guerra 4.40", 250) };
            expect(SongSearch::matchScore(burbujas, others[0]) < SongSearch::otherSong);
            SongSearch::rank(burbujas, others);
            expect(others.empty());
        }

        beginTest("Biblioteca: la misma canción aunque el artista o las tildes se escriban distinto");
        {
            std::vector<Database::KeyRow> rows {
                { 1, TrackNaming::matchKey("Juan Luis Guerra 4.40", "Burbujas de amor").toStdString(), "Juan Luis Guerra 4.40", "Burbujas de amor", 251 },
                { 2, TrackNaming::matchKey("WILFRIDO VARGAS", juce::String::fromUTF8("VOLVERÉ")).toStdString(), "WILFRIDO VARGAS", "VOLVERE", 248 },
            };
            expectEquals(SongSearch::findInLibrary(track("Juan Luis Guerra", "Burbujas de Amor", 250), rows), 1);
            expectEquals(SongSearch::findInLibrary(track("Wilfrido Vargas", "Volvere", 248), rows), 2);
            expectEquals(SongSearch::findInLibrary(track("Grupo Extra", "Burbujas de Amor", 245), rows), -1);
            expectEquals(SongSearch::findInLibrary(track("Juan Luis Guerra", "La Bilirrubina", 214), rows), -1);
        }

        beginTest("En su hilo: la búsqueda y el enlace; lo ya encontrado no se vuelve a pedir");
        {
            FakeServices services;
            SongSearcher searcher(services.transport(), "es");
            searcher.setYouTubeInterval(0);

            searcher.search("Juan Luis Guerra Burbujas");
            expect(waitFor([&] { return searcher.getResults().generation == 1; }));
            const auto results = searcher.getResults();
            expectEquals((int) results.tracks.size(), 2);
            if (results.tracks.size() != 2) return;
            expect(!results.working && results.error.isEmpty());
            expect(services.requests.front().headers.contains("User-Agent: RAAADIO/"));

            searcher.resolve(results.tracks[0]);
            const auto links = linksWhenDone(searcher, results.tracks[0].id);
            expect(links.state == SongSearcher::Links::State::Done);
            expectEquals((int) links.videos.size(), 1);
            if (links.videos.size() != 1) return;
            expectEquals(links.videos[0].id, juce::String("v0ckuv1xBm0"));
            expect(links.videos[0].score >= SongSearch::goodMatch);
            expectEquals(links.track.year, 2012, "con el detalle de Deezer");
            expectEquals(services.count("music.youtube.com"), 1);
            expectEquals(services.count("www.youtube.com"), 0, "YouTube Music coincidió: no hace falta buscar en YouTube");

            searcher.resolve(results.tracks[0]);
            juce::Thread::sleep(50);
            expectEquals(services.count("music.youtube.com"), 1, "ya estaba");
        }

        beginTest("En su hilo: si YouTube Music no la tiene se busca en YouTube; si YouTube pide esperar, se espera");
        {
            FakeServices services;
            services.music.body = musicSearch({ musicItem("cover", "Burbujas de amor", "Caracol Televisión", "La Descarga", "3:15") });
            services.web.body = webSearch({ webItem("v0ckuv1xBm0", "Burbujas De Amor", "Juan Luis Guerra", "4:09", "BADGE_STYLE_TYPE_VERIFIED_ARTIST") });
            SongSearcher searcher(services.transport(), "es");
            searcher.setYouTubeInterval(0);

            auto burbujas = track("Juan Luis Guerra", "Burbujas de Amor", 250);
            searcher.resolve(burbujas);
            auto links = linksWhenDone(searcher, burbujas.id);
            expectEquals(services.count("www.youtube.com"), 1);
            expect(links.videos.size() == 2 && links.videos[0].id == "v0ckuv1xBm0" && !links.videos[0].fromMusic);

            // Sin conexión: error, y se puede volver a pedir.
            services.set(services.music, { 0, {}, {} });
            services.set(services.web, { 0, {}, {} });
            auto other = track("Kinito Méndez", "El Suero De Amor", 279);
            other.id = 8;
            searcher.resolve(other);
            links = linksWhenDone(searcher, other.id);
            expect(links.state == SongSearcher::Links::State::Error);
            expectEquals(links.error, juce::String("offline"));

            // HTTP 429: YouTube pidió esperar; nada más se le pide durante un rato.
            services.set(services.music, { 429, {}, {} });
            auto third = track("Wilfrido Vargas", "Volvere", 248);
            third.id = 9;
            searcher.resolve(third);
            links = linksWhenDone(searcher, third.id);
            expectEquals(links.error, juce::String("youtube_wait"));
            const int before = services.count("youtube.com");
            auto fourth = track("Eddy Herrera", "Carolina", 240);
            fourth.id = 10;
            searcher.resolve(fourth);
            links = linksWhenDone(searcher, fourth.id);
            expectEquals(links.error, juce::String("youtube_wait"));
            expectEquals(services.count("youtube.com"), before);
        }

        // Contra los servicios de verdad, solo si se pide (RAAADIO_ONLINE_TESTS=1): avisa si
        // Deezer o YouTube cambiaron la forma de sus respuestas.
        if (juce::SystemStats::getEnvironmentVariable("RAAADIO_ONLINE_TESTS", {}) == "1")
        {
            beginTest("En línea de verdad: Deezer y YouTube Music responden como se espera");
            SongSearcher searcher({}, "es");
            searcher.search("Juan Luis Guerra Burbujas de amor");
            expect(waitFor([&] { return searcher.getResults().generation == 1; }, 20000), "Deezer respondió");
            const auto results = searcher.getResults();
            expect(results.error.isEmpty(), results.error);
            expect(!results.tracks.empty(), "Deezer encontró la canción");
            if (results.tracks.empty()) return;
            const auto& first = results.tracks.front();
            logMessage("Deezer: " + first.artist + " - " + first.title + " (" + first.album + ", " + first.isrc + ")");

            searcher.resolve(first);
            const auto links = linksWhenDone(searcher, first.id, 40000);
            expect(links.state == SongSearcher::Links::State::Done, links.error);
            expect(!links.videos.empty() && links.videos.front().score >= SongSearch::goodMatch, "YouTube Music la tiene");
            expect(links.cover.isValid(), "carátula");
            expect(links.track.year > 0, "año del detalle");
            for (const auto& v : links.videos)
                logMessage(juce::String(v.score, 2) + "  " + SongSearch::youTubeUrl(v.id) + "  " + v.title + " | " + v.artists
                           + " | " + juce::String(v.duration) + " s" + (v.fromMusic ? " | YouTube Music" : " | YouTube"));
        }
    }
};

//==============================================================================
class SongDownloadTests : public juce::UnitTest
{
public:
    SongDownloadTests() : juce::UnitTest("Descargar canciones", "RAAADIO") {}

    template <typename Predicate>
    static bool waitFor(Predicate done, int timeoutMs = 10000)
    {
        const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (!done()) {
            if (juce::Time::getMillisecondCounter() > until) return false;
            juce::Thread::sleep(5);
        }
        return true;
    }

    // Un MP3 de verdad (LAME), como el que deja FFmpeg: un tono de unos segundos.
    static juce::MemoryBlock encodeMp3(double seconds)
    {
        Mp3Encoder encoder;
        juce::MemoryBlock out;
        if (!encoder.open(44100.0, 128)) return out;
        std::vector<float> left(1024), right(1024);
        const int total = (int) (seconds * 44100.0);
        for (int pos = 0; pos < total; pos += 1024) {
            const int n = juce::jmin(1024, total - pos);
            for (int i = 0; i < n; ++i)
                left[(size_t) i] = right[(size_t) i] = 0.3f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * (pos + i) / 44100.0)
                                                     * (0.6f + 0.4f * (float) std::sin((pos + i) / 9000.0));
            encoder.encode(left.data(), right.data(), n, out);
        }
        encoder.flush(out);
        return out;
    }

    // yt-dlp de mentira: deja el MP3 donde lo pide "-o", o responde con un error, o espera
    // hasta que lo cancelen.
    struct FakeYtDlp {
        std::atomic<int> downloads { 0 }, updates { 0 };
        juce::MemoryBlock mp3;
        juce::String errorOutput;
        std::atomic<bool> block { false };

        SongDownloader::Runner runner()
        {
            return [this](const juce::StringArray& command, const ProcessRunner::LineCallback& onLine, const ProcessRunner::StopCheck& shouldStop) {
                if (command.contains("-U")) { ++updates; return 0; }
                ++downloads;
                if (block) {
                    onLine("[raaadio]  10.0%");
                    while (!shouldStop()) juce::Thread::sleep(5);
                    return (int) ProcessRunner::stopped;
                }
                if (errorOutput.isNotEmpty()) {
                    onLine("[youtube] Extracting URL");
                    onLine(errorOutput);
                    return 1;
                }
                const auto& outputTemplate = command[command.indexOf("-o") + 1];
                const auto id = command[command.size() - 1].fromLastOccurrenceOf("v=", false, false);
                const auto file = juce::File(outputTemplate).getParentDirectory().getChildFile(id + ".mp3");
                file.replaceWithData(mp3.getData(), mp3.getSize());
                onLine("[raaadio]  50.0%");
                onLine("[raaadio] 100.0%");
                onLine(file.getFullPathName());
                return 0;
            };
        }
    };

    static SongDownloader::Services fakeServices(const juce::File& root, FakeYtDlp& yt)
    {
        SongDownloader::Services s;
        s.toolsFolder = root.getChildFile("Herramientas");
        s.toolsFolder.getChildFile("yt-dlp.exe").create();
        s.toolsFolder.getChildFile("ffmpeg").getChildFile("ffmpeg.exe").create();
        s.workFolder = root.getChildFile("Trabajo");
        s.pathVariable = juce::String();
        s.minGapSeconds = 0;
        s.maxJitterSeconds = 0;
        s.run = yt.runner();
        return s;
    }

    static SongSearch::Track frioFrio()
    {
        SongSearch::Track t;
        t.id = 42;
        t.artist = "Juan Luis Guerra";
        t.featured = { "Romeo Santos" };
        t.titleShort = juce::String::fromUTF8("Frío, Frío");
        t.version = "(En Vivo)";
        t.title = t.titleShort + " " + t.version;
        t.album = "Asondeguerra Tour";
        t.isrc = "USJ3V1200001";
        t.year = 2012;
        return t;
    }

    static SongSearch::Video video(const char* id)
    {
        SongSearch::Video v;
        v.id = id;
        v.score = 1.0;
        return v;
    }

    static std::optional<SongDownloader::Job> finished(SongDownloader& d, int jobId, int timeoutMs = 20000)
    {
        std::optional<SongDownloader::Job> result;
        waitFor([&] {
            for (const auto& j : d.getJobs())
                if (j.id == jobId && j.isFinished()) { result = j; return true; }
            return false;
        }, timeoutMs);
        return result;
    }

    void runTest() override
    {
        beginTest("Procesos: la salida llega línea por línea, con el código de salida");
        {
            juce::StringArray lines;
            const int code = ProcessRunner::run({ "cmd.exe", "/c", "echo uno& echo dos& exit 3" },
                                                [&lines](const juce::String& l) { lines.add(l.trim()); }, [] { return false; });
            expectEquals(code, 3);
            expectEquals(lines.joinIntoString("|"), juce::String("uno|dos"));
            expectEquals(ProcessRunner::run({ "no-existe-este-programa.exe" }, {}, {}), (int) ProcessRunner::couldNotStart);

            // Comillas y barras como las lee Windows.
            expectEquals(ProcessRunner::commandLine({ "a.exe", "C:\\Con espacio\\", "sin", "" }),
                         juce::String("a.exe \"C:\\Con espacio\\\\\" sin \"\""));
            expectEquals(ProcessRunner::commandLine({ "x", "di \"hola\"" }), juce::String("x \"di \\\"hola\\\"\""));
        }

        beginTest("Procesos: cancelar cierra también los procesos que abrió (yt-dlp abre otro y FFmpeg)");
        {
            TempLab lab;
            const auto marker = lab.root.getChildFile("marca.txt");
            const auto inner = lab.root.getChildFile("hijo.bat");
            const auto outer = lab.root.getChildFile("padre.bat");
            inner.replaceWithText("@ping -n 4 127.0.0.1 >nul\r\n@echo x > \"" + marker.getFullPathName() + "\"\r\n");
            outer.replaceWithText("@start \"\" /b cmd /c \"" + inner.getFullPathName() + "\"\r\n@ping -n 30 127.0.0.1 >nul\r\n");
            const auto start = juce::Time::getMillisecondCounter();
            const int code = ProcessRunner::run({ "cmd.exe", "/c", outer.getFullPathName() }, {},
                                                [start] { return juce::Time::getMillisecondCounter() - start > 700; });
            expectEquals(code, (int) ProcessRunner::stopped);
            expect(juce::Time::getMillisecondCounter() - start < 4000, "se corta enseguida");
            juce::Thread::sleep(4500);
            expect(!marker.exists(), "el proceso que abrió también se cerró");
        }

        beginTest("Etiquetas: el MP3 descargado lleva los datos de Deezer y el audio no cambia");
        {
            TempLab lab;
            // Como lo deja FFmpeg: una etiqueta ID3v2.4 mínima y el audio.
            juce::MemoryOutputStream original;
            const juce::uint8 ffmpegTag[] = { 'I', 'D', '3', 4, 0, 0, 0, 0, 0, 24, 'T', 'S', 'S', 'E', 0, 0, 0, 14, 0, 0,
                                              3, 'L', 'a', 'v', 'f', '6', '3', '.', '7', '.', '1', '0', '0', 0 };
            original.write(ffmpegTag, sizeof(ffmpegTag));
            const auto audio = encodeMp3(1.0);
            original.write(audio.getData(), audio.getSize());
            const auto file = lab.root.getChildFile("cancion.mp3");
            file.replaceWithData(original.getData(), original.getDataSize());

            const auto t = SongDownload::tagsFor(frioFrio(), "JQwqpsuENeQ");
            expect(TagWriter::writeMp3(file, t));
            const auto read = TagReader::read(file);
            expectEquals(read.artist, juce::String("Juan Luis Guerra feat. Romeo Santos"));
            expectEquals(read.title, juce::String::fromUTF8("Frío, Frío (En Vivo)"));

            juce::MemoryBlock written;
            file.loadFileAsData(written);
            size_t tagLength = 0;
            TagReader::parseId3v2(static_cast<const juce::uint8*>(written.getData()), written.getSize(), &tagLength);
            expect(tagLength > 0 && written.getSize() - tagLength == audio.getSize());
            expect(memcmp(static_cast<const char*>(written.getData()) + tagLength, audio.getData(), audio.getSize()) == 0,
                   "el audio queda igual, sin la etiqueta de FFmpeg");
            const std::string_view tagBytes(static_cast<const char*>(written.getData()), tagLength);
            expect(tagBytes.find("TSRC") != std::string_view::npos && tagBytes.find("TALB") != std::string_view::npos
                   && tagBytes.find("TXXX") != std::string_view::npos, "ISRC, álbum y el enlace de origen");

            // Etiquetar otra vez reemplaza la etiqueta, no la duplica.
            expect(TagWriter::writeMp3(file, t));
            juce::MemoryBlock again;
            file.loadFileAsData(again);
            expectEquals((int) again.getSize(), (int) written.getSize());
        }

        beginTest("Herramientas: primero las de RAAADIO, luego las del PATH; lo que falta se instala");
        {
            TempLab lab;
            const auto own = lab.root.getChildFile("Herramientas");
            const auto path = lab.root.getChildFile("path con espacios");
            path.getChildFile("node.exe").create();
            path.getChildFile("ffmpeg.exe").create();

            auto found = DownloadTools::find(own, "C:\\no\\existe;\"" + path.getFullPathName() + "\"");
            expect(!found.ytDlp.existsAsFile() && !found.ready());
            expect(found.ffmpegFolder == path, found.ffmpegFolder.getFullPathName());
            expectEquals(found.jsRuntimeName, juce::String("node"));
            auto missing = DownloadTools::missing(found, own);
            expectEquals((int) missing.size(), 1, "solo yt-dlp: FFmpeg y Node ya están");
            expectEquals(missing[0].name, juce::String("yt-dlp"));

            own.getChildFile("yt-dlp.exe").create();
            own.getChildFile("ffmpeg").getChildFile("ffmpeg.exe").create();
            found = DownloadTools::find(own, path.getFullPathName());
            expect(found.ready() && found.managedYtDlp);
            expect(found.ffmpegFolder == own.getChildFile("ffmpeg"), found.ffmpegFolder.getFullPathName());

            missing = DownloadTools::missing(DownloadTools::find(lab.root.getChildFile("vacia"), {}), lab.root.getChildFile("vacia"));
            expectEquals((int) missing.size(), 3);
            expectEquals(DownloadTools::totalMegabytes(missing), 141);
        }

        beginTest("Herramientas: el SHA-256 en las listas de yt-dlp, FFmpeg y Deno; del zip, solo lo que hace falta");
        {
            const juce::String hex("66674953fe251b89f4d08c5f0e35e0728679bd67ab3d7d05c0562af101dd3e7a");
            expectEquals(DownloadTools::findSha256("aaaa  yt-dlp_x86.exe\n" + hex + "  yt-dlp.exe\n", "yt-dlp.exe"), hex);
            expectEquals(DownloadTools::findSha256(hex.replace("6", "7") + "  yt-dlp.exe.zip\n" + hex + " *yt-dlp.exe", "yt-dlp.exe"), hex);
            expectEquals(DownloadTools::findSha256("Algorithm : SHA256\r\nHash      : " + hex.toUpperCase() + "\r\nPath      : C:\\a\\deno.zip\r\n",
                                                   "deno-x86_64-pc-windows-msvc.zip"), hex);
            expect(DownloadTools::findSha256(hex + "  otro.exe", "yt-dlp.exe").isEmpty());

            TempLab lab;
            const auto zipFile = lab.root.getChildFile("ffmpeg.zip");
            {
                juce::ZipFile::Builder builder;
                for (auto* name : { "ffmpeg-x/bin/ffmpeg.exe", "ffmpeg-x/bin/ffprobe.exe", "ffmpeg-x/bin/ffplay.exe",
                                    "ffmpeg-x/bin/avcodec-63.dll", "ffmpeg-x/doc/leeme.txt", "ffmpeg-x/bin/../../../fuera.exe" })
                    builder.addEntry(new juce::MemoryInputStream(name, strlen(name), true), 9, name, juce::Time::getCurrentTime());
                juce::FileOutputStream out(zipFile);
                builder.writeToStream(out, nullptr);
            }
            DownloadTools::Asset asset;
            asset.zip = true;
            asset.zipFolder = "bin/";
            asset.skip = { "ffplay.exe" };
            asset.destination = lab.root.getChildFile("Herramientas").getChildFile("ffmpeg");
            expectEquals(DownloadTools::extract(zipFile, asset), 4);
            expect(asset.destination.getChildFile("ffmpeg.exe").existsAsFile() && asset.destination.getChildFile("avcodec-63.dll").existsAsFile());
            expect(!asset.destination.getChildFile("ffplay.exe").exists() && !asset.destination.getChildFile("leeme.txt").exists());
            expect(!lab.root.getChildFile("fuera.exe").exists(), "una entrada con .. no sale de la carpeta");
        }

        beginTest("yt-dlp: argumentos, progreso, ruta final y qué hacer con cada error");
        {
            DownloadTools::Paths tools;
            tools.ytDlp = juce::File("C:\\Herramientas\\yt-dlp.exe");
            tools.ffmpegFolder = juce::File("C:\\Herramientas\\ffmpeg");
            const auto args = SongDownload::arguments(tools, "v0ckuv1xBm0", juce::File("C:\\Temp\\RAAADIO\\3"));
            expectEquals(args[0], tools.ytDlp.getFullPathName());
            expect(args.contains("--no-config") && args.contains("--no-playlist") && args.contains("-x"));
            expectEquals(args[args.indexOf("--audio-format") + 1], juce::String("mp3"));
            expectEquals(args[args.indexOf("--ffmpeg-location") + 1], tools.ffmpegFolder.getFullPathName());
            expectEquals(args[args.indexOf("-o") + 1], juce::String("C:\\Temp\\RAAADIO\\3\\%(id)s.%(ext)s"));
            expectEquals(args[args.indexOf("--retries") + 1], juce::String("3"));
            expect(!args.contains("--js-runtimes"), "sin motor de JavaScript no se pide");
            expectEquals(args[args.size() - 1], juce::String("https://www.youtube.com/watch?v=v0ckuv1xBm0"));

            expect(SongDownload::isValidVideoId("v0ckuv1xBm0") && SongDownload::isValidVideoId("-YRkhpmAsvE"));
            expect(!SongDownload::isValidVideoId("v0ckuv1xBm") && !SongDownload::isValidVideoId("--exec=calc") && !SongDownload::isValidVideoId("a b c d e f"));

            expectWithinAbsoluteError(SongDownload::parseProgress("[raaadio]  45.3%"), 0.453, 0.0001);
            expectEquals(SongDownload::parseProgress("[raaadio] 100.0%"), 1.0);
            expectEquals(SongDownload::parseProgress("[raaadio]   N/A%"), -1.0);
            expectEquals(SongDownload::parseProgress("[download]  45.3%"), -1.0);
            expectEquals(SongDownload::parseFinalPath("C:\\Temp\\RAAADIO\\3\\v0ckuv1xBm0.mp3"), juce::String("C:\\Temp\\RAAADIO\\3\\v0ckuv1xBm0.mp3"));
            expect(SongDownload::parseFinalPath("[youtube] v0ckuv1xBm0: Downloading webpage").isEmpty());

            using P = SongDownload::Problem;
            // Mensajes reales de yt-dlp.
            expect(SongDownload::classify("ERROR: [youtube] abc: Video unavailable. This content isn't available, try again later. "
                                          "The current session has been rate-limited by YouTube for up to an hour") == P::RateLimited);
            expect(SongDownload::classify("ERROR: [youtube] abc: Unable to download API page: HTTP Error 429: Too Many Requests") == P::RateLimited);
            expect(SongDownload::classify(juce::String::fromUTF8("ERROR: [youtube] abc: Sign in to confirm you\xe2\x80\x99re not a bot. Use --cookies"))
                   == P::BotCheck);
            expect(SongDownload::classify("ERROR: [youtube] abc: Sign in to confirm your age. This video may be inappropriate for some users.")
                   == P::AgeRestricted);
            expect(SongDownload::classify("ERROR: [youtube] abc: Video unavailable. This video has been removed by the uploader") == P::Unavailable);
            expect(SongDownload::classify("ERROR: [youtube] abc: The uploader has not made this video available in your country") == P::Region);
            expect(SongDownload::classify("ERROR: Postprocessing: ffprobe and ffmpeg not found. Please install or provide the path") == P::Tools);
            expect(SongDownload::classify("ERROR: [youtube] abc: Unable to download API page: <urlopen error [Errno 11001] getaddrinfo failed>")
                   == P::Network);
            expectEquals(SongDownload::pauseMinutes(P::RateLimited), 60);
            expectEquals(SongDownload::pauseMinutes(P::BotCheck), 360);
            expectEquals(SongDownload::pauseMinutes(P::Unavailable), 0);
            expect(SongDownload::problemFromKey(SongDownload::problemKey(P::BotCheck)) == P::BotCheck);

            auto acdc = frioFrio();
            acdc.artist = "AC/DC";
            acdc.featured.clear();
            acdc.title = "Back in Black?";
            expect(!SongDownload::fileName(acdc).containsAnyOf("/\\?:*\"<>|"));
            expect(SongDownload::fileName(frioFrio()).startsWith("Juan Luis Guerra - "));
        }

        beginTest("Ritmo: al menos 20 s entre descargas y no más de 20 por hora");
        {
            const auto now = juce::Time(2026, 8, 29, 10, 0, 0);
            expectEquals(SongDownload::secondsToWait({}, now), 0);
            expectEquals(SongDownload::secondsToWait({ now - juce::RelativeTime::seconds(5) }, now), 15);
            expectEquals(SongDownload::secondsToWait({ now - juce::RelativeTime::seconds(30) }, now), 0);
            juce::Array<juce::Time> twenty;
            for (int i = 0; i < 20; ++i) twenty.add(now - juce::RelativeTime::minutes(58 - i * 2));  // la más vieja hace 58 min
            expectEquals(SongDownload::secondsToWait(twenty, now), 120, "hasta que la más vieja salga de la hora");
            twenty.remove(0);
            expectEquals(SongDownload::secondsToWait(twenty, now), 0);
        }

        beginTest("Base: cada intento queda anotado y cuenta para el tope por hora");
        {
            LibraryFixture fx;
            auto& db = fx.library.getDatabase();
            const int a = db.startDownload("v0ckuv1xBm0", 1857061907LL, "Juan Luis Guerra", "Burbujas de Amor");
            const int b = db.startDownload("JQwqpsuENeQ", 42, "Juan Luis Guerra", juce::String::fromUTF8("Frío, Frío").toStdString());
            expect(a > 0 && b > a);
            expect(db.finishDownload(a, Database::DownloadStatus::Done, 7, ""));
            expect(db.finishDownload(b, Database::DownloadStatus::Failed, -1, "rate_limited: try again later"));
            const auto rows = db.getRecentDownloads(10);
            expectEquals((int) rows.size(), 2);
            expect(rows[0].id == b && rows[0].status == Database::DownloadStatus::Failed && rows[0].error == "rate_limited: try again later");
            expect(rows[1].status == Database::DownloadStatus::Done && rows[1].trackId == 7 && rows[1].deezerId == 1857061907LL);
            expectEquals((int) db.getDownloadStartsSince(60).size(), 2);
        }

        beginTest("En su hilo: baja, etiqueta y entra a la biblioteca por revisar, en su propio lote");
        {
            LibraryFixture fx;
            FakeYtDlp yt;
            yt.mp3 = encodeMp3(8.0);
            expect(yt.mp3.getSize() > 1000, "LAME está junto a las pruebas");
            auto services = fakeServices(fx.lab.root, yt);
            SongDownloader downloader(fx.library, services);
            expect(downloader.getStatus().toolsReady);

            const int id = downloader.enqueue(frioFrio(), video("JQwqpsuENeQ"));
            expect(id > 0);
            expectEquals(downloader.enqueue(frioFrio(), video("JQwqpsuENeQ")), 0, "ya está en la fila");
            auto otherSong = frioFrio();
            otherSong.id = 99;
            expectEquals(downloader.enqueue(otherSong, video("--exec=calc")), 0, "un id de video que no es válido");

            const auto job = finished(downloader, id);
            expect(job.has_value() && job->state == SongDownloader::State::Done, job.has_value() ? job->detail : juce::String("timeout"));
            if (!job.has_value() || job->trackId < 0) return;
            expectEquals(yt.updates.load(), 1, "yt-dlp de RAAADIO se actualiza una vez al día");

            const auto track = fx.library.getTrackById(job->trackId);
            expectEquals(track.status, 1, "por revisar");
            const juce::File file(juce::String::fromUTF8(track.path.c_str()));
            expect(file.getFullPathName().contains("_Entrada"));
            expectEquals(file.getFileName(), juce::String::fromUTF8("Juan Luis Guerra - Frío, Frío (En Vivo).mp3"));
            const auto tags = TagReader::read(file);
            expectEquals(tags.artist, juce::String("Juan Luis Guerra feat. Romeo Santos"));
            expect(juce::String::fromUTF8(track.artist.c_str()).startsWith("Juan Luis Guerra"));
            expectEquals(juce::String(fx.library.getDatabase().getOnlineIds(job->trackId).second), juce::String("USJ3V1200001"));
            expect(!services.workFolder.getChildFile(juce::String(id)).exists(), "la carpeta de trabajo se borra");

            const auto batches = fx.library.getImportBatches();
            expect(!batches.empty() && juce::String::fromUTF8(batches[0].source.c_str()).startsWith("YouTube: Juan Luis Guerra") && batches[0].pending == 1,
                   "un lote propio: se puede deshacer");
            const auto records = fx.library.getDatabase().getRecentDownloads(5);
            expect(!records.empty() && records[0].status == Database::DownloadStatus::Done && records[0].trackId == job->trackId
                   && records[0].videoId == "JQwqpsuENeQ");

            // Otra vez la misma: se reconoce y no se copia dos veces.
            const int again = downloader.enqueue(frioFrio(), video("JQwqpsuENeQ"));
            const auto second = finished(downloader, again);
            expect(second.has_value() && second->state == SongDownloader::State::Duplicate);
        }

        beginTest("En su hilo: si YouTube pide esperar, la fila se detiene (aunque se cierre el programa) y la canción espera");
        {
            LibraryFixture fx;
            FakeYtDlp yt;
            yt.errorOutput = "ERROR: [youtube] JQwqpsuENeQ: Video unavailable. This content isn't available, try again later. "
                             "The current session has been rate-limited by YouTube for up to an hour";
            const auto before = juce::Time::getCurrentTime();
            {
                SongDownloader downloader(fx.library, fakeServices(fx.lab.root, yt));
                const int id = downloader.enqueue(frioFrio(), video("JQwqpsuENeQ"));
                expect(waitFor([&] { return downloader.getStatus().pausedUntil > before; }));
                const auto status = downloader.getStatus();
                expect(status.pausedUntil >= before + juce::RelativeTime::minutes(59));
                expect(status.pauseReason == SongDownload::Problem::RateLimited);
                juce::Thread::sleep(300);
                expectEquals(yt.downloads.load(), 1, "no se insiste");
                const auto jobs = downloader.getJobs();
                expect(jobs.size() == 1 && jobs[0].id == id && jobs[0].state == SongDownloader::State::Queued, "espera su turno, no falla");
                expectEquals(status.queued, 1);
            }
            SongDownloader reopened(fx.library, fakeServices(fx.lab.root, yt));
            expect(reopened.getStatus().pausedUntil >= before + juce::RelativeTime::minutes(59), "la pausa se guarda");
            expect(fx.library.getDatabase().getRecentDownloads(1)[0].error.rfind("rate_limited", 0) == 0);
        }

        beginTest("En su hilo: un video no disponible falla sin detener la fila; cancelar corta la descarga");
        {
            LibraryFixture fx;
            FakeYtDlp yt;
            yt.errorOutput = "ERROR: [youtube] JQwqpsuENeQ: Video unavailable. This video has been removed by the uploader";
            SongDownloader downloader(fx.library, fakeServices(fx.lab.root, yt));
            const auto failed = finished(downloader, downloader.enqueue(frioFrio(), video("JQwqpsuENeQ")));
            expect(failed.has_value() && failed->state == SongDownloader::State::Failed && failed->problem == SongDownload::Problem::Unavailable);
            expect(failed.has_value() && failed->detail.contains("removed by the uploader"));
            expect(downloader.getStatus().pausedUntil == juce::Time());

            yt.block = true;
            auto other = frioFrio();
            other.id = 43;
            const int id = downloader.enqueue(other, video("G6fJreMyIks"));
            expect(waitFor([&] {
                for (const auto& j : downloader.getJobs())
                    if (j.id == id) return j.state == SongDownloader::State::Downloading && j.progress > 0.05;
                return false;
            }));
            downloader.cancel(id);
            const auto cancelled = finished(downloader, id, 3000);
            expect(cancelled.has_value() && cancelled->state == SongDownloader::State::Cancelled);
        }

        beginTest("Instalación: baja, verifica el SHA-256 y extrae; una firma que no coincide no se instala");
        {
            // Lo que "publica" GitHub: yt-dlp.exe y los zip de FFmpeg y Deno, con sus checksums.
            TempLab lab;
            auto zipOf = [&lab](const juce::String& name, std::initializer_list<const char*> entries) {
                const auto file = lab.root.getChildFile(name);
                juce::ZipFile::Builder builder;
                for (auto* e : entries) builder.addEntry(new juce::MemoryInputStream(e, strlen(e), true), 9, e, juce::Time::getCurrentTime());
                juce::FileOutputStream out(file);
                builder.writeToStream(out, nullptr);
                out.flush();
                return file;
            };
            const auto ytDlp = lab.root.getChildFile("yt-dlp.exe");
            ytDlp.replaceWithText("yt-dlp de prueba");
            const auto ffmpegZip = zipOf("ffmpeg.zip", { "ffmpeg-x/bin/ffmpeg.exe", "ffmpeg-x/bin/ffplay.exe", "ffmpeg-x/LICENSE.txt" });
            const auto denoZip = zipOf("deno.zip", { "deno.exe" });
            auto sha = [](const juce::File& f) { return juce::SHA256(f).toHexString(); };

            auto services = [&](LibraryFixture& fx, const juce::String& ffmpegHash) {
                SongDownloader::Services s;
                s.toolsFolder = fx.lab.root.getChildFile("Herramientas");
                s.workFolder = fx.lab.root.getChildFile("Trabajo");
                s.pathVariable = juce::String();
                s.transport = [&, ffmpegHash](const HttpTransport::Request& r) {
                    if (r.url.endsWith("SHA2-256SUMS")) return HttpTransport::Response { 200, sha(ytDlp) + "  yt-dlp.exe\n", {} };
                    if (r.url.endsWith("checksums.sha256"))
                        return HttpTransport::Response { 200, ffmpegHash + "  ffmpeg-master-latest-win64-gpl-shared.zip\n", {} };
                    if (r.url.endsWith(".sha256sum")) return HttpTransport::Response { 200, "Hash      : " + sha(denoZip).toUpperCase(), {} };
                    return HttpTransport::Response { 404, {}, {} };
                };
                s.fetch = [&](const juce::String& url, const juce::File& target, const HttpTransport::Progress& progress) {
                    const auto source = url.endsWith("yt-dlp.exe") ? ytDlp : url.contains("ffmpeg") ? ffmpegZip : denoZip;
                    progress(0.5);
                    progress(1.0);
                    return source.copyFileTo(target);
                };
                return s;
            };

            {
                LibraryFixture fx;
                SongDownloader downloader(fx.library, services(fx, sha(ffmpegZip)));
                expectEquals((int) downloader.getMissingTools().size(), 3);
                downloader.installTools();
                expect(waitFor([&] { return !downloader.getStatus().installing; }));
                const auto status = downloader.getStatus();
                expect(status.toolsReady && status.installError.isEmpty(), status.installError);
                const auto folder = fx.lab.root.getChildFile("Herramientas");
                expectEquals(folder.getChildFile("yt-dlp.exe").loadFileAsString(), juce::String("yt-dlp de prueba"));
                expect(folder.getChildFile("ffmpeg/ffmpeg.exe").existsAsFile() && !folder.getChildFile("ffmpeg/ffplay.exe").exists());
                expect(folder.getChildFile("deno/deno.exe").existsAsFile());
                expect(downloader.getMissingTools().empty() && downloader.getTools().jsRuntimeName == "deno");
                expect(folder.findChildFiles(juce::File::findFiles, false, "descargando-*").isEmpty(), "no quedan archivos a medias");
            }
            {
                LibraryFixture fx;
                SongDownloader downloader(fx.library, services(fx, juce::String::repeatedString("0", 64)));
                downloader.installTools();
                expect(waitFor([&] { return !downloader.getStatus().installing; }));
                const auto status = downloader.getStatus();
                expectEquals(status.installError, juce::String("checksum"));
                expectEquals(status.installErrorStep, juce::String("FFmpeg"));
                expect(!status.toolsReady && !fx.lab.root.getChildFile("Herramientas/ffmpeg/ffmpeg.exe").exists());
            }
        }

        // De verdad, solo si se pide (RAAADIO_DOWNLOAD_TESTS=1): instala yt-dlp, FFmpeg y Deno en una
        // carpeta temporal y descarga "Big Buck Bunny" de la Blender Foundation (Creative Commons BY).
        if (juce::SystemStats::getEnvironmentVariable("RAAADIO_DOWNLOAD_TESTS", {}) == "1")
        {
            beginTest("De verdad: instalar las herramientas y descargar un video con licencia Creative Commons");
            LibraryFixture fx;
            SongDownloader::Services s;
            s.toolsFolder = fx.lab.root.getChildFile("Herramientas");
            s.workFolder = fx.lab.root.getChildFile("Trabajo");
            s.pathVariable = juce::String();
            s.minGapSeconds = 0;
            s.maxJitterSeconds = 0;
            SongDownloader downloader(fx.library, s);
            downloader.installTools();
            expect(waitFor([&] { return !downloader.getStatus().installing; }, 15 * 60 * 1000));
            const auto status = downloader.getStatus();
            expect(status.toolsReady, status.installErrorStep + " " + status.installError);
            if (!status.toolsReady) return;

            SongSearch::Track bunny;
            bunny.id = 1;
            bunny.artist = "Blender Foundation";
            bunny.title = bunny.titleShort = "Big Buck Bunny";
            bunny.album = "Big Buck Bunny";
            bunny.year = 2008;
            const auto start = juce::Time::getMillisecondCounter();
            const auto done = finished(downloader, downloader.enqueue(bunny, video("aqz-KE-bpKQ")), 5 * 60 * 1000);
            expect(done.has_value() && done->state == SongDownloader::State::Done, done.has_value() ? done->detail : juce::String("timeout"));
            logMessage("Descarga real: " + juce::String((juce::Time::getMillisecondCounter() - start) / 1000) + " s");
            if (done.has_value() && done->trackId > 0) {
                const auto track = fx.library.getTrackById(done->trackId);
                logMessage("En la biblioteca: " + juce::String::fromUTF8(track.path.c_str()) + " (" + juce::String(track.duration, 1) + " s)");
                expect(track.status == 1 && track.duration > 600.0);
                expectEquals(TagReader::read(juce::File(juce::String::fromUTF8(track.path.c_str()))).title, juce::String("Big Buck Bunny"));
            }

            // Cancelar a medias no deja yt-dlp ni FFmpeg corriendo.
            auto again = bunny;
            again.id = 2;
            const int id = downloader.enqueue(again, video("aqz-KE-bpKQ"));
            waitFor([&] {
                for (const auto& j : downloader.getJobs())
                    if (j.id == id) return j.state == SongDownloader::State::Downloading && j.progress > 0.02;
                return false;
            }, 60000);
            downloader.cancel(id);
            const auto cancelled = finished(downloader, id, 10000);
            expect(cancelled.has_value() && cancelled->state == SongDownloader::State::Cancelled);
            juce::Thread::sleep(1000);
            juce::String processes;
            ProcessRunner::run({ "tasklist", "/NH" }, [&processes](const juce::String& l) { processes << l << "\n"; }, {});
            expect(!processes.containsIgnoreCase("yt-dlp.exe") && !processes.containsIgnoreCase("ffmpeg.exe"), "no queda nada corriendo");
        }
    }
};

//==============================================================================
class ConversionTests : public juce::UnitTest
{
public:
    ConversionTests() : juce::UnitTest("Importar audios de WhatsApp, M4A y AAC", "RAAADIO") {}

    // Un FFmpeg de mentira: escribe en el FLAC de destino un tono que depende del tamaño del
    // original (como si lo hubiera decodificado) y cuenta las veces que corre.
    struct FakeFfmpeg
    {
        juce::File exe;
        int runs = 0;
        int exitCode = 0;

        AudioConverter::Services services()
        {
            return { [this] { return exe; },
                     [this](const juce::StringArray& command, const ProcessRunner::LineCallback& onLine, const ProcessRunner::StopCheck&) {
                         ++runs;
                         if (exitCode != 0) {
                             onLine("Invalid data found when processing input");
                             return exitCode;
                         }
                         const juce::File source(command[command.indexOf("-i") + 1].fromFirstOccurrenceOf("file:", false, false));
                         const juce::File target(command[command.size() - 1].fromFirstOccurrenceOf("file:", false, false));
                         const double frequency = 300.0 + (double) (source.getSize() % 400);
                         juce::FlacAudioFormat flac;
                         writeAudio(flac, target, 4.0, [frequency](double t) { return tone(t, 0.4f, frequency); }, {}, 16);
                         return 0;
                     } };
        }
    };

    // La primera página de un Ogg con Opus, como empiezan las notas de voz de WhatsApp.
    static juce::File writeOggOpusHeader(const juce::File& file)
    {
        juce::MemoryOutputStream page;
        page.write("OggS", 4);
        const juce::uint8 header[22] = { 0, 2 };  // versión, primera página; el resto en cero
        page.write(header, sizeof(header));
        page.writeByte(1);    // un segmento
        page.writeByte(19);   // de 19 bytes
        page.write("OpusHead", 8);
        const juce::uint8 opusHead[11] = { 1, 1 };
        page.write(opusHead, sizeof(opusHead));
        file.replaceWithData(page.getData(), page.getDataSize());
        return file;
    }

    static void importAndWait(MusicLibrary& library, const juce::Array<juce::File>& files, const juce::String& category = {})
    {
        library.importFiles(files, category);
        for (int i = 0; i < 600 && library.isImporting(); ++i) juce::Thread::sleep(20);
    }

    void runTest() override
    {
        beginTest("Se aceptan los formatos de WhatsApp, M4A y AAC; un video no");
        {
            LibraryFixture fx;
            for (auto* name : { "PTT-20261022-WA0003.opus", "WhatsApp Audio.mpeg", "cancion.M4A", "audio.aac", "viejo.amr",
                                "radio.mpga", "WhatsApp Ptt.ogg", "cancion.mp3" })
                expect(fx.library.isImportable(fx.lab.root.getChildFile(name)), name);
            expect(!fx.library.isImportable(fx.lab.root.getChildFile("video.mp4")));
            expect(!fx.library.isImportable(fx.lab.root.getChildFile("notas.txt")));
            const auto wildcard = fx.library.getSupportedFileWildcard();
            for (auto* pattern : { "*.opus", "*.m4a", "*.aac", "*.mpeg", "*.ogg", "*.mp3" })
                expect(wildcard.contains(pattern), wildcard);
        }

        beginTest("FFmpeg: el primer audio a FLAC con sus etiquetas, sin su versión, y nombres que no se confunden con opciones");
        {
            TempLab lab;
            const auto ffmpeg = lab.root.getChildFile("ffmpeg.exe");
            const auto source = lab.root.getChildFile("-raro: WhatsApp Ptt 2026.ogg");
            const auto target = lab.root.getChildFile("salida.flac");
            const auto args = AudioConverter::arguments(ffmpeg, source, target);
            expectEquals(args[0], ffmpeg.getFullPathName());
            expectEquals(args[args.indexOf("-i") + 1], "file:" + source.getFullPathName());
            expectEquals(args[args.size() - 1], "file:" + target.getFullPathName());
            expectEquals(args[args.indexOf("-map") + 1], juce::String("0:a:0"));
            expect(args.contains("0:s:a:0"), "las etiquetas del Opus van en el audio");
            expectEquals(args[args.indexOf("-c:a") + 1], juce::String("flac"));
            expect(args.contains("+bitexact") && args.contains("-nostdin"));
        }

        beginTest("Un .mpeg de WhatsApp que es MP3 se guarda como .mp3, igual al original, sin FFmpeg");
        {
            LibraryFixture fx;
            TempLab outside;
            const auto audio = SongDownloadTests::encodeMp3(6.0);
            expect(audio.getSize() > 1000, "LAME está junto a las pruebas");
            const auto mpeg = outside.root.getChildFile("WhatsApp Audio 2026-10-22 at 10.15.22.mpeg");
            mpeg.replaceWithData(audio.getData(), audio.getSize());
            TagWriter::Tags tags;
            tags.artist = "Diomedes Diaz";
            tags.title = "Sin Ti";
            expect(TagWriter::writeMp3(mpeg, tags));
            expect(AudioConverter::looksLikeMp3(mpeg), "después de la etiqueta ID3 empieza el MP3");

            expect(fx.library.processAndAddTrack(mpeg) == MusicLibrary::ImportResult::Added);
            const auto tracks = fx.library.search("", "");
            expectEquals((int) tracks.size(), 1);
            const auto track = tracks.empty() ? TrackInfo {} : tracks.front();
            const juce::File stored(juce::String::fromUTF8(track.path.c_str()));
            expectEquals(stored.getFileName(), juce::String("WhatsApp Audio 2026-10-22 at 10.15.22.mp3"));
            expect(stored.hasIdenticalContentTo(mpeg), "sin recodificar");
            expectEquals(juce::String(track.fileHash), juce::SHA256(mpeg).toHexString());
            expectEquals(juce::String::fromUTF8(track.artist.c_str()), juce::String("Diomedes Diaz"));
            expectEquals(juce::String::fromUTF8(track.title.c_str()), juce::String("Sin Ti"));
            expectWithinAbsoluteError(track.duration, 6.0, 0.1);

            const auto video = outside.root.getChildFile("video.mpeg");
            const juce::uint8 packHeader[] = { 0x00, 0x00, 0x01, 0xba, 0x44, 0x00, 0x04, 0x00, 0x04, 0x01, 0xff, 0xfb, 0x90, 0x00 };
            video.replaceWithData(packHeader, sizeof(packHeader));
            expect(!AudioConverter::looksLikeMp3(video), "un video MPEG no es un MP3");
            expect(fx.library.processAndAddTrack(video) == MusicLibrary::ImportResult::NeedsFfmpeg);
        }

        beginTest("Las notas de voz de WhatsApp (Ogg con Opus) se distinguen de un Ogg Vorbis");
        {
            TempLab lab;
            expect(AudioConverter::isOggOpus(writeOggOpusHeader(lab.root.getChildFile("WhatsApp Ptt.ogg"))));
            juce::OggVorbisAudioFormat ogg;
            const auto vorbis = writeAudio(ogg, lab.root.getChildFile("vorbis.ogg"), 1.0, toneBetween({ { 0.0, 1.0 } }), {}, 16, 1);
            expect(!AudioConverter::isOggOpus(vorbis));
            expect(!AudioConverter::isOggOpus(lab.touch("vacio.ogg")));
        }

        beginTest("Sin FFmpeg: lo que necesita conversión queda esperando; un archivo dañado falla");
        {
            LibraryFixture fx;
            TempLab outside;
            const auto opus = outside.root.getChildFile("PTT-20261022-WA0003.opus");
            opus.replaceWithText("nota de voz");
            expect(fx.library.processAndAddTrack(opus) == MusicLibrary::ImportResult::NeedsFfmpeg);
            const auto ptt = writeOggOpusHeader(outside.root.getChildFile("WhatsApp Ptt 2026-10-22 at 10.15.22.ogg"));
            expect(fx.library.processAndAddTrack(ptt) == MusicLibrary::ImportResult::NeedsFfmpeg);
            const auto broken = outside.root.getChildFile("rota.wav");
            broken.replaceWithText("no soy audio");
            expect(fx.library.processAndAddTrack(broken) == MusicLibrary::ImportResult::Failed);
            expect(fx.library.search("", "").empty());
        }

        beginTest("Con FFmpeg: se guarda como FLAC con el nombre original y la huella del FLAC");
        {
            LibraryFixture fx;
            TempLab outside;
            FakeFfmpeg ffmpeg;
            ffmpeg.exe = outside.touch("ffmpeg/ffmpeg.exe");
            fx.library.setConverter(ffmpeg.services());

            const auto opus = outside.root.getChildFile("PTT-20261022-WA0003.opus");
            opus.replaceWithText("nota de voz");
            expect(fx.library.processAndAddTrack(opus) == MusicLibrary::ImportResult::Added);
            expectEquals(ffmpeg.runs, 1);
            const auto tracks = fx.library.search("", "");
            expectEquals((int) tracks.size(), 1);
            const auto track = tracks.empty() ? TrackInfo {} : tracks.front();
            const juce::File stored(juce::String::fromUTF8(track.path.c_str()));
            expectEquals(stored.getFileName(), juce::String("PTT-20261022-WA0003.flac"));
            expect(stored.isAChildOf(fx.lab.root.getChildFile("Library/_Entrada")), "por revisar, como la música importada");
            expectEquals(juce::String(track.fileHash), juce::SHA256(stored).toHexString(),
                         "los respaldos verifican la copia con la huella del archivo guardado");
            expectWithinAbsoluteError(track.duration, 4.0, 0.01);
            expect(track.analysisVersion == TrackAnalyzer::version);

            expect(fx.library.processAndAddTrack(opus) == MusicLibrary::ImportResult::Duplicate,
                   "la misma conversión da el mismo FLAC");
            expectEquals((int) fx.library.search("", "").size(), 1);

            beginTest("Con FFmpeg: lo que FFmpeg no lee falla; un formato conocido que JUCE no lee se intenta");
            ffmpeg.exitCode = 1;
            const auto bad = outside.root.getChildFile("AUD-20261022-WA0001.m4a");
            bad.replaceWithText("no soy un m4a");
            expect(fx.library.processAndAddTrack(bad) == MusicLibrary::ImportResult::Failed);
            expectEquals((int) fx.library.search("", "").size(), 1);

            ffmpeg.exitCode = 0;
            const auto mp2InWav = outside.root.getChildFile("CUNA FERRETERIA.wav");
            mp2InWav.replaceWithText("un WAV con MP2 adentro, como los de las automatizaciones viejas");
            const int before = ffmpeg.runs;
            expect(fx.library.processAndAddTrack(mp2InWav) == MusicLibrary::ImportResult::Added);
            expectEquals(ffmpeg.runs, before + 1);
            expect(fx.lab.root.getChildFile("Library/_Entrada/0/CUNA FERRETERIA.flac").existsAsFile());
        }

        beginTest("Una carpeta sin FFmpeg: el resumen cuenta lo que espera y esos archivos se importan después");
        {
            LibraryFixture fx;
            TempLab outside;
            const auto folder = outside.root.getChildFile("WhatsApp");
            writeWav(folder.getChildFile("cuna.wav"), 3.0, toneBetween({ { 0.0, 3.0 } }));
            folder.getChildFile("PTT-20261022-WA0001.opus").replaceWithText("a");
            folder.getChildFile("AUD-20261022-WA0002.m4a").replaceWithText("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
            folder.getChildFile("foto.jpg").replaceWithText("no es audio");

            const auto spots = fx.library.getSystemCategory(CategoryRole::Spots).name;
            importAndWait(fx.library, { folder }, spots);
            auto summary = fx.library.getLastImportSummary();
            expectEquals(summary.added, 1);
            expectEquals(summary.needFfmpeg, 2);
            expectEquals(summary.failed, 0);
            const auto waiting = fx.library.getFilesWaitingForFfmpeg();
            expectEquals(waiting.files.size(), 2);
            expectEquals(waiting.category, juce::String(spots));

            FakeFfmpeg ffmpeg;
            ffmpeg.exe = outside.touch("ffmpeg/ffmpeg.exe");
            fx.library.setConverter(ffmpeg.services());
            importAndWait(fx.library, waiting.files, waiting.category);
            summary = fx.library.getLastImportSummary();
            expectEquals(summary.added, 2);
            expectEquals(summary.needFfmpeg, 0);
            expect(fx.library.getFilesWaitingForFfmpeg().files.isEmpty());
            expectEquals((int) fx.library.getTracksWithRole(CategoryRole::Spots).size(), 3);
        }

        // Con el FFmpeg de verdad, si está instalado (lo instala la descarga de canciones).
        const auto tools = DownloadTools::find(DownloadTools::defaultFolder(), juce::SystemStats::getEnvironmentVariable("PATH", {}));
        const auto ffmpeg = tools.ffmpegFolder == juce::File() ? juce::File() : tools.ffmpegFolder.getChildFile(DownloadTools::Paths::exe("ffmpeg"));
        if (!ffmpeg.existsAsFile()) {
            logMessage("FFmpeg no está instalado: se omite la conversión de verdad");
            return;
        }

        beginTest("De verdad: una nota de voz en Opus y un M4A con etiquetas entran como FLAC");
        {
            LibraryFixture fx;
            TempLab outside;
            fx.library.setConverter({});
            const auto wav = writeWav(outside.root.getChildFile("fuente.wav"), 5.0, toneBetween({ { 0.5, 5.0 } }));
            auto encode = [&](const juce::File& target, const juce::StringArray& options) {
                juce::StringArray command { ffmpeg.getFullPathName(), "-nostdin", "-loglevel", "error", "-y", "-i", wav.getFullPathName() };
                command.addArray(options);
                command.add(target.getFullPathName());
                juce::String output;
                const int code = ProcessRunner::run(command, [&output](const juce::String& l) { output << l << "\n"; }, {});
                expect(code == 0 && target.existsAsFile(), output);
                return target;
            };
            const auto ptt = encode(outside.root.getChildFile("WhatsApp Ptt 2026-10-22 at 10.15.22.ogg"),
                                    { "-ac", "1", "-c:a", "libopus", "-b:a", "24k" });
            const auto m4a = encode(outside.root.getChildFile("AUD-20261022-WA0007.m4a"),
                                    { "-c:a", "aac", "-metadata", "artist=Joe Arroyo", "-metadata", utf8("title=La Rebelión") });
            expect(AudioConverter::isOggOpus(ptt));

            expect(fx.library.processAndAddTrack(ptt) == MusicLibrary::ImportResult::Added);
            expect(fx.library.processAndAddTrack(m4a) == MusicLibrary::ImportResult::Added);
            expect(fx.library.processAndAddTrack(ptt) == MusicLibrary::ImportResult::Duplicate, "FFmpeg da siempre el mismo FLAC");

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            for (const auto& track : fx.library.search("", "")) {
                const juce::File stored(juce::String::fromUTF8(track.path.c_str()));
                expect(stored.hasFileExtension(".flac"), stored.getFileName());
                std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(stored));
                expect(reader != nullptr && reader->getFormatName() == "FLAC file" && reader->bitsPerSample == 16);
                expectWithinAbsoluteError(track.duration, 5.0, 0.05);
                expectWithinAbsoluteError(track.cueIn, 0.5, 0.05);
                if (stored.getFileNameWithoutExtension() == "AUD-20261022-WA0007") {
                    expectEquals(juce::String::fromUTF8(track.artist.c_str()), juce::String("Joe Arroyo"));
                    expectEquals(juce::String::fromUTF8(track.title.c_str()), utf8("La Rebelión"));
                    expectEquals((int) reader->numChannels, 2);
                } else {
                    expectEquals((int) reader->numChannels, 1);
                    expectEquals(reader->sampleRate, 48000.0);
                }
            }
            expectEquals((int) fx.library.search("", "").size(), 2);
        }
    }
};

//==============================================================================
class DeckCueTests : public juce::UnitTest
{
public:
    DeckCueTests() : juce::UnitTest("Puntos de entrada y salida en el deck", "RAAADIO") {}

    void runTest() override
    {
        LibraryFixture fx;
        TempLab outside;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        juce::AudioThumbnailCache cache(4);
        Deck deck(1, fx.library, formats, cache);
        Mixer mixer;
        mixer.addDeck(&deck);
        mixer.prepareToPlay(512, 44100.0);
        expect(!deck.isLoaded() && !deck.hasFinished(), "un deck vacío no ha terminado nada (y no se cae al preguntarle)");

        auto import = [&](const juce::String& name, double seconds, const Signal& signal) {
            const auto file = writeWav(outside.root.getChildFile(name + ".wav"), seconds, signal);
            fx.library.processAndAddTrack(file);
            return juce::File(fx.library.search(name.fromFirstOccurrenceOf(" - ", false, false), "").front().path);
        };

        beginTest("Arranca en el punto de entrada y deja el aire en el de salida");
        {
            // 1 s de silencio, tono hasta 15 s y 5 s de silencio al final.
            deck.loadTrack(import("PRUEBA - CORTA", 20.0, toneBetween({ { 1.0, 15.0 } })));
            expectWithinAbsoluteError(deck.getCurrentPosition(), 0.98, 0.03);
            expectWithinAbsoluteError(deck.getEndPosition(), 15.02, 0.03);
            expectWithinAbsoluteError(deck.getRemainingSeconds(), 14.04, 0.05);

            deck.setPosition(11.0);
            deck.play();
            expectWithinAbsoluteError(deck.getMixPoint(), deck.getEndPosition(), 1e-9,
                                      "termina en seco: no hay final que se desvanezca");
            pump(mixer, 3.0, { &deck });
            expect(!deck.isReadyToMix(), "la siguiente no entra antes del punto de salida");
            expect(!deck.hasFinished());
            pump(mixer, 1.2, { &deck });
            expect(deck.isReadyToMix() && deck.hasFinished(), "al llegar al punto de salida termina, sin esperar el silencio final");
            deck.unload();
        }

        auto toSystemCategory = [&](const juce::File& file, CategoryRole role) {
            const auto track = fx.library.getTrackByPath(file.getFullPathName());
            fx.library.getDatabase().addTrackCategory(track.id, fx.library.getSystemCategory(role).id);
        };

        beginTest("Una canción con final largo: la siguiente entra cuando el fundido ya bajó y lo que queda se desvanece");
        {
            // Fundido desde los 20 s: baja 18 dB de su sonoridad poco después de los 22.5 s y
            // llega a -50 dBFS a los 26.6 s.
            const auto file = import("PRUEBA - FINAL LARGO", 30.0, toneWithFadeOut(20.0));
            deck.loadTrack(file);
            const double mix = deck.getMixPoint();
            expect(mix > 22.3 && mix < 22.9, "punto de mezcla: " + juce::String(mix, 3));
            expectWithinAbsoluteError(deck.getEndPosition(), 26.6, 0.1);

            deck.setPosition(mix - 0.5);
            deck.play();
            pump(mixer, 0.3, { &deck });
            expect(!deck.isReadyToMix());
            pump(mixer, 0.4, { &deck });
            expect(deck.isReadyToMix(), "llegó al punto de mezcla");

            deck.startMixFade();  // lo que hace AutoDJ al poner la siguiente
            expect(deck.isFadingOut());
            pump(mixer, 1.5, { &deck });
            expect(!deck.hasFinished(), "a mitad del fundido todavía se oye");
            pump(mixer, 1.6, { &deck });
            expect(deck.hasFinished(), "el fundido de 3 s terminó antes del punto de salida");
            expect(deck.getCurrentPosition() < deck.getEndPosition());
            deck.unload();

            toSystemCategory(file, CategoryRole::Sweepers);
            deck.loadTrack(file);
            expectWithinAbsoluteError(deck.getMixPoint(), deck.getEndPosition(), 1e-9,
                                      "un separador se oye entero: la siguiente entra en su punto de salida");
            deck.unload();
        }

        beginTest("Las identificaciones, cuñas y separadores se oyen enteros, aunque terminen con la voz más suave");
        {
            const auto file = import("PRUEBA - VOZ", 15.0, voiceWithHum);
            deck.loadTrack(file);
            expect(deck.getMixPoint() >= 12.3 && deck.getMixPoint() < deck.getEndPosition() - 2.0,
                   "en una categoría de música, la siguiente entra después de la última palabra, sin esperar el zumbido");
            deck.unload();

            for (auto role : { CategoryRole::StationIds, CategoryRole::Spots }) {
                toSystemCategory(file, role);
                deck.loadTrack(file);
                expectWithinAbsoluteError(deck.getMixPoint(), deck.getEndPosition(), 1e-9);
                deck.unload();
            }
        }

        beginTest("Una pausa intencional al final de la canción ya no la corta");
        {
            // Pausa de 2 s en 22-24 s de una canción de 30 s (pasado el 70 %).
            deck.loadTrack(import("PRUEBA - PAUSA", 30.0, toneBetween({ { 0.0, 22.0 }, { 24.0, 30.0 } })));
            deck.setPosition(21.5);
            deck.play();
            pump(mixer, 1.5, { &deck });
            expect(!deck.hasFinished() && !deck.isReadyToMix(), "en medio de la pausa sigue sonando");
            pump(mixer, 1.5, { &deck });
            expect(deck.isPlaying() && !deck.hasFinished(), "la canción continúa después de la pausa");
            deck.unload();
        }

        // El deck analiza en otro hilo; sin bucle de mensajes, la prueba aplica el resultado.
        auto waitForAnalysis = [&deck] {
            for (int i = 0; i < 200; ++i) {
                if (deck.applyPendingAnalysis()) return true;
                juce::Thread::sleep(50);
            }
            return false;
        };

        beginTest("Una pista sin analizar suena de inmediato y se analiza en otro hilo");
        {
            const auto file = writeWav(fx.lab.root.getChildFile("Library/GENERAL/SIN ANALIZAR.wav"), 12.0,
                                       toneBetween({ { 0.5, 12.0 } }));
            auto legacy = makeTrack(file.getFullPathName());
            legacy.normalizationGain = 1.5;  // ganancia por pico de la versión anterior
            fx.library.getDatabase().addTrack(legacy);

            deck.loadTrack(file);
            expect(deck.isLoaded(), "cargar no espera al análisis");
            expectWithinAbsoluteError(deck.getNormalizationGain(), 1.5f, 0.001f);

            expect(waitForAnalysis());
            const auto stored = fx.library.getTrackByPath(file.getFullPathName());
            expectEquals(stored.analysisVersion, TrackAnalyzer::version, "el análisis queda guardado");
            expectWithinAbsoluteError(deck.getCurrentPosition(), 0.48, 0.03, "salta el silencio inicial");
            expectWithinAbsoluteError((double) deck.getNormalizationGain(), stored.normalizationGain, 1.0e-4,
                                      "si aún no suena, toma la ganancia nueva");
            deck.unload();
        }

        beginTest("Si ya está al aire cuando termina el análisis, la ganancia no salta");
        {
            const auto file = writeWav(outside.root.getChildFile("fuera de la biblioteca.wav"), 15.0,
                                       toneBetween({ { 0.0, 12.0 } }));
            deck.loadTrack(file);
            deck.play();
            expect(waitForAnalysis());
            expectWithinAbsoluteError(deck.getNormalizationGain(), 1.0f, 0.001f);
            expectWithinAbsoluteError(deck.getEndPosition(), 12.02, 0.03, "el punto de salida sí se aplica");
            deck.unload();
        }
    }
};

//==============================================================================
namespace
{
    TrackInfo song(int id, const juce::String& artist, const juce::String& title, double seconds)
    {
        auto t = makeTrack("/m/" + juce::String(id) + ".mp3", artist, title);
        t.id = id;
        t.duration = seconds;
        return t;
    }

    juce::String localTimestamp(juce::Time t) { return t.formatted("%Y-%m-%d %H:%M:%S"); }
}

class GeneratorTests : public juce::UnitTest
{
public:
    GeneratorTests() : juce::UnitTest("Generador de listas", "RAAADIO") {}

    void runTest() override
    {
        // 12 canciones de 3 minutos, 4 artistas, todas de la categoría 1.
        PlaylistGenerator::Pool pool;
        for (int i = 0; i < 12; ++i) {
            pool.candidates.push_back(song(i + 1, "ARTISTA " + juce::String(i % 4), "CANCION " + juce::String(i), 180.0));
            pool.categoriesOf[i + 1] = { 1 };
        }
        juce::Random random(42);
        juce::String error;

        beginTest("Respeta la separación de artistas y llega a la duración pedida");
        {
            GeneratorSettings settings;
            settings.artistSeparation = 3;
            settings.titleSeparation = 5;
            settings.categorySeparation = 3;  // con una sola categoría no puede cumplirse
            settings.lastPlayedSeparation = 0;
            settings.targetDurationVal = 60;

            const auto list = PlaylistGenerator::buildSequence(settings, pool, random, error);
            expect(error.isEmpty(), "una sola categoría ya no deja al generador atascado: " + error);

            double total = 0.0;
            for (const auto& t : list) total += t.getAirDuration();
            expect(total >= 3600.0 && total - list.back().getAirDuration() < 3600.0, "se detiene al llegar a la hora");

            bool separated = true;
            for (size_t i = 1; i < list.size(); ++i)
                for (size_t back = 1; back <= 3 && back <= i; ++back)
                    separated &= list[i].artist != list[i - back].artist;
            expect(separated, "ningún artista se repite en menos de 3 pistas");
        }

        beginTest("Intercala un separador cada N canciones, y su duración cuenta");
        {
            auto withIds = pool;
            withIds.separators = { song(100, "RAAADIO", "ID 1", 10.0), song(101, "RAAADIO", "ID 2", 10.0) };
            GeneratorSettings settings;
            settings.categorySeparation = 0;
            settings.lastPlayedSeparation = 0;
            settings.targetDurationVal = 30;
            settings.separatorCategoryId = 9;
            settings.separatorInterval = 3;

            const auto list = PlaylistGenerator::buildSequence(settings, withIds, random, error);
            bool pattern = !list.empty();
            for (size_t i = 0; i < list.size(); ++i)
                pattern &= (list[i].id >= 100) == (i % 4 == 0);
            expect(pattern, "separador, tres canciones, separador...");

            double total = 0.0;
            for (const auto& t : list) total += t.getAirDuration();
            expect(total >= 1800.0 && total - list.back().getAirDuration() < 1800.0);
        }

        auto seconds = [](const std::vector<TrackInfo>& list) {
            double total = 0.0;
            for (const auto& t : list) total += t.getAirDuration();
            return total;
        };
        auto repeats = [](const std::vector<TrackInfo>& list) {
            std::set<int> seen;
            int n = 0;
            for (const auto& t : list) n += seen.insert(t.id).second ? 0 : 1;
            return n;
        };

        beginTest("Una categoría chica junto a una grande: con los valores por defecto no se repiten canciones");
        {
            // 300 de salsa y 10 boleros. Antes, para "cambiar de categoría", la mitad de la lista
            // eran boleros y cada uno sonaba hasta 4 veces en 4 horas.
            PlaylistGenerator::Pool skewed;
            for (int i = 0; i < 300; ++i) {
                skewed.candidates.push_back(song(1000 + i, "SALSERO " + juce::String(i % 60), "SALSA " + juce::String(i), 210.0));
                skewed.categoriesOf[1000 + i] = { 1 };
            }
            for (int i = 0; i < 10; ++i) {
                skewed.candidates.push_back(song(2000 + i, "BOLERISTA " + juce::String(i % 5), "BOLERO " + juce::String(i), 180.0));
                skewed.categoriesOf[2000 + i] = { 2 };
            }
            GeneratorSettings settings;  // los valores por defecto
            settings.lastPlayedSeparation = 0;
            settings.targetDurationVal = 4;
            settings.targetDurationIsHours = true;
            juce::Random r(7);
            PlaylistGenerator::Report report;
            const auto list = PlaylistGenerator::buildSequence(settings, skewed, r, error, &report);
            expect(error.isEmpty() && report.reachedTarget);
            expect(seconds(list) >= 4 * 3600.0);
            expectEquals(repeats(list), 0, "hay canciones de sobra: ninguna se repite");
            expectEquals(report.repeatedSongs, 0);
            expectEquals(report.relaxed(), 0, "una regla que la biblioteca no permite no cuenta como incumplida");
            expectEquals(PlaylistGenerator::feasibleCategorySeparation(3, skewed), 0);

            bool artistsApart = true;
            for (size_t i = 1; i < list.size(); ++i)
                for (size_t back = 1; back <= 5 && back <= i; ++back) artistsApart &= list[i].artist != list[i - back].artist;
            expect(artistsApart, "y los artistas siguen separados por 5 canciones");
        }

        beginTest("Categorías parejas: cambia de categoría como se pide");
        {
            PlaylistGenerator::Pool even;
            for (int i = 0; i < 160; ++i) {
                even.candidates.push_back(song(i + 1, "ARTISTA " + juce::String(i), "TEMA " + juce::String(i), 200.0));
                even.categoriesOf[i + 1] = { 1 + i % 4 };
            }
            expectEquals(PlaylistGenerator::feasibleCategorySeparation(3, even), 3);
            GeneratorSettings settings;
            settings.lastPlayedSeparation = 0;
            settings.targetDurationVal = 2;
            settings.targetDurationIsHours = true;
            juce::Random r(3);
            PlaylistGenerator::Report report;
            const auto list = PlaylistGenerator::buildSequence(settings, even, r, error, &report);
            bool apart = true;
            for (size_t i = 1; i < list.size(); ++i)
                for (size_t back = 1; back <= 3 && back <= i; ++back)
                    apart &= even.categoriesOf[list[i].id] != even.categoriesOf[list[i - back].id];
            expect(apart && report.relaxed() == 0 && repeats(list) == 0);
        }

        beginTest("Sigue de la cola: no repite lo que ya está en ella ni el artista de la última canción");
        {
            // 40 canciones de 8 artistas; en la cola ya están las 20 primeras.
            PlaylistGenerator::Pool queuedPool;
            for (int i = 0; i < 40; ++i) {
                queuedPool.candidates.push_back(song(i + 1, "ARTISTA " + juce::String(i % 8), "TEMA " + juce::String(i), 180.0));
                queuedPool.categoriesOf[i + 1] = { 1 };
            }
            for (int i = 0; i < 20; ++i) {
                queuedPool.preceding.push_back(queuedPool.candidates[(size_t) i]);
                queuedPool.queued.insert(i + 1);
            }
            GeneratorSettings settings;
            settings.artistSeparation = 3;
            settings.lastPlayedSeparation = 0;
            settings.targetDurationVal = 30;  // 10 canciones: caben sin repetir las de la cola
            bool fresh = true, boundary = true;
            for (int seed = 1; seed <= 20; ++seed) {
                juce::Random r(seed);
                PlaylistGenerator::Report report;
                const auto list = PlaylistGenerator::buildSequence(settings, queuedPool, r, error, &report);
                for (const auto& t : list) fresh &= t.id > 20;
                fresh &= report.repeatedSongs == 0;
                // Las tres últimas de la cola son de los artistas 1, 2 y 3 (pistas 18, 19 y 20).
                for (size_t i = 0; i < 3 && i < list.size(); ++i)
                    for (size_t back = 0; back + i < 3; ++back)
                        boundary &= list[i].artist != queuedPool.preceding[19 - back].artist;
            }
            expect(fresh, "las 20 que ya están en la cola no vuelven a salir");
            expect(boundary, "la separación de artistas cuenta desde lo que ya está en la cola");

            // Si se pide más de lo que queda sin usar, se repite, y el informe lo dice.
            settings.targetDurationVal = 90;
            juce::Random r(5);
            PlaylistGenerator::Report report;
            const auto list = PlaylistGenerator::buildSequence(settings, queuedPool, r, error, &report);
            expect(seconds(list) >= 90 * 60.0 && report.repeatedSongs > 0 && error.isEmpty());
        }

        beginTest("Con pocas canciones llega igual a la duración: repite, pero nunca la misma seguida");
        {
            PlaylistGenerator::Pool tiny;
            for (int i = 0; i < 3; ++i) {
                tiny.candidates.push_back(song(i + 1, "UNICO ARTISTA", "TEMA " + juce::String(i), 200.0));
                tiny.categoriesOf[i + 1] = { 1 };
            }
            GeneratorSettings settings;  // pide separar 5 artistas y 10 títulos: imposible con 3 canciones
            settings.lastPlayedSeparation = 0;
            juce::Random r(11);
            PlaylistGenerator::Report report;
            const auto list = PlaylistGenerator::buildSequence(settings, tiny, r, error, &report);
            expect(error.isEmpty() && report.reachedTarget && seconds(list) >= 3600.0, "antes se quedaba atascado o a medias");
            expect(report.repeatedSongs > 0);
            bool neverBackToBack = true;
            for (size_t i = 2; i < list.size(); ++i) neverBackToBack &= list[i].id != list[i - 1].id && list[i].id != list[i - 2].id;
            expect(neverBackToBack, "con 3 canciones, cada una vuelve después de las otras dos");

            // Un artista con casi todas las canciones: se llega a la duración acercándolo.
            PlaylistGenerator::Pool dominated;
            for (int i = 0; i < 20; ++i) dominated.candidates.push_back(song(i + 1, "DIOMEDES", "TEMA " + juce::String(i), 240.0));
            dominated.candidates.push_back(song(50, "OTRO", "UNA", 240.0));
            dominated.candidates.push_back(song(51, "OTRO MAS", "DOS", 240.0));
            const auto second = PlaylistGenerator::buildSequence(settings, dominated, r, error, &report);
            expect(error.isEmpty() && seconds(second) >= 3600.0);
        }

        beginTest("Una pista sin duración no deja al generador dando vueltas; hay un tope de filas");
        {
            PlaylistGenerator::Pool broken;
            broken.candidates.push_back(song(1, "A", "SIN DURACION", 0.0));
            GeneratorSettings settings;
            juce::Random r(1);
            PlaylistGenerator::Report report;
            const auto list = PlaylistGenerator::buildSequence(settings, broken, r, error, &report);
            expectEquals((int) list.size(), PlaylistGenerator::maxRows);
            expect(!report.reachedTarget && error.isNotEmpty());

            // Y de la biblioteca ni siquiera sale como candidata.
            std::vector<TrackInfo> all = { song(1, "A", "SIN DURACION", 0.0), song(2, "B", "BUENA", 200.0) };
            settings.lastPlayedSeparation = 0;
            const auto candidates = PlaylistGenerator::selectCandidates(all, {}, {}, settings, juce::Time::getCurrentTime());
            expect(candidates.size() == 1 && candidates[0].id == 2);
        }

        beginTest("Ajustes: a una plantilla vieja le faltan campos y quedan con su valor por defecto");
        {
            const auto old = GeneratorSettings::fromVar(juce::JSON::parse(R"({"includedCategories": [4], "targetDurationVal": 90, "insertionMode": 7})"));
            expect(old.includedCategories == std::vector<int> { 4 });
            expectEquals(old.targetDurationVal, 90);
            expectEquals(old.artistSeparation, 5);
            expectEquals(old.titleSeparation, 10);
            expectEquals(old.lastPlayedSeparation, 60);
            expectEquals(old.separatorCategoryId, -1);
            expect(old.insertionMode == InsertionMode::Replace, "un modo fuera de rango se limita");
            expect(old.hasDefaultRules() && !old.usesSeparators());

            GeneratorSettings custom;
            custom.artistSeparation = 0;
            custom.separatorCategoryId = 3;
            custom.separatorInterval = 4;
            custom.targetDurationIsHours = true;
            const auto back = GeneratorSettings::fromVar(custom.toVar());
            expect(back.artistSeparation == 0 && back.usesSeparators() && back.targetDurationIsHours && !back.hasDefaultRules());
            expectEquals(back.targetSeconds(), 60 * 3600.0);
        }

        beginTest("Con la biblioteca: no usa archivos que faltan, sigue de la cola y dice con qué cuenta");
        {
            LibraryFixture fx;
            auto& lib = fx.library;
            std::vector<TrackInfo> songs;
            songs.reserve(8);
            for (int i = 0; i < 8; ++i) songs.push_back(fx.add("Library/M/" + juce::String(i) + ".wav", "TEMA " + juce::String(i)));
            juce::File(juce::String::fromUTF8(songs[7].path.c_str())).deleteFile();  // a esta le falta el archivo

            GeneratorSettings settings;
            settings.targetDurationVal = 9;  // tres canciones de 3 minutos
            Playlist playlist;
            PlaylistGenerator generator(lib);

            const auto available = generator.check(settings, playlist);
            expectEquals(available.songs, 7, "la que no tiene archivo no cuenta");
            expectEquals(available.missingFiles, 1);
            expectWithinAbsoluteError(available.seconds, 7 * 180.0, 0.5);
            expect(available.recentlyPlayed == 0 && available.alreadyQueued == 0);

            const auto first = generator.generate(settings, playlist);
            expect(first.success && first.reachedTarget && first.message.isEmpty());
            expect(first.songs == 3 && first.separators == 0 && first.tracksGenerated == 3 && first.tracks.size() == 3);
            expectWithinAbsoluteError(first.seconds, 540.0, 0.5);
            expectEquals(first.missingFiles, 1);
            expectEquals(first.repeatedSongs, 0);

            // Otro bloque al final: no repite las tres que ya están en la cola.
            const auto second = generator.generate(settings, playlist);
            std::set<int> ids;
            for (const auto& t : playlist.getQueue()) ids.insert(t.id);
            expect(second.success && playlist.getQueue().size() == 6 && ids.size() == 6, "seis canciones distintas en la cola");
            expect(ids.count(songs[7].id) == 0, "la que no tiene archivo no sale");
            expectEquals(generator.check(settings, playlist).alreadyQueued, 6);

            // Queda una sola sin usar: el tercer bloque repite dos, y lo dice.
            const auto third = generator.generate(settings, playlist);
            expect(third.success && third.songs == 3);
            expectEquals(third.repeatedSongs, 2);

            // Lo que acaba de sonar no entra (60 minutos por defecto).
            lib.incrementPlayCount(songs[0].id);
            const auto later = generator.check(settings, playlist);
            expectEquals(later.songs, 6);
            expectEquals(later.recentlyPlayed, 1);

            // Reemplazar: la cola anterior no cuenta.
            settings.insertionMode = InsertionMode::Replace;
            const auto replaced = generator.generate(settings, playlist);
            expect(replaced.success && playlist.getQueue().size() == 3);
            expectEquals(replaced.repeatedSongs, 0);

            // Sin nada que usar: el error lo dice.
            settings.includedCategories = { 9999 };
            const auto nothing = generator.generate(settings, playlist);
            expect(!nothing.success && nothing.message.isNotEmpty() && playlist.getQueue().size() == 3, "la cola no se toca si no hay con qué");
        }

        beginTest("Candidatas: categorías incluidas y excluidas, y última reproducción en hora local");
        {
            const auto now = juce::Time::getCurrentTime();
            std::vector<TrackInfo> all = { song(1, "A", "1", 200), song(2, "B", "2", 200), song(3, "C", "3", 200),
                                           song(4, "D", "4", 200), song(5, "E", "5", 200), song(6, "F", "6", 200) };
            all[3].lastPlayed = localTimestamp(now - juce::RelativeTime::minutes(10)).toStdString();
            all[4].lastPlayed = localTimestamp(now - juce::RelativeTime::hours(2)).toStdString();
            const std::unordered_map<int, std::vector<int>> categories = { { 1, { 1 } }, { 2, { 1, 2 } }, { 3, { 3 } },
                                                                           { 4, { 1 } }, { 5, { 1 } } };  // la 6 no tiene
            auto ids = [](const std::vector<TrackInfo>& tracks) {
                juce::String s;
                for (const auto& t : tracks) s << t.id;
                return s;
            };

            GeneratorSettings settings;
            settings.includedCategories = { 1 };
            settings.excludedCategories = { 2 };
            settings.lastPlayedSeparation = 60;
            expectEquals(ids(PlaylistGenerator::selectCandidates(all, categories, { 1, 2, 3 }, settings, now)), juce::String("15"),
                         "la que sonó hace 10 minutos queda fuera (antes la hora local se leía como UTC)");

            settings.includedCategories.clear();
            settings.excludedCategories.clear();
            expectEquals(ids(PlaylistGenerator::selectCandidates(all, categories, { 1, 2, 3 }, settings, now)), juce::String("12356"),
                         "sin categorías elegidas vale toda la música, también la que no tiene categoría");
            expectEquals(ids(PlaylistGenerator::selectCandidates(all, categories, { 1, 2 }, settings, now)), juce::String("1256"),
                         "la de otras categorías (las de la emisora) no entra");
        }
    }
};

//==============================================================================
class CategoryTests : public juce::UnitTest
{
public:
    CategoryTests() : juce::UnitTest("Categorías de la emisora", "RAAADIO") {}

    static juce::String names(const std::vector<Category>& categories)
    {
        juce::StringArray n;
        for (const auto& c : categories) n.add(juce::String(c.name));
        return n.joinIntoString(",");
    }

    static int categoryId(MusicLibrary& library, const juce::String& name)
    {
        for (const auto& c : library.getCategories())
            if (juce::String(c.name) == name) return c.id;
        return -1;
    }

    void runTest() override
    {
        beginTest("Una base nueva trae solo las tres categorías fijas, en orden");
        {
            LibraryFixture fx;
            const auto categories = fx.library.getCategories();
            expectEquals(names(categories), juce::String::fromUTF8("IDENTIFICACIONES,CUÑAS,SEPARADORES"),
                         "ni géneros ni EMERGENCIA");
            if (categories.size() == 3) {
                expect(categories[0].role == CategoryRole::StationIds && categories[1].role == CategoryRole::Spots
                       && categories[2].role == CategoryRole::Sweepers);
                expectEquals(categories[1].folderFormat, 0, "las cuñas se guardan en carpetas por fecha");
            }
        }

        beginTest("Las fijas no se renombran, no se eliminan ni se mueven; las demás sí");
        {
            LibraryFixture fx;
            auto& lib = fx.library;
            const juce::String spots(lib.getSystemCategory(CategoryRole::Spots).name);
            lib.addCategory("SALSA");
            lib.addCategory("BOLERO");

            expect(!lib.renameCategory(spots, "ANUNCIOS"), "renombrar CUÑAS rompería la publicidad");
            expect(!lib.deleteCategory("IDENTIFICACIONES"));
            expect(!lib.renameCategory("SALSA", spots), "una categoría no puede tomar el nombre de una fija");

            juce::StringArray order { "BOLERO", "SALSA", "SEPARADORES" };
            order.add(spots);
            order.add("IDENTIFICACIONES");
            lib.updateCategoryOrder(order);
            expectEquals(names(lib.getCategories()), juce::String::fromUTF8("IDENTIFICACIONES,CUÑAS,SEPARADORES,BOLERO,SALSA"),
                         "las fijas siguen primero aunque se reordene");

            const auto track = fx.add("Library/BOLERO.wav", "BOLERO");
            lib.setTrackCategories(track.id, { categoryId(lib, "BOLERO") });
            expect(lib.renameCategory("SALSA", "SALSA BRAVA"));
            expect(lib.deleteCategory("BOLERO"));
            expectEquals(names(lib.getCategories()), juce::String::fromUTF8("IDENTIFICACIONES,CUÑAS,SEPARADORES,SALSA BRAVA"));
            expectEquals(lib.getTrackById(track.id).id, track.id, "eliminar la categoría no borra sus pistas");
            expect(lib.getTrackCategories(track.id).empty());
        }

        beginTest("Base de una versión anterior: ANUNCIOS y PROMOCIONES pasan a CUÑAS y SEPARADORES con sus pistas");
        {
            TempLab lab;
            const auto dbFile = lab.root.getChildFile("raaadio.db");

            // Esquema y categorías como los dejaban las versiones anteriores; además, el
            // usuario ya había creado SEPARADORES.
            sqlite3* raw = nullptr;
            sqlite3_open(dbFile.getFullPathName().toRawUTF8(), &raw);
            sqlite3_exec(raw,
                "CREATE TABLE tracks (id INTEGER PRIMARY KEY AUTOINCREMENT, path TEXT UNIQUE, artist TEXT, title TEXT, "
                "play_count INTEGER DEFAULT 0, duration REAL DEFAULT 0.0);"
                "CREATE TABLE categories (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT UNIQUE, "
                "sort_order INTEGER DEFAULT 0, folder_format INTEGER DEFAULT 1);"
                "CREATE TABLE track_categories (track_id INTEGER, category_id INTEGER, PRIMARY KEY (track_id, category_id));"
                "INSERT INTO categories (id, name, sort_order, folder_format) VALUES "
                "(1, 'IDENTIFICACIONES', 1, 1), (2, 'ANUNCIOS', 2, 0), (3, 'PROMOCIONES', 3, 1), (4, 'EMERGENCIA', 4, 1), "
                "(5, 'SALSA', 5, 1), (6, 'VALLENATO', 6, 1), (7, 'MIS ÉXITOS', 7, 1), (8, 'SEPARADORES', 8, 1);"
                "INSERT INTO tracks (id, path, artist, title) VALUES (1, 'a.mp3', 'A', 'CUNA'), (2, 'b.mp3', 'B', 'PROMO'), "
                "(3, 'c.mp3', 'C', 'VALLENATO'), (4, 'd.mp3', 'D', 'CORTINILLA');"
                "INSERT INTO track_categories VALUES (1, 2), (2, 3), (3, 6), (4, 8);",
                nullptr, nullptr, nullptr);
            sqlite3_close(raw);

            Database db;
            db.open(dbFile.getFullPathName().toStdString());
            expect(db.initSchema());
            // _Temporales: la categoría fija de los audios temporales (la aplicación no la muestra
            // entre las demás: MusicLibrary::getCategories).
            expectEquals(names(db.getCategories()),
                         juce::String::fromUTF8("IDENTIFICACIONES,CUÑAS,SEPARADORES,_Temporales,VALLENATO,MIS ÉXITOS"),
                         "se borran los géneros y EMERGENCIA vacíos; los que tienen pistas y los del usuario se quedan");
            expectEquals(db.getSystemCategory(CategoryRole::Temporaries).folderFormat, 0, "los temporales, en carpetas por fecha");

            const auto spots = db.getSystemCategory(CategoryRole::Spots);
            expectEquals(spots.id, 2, "ANUNCIOS se renombra: conserva su id y su formato de carpetas");
            expectEquals(spots.folderFormat, 0);
            expect(db.getTrackCategories(1) == std::vector<int> { 2 }, "la cuña sigue en su categoría");
            expectEquals(db.getSystemCategory(CategoryRole::StationIds).id, 1);
            expectEquals(db.getSystemCategory(CategoryRole::Sweepers).id, 8, "la SEPARADORES que ya existía se adopta");
            expect(db.getTrackCategories(2) == std::vector<int> { 8 }, "las pistas de PROMOCIONES se unen a SEPARADORES");
            expect(db.getTrackCategories(4) == std::vector<int> { 8 });
            expect(db.getTrackCategories(3) == std::vector<int> { 6 });

            // La limpieza se hace una sola vez: una categoría vacía creada después se respeta.
            db.addCategory("SALSA");
            db.close();
            Database reopened;
            reopened.open(dbFile.getFullPathName().toStdString());
            expect(reopened.initSchema());
            expectEquals(names(reopened.getCategories()),
                         juce::String::fromUTF8("IDENTIFICACIONES,CUÑAS,SEPARADORES,_Temporales,VALLENATO,MIS ÉXITOS,SALSA"),
                         "al reiniciar no se vuelve a limpiar ni se repite la migración");
        }

        beginTest("Sin categorías elegidas, el generador arma la música sin cuñas ni identificaciones");
        {
            LibraryFixture fx;
            auto& lib = fx.library;
            lib.addCategory("SALSA");
            const auto song = fx.add("Library/SALSA.wav", "CANCION");
            const auto spot = fx.add("Library/CUNA.wav", "CUNA");
            const auto id = fx.add("Library/ID.wav", "ESTACION");
            const auto loose = fx.add("Library/GENERAL/SUELTA.wav", "SIN CATEGORIA");
            lib.setTrackCategories(song.id, { categoryId(lib, "SALSA") });
            lib.setTrackCategories(spot.id, { lib.getSystemCategory(CategoryRole::Spots).id });
            lib.setTrackCategories(id.id, { lib.getSystemCategory(CategoryRole::StationIds).id });

            GeneratorSettings settings;
            settings.lastPlayedSeparation = 0;
            settings.targetDurationVal = 10;
            Playlist playlist;
            PlaylistGenerator generator(lib);
            expect(generator.generate(settings, playlist).success);

            bool onlyMusic = !playlist.getQueue().empty(), looseUsed = false;
            for (const auto& t : playlist.getQueue()) {
                onlyMusic &= t.id == song.id || t.id == loose.id;
                looseUsed |= t.id == loose.id;
            }
            expect(onlyMusic, "ni cuñas ni identificaciones");
            expect(looseUsed, "la canción sin categoría también suena");
        }
    }
};

//==============================================================================
class QueueTimingTests : public juce::UnitTest
{
public:
    QueueTimingTests() : juce::UnitTest("Horarios de la cola y la pauta", "RAAADIO") {}

    void runTest() override
    {
        beginTest("La siguiente pista entra en el punto de mezcla de la música; lo demás se oye entero");
        {
            expectWithinAbsoluteError(QueueTiming::secondsUntilNext(song(1, "A", "A", 200.0)), 200.0, 1e-9,
                                      "sin análisis no se adelanta");
            auto trimmed = song(3, "B", "B", 200.0);
            trimmed.cueIn = 1.0;
            trimmed.cueOut = 191.0;
            trimmed.mixPoint = 186.0;
            expectWithinAbsoluteError(QueueTiming::secondsUntilNext(trimmed), 185.0, 1e-9, "cuenta de entrada a mezcla");

            auto spot = trimmed;
            spot.systemRole = CategoryRole::Spots;
            expectWithinAbsoluteError(QueueTiming::secondsUntilNext(spot), 190.0, 1e-9, "una cuña, hasta su punto de salida");

            auto shortTrack = song(2, "ID", "ID", 8.0);
            shortTrack.cueOut = 8.0;
            shortTrack.mixPoint = 6.0;
            expectWithinAbsoluteError(QueueTiming::secondsUntilNext(shortTrack), 8.0, 1e-9, "lo corto, también");
        }

        beginTest("Una tanda se inserta donde empieza más cerca de su hora");
        {
            const std::vector<TrackInfo> queue = { song(1, "A", "A", 180.0), song(2, "B", "B", 180.0), song(3, "C", "C", 180.0) };
            const auto offsets = QueueTiming::startOffsets(queue);
            expectEquals((int) offsets.size(), 3);
            expectWithinAbsoluteError(offsets[2], 360.0, 1e-9);

            const juce::Time start(2026, 8, 22, 12, 0, 0, 0, true);
            auto at = [&](double seconds) { return start + juce::RelativeTime(seconds); };
            expectEquals(QueueTiming::findInsertionIndex(queue, start, at(-600.0)), 0, "la hora ya pasó: va primero");
            expectEquals(QueueTiming::findInsertionIndex(queue, start, at(360.0)), 2, "entre la segunda y la tercera");
            expectEquals(QueueTiming::findInsertionIndex(queue, start, at(3600.0)), 3, "la cola termina antes: va al final");
            expectEquals(QueueTiming::findInsertionIndex({}, start, at(60.0)), 0);
        }

        beginTest("La tanda de una franja: una identificación y sus cuñas");
        {
            LibraryFixture fx;
            const auto id = fx.add("Library/ID.wav", "ESTACION");
            const auto ad1 = fx.add("Library/AD1.wav", "CUNA 1");
            const auto ad2 = fx.add("Library/AD2.wav", "CUNA 2");
            fx.library.setTrackCategories(id.id, { fx.library.getSystemCategory(CategoryRole::StationIds).id });

            AdvertisingManager ads;
            ads.initialise(fx.library);
            ads.setCueNumber(ad1.id, 1);
            ads.setCueNumber(ad2.id, 2);
            ads.addSchedule(1, "12:00");
            ads.addSchedule(2, "12:00");

            juce::Random random;
            const auto block = AdBreaks::buildBreak(fx.library, ads, "12:00", random);
            expectEquals((int) block.size(), 3);
            if (block.size() == 3) {
                expectEquals(block[0].id, id.id, "abre con la identificación");
                expect(block[1].id == ad1.id && block[2].id == ad2.id);
            }
            expect(AdBreaks::buildBreak(fx.library, ads, "13:00", random).empty(), "sin cuñas a esa hora no hay tanda");
            expectEquals(AdBreaks::timeOfSlot("07:30", juce::Time(2026, 8, 22, 23, 0, 0, 0, true)).formatted("%Y-%m-%d %H:%M"),
                         juce::String("2026-09-22 07:30"));
        }

        beginTest("Pauta automática: qué tandas tocan y cuáles ya se insertaron");
        {
            const juce::StringArray slots { "06:00", "12:00", "23:50", "00:00" };
            auto at = [](int day, int h, int m) { return juce::Time(2026, 8, day, h, m, 0, 0, true); };
            auto keys = [](const std::vector<juce::Time>& times) {
                juce::StringArray k;
                for (const auto& t : times) k.add(AdScheduler::keyFor(t));
                return k.joinIntoString(",");
            };

            expectEquals(keys(AdScheduler::dueBreaks(slots, at(22, 11, 45), {})), juce::String("2026-09-22 12:00"),
                         "20 minutos antes");
            expectEquals(keys(AdScheduler::dueBreaks(slots, at(22, 11, 45), { "2026-09-22 12:00" })), juce::String(),
                         "una tanda ya insertada no se repite");
            expectEquals(keys(AdScheduler::dueBreaks(slots, at(22, 23, 45), {})),
                         juce::String("2026-09-22 23:50,2026-09-23 00:00"), "cruza la medianoche");
            expectEquals(keys(AdScheduler::dueBreaks(slots, at(23, 0, 5), { "2026-09-23 00:00" })), juce::String(),
                         "la de las 23:50 ya quedó atrás de la tolerancia");
            expectEquals(keys(AdScheduler::dueBreaks(slots, at(22, 12, 8), {})), juce::String("2026-09-22 12:00"),
                         "si la emisora estaba cerrada, se inserta hasta 10 minutos tarde");
        }

        beginTest("Pauta automática: inserta la tanda a su hora, una sola vez");
        {
            LibraryFixture fx;
            const auto ad = fx.add("Library/AD.wav", "CUNA");
            AdvertisingManager ads;
            ads.initialise(fx.library);
            ads.setCueNumber(ad.id, 7);
            ads.addSchedule(7, "12:00");

            Playlist playlist;
            std::vector<TrackInfo> queue;
            queue.reserve(10);
            for (int i = 0; i < 10; ++i) queue.push_back(song(1000 + i, "A" + juce::String(i), "T", 180.0));
            playlist.insertTracks(queue, 0);

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            juce::AudioThumbnailCache cache(4);
            Deck deckA(1, fx.library, formats, cache), deckB(2, fx.library, formats, cache);

            const juce::Time now(2026, 8, 22, 11, 50, 0, 0, true);
            {
                AdScheduler scheduler(fx.library, ads, playlist, deckA, deckB);
                scheduler.checkNow(now);
                expectEquals(playlist.getQueue().size(), queue.size(), "sin la pauta automática activada no hace nada");

                ads.setAutoScheduleEnabled(true);
                scheduler.checkNow(now);
                const auto withBreak = playlist.getQueue();
                expectEquals((int) withBreak.size(), (int) queue.size() + 1);
                // Nada suena: la cola empieza ya; cada pista abre 180 s después de la anterior,
                // así que la posición 3 (a las 11:59:00) es la más cercana a las 12:00.
                expectEquals(withBreak[3].id, ad.id);

                scheduler.checkNow(now + juce::RelativeTime::minutes(1));
                expectEquals((int) playlist.getQueue().size(), (int) queue.size() + 1, "no se inserta dos veces");
            }
            AdScheduler restarted(fx.library, ads, playlist, deckA, deckB);
            restarted.checkNow(now + juce::RelativeTime::minutes(2));
            expectEquals((int) playlist.getQueue().size(), (int) queue.size() + 1, "ni después de reiniciar la emisora");
        }

        beginTest("Si la cola se vacía o se reemplaza antes de que suene la tanda, la tanda vuelve a insertarse");
        {
            LibraryFixture fx;
            const auto ad = fx.add("Library/AD.wav", "CUNA");
            AdvertisingManager ads;
            ads.initialise(fx.library);
            ads.setCueNumber(ad.id, 7);
            ads.addSchedule(7, "12:00");
            ads.setAutoScheduleEnabled(true);

            Playlist playlist;
            std::vector<TrackInfo> queue;
            queue.reserve(10);
            for (int i = 0; i < 10; ++i) queue.push_back(song(1000 + i, "A" + juce::String(i), "T", 180.0));
            playlist.insertTracks(queue, 0);

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            juce::AudioThumbnailCache cache(4);
            Deck deckA(1, fx.library, formats, cache), deckB(2, fx.library, formats, cache);
            AdScheduler scheduler(fx.library, ads, playlist, deckA, deckB);
            auto adsInQueue = [&playlist, &ad] {
                int n = 0;
                for (const auto& t : playlist.getQueue()) n += t.id == ad.id ? 1 : 0;
                return n;
            };

            const juce::Time now(2026, 8, 22, 11, 50, 0, 0, true);
            scheduler.checkNow(now);
            expectEquals(adsInQueue(), 1);

            // El generador en modo Reemplazar vacía la cola y pone la lista nueva.
            playlist.clear();
            playlist.insertTracks(queue, 0);
            scheduler.checkNow(now + juce::RelativeTime::minutes(1));
            expectEquals(adsInQueue(), 1, "la tanda vuelve, en la cola nueva");
            const auto replaced = playlist.getQueue();
            expect(replaced.size() > 3 && replaced[3].id == ad.id, "en el punto más cercano a las 12:00");
            scheduler.checkNow(now + juce::RelativeTime::minutes(2));
            expectEquals(adsInQueue(), 1, "una sola vez");

            // Una tanda que ya empezó a sonar no se repite aunque después se vacíe la cola.
            while (!playlist.isEmpty() && playlist.popNext().id != ad.id) {}
            playlist.clear();
            playlist.insertTracks(queue, 0);
            scheduler.checkNow(now + juce::RelativeTime::minutes(3));
            expectEquals(adsInQueue(), 0, "ya salió al aire");
        }
    }
};

//==============================================================================
class OnAirTests : public juce::UnitTest
{
public:
    OnAirTests() : juce::UnitTest("Reproducir ahora y lo que oyen los oyentes", "RAAADIO") {}

    void runTest() override
    {
        LibraryFixture fx;
        TempLab outside;
        auto toneFile = [&outside](const juce::String& name) {
            return writeWav(outside.root.getChildFile(name + ".wav"), 30.0, toneBetween({ { 0.0, 30.0 } }));
        };
        const auto first = toneFile("PRIMERA"), second = toneFile("SEGUNDA"), third = toneFile("TERCERA");

        AudioEngine engine(fx.library, false);
        auto& mixer = engine.getMixer();
        mixer.prepareToPlay(512, 44100.0);
        auto& a = engine.getDeck(0);
        auto& b = engine.getDeck(1);
        auto loaded = [](const Deck& d, const juce::File& f) { return juce::File(juce::String(d.getCurrentTrack().path)) == f; };

        beginTest("Sin nada al aire, la pista entra en el deck A y sale al aire");
        {
            a.setRouting(false, true);  // lo habían dejado en preescucha
            expect(OnAir::playNow(a, b, first));
            expect(loaded(a, first) && OnAir::isOnAir(a));
            expect(!b.isLoaded(), "el deck B no se toca");
            expect(OnAir::nowPlaying(a, b) == &a);
        }

        beginTest("Con algo al aire, entra en el deck libre y lo que sonaba baja con un fundido de 2 s");
        {
            pump(mixer, 0.5, { &a });
            expect(OnAir::playNow(a, b, second));
            expect(loaded(b, second) && OnAir::isOnAir(b), "la pista nueva suena en B");
            expect(loaded(a, first) && a.isPlaying(), "A no se corta en seco");
            pump(mixer, 1.0, { &a, &b });
            expect(!a.hasFinished(), "a mitad del fundido todavía se oye");
            pump(mixer, 1.2, { &a, &b });
            expect(a.hasFinished(), "el fundido terminó");
            expect(OnAir::nowPlaying(a, b) == &b);
            a.unload();  // lo que hace AutoDJ con un deck que terminó
        }

        beginTest("Una preescucha no cambia el título que ven los oyentes");
        {
            int stateChanges = 0;
            a.onStateChange = [&stateChanges] { ++stateChanges; };
            a.loadTrack(first);
            a.setRouting(false, true);  // solo CUE
            juce::Thread::sleep(20);    // arranca después que B
            a.play();
            expect(OnAir::nowPlaying(a, b) == &b, "sigue siendo la pista de B aunque A arrancó después");

            stateChanges = 0;
            a.setRouting(true, false);
            expectEquals(stateChanges, 1, "pasar al aire avisa, para actualizar el título");
            expect(OnAir::nowPlaying(a, b) == &a);
            a.setRouting(false, true);
            expect(OnAir::nowPlaying(a, b) == &b);
            a.onStateChange = nullptr;
        }

        beginTest("Con un deck al aire y el otro en preescucha, reproducir ahora toma el de la preescucha");
        {
            expect(OnAir::playNow(a, b, third));
            expect(loaded(a, third) && OnAir::isOnAir(a));
            expect(loaded(b, second) && b.isPlaying(), "B baja con el fundido, no se corta");
            pump(mixer, 2.2, { &a, &b });
            expect(b.hasFinished());
            b.unload();
        }

        beginTest("Si el archivo no se puede leer, lo que está al aire sigue igual");
        {
            const auto broken = outside.root.getChildFile("ROTA.wav");
            broken.replaceWithText("no es audio");
            expect(!OnAir::playNow(a, b, broken));
            expect(!OnAir::playNow(a, b, outside.root.getChildFile("NO EXISTE.wav")));
            expect(loaded(a, third) && OnAir::isOnAir(a));
            expect(!b.isLoaded());
        }

        beginTest("Con A al aire y B en preescucha, reproducir ahora toma B y A baja con el fundido");
        {
            b.loadTrack(first);
            b.setRouting(false, true);  // solo CUE
            b.play();
            expect(OnAir::playNow(a, b, second));
            expect(loaded(b, second) && OnAir::isOnAir(b), "la pista nueva entra en el deck de la preescucha");
            expect(loaded(a, third) && a.isPlaying(), "A no se corta en seco");
            pump(mixer, 2.2, { &a, &b });
            expect(a.hasFinished());
            a.unload();
        }

        beginTest("En plena mezcla (los dos al aire), reproducir ahora reemplaza al que arrancó antes");
        {
            a.loadTrack(first);
            a.setRouting(true, false);
            juce::Thread::sleep(20);  // B arrancó antes: es el que ya se está yendo
            a.play();
            expect(OnAir::isOnAir(a) && OnAir::isOnAir(b));
            expect(OnAir::playNow(a, b, third));
            expect(loaded(b, third) && OnAir::isOnAir(b), "entra en B");
            expect(loaded(a, first) && a.isPlaying(), "A sigue sonando y se despide con el fundido");
            pump(mixer, 2.2, { &a, &b });
            expect(a.hasFinished());
            expect(OnAir::nowPlaying(a, b) == &b);
            a.unload();
        }

        auto trackOf = [](const juce::File& f) {
            TrackInfo t;
            t.path = f.getFullPathName().toStdString();
            t.artist = "ARTISTA";
            t.title = "TITULO";
            return t;
        };

        beginTest("Desde la cola, con algo al aire, primero pregunta: cancelar no cambia nada y aceptar la pone");
        {
            int started = 0;
            OnAir::requestPlayNow(a, b, trackOf(first), [&] { ++started; return true; });
            expect(!a.isLoaded(), "mientras se pregunta no se toca nada");
            expect(answerModal(0), "se cancela");
            expect(started == 0 && !a.isLoaded() && OnAir::isOnAir(b));

            OnAir::requestPlayNow(a, b, trackOf(first), [&] { ++started; return true; });
            expect(answerModal(1), "se acepta");
            expect(started == 1 && loaded(a, first) && OnAir::isOnAir(a));
            pump(mixer, 2.2, { &a, &b });
            b.unload();
        }

        beginTest("Si mientras preguntaba la pista ya salió de la cola, aceptar no la pone; si no se puede leer, avisa");
        {
            OnAir::requestPlayNow(a, b, trackOf(second), [] { return false; });
            expect(answerModal(1));
            expect(!b.isLoaded() && loaded(a, first), "la cola cambió: no se pone");

            OnAir::requestPlayNow(a, b, trackOf(outside.root.getChildFile("ROTA.wav")), {});
            expect(answerModal(1), "pregunta");
            expect(answerModal(1), "y avisa que no se pudo poner");
            expect(loaded(a, first) && OnAir::isOnAir(a), "lo que estaba al aire sigue");
        }

        beginTest("Sin nada al aire, reproducir ahora no pregunta");
        {
            a.stop();
            a.unload();
            OnAir::requestPlayNow(a, b, trackOf(second), {});
            expect(juce::Component::getCurrentlyModalComponent() == nullptr);
            expect(loaded(a, second) && OnAir::isOnAir(a));
            a.stop();
            a.unload();
        }
    }
};

//==============================================================================
class AutoMixTests : public juce::UnitTest
{
public:
    AutoMixTests() : juce::UnitTest("Encadenado de AutoDJ", "RAAADIO") {}

    void runTest() override
    {
        LibraryFixture fx;
        TempLab outside;
        auto import = [&](const juce::String& name, double seconds, const Signal& signal, CategoryRole role) {
            const auto file = writeWav(outside.root.getChildFile(name + ".wav"), seconds, signal);
            fx.library.processAndAddTrack(file);
            const auto track = fx.library.search(name.fromFirstOccurrenceOf(" - ", false, false), "").front();
            if (role != CategoryRole::None)
                fx.library.getDatabase().addTrackCategory(track.id, fx.library.getSystemCategory(role).id);
            return fx.library.getTrackById(track.id);
        };
        const auto music = import("PRUEBA - CANCION", 30.0, toneWithFadeOut(20.0), CategoryRole::None);
        const auto sweeper = import("PRUEBA - SEPARADOR", 15.0, voiceWithHum, CategoryRole::Sweepers);
        const auto stationId = import("PRUEBA - ID", 2.0, toneBetween({ { 0.0, 2.0 } }), CategoryRole::StationIds);
        const auto next = import("PRUEBA - SIGUIENTE", 30.0, toneBetween({ { 0.0, 30.0 } }), CategoryRole::None);

        AudioEngine engine(fx.library, false);
        auto& mixer = engine.getMixer();
        mixer.prepareToPlay(512, 44100.0);
        auto& a = engine.getDeck(0);
        auto& b = engine.getDeck(1);
        Playlist playlist;
        AutoDJ autoDJ(engine, playlist);

        auto playing = [](const Deck& d, const TrackInfo& t) { return d.isPlaying() && d.getCurrentTrack().id == t.id; };
        // Como el temporizador de AutoDJ: una revisión cada ~46 ms de audio.
        auto run = [&](double seconds) {
            const double tick = 4 * 512 / 44100.0;
            for (int i = 0; i * tick < seconds; ++i) {
                pump(mixer, tick + 1.0e-9, { &a, &b });
                autoDJ.timerCallback();
            }
        };

        beginTest("Una canción con final largo: lo siguiente entra en su punto de mezcla y ella se desvanece");
        {
            playlist.insertTracks({ music, sweeper, next }, 0);
            expect(autoDJ.resumePlayback());
            expect(playing(a, music));
            a.setPosition(a.getMixPoint() - 0.3);
            run(0.2);
            expect(!b.isPlaying(), "todavía no");
            run(0.3);
            expect(playing(b, sweeper), "el separador entra en el punto de mezcla");
            expect(a.isPlaying() && a.isFadingOut(), "la canción no se corta: se desvanece");
            run(3.2);
            expect(!a.isLoaded(), "terminado el fundido, AutoDJ la descarga");
        }

        beginTest("Un separador se oye entero: lo siguiente no entra mientras habla");
        {
            b.setPosition(12.0);  // la última palabra, más suave, va de 12.0 a 12.3 s; el zumbido, hasta 15 s
            run(2.5);
            expect(playing(b, sweeper) && !a.isPlaying(), "ni durante el zumbido del final");
            run(0.7);
            expect(playing(a, next), "al terminar el separador entra la siguiente");
            expect(!b.isLoaded());
        }

        beginTest("Una identificación más corta que el fundido de la canción anterior: lo siguiente no espera al fundido");
        {
            a.unload();
            playlist.clear();
            playlist.insertTracks({ music, stationId, next }, 0);
            expect(autoDJ.resumePlayback());
            expect(playing(a, music));
            a.setPosition(a.getMixPoint() - 0.1);
            run(0.3);
            expect(playing(b, stationId) && a.isFadingOut());
            run(2.3);
            expect(playing(b, next), "la identificación terminó: entra la siguiente en el mismo deck");
            expect(a.isFadingOut(), "mientras la canción todavía se desvanece");
            run(1.0);
            expect(!a.isLoaded() && playing(b, next));
        }
    }
};

//==============================================================================
// Respaldos, restauración y copia de la emisora. Todo ocurre en laboratorios temporales:
// nunca se toca la configuración real del equipo.
class BackupTests : public juce::UnitTest
{
public:
    BackupTests() : juce::UnitTest("Respaldos y carpeta de la emisora", "RAAADIO") {}

    // Una emisora abierta en un laboratorio temporal, que se puede cerrar a mitad de prueba.
    struct Station
    {
        Station()
        {
            PersistenceManager::setLabRootOverride(lab.root);
            for (auto* folder : { "Database", "Library", "Recordings", "Backups" })
                lab.root.getChildFile(folder).createDirectory();
            open();
        }

        void open()
        {
            library = std::make_unique<MusicLibrary>();
            library->initialise(dbFile());
        }

        juce::File dbFile() const { return lab.root.getChildFile("Database/raaadio.db"); }
        juce::File backups() const { return lab.root.getChildFile("Backups"); }

        TrackInfo addTrack(const juce::String& title)
        {
            const auto file = lab.root.getChildFile("Library/GENERAL/" + title + ".ogg");
            file.getParentDirectory().createDirectory();
            file.replaceWithText("audio de " + title);
            library->getDatabase().addTrack(makeTrack(file.getFullPathName(), "ARTISTA", title));
            return library->getTrackByPath(file.getFullPathName());
        }

        juce::StringArray titles() const
        {
            juce::StringArray result;
            for (const auto& t : library->search("", "")) result.add(juce::String(t.title));
            result.sort(true);
            return result;
        }

        PersistenceManager::DatabaseSnapshot snapshot()
        {
            return [this](const juce::File& target) { return library->backupDatabaseTo(target); };
        }

        TempLab lab;
        std::unique_ptr<MusicLibrary> library;
    };

    static juce::StringArray titlesIn(const juce::File& database)
    {
        Database db;
        db.open(database.getFullPathName().toStdString());
        juce::StringArray result;
        for (const auto& t : db.searchTracks("", "")) result.add(juce::String(t.title));
        result.sort(true);
        return result;
    }

    // Daña una base como lo haría un corte de luz a mitad de escritura en un disco sin
    // protección: se conserva la cabecera, el resto de la primera página queda basura.
    static void corrupt(const juce::File& database)
    {
        juce::MemoryBlock data;
        database.loadFileAsData(data);
        for (size_t i = 1024; i + 1024 < data.getSize(); ++i)
            data[i] = (char) 0xAB;
        database.replaceWithData(data.getData(), data.getSize());
    }

    void runTest() override
    {
        beginTest("Un respaldo es la base completa, revisada y con manifiesto; si se altera, no verifica");
        {
            Station station;
            const auto a = station.addTrack("A");
            station.library->withDatabase([&a](Database& db) {
                db.saveTemplate(juce::String::fromUTF8("MAÑANA").toStdString(), "{}");
                Database::AdPlan plan;
                plan.numberByTrack[a.id] = 7;
                return db.saveAdPlan(plan);
            });

            juce::File first, second;
            expect(PersistenceManager::createBackup(station.snapshot(), &first).wasOk());
            expect(first.isAChildOf(station.backups()));
            expect(titlesIn(first.getChildFile("raaadio.db")) == juce::StringArray { "A" },
                   "la copia se abre y tiene lo que había, aunque la base estaba en uso");

            const auto manifest = juce::JSON::parse(first.getChildFile("manifest.json").loadFileAsString());
            expectEquals(manifest["integrity"].toString(), juce::String("ok"));
            expectEquals((int) manifest["schemaVersion"], 3);
            expectEquals(manifest["files"][0]["sha256"].toString(), juce::SHA256(first.getChildFile("raaadio.db")).toHexString());
            expect(PersistenceManager::verifyBackup(first).wasOk());
            {
                Database copy;
                copy.open(first.getChildFile("raaadio.db").getFullPathName().toStdString());
                expectEquals(copy.loadAdPlan().numberByTrack[a.id], 7, "la pauta va dentro de la base");
                expectEquals(juce::String(copy.loadTemplate(juce::String::fromUTF8("MAÑANA").toStdString())), juce::String("{}"),
                             "y las plantillas");
            }

            expect(PersistenceManager::createBackup(station.snapshot(), &second).wasOk());
            expect(second != first, "dos respaldos en el mismo segundo no se pisan");
            expect(PersistenceManager::createBackup({}, nullptr).failed(), "sin copia de la base no hay respaldo");

            // Un respaldo que cambió después de hecho (un disco que falla) ya no sirve.
            first.getChildFile("raaadio.db").appendText("x");
            expect(PersistenceManager::verifyBackup(first).failed());
            expect(PersistenceManager::stageRestore(first).failed(), "no se restaura un respaldo alterado");
            expect(!PersistenceManager::hasPendingRestore());
            expect(PersistenceManager::findLatestVerifiedBackup(station.backups()) == second);
        }

        beginTest("Exportar la emisora a otra carpeta deja una copia verificada; si falla, no queda nada a medias");
        {
            Station station;
            station.addTrack("A");
            TempLab outside;

            juce::File exported;
            expect(PersistenceManager::exportPersistence(outside.root, station.snapshot(), &exported).wasOk());
            expect(exported.isAChildOf(outside.root) && exported.getFileName().startsWith("RAAADIO_Export_"));
            expect(PersistenceManager::verifyBackup(exported).wasOk());
            expect(titlesIn(exported.getChildFile("raaadio.db")) == juce::StringArray { "A" });

            expect(PersistenceManager::exportPersistence(outside.root, {}, nullptr).failed(), "sin copia de la base no se exporta");
            expectEquals(outside.root.getNumberOfChildFiles(juce::File::findDirectories), 1, "y la carpeta a medias se borra");
            expect(PersistenceManager::exportPersistence(outside.root.getChildFile("NO EXISTE"), station.snapshot(), nullptr).failed());
        }

        beginTest("Retención escalonada: todos los días de dos semanas, un respaldo por semana y uno por mes");
        {
            const juce::Time now(2026, 8, 23, 12, 0, 0, 0, true);
            auto dayAt = [&now](int daysAgo) {
                const auto d = now - juce::RelativeTime::days(daysAgo);
                return juce::Time(d.getYear(), d.getMonth(), d.getDayOfMonth(), 3, 0, 0, 0, true);
            };
            std::vector<juce::Time> times;
            times.reserve(400);
            for (int d = 0; d < 400; ++d) times.push_back(dayAt(d));  // uno por día, del más reciente al más viejo
            std::set<size_t> pruned;
            for (auto i : PersistenceManager::backupsToPrune(times, now)) pruned.insert(i);
            auto kept = [&pruned](int d) { return pruned.count((size_t) d) == 0; };
            auto lastOfMonth = [&](int d) { return d == 0 || times[(size_t) d].getMonth() != times[(size_t) d - 1].getMonth(); };
            auto sunday = [&](int d) { return times[(size_t) d].getDayOfWeek() == 0; };

            bool days = true, weeks = true, months = true, old = true;
            for (int d = 0; d < 14; ++d) days = days && kept(d);
            for (int d = 20; d < 50; ++d) weeks = weeks && (kept(d) == (sunday(d) || lastOfMonth(d)));
            for (int d = 70; d < 360; ++d) months = months && (kept(d) == lastOfMonth(d));
            for (int d = 380; d < 400; ++d) old = old && !kept(d);
            expect(days, "las dos últimas semanas, completas");
            expect(weeks, "hasta dos meses atrás, el último de cada semana (y de cada mes)");
            expect(months, "hasta un año atrás, el último de cada mes");
            expect(old, "más de un año: se borran");
            expect(times.size() - pruned.size() < 45);

            // En disco: carpetas vacías fuera, lo que no es un respaldo se deja.
            Station station;
            auto make = [&station](juce::Time t) {
                const auto folder = station.backups().getChildFile(t.formatted("%Y/%m/%d/%H_%M_%S"));
                folder.getChildFile("raaadio.db").create();
                return folder;
            };
            const auto recent = make(dayAt(1));
            for (int d = 2; d <= 12; ++d) make(dayAt(d));  // más de los 10 que siempre se guardan
            const auto veryOld = make(dayAt(390));
            const auto other = station.backups().getChildFile("copia manual");
            other.getChildFile("raaadio.db").create();
            const auto partial = station.backups().getChildFile(dayAt(2).formatted("%Y/%m/%d/%H_%M_%S") + ".partial");
            partial.createDirectory();
            PersistenceManager::pruneBackups(station.backups(), now);
            expect(recent.isDirectory() && !veryOld.exists() && !veryOld.getParentDirectory().exists(),
                   "sin dejar carpetas de día vacías");
            expect(other.isDirectory() && partial.isDirectory(), "lo que no es un respaldo de RAAADIO no se toca");
            expect(PersistenceManager::getLatestBackupTime(station.backups()) == dayAt(1));
        }

        beginTest("Destino externo: el respaldo llega completo y verificado; la música solo suma lo nuevo y nunca borra");
        {
            Station station;
            station.addTrack("A");
            station.addTrack("B");
            TempLab external;
            juce::File backup;
            expect(PersistenceManager::createBackup(station.snapshot(), &backup).wasOk());

            const auto externalStation = PersistenceManager::getExternalStationFolder(external.root, "1a2b3c4d");
            expectEquals(externalStation.getFileName(), juce::String("RAAADIO-1a2b3c4d"));
            const auto externalBackups = externalStation.getChildFile("Backups");
            expect(PersistenceManager::copyBackupTo(backup, station.backups(), externalBackups).wasOk());
            const auto copy = externalBackups.getChildFile(backup.getRelativePathFrom(station.backups()));
            expect(PersistenceManager::verifyBackup(copy).wasOk(), "la copia externa verifica");
            expect(!copy.getSiblingFile(copy.getFileName() + ".partial").exists());
            expect(PersistenceManager::copyBackupTo(backup, station.backups(), externalBackups).wasOk(), "si ya está, no se repite");

            const auto music = station.lab.root.getChildFile("Library");
            const auto mirror = externalStation.getChildFile("Library");
            auto realHash = [](const juce::File& f) { return juce::SHA256(f).toHexString(); };
            auto first = PersistenceManager::mirrorFolder(music, mirror, realHash, {});
            expect(first.copied == 2 && first.failed == 0, juce::String(first.copied) + " / " + juce::String(first.failed));
            auto second = PersistenceManager::mirrorFolder(music, mirror, realHash, {});
            expect(second.copied == 0 && second.unchanged == 2, "lo que ya está no se copia de nuevo");

            station.addTrack("C");
            music.getChildFile("GENERAL/A.ogg").deleteFile();
            auto third = PersistenceManager::mirrorFolder(music, mirror, realHash, {});
            expectEquals(third.copied, 1);
            expect(mirror.getChildFile("GENERAL/A.ogg").existsAsFile(), "lo borrado en la emisora sigue en la copia");

            // Una copia que no coincide con el hash de la base no se da por buena.
            music.getChildFile("GENERAL/D.ogg").replaceWithText("audio de D");
            auto bad = PersistenceManager::mirrorFolder(music, mirror, [](const juce::File& f) {
                return f.getFileName() == "D.ogg" ? juce::String("0000") : juce::String();
            }, {});
            expect(bad.failed == 1 && bad.failures.contains("GENERAL" + juce::File::getSeparatorString() + "D.ogg"), bad.failures.joinIntoString(","));
            expect(!mirror.getChildFile("GENERAL/D.ogg").exists() && !mirror.getChildFile("GENERAL/D.ogg.partial").exists());

            auto cancelled = PersistenceManager::mirrorFolder(music, mirror, {}, [] { return false; });
            expect(cancelled.cancelled);
        }

        beginTest("Una base dañada se reemplaza al abrir por el último respaldo verificado; la dañada se guarda aparte");
        {
            Station station;
            station.addTrack("BUENA");
            expect(PersistenceManager::createBackup(station.snapshot()).wasOk());
            station.library = nullptr;

            expect(!PersistenceManager::checkDatabase(station.dbFile()).damaged, "una base sana no se toca");
            corrupt(station.dbFile());
            const auto health = PersistenceManager::checkDatabase(station.dbFile());
            expect(health.damaged && health.recovered, health.problem);
            expect(health.damagedCopy.existsAsFile() && health.damagedCopy.getFileName().startsWith("raaadio.db.damaged-"));
            station.open();
            expect(station.titles() == juce::StringArray { "BUENA" });

            Station unprotected;  // sin respaldos
            unprotected.addTrack("UNICA");
            unprotected.library = nullptr;
            corrupt(unprotected.dbFile());
            const auto alone = PersistenceManager::checkDatabase(unprotected.dbFile());
            expect(alone.damaged && !alone.recovered, "sin respaldo verificado se deja como está y se avisa");
            expect(unprotected.dbFile().existsAsFile());

            // Si el único respaldo es de una versión anterior, la pauta que trae en archivos
            // también vuelve (la base del respaldo la importa al abrirse).
            Station legacy;
            const auto spot = legacy.addTrack("CUNA");
            const auto oldBackup = legacy.backups().getChildFile("2026/09/20/10_00_00");
            oldBackup.createDirectory();
            expect(legacy.library->backupDatabaseTo(oldBackup.getChildFile("raaadio.db")));
            sqlite3* raw = nullptr;
            sqlite3_open(oldBackup.getChildFile("raaadio.db").getFullPathName().toRawUTF8(), &raw);
            sqlite3_exec(raw, "PRAGMA user_version = 1;", nullptr, nullptr, nullptr);
            sqlite3_close(raw);
            oldBackup.getChildFile("Config").createDirectory();
            oldBackup.getChildFile("Config/ads_data.json").replaceWithText(
                "{ \"assignments\": [ { \"tid\": " + juce::String(spot.id) + ", \"num\": 6 } ] }");
            legacy.library = nullptr;
            corrupt(legacy.dbFile());
            expect(PersistenceManager::checkDatabase(legacy.dbFile()).recovered);
            legacy.open();
            const auto recoveredPlan = legacy.library->getDatabase().loadAdPlan();
            expect(recoveredPlan.numberByTrack.count(spot.id) == 1 && recoveredPlan.numberByTrack.at(spot.id) == 6,
                   "la pauta del respaldo anterior también se recupera");
        }

        beginTest("Restaurar trae la base del respaldo; la cola y las tandas ya insertadas quedan como estaban");
        {
            Station station;
            const auto before = station.addTrack("ANTES");
            station.library->withDatabase([&before](Database& db) {
                Database::AdPlan plan;
                plan.numberByTrack[before.id] = 5;
                return db.saveAdPlan(plan) && db.saveQueue({ { before.id, {} } }) && db.saveAdBreaksDone({ "2026-09-23 12:00" });
            });
            juce::File backup;
            expect(PersistenceManager::createBackup(station.snapshot(), &backup).wasOk());

            // Después del respaldo la emisora siguió.
            const auto after = station.addTrack("DESPUES");
            station.library->withDatabase([&before, &after](Database& db) {
                Database::AdPlan plan;
                plan.numberByTrack[before.id] = 9;
                return db.saveAdPlan(plan) && db.saveQueue({ { before.id, {} }, { after.id, {} } })
                    && db.saveAdBreaksDone({ "2026-09-23 12:00", "2026-09-23 13:00" });
            });

            expect(PersistenceManager::stageRestore(backup).wasOk());
            expect(PersistenceManager::hasPendingRestore());
            expect(station.titles() == juce::StringArray { "ANTES", "DESPUES" }, "la emisora en marcha no cambia");
            station.library = nullptr;

            juce::File previous;
            expect(PersistenceManager::applyPendingRestore(&previous).wasOk());
            expect(!PersistenceManager::hasPendingRestore());
            expect(titlesIn(previous) == juce::StringArray { "ANTES", "DESPUES" }, "lo que había quedó respaldado");

            station.open();
            station.library->withDatabase([&previous](Database& db) { return db.carryOverLiveState(previous); });
            expect(station.titles() == juce::StringArray { "ANTES" });
            auto& db = station.library->getDatabase();
            expectEquals(db.loadAdPlan().numberByTrack[before.id], 5, "la pauta es la del respaldo");
            const auto queue = db.loadQueue();
            expect(queue.size() == 1 && queue[0].trackId == before.id, "la cola es la del momento, sin pistas que no existen");
            expect(db.loadAdBreaksDone().count("2026-09-23 13:00") == 1, "las tandas ya insertadas no se repiten");
        }

        beginTest("Los respaldos de versiones anteriores (con la pauta en un archivo) se restauran e importan");
        {
            Station station;
            TempLab outside;
            expect(PersistenceManager::stageRestore(outside.root).failed(), "una carpeta sin raaadio.db");
            outside.root.getChildFile("raaadio.db").replaceWithText("no es una base de datos");
            expect(PersistenceManager::stageRestore(outside.root).failed(), "un archivo que no es SQLite");
            expect(!PersistenceManager::hasPendingRestore());

            // Formato anterior: base del esquema 1 y ads_data.json junto a ella, sin manifiesto.
            const auto old = station.addTrack("VIEJA");
            outside.root.getChildFile("raaadio.db").deleteFile();
            expect(station.library->backupDatabaseTo(outside.root.getChildFile("raaadio.db")));
            sqlite3* raw = nullptr;
            sqlite3_open(outside.root.getChildFile("raaadio.db").getFullPathName().toRawUTF8(), &raw);
            sqlite3_exec(raw, "PRAGMA user_version = 1;", nullptr, nullptr, nullptr);
            sqlite3_close(raw);
            outside.root.getChildFile("ads_data.json").replaceWithText(
                "{ \"assignments\": [ { \"tid\": " + juce::String(old.id) + ", \"num\": 4 } ], "
                "\"schedules\": [ { \"num\": 4, \"times\": [ \"08:00\" ] } ], \"autoSchedule\": true }");

            expect(PersistenceManager::stageRestore(outside.root).wasOk());
            station.library = nullptr;
            expect(PersistenceManager::applyPendingRestore().wasOk());
            station.open();

            const auto plan = station.library->getDatabase().loadAdPlan();
            expectEquals(plan.numberByTrack.count(old.id) ? plan.numberByTrack.at(old.id) : -1, 4);
            expect(plan.autoSchedule && plan.slotsByNumber.count(4) && plan.slotsByNumber.at(4) == std::vector<std::string> { "08:00" });
            expect(station.lab.root.getChildFile("Config/ads_data.json.migrated").existsAsFile()
                   && !station.lab.root.getChildFile("Config/ads_data.json").exists(), "el archivo queda como constancia");
        }

        beginTest("Mover la emisora copia todo a la carpeta nueva; si no está vacía o se cancela, no se usa");
        {
            Station station;
            station.addTrack("A");
            station.lab.touch("Recordings/2026/09/23/programa.wav");
            station.lab.touch("Backups/2026/09/22/08_00_00/raaadio.db");
            station.lab.touch("Logs/raaadio.log");
            station.lab.touch("raaadio.lock");

            TempLab destination;
            const auto target = PersistenceManager::labFolderFor(destination.root);
            juce::StringArray skipped;
            double lastProgress = 0.0;
            expect(PersistenceManager::copyLabTo(target, station.snapshot(), [&lastProgress](double p) { lastProgress = p; return true; },
                                                 skipped).wasOk());
            expect(PersistenceManager::containsStation(target));
            expect(titlesIn(target.getChildFile("Database/raaadio.db")) == juce::StringArray { "A" });
            expect(target.getChildFile("Library/GENERAL/A.ogg").existsAsFile() && target.getChildFile("Recordings/2026/09/23/programa.wav").existsAsFile()
                   && target.getChildFile("Backups/2026/09/22/08_00_00/raaadio.db").existsAsFile(),
                   "música, grabaciones y respaldos");
            expect(!target.getChildFile("Logs/raaadio.log").exists(), "el registro de la app no hace falta");
            expect(!target.getChildFile("raaadio.lock").exists(), "el candado es de este equipo");
            expect(skipped.isEmpty());
            expectWithinAbsoluteError(lastProgress, 1.0, 1.0e-9);

            expect(PersistenceManager::copyLabTo(target, station.snapshot(), {}, skipped).failed(), "ya no está vacía");
            expect(PersistenceManager::copyLabTo(station.lab.root.getChildFile("Library/raaadio_lab"), station.snapshot(), {}, skipped).failed(),
                   "dentro de la emisora actual");

            const auto cancelled = destination.root.getChildFile("otra/raaadio_lab");
            expect(PersistenceManager::copyLabTo(cancelled, station.snapshot(), [](double) { return false; }, skipped).failed());
            expect(!cancelled.exists(), "al cancelar se borra lo copiado");
        }
    }
};

//==============================================================================
class DeadAirTests : public juce::UnitTest
{
public:
    DeadAirTests() : juce::UnitTest("Protección contra silencio", "RAAADIO") {}

    void runTest() override
    {
        using Action = DeadAirGuard::Action;
        DeadAirGuard::Situation base;
        base.everOnAir = true;
        base.silenceSeconds = 25.0;
        base.threshold = 20.0;
        base.queueEmpty = true;

        beginTest("Qué hace según la situación");
        {
            auto s = base;
            expect(DeadAirGuard::decide(s) == Action::RefillAndResume, "nada suena y la cola está vacía");
            s.queueEmpty = false;
            expect(DeadAirGuard::decide(s) == Action::Resume, "nada suena pero hay cola");

            s.deckPlaying[1] = true;
            s.deckSilenceSeconds[1] = 25.0;
            expect(DeadAirGuard::decide(s) == Action::SkipDeckB, "el deck suena pero su pista es silencio");
            s.deckSilenceSeconds[1] = 0.0;
            expect(DeadAirGuard::decide(s) == Action::Alarm, "suena pero no llega al aire (fader cerrado): solo aviso");

            s = base;
            s.autoMode = false;
            expect(DeadAirGuard::decide(s) == Action::Alarm, "en manual solo avisa");
            s = base;
            s.talkover = true;
            expect(DeadAirGuard::decide(s) == Action::Alarm, "con la locución abierta solo avisa");

            s = base;
            s.silenceSeconds = 10.0;
            expect(DeadAirGuard::decide(s) == Action::None, "antes del umbral no hace nada");
            s = base;
            s.everOnAir = false;
            expect(DeadAirGuard::decide(s) == Action::None, "recién abierta la emisora no hace nada");
            s = base;
            s.enabled = false;
            expect(DeadAirGuard::decide(s) == Action::None, "desactivada no hace nada");
        }

        LibraryFixture fx;
        const auto file = writeWav(fx.lab.root.getChildFile("Library/GENERAL/TONO.wav"), 20.0, toneBetween({ { 0.0, 20.0 } }));
        fx.library.processAndAddTrack(file);
        const auto track = fx.library.search("", "").front();

        beginTest("El mezclador mide cuánto lleva en silencio la salida y cada deck");
        {
            AudioEngine engine(fx.library, false);
            auto& mixer = engine.getMixer();
            mixer.prepareToPlay(512, 44100.0);
            pump(mixer, 2.0);
            expectWithinAbsoluteError(mixer.getMainSilenceSeconds(), 2.0, 0.05, "nada suena");

            auto& deck = engine.getDeck(0);
            deck.loadTrack(juce::File(track.path));
            deck.play();
            pump(mixer, 0.5, { &deck });
            expect(mixer.getMainSilenceSeconds() < 0.05, "con música el contador vuelve a cero");

            mixer.setChannelVolume(0, -60.0f);  // fader cerrado
            pump(mixer, 1.0, { &deck });
            expect(mixer.getMainSilenceSeconds() > 0.9, "la salida queda en silencio");
            expect(mixer.getDeckSilenceSeconds(0) < 0.05, "pero el deck sí entrega sonido: no es culpa de la pista");
        }

        beginTest("AutoDJ reanuda la cola cuando nada suena");
        {
            AudioEngine engine(fx.library, false);
            engine.getMixer().prepareToPlay(512, 44100.0);
            Playlist playlist;
            AutoDJ autoDJ(engine, playlist);
            playlist.insertTracks({ track }, 0);
            engine.getDeck(0).setRouting(false, true);  // lo dejaron en preescucha

            expect(autoDJ.resumePlayback());
            expect(engine.getDeck(0).isPlaying() && engine.getDeck(0).isRoutedToMain(), "sale al aire");
            expect(playlist.isEmpty());
        }
    }
};

//==============================================================================
class AirLogTests : public juce::UnitTest
{
public:
    AirLogTests() : juce::UnitTest("Registro de lo emitido", "RAAADIO") {}

    void runTest() override
    {
        beginTest("Una grabación queda en un WAV de su carpeta del día, con todo lo que entró mientras grababa");
        {
            TempLab lab;
            PersistenceManager::setLabRootOverride(lab.root);  // el grabador crea su carpeta al construirse
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            RecordingManager recorder;
            recorder.initialise(formats);
            recorder.setRecordingsFolder(lab.root.getChildFile("Grabaciones"));
            recorder.prepare(44100.0);

            expect(recorder.startRecording(true, false).isEmpty());
            expect(recorder.getState() == RecordingManager::Recording);
            juce::AudioBuffer<float> block(2, 4410);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block.getNumSamples(); ++i)
                    block.setSample(ch, i, 0.25f * std::sin(juce::MathConstants<float>::twoPi * 440.0f * (float) i / 44100.0f));
            for (int n = 0; n < 10; ++n) recorder.pushBlock(block, 0, block.getNumSamples());  // 1 s
            expectEquals(recorder.getDurationString(), juce::String("00:00:01"));
            recorder.stopRecording();
            expect(recorder.getState() == RecordingManager::Idle);

            const auto file = recorder.getLastRecording();
            expect(file.existsAsFile() && file.isAChildOf(lab.root.getChildFile("Grabaciones")));
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
            expect(reader != nullptr);
            if (reader != nullptr) expectEquals((int) reader->lengthInSamples, 44100, "no se pierde ninguna muestra");
        }

        beginTest("Un archivo por hora, sin cortar el audio al cambiar de hora");
        {
            TempLab lab;
            std::atomic<juce::int64> fakeNow { juce::Time(2026, 8, 22, 10, 59, 58, 0, true).toMilliseconds() };
            AirLogRecorder recorder(lab.root);
            recorder.setClock([&fakeNow] { return juce::Time(fakeNow.load()); });
            recorder.prepare(44100.0);
            recorder.setEnabled(true);

            juce::AudioBuffer<float> block(2, 441);  // 10 ms
            for (int i = 0; i < 441; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    block.setSample(ch, i, tone(i / 44100.0, 0.3f, 1000.0));

            auto feed = [&](double seconds) {
                for (int i = 0; i < (int) (seconds * 100.0); ++i) {
                    recorder.pushBlock(block, 0, 441);
                    if (i % 10 == 9) juce::Thread::sleep(2);
                }
                juce::Thread::sleep(300);  // que el hilo escriba lo encolado
            };
            feed(2.0);
            fakeNow = juce::Time(2026, 8, 22, 11, 0, 1, 0, true).toMilliseconds();
            feed(3.0);
            recorder.setEnabled(false);

            const auto first = AirLogRecorder::fileForSegment(lab.root, juce::Time(2026, 8, 22, 10, 59, 0, 0, true));
            const auto second = AirLogRecorder::fileForSegment(lab.root, juce::Time(2026, 8, 22, 11, 0, 0, 0, true));
            expectEquals(second.getRelativePathFrom(lab.root).replaceCharacter('\\', '/'), juce::String("2026/09/22/2026-09-22_11-00.ogg"));
            expect(first.existsAsFile() && second.existsAsFile(), "el tramo que empezó a las 10:59 y el de las 11:00");

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            double total = 0.0;
            for (const auto& f : { first, second }) {
                std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(f));
                expect(reader != nullptr && reader->numChannels == 1, "Ogg mono legible: " + f.getFileName());
                if (reader != nullptr) total += (double) reader->lengthInSamples / reader->sampleRate;
            }
            expectWithinAbsoluteError(total, 5.0, 0.1, "no se perdió audio en el cambio de hora");
        }

        beginTest("Los registros más viejos que la retención se borran");
        {
            TempLab lab;
            const auto now = juce::Time(2026, 8, 22, 12, 0, 0, 0, true);
            const auto old = AirLogRecorder::fileForSegment(lab.root, now - juce::RelativeTime::days(40));
            const auto recent = AirLogRecorder::fileForSegment(lab.root, now - juce::RelativeTime::days(2));
            for (const auto& f : { old, recent }) {
                f.getParentDirectory().createDirectory();
                f.replaceWithText("x");
            }
            old.setLastModificationTime(now - juce::RelativeTime::days(40));
            recent.setLastModificationTime(now - juce::RelativeTime::days(2));

            expectEquals(AirLogRecorder::deleteOlderThan(lab.root, 30, now), 1);
            expect(!old.exists() && recent.existsAsFile());
            expect(!old.getParentDirectory().exists(), "la carpeta del día vacía también se borra");
        }
    }
};

//==============================================================================
class CartwallTests : public juce::UnitTest
{
public:
    CartwallTests() : juce::UnitTest("Botonera", "RAAADIO") {}

    void runTest() override
    {
        LibraryFixture fx;
        const auto file = writeWav(fx.lab.root.getChildFile("Library/IDS/CORTINILLA.wav"), 6.0, toneBetween({ { 0.5, 5.0 } }));
        fx.library.processAndAddTrack(file);
        const auto jingle = fx.library.search("", "").front();

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        beginTest("Un cartucho suena directo al aire, por encima de los decks");
        {
            AudioEngine engine(fx.library, false);
            CartPlayer carts(fx.library, formats);
            auto& mixer = engine.getMixer();
            mixer.setCartSource(&carts);
            mixer.prepareToPlay(512, 44100.0);

            expect(carts.assign(3, jingle));
            expectWithinAbsoluteError(carts.getRemainingSeconds(3), jingle.cueOut - jingle.cueIn, 0.01,
                                      "dura de su punto de entrada al de salida");

            TrackInfo played {};
            carts.onCartPlayed = [&played](const TrackInfo& t) { played = t; };
            mixer.setChannelVolume(0, -60.0f);
            mixer.setChannelVolume(1, -60.0f);  // aunque los faders estén cerrados
            carts.toggle(3);
            expect(carts.isPlaying(3));
            expectEquals(played.id, jingle.id, "queda en el historial");

            pump(mixer, 1.0, {}, &carts);
            expect(mixer.getMainSilenceSeconds() < 0.05, "se oye en la salida al aire");

            carts.toggle(3);
            expect(!carts.isPlaying(3), "un segundo clic lo detiene");
            mixer.setCartSource(nullptr);
        }

        beginTest("Se asigna desde la biblioteca, la lista de cuñas o la cola");
        {
            expectEquals(DragFormats::firstPath("track_list_D:\\m\\a.mp3|D:\\m\\b.mp3"), juce::String("D:\\m\\a.mp3"));
            expectEquals(DragFormats::firstPath("track_path_D:\\m\\a.mp3"), juce::String("D:\\m\\a.mp3"));
            expectEquals(DragFormats::firstPath("playlist_drag_3:D:\\m\\a.mp3|4:D:\\m\\b.mp3"), juce::String("D:\\m\\a.mp3"),
                         "desde la cola, la ruta con letra de unidad queda entera");
            expect(DragFormats::firstPath("categoria_5").isEmpty(), "otros arrastres no sirven");

            // Una cortinilla de una versión anterior, todavía sin analizar.
            const auto legacyFile = writeWav(fx.lab.root.getChildFile("Library/IDS/VIEJA.wav"), 4.0, toneBetween({ { 1.0, 4.0 } }));
            fx.library.getDatabase().addTrack(makeTrack(legacyFile.getFullPathName(), "RAAADIO", "VIEJA"));
            const auto legacy = fx.library.getTrackByPath(legacyFile.getFullPathName());

            CartPlayer carts(fx.library, formats, false);  // de prueba: no pisa la botonera guardada
            expect(carts.assignFromLibrary(0, legacy));
            expectWithinAbsoluteError(carts.getRemainingSeconds(0), 3.04, 0.05, "se analiza al asignarla: arranca en su punto de entrada");
            expectEquals(fx.library.getTrackByPath(legacyFile.getFullPathName()).analysisVersion, TrackAnalyzer::version);
        }

        beginTest("La botonera se recuerda al reiniciar");
        {
            CartPlayer restored(fx.library, formats);
            expect(restored.isAssigned(3));
            expectEquals(restored.getTrack(3).id, jingle.id);
            expect(!restored.isAssigned(0));

            restored.clear(3);
            CartPlayer cleared(fx.library, formats);
            expect(!cleared.isAssigned(3), "quitar también se guarda");
        }
    }
};

//==============================================================================
// Cierra solo el lado que envía (FIN) y deja leer, como un servidor que corta a un emisor
// pero sigue tragando datos unos segundos: los envíos no fallan y solo la lectura lo nota.
// JUCE no lo ofrece; en Windows se declara aquí para no traer <winsock2.h> a este archivo.
#if JUCE_WINDOWS
extern "C" __declspec(dllimport) int __stdcall shutdown(juce::pointer_sized_uint socket, int how);
static void closeSendingSide(juce::StreamingSocket& s) { shutdown((juce::pointer_sized_uint) s.getRawSocketHandle(), 1 /* SD_SEND */); }
#else
#include <sys/socket.h>
static void closeSendingSide(juce::StreamingSocket& s) { ::shutdown(s.getRawSocketHandle(), SHUT_WR); }
#endif

class StreamingTests : public juce::UnitTest
{
public:
    StreamingTests() : juce::UnitTest("Transmisión Icecast", "RAAADIO") {}

    // Servidor Icecast de mentira en 127.0.0.1. Una conexión PUT o SOURCE recibe la respuesta
    // (200 o 401; 405 a PUT si rejectPut) y, si se aceptó, se guarda lo que llega hasta que
    // el emisor cierra (o se agota el tiempo): no corta por su cuenta, porque el emisor nota
    // enseguida un cierre y pasaría a error. Con cutAfterMs, a ese tiempo manda kickMessage
    // (si hay) y corta como un servidor que echa al emisor (ver closeSendingSide). Las
    // peticiones /admin/metadata se anotan y se responden como Icecast.
    struct FakeIcecast {
        explicit FakeIcecast(bool accept, bool rejectPutRequests = false, int cutAfter = 0, juce::String kick = {})
            : accepting(accept), rejectPut(rejectPutRequests), cutAfterMs(cutAfter), kickMessage(std::move(kick)) {
            listening = server.createListener(0, "127.0.0.1");
            thread = std::thread([this] { acceptLoop(); });
        }
        ~FakeIcecast() {
            server.close();
            if (thread.joinable()) thread.join();
            for (auto& t : sources) t.join();
        }
        int port() const { return server.getBoundPort(); }

        static juce::String readHeaders(juce::StreamingSocket& client) {
            juce::String headers;
            char c = 0;
            while (!headers.endsWith("\r\n\r\n") && headers.length() < 8192
                   && client.waitUntilReady(true, 5000) == 1 && client.read(&c, 1, false) == 1)
                headers += juce::String::charToString((juce::juce_wchar) (juce::uint8) c);
            return headers;
        }

        void acceptLoop() {
            for (;;) {
                std::shared_ptr<juce::StreamingSocket> client(server.waitForNextConnection());
                if (client == nullptr) return;
                const auto headers = readHeaders(*client);
                if (headers.startsWith("GET /admin/metadata")) {
                    {
                        const juce::ScopedLock sl(lock);
                        adminRequests.add(headers);
                        adminTimes.add(juce::Time::getMillisecondCounter());
                    }
                    const char* reply = "HTTP/1.0 200 OK\r\nContent-Type: text/xml\r\n\r\n"
                                        "<?xml version=\"1.0\"?>\n<iceresponse><message>Metadata update successful</message>"
                                        "<return>1</return></iceresponse>\n";
                    client->write(reply, (int) strlen(reply));
                    continue;
                }
                {
                    const juce::ScopedLock sl(lock);
                    requests.add(headers);
                }
                sources.emplace_back([this, client, headers] { serveSource(*client, headers); });
            }
        }

        void serveSource(juce::StreamingSocket& client, const juce::String& headers) {
            const bool ok = accepting && !(rejectPut && headers.startsWith("PUT "));
            const char* reply = ok ? "HTTP/1.0 200 OK\r\n\r\n"
                                   : accepting ? "HTTP/1.0 405 Method Not Allowed\r\n\r\n" : "HTTP/1.0 401 Unauthorized\r\n\r\n";
            client.write(reply, (int) strlen(reply));
            if (!ok) return;

            const auto start = juce::Time::getMillisecondCounter();
            const auto deadline = start + 8000;
            bool cut = false;
            char buffer[8192];
            while (juce::Time::getMillisecondCounter() < deadline) {
                if (cutAfterMs > 0 && !cut && juce::Time::getMillisecondCounter() - start >= (juce::uint32) cutAfterMs) {
                    if (kickMessage.isNotEmpty()) client.write(kickMessage.toRawUTF8(), (int) kickMessage.getNumBytesAsUTF8());
                    closeSendingSide(client);
                    cut = true;
                }
                if (client.waitUntilReady(true, 200) != 1) continue;
                const int n = client.read(buffer, (int) sizeof(buffer), false);
                if (n <= 0) break;
                {
                    const juce::ScopedLock sl(lock);
                    received.append(buffer, (size_t) n);
                }
            }
        }
        // Busca los bytes UTF-8 de text dentro de lo recibido (datos binarios, con ceros).
        bool contains(const juce::String& text) {
            const juce::ScopedLock sl(lock);
            const char* utf8 = text.toRawUTF8();
            const char* begin = static_cast<const char*>(received.getData());
            const char* end = begin + received.getSize();
            return std::search(begin, end, utf8, utf8 + strlen(utf8)) != end;
        }
        size_t size() { const juce::ScopedLock sl(lock); return received.getSize(); }
        juce::String sourceRequest() { const juce::ScopedLock sl(lock); return requests[0]; }
        juce::StringArray sourceRequests() { const juce::ScopedLock sl(lock); return requests; }

        // Hora (contador de milisegundos) de la primera petición de título que contiene text; 0 si no llegó.
        juce::uint32 adminRequestTime(const juce::String& text) {
            const juce::ScopedLock sl(lock);
            for (int i = 0; i < adminRequests.size(); ++i)
                if (adminRequests[i].contains(text)) return adminTimes[i];
            return 0;
        }
        juce::String firstAdminRequest() { const juce::ScopedLock sl(lock); return adminRequests[0]; }

        // Cuadros MP3: la sincronía de 11 bits (0xFFE) en dos bytes seguidos.
        int mp3FrameSyncs() {
            const juce::ScopedLock sl(lock);
            const auto* bytes = static_cast<const juce::uint8*>(received.getData());
            int count = 0;
            for (size_t i = 0; i + 1 < received.getSize(); ++i)
                if (bytes[i] == 0xFF && (bytes[i + 1] & 0xE0) == 0xE0) ++count;
            return count;
        }

        bool accepting, rejectPut;
        int cutAfterMs;
        juce::String kickMessage;
        juce::StreamingSocket server;
        bool listening = false;
        std::thread thread;
        std::vector<std::thread> sources;
        juce::CriticalSection lock;
        juce::StringArray requests;  // las conexiones de audio, en orden
        juce::StringArray adminRequests;
        juce::Array<juce::uint32> adminTimes;
        juce::MemoryBlock received;
    };

    void runTest() override
    {
        StreamEncoder::Server server;
        server.host = "127.0.0.1";
        server.mount = "prueba";
        server.user = "source";
        server.password = "secreto";
        server.name = "Radio Prueba";

        beginTest("La petición lleva el punto de montaje, la clave, el formato y los datos de la emisora");
        {
            auto s = server;
            s.format = StreamEncoder::Format::Mp3;
            s.bitrate = 192;
            s.genre = "Tropical";
            s.website = "https://radio.example.org";
            s.name = "Radio\r\nX-Inyectada: 1";
            const auto request = StreamEncoder::buildSourceRequest(s, 48000.0);
            expect(request.startsWith("PUT /prueba HTTP/1.1\r\n"), request);
            expect(StreamEncoder::buildSourceRequest(s, 48000.0, StreamEncoder::Method::Source).startsWith("SOURCE /prueba HTTP/1.0\r\n"),
                   "para los servidores anteriores a Icecast 2.4");
            expect(request.contains("Host: 127.0.0.1:8000\r\n"), request);
            expect(request.contains("Authorization: Basic " + juce::Base64::toBase64("source:secreto") + "\r\n"));
            expect(request.contains("Content-Type: audio/mpeg\r\n") && request.endsWith("\r\n\r\n"));
            expect(request.contains("ice-genre: Tropical\r\n") && request.contains("ice-url: https://radio.example.org\r\n"));
            expect(request.contains("ice-public: 0\r\n") && !request.contains("ice-description"), "lo vacío no se manda");
            expect(request.contains("ice-audio-info: ice-samplerate=48000;ice-bitrate=192;ice-channels=2\r\n"), request);
            expect(!request.contains("\r\nX-Inyectada"), "un salto de línea en el nombre no crea otra cabecera");

            s.format = StreamEncoder::Format::OggVorbis;
            s.isPublic = true;
            const auto ogg = StreamEncoder::buildSourceRequest(s);
            expect(ogg.contains("Content-Type: application/ogg\r\n") && ogg.contains("ice-public: 1\r\n"));

            expect(StreamEncoder::isAccepted("HTTP/1.0 200 OK") && StreamEncoder::isAccepted("ICY 200 OK"));
            expect(!StreamEncoder::isAccepted("HTTP/1.0 401 Unauthorized") && !StreamEncoder::isAccepted(""));
        }

        beginTest("El título en MP3 va por /admin/metadata, en UTF-8 y con los caracteres escapados");
        {
            const auto song = StreamEncoder::songText("ARTISTA", juce::String::fromUTF8("CANCIÓN & más"));
            expectEquals(song, juce::String::fromUTF8("ARTISTA - CANCIÓN & más"));
            expectEquals(StreamEncoder::songText("", "Radio Prueba"), juce::String("Radio Prueba"), "sin artista, solo el título");
            expectEquals(StreamEncoder::songText("ARTISTA", " "), juce::String("ARTISTA"));

            const auto request = StreamEncoder::buildMetadataRequest(server, song);
            expect(request.startsWith("GET /admin/metadata?mount=%2Fprueba&mode=updinfo&song=ARTISTA%20-%20CANCI%C3%93N%20%26%20m%C3%A1s"
                                      "&charset=UTF-8 HTTP/1.0\r\n"), request);
            expect(request.contains("Authorization: Basic " + juce::Base64::toBase64("source:secreto") + "\r\n"));
        }

        beginTest("MP3 con LAME: se codifica y se vuelve a leer igual");
        {
            expect(Mp3Encoder::isAvailable(), "falta libmp3lame.dll junto a las pruebas");
            expect(Mp3Encoder::libraryVersion().startsWith("3."), Mp3Encoder::libraryVersion());
            expectEquals(Mp3Encoder::outputRateFor(48000.0), 48000);
            expectEquals(Mp3Encoder::outputRateFor(96000.0), 44100, "una tasa que MP3 no admite pasa a 44,1 kHz");

            Mp3Encoder encoder;
            expect(encoder.open(44100.0, 128));
            std::vector<float> left(44100 * 2), right(44100 * 2);
            for (size_t i = 0; i < left.size(); ++i)
                left[i] = right[i] = tone(i / 44100.0, 0.5f, 1000.0);
            juce::MemoryBlock mp3;
            for (size_t done = 0; done < left.size(); done += 1000)
                expect(encoder.encode(left.data() + done, right.data() + done, (int) juce::jmin<size_t>(1000, left.size() - done), mp3));
            encoder.flush(mp3);

            // 2 s a 128 kbps: unos 32 000 bytes.
            expect(mp3.getSize() > 28000 && mp3.getSize() < 36000, juce::String((int) mp3.getSize()) + " bytes");
            const auto* bytes = static_cast<const juce::uint8*>(mp3.getData());
            expect(mp3.getSize() > 2 && bytes[0] == 0xFF && (bytes[1] & 0xE0) == 0xE0, "empieza con un cuadro MP3, sin etiqueta");

            juce::MP3AudioFormat format;
            std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(new juce::MemoryInputStream(mp3, false), true));
            expect(reader != nullptr);
            if (reader != nullptr) {
                expectEquals((int) reader->sampleRate, 44100);
                expect(reader->lengthInSamples >= 88200 && reader->lengthInSamples < 88200 + 4000,
                       juce::String(reader->lengthInSamples));
                juce::AudioBuffer<float> decoded(2, 44100);
                reader->read(&decoded, 0, 44100, 22050, true, true);
                const float rms = decoded.getRMSLevel(0, 0, 44100);
                expectWithinAbsoluteError(rms, 0.5f / std::sqrt(2.0f), 0.02f);
            }
        }

        juce::AudioBuffer<float> block(2, 512);
        for (int i = 0; i < 512; ++i)
            for (int ch = 0; ch < 2; ++ch)
                block.setSample(ch, i, tone(i / 44100.0, 0.3f, 441.0));

        beginTest("Transmite Ogg Vorbis con el título, y lo cambia en cada canción");
        {
            FakeIcecast icecast(true);
            expect(icecast.listening);
            auto s = server;
            s.format = StreamEncoder::Format::OggVorbis;
            s.port = icecast.port();

            StreamEncoder encoder;
            encoder.prepare(44100.0);
            encoder.setNowPlaying("ARTISTA", juce::String::fromUTF8("CANCIÓN"));
            encoder.start(s);

            bool changedTitle = false;
            for (int i = 0; i < 1500 && !icecast.contains("TITLE=SIGUIENTE"); ++i) {
                encoder.pushBlock(block, 0, 512);
                if (!changedTitle && icecast.size() > 8000) {
                    encoder.setNowPlaying("OTRO", "SIGUIENTE");
                    changedTitle = true;
                }
                juce::Thread::sleep(3);
            }
            expect(encoder.getStatus() == StreamEncoder::Status::Connected);
            encoder.stop();

            const auto request = icecast.sourceRequest();
            expect(request.startsWith("PUT /prueba HTTP/1.1") && request.contains("application/ogg"), request);
            expect(icecast.contains("OggS"), "llegan páginas Ogg");
            expect(icecast.contains(juce::String::fromUTF8("TITLE=CANCIÓN")) && icecast.contains("ARTIST=ARTISTA"),
                   "el título y el artista con tildes");
            expect(icecast.contains("TITLE=SIGUIENTE"), "al cambiar de canción empieza un flujo encadenado con el título nuevo");
        }

        beginTest("Transmite MP3 y manda el título aparte: al conectar enseguida, después con el retardo");
        {
            FakeIcecast icecast(true);
            expect(icecast.listening);
            auto s = server;
            s.format = StreamEncoder::Format::Mp3;
            s.port = icecast.port();
            s.metadataDelaySeconds = 1;

            StreamEncoder encoder;
            encoder.prepare(44100.0);
            encoder.setNowPlaying("ARTISTA", juce::String::fromUTF8("CANCIÓN"));
            encoder.start(s);

            juce::uint32 changedAt = 0, arrivedAt = 0;
            for (int i = 0; i < 1500 && arrivedAt == 0; ++i) {
                encoder.pushBlock(block, 0, 512);
                if (changedAt == 0 && icecast.size() > 4000 && icecast.adminRequestTime("CANCI%C3%93N") != 0) {
                    encoder.setNowPlaying("OTRO", "SIGUIENTE");
                    changedAt = juce::Time::getMillisecondCounter();
                }
                if (changedAt != 0) arrivedAt = icecast.adminRequestTime("SIGUIENTE");
                juce::Thread::sleep(3);
            }
            expect(encoder.getStatus() == StreamEncoder::Status::Connected);
            encoder.stop();

            const auto request = icecast.sourceRequest();
            expect(request.startsWith("PUT /prueba HTTP/1.1") && request.contains("Content-Type: audio/mpeg"), request);
            expect(icecast.mp3FrameSyncs() > 10, "llegan cuadros MP3");
            expect(!icecast.contains("OggS"), "no es Ogg");
            expect(icecast.adminRequestTime("song=ARTISTA%20-%20CANCI%C3%93N") != 0, "el título de la canción al conectar");
            if (icecast.adminRequestTime("CANCI%C3%93N") != 0)
                expect(icecast.firstAdminRequest().contains("mount=%2Fprueba&mode=updinfo"), icecast.firstAdminRequest());
            expect(arrivedAt != 0, "el título siguiente llegó");
            if (arrivedAt != 0)
                expect(arrivedAt - changedAt >= 900 && arrivedAt - changedAt < 4000,
                       "con el retardo de 1 s: " + juce::String((int) (arrivedAt - changedAt)) + " ms");
        }

        beginTest("Si el servidor no acepta PUT, se presenta con SOURCE y transmite");
        {
            FakeIcecast icecast(true, true);
            expect(icecast.listening);
            auto s = server;
            s.format = StreamEncoder::Format::Mp3;
            s.port = icecast.port();

            StreamEncoder encoder;
            encoder.prepare(44100.0);
            encoder.start(s);
            for (int i = 0; i < 1500 && icecast.mp3FrameSyncs() < 20; ++i) {
                encoder.pushBlock(block, 0, 512);
                juce::Thread::sleep(3);
            }
            expect(encoder.getStatus() == StreamEncoder::Status::Connected);
            encoder.stop();

            const auto requests = icecast.sourceRequests();
            expect(requests.size() == 2, juce::String(requests.size()) + " conexiones");
            expect(requests[0].startsWith("PUT /prueba HTTP/1.1") && requests[1].startsWith("SOURCE /prueba HTTP/1.0"),
                   requests.joinIntoString("\n"));
            expect(icecast.mp3FrameSyncs() >= 20, "llega el audio por la conexión SOURCE");
        }

        beginTest("Si el servidor corta, se nota enseguida aunque siga aceptando datos");
        {
            FakeIcecast icecast(true, false, 500);
            expect(icecast.listening);
            auto s = server;
            s.format = StreamEncoder::Format::Mp3;
            s.port = icecast.port();

            StreamEncoder encoder;
            encoder.prepare(44100.0);
            encoder.start(s);
            juce::uint32 connectedAt = 0, errorAt = 0;
            for (int i = 0; i < 1500 && errorAt == 0; ++i) {
                encoder.pushBlock(block, 0, 512);
                const auto now = juce::Time::getMillisecondCounter();
                if (connectedAt == 0 && encoder.getStatus() == StreamEncoder::Status::Connected) connectedAt = now;
                if (connectedAt != 0 && encoder.getStatus() == StreamEncoder::Status::Error) errorAt = now;
                juce::Thread::sleep(3);
            }
            const auto error = encoder.getLastError();
            encoder.stop();

            expect(connectedAt != 0 && errorAt != 0, "conectó y después se dio cuenta del corte");
            if (errorAt != 0)
                expect(errorAt - connectedAt < 2000, "el servidor cortó a los 0,5 s y se notó a los "
                                                     + juce::String((int) (errorAt - connectedAt)) + " ms");
            expect(error.startsWith(TRANS("stream_err_closed")), "lo notó la lectura, no un envío fallido: " + error);
        }

        beginTest("Si el servidor echa al emisor con una página, el error dice el código y la página se guarda");
        {
            const auto saved = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("raaadio_stream_server_message.html");
            saved.deleteFile();
            FakeIcecast icecast(true, false, 300,
                                "HTTP/1.1 403 Forbidden\r\nContent-Type: text/html\r\n\r\n<html><head><title>Bloqueado</title></head>"
                                "<body><h1>Transmisión detenida</h1><p>Motivo de prueba</p></body></html>");
            expect(icecast.listening);
            auto s = server;
            s.format = StreamEncoder::Format::Mp3;
            s.port = icecast.port();

            StreamEncoder encoder;
            encoder.prepare(44100.0);
            encoder.start(s);
            bool connected = false;
            for (int i = 0; i < 1500 && !(connected && encoder.getStatus() == StreamEncoder::Status::Error); ++i) {
                encoder.pushBlock(block, 0, 512);
                connected = connected || encoder.getStatus() == StreamEncoder::Status::Connected;
                juce::Thread::sleep(3);
            }
            const auto error = encoder.getLastError();
            encoder.stop();

            expect(error.startsWith(TRANS("stream_err_closed")) && error.endsWith("(403 Forbidden)"), error);
            expect(saved.loadFileAsString().contains("<title>Bloqueado</title>"), "la página entera, para leer el motivo");
        }

        beginTest("Lo que se transmite no pasa de -1 dBFS");
        {
            MasterLimiter limiter(StreamEncoder::ceilingDb);
            limiter.prepare(44100.0);
            juce::AudioBuffer<float> loud(2, 44100);
            for (int i = 0; i < 44100; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    loud.setSample(ch, i, tone(i / 44100.0, 1.0f, 440.0));
            limiter.process(loud);
            expect(loud.getMagnitude(0, 44100) <= juce::Decibels::decibelsToGain(-1.0f) + 1.0e-5f,
                   juce::String(juce::Decibels::gainToDecibels(loud.getMagnitude(0, 44100))) + " dBFS");

            MasterLimiter quiet(StreamEncoder::ceilingDb);
            quiet.prepare(44100.0);
            juce::AudioBuffer<float> soft(2, 4410);
            for (int i = 0; i < 4410; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    soft.setSample(ch, i, tone(i / 44100.0, 0.5f, 440.0));
            const float before = soft.getRMSLevel(0, 0, 4410);
            quiet.process(soft);
            expectWithinAbsoluteError(soft.getRMSLevel(0, 0, 4410), before, 1.0e-6f, "por debajo del techo no se toca");

            expectEquals(MasterLimiter().getCeiling(), 1.0f, "la salida al aire sigue en 0 dBFS");
        }

        beginTest("Una clave equivocada queda como error, con el motivo");
        {
            FakeIcecast icecast(false);
            auto s = server;
            s.port = icecast.port();
            StreamEncoder encoder;
            encoder.start(s);
            for (int i = 0; i < 100 && encoder.getStatus() != StreamEncoder::Status::Error; ++i)
                juce::Thread::sleep(20);
            expect(encoder.getStatus() == StreamEncoder::Status::Error);
            juce::Thread::sleep(2500);  // pasa el primer reintento
            expect(encoder.getStatus() == StreamEncoder::Status::Error, "al reintentar sigue mostrando el error");
            expect(encoder.getLastError().contains("401"), encoder.getLastError());
            encoder.stop();
            expect(encoder.getStatus() == StreamEncoder::Status::Off);
        }

        beginTest("Con TLS, un servidor que no lo habla da un error claro y no se queda esperando");
        {
            juce::StreamingSocket listener;
            expect(listener.createListener(0, "127.0.0.1"));
            std::thread plainServer([&listener] {
                std::unique_ptr<juce::StreamingSocket> client(listener.waitForNextConnection());
                if (client == nullptr) return;
                const char* reply = "HTTP/1.0 400 Bad Request\r\n\r\n";
                client->write(reply, (int) strlen(reply));
                juce::Thread::sleep(300);
            });
            StreamSocket socket;
            const auto started = juce::Time::getMillisecondCounter();
            expect(!socket.connect("127.0.0.1", listener.getBoundPort(), true, 4000));
            expect(socket.getLastError().isNotEmpty(), "dice que falló el cifrado, no que no hay conexión");
            expect(juce::Time::getMillisecondCounter() - started < 3000, "no espera el tiempo máximo");
            plainServer.join();
            listener.close();

            StreamSocket refused;  // nada escucha en el puerto 9: falla la conexión, no el cifrado
            expect(!refused.connect("127.0.0.1", 9, true, 1000) && refused.getLastError().isEmpty());
        }

        // Contra un servidor de verdad (red y certificado reales): solo si se pide, para que
        // las pruebas no dependan de internet. RAAADIO_ONLINE_TESTS=1 para correrla.
        if (juce::SystemStats::getEnvironmentVariable("RAAADIO_ONLINE_TESTS", {}) == "1") {
            beginTest("TLS con un servidor Icecast real: valida el certificado y lee la respuesta");
            {
                StreamSocket socket;
                const bool connected = socket.connect("morcast.caster.fm", 10827, true, 8000);
                expect(connected, "(el servidor gratuito de caster.fm se apaga solo) " + socket.getLastError());
                const juce::String request = "GET /status-json.xsl HTTP/1.0\r\nHost: morcast.caster.fm:10827\r\n\r\n";
                expect(socket.writeAll(request.toRawUTF8(), (int) request.getNumBytesAsUTF8()));
                juce::MemoryBlock reply;
                char buffer[4096];
                for (int n; (n = socket.readSome(buffer, (int) sizeof(buffer), 5000)) > 0;)
                    reply.append(buffer, (size_t) n);
                const auto text = reply.toString();
                expect(text.startsWith("HTTP/1.") && text.contains("icestats"), text.substring(0, 300));
            }
        }
    }
};

//==============================================================================
class StationSettingsTests : public juce::UnitTest
{
public:
    StationSettingsTests() : juce::UnitTest("Ajustes de emisión", "RAAADIO") {}

    static juce::String storedText(MusicLibrary& library)
    {
        return juce::String(library.withDatabase([](Database& db) { return db.getSetting("station"); }));
    }

    static void store(MusicLibrary& library, const juce::String& json)
    {
        library.withDatabase([&json](Database& db) { return db.setSetting("station", json.toStdString()); });
    }

    void runTest() override
    {
        beginTest("Se guardan en la base y se leen; los valores fuera de rango se corrigen");
        {
            LibraryFixture fx;
            {
                StationConfig config(&fx.library);
                auto s = config.get();
                expect(s.deadAirEnabled && s.deadAirSeconds == 20 && !s.streamEnabled, "valores por defecto");
                expect(s.backupEnabled && s.backupHour == 3 && s.backupExternalFolder.isEmpty() && s.backupMirrorLibrary,
                       "respaldo diario a las 03:00, sin destino externo");
                s.deadAirSeconds = 45;
                s.airLogEnabled = true;
                s.streamHost = "radio.example.org";
                s.streamPassword = juce::String::fromUTF8("contraseña");
                s.backupHour = 22;
                s.backupExternalFolder = "F:\\Respaldos";
                s.backupMirrorLibrary = false;
                config.set(s);
            }
            StationConfig reopened(&fx.library);
            expectEquals(reopened.get().deadAirSeconds, 45);
            expect(reopened.get().airLogEnabled);
            expectEquals(reopened.get().streamHost, juce::String("radio.example.org"));
            expectEquals(reopened.get().streamPassword, juce::String::fromUTF8("contraseña"));
            expectEquals(reopened.get().backupHour, 22);
            expectEquals(reopened.get().backupExternalFolder, juce::String("F:\\Respaldos"));
            expect(!reopened.get().backupMirrorLibrary);

           #if JUCE_WINDOWS
            expect(!storedText(fx.library).contains(juce::String::fromUTF8("contraseña")), "la clave no queda en claro en la base");
            expect(!storedText(fx.library).contains("streamPassword\""), "ni con el nombre de antes");
           #endif
        }

        beginTest("Clave: la de versiones anteriores se cifra al abrir; la de otro equipo se conserva hasta reescribirla");
        {
            LibraryFixture fx;
            store(fx.library, R"({ "streamHost": "radio.example.org", "streamPassword": "vieja" })");
            StationConfig fromLegacy(&fx.library);
            expectEquals(fromLegacy.get().streamPassword, juce::String("vieja"));
           #if JUCE_WINDOWS
            expect(!storedText(fx.library).contains("vieja"), "se reescribe cifrada enseguida");
           #endif

            store(fx.library, R"({ "streamHost": "radio.example.org", "streamPasswordProtected": "dpapi:AAAAAAAA" })");
            StationConfig fromOtherPc(&fx.library);
            expect(fromOtherPc.get().streamPassword.isEmpty() && fromOtherPc.get().isStreamPasswordUnreadable(),
                   "cifrada en otro equipo: hay que volver a escribirla");
            auto changed = fromOtherPc.get();
            changed.deadAirSeconds = 30;
            fromOtherPc.set(changed);
            expect(storedText(fx.library).contains("dpapi:AAAAAAAA"), "guardar otro ajuste no la borra");
            changed.streamPassword = "nueva";
            fromOtherPc.set(changed);
            StationConfig reread(&fx.library);
            expectEquals(reread.get().streamPassword, juce::String("nueva"));
            expect(!reread.get().isStreamPasswordUnreadable());

            juce::String secret;
            expect(SecretStore::protect({}).isEmpty() && SecretStore::unprotect({}, secret) && secret.isEmpty());
            expect(!SecretStore::unprotect("dpapi:%%%", secret));
        }

        beginTest("Formato, datos de la emisora y envío de lo que suena; las emisoras de antes siguen en Ogg");
        {
            LibraryFixture fx;
            {
                StationConfig config(&fx.library);
                expect(config.get().streamFormat == StationSettings::StreamFormat::Mp3, "una emisora nueva transmite en MP3");
                expectEquals(config.get().streamBitrate, 128);
                auto s = config.get();
                s.streamFormat = StationSettings::StreamFormat::OggVorbis;
                s.streamBitrate = 192;
                s.streamGenre = "Tropical";
                s.streamDescription = juce::String::fromUTF8("La que más suena");
                s.streamWebsite = "https://radio.example.org";
                s.streamPublic = true;
                s.streamTls = true;
                s.streamMetadataDelay = 6;
                s.nowPlayingPushUrl = "https://radio.example.org/ahora.php";
                config.set(s);
            }
            StationConfig reread(&fx.library);
            const auto& r = reread.get();
            expect(r.streamFormat == StationSettings::StreamFormat::OggVorbis);
            expectEquals(r.streamBitrate, 192);
            expectEquals(r.streamGenre, juce::String("Tropical"));
            expectEquals(r.streamDescription, juce::String::fromUTF8("La que más suena"));
            expectEquals(r.streamWebsite, juce::String("https://radio.example.org"));
            expect(r.streamPublic);
            expect(r.streamTls, "la conexión cifrada se recuerda");
            expectEquals(r.streamMetadataDelay, 6);
            expectEquals(r.nowPlayingPushUrl, juce::String("https://radio.example.org/ahora.php"));

            const auto withServer = StationSettings::fromVar(juce::JSON::parse(R"({ "streamHost": "radio.example.org", "streamQuality": 6 })"));
            expect(withServer.streamFormat == StationSettings::StreamFormat::OggVorbis, "ya transmitía en Ogg: no cambia sola");
            expectEquals(withServer.streamQuality, 6);
            const auto withoutServer = StationSettings::fromVar(juce::JSON::parse(R"({ "deadAirSeconds": 30 })"));
            expect(withoutServer.streamFormat == StationSettings::StreamFormat::Mp3, "sin servidor configurado, MP3");
            expect(!withoutServer.streamTls, "sin cifrar, salvo que se pida (Zeno.fm, por ejemplo, va sin cifrar)");

            const auto clamped = StationSettings::fromVar(juce::JSON::parse(R"({ "streamBitrate": 999, "streamMetadataDelay": -5, "backupHour": 30 })"));
            expectEquals(clamped.streamBitrate, 320);
            expectEquals(clamped.streamMetadataDelay, 0);
            expectEquals(clamped.backupHour, 23);

            expect(NowPlayingPublisher::isValidPushUrl("https://radio.example.org/ahora.php"));
            expect(!NowPlayingPublisher::isValidPushUrl("radio.example.org") && !NowPlayingPublisher::isValidPushUrl("ftp://x.org/a")
                   && !NowPlayingPublisher::isValidPushUrl("https://radio.example.org/a b"));
        }

        auto broken = StationSettings().toVar();
        broken.getDynamicObject()->setProperty("deadAirSeconds", 0);
        broken.getDynamicObject()->setProperty("streamPort", 99999);
        const auto fixed = StationSettings::fromVar(broken);
        expectEquals(fixed.deadAirSeconds, 5);
        expectEquals(fixed.streamPort, 65535);
    }
};

//==============================================================================
class NowPlayingTests : public juce::UnitTest
{
public:
    NowPlayingTests() : juce::UnitTest("Lo que suena", "RAAADIO") {}

    static HistoryEntry played(const TrackInfo& track, juce::Time when)
    {
        HistoryEntry entry;
        entry.timestamp = when;
        entry.track = track;
        return entry;
    }

    static TrackInfo song(int id, const juce::String& artist, const juce::String& title)
    {
        auto t = makeTrack("C:/lab/" + title + ".mp3", artist, title);
        t.id = id;
        t.duration = 200.0;
        return t;
    }

    // GET a un servidor HTTP local; devuelve la respuesta completa.
    static juce::String httpGet(int port, const juce::String& path)
    {
        juce::StreamingSocket socket;
        if (!socket.connect("127.0.0.1", port, 2000)) return {};
        const auto request = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
        socket.write(request.toRawUTF8(), (int) request.getNumBytesAsUTF8());
        juce::MemoryBlock response;
        char buffer[1024];
        while (socket.waitUntilReady(true, 2000) == 1) {
            const int n = socket.read(buffer, (int) sizeof(buffer), false);
            if (n <= 0) break;
            response.append(buffer, (size_t) n);
        }
        return response.toString();
    }

    void runTest() override
    {
        const juce::Time now(2026, 8, 24, 16, 0, 0, 0, true);
        auto seconds = [](double s) { return juce::RelativeTime(s); };
        const auto a = song(1, "ARTISTA A", "UNO"), b = song(2, "ARTISTA B", "DOS"), c = song(3, "ARTISTA C", "TRES"),
                   d = song(4, "ARTISTA D", "CUATRO"), e = song(5, "ARTISTA E", "CINCO");
        auto spot = song(9, "", juce::String::fromUTF8("CUÑA FERRETERÍA"));
        spot.duration = 30.0;
        auto isMusic = [](const TrackInfo& t) { return t.id != 9; };

        beginTest("Anterior, actual y siguientes: solo música, sin repetir la que suena");
        {
            NowPlaying::Inputs in;
            in.station = "Radio Prueba";
            in.now = now;
            in.onAir = NowPlaying::Airing { c, now - seconds(60), 136.5 };
            in.history = { played(c, now - seconds(60)), played(spot, now - seconds(90)),
                           played(b, now - seconds(290)), played(a, now - seconds(490)) };
            in.upcoming = { { spot, 136.5 }, { d, 163.0 }, { e, 359.5 } };
            in.isMusic = isMusic;
            const auto s = NowPlaying::build(in);

            expect(s.state == NowPlaying::State::Music);
            expectEquals(s.streamArtist(), juce::String("ARTISTA C"));
            expectEquals(s.streamTitle(), juce::String("TRES"));
            expect(s.currentEnds == now + seconds(137), "entra lo siguiente, redondeado al segundo");
            expectEquals((int) s.previous.size(), 2, "la que suena y la cuña no son anteriores");
            if (s.previous.size() == 2)
                expect(s.previous[0].title == "DOS" && s.previous[1].title == "UNO" && s.previous[0].time == now - seconds(290));
            expectEquals((int) s.next.size(), 2, "la cuña de la cola no aparece");
            if (s.next.size() == 2)
                expect(s.next[0].title == "CUATRO" && s.next[0].time == now + seconds(163) && s.next[1].title == "CINCO");

            const auto json = juce::JSON::parse(NowPlaying::toJson(s, now));
            expectEquals(json["station"].toString(), juce::String("Radio Prueba"));
            expectEquals(json["state"].toString(), juce::String("music"));
            expectEquals(json["updated"].toString(), now.toISO8601(true));
            expectEquals(json["now"]["text"].toString(), juce::String("ARTISTA C - TRES"));
            expectEquals(json["now"]["started"].toString(), (now - seconds(60)).toISO8601(true));
            expectEquals((int) json["now"]["duration"], 200);
            expectEquals(json["previous"][0]["title"].toString(), juce::String("DOS"));
            expectEquals(json["previous"][0]["played"].toString(), (now - seconds(290)).toISO8601(true));
            expectEquals(json["next"][0]["artist"].toString(), juce::String("ARTISTA D"));
            expectEquals(json["next"][0]["eta"].toString(), (now + seconds(163)).toISO8601(true));
        }

        beginTest("Durante una tanda los oyentes ven el nombre de la emisora");
        {
            NowPlaying::Inputs in;
            in.station = "Radio Prueba";
            in.now = now;
            in.onAir = NowPlaying::Airing { spot, now - seconds(5), 25.0 };
            in.history = { played(spot, now - seconds(5)), played(c, now - seconds(200)) };
            in.upcoming = { { d, 25.0 } };
            in.isMusic = isMusic;
            const auto s = NowPlaying::build(in);

            expect(s.state == NowPlaying::State::Break);
            expect(s.streamArtist().isEmpty());
            expectEquals(s.streamTitle(), juce::String("Radio Prueba"));
            expect(s.previous.size() == 1 && s.previous[0].title == "TRES", "la última canción sigue siendo la anterior");

            const auto json = juce::JSON::parse(NowPlaying::toJson(s, now));
            expectEquals(json["state"].toString(), juce::String("break"));
            expect(json["now"].isVoid(), "now es null");
            expectEquals(json["next"][0]["title"].toString(), juce::String("CUATRO"));
        }

        beginTest("Sin nada al aire; sin hora conocida la hora prevista es null; límites de la lista");
        {
            NowPlaying::Inputs in;
            in.station = "Radio Prueba";
            in.now = now;
            for (int i = 0; i < 8; ++i) in.history.push_back(played(a, now - seconds(600 + 200 * i)));
            in.upcoming = { { a, -1.0 }, { b, -1.0 }, { c, -1.0 }, { d, -1.0 } };
            const auto s = NowPlaying::build(in);

            expect(s.state == NowPlaying::State::Off);
            expectEquals(s.streamTitle(), juce::String("Radio Prueba"));
            expectEquals((int) s.previous.size(), 5);
            expectEquals((int) s.next.size(), 3);

            const auto json = juce::JSON::parse(NowPlaying::toJson(s, now));
            expectEquals(json["state"].toString(), juce::String("off"));
            expect(json["now"].isVoid() && json["next"][0]["eta"].isVoid());
            expectEquals(json["previous"].size(), 5);
        }

        beginTest("Las pistas de identificaciones, cuñas y separadores se reconocen por su categoría");
        {
            LibraryFixture fx;
            auto& lib = fx.library;
            lib.addCategory("SALSA");
            int salsa = -1;
            for (const auto& category : lib.getCategories())
                if (juce::String(category.name) == "SALSA") salsa = category.id;

            const auto music = fx.add("Musica/uno.mp3", "UNO");
            const auto spotTrack = fx.add("Cunas/ferreteria.mp3", "FERRETERIA");
            const auto sweeper = fx.add("Separadores/barrido.mp3", "BARRIDO");
            auto& db = lib.getDatabase();
            db.addTrackCategory(music.id, salsa);
            db.addTrackCategory(spotTrack.id, lib.getSystemCategory(CategoryRole::Spots).id);
            db.addTrackCategory(sweeper.id, salsa);
            db.addTrackCategory(sweeper.id, lib.getSystemCategory(CategoryRole::Sweepers).id);

            const auto roles = lib.getSystemRolesByTrack();
            expect(roles.find(music.id) == roles.end(), "la música no aparece");
            expect(roles.count(spotTrack.id) == 1 && roles.at(spotTrack.id) == CategoryRole::Spots);
            expect(roles.count(sweeper.id) == 1 && roles.at(sweeper.id) == CategoryRole::Sweepers,
                   "en una categoría de música y en SEPARADORES: es separador");
        }

        beginTest("El publicador arma lo que suena con los decks, la cola y el historial");
        {
            LibraryFixture fx;
            TempLab outside;
            // Duraciones distintas: el mismo audio se tomaría por el mismo archivo.
            auto import = [&](const juce::String& name, double seconds, const juce::String& category = {}) {
                const auto file = writeWav(outside.root.getChildFile(name + ".wav"), seconds, toneBetween({ { 0.0, seconds } }));
                fx.library.processAndAddTrack(file, category);
                return fx.library.search(name.fromFirstOccurrenceOf(" - ", false, false), "").front();
            };
            const auto cero = import("ARTISTA - CERO", 20.0), uno = import("ARTISTA - UNO", 21.0),
                       dos = import("ARTISTA - DOS", 22.0), tres = import("ARTISTA - TRES", 23.0);
            const auto spotTrack = import(juce::String::fromUTF8("FERRETERÍA - CUÑA"), 24.0,
                                          juce::String(fx.library.getSystemCategory(CategoryRole::Spots).name));

            AudioEngine engine(fx.library, false);
            auto& mixer = engine.getMixer();
            mixer.prepareToPlay(512, 44100.0);
            auto& deckA = engine.getDeck(0);
            auto& deckB = engine.getDeck(1);
            HistoryManager history;
            history.initialise(fx.library);
            history.addEntry(cero, "Deck B");
            deckA.onTrackLog = [&history](const TrackInfo& t) { history.addEntry(t, "Deck A"); };
            deckA.loadTrack(juce::File(juce::String(uno.path)));
            deckA.setRouting(true, false);
            deckA.play();
            pump(mixer, 0.3, { &deckA });

            Playlist playlist;
            playlist.addTrack(dos);
            playlist.addTrack(spotTrack);
            playlist.addTrack(tres);

            StreamEncoder encoder;
            ApiServer api;
            api.start(0);
            NowPlayingPublisher publisher(deckA, deckB, playlist, history, fx.library, encoder, api);

            const auto response = httpGet(api.getPort(), "/nowplaying.json");
            const auto json = juce::JSON::parse(response.fromFirstOccurrenceOf("\r\n\r\n", false, false));
            expectEquals(json["state"].toString(), juce::String("music"), response);
            expectEquals(json["now"]["title"].toString(), juce::String("Uno"));
            expect(!json["now"]["ends"].isVoid(), "se sabe cuándo entra la siguiente");
            expectEquals(json["previous"].size(), 1);
            expectEquals(json["previous"][0]["title"].toString(), juce::String("Cero"), "la que suena no es anterior");
            expectEquals(json["next"].size(), 2);
            expectEquals(json["next"][0]["title"].toString(), juce::String("Dos"));
            expectEquals(json["next"][1]["title"].toString(), juce::String("Tres"), "la cuña de la cola no aparece");
            expect(!json["next"][0]["eta"].isVoid());
            expect(httpGet(api.getPort(), "/nowplaying").endsWith("Artista - Uno"));
            api.stop();
            deckA.stop();
        }

        beginTest("El servidor local entrega el texto y el JSON");
        {
            ApiServer api;
            api.start(0);
            const int port = api.getPort();
            expect(port > 0);
            api.update("ARTISTA - TITULO", R"({"state":"music"})");

            const auto json = httpGet(port, "/nowplaying.json");
            expect(json.startsWith("HTTP/1.1 200") && json.contains("Content-Type: application/json; charset=utf-8"), json);
            expect(json.endsWith(R"({"state":"music"})"), json);
            const auto text = httpGet(port, "/nowplaying?cache=1");
            expect(text.contains("Content-Type: text/plain") && text.endsWith("ARTISTA - TITULO"), text);
            expect(httpGet(port, "/otra").startsWith("HTTP/1.1 404"));
            api.stop();
        }
    }
};

//==============================================================================
class StationDataTests : public juce::UnitTest
{
public:
    StationDataTests() : juce::UnitTest("Estado de la emisora en la base de datos", "RAAADIO") {}

    void runTest() override
    {
        beginTest("Al abrir una emisora de una versión anterior, sus archivos pasan a la base una sola vez");
        {
            TempLab lab;
            PersistenceManager::setLabRootOverride(lab.root);
            const auto dbFile = lab.root.getChildFile("Database/raaadio.db");
            dbFile.getParentDirectory().createDirectory();
            TrackInfo a, b, c;
            {
                MusicLibrary library;
                library.initialise(dbFile);
                for (auto* name : { "A", "B", "C" }) {
                    const auto file = lab.touch(juce::String("Library/") + name + ".ogg");
                    library.getDatabase().addTrack(makeTrack(file.getFullPathName(), "ARTISTA", name));
                }
                a = library.getTrackByPath(lab.root.getChildFile("Library/A.ogg").getFullPathName());
                b = library.getTrackByPath(lab.root.getChildFile("Library/B.ogg").getFullPathName());
                c = library.getTrackByPath(lab.root.getChildFile("Library/C.ogg").getFullPathName());
            }
            // Así quedaba una emisora de una versión anterior: esquema 1 y los datos en archivos.
            sqlite3* raw = nullptr;
            sqlite3_open(dbFile.getFullPathName().toRawUTF8(), &raw);
            sqlite3_exec(raw, "PRAGMA user_version = 1;", nullptr, nullptr, nullptr);
            sqlite3_close(raw);

            const auto id = [](const TrackInfo& t) { return juce::String(t.id); };
            lab.root.getChildFile("Config").createDirectory();
            lab.root.getChildFile("Config/ads_data.json").replaceWithText(
                "{ \"assignments\": [ { \"tid\": " + id(a) + ", \"num\": 3 }, { \"tid\": 9999, \"num\": 4 } ], "
                "\"schedules\": [ { \"num\": 3, \"times\": [ \"07:00\", \"12:00\" ] } ], \"autoSchedule\": true }");
            lab.root.getChildFile("Config/queue.json").replaceWithText(
                "{ \"tracks\": [ { \"id\": " + id(b) + " }, { \"id\": 424242, \"path\": \""
                + juce::String(c.path).replace("\\", "\\\\") + "\" }, { \"id\": " + id(a) + ", \"break\": \"2026-09-23 12:00\" } ] }");
            lab.root.getChildFile("Config/cartwall.json").replaceWithText("[ -1, " + id(c) + ", 9999 ]");
            lab.root.getChildFile("Config/ad_breaks_done.json").replaceWithText("[ \"2026-09-23 12:00\" ]");
            lab.root.getChildFile("Config/station.json").replaceWithText(R"({ "streamHost": "radio.example.org", "streamName": "RADIO VIEJA" })");
            lab.root.getChildFile("Templates").createDirectory();
            lab.root.getChildFile(juce::String::fromUTF8("Templates/MAÑANA.json")).replaceWithText(juce::JSON::toString(GeneratorSettings().toVar()));
            lab.root.getChildFile("Templates/ROTA.json").replaceWithText("esto no es JSON");

            MusicLibrary library;
            library.initialise(dbFile);
            auto& db = library.getDatabase();
            expectEquals(db.getSchemaVersion(), 3);

            AdvertisingManager ads;
            ads.initialise(library);
            expectEquals(ads.getCueNumber(a.id), 3);
            expectEquals(ads.getCueNumber(9999), -1, "una pista que ya no existe no entra a la pauta");
            expect(ads.getSchedules(3) == juce::StringArray { "07:00", "12:00" } && ads.isAutoScheduleEnabled());

            Playlist playlist;
            PlaylistStore store(playlist, library);
            expectEquals(store.restore(), 3);
            const auto queue = playlist.getQueue();
            expect(queue.size() == 3 && queue[0].id == b.id && queue[1].id == c.id && queue[2].id == a.id,
                   "en orden; la pista que cambió de id se reconoce por la ruta");
            if (queue.size() == 3) expectEquals(juce::String(queue[2].breakKey), juce::String("2026-09-23 12:00"));

            const auto carts = db.loadCartwall(CartPlayer::numCarts);
            expect(carts[0] == -1 && carts[1] == c.id && carts[2] == -1, "la botonera, sin la pista que no existe");
            expect(db.loadAdBreaksDone().count("2026-09-23 12:00") == 1);

            StationConfig station(&library);
            expectEquals(station.get().streamName, juce::String("RADIO VIEJA"));
            expect(station.get().streamFormat == StationSettings::StreamFormat::OggVorbis, "ya transmitía: sigue en Ogg");

            TemplateManager templates(library);
            expect(templates.getTemplateNames().contains(juce::String::fromUTF8("MAÑANA")) && !templates.getTemplateNames().contains("ROTA"));

            const auto config = lab.root.getChildFile("Config");
            expect(config.getChildFile("ads_data.json.migrated").existsAsFile() && !config.getChildFile("ads_data.json").exists(),
                   "los archivos quedan como constancia, sin volver a leerse");
            expect(lab.root.getChildFile("Templates/ROTA.json.unreadable").existsAsFile(), "uno ilegible se aparta, no se pierde");

            // Una sola vez: un archivo nuevo en Config ya no se importa.
            config.getChildFile("ads_data.json").replaceWithText("{ \"assignments\": [ { \"tid\": " + id(a) + ", \"num\": 9 } ] }");
            MusicLibrary reopened;
            reopened.initialise(dbFile);
            AdvertisingManager again;
            again.initialise(reopened);
            expectEquals(again.getCueNumber(a.id), 3);
        }

        beginTest("Borrar una pista la quita de la pauta, la cola y la botonera; guardar después la omite sin fallar");
        {
            LibraryFixture fx;
            const auto a = fx.add("Library/A.ogg", "A");
            const auto b = fx.add("Library/B.ogg", "B");
            auto& db = fx.library.getDatabase();
            Database::AdPlan plan;
            plan.numberByTrack = { { a.id, 1 }, { b.id, 2 } };
            expect(db.saveAdPlan(plan) && db.saveQueue({ { a.id, {} }, { b.id, {} } }) && db.saveCartwall({ a.id, -1, b.id }));

            fx.library.deleteTrack(a.id, false);
            expect(db.loadAdPlan().numberByTrack.count(a.id) == 0 && db.loadAdPlan().numberByTrack.count(b.id) == 1);
            expect(db.loadQueue().size() == 1 && db.loadQueue()[0].trackId == b.id);
            const auto carts = db.loadCartwall(3);
            expect(carts[0] == -1 && carts[2] == b.id);

            // Lo que hay en memoria todavía la nombra: se guarda lo demás.
            expect(db.saveQueue({ { a.id, {} }, { b.id, "2026-09-23 12:00" } }));
            expect(db.loadQueue().size() == 1 && db.loadQueue()[0].breakKey == "2026-09-23 12:00");
            expect(db.saveAdPlan(plan));
            expect(db.loadAdPlan().numberByTrack.size() == 1);
        }

        beginTest("Plantillas con cualquier nombre; identificador de la emisora estable; base sana");
        {
            LibraryFixture fx;
            TemplateManager templates(fx.library);
            GeneratorSettings settings;
            settings.targetDurationVal = 95;
            const auto name = juce::String::fromUTF8("MAÑANA / TARDE: 2");
            templates.saveTemplate(name, settings);
            expect(templates.getTemplateNames().contains(name), "barras y dos puntos no son un problema");
            expectEquals(templates.loadTemplate(name).targetDurationVal, 95);
            templates.deleteTemplate(name);
            expect(templates.getTemplateNames().isEmpty());

            auto& db = fx.library.getDatabase();
            const auto id = db.getStationId();
            expect(id.size() == 8 && db.getStationId() == id);
            expectEquals(juce::String(db.quickCheck()), juce::String("ok"));
        }
    }
};

//==============================================================================
class StationSafetyTests : public juce::UnitTest
{
public:
    StationSafetyTests() : juce::UnitTest("Respaldo programado, candado y alertas", "RAAADIO") {}

    void runTest() override
    {
        beginTest("El respaldo diario toca a su hora, y al abrir si esa hora pasó sin respaldo");
        {
            auto at = [](int day, int h, int m) { return juce::Time(2026, 8, day, h, m, 0, 0, true); };
            expect(BackupService::lastScheduledTime(at(23, 11, 0), 3) == at(23, 3, 0));
            expect(BackupService::lastScheduledTime(at(23, 2, 0), 3) == at(22, 3, 0), "antes de la hora, la de ayer");
            expect(BackupService::isDue(at(23, 11, 0), 3, at(22, 11, 0)), "abrió a las 11: el de las 3 no se hizo");
            expect(!BackupService::isDue(at(23, 11, 0), 3, at(23, 3, 1)));
            expect(!BackupService::isDue(at(23, 2, 0), 3, at(22, 11, 0)), "todavía no son las 3");
            expect(BackupService::isDue(at(23, 3, 0), 3, at(22, 11, 0)));
            expect(BackupService::isDue(at(23, 3, 0), 3, juce::Time()), "sin ningún respaldo");
        }

        beginTest("Respaldar ahora: la base aquí y en el destino externo, con la música");
        {
            TempLab lab, external;
            PersistenceManager::setLabRootOverride(lab.root);
            lab.root.getChildFile("Database").createDirectory();
            MusicLibrary library;
            library.initialise(lab.root.getChildFile("Database/raaadio.db"));
            const auto song = lab.touch("Library/GENERAL/uno.ogg");
            library.getDatabase().addTrack(makeTrack(song.getFullPathName(), "A", "UNO"));

            StationConfig station(&library);
            auto s = station.get();
            s.backupExternalFolder = external.root.getFullPathName();
            station.set(s);

            BackupService service(library, station);
            service.runNow();
            for (int i = 0; i < 500 && service.getStatus().running; ++i)
                juce::Thread::sleep(20);
            const auto status = service.getStatus();
            expect(!status.running && status.lastOk && status.externalOk, status.lastError + status.externalError);
            expect(PersistenceManager::getLatestBackupTime().toMilliseconds() > 0);
            const auto folder = service.getExternalStationFolder();
            expect(folder.getChildFile("Library/GENERAL/uno.ogg").existsAsFile(), "la música quedó en el destino");
            expect(PersistenceManager::findLatestVerifiedBackup(folder.getChildFile("Backups")) != juce::File(),
                   "y un respaldo verificado");
            expectEquals(status.mirror.copied, 1);

            // Un destino que no está (un USB desconectado) queda como error, sin afectar lo local.
            s.backupExternalFolder = external.root.getChildFile("no_existe").getFullPathName();
            station.set(s);
            service.runNow();
            for (int i = 0; i < 500 && service.getStatus().running; ++i)
                juce::Thread::sleep(20);
            expect(service.getStatus().lastOk && !service.getStatus().externalOk);
        }

        beginTest("Candado: otro equipo con la emisora abierta se detecta; un latido viejo o de este equipo, no");
        {
            TempLab lab;
            StationLock lock(lab.root);
            expect(!lock.otherOwner().has_value(), "sin candado");

            const auto now = juce::Time::getCurrentTime();
            lab.root.getChildFile("raaadio.lock").replaceWithText(StationLock::describe({ "CABINA-2", "operador", now }));
            const auto other = lock.otherOwner(now);
            expect(other.has_value() && other->computer == "CABINA-2" && other->user == "operador");
            expect(!lock.otherOwner(now + juce::RelativeTime::minutes(5)).has_value(), "se cerró mal hace rato: se toma");

            lab.root.getChildFile("raaadio.lock").replaceWithText(StationLock::describe({ StationLock::thisComputer(), "yo", now }));
            expect(!lock.otherOwner(now).has_value(), "el de este equipo (quedó de un cierre inesperado)");

            lock.acquire();
            const auto owner = StationLock::parse(lab.root.getChildFile("raaadio.lock").loadFileAsString());
            expect(owner.has_value() && owner->computer == StationLock::thisComputer());
            lock.release();
            expect(!lab.root.getChildFile("raaadio.lock").exists(), "al cerrar se libera");
            expect(!StationLock::parse("basura").has_value());
        }

        beginTest("Alertas: disco casi lleno y respaldos atrasados o fallidos");
        {
            using namespace StationHealth;
            const juce::int64 gb = (juce::int64) 1 << 30;
            expect(diskLevel(500 * gb, 1000 * gb) == Level::Ok);
            expect(diskLevel(4 * gb, 1000 * gb) == Level::Warning);
            expect(diskLevel(20 * gb, 1000 * gb) == Level::Warning, "menos del 5 %");
            expect(diskLevel(gb / 2, 1000 * gb) == Level::Critical);
            expect(diskLevel(-1, -1) == Level::Ok, "si no se sabe, no se alarma");

            Inputs in;
            in.now = juce::Time(2026, 8, 23, 12, 0, 0, 0, true);
            in.diskFree = 500 * gb;
            in.diskTotal = 1000 * gb;
            in.latestBackup = in.now - juce::RelativeTime::hours(9);
            expect(evaluate(in).level == Level::Ok);

            in.latestBackup = in.now - juce::RelativeTime::hours(40);
            expect(evaluate(in).level == Level::Warning && evaluate(in).text == TRANS("health_backup_old"));
            in.backupEnabled = false;
            expect(evaluate(in).level == Level::Ok, "con el respaldo automático apagado no se reclama");

            in.backupEnabled = true;
            in.latestBackup = in.now;
            in.externalConfigured = in.externalAttempted = true;
            in.externalOk = false;
            in.externalError = "sin USB";
            in.diskFree = gb / 4;
            const auto alert = evaluate(in);
            expect(alert.level == Level::Critical && alert.text == TRANS("health_disk_full"), "lo más grave manda");
            expectEquals(alert.details.size(), 2, "el detalle trae las dos");
        }

        beginTest("La carpeta Recordings/Registro pasa a Recordings/AirLog");
        {
            TempLab lab;
            PersistenceManager::setLabRootOverride(lab.root);
            lab.touch("Recordings/Registro/2026/09/23/10.ogg");
            PersistenceManager::migrateLegacyFolders();
            expect(PersistenceManager::getAirLogFolder().getChildFile("2026/09/23/10.ogg").existsAsFile());
            expect(!lab.root.getChildFile("Recordings/Registro").exists());
        }
    }
};

//==============================================================================
class PreviewTests : public juce::UnitTest
{
public:
    PreviewTests() : juce::UnitTest("Preescucha de la revisión", "RAAADIO") {}

    void runTest() override
    {
        TempLab lab;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        const auto file = writeWav(lab.root.getChildFile("PREESCUCHA.wav"), 9.0, [this](double t) { return tone(t, 0.3f, 440.0); });

        beginTest("Suena desde el primer tercio de lo que sale al aire y se detiene");
        {
            PreviewPlayer preview(formats);
            preview.prepareToPlay(512, 44100.0);
            TrackInfo track;
            track.id = 7;
            track.path = file.getFullPathName().toStdString();
            track.cueIn = 0.0;
            track.cueOut = 6.0;
            track.normalizationGain = 0.5;
            expect(preview.play(track));
            expect(preview.isPlaying());
            expectEquals(preview.getTrackId(), 7);

            juce::AudioBuffer<float> out(2, 512);
            float peak = 0.0f;
            pumpMessagesUntil([&] {
                out.clear();
                juce::AudioSourceChannelInfo info(&out, 0, 512);
                preview.getNextAudioBlock(info);
                peak = juce::jmax(peak, out.getMagnitude(0, 512));
                return peak > 0.05f;
            }, 5000);
            expect(peak > 0.05f && peak < 0.2f, "suena con la ganancia de la pista (0,3 × 0,5)");

            preview.stop();
            expect(!preview.isPlaying());
            expectEquals(preview.getTrackId(), -1);
            preview.releaseResources();
        }

        beginTest("Un archivo que no es audio no suena");
        {
            PreviewPlayer preview(formats);
            TrackInfo track;
            track.path = lab.touch("NO ES AUDIO.wav").getFullPathName().toStdString();
            expect(!preview.play(track));
            expect(!preview.isPlaying());
        }
    }
};

//==============================================================================
class HttpTransportTests : public juce::UnitTest
{
public:
    HttpTransportTests() : juce::UnitTest("Pedidos HTTP de verdad, contra un servidor local", "RAAADIO") {}

    void runTest() override
    {
        LocalHttpServer server([](const juce::String& method, const juce::String& path, const juce::String& body) {
            if (path == "/hola") return httpResponse(200, "hola");
            if (path == "/eco") return httpResponse(200, method + ":" + body);
            if (path == "/archivo") return httpResponse(200, juce::String::repeatedString("x", 200000));
            if (path == "/cortado") return httpResponse(200, "abc", 1000);
            return httpResponse(404, "no existe");
        });

        beginTest("GET y POST traen el estado y el cuerpo; un pedido binario trae los datos");
        {
            HttpTransport::Request get;
            get.url = server.url("/hola");
            auto response = HttpTransport::send(get);
            expectEquals(response.status, 200);
            expectEquals(response.body, juce::String("hola"));

            HttpTransport::Request post;
            post.url = server.url("/eco");
            post.body = "{\"a\":1}";
            post.headers = "Content-Type: application/json";
            expectEquals(HttpTransport::send(post).body, juce::String("POST:{\"a\":1}"));

            HttpTransport::Request binary;
            binary.url = server.url("/hola");
            binary.binary = true;
            expectEquals((int) HttpTransport::send(binary).data.getSize(), 4);

            HttpTransport::Request missing;
            missing.url = server.url("/nada");
            expectEquals(HttpTransport::send(missing).status, 404);

            HttpTransport::Request nobody;
            nobody.url = "http://127.0.0.1:1/x";
            expectEquals(HttpTransport::send(nobody).status, 0, "sin servidor no hay respuesta");
        }

        beginTest("Bajar un archivo: llega completo con su avance; si se corta, se cancela o no existe, no queda nada");
        {
            TempLab lab;
            const auto target = lab.root.getChildFile("bajado/archivo.bin");
            int calls = 0;
            expect(HttpTransport::downloadToFile(server.url("/archivo"), target, [&calls](double) { ++calls; return true; }));
            expectEquals(target.getSize(), (juce::int64) 200000);
            expect(calls > 0 && !target.getSiblingFile("archivo.bin.part").exists());

            const auto cancelled = lab.root.getChildFile("cancelado.bin");
            expect(!HttpTransport::downloadToFile(server.url("/archivo"), cancelled, [](double) { return false; }));
            expect(!cancelled.exists() && !cancelled.getSiblingFile("cancelado.bin.part").exists());

            const auto cut = lab.root.getChildFile("cortado.bin");
            expect(!HttpTransport::downloadToFile(server.url("/cortado"), cut, {}));
            expect(!cut.exists() && !cut.getSiblingFile("cortado.bin.part").exists());

            expect(!HttpTransport::downloadToFile(server.url("/nada"), lab.root.getChildFile("nada.bin"), {}));
        }
    }
};

//==============================================================================
class DeadAirGuardTests : public juce::UnitTest
{
public:
    DeadAirGuardTests() : juce::UnitTest("Protección contra silencio, funcionando", "RAAADIO") {}

    // Hace sonar el mezclador y deja correr los temporizadores hasta que done().
    bool runUntil(Mixer& mixer, std::initializer_list<Deck*> decks, const std::function<bool()>& done, int timeoutMs = 8000)
    {
        return pumpMessagesUntil([&] { pump(mixer, 0.05, decks); return done(); }, timeoutMs);
    }

    TrackInfo addSong(LibraryFixture& fx, const juce::String& name, int categoryId)
    {
        const auto file = writeWav(fx.lab.root.getChildFile("Library/M/" + name + ".wav"), 20.0,
                                   [this](double t) { return tone(t, 0.3f, 523.0); });
        fx.library.getDatabase().addTrack(makeTrack(file.getFullPathName(), "ARTISTA " + name, name));
        const auto track = fx.library.getTrackByPath(file.getFullPathName());
        if (categoryId >= 0) fx.library.getDatabase().addTrackCategory(track.id, categoryId);
        return track;
    }

    static int categoryIdOf(MusicLibrary& library, const juce::String& name)
    {
        library.addCategory(name);
        for (const auto& c : library.getCategories())
            if (juce::String(c.name) == name) return c.id;
        return -1;
    }

    void runTest() override
    {
        beginTest("Antes de que algo salga al aire no hace nada; en manual solo avisa; apagada no avisa");
        {
            LibraryFixture fx;
            AudioEngine engine(fx.library, false);
            auto& mixer = engine.getMixer();
            mixer.prepareToPlay(512, 44100.0);
            auto& a = engine.getDeck(0);
            auto& b = engine.getDeck(1);
            Playlist playlist;
            AutoDJ autoDJ(engine, playlist);
            DeadAirGuard guard(engine, autoDJ, playlist, fx.library);
            guard.setThresholdSeconds(0.3);

            runUntil(mixer, {}, [] { return false; }, 1200);
            expect(!guard.isAlarmActive(), "sin haber salido nada al aire, el silencio es el de una emisora recién abierta");

            const auto silence = writeWav(fx.lab.root.getChildFile("SILENCIO.wav"), 30.0, [](double) { return 0.0f; });
            a.loadTrack(silence);
            a.setRouting(true, false);
            a.play();
            autoDJ.setEnabled(false);
            expect(runUntil(mixer, { &a, &b }, [&] { return guard.isAlarmActive(); }), "en manual, el silencio solo se avisa");
            expect(a.isPlaying(), "y no se toca nada");

            guard.setEnabled(false);
            expect(!guard.isAlarmActive());
            a.stop();
        }

        beginTest("En automático salta una pista que es silencio, reanuda la cola y si está vacía pone música de emergencia");
        {
            LibraryFixture fx;
            const int emergency = categoryIdOf(fx.library, "EMERGENCIA");
            const auto rescue = addSong(fx, "RESCATE", emergency);

            AudioEngine engine(fx.library, false);
            auto& mixer = engine.getMixer();
            mixer.prepareToPlay(512, 44100.0);
            auto& a = engine.getDeck(0);
            auto& b = engine.getDeck(1);
            Playlist playlist;
            AutoDJ autoDJ(engine, playlist);
            autoDJ.setEnabled(true);
            DeadAirGuard guard(engine, autoDJ, playlist, fx.library);
            guard.setThresholdSeconds(0.3);
            guard.setEmergencyCategory("emergencia");

            const auto silence = writeWav(fx.lab.root.getChildFile("SILENCIO.wav"), 30.0, [](double) { return 0.0f; });
            // Al terminar el salto, la misma vuelta de mensajes puede rellenar la cola y poner
            // la música en ese mismo deck: lo que cuenta es que ya no suene la pista muda.
            auto music = [&](Deck& d) {
                return d.isPlaying() && juce::File(juce::String(d.getCurrentTrack().path)) != silence;
            };
            a.loadTrack(silence);
            a.setRouting(true, false);
            a.play();
            expect(runUntil(mixer, { &a, &b }, [&] { return !a.isPlaying() || music(a); }), "la pista muda se salta");

            expect(runUntil(mixer, { &a, &b }, [&] { return music(a) || music(b); }),
                   "con la cola vacía entra la música de emergencia");
            const auto& playing = music(a) ? a : b;
            expectEquals(juce::String(playing.getCurrentTrack().title), juce::String(rescue.title));
        }

        beginTest("Sin música en la categoría de emergencia, usa la de toda la biblioteca");
        {
            LibraryFixture fx;
            categoryIdOf(fx.library, "EMERGENCIA");  // existe, pero está vacía
            const auto any = addSong(fx, "CUALQUIERA", -1);

            AudioEngine engine(fx.library, false);
            auto& mixer = engine.getMixer();
            mixer.prepareToPlay(512, 44100.0);
            auto& a = engine.getDeck(0);
            auto& b = engine.getDeck(1);
            Playlist playlist;
            AutoDJ autoDJ(engine, playlist);
            autoDJ.setEnabled(true);
            DeadAirGuard guard(engine, autoDJ, playlist, fx.library);
            guard.setThresholdSeconds(0.3);

            const auto silence = writeWav(fx.lab.root.getChildFile("SILENCIO.wav"), 30.0, [](double) { return 0.0f; });
            auto music = [&](Deck& d) {
                return d.isPlaying() && juce::File(juce::String(d.getCurrentTrack().path)) != silence;
            };
            b.loadTrack(silence);
            b.setRouting(true, false);
            b.play();
            expect(runUntil(mixer, { &a, &b }, [&] { return !b.isPlaying() || music(b); }), "también en el deck B");
            expect(runUntil(mixer, { &a, &b }, [&] { return music(a) || music(b); }));
            const auto& playing = music(a) ? a : b;
            expectEquals(juce::String(playing.getCurrentTrack().title), juce::String(any.title));
        }
    }
};

//==============================================================================
class TemporaryAudioTests : public juce::UnitTest
{
public:
    TemporaryAudioTests() : juce::UnitTest("Audios temporales", "RAAADIO") {}

    static juce::String temporariesName(MusicLibrary& library)
    {
        return juce::String::fromUTF8(library.getSystemCategory(CategoryRole::Temporaries).name.c_str());
    }

    static void importAndWait(MusicLibrary& library, const juce::Array<juce::File>& files)
    {
        library.importTemporaries(files);
        for (int i = 0; i < 600 && library.isImporting(); ++i) juce::Thread::sleep(20);
    }

    // Un mensaje de voz: tono a -6 dBFS entre 0.5 s y el final.
    static juce::File writeMessage(const juce::File& file, double seconds, const juce::StringPairArray& tags = {})
    {
        return writeWav(file, seconds, toneBetween({ { 0.5, seconds } }), tags);
    }

    // yt-dlp y FFmpeg de mentira para los enlaces: yt-dlp deja un .m4a donde lo pide "-o" (o
    // responde con el error de errorFor(url)), FFmpeg escribe un MP3 de verdad en su destino.
    struct FakeTools
    {
        juce::MemoryBlock mp3;
        std::function<juce::String(const juce::String& url)> errorFor;
        juce::StringArray lastYtDlp, lastFfmpeg;
        std::atomic<int> downloads { 0 }, conversions { 0 };
        juce::CriticalSection lock;

        SongDownloader::Runner runner()
        {
            return [this](const juce::StringArray& command, const ProcessRunner::LineCallback& onLine, const ProcessRunner::StopCheck&) {
                if (command.contains("-U")) return 0;
                if (command[0].endsWithIgnoreCase("ffmpeg.exe")) {
                    ++conversions;
                    { const juce::ScopedLock sl(lock); lastFfmpeg = command; }
                    const juce::File target(command[command.size() - 1].fromFirstOccurrenceOf("file:", false, false));
                    target.replaceWithData(mp3.getData(), mp3.getSize());
                    return 0;
                }
                ++downloads;
                { const juce::ScopedLock sl(lock); lastYtDlp = command; }
                const auto url = command[command.size() - 1];
                if (errorFor) {
                    const auto error = errorFor(url);
                    if (error.isNotEmpty()) {
                        onLine("[info] Extracting URL: " + url);
                        onLine(error);
                        return 1;
                    }
                }
                // Lo que imprime yt-dlp con los argumentos de LinkDownload.
                if (!url.contains("youtube")) {
                    onLine(juce::String::fromUTF8("[raaadio-title] \"Saludo de la Banda Municipal\\ndesde el parque https://t.co/abc\""));
                    onLine("[raaadio-author] \"bandamunicipal\"");
                }
                const auto folder = juce::File(command[command.indexOf("-o") + 1]).getParentDirectory();
                const auto file = folder.getChildFile(url.contains("youtube") ? "JQwqpsuENeQ.mp3" : "C0aBcDeFgHi.m4a");
                file.replaceWithText("audio de mentira");
                if (url.contains("youtube")) file.replaceWithData(mp3.getData(), mp3.getSize());
                onLine("[raaadio]  40.0%");
                onLine("[raaadio] 100.0%");
                onLine(file.getFullPathName());
                return 0;
            };
        }
    };

    void runTest() override
    {
        beginTest("Importar temporal: entra directo a su carpeta, analizado y normalizado, sin pasar por la revisión");
        {
            LibraryFixture fx;
            TempLab outside;
            const auto greeting = writeMessage(outside.root.getChildFile(utf8("Saludo para doña Rosa.wav")), 6.0);
            juce::StringPairArray tags;
            tags.set(juce::WavAudioFormat::riffInfoTitle, utf8("Mensaje de la Junta de Acción Comunal"));
            tags.set(juce::WavAudioFormat::riffInfoArtist, "JAC El Prado");
            const auto message = writeMessage(outside.root.getChildFile("PTT-20261124-WA0003.wav"), 5.0, tags);
            importAndWait(fx.library, { greeting, message });

            const auto summary = fx.library.getLastImportSummary();
            expectEquals(summary.added, 2);
            expectEquals(summary.toReview, 0, "no entran a la revisión de importados");
            const auto temporaries = fx.library.getTemporaries();
            expectEquals((int) temporaries.size(), 2);
            for (const auto& t : temporaries) {
                const juce::File file(juce::String::fromUTF8(t.path.c_str()));
                expect(t.systemRole == CategoryRole::Temporaries);
                expect(!t.isPending(), "no esperan revisión");
                expect(file.isAChildOf(fx.lab.root.getChildFile("Library/_Temporales")), file.getFullPathName());
                expectEquals(t.analysisVersion, TrackAnalyzer::version, "se analizan al importarlos");
                expectWithinAbsoluteError(t.normalizationGain, TrackAnalyzer::gainFor(t.loudnessLufs, t.truePeakDb), 1.0e-6,
                                          "suenan al mismo volumen que todo lo demás");
                expect(fx.library.isTemporary(t.id));
            }
            juce::StringArray titles, authors;
            for (const auto& t : temporaries) {
                titles.add(juce::String::fromUTF8(t.title.c_str()));
                authors.add(juce::String::fromUTF8(t.artist.c_str()));
            }
            expect(titles.contains(utf8("Saludo para doña Rosa")), "el nombre del archivo tal cual: " + titles.joinIntoString(" | "));
            expect(titles.contains(utf8("Mensaje de la Junta de Acción Comunal")) && authors.contains("JAC El Prado"), "o sus etiquetas");

            expectEquals(fx.library.getPendingCount(), 0);
            expectEquals(fx.library.getUnreviewedCount(), 0, "tampoco cuentan como biblioteca sin revisar");
            expect(fx.library.search("", "").empty(), "la biblioteca no los muestra");
            expect(fx.library.search("Rosa", "").empty());
            for (const auto& c : fx.library.getCategories())
                expect(c.role != CategoryRole::Temporaries, "no aparecen entre las categorías");
            expect(fx.library.getSystemCategory(CategoryRole::Temporaries).id != -1);

            // El mismo archivo otra vez: no se copia dos veces.
            const auto temporariesCategory = temporariesName(fx.library);
            int existing = -1;
            expect(fx.library.processAndAddTrack(greeting, temporariesCategory, 0, &existing) == MusicLibrary::ImportResult::Duplicate);
            expectEquals((int) fx.library.getTemporaries().size(), 2);
            expect(existing != -1 && fx.library.isTemporary(existing));

            // Como cuña es otra pista: borrar el temporal no se la lleva.
            const auto spots = juce::String::fromUTF8(fx.library.getSystemCategory(CategoryRole::Spots).name.c_str());
            expect(fx.library.processAndAddTrack(greeting, spots) == MusicLibrary::ImportResult::Added, "la cuña tiene su propio archivo");
            const auto spotTracks = fx.library.getTracksWithRole(CategoryRole::Spots);
            expectEquals((int) spotTracks.size(), 1);
            expectEquals((int) fx.library.getTrackCategories(existing).size(), 1, "el temporal sigue solo en su categoría");
            expect(fx.library.deleteTemporary(existing));
            if (!spotTracks.empty()) {
                expect(juce::File(juce::String::fromUTF8(spotTracks[0].path.c_str())).existsAsFile());
                expect(!fx.library.isTemporary(spotTracks[0].id));
            }

            // Y al revés: un temporal igual a una cuña que ya estaba también es otra pista.
            expect(fx.library.processAndAddTrack(greeting, temporariesCategory) == MusicLibrary::ImportResult::Added);
            expectEquals((int) fx.library.getTracksWithRole(CategoryRole::Spots).size(), 1);
        }

        beginTest("Quitar uno y Vaciar: se borran de verdad, salvo lo que está en un deck, sonando o en la cola");
        {
            LibraryFixture fx;
            TempLab outside;
            juce::Array<juce::File> files;
            for (int i = 0; i < 4; ++i) files.add(writeMessage(outside.root.getChildFile("mensaje " + juce::String(i) + ".wav"), 3.0 + i));
            importAndWait(fx.library, files);
            auto temporaries = fx.library.getTemporaries();
            expectEquals((int) temporaries.size(), 4);
            if (temporaries.size() != 4) return;

            // Nunca una canción, una cuña ni un separador.
            const auto song = fx.add("Library/Musica/A/cancion.wav", "CANCION");
            expect(!fx.library.deleteTemporary(song.id));
            expect(juce::File(juce::String::fromUTF8(song.path.c_str())).existsAsFile() && fx.library.getTrackById(song.id).id == song.id);

            // Uno: el archivo se borra de verdad (no va a _Descartadas) y el registro también.
            const juce::File first(juce::String::fromUTF8(temporaries[0].path.c_str()));
            expect(fx.library.deleteTemporary(temporaries[0].id));
            expect(!first.exists(), "el archivo se borra");
            expectEquals(fx.library.getTrackById(temporaries[0].id).id, -1);
            expect(!fx.lab.root.getChildFile("Library/_Descartadas").exists(), "no se guarda en _Descartadas");

            // Lo que está en uso no se borra al vaciar.
            TemporaryAudio::Usage usage;
            usage.queued.insert(temporaries[1].id);
            usage.onDecks.insert(temporaries[2].id);
            expect(TemporaryAudio::whyBusy(temporaries[1].id, usage) == TemporaryAudio::Busy::Queued);
            expect(TemporaryAudio::whyBusy(temporaries[2].id, usage) == TemporaryAudio::Busy::OnDeck);
            expect(TemporaryAudio::whyBusy(temporaries[3].id, usage) == TemporaryAudio::Busy::No);
            usage.playingCarts.insert(99);
            expect(TemporaryAudio::whyBusy(99, usage) == TemporaryAudio::Busy::InCart);

            auto result = TemporaryAudio::emptyAll(fx.library, usage);
            expectEquals(result.deleted, 1);
            expectEquals(result.busy, 2);
            expectEquals(result.failed, 0);
            temporaries = fx.library.getTemporaries();
            expectEquals((int) temporaries.size(), 2, "quedan el de la cola y el del deck");
            for (const auto& t : temporaries)
                expect(juce::File(juce::String::fromUTF8(t.path.c_str())).existsAsFile());

            // Un archivo que otro programa tiene abierto no se borra, y su registro se queda.
            if (temporaries.size() == 2) {
                const juce::File locked(juce::String::fromUTF8(temporaries[0].path.c_str()));
                {
                    juce::FileOutputStream holder(locked);
                    expect(holder.openedOk());
                    result = TemporaryAudio::emptyAll(fx.library, {});
                    expectEquals(result.deleted, 1);
                    expectEquals(result.failed, 1);
                    expectEquals((int) fx.library.getTemporaries().size(), 1, "el registro se queda con su archivo");
                }
                result = TemporaryAudio::emptyAll(fx.library, {});
                expectEquals(result.deleted, 1);
            }
            expect(fx.library.getTemporaries().empty());
            expect(!fx.lab.root.getChildFile("Library/_Temporales").exists(), "no quedan carpetas vacías");
            expect(fx.library.getTrackById(song.id).id == song.id, "la música sigue ahí");
        }

        beginTest("El generador no los usa, aunque se elija su categoría a propósito");
        {
            LibraryFixture fx;
            TempLab outside;
            auto& lib = fx.library;
            lib.addCategory("SALSA");
            int salsa = -1;
            for (const auto& c : lib.getCategories())
                if (juce::String(c.name) == "SALSA") salsa = c.id;
            const auto song = fx.add("Library/SALSA/cancion.wav", "CANCION");
            lib.setTrackCategories(song.id, { salsa });
            const auto loose = fx.add("Library/GENERAL/suelta.wav", "SIN CATEGORIA");
            juce::Array<juce::File> files;
            for (int i = 0; i < 3; ++i) files.add(writeMessage(outside.root.getChildFile("saludo " + juce::String(i) + ".wav"), 30.0 + i));  // audios distintos: no son duplicados
            importAndWait(lib, files);
            expectEquals((int) lib.getTemporaries().size(), 3);
            const int temporariesId = lib.getSystemCategory(CategoryRole::Temporaries).id;

            auto noTemporaries = [&lib](const Playlist& playlist) {
                for (const auto& t : playlist.getQueue())
                    if (lib.isTemporary(t.id)) return false;
                return true;
            };
            GeneratorSettings settings;
            settings.lastPlayedSeparation = 0;
            settings.targetDurationVal = 20;
            PlaylistGenerator generator(lib);
            Playlist playlist;
            expect(generator.generate(settings, playlist).success);
            expect(!playlist.getQueue().empty() && noTemporaries(playlist), "toda la música, sin los temporales");
            bool looseUsed = false;
            for (const auto& t : playlist.getQueue()) looseUsed |= t.id == loose.id;
            expect(looseUsed, "la música sin categoría sí");

            settings.includedCategories = { temporariesId };
            Playlist only;
            expect(!generator.generate(settings, only).success, "ni eligiendo la categoría de los temporales");
            expect(only.getQueue().empty());

            settings.includedCategories = { salsa };
            settings.separatorCategoryId = temporariesId;
            settings.separatorInterval = 1;
            Playlist withSeparators;
            generator.generate(settings, withSeparators);
            expect(noTemporaries(withSeparators), "ni como separadores");

            // La lógica pura también los deja fuera.
            auto temporary = lib.getTemporaries().front();
            const std::unordered_map<int, std::vector<int>> categoriesOf { { temporary.id, { temporariesId } } };
            GeneratorSettings all;
            all.includedCategories = { temporariesId };
            all.lastPlayedSeparation = 0;
            expect(PlaylistGenerator::selectCandidates({ temporary }, categoriesOf, { temporariesId }, all, juce::Time::getCurrentTime()).empty());
        }

        beginTest("AutoDJ no los usa: con la cola vacía, la protección contra silencio pone música, no un saludo");
        {
            LibraryFixture fx;
            TempLab outside;
            const auto songFile = writeWav(fx.lab.root.getChildFile("Library/M/CANCION.wav"), 20.0,
                                           [](double t) { return tone(t, 0.3f, 523.0); });
            fx.library.getDatabase().addTrack(makeTrack(songFile.getFullPathName(), "ARTISTA", "CANCION"));
            juce::Array<juce::File> files;
            for (int i = 0; i < 3; ++i) files.add(writeMessage(outside.root.getChildFile("saludo " + juce::String(i) + ".wav"), 20.0 + i));
            importAndWait(fx.library, files);
            expectEquals((int) fx.library.getTemporaries().size(), 3);

            AudioEngine engine(fx.library, false);
            auto& mixer = engine.getMixer();
            mixer.prepareToPlay(512, 44100.0);
            auto& a = engine.getDeck(0);
            auto& b = engine.getDeck(1);
            Playlist playlist;
            AutoDJ autoDJ(engine, playlist);
            autoDJ.setEnabled(true);
            DeadAirGuard guard(engine, autoDJ, playlist, fx.library);
            guard.setThresholdSeconds(0.3);
            guard.setEmergencyCategory(temporariesName(fx.library));  // ni si alguien la escribe como emergencia

            const auto silence = writeWav(fx.lab.root.getChildFile("SILENCIO.wav"), 30.0, [](double) { return 0.0f; });
            auto music = [&](Deck& d) { return d.isPlaying() && juce::File(juce::String(d.getCurrentTrack().path)) != silence; };
            a.loadTrack(silence);
            a.setRouting(true, false);
            a.play();
            expect(pumpMessagesUntil([&] { pump(mixer, 0.05, { &a, &b }); return music(a) || music(b); }, 8000),
                   "con la cola vacía entra música");
            const auto& playing = music(a) ? a : b;
            expectEquals(juce::String(playing.getCurrentTrack().title), juce::String("CANCION"));
            expect(!fx.library.isTemporary(playing.getCurrentTrack().id));
            for (const auto& t : playlist.getQueue())
                expect(!fx.library.isTemporary(t.id), "la cola de rescate no lleva temporales");
            a.stop();
            b.stop();
        }

        beginTest("Enlaces de redes: se reconocen y se arman los argumentos de yt-dlp (sin conexión, sin cookies)");
        {
            expectEquals(LinkDownload::normalise("https://www.instagram.com/reel/C0aBcDeFgHi/?igsh=abc"),
                         juce::String("https://www.instagram.com/reel/C0aBcDeFgHi/?igsh=abc"));
            expectEquals(LinkDownload::normalise(utf8("Mira este saludo: https://fb.watch/abC12/ \xf0\x9f\x91\x8f gracias")),
                         juce::String("https://fb.watch/abC12/"), "el enlace dentro de un mensaje");
            expectEquals(LinkDownload::normalise("instagram.com/p/XYZ"), juce::String("https://instagram.com/p/XYZ"));
            expectEquals(LinkDownload::normalise("<https://x.com/radio/status/1234>."), juce::String("https://x.com/radio/status/1234"));
            for (auto* bad : { "", "hola", "file:///C:/Windows/notepad.exe", "javascript:alert(1)", "--exec calc", "https://",
                               "ftp://x.com/a", "https://localhost/a", "https://ex\"ample.com/a" })
                expect(LinkDownload::normalise(bad).isEmpty(), bad);

            expectEquals(LinkDownload::siteName("https://www.facebook.com/watch/?v=1"), juce::String("Facebook"));
            expectEquals(LinkDownload::siteName("https://fb.watch/abc/"), juce::String("Facebook"));
            expectEquals(LinkDownload::siteName("https://www.instagram.com/reel/a/"), juce::String("Instagram"));
            expectEquals(LinkDownload::siteName("https://twitter.com/a/status/1"), juce::String("X"));
            expectEquals(LinkDownload::siteName("https://x.com/a/status/1"), juce::String("X"));
            expectEquals(LinkDownload::siteName("https://vm.tiktok.com/ZM123/"), juce::String("TikTok"));
            expectEquals(LinkDownload::siteName("https://youtu.be/JQwqpsuENeQ"), juce::String("YouTube"));
            expectEquals(LinkDownload::siteName("https://radio.example.org/audio/1"), juce::String("radio.example.org"));
            expect(LinkDownload::isYouTube("JQwqpsuENeQ") && LinkDownload::isYouTube("https://m.youtube.com/watch?v=JQwqpsuENeQ"));
            expect(!LinkDownload::isYouTube("https://www.instagram.com/reel/a/"));

            TempLab lab;
            DownloadTools::Paths tools;
            tools.ytDlp = lab.root.getChildFile("yt-dlp.exe");
            tools.ffmpegFolder = lab.root.getChildFile("ffmpeg");
            const auto url = LinkDownload::normalise("https://www.instagram.com/reel/C0aBcDeFgHi/");
            const auto folder = lab.root.getChildFile("Trabajo").getChildFile("7");
            const auto args = LinkDownload::arguments(tools, url, folder);
            expectEquals(args[0], tools.ytDlp.getFullPathName());
            expect(args.contains("--no-config"), "sin la configuración del usuario de yt-dlp");
            expectEquals(args[args.size() - 1], url);
            expectEquals(args[args.size() - 2], juce::String("--"), "el enlace nunca se toma por una opción");
            expectEquals(args[args.indexOf("-f") + 1], juce::String("bestaudio/best"), "solo el audio si el sitio lo da aparte");
            expect(!args.contains("-x") && !args.contains("--extract-audio"), "a MP3 lo pasa FFmpeg (AudioConverter) después");
            expectEquals(args[args.indexOf("--match-filter") + 1], juce::String("!is_live"));
            expectEquals(args[args.indexOf("--playlist-items") + 1], juce::String("1"));
            expectEquals(args[args.indexOf("--ffmpeg-location") + 1], tools.ffmpegFolder.getFullPathName());
            expect(args[args.indexOf("-o") + 1].startsWith(folder.getFullPathName()));
            for (const auto& a : args)
                for (auto* forbidden : { "--cookies", "--cookies-from-browser", "--username", "--password", "--netrc" })
                    expect(!a.startsWith(forbidden), "sin cookies ni cuentas del usuario: " + a);
            expect(args.joinIntoString(" ").contains("[raaadio-title]") && args.contains("after_move:filepath"));

            expectEquals(LinkDownload::parseTitle(utf8("[raaadio-title] \"Saludo \\u00e9xito\\nl\xc3\xadnea 2\"")), utf8("Saludo éxito\nlínea 2"));
            expectEquals(LinkDownload::parseTitle("[raaadio-title] NA"), juce::String());
            expectEquals(LinkDownload::parseAuthor("[raaadio-author] \"radio_el_prado\""), juce::String("radio_el_prado"));
            expect(LinkDownload::parseTitle("[raaadio-author] \"x\"").isEmpty());
            expectEquals(LinkDownload::parseFinalPath("C:\\Temp\\RAAADIO\\3\\C0aBcDeFgHi.mp4"), juce::String("C:\\Temp\\RAAADIO\\3\\C0aBcDeFgHi.mp4"));
            expect(LinkDownload::parseFinalPath("[download] 100% of 2.1MiB").isEmpty());

            expectEquals(LinkDownload::cleanTitle(utf8("Radio Prado - Saludo a  las madres\ndel barrio https://t.co/xyz"), "radio", "X"),
                         utf8("Radio Prado - Saludo a las madres del barrio"));
            const auto longTitle = LinkDownload::cleanTitle(juce::String::repeatedString("palabra ", 40), {}, "X");
            expect(longTitle.length() <= 91 && longTitle.endsWith(juce::String::charToString((juce::juce_wchar) 0x2026)));
            expectEquals(LinkDownload::cleanTitle("https://t.co/abc", "bandamunicipal", "Instagram"), juce::String("bandamunicipal"));
            expectEquals(LinkDownload::cleanTitle({}, {}, "Instagram"), juce::String("Instagram"));

            using P = SongDownload::Problem;
            expect(LinkDownload::classify("ERROR: [Instagram] C0aBcDeFgHi: Requested content is not available, rate-limit reached or login "
                                          "required. Use --cookies, --cookies-from-browser, --username and --password, --netrc-cmd, or "
                                          "--netrc (instagram) to provide account credentials") == P::LoginRequired);
            expect(LinkDownload::classify("ERROR: [facebook] 1234567890: Cannot parse data; please report this issue on "
                                          "https://github.com/yt-dlp/yt-dlp/issues?q= , filling out the appropriate issue template")
                   == P::LoginRequired);
            expect(LinkDownload::classify("ERROR: [twitter] 1234: NSFW tweet requires authentication. Use --cookies") == P::LoginRequired);
            expect(LinkDownload::classify("ERROR: [twitter] 1234: No video could be found in this tweet") == P::NoAudio);
            expect(LinkDownload::classify("ERROR: Unsupported URL: https://example.com/foo") == P::Unsupported);
            expect(LinkDownload::classify("[info] abc: does not pass filter (!is_live), skipping ..") == P::Live);
            expect(LinkDownload::classify(utf8("ERROR: [youtube] abc: Sign in to confirm you\xe2\x80\x99re not a bot. Use --cookies"))
                   == P::BotCheck, "lo de YouTube sigue siendo lo de YouTube");
            expect(LinkDownload::classify("ERROR: [generic] Unable to download webpage: HTTP Error 404: Not Found") == P::Unavailable);
            expect(LinkDownload::classify("ERROR: [Instagram] abc: Unable to download webpage: <urlopen error [Errno 11001] getaddrinfo failed>")
                   == P::Network);
            expect(LinkDownload::classify("ERROR: Postprocessing: ffprobe and ffmpeg not found. Please install or provide the path")
                   == P::Tools);
            expect(SongDownload::problemFromKey(SongDownload::problemKey(P::LoginRequired)) == P::LoginRequired);

            // FFmpeg a MP3: el primer audio (de un video no queda la imagen), con LAME, sin versión.
            const auto mp3Args = AudioConverter::arguments(lab.root.getChildFile("ffmpeg.exe"), lab.root.getChildFile("C0a.m4a"),
                                                           lab.root.getChildFile("salida.mp3"), AudioConverter::Format::Mp3);
            expectEquals(mp3Args[mp3Args.indexOf("-map") + 1], juce::String("0:a:0"));
            expectEquals(mp3Args[mp3Args.indexOf("-c:a") + 1], juce::String("libmp3lame"));
            expectEquals(mp3Args[mp3Args.indexOf("-f") + 1], juce::String("mp3"));
            expect(mp3Args.contains("+bitexact") && mp3Args[mp3Args.size() - 1].endsWith("salida.mp3"));
        }

        beginTest("En su hilo: un enlace de Instagram baja, FFmpeg lo pasa a MP3 y entra a Temporales");
        {
            LibraryFixture fx;
            SongDownloadTests::FakeYtDlp unused;
            FakeTools tools;
            tools.mp3 = SongDownloadTests::encodeMp3(6.0);
            expect(tools.mp3.getSize() > 1000, "LAME está junto a las pruebas");
            auto services = SongDownloadTests::fakeServices(fx.lab.root, unused);
            services.run = tools.runner();
            SongDownloader downloader(fx.library, services);

            const auto url = juce::String("https://www.instagram.com/reel/C0aBcDeFgHi/?igsh=x");
            const int id = downloader.enqueueLink("Escucha esto: " + url);
            expect(id > 0);
            expectEquals(downloader.enqueueLink(url), 0, "ya está en la fila");
            expectEquals(downloader.enqueueLink("no es un enlace"), 0);

            const auto job = SongDownloadTests::finished(downloader, id);
            expect(job.has_value() && job->state == SongDownloader::State::Done, job.has_value() ? job->detail : juce::String("timeout"));
            if (!job.has_value() || job->trackId < 0) return;
            expect(job->kind == SongDownloader::Kind::Temporary && job->site == "Instagram");

            const auto track = fx.library.getTrackById(job->trackId);
            expect(fx.library.isTemporary(track.id));
            expectEquals(juce::String::fromUTF8(track.title.c_str()), juce::String("Saludo de la Banda Municipal desde el parque"));
            expectEquals(juce::String::fromUTF8(track.artist.c_str()), juce::String("bandamunicipal"));
            const juce::File file(juce::String::fromUTF8(track.path.c_str()));
            expect(file.hasFileExtension("mp3") && file.isAChildOf(fx.lab.root.getChildFile("Library/_Temporales")), file.getFullPathName());
            expectEquals(track.analysisVersion, TrackAnalyzer::version, "también se analiza y normaliza");
            expectEquals(tools.conversions.load(), 1, "a MP3 con FFmpeg");
            {
                const juce::ScopedLock sl(tools.lock);
                expect(tools.lastFfmpeg.contains("libmp3lame"));
                expect(tools.lastFfmpeg[tools.lastFfmpeg.indexOf("-i") + 1].endsWith(".m4a"));
                expectEquals(tools.lastYtDlp[tools.lastYtDlp.size() - 1], url, "lo pegado, limpio");
            }
            expect(!services.workFolder.getChildFile(juce::String(id)).exists(), "la carpeta de trabajo se borra");
            const auto records = fx.library.getDatabase().getRecentDownloads(1);
            expect(!records.empty() && records[0].videoId == url.toStdString() && records[0].status == Database::DownloadStatus::Done);
            expect(!downloader.jobForTrack(0).has_value(), "la búsqueda de canciones no lo ve");
            expectEquals(downloader.getStatus().finished, 0, "ni lo cuenta entre sus canciones");
            expectEquals((int) downloader.getJobs(SongDownloader::Kind::Temporary).size(), 1);

            // Privado o con sesión: un error claro, sin detener nada.
            tools.errorFor = [](const juce::String&) {
                return juce::String("ERROR: [Instagram] DdDdDdDdDdD: Requested content is not available, rate-limit reached or login "
                                    "required. Use --cookies, --cookies-from-browser, --username and --password");
            };
            const auto failed = SongDownloadTests::finished(downloader, downloader.enqueueLink("https://www.instagram.com/p/DdDdDdDdDdD/"));
            expect(failed.has_value() && failed->state == SongDownloader::State::Failed
                   && failed->problem == SongDownload::Problem::LoginRequired);
            expect(downloader.getStatus().pausedUntil == juce::Time(), "no es YouTube pidiendo esperar: nada se detiene");
            expectEquals((int) fx.library.getTemporaries().size(), 1);
        }

        beginTest("En su hilo: si YouTube pidió esperar, los enlaces de otras redes siguen bajando");
        {
            LibraryFixture fx;
            SongDownloadTests::FakeYtDlp unused;
            FakeTools tools;
            tools.mp3 = SongDownloadTests::encodeMp3(5.0);
            tools.errorFor = [](const juce::String& url) {
                return url.contains("youtube") ? juce::String("ERROR: [youtube] JQwqpsuENeQ: Video unavailable. This content isn't "
                                                              "available, try again later. The current session has been rate-limited")
                                               : juce::String();
            };
            auto services = SongDownloadTests::fakeServices(fx.lab.root, unused);
            services.run = tools.runner();
            SongDownloader downloader(fx.library, services);

            const auto before = juce::Time::getCurrentTime();
            const int song = downloader.enqueue(SongDownloadTests::frioFrio(), SongDownloadTests::video("JQwqpsuENeQ"));
            expect(SongDownloadTests::waitFor([&] { return downloader.getStatus().pausedUntil > before; }));
            const int youtubeLink = downloader.enqueueLink("https://www.youtube.com/watch?v=JQwqpsuENeQ");
            const int link = downloader.enqueueLink("https://www.facebook.com/watch/?v=1234567890");
            const auto done = SongDownloadTests::finished(downloader, link);
            expect(done.has_value() && done->state == SongDownloader::State::Done, "Facebook no espera a YouTube");
            juce::Thread::sleep(200);
            for (const auto& j : downloader.getJobs())
                if (j.id == song || j.id == youtubeLink)
                    expect(j.state == SongDownloader::State::Queued, "lo de YouTube espera su turno");
            expectEquals(tools.downloads.load(), 2, "a YouTube no se le insiste");
        }
    }
};

//==============================================================================
// Interfaz. Se construye y se dibuja sin ventana, igual que el modo --ui-snapshot, con el
// aspecto de la aplicación (RaaadioLookAndFeel). Nunca guarda el idioma ni el tema del
// usuario: setLanguage y setTheme se llaman sin persistir y el estado global se restaura.

struct UiEnvironment
{
    UiEnvironment(const juce::String& language = "es", bool dark = true)
    {
        LanguageManager::getInstance().setLanguage(language, false);
        ThemeManager::getInstance().setTheme(dark ? ThemeManager::Theme::Dark : ThemeManager::Theme::Light, false);
        juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
    }
    ~UiEnvironment()
    {
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        juce::LocalisedStrings::setCurrentMappings(nullptr);  // las demás pruebas comparan claves
    }
    RaaadioLookAndFeel lookAndFeel;
};

// Para operar la interfaz como un usuario: buscar controles por su texto (la clave de
// TRANS, con o sin su "%1"), pulsarlos, hacer clic, elegir opciones de un menú y contestar
// diálogos.
namespace ui
{
    // Lo que la interfaz quiso abrir o copiar fuera de RAAADIO ("acción:destino").
    juce::StringArray& externalActions()
    {
        static juce::StringArray actions;
        return actions;
    }

    // Los diálogos de archivos contestan "Cancelar".
    void cancelFileChoosers()
    {
        FileChoosers::setHandler([](FileChoosers::Kind, const juce::String&,
                                    const std::function<void(const juce::Array<juce::File>&)>& onChosen) { onChosen({}); });
    }

    template <typename T>
    T* find(juce::Component& root, const std::function<bool(T&)>& matches = {})
    {
        for (auto* child : root.getChildren())
        {
            if (child == nullptr) continue;
            if (auto* c = dynamic_cast<T*>(child))
                if (!matches || matches(*c)) return c;
            if (auto* found = find<T>(*child, matches)) return found;
        }
        return nullptr;
    }

    // Pulsar un control puede rehacer la pantalla (una tabla vuelve a crear sus filas): lo que
    // se va a recorrer se guarda como SafePointer, que queda en nada si el control se borra.
    template <typename T>
    std::vector<juce::Component::SafePointer<T>> safe(const std::vector<T*>& components)
    {
        return { components.begin(), components.end() };
    }

    template <typename T>
    std::vector<T*> findAll(juce::Component& root)
    {
        std::vector<T*> result;
        for (auto* child : root.getChildren())
        {
            if (child == nullptr) continue;
            if (auto* c = dynamic_cast<T*>(child)) result.push_back(c);
            const auto inner = findAll<T>(*child);
            result.insert(result.end(), inner.begin(), inner.end());
        }
        return result;
    }

    // Visible con todos sus padres (isShowing() además exige una ventana en el escritorio, y
    // estas pantallas se dibujan sin ventana).
    bool shown(juce::Component& c)
    {
        for (auto* p = &c; p != nullptr; p = p->getParentComponent())
            if (!p->isVisible()) return false;
        return true;
    }

    // El texto que se ve para una clave: el mismo, o uno que tiene, en orden, todas las partes
    // fijas que hay entre sus %1, %2...
    bool matchesKey(const juce::String& shown, const juce::String& key)
    {
        const auto text = TRANS(key);
        if (shown == text) return true;
        if (!text.containsChar('%') || shown.isEmpty()) return false;
        auto pattern = text;
        for (int n = 1; n <= 9; ++n) pattern = pattern.replace("%" + juce::String(n), juce::String::charToString(1));
        int from = 0, fixedParts = 0;
        for (const auto& part : juce::StringArray::fromTokens(pattern, juce::String::charToString(1), ""))
        {
            if (part.isEmpty()) continue;
            const int at = shown.indexOf(from, part);
            if (at < 0) return false;
            from = at + part.length();
            ++fixedParts;
        }
        return fixedParts > 0;
    }

    juce::Button* button(juce::Component& root, const juce::String& key)
    {
        return find<juce::Button>(root, [&](juce::Button& b) {
            return matchesKey(b.getButtonText(), key) || matchesKey(b.getTooltip(), key) || b.getName() == key;
        });
    }

    // Pulsa el botón como lo haría el usuario (solo si está habilitado).
    bool click(juce::Component& root, const juce::String& key)
    {
        if (auto* b = button(root, key))
        {
            b->triggerClick();
            dispatchMessagesFor(30);
            return true;
        }
        return false;
    }

    juce::MouseEvent mouseAt(juce::Component& c, juce::Point<float> position, juce::ModifierKeys mods = {}, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), position, mods,
                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                 juce::MouseInputSource::defaultTiltY, &c, &c, now, position, now, clicks, false };
    }

    const juce::ModifierKeys rightButton { juce::ModifierKeys::rightButtonModifier };

    juce::AccessibilityHandler* findMenuItemWhere(juce::Component& c, const std::function<bool(const juce::String&)>& matches)
    {
        if (auto* handler = c.getAccessibilityHandler())
            if (handler->getRole() == juce::AccessibilityRole::menuItem && matches(handler->getTitle()))
                return handler;
        for (auto* child : c.getChildren())
            if (auto* found = findMenuItemWhere(*child, matches)) return found;
        return nullptr;
    }

    juce::AccessibilityHandler* findMenuItem(juce::Component& c, const juce::String& key)
    {
        return findMenuItemWhere(c, [&key](const juce::String& title) { return matchesKey(title, key); });
    }

    // Elige una opción del menú emergente abierto por su texto, con la acción "press" de
    // accesibilidad de JUCE (lo mismo que hace un lector de pantalla).
    bool chooseMenuItemWhere(const std::function<bool(const juce::String&)>& matches, const juce::String& description,
                             int timeoutMs = 3000)
    {
        juce::AccessibilityHandler* item = nullptr;
        pumpMessagesUntil([&] {
            auto& desktop = juce::Desktop::getInstance();
            for (int i = desktop.getNumComponents(); --i >= 0 && item == nullptr;)
                if (auto* window = desktop.getComponent(i)) item = findMenuItemWhere(*window, matches);
            return item != nullptr;
        }, timeoutMs);
        if (item == nullptr)
        {
            // Para saber por qué: las opciones que sí hay a la vista.
            std::function<void(juce::Component&)> list = [&list](juce::Component& c) {
                if (auto* handler = c.getAccessibilityHandler())
                    if (handler->getRole() == juce::AccessibilityRole::menuItem)
                        std::cout << "    opción del menú: \"" << handler->getTitle() << "\"\n";
                for (auto* child : c.getChildren()) list(*child);
            };
            std::cout << "  no se encontró " << description << " en el menú\n";
            auto& desktop = juce::Desktop::getInstance();
            for (int i = 0; i < desktop.getNumComponents(); ++i)
                if (auto* window = desktop.getComponent(i)) list(*window);
            juce::PopupMenu::dismissAllActiveMenus();
            dispatchMessagesFor(30);
            return false;
        }
        item->getActions().invoke(juce::AccessibilityActionType::press);
        dispatchMessagesFor(50);
        return true;
    }

    bool chooseMenuItem(const juce::String& key, int timeoutMs = 3000)
    {
        return chooseMenuItemWhere([&key](const juce::String& title) { return matchesKey(title, key); },
                                   "\"" + TRANS(key) + "\"", timeoutMs);
    }

    // Abre un submenú y elige una de sus opciones sin dejar pasar mensajes en medio: JUCE
    // cierra los menús en su temporizador cuando el proceso no está en primer plano.
    bool chooseSubmenuItem(const juce::String& parentKey, const std::function<bool(const juce::String&)>& matches,
                           int timeoutMs = 3000)
    {
        juce::AccessibilityHandler* parent = nullptr;
        pumpMessagesUntil([&] {
            auto& desktop = juce::Desktop::getInstance();
            for (int i = desktop.getNumComponents(); --i >= 0 && parent == nullptr;)
                if (auto* window = desktop.getComponent(i)) parent = findMenuItem(*window, parentKey);
            return parent != nullptr;
        }, timeoutMs);
        if (parent == nullptr) return false;
        parent->getActions().invoke(juce::AccessibilityActionType::press);  // muestra el submenú
        juce::AccessibilityHandler* item = nullptr;
        auto& desktop = juce::Desktop::getInstance();
        for (int i = desktop.getNumComponents(); --i >= 0 && item == nullptr;)
            if (auto* window = desktop.getComponent(i)) item = findMenuItemWhere(*window, matches);
        if (item == nullptr)
        {
            juce::PopupMenu::dismissAllActiveMenus();
            dispatchMessagesFor(30);
            return false;
        }
        item->getActions().invoke(juce::AccessibilityActionType::press);
        dispatchMessagesFor(50);
        return true;
    }

    bool chooseMenuItemStartingWith(const juce::String& prefix, int timeoutMs = 3000)
    {
        return chooseMenuItemWhere([&prefix](const juce::String& title) { return title.startsWith(prefix); },
                                   "\"" + prefix + "…\"", timeoutMs);
    }

    // Cierra lo que quede abierto (menús y diálogos), contestando "Cancelar" (o answer).
    void closeEverything(int answer = 0)
    {
        juce::PopupMenu::dismissAllActiveMenus();
        for (int i = 0; i < 10 && juce::Component::getCurrentlyModalComponent() != nullptr; ++i) answerModal(answer, 1000);
        dispatchMessagesFor(30);
    }

    // Contesta el diálogo de JUCE que se abrió escribiendo en su campo (si lo tiene).
    bool answerWithText(const juce::String& field, const juce::String& text, int result)
    {
        juce::Component* modal = nullptr;
        pumpMessagesUntil([&] { modal = juce::Component::getCurrentlyModalComponent(); return modal != nullptr; }, 3000);
        auto* alert = dynamic_cast<juce::AlertWindow*>(modal);
        if (alert == nullptr) return false;
        if (auto* editor = alert->getTextEditor(field)) editor->setText(text);
        return answerModal(result);
    }

    void setText(juce::TextEditor& editor, const juce::String& text)
    {
        editor.setText(text, true);
        dispatchMessagesFor(10);
    }

    void press(juce::Component& c, const juce::KeyPress& key)
    {
        c.keyPressed(key);
        dispatchMessagesFor(20);
    }

    bool isOneOf(juce::Button& b, const juce::StringArray& keys)
    {
        for (const auto& key : keys)
            if (matchesKey(b.getButtonText(), key) || matchesKey(b.getTooltip(), key)) return true;
        return false;
    }

    // Opera todos los controles visibles de un contenedor: recorre cada opción de cada lista,
    // escribe en cada campo (con Enter y al salir de él) y pulsa cada botón, menos los de
    // skipKeys (los que abren el navegador o el Explorador, o se conectan a internet). Lo que
    // se abra (un menú, un diálogo) se cierra cancelando.
    void exercise(juce::Component& root, const juce::StringArray& skipKeys, const juce::String& text = "prueba",
                  int dialogAnswer = 0)
    {
        // Las primeras filas de cada lista y tabla: clic en cada columna, doble clic y Enter.
        for (auto& list : safe(findAll<juce::ListBox>(root)))
        {
            if (list == nullptr || !shown(*list)) continue;
            const auto mouse = mouseAt(*list, { 5.0f, 5.0f });
            if (auto* table = dynamic_cast<juce::TableListBox*>(list.getComponent()))
            {
                auto* model = table->getTableListBoxModel();
                const int rows = model != nullptr ? juce::jmin(3, model->getNumRows()) : 0;
                const int columns = table->getHeader().getNumColumns(true);
                for (int row = 0; row < rows && list != nullptr; ++row)
                {
                    table->selectRow(row);
                    for (int c = 0; c < columns && list != nullptr; ++c)
                    {
                        const int id = table->getHeader().getColumnIdOfIndex(c, true);
                        model->getCellTooltip(row, id);
                        model->cellClicked(row, id, mouse);
                        if (list == nullptr) break;
                        model->cellDoubleClicked(row, id, mouse);
                        closeEverything(dialogAnswer);
                    }
                    if (list == nullptr) break;
                    model->returnKeyPressed(row);
                    closeEverything(dialogAnswer);
                }
            }
            else if (auto* model = list->getListBoxModel())
            {
                const int rows = juce::jmin(3, model->getNumRows());
                for (int row = 0; row < rows && list != nullptr; ++row)
                {
                    list->selectRow(row);
                    model->getTooltipForRow(row);
                    model->listBoxItemClicked(row, mouse);
                    if (list == nullptr) break;
                    model->listBoxItemDoubleClicked(row, mouse);
                    closeEverything(dialogAnswer);
                    if (list == nullptr) break;
                    model->returnKeyPressed(row);
                    closeEverything(dialogAnswer);
                }
            }
        }

        for (auto& combo : safe(findAll<juce::ComboBox>(root)))
        {
            if (combo == nullptr || !shown(*combo) || !combo->isEnabled()) continue;
            const int original = combo->getSelectedItemIndex();
            for (int i = 0; combo != nullptr && i < combo->getNumItems(); ++i)
                combo->setSelectedItemIndex(i, juce::sendNotificationSync);
            if (combo != nullptr) combo->setSelectedItemIndex(juce::jmax(0, original), juce::sendNotificationSync);
            closeEverything(dialogAnswer);
        }
        for (auto& editor : safe(findAll<juce::TextEditor>(root)))
        {
            if (editor == nullptr || !shown(*editor) || editor->isReadOnly()) continue;
            const auto original = editor->getText();
            for (const auto& value : { text, original })
            {
                if (editor == nullptr) break;
                editor->setText(value, true);
                if (editor != nullptr && editor->onReturnKey) editor->onReturnKey();
                if (editor != nullptr && editor->onFocusLost) editor->onFocusLost();
                dispatchMessagesFor(10);
            }
            closeEverything(dialogAnswer);
        }
        for (auto& slider : safe(findAll<juce::Slider>(root)))
        {
            if (slider == nullptr || !shown(*slider) || !slider->isEnabled()) continue;
            const auto original = slider->getValue();
            for (const auto value : { slider->getMinimum(), (slider->getMinimum() + slider->getMaximum()) / 2.0,
                                      slider->getMaximum(), original })
            {
                if (slider == nullptr) break;
                slider->setValue(value, juce::sendNotificationSync);
                if (slider != nullptr && slider->onDragEnd) slider->onDragEnd();
            }
            dispatchMessagesFor(10);
        }
        for (auto& b : safe(findAll<juce::Button>(root)))
        {
            if (b == nullptr || !shown(*b) || !b->isEnabled() || isOneOf(*b, skipKeys)) continue;
            b->triggerClick();
            dispatchMessagesFor(20);
            closeEverything(dialogAnswer);
        }
    }

    // Dibuja el componente completo, para recorrer su paint() en el estado actual.
    bool draws(juce::Component& c)
    {
        if (c.getWidth() <= 0 || c.getHeight() <= 0) return false;
        const auto image = c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f);
        return image.isValid();
    }

    // Espera el diálogo modal que se abre (una ventana de diálogo de JUCE).
    juce::Component* waitForModal(int timeoutMs = 3000)
    {
        juce::Component* modal = nullptr;
        pumpMessagesUntil([&] { modal = juce::Component::getCurrentlyModalComponent(); return modal != nullptr; }, timeoutMs);
        return modal;
    }

    // Pone el componente en una ventana de verdad mientras dura (un menú anclado a un
    // componente, o las teclas de la ventana, necesitan que esté en el escritorio) y al
    // terminar lo saca sin borrarlo.
    struct OnScreen
    {
        explicit OnScreen(juce::Component& c) : window("RAAADIO (pruebas)", juce::Colours::black, 0)
        {
            window.setContentNonOwned(&c, true);
            window.setVisible(true);
            dispatchMessagesFor(50);
        }
        ~OnScreen()
        {
            juce::PopupMenu::dismissAllActiveMenus();
            window.clearContentComponent();
            dispatchMessagesFor(30);
        }
        bool pressKey(const juce::KeyPress& key)
        {
            auto* peer = window.getPeer();
            const bool used = peer != nullptr && peer->handleKeyPress(key);
            dispatchMessagesFor(20);
            return used;
        }
        juce::DocumentWindow window;
    };

    // La pantalla de --ui-snapshot armada con sus datos de demostración, en un laboratorio
    // propio, sin llegar a capturarla.
    struct Screen
    {
        // prepare: lo que tiene la carpeta de la emisora antes de abrirla (un respaldo a
        // medio restaurar, una base dañada...).
        explicit Screen(const juce::String& view, int width = 1366, int height = 768,
                        const std::function<void(const juce::File& station)>& prepare = {})
        {
            // La emisora en "raaadio_lab", como cuando se elige una carpeta en la aplicación.
            station = lab.root.getChildFile("raaadio_lab");
            station.createDirectory();
            PersistenceManager::setLabRootOverride(station);
            if (prepare) prepare(station);
            UiSnapshotRunner::Options options;
            options.output = lab.root.getChildFile("captura.png");
            options.width = width;
            options.height = height;
            options.view = view;
            runner = std::make_unique<UiSnapshotRunner>(options);
            dispatchMessagesFor(50);
        }
        ~Screen()
        {
            closeEverything();
            runner.reset();
            dispatchMessagesFor(30);
        }
        juce::Component& view() { return *runner->getView(); }
        MainComponent& main() { return runner->getMainComponent(); }
        MusicLibrary& library() { return runner->getLibrary(); }
        AudioEngine& engine() { return runner->getAudioEngine(); }
        Playlist& playlist() { return runner->getPlaylist(); }

        TempLab lab;
        juce::File station;
        std::unique_ptr<UiSnapshotRunner> runner;
    };
}

class UiReviewTests : public juce::UnitTest
{
public:
    UiReviewTests() : juce::UnitTest("Interfaz: revisión de lo importado", "RAAADIO") {}

    // El botón de una duda que se está viendo (sí o no), si hay alguno.
    static juce::Button* issueButton(juce::Component& view, bool yes)
    {
        static const char* yesKeys[] = { "review_btn_remove_copy", "review_btn_use_online", "review_btn_format_ok",
                                         "review_btn_its_fine", "review_btn_featured_yes", "review_btn_use_known" };
        static const char* noKeys[] = { "review_btn_keep_both", "review_btn_keep_mine", "review_btn_swap_folder",
                                        "review_btn_swap", "review_btn_featured_no", "review_btn_other_artist" };
        for (const auto* key : (yes ? yesKeys : noKeys))
            if (auto* b = ui::find<juce::Button>(view, [key](juce::Button& b) {
                    return ui::shown(b) && ui::matchesKey(b.getButtonText(), key); }))
                return b;
        return nullptr;
    }

    void runTest() override
    {
        UiEnvironment environment;

        beginTest("Filtros, carpetas y formato de la carpeta");
        {
            ui::Screen screen("review");
            auto& view = screen.view();
            auto* table = ui::find<juce::TableListBox>(view);
            expect(table != nullptr && table->getNumRows() > 0);

            for (const auto* key : { "review_filter_check", "review_filter_duplicates", "review_filter_ready", "review_filter_all" })
            {
                expect(ui::click(view, key), key);
                expect(ui::draws(view), key);
            }
            auto* folders = ui::find<juce::ComboBox>(view);
            expect(folders != nullptr && folders->getNumItems() > 1);
            for (int i = folders->getNumItems(); --i >= 0;)
            {
                folders->setSelectedItemIndex(i, juce::sendNotificationSync);
                expect(ui::click(view, "review_format_artist_title"));
                expect(ui::click(view, "review_format_title_artist"));
                expect(ui::draws(view));
            }
            folders->setSelectedItemIndex(0, juce::sendNotificationSync);

            expect(ui::click(view, "review_scope_library"));
            expect(ui::draws(view), "la biblioteca sin revisar");
            expect(ui::click(view, "review_scope_new"));
            int shownArtists = 0;
            dynamic_cast<ImportReviewView&>(view).onShowArtists = [&shownArtists] { ++shownArtists; };
            expect(ui::click(view, "review_scope_artists") && shownArtists == 1);
        }

        beginTest("Cada duda se contesta con un clic, por sí y por no");
        {
            for (const bool yes : { true, false })
            {
                ui::Screen screen("review");
                auto& view = screen.view();
                auto* table = ui::find<juce::TableListBox>(view);
                int answered = 0;
                for (int pass = 0; pass < 3; ++pass)
                    for (int row = 0; row < table->getNumRows(); ++row)
                    {
                        table->selectRow(row);
                        dispatchMessagesFor(10);
                        expect(ui::draws(view));
                        if (auto* b = issueButton(view, yes))
                        {
                            b->triggerClick();
                            dispatchMessagesFor(30);
                            ++answered;
                        }
                    }
                expect(answered > 0, yes ? "sí" : "no");
            }
        }

        beginTest("Editar los créditos: sin título no se guarda; con título se guarda y pasa a la siguiente");
        {
            ui::Screen screen("review");
            auto& view = screen.view();
            auto* table = ui::find<juce::TableListBox>(view);
            table->selectRow(0);
            dispatchMessagesFor(20);
            auto* editor = ui::find<CreditsEditor>(view);
            expect(editor != nullptr);
            auto fields = ui::findAll<juce::TextEditor>(*editor);
            expect(fields.size() >= 6);
            const auto original = editor->getValues();

            auto* title = ui::find<juce::TextEditor>(*editor, [&](juce::TextEditor& e) { return e.getText() == original.title; });
            expect(title != nullptr);
            ui::setText(*title, {});
            expect(ui::click(view, "review_save_edit"));
            expect(editor->hasChanges(), "sin título no se guarda");

            ui::setText(*title, "Un Titulo Nuevo");
            auto* artist = fields.front();
            ui::setText(*artist, "Wilf");  // sugerencias de artistas conocidos
            for (auto* suggestion : ui::findAll<juce::TextButton>(*editor))
                if (ui::shown(*suggestion)) { suggestion->triggerClick(); dispatchMessagesFor(20); break; }
            ui::setText(*artist, "Artista Corregido");
            expect(ui::click(view, "review_save_edit"));
            expect(!editor->hasChanges(), "se guardó");

            table->selectRow(1);
            dispatchMessagesFor(20);
            if (auto* first = ui::findAll<juce::TextEditor>(*editor).front())
            {
                ui::setText(*first, "Otro Artista");
                table->selectRow(2);  // cambiar de fila guarda lo editado
                dispatchMessagesFor(20);
            }
            expect(ui::draws(view));
        }

        beginTest("Menús de categoría, mayúsculas y de la fila; el teclado; quitar y confirmar");
        {
            ui::Screen screen("review");
            auto& view = screen.view();
            auto& review = dynamic_cast<ImportReviewView&>(view);
            auto* table = ui::find<juce::TableListBox>(view);
            int changes = 0;
            review.onLibraryChanged = [&changes] { ++changes; };
            table->selectRow(0);
            dispatchMessagesFor(20);

            expect(ui::click(view, "review_category"));
            juce::String category;
            for (const auto& c : screen.library().getCategories())
                if (!c.isSystem()) { category = MusicLibrary::getDisplayName(c); break; }
            expect(ui::chooseMenuItem(category), "se elige una categoría");
            expect(ui::click(view, "review_category"));
            expect(ui::chooseMenuItem(category), "y se quita");

            expect(ui::click(view, "review_case"));
            expect(ui::chooseMenuItem("review_case_upper"));
            expect(ui::click(view, "review_case"));
            expect(ui::chooseMenuItem("review_case_title"));
            expect(ui::click(view, "review_swap"));

            review.cellClicked(0, 1, ui::mouseAt(*table, { 10.0f, 10.0f }, ui::rightButton));
            expect(ui::chooseMenuItem("review_swap"), "menú de la fila: intercambiar");
            review.cellClicked(0, 1, ui::mouseAt(*table, { 10.0f, 10.0f }));  // un clic normal no abre el menú
            ui::closeEverything();

            ui::press(view, juce::KeyPress('i', juce::ModifierKeys::commandModifier, 0));
            ui::press(view, juce::KeyPress('x'));
            review.returnKeyPressed(0);

            review.deleteKeyPressed(0);
            expect(answerModal(0), "quitar pregunta; se cancela");
            review.cellClicked(0, 1, ui::mouseAt(*table, { 10.0f, 10.0f }, ui::rightButton));
            expect(ui::chooseMenuItem("review_discard_btn"));
            expect(answerModal(1), "y se acepta");
            expect(changes == 1, "quitar avisa que cambió la biblioteca");

            table->selectRow(0);
            review.cellClicked(0, 1, ui::mouseAt(*table, { 10.0f, 10.0f }, ui::rightButton));
            expect(ui::chooseMenuItem("review_confirm_selection"));
            expect(pumpMessagesUntil([&] { return changes == 2; }, 10000), "confirmar la fila elegida");

            ui::press(view, juce::KeyPress(juce::KeyPress::returnKey, juce::ModifierKeys::commandModifier, 0));
            pumpMessagesUntil([&] { return changes >= 3; }, 10000);

            expect(ui::click(view, "review_confirm_ready"));
            pumpMessagesUntil([&] { return changes >= 4; }, 20000);
            expect(ui::click(view, "review_confirm_ready"));  // ya no queda nada listo
            expect(ui::draws(view));
        }

        beginTest("Deshacer una importación desde la revisión; preescuchar por Cue");
        {
            ui::Screen screen("review");
            auto& engine = screen.engine();
            // Un lote de verdad: los datos de demostración entran sin pasar por la importación.
            TempLab outside;
            for (int i = 0; i < 2; ++i)
                writeWav(outside.root.getChildFile("LOTE/CANCION " + juce::String(i) + " - GRUPO LOTE.wav"), 2.0,
                         [i](double t) { return tone(t, 0.3f, 600.0 + 50.0 * i); });
            screen.library().addCategory("LOTE");
            screen.library().importFiles({ outside.root.getChildFile("LOTE") }, "LOTE");
            pumpMessagesUntil([&] { return !screen.library().isImporting(); }, 20000);
            ImportReviewView review(screen.library(), &engine.getPreview(), ImportReview::Scope::NewImports);
            review.setSize(1366, 768);
            int changes = 0;
            review.onLibraryChanged = [&changes] { ++changes; };

            expect(ui::click(review, "review_undo_import"));
            ui::closeEverything();
            expect(ui::click(review, "review_undo_import"));
            const auto batches = screen.library().getImportBatches();
            expect(!batches.empty());
            juce::AccessibilityHandler* item = nullptr;
            pumpMessagesUntil([&] {
                auto& desktop = juce::Desktop::getInstance();
                for (int i = desktop.getNumComponents(); --i >= 0 && item == nullptr;)
                    if (auto* window = desktop.getComponent(i))
                        item = ui::findMenuItem(*window, "review_undo_item");
                return item != nullptr;
            }, 3000);
            expect(item != nullptr, "el lote aparece en el menú");
            if (item != nullptr)
            {
                const int before = (int) screen.library().getPendingCount();
                item->getActions().invoke(juce::AccessibilityActionType::press);
                expect(answerModal(1), "deshacer pregunta");
                expect(changes == 1 && (int) screen.library().getPendingCount() < before, "el lote salió de la biblioteca");
            }
            expect(ui::draws(review));

            ui::Screen again("review");
            ImportReviewView withPreview(again.library(), &again.engine().getPreview(), ImportReview::Scope::NewImports);
            withPreview.setSize(1366, 768);
            auto* table = ui::find<juce::TableListBox>(withPreview);
            table->selectRow(0);
            dispatchMessagesFor(20);
            expect(ui::click(withPreview, "review_listen"), "las pistas de la demostración no son audio: avisa");
            withPreview.cellClicked(0, 1, ui::mouseAt(*table, { 10.0f, 10.0f }, ui::rightButton));
            ui::chooseMenuItem("review_listen");
            expect(ui::draws(withPreview));
        }

        beginTest("La ventana de revisión cambia entre lo nuevo, la biblioteca y los artistas");
        {
            ui::Screen screen("review");
            ImportReviewWindow window(screen.library(), nullptr, ImportReview::Scope::NewImports);
            int closed = 0, changed = 0;
            window.onClosed = [&closed] { ++closed; };
            window.onLibraryChanged = [&changed] { ++changed; };
            window.showArtists();
            window.showReview(ImportReview::Scope::Library);
            window.showReview(ImportReview::Scope::NewImports);
            expect(ui::click(window, "review_close"));
            expect(closed == 1);
            window.closeButtonPressed();
            expect(closed == 2);
        }
    }
};

class UiSettingsTests : public juce::UnitTest
{
public:
    UiSettingsTests() : juce::UnitTest("Interfaz: configuración", "RAAADIO") {}

    static juce::Component& tab(juce::Component& view, int index)
    {
        auto* tabs = ui::find<juce::TabbedComponent>(view);
        tabs->setCurrentTabIndex(index);
        dispatchMessagesFor(20);
        return *tabs->getCurrentContentComponent();
    }

    // Lo que "elige" el usuario en el diálogo de carpetas.
    juce::File answer;

    void runTest() override
    {
        UiEnvironment environment;
        FileChoosers::setHandler([this](FileChoosers::Kind, const juce::String&,
                                        const std::function<void(const juce::Array<juce::File>&)>& onChosen) {
            onChosen(answer == juce::File() ? juce::Array<juce::File>() : juce::Array<juce::File> { answer });
        });
        // Se conectan a internet: se prueban aparte, sin conexión.
        const juce::StringArray external { "stream_enable", "online_toggle" };

        beginTest("Biblioteca y datos: opciones, respaldo, destino externo, exportar, restaurar e importar");
        {
            ui::Screen screen("settings");
            auto& library = tab(screen.view(), 1);
            answer = juce::File();
            ui::exercise(library, external);
            expect(ui::draws(screen.view()));

            answer = screen.station.getChildFile("Backups");
            answer.createDirectory();
            expect(ui::click(library, "backup_external_choose"));
            expect(answerModal(1), "un destino dentro de la emisora no sirve");
            TempLab externalDrive;
            answer = externalDrive.root;
            expect(ui::click(library, "backup_external_choose"));
            expect(answerModal(1), "en el mismo disco se avisa y se acepta igual");
            expect(screen.runner->getMainComponent().isVisible());
            expect(ui::click(library, "backup_external_clear"));
            expect(ui::click(library, "btn_backup"));
            expect(ui::draws(library));

            TempLab exportTarget;
            answer = exportTarget.root;
            expect(ui::click(library, "btn_export"));
            expect(answerModal(1), "exportado");

            juce::File backup;
            expect(PersistenceManager::createBackup([&screen](const juce::File& target) {
                return screen.library().backupDatabaseTo(target); }, &backup).wasOk());
            answer = backup;
            expect(ui::click(library, "btn_restore"));
            expect(answerModal(0), "restaurar queda preparado y ofrece reiniciar");
            answer = exportTarget.root;
            expect(ui::click(library, "btn_restore"));
            expect(answerModal(1), "una carpeta que no es un respaldo da error");
            answer = backup;
            expect(ui::click(library, "btn_import"));
            expect(answerModal(0), "importar pregunta; se cancela");
            expect(ui::click(library, "btn_import"));
            expect(answerModal(1), "se acepta");
            expect(answerModal(0), "y ofrece reiniciar");
            expect(ui::draws(library), "con una restauración pendiente");
        }

        beginTest("Sin el servicio de respaldo, respaldar avisa al terminar");
        {
            ui::Screen screen("settings");
            SettingsDialog dialog(screen.engine().getDeviceManager(), screen.engine().getCueDeviceManager(),
                                  screen.library(), screen.runner->getAdsManager(),
                                  screen.runner->getStationConfig(), screen.runner->getStreamEncoder(),
                                  nullptr, nullptr, PersistenceManager::getAirLogFolder());
            dialog.setSize(600, 640);
            auto& library = tab(dialog, 1);
            expect(ui::click(library, "btn_backup"));
            expect(answerModal(1), "respaldo hecho");
            dialog.sendLookAndFeelChange();
        }

        beginTest("Mover la emisora: la misma, una que ya existe, una carpeta ocupada y una copia completa");
        {
            ui::Screen screen("settings");
            auto& library = tab(screen.view(), 1);

            answer = juce::File();
            expect(ui::click(library, "btn_change_lab_root"), "cancelar no hace nada");
            answer = screen.lab.root;
            expect(ui::click(library, "btn_change_lab_root"));
            expect(answerModal(1), "es la misma carpeta");

            TempLab other;
            other.touch("raaadio_lab/Database/raaadio.db");
            answer = other.root;
            expect(ui::click(library, "btn_change_lab_root"));
            expect(answerModal(1), "ya hay una emisora: se usa esa");
            expect(answerModal(0), "y ofrece reiniciar");

            answer = screen.station.getChildFile("Library");
            expect(ui::click(library, "btn_change_lab_root"));
            expect(answerModal(1), "dentro de la emisora no se puede");

            TempLab busy;
            busy.touch("raaadio_lab/otra cosa.txt");
            answer = busy.root;
            expect(ui::click(library, "btn_change_lab_root"));
            expect(answerModal(1), "una carpeta con otras cosas no se usa");

            TempLab empty;
            answer = empty.root;
            expect(ui::click(library, "btn_change_lab_root"));
            expect(answerModal(0), "copiar pregunta; se cancela");
            expect(ui::click(library, "btn_change_lab_root"));
            expect(answerModal(1), "se acepta: copia con avance");
            // La ventana de avance también es modal: contestarla sería cancelar la copia (JUCE
            // detiene el hilo si deja de serlo). Se espera el aviso del final, o el de un error.
            juce::String finalDialog;
            expect(pumpMessagesUntil([&finalDialog] {
                auto* modal = juce::Component::getCurrentlyModalComponent();
                if (modal == nullptr || modal->getName() == TRANS("lab_copying")) return false;
                finalDialog = modal->getName();
                return true;
            }, 60000), "la copia termina");
            expectEquals(finalDialog, TRANS("restart_title"), "sin errores");
            expect(answerModal(0), "al terminar ofrece reiniciar");
            expect(PersistenceManager::containsStation(empty.root.getChildFile("raaadio_lab")), "la copia está completa");
        }

        beginTest("Borrar la biblioteca pregunta y respalda antes");
        {
            ui::Screen screen("settings");
            auto& library = tab(screen.view(), 1);
            expect(ui::click(library, "db_reset"));
            expect(answerModal(0), "se cancela");
            expect(!screen.library().getCategories().empty());
            expect(ui::click(library, "db_reset"));
            expect(answerModal(1), "se acepta");
            expect(answerModal(1), "y avisa que quedó vacía");
        }

        beginTest("Emisión, idioma y tema");
        {
            ui::Screen screen("settings");
            auto& broadcast = tab(screen.view(), 2);
            ui::exercise(broadcast, external, "http://127.0.0.1:1/ahora.php");  // nada sale del equipo
            ui::exercise(broadcast, external, "no es una dirección");
            if (auto* host = ui::find<juce::TextEditor>(broadcast, [](juce::TextEditor& e) {
                    return e.getText() == "stream.example.org"; }))
            {
                // Sin servidor, "Transmitir" no se queda marcado ni se conecta a nada.
                ui::setText(*host, {});
                if (host->onFocusLost) host->onFocusLost();
                expect(ui::click(broadcast, "stream_enable"));
                expect(!ui::button(broadcast, "stream_enable")->getToggleState());
            }
            expect(ui::draws(broadcast));

            auto& language = tab(screen.view(), 3);
            ui::exercise(language, external);
            expect(ui::draws(language));
            LanguageManager::getInstance().setLanguage("es", false);
            ThemeManager::getInstance().setTheme(ThemeManager::Theme::Dark, false);

            for (int i = 0; i < 5; ++i) expect(ui::draws(tab(screen.view(), i)));
            screen.view().sendLookAndFeelChange();
        }

        ui::cancelFileChoosers();
    }
};

class UiQueueTests : public juce::UnitTest
{
public:
    UiQueueTests() : juce::UnitTest("Interfaz: cola de reproducción", "RAAADIO") {}

    static void clickMoveButtons(juce::Component& queue)
    {
        for (auto& b : ui::safe(ui::findAll<juce::Button>(queue)))
            if (b != nullptr && ui::shown(*b)
                && (ui::matchesKey(b->getTooltip(), "tip_move_up") || ui::matchesKey(b->getTooltip(), "tip_move_down")))
            {
                b->triggerClick();
                dispatchMessagesFor(10);
            }
    }

    void runTest() override
    {
        UiEnvironment environment;

        beginTest("Subir y bajar filas y bloques, el menú de la fila, quitar y vaciar la cola");
        {
            ui::Screen screen("main");
            auto* queue = ui::find<PlaylistComponent>(screen.main());
            auto* table = queue != nullptr ? ui::find<juce::TableListBox>(*queue) : nullptr;
            expect(table != nullptr && table->getNumRows() > 6);
            auto size = [&screen] { return (int) screen.playlist().getQueue().size(); };
            const int start = size();
            auto rightClick = [&](int row) { queue->cellClicked(row, 1, ui::mouseAt(*table, { 10.0f, 10.0f }, ui::rightButton)); };

            table->selectRow(3);
            table->updateContent();
            expect(ui::draws(*queue));
            clickMoveButtons(*queue);
            table->selectRangeOfRows(2, 4);
            table->updateContent();
            expect(ui::draws(*queue), "un bloque seleccionado: solo subir el primero y bajar el último");
            clickMoveButtons(*queue);
            expectEquals(size(), start, "mover no quita nada");

            table->selectRow(4);
            rightClick(4);
            expect(ui::chooseMenuItem("menu_move_to_top"));
            rightClick(3);
            expect(ui::chooseMenuItem("menu_remove"));
            expectEquals(size(), start - 1);
            queue->cellClicked(2, 1, ui::mouseAt(*table, { 10.0f, 10.0f }));  // un clic normal no abre el menú
            rightClick(9999);
            table->selectRow(2);
            queue->deleteKeyPressed(2);
            expectEquals(size(), start - 2, "Suprimir quita la fila");

            table->selectRow(1);
            rightClick(1);
            expect(ui::chooseMenuItem("menu_play_now"));
            expect(answerModal(1), "con algo al aire, pregunta");
            ui::closeEverything();  // la pista de ejemplo no es audio: avisa que no pudo
            expectEquals(size(), start - 3, "la pista sale de la cola");

            rightClick(1);
            expect(ui::chooseMenuItem("menu_clear_queue"));
            expect(answerModal(0) && size() > 0, "vaciar pregunta; se cancela");
            rightClick(1);
            expect(ui::chooseMenuItem("menu_clear_queue"));
            expect(answerModal(1) && size() == 0, "se vacía");
            expect(ui::draws(*queue), "la cola vacía");
        }

        beginTest("Arrastrar desde la biblioteca y reordenar la cola arrastrando");
        {
            ui::Screen screen("main");
            auto* queue = ui::find<PlaylistComponent>(screen.main());
            auto* table = ui::find<juce::TableListBox>(*queue);
            auto size = [&screen] { return (int) screen.playlist().getQueue().size(); };
            const auto tracks = screen.library().search("", "");
            expect(tracks.size() > 2);

            const juce::var fromLibrary("track_list_" + juce::String(tracks[0].path) + "|" + juce::String(tracks[1].path));
            using Source = juce::DragAndDropTarget::SourceDetails;
            expect(queue->isInterestedInDragSource(Source(fromLibrary, nullptr, {})));
            expect(!queue->isInterestedInDragSource(Source(juce::var("otra cosa"), nullptr, {})));
            queue->itemDragEnter(Source(fromLibrary, nullptr, {}));
            for (const int y : { 2, 30, table->getHeight() / 2, table->getHeight() - 10, table->getHeight() + 60 })
            {
                queue->itemDragMove(Source(fromLibrary, nullptr, { 20, table->getY() + y }));
                expect(ui::draws(*queue));
            }
            const int before = size();
            queue->itemDropped(Source(fromLibrary, nullptr, { 20, table->getY() + 30 }));
            expectEquals(size(), before + 2);
            queue->itemDragExit(Source(fromLibrary, nullptr, {}));

            juce::SparseSet<int> rows;
            rows.addRange({ 1, 3 });
            const auto reorder = queue->getDragSourceDescription(rows);
            expect(reorder.toString().startsWith("playlist_drag_"));
            expect(queue->getDragSourceDescription(juce::SparseSet<int>()).isVoid());
            queue->itemDragMove(Source(reorder, nullptr, { 20, table->getY() + table->getHeight() / 2 }));
            queue->itemDropped(Source(reorder, nullptr, {}));
            expectEquals(size(), before + 2, "reordenar no cambia cuántas hay");
        }

        beginTest("Arrastrando hacia la cola: cerca del borde se desplaza sola, y la rueda mueve la lista que está debajo");
        {
            ui::Screen screen("main");
            auto* queue = ui::find<PlaylistComponent>(screen.main());
            auto* table = ui::find<juce::TableListBox>(*queue);
            auto* viewport = table->getViewport();
            expect(viewport->canScrollVertically(), "la cola de demostración no cabe en la pantalla");
            const auto tracks = screen.library().search("", "");
            const juce::var fromLibrary("track_list_" + juce::String(tracks[0].path));
            using Source = juce::DragAndDropTarget::SourceDetails;
            auto at = [&](int y) { return Source(fromLibrary, nullptr, { table->getX() + 40, table->getY() + y }); };

            viewport->setViewPosition(0, 0);
            queue->itemDragEnter(at(table->getHeight() - 8));
            queue->itemDragMove(at(table->getHeight() - 8));
            expect(pumpMessagesUntil([&] { return viewport->getViewPositionY() > 3 * table->getRowHeight(); }, 5000),
                   "cerca del borde de abajo baja sola");

            queue->itemDragMove(at(table->getHeight() / 2));
            const int middle = viewport->getViewPositionY();
            dispatchMessagesFor(300);
            expectEquals(viewport->getViewPositionY(), middle, "en el medio se queda quieta");

            queue->itemDragMove(at(5));  // sobre la cabecera
            expect(pumpMessagesUntil([&] { return viewport->getViewPositionY() == 0; }, 8000), "sobre la cabecera sube sola");
            queue->itemDragMove(at(table->getHeight() - 8));
            queue->itemDragExit(at(table->getHeight() + 100));
            const int afterExit = viewport->getViewPositionY();
            dispatchMessagesFor(300);
            expectEquals(viewport->getViewPositionY(), afterExit, "al salir de la cola deja de desplazarla");

            // La rueda durante el arrastre: JUCE se la manda a la lista de origen (la biblioteca)
            // y WheelRouter la pasa a la que está bajo el puntero.
            ui::OnScreen onScreen(screen.main());
            auto* libraryTable = ui::find<juce::TableListBox>(*ui::find<LibraryView>(screen.main()));
            auto* libraryViewport = libraryTable->getViewport();
            expect(libraryViewport->canScrollVertically());
            viewport->setViewPosition(0, 0);
            libraryViewport->setViewPosition(0, 0);

            WheelRouter router;
            router.begin(libraryTable);
            const juce::MouseWheelDetails down { 0.0f, -0.6f, false, false, false };
            auto wheelAt = [&](juce::Component& over) {
                const auto e = ui::mouseAt(*libraryTable, libraryTable->getLocalPoint(nullptr, over.getScreenBounds().getCentre()).toFloat());
                libraryViewport->useMouseWheelMoveIfNeeded(e.getEventRelativeTo(libraryViewport), down);  // lo que hace JUCE
                router.mouseWheelMove(e, down);
                dispatchMessagesFor(20);
            };
            wheelAt(*table);
            expectEquals(libraryViewport->getViewPositionY(), 0, "la biblioteca no se mueve");
            expect(viewport->getViewPositionY() > 0, "se mueve la cola, que está bajo el puntero");

            const int queueY = viewport->getViewPositionY();
            wheelAt(*libraryTable);
            expect(libraryViewport->getViewPositionY() > 0, "sobre la biblioteca se mueve ella");
            expectEquals(viewport->getViewPositionY(), queueY);
            router.end();
            expect(!router.isActive());
        }

        beginTest("Doble clic abre la ficha de la pista; el botón abre el generador");
        {
            ui::Screen screen("main");
            auto* queue = ui::find<PlaylistComponent>(screen.main());
            auto* table = ui::find<juce::TableListBox>(*queue);

            queue->cellDoubleClicked(1, 1, ui::mouseAt(*table, { 10.0f, 10.0f }, {}, 2));
            auto* modal = ui::waitForModal();
            auto* details = modal != nullptr ? ui::find<TrackDetailsDialog>(*modal) : nullptr;
            expect(details != nullptr, "se abre la ficha");
            if (details != nullptr)
            {
                details->onSave();
                details->onCancel();
            }
            pumpMessagesUntil([] { return juce::Component::getCurrentlyModalComponent() == nullptr; }, 3000);

            expect(ui::click(*queue, "generator_btn"));
            modal = ui::waitForModal();
            auto* generator = modal != nullptr ? ui::find<PlaylistGeneratorDialog>(*modal) : nullptr;
            expect(generator != nullptr, "se abre el generador");
            if (generator != nullptr)
            {
                generator->onGenerateSuccess();
                generator->onCancel();
            }
            pumpMessagesUntil([] { return juce::Component::getCurrentlyModalComponent() == nullptr; }, 3000);
        }
    }
};

class UiLibraryTests : public juce::UnitTest
{
public:
    UiLibraryTests() : juce::UnitTest("Interfaz: biblioteca, categorías e importación", "RAAADIO") {}

    // Lo que "elige" el usuario en los diálogos de archivos.
    juce::Array<juce::File> answer;

    // Contesta el diálogo de JUCE que se abrió escribiendo en su campo (si lo tiene).
    static bool answerWithText(const juce::String& field, const juce::String& text, int result)
    {
        auto* alert = dynamic_cast<juce::AlertWindow*>(ui::waitForModal());
        if (alert == nullptr) return false;
        if (auto* editor = alert->getTextEditor(field)) editor->setText(text);
        return answerModal(result);
    }

    void runTest() override
    {
        UiEnvironment environment;
        FileChoosers::setHandler([this](FileChoosers::Kind, const juce::String&,
                                        const std::function<void(const juce::Array<juce::File>&)>& onChosen) { onChosen(answer); });

        beginTest("Canciones: buscar, refrescar, menú de la fila, categorías, ficha y arrastrar");
        {
            ui::Screen screen("main");
            auto* view = ui::find<LibraryView>(screen.main());
            auto* table = ui::find<juce::TableListBox>(*view);
            expect(table != nullptr && table->getNumRows() > 3);
            juce::String searched;
            view->onSearchOnline = [&searched](const juce::String& query) { searched = query; };  // sin internet
            juce::TextEditor* search = nullptr;
            for (auto* child : view->getChildren())
                if (auto* box = dynamic_cast<juce::TextEditor*>(child)) search = box;
            expect(search != nullptr);

            ui::setText(*search, "zzzz no existe");
            expect(ui::draws(*view), "sin resultados");
            expect(ui::click(*view, "library_search_online") && searched == "zzzz no existe");
            expect(ui::click(*view, "tip_online_search"));
            ui::setText(*search, {});
            expect(ui::click(*view, "btn_refresh"));
            for (int column = 0; column < 12; ++column) view->getCellTooltip(0, column);

            juce::String category;
            for (const auto& c : screen.library().getCategories())
                if (!c.isSystem()) { category = MusicLibrary::getDisplayName(c); break; }
            auto menu = [&](int row) { view->cellClicked(row, 1, ui::mouseAt(*table, { 10.0f, 10.0f }, ui::rightButton)); };
            const auto queued = screen.playlist().getQueue().size();
            table->selectRow(0);
            menu(0);
            expect(ui::chooseMenuItem("menu_add_to_start"));
            menu(0);
            expect(ui::chooseMenuItem("menu_add_to_end"));
            expectEquals((int) screen.playlist().getQueue().size(), (int) queued + 2);
            menu(0);
            expect(ui::chooseSubmenuItem("menu_add_to_category", [&category](const juce::String& t) { return t == category; }), "agregar a una categoría");
            menu(0);
            expect(ui::chooseSubmenuItem("menu_remove_from_category", [&category](const juce::String& t) { return t == category; }), "y quitarla");
            menu(0);
            expect(ui::chooseMenuItem("menu_remove_from_all_categories"));
            menu(0);
            expect(ui::chooseMenuItem("menu_play_now"));
            ui::closeEverything();
            view->cellClicked(0, 1, ui::mouseAt(*table, { 10.0f, 10.0f }));
            table->selectRangeOfRows(0, 2);
            menu(1);
            expect(ui::chooseMenuItem("menu_add_to_end"), "varias a la vez");
            expect(ui::draws(*view));

            juce::SparseSet<int> rows;
            rows.addRange({ 0, 2 });
            expect(view->getDragSourceDescription(rows).toString().startsWith("track_list_"));

            view->cellDoubleClicked(0, 1, ui::mouseAt(*table, { 10.0f, 10.0f }, {}, 2));
            auto* modal = ui::waitForModal();
            auto* details = modal != nullptr ? ui::find<TrackDetailsDialog>(*modal) : nullptr;
            expect(details != nullptr, "doble clic abre la ficha");
            if (details != nullptr)
            {
                details->onSave();
                details->onCancel();
            }
            pumpMessagesUntil([] { return juce::Component::getCurrentlyModalComponent() == nullptr; }, 3000);
        }

        beginTest("Categorías: filtrar, crear, renombrar, formato de carpeta, quitar y reordenar");
        {
            ui::Screen screen("main");
            auto* sidebar = ui::find<CategorySidebarComponent>(screen.main());
            auto* list = ui::find<juce::ListBox>(*sidebar);
            auto editors = ui::findAll<juce::TextEditor>(*sidebar);
            expect(list != nullptr && editors.size() >= 2);
            auto categoryCount = [&screen] { return (int) screen.library().getCategories().size(); };
            const int start = categoryCount();

            ui::setText(*editors[0], "sal");
            expect(ui::draws(*sidebar), "filtrado");
            ui::setText(*editors[0], {});
            ui::setText(*editors[1], "nueva prueba");
            editors[1]->onReturnKey();
            ui::setText(*editors[1], "otra prueba");
            expect(ui::click(*sidebar, "tip_add_category"));
            expectEquals(categoryCount(), start + 2, "se crean con Enter o con el botón");

            const int rows = list->getModel()->getNumRows();
            int userRow = -1;
            for (int row = 0; row < rows; ++row)
            {
                sidebar->getTooltipForRow(row);
                sidebar->listBoxItemClicked(row, ui::mouseAt(*list, { 5.0f, 5.0f }));
                if (userRow < 0 && row > 1 && sidebar->getTooltipForRow(row).isEmpty()) userRow = row;
            }
            expect(userRow > 1, "hay una categoría del usuario");
            sidebar->listBoxItemClicked(2, ui::mouseAt(*list, { 5.0f, 5.0f }, ui::rightButton));  // fija: sin menú
            ui::closeEverything();

            sidebar->listBoxItemClicked(userRow, ui::mouseAt(*list, { 5.0f, 5.0f }, ui::rightButton));
            expect(ui::chooseMenuItem("rename"));
            expect(answerWithText("name", "renombrada prueba", 1), "renombrar");
            sidebar->listBoxItemClicked(userRow, ui::mouseAt(*list, { 5.0f, 5.0f }, ui::rightButton));
            expect(ui::chooseMenuItem("rename"));
            expect(answerWithText("name", "no importa", 0), "renombrar se cancela");

            sidebar->listBoxItemDoubleClicked(userRow, ui::mouseAt(*list, { 5.0f, 5.0f }, {}, 2));
            expect(answerModal(1), "formato de carpeta: artista");
            sidebar->listBoxItemDoubleClicked(userRow, ui::mouseAt(*list, { 5.0f, 5.0f }, {}, 2));
            expect(answerModal(-1), "o cancelar");
            sidebar->listBoxItemDoubleClicked(9999, ui::mouseAt(*list, { 5.0f, 5.0f }, {}, 2));

            sidebar->deleteKeyPressed(userRow);
            expect(answerModal(0), "quitar pregunta");
            sidebar->listBoxItemClicked(userRow, ui::mouseAt(*list, { 5.0f, 5.0f }, ui::rightButton));
            expect(ui::chooseMenuItem("menu_delete_category"));
            expect(answerModal(1));
            expectEquals(categoryCount(), start + 1, "se quitó una");
            sidebar->returnKeyPressed(userRow);

            juce::SparseSet<int> selected;
            selected.addRange({ userRow, userRow + 1 });
            const auto drag = sidebar->getDragSourceDescription(selected);
            expect(drag.toString().startsWith("category_drag_"));
            expect(sidebar->getDragSourceDescription(juce::SparseSet<int>()).isVoid());
            using Source = juce::DragAndDropTarget::SourceDetails;
            expect(sidebar->isInterestedInDragSource(Source(drag, nullptr, {})));
            for (const int y : { 5, list->getHeight() / 2, list->getHeight() - 5, list->getHeight() + 40 })
            {
                sidebar->itemDragMove(Source(drag, nullptr, { 20, list->getY() + y }));
                expect(ui::draws(*sidebar));
            }
            sidebar->itemDragExit(Source(drag, nullptr, {}));
            sidebar->itemDragMove(Source(drag, nullptr, { 20, list->getY() + list->getHeight() - 5 }));
            sidebar->itemDropped(Source(drag, nullptr, { 20, list->getY() + list->getHeight() - 5 }));
            ui::setText(*editors[0], "x");
            expect(sidebar->getDragSourceDescription(selected).isVoid(), "con el filtro activo no se reordena");
        }

        beginTest("Importar: la ventana avanzada (archivos y carpeta), las cuñas y la revisión al terminar");
        {
            ui::Screen screen("main");
            auto* libraryComponent = ui::find<LibraryComponent>(screen.main());
            TempLab outside;
            const auto song = writeWav(outside.root.getChildFile("NUEVAS/TEMA NUEVO - GRUPO NUEVO.wav"), 2.0,
                                       [](double t) { return tone(t, 0.3f, 700.0); });
            const auto spot = writeWav(outside.root.getChildFile("CUNA DEL MES.wav"), 2.0,
                                       [](double t) { return tone(t, 0.3f, 900.0); });

            answer = {};
            expect(ui::click(*libraryComponent, "advanced_import_btn"));
            auto* modal = ui::waitForModal();
            expect(modal != nullptr && ui::find<AdvancedImportDialog>(*modal) != nullptr);
            expect(ui::click(*modal, "label_files"), "cancelar el diálogo no importa nada");
            answer = { song.getParentDirectory() };
            expect(ui::click(*modal, "label_folder"), "una carpeta");
            pumpMessagesUntil([] { return juce::Component::getCurrentlyModalComponent() == nullptr; }, 3000);
            pumpMessagesUntil([&] { return !screen.library().isImporting(); }, 20000);

            expect(ui::click(*libraryComponent, "advanced_import_btn"));
            modal = ui::waitForModal();
            answer = { song };
            expect(modal != nullptr && ui::click(*modal, "label_files"), "o archivos sueltos");
            pumpMessagesUntil([] { return juce::Component::getCurrentlyModalComponent() == nullptr; }, 3000);

            answer = { spot };
            expect(ui::click(*libraryComponent, "tip_import_ads"), "las cuñas");
            pumpMessagesUntil([&] { return !screen.library().isImporting(); }, 20000);
            dispatchMessagesFor(600);  // el temporizador muestra el resumen y abre la revisión
            expect(ui::draws(*libraryComponent));

            expect(ui::click(*libraryComponent, "tip_review_button"), "abre (o trae al frente) la revisión");
            dispatchMessagesFor(50);
            expect(ui::click(*libraryComponent, "cancel"));
        }

        ui::cancelFileChoosers();
    }
};

class UiAdsTests : public juce::UnitTest
{
public:
    UiAdsTests() : juce::UnitTest("Interfaz: publicidad y botonera", "RAAADIO") {}

    void runTest() override
    {
        UiEnvironment environment;
        using Source = juce::DragAndDropTarget::SourceDetails;

        beginTest("Lista de cuñas: número de cuña, menú de la fila, ficha y arrastrar");
        {
            ui::Screen screen("ads");
            auto* ads = ui::find<AdsListView>(screen.main());
            auto* table = ads != nullptr ? ui::find<juce::TableListBox>(*ads) : nullptr;
            expect(table != nullptr && table->getNumRows() > 1);
            const int rows = table->getNumRows();
            const auto mouse = ui::mouseAt(*table, { 10.0f, 10.0f });

            // El número de cada cuña (las identificaciones no llevan número: avisa).
            const juce::String numbers[] = { "7", "", "12", "5" };
            for (int row = 0; row < rows; ++row)
            {
                ads->cellDoubleClicked(row, 1, mouse);
                auto* alert = dynamic_cast<juce::AlertWindow*>(ui::waitForModal());
                expect(alert != nullptr);
                if (alert != nullptr && alert->getTextEditor("number") != nullptr)
                {
                    alert->getTextEditor("number")->setText(numbers[row % 4]);
                    expect(answerModal(row % 5 == 4 ? 0 : 1));
                }
                else
                    expect(answerModal(1));
            }

            juce::String category;
            for (const auto& c : screen.library().getCategories())
                if (!c.isSystem()) { category = MusicLibrary::getDisplayName(c); break; }
            auto menu = [&](int row) { ads->cellClicked(row, 2, ui::mouseAt(*table, { 10.0f, 10.0f }, ui::rightButton)); };
            const auto queued = screen.playlist().getQueue().size();
            menu(0);
            expect(ui::chooseMenuItem("menu_add_to_start"));
            menu(0);
            expect(ui::chooseMenuItem("menu_add_to_end"));
            expectEquals((int) screen.playlist().getQueue().size(), (int) queued + 2);
            menu(0);
            expect(ui::chooseSubmenuItem("menu_add_to_category", [&category](const juce::String& t) { return t == category; }));
            menu(0);
            expect(ui::chooseSubmenuItem("menu_remove_from_category", [&category](const juce::String& t) { return t == category; }));
            menu(0);
            expect(ui::chooseMenuItem("menu_remove_from_all_categories"));
            bool edited = false;  // las identificaciones no tienen número: no traen la opción
            for (int row = 0; row < rows && !edited; ++row)
            {
                table->selectRow(row);
                menu(row);
                edited = ui::chooseMenuItem("menu_edit_cue", 300);
                ui::closeEverything();
            }
            expect(edited, "asignar el número desde el menú");
            menu(0);
            expect(ui::chooseSubmenuItem("menu_assign_cart", [](const juce::String& t) { return t.startsWith("F2"); }), "a la botonera");
            menu(0);
            expect(ui::chooseMenuItem("menu_play_now"));
            ui::closeEverything();
            ads->cellClicked(0, 2, mouse);
            ads->cellClicked(rows + 5, 2, ui::mouseAt(*table, { 10.0f, 10.0f }, ui::rightButton));
            ui::closeEverything();

            juce::SparseSet<int> selected;
            selected.addRange({ 0, 2 });
            expect(ads->getDragSourceDescription(selected).toString().startsWith("track_list_"));
            expect(ads->getDragSourceDescription(juce::SparseSet<int>()).isVoid());

            ads->cellDoubleClicked(0, 3, ui::mouseAt(*table, { 10.0f, 10.0f }, {}, 2));
            auto* modal = ui::waitForModal();
            auto* details = modal != nullptr ? ui::find<TrackDetailsDialog>(*modal) : nullptr;
            expect(details != nullptr, "la ficha de la cuña");
            if (details != nullptr)
            {
                details->onSave();
                details->onCancel();
            }
            pumpMessagesUntil([] { return juce::Component::getCurrentlyModalComponent() == nullptr; }, 3000);
            expect(ui::click(*ads, "btn_refresh_list"));
            expect(ui::draws(*ads));
        }

        beginTest("Pauta: la automática, las franjas de un número y las tandas a mano");
        {
            ui::Screen screen("ads");
            auto* schedule = ui::find<ScheduleView>(screen.main());
            auto* table = ui::find<juce::TableListBox>(*schedule);
            auto* number = ui::find<juce::TextEditor>(*schedule);
            expect(schedule != nullptr && table != nullptr && number != nullptr);
            const auto mouse = ui::mouseAt(*table, { 10.0f, 10.0f });

            expect(ui::click(*schedule, "schedule_auto"));
            expect(ui::click(*schedule, "schedule_auto"));
            ui::setText(*number, {});
            schedule->cellClicked(0, 3, mouse);  // sin número no se marca nada
            ui::setText(*number, "7");
            for (int row = 0; row < 4; ++row)
            {
                schedule->cellClicked(row, 3, mouse);
                schedule->cellClicked(row, 1, mouse);
            }
            schedule->cellClicked(1, 3, mouse);  // y se desmarca
            schedule->cellClicked(9999, 3, mouse);
            schedule->setSelectedCueNumber(12);
            schedule->setSelectedCueNumber(-1);
            table->updateContent();
            expect(ui::draws(*schedule));

            int pressed = 0;
            for (auto& b : ui::safe(ui::findAll<juce::Button>(*schedule)))
                if (b != nullptr && b->getTooltip().startsWith(TRANS("schedule_generate")) && pressed < 6)
                {
                    b->triggerClick();
                    dispatchMessagesFor(20);
                    ui::closeEverything();  // una franja sin cuñas avisa
                    ++pressed;
                }
            expect(pressed > 0, "insertar tandas a mano");
        }

        beginTest("Botonera: asignar soltando pistas, tocar y detener, quitar con el menú, y la pestaña llena");
        {
            ui::Screen screen("cartwall");
            auto* cartwall = ui::find<CartwallView>(screen.main());
            expect(cartwall != nullptr);
            const juce::var drag("track_list_" + juce::String(screen.engine().getDeck(0).getCurrentTrack().path));
            expect(cartwall->isInterestedInDragSource(Source(drag, nullptr, {})));
            expect(!cartwall->isInterestedInDragSource(Source(juce::var("nada"), nullptr, {})));

            std::vector<juce::Point<int>> points;
            for (int y = 5; y < cartwall->getHeight(); y += 30)
                for (int x = 5; x < cartwall->getWidth(); x += 45)
                    points.push_back({ x, y });
            for (const auto& p : points)
            {
                cartwall->itemDragMove(Source(drag, nullptr, p));
                cartwall->itemDropped(Source(drag, nullptr, p));
                cartwall->mouseMove(ui::mouseAt(*cartwall, p.toFloat()));
            }
            cartwall->itemDragExit(Source(drag, nullptr, {}));
            expect(ui::draws(*cartwall), "los cartuchos asignados");

            for (const auto& p : points) cartwall->mouseDown(ui::mouseAt(*cartwall, p.toFloat()));
            dispatchMessagesFor(100);
            expect(ui::draws(*cartwall), "sonando");
            for (const auto& p : points) cartwall->mouseDown(ui::mouseAt(*cartwall, p.toFloat()));
            cartwall->mouseExit(ui::mouseAt(*cartwall, { 0.0f, 0.0f }));

            {
                // El menú del cartucho se ancla a la botonera: tiene que estar en una ventana.
                ui::OnScreen onScreen(screen.main());
                bool removed = false;
                for (const auto& p : points)
                {
                    cartwall->mouseDown(ui::mouseAt(*cartwall, p.toFloat(), ui::rightButton));
                    if (ui::chooseMenuItem("cartwall_remove", 300)) { removed = true; break; }
                }
                expect(removed, "quitar un cartucho");

                // F1 a F12 tocan los cartuchos; con un modificador, no.
                expect(onScreen.pressKey(juce::KeyPress(juce::KeyPress::F1Key)), "F1");
                expect(onScreen.pressKey(juce::KeyPress(juce::KeyPress::F1Key)), "otra vez F1 lo detiene");
                expect(onScreen.pressKey(juce::KeyPress(juce::KeyPress::F12Key)), "F12");
                onScreen.pressKey(juce::KeyPress(juce::KeyPress::F2Key, juce::ModifierKeys::shiftModifier, 0));
                onScreen.pressKey(juce::KeyPress('a'));
            }

            // Soltar sobre la pestaña: el primer cartucho libre; con todos ocupados, avisa.
            auto* tabs = ui::find<juce::TabbedComponent>(screen.main());
            auto& bar = tabs->getTabbedButtonBar();
            // La pestaña de la botonera por su nombre (después vienen los temporales).
            auto* cartTab = dynamic_cast<juce::DragAndDropTarget*>(bar.getTabButton(tabs->getTabNames().indexOf(TRANS("tab_cartwall"))));
            expect(cartTab != nullptr && cartTab->isInterestedInDragSource(Source(drag, nullptr, {})));
            for (int i = 0; i < CartPlayer::numCarts + 1; ++i)
            {
                cartTab->itemDragEnter(Source(drag, nullptr, {}));
                cartTab->itemDropped(Source(drag, nullptr, {}));
            }
            expect(answerModal(1), "con la botonera llena avisa");
            cartTab->itemDragExit(Source(drag, nullptr, {}));
        }
    }
};

class UiConsoleTests : public juce::UnitTest
{
public:
    UiConsoleTests() : juce::UnitTest("Interfaz: decks, mezclador, grabadora y barra de estado", "RAAADIO") {}

    void runTest() override
    {
        UiEnvironment environment;
        using Source = juce::DragAndDropTarget::SourceDetails;

        beginTest("Decks: soltar archivos y pistas, al aire no se reemplaza, sus controles y la forma de onda");
        {
            ui::Screen screen("main");
            auto decks = ui::findAll<DeckComponent>(screen.main());
            expect(decks.size() == 2);
            const juce::String audio(screen.engine().getDeck(0).getCurrentTrack().path);
            const juce::String queued(screen.playlist().getQueue().front().path);
            TempLab outside;
            const auto notAudio = outside.touch("NO ES AUDIO.txt");

            // El deck que no está al aire acepta lo que se le suelte.
            const bool firstOnAir = OnAir::isOnAir(screen.engine().getDeck(0));
            auto* free = firstOnAir ? decks[1] : decks[0];
            auto* onAir = firstOnAir ? decks[0] : decks[1];
            expect(free->isInterestedInFileDrag({}));
            free->fileDragEnter({}, 0, 0);
            expect(ui::draws(*free), "resaltado mientras se arrastra");
            free->fileDragExit({});
            free->filesDropped({ notAudio.getFullPathName() }, 0, 0);
            free->filesDropped({}, 0, 0);
            free->filesDropped({ audio }, 0, 0);
            for (const auto& description : { "track_path_" + audio, "track_list_" + audio, "playlist_drag_0:" + queued,
                                              juce::String("otra cosa") })
            {
                const Source source(juce::var(description), nullptr, {});
                free->isInterestedInDragSource(source);
                free->itemDragEnter(source);
                free->itemDragExit(source);
                free->itemDropped(source);
            }
            expect(ui::draws(*free));

            onAir->filesDropped({ audio }, 0, 0);
            expect(answerModal(1), "al aire no se reemplaza la pista: avisa");
            onAir->itemDropped(Source(juce::var("track_list_" + audio), nullptr, {}));
            expect(answerModal(1));

            for (auto* deck : decks)
            {
                ui::exercise(*deck, {});
                expect(ui::draws(*deck));
            }

            for (auto* waveform : ui::findAll<WaveformComponent>(screen.main()))
            {
                waveform->setCueRegion(1.0, 20.0);
                waveform->setEndWarningSeconds(30.0);
                waveform->setScalingFactor(2.0f);
                for (const float x : { 0.1f, 0.5f, 0.9f })
                {
                    const auto e = ui::mouseAt(*waveform, { (float) waveform->getWidth() * x, (float) waveform->getHeight() / 2.0f });
                    waveform->mouseMove(e);
                    waveform->mouseDown(e);
                    waveform->mouseDrag(e);
                }
                waveform->setPosition(0.25);
                expect(ui::draws(*waveform));
            }
        }

        beginTest("Mezclador, grabadora, historial y barra de estado");
        {
            ui::Screen screen("main");
            auto* mixer = ui::find<MixerComponent>(screen.main());
            expect(mixer != nullptr);
            ui::exercise(*mixer, {});
            expect(ui::draws(*mixer));

            auto* recorder = ui::find<RecordingPanel>(screen.main());
            expect(recorder != nullptr);
            for (const auto* key : { "tip_record", "rec_btn_pause", "rec_btn_resume", "tip_record_save" })
            {
                ui::click(*recorder, key);
                dispatchMessagesFor(150);
                ui::closeEverything();
                expect(ui::draws(*recorder), key);
            }
            ui::exercise(*recorder, {});

            if (auto* history = ui::find<HistoryView>(screen.main()))
            {
                ui::exercise(*history, {});
                expect(ui::draws(*history));
            }

            auto* status = ui::find<StatusBar>(screen.main());
            expect(status != nullptr);
            int asked = 0;
            status->onSettingsRequested = [&asked] { ++asked; };
            expect(ui::click(*status, "settings") && asked == 1);
            for (int x = 0; x < status->getWidth(); x += 40)
                status->mouseUp(ui::mouseAt(*status, { (float) x, (float) status->getHeight() / 2.0f }));
            status->getTooltip();
            expect(ui::draws(*status));
        }
    }
};

class UiDialogTests : public juce::UnitTest
{
public:
    UiDialogTests() : juce::UnitTest("Interfaz: generador, ficha, artistas, búsqueda en internet y arranque", "RAAADIO") {}

    void runTest() override
    {
        UiEnvironment environment;

        beginTest("Generador: modos, reglas, categorías, plantillas, generar y deshacer");
        {
            ui::Screen screen("generator");
            auto& dialog = dynamic_cast<PlaylistGeneratorDialog&>(screen.view());
            int generated = 0, cancelled = 0;
            dialog.onGenerateSuccess = [&generated] { ++generated; };
            dialog.onCancel = [&cancelled] { ++cancelled; };

            expect(ui::click(dialog, "gen_mode_advanced"));
            ui::exercise(dialog, { "gen_tpl_save", "gen_tpl_delete" }, "7");
            expect(generated > 0 && cancelled > 0, "generar y cerrar");

            if (auto* list = ui::find<juce::ListBox>(dialog))
            {
                auto* model = list->getListBoxModel();
                for (int row = 0; row < model->getNumRows(); ++row)
                {
                    model->listBoxItemClicked(row, ui::mouseAt(*list, { 30.0f, 5.0f }));
                    model->listBoxItemClicked(row, ui::mouseAt(*list, { 30.0f, 5.0f }, ui::rightButton));
                    model->listBoxItemClicked(row, ui::mouseAt(*list, { (float) list->getWidth() - 25.0f, 5.0f }));
                }
                model->listBoxItemClicked(9999, ui::mouseAt(*list, { 5.0f, 5.0f }));
            }

            expect(ui::click(dialog, "gen_tpl_save"));
            expect(ui::answerWithText("name", "PLANTILLA DE PRUEBA", 1), "guardar una plantilla");
            expect(ui::click(dialog, "gen_tpl_save"));
            expect(ui::answerWithText("name", "  ", 1), "sin nombre no se guarda");
            expect(ui::click(dialog, "gen_tpl_save"));
            expect(answerModal(0));
            expect(ui::click(dialog, "gen_tpl_delete"));
            expect(answerModal(0), "quitar la plantilla pregunta");
            expect(ui::click(dialog, "gen_tpl_delete"));
            expect(answerModal(1));

            expect(ui::click(dialog, "gen_btn_generate2") || ui::click(dialog, "gen_btn_replace"));
            expect(ui::click(dialog, "gen_btn_undo"), "deshacer lo generado");
            expect(ui::click(dialog, "gen_mode_simple"));
            ui::exercise(dialog, { "gen_tpl_save", "gen_tpl_delete" }, "2");
            expect(ui::draws(dialog));
        }

        beginTest("Ficha de la pista: categorías, guardar, abrir el archivo, actualizar y borrar");
        {
            ui::Screen screen("track");
            auto& details = dynamic_cast<TrackDetailsDialog&>(screen.view());
            int saved = 0;
            details.onSave = [&saved] { ++saved; };
            ui::externalActions().clear();
            ui::exercise(details, { "btn_delete" }, "Nuevo Valor", 1);
            expect(ui::externalActions().size() == 1 && ui::externalActions()[0].startsWith(juce::String((int) External::Action::OpenFile)),
                   "abrir la pista con el reproductor del sistema (anotado, no se abre)");
            expect(saved > 0, "guardar");
            expect(ui::click(details, "btn_delete"));
            expect(answerModal(0), "borrar pregunta");
            expect(ui::click(details, "btn_delete"));
            ui::closeEverything(1);
            expect(ui::draws(details));
        }

        beginTest("Artistas: buscar, filtros, renombrar, fusionar los parecidos y \"no es el mismo\"");
        {
            ui::Screen screen("artists");
            auto& artists = dynamic_cast<ArtistsView&>(screen.view());
            int review = 0, closed = 0, changed = 0;
            artists.onShowReview = [&review](ImportReview::Scope) { ++review; };
            artists.onCloseRequested = [&closed] { ++closed; };
            artists.onLibraryChanged = [&changed] { ++changed; };
            ui::exercise(artists, {}, "Armando", 0);
            ui::exercise(artists, {}, "Armando Manzanero", 1);
            expect(review > 0 && closed > 0, "cambiar de pestaña y cerrar");

            // Renombrar: el artista elegido, un nombre nuevo y Enter (el recorrido de arriba
            // escribe antes en el buscador, que vuelve a filtrar y deja sin artista elegido).
            artists.reload();
            expect(ui::click(artists, "review_filter_all"), "todos (el recorrido dejó el último filtro)");
            auto* list = ui::find<juce::TableListBox>(artists);
            expect(list != nullptr && list->getNumRows() > 0);
            list->selectRow(0);
            dispatchMessagesFor(20);
            auto* name = ui::find<juce::TextEditor>(artists, [](juce::TextEditor& e) {
                return ui::shown(e) && e.getText().isNotEmpty(); });
            expect(name != nullptr, "el nombre del artista elegido");
            if (name != nullptr)
            {
                const int before = changed;
                ui::setText(*name, name->getText() + " Renombrado");
                name->onReturnKey();
                ui::closeEverything(1);
                expect(changed > before, "renombrar cambió la biblioteca");
            }
            expect(ui::draws(artists));
        }

        beginTest("Búsqueda en internet (sin conexión): resultados, videos, enlaces, copiar y descargar");
        {
            ui::Screen screen("online-search");
            auto& search = dynamic_cast<OnlineSearchView&>(screen.view());
            int closed = 0, review = 0;
            search.onCloseRequested = [&closed] { ++closed; };
            search.onOpenReview = [&review] { ++review; };
            pumpMessagesUntil([&] { return ui::draws(search) && ui::find<juce::ListBox>(search) != nullptr; }, 5000);
            dispatchMessagesFor(300);
            ui::externalActions().clear();
            ui::exercise(search, {}, "Juan Luis Guerra", 1);
            dispatchMessagesFor(300);
            ui::exercise(search, {}, "Juan Luis Guerra", 0);
            expect(ui::externalActions().size() > 2, "abrir YouTube, Deezer y Spotify y copiar el enlace (anotado)");
            expect(closed > 0);
            search.keyPressed(juce::KeyPress('c', juce::ModifierKeys::commandModifier, 0));
            search.keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
            search.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
            search.search("Burbujas de amor");
            dispatchMessagesFor(500);
            expect(ui::draws(search));
        }

        beginTest("Al abrir: una restauración pendiente se aplica y avisa; una base dañada se recupera del respaldo");
        {
            juce::File backup;
            {
                ui::Screen first("empty");
                expect(PersistenceManager::createBackup([&first](const juce::File& target) {
                    return first.library().backupDatabaseTo(target); }, &backup).wasOk());
                expect(PersistenceManager::stageRestore(backup).wasOk());
                // La misma emisora, abierta otra vez.
                PersistenceManager::setLabRootOverride(first.station);
                UiSnapshotRunner::Options options;
                options.output = first.lab.root.getChildFile("x.png");
                options.view = "empty";
                {
                    UiSnapshotRunner reopened(options);
                    expect(answerModal(1, 5000), "avisa que se restauró");
                }
                // Una base dañada, con su respaldo verificado al lado.
                first.station.getChildFile("Database/raaadio.db").replaceWithText("esto no es una base de datos");
                {
                    UiSnapshotRunner damaged(options);
                    expect(answerModal(1, 5000), "avisa que la base se recuperó del respaldo");
                }
            }
        }

        beginTest("La ventana principal: configuración desde la barra de estado y las teclas de la ventana");
        {
            ui::Screen screen("main");
            auto* status = ui::find<StatusBar>(screen.main());
            expect(ui::click(*status, "settings"));
            auto* modal = ui::waitForModal();
            auto* settings = modal != nullptr ? ui::find<SettingsDialog>(*modal) : nullptr;
            expect(settings != nullptr, "se abre la configuración");
            if (settings != nullptr && settings->onLibraryReset) settings->onLibraryReset();
            ui::closeEverything();
            expect(screen.playlist().getQueue().empty(), "borrar la biblioteca vacía la cola");

            ui::OnScreen onScreen(screen.main());
            onScreen.pressKey(juce::KeyPress(juce::KeyPress::F5Key));
            expect(ui::draws(screen.main()));
        }
    }
};

class UiTemporariesTests : public juce::UnitTest
{
public:
    UiTemporariesTests() : juce::UnitTest("Interfaz: audios temporales", "RAAADIO") {}

    juce::Array<juce::File> answer;  // lo que "elige" el usuario en el diálogo de archivos

    void runTest() override
    {
        UiEnvironment environment;
        FileChoosers::setHandler([this](FileChoosers::Kind, const juce::String&,
                                        const std::function<void(const juce::Array<juce::File>&)>& onChosen) { onChosen(answer); });

        beginTest("Importar temporal: el botón junto a Importar cuñas los agrega y abre la pestaña");
        {
            ui::Screen screen("main");
            auto& lib = screen.library();
            const int before = (int) lib.getTemporaries().size();
            TempLab outside;
            answer = { TemporaryAudioTests::writeMessage(outside.root.getChildFile("Saludo nuevo.wav"), 4.0) };
            expect(ui::click(screen.main(), "tip_import_temporal"), "el botón está en la fila de las pestañas");
            expect(pumpMessagesUntil([&] { return !lib.isImporting() && (int) lib.getTemporaries().size() == before + 1; }, 10000));
            auto* tabs = ui::find<DropSwitchingTabs>(screen.main());
            expect(tabs != nullptr && tabs->getCurrentTabIndex() == 4, "se abre la pestaña Temporales");
            auto* view = ui::find<TemporariesView>(screen.main());
            auto* table = view != nullptr ? ui::find<juce::TableListBox>(*view) : nullptr;
            expect(pumpMessagesUntil([&] { return table != nullptr && table->getNumRows() == before + 1; }, 3000), "y aparece en la lista");
            expect(ui::draws(screen.main()));
            answer = {};
        }

        beginTest("Desde la lista: a la cola; lo que está en la cola no se borra; Vaciar borra lo demás");
        {
            ui::Screen screen("temporaries");
            auto& lib = screen.library();
            auto* view = ui::find<TemporariesView>(screen.main());
            auto* table = view != nullptr ? ui::find<juce::TableListBox>(*view) : nullptr;
            expect(table != nullptr && table->getNumRows() == 4);
            if (view == nullptr || table == nullptr) return;
            for (int column = 0; column < 7; ++column) view->getCellTooltip(0, column);

            // La demostración: el mensaje de la JAC ya está en la cola; el saludo (fila 0), no.
            const auto rows = lib.getTemporaries();
            table->selectRow(0);
            const auto queued = screen.playlist().getQueue().size();
            expect(ui::click(*view, "tip_temp_queue"));
            expect(ui::chooseMenuItem("temp_queue_end"));
            expectEquals((int) screen.playlist().getQueue().size(), (int) queued + 1);

            expect(ui::click(*view, "tip_temp_remove"));
            ui::closeEverything();  // el aviso de por qué no se puede
            expectEquals((int) lib.getTemporaries().size(), 4, "lo que está en la cola se queda");

            expect(ui::click(*view, "tip_temp_empty"));
            expect(answerModal(1), "Vaciar pregunta antes");
            const auto left = lib.getTemporaries();
            expectEquals((int) left.size(), 2, "quedan los dos que están en la cola");
            for (const auto& t : rows) {
                const bool kept = std::any_of(left.begin(), left.end(), [&t](const TrackInfo& k) { return k.id == t.id; });
                expect(kept == juce::File(juce::String::fromUTF8(t.path.c_str())).existsAsFile(), "lo borrado se borra del disco");
            }
            expect(pumpMessagesUntil([&] { return table->getNumRows() == 2; }, 3000));
            expect(ui::draws(*view));
        }

        beginTest("Pegar un enlace: se ve lo que baja y se puede cancelar; lo que no es un enlace solo avisa");
        {
            ui::Screen screen("temporaries-link");
            auto* view = ui::find<TemporariesView>(screen.main());
            expect(view != nullptr);
            if (view == nullptr) return;
            auto cancelVisible = [&] {
                auto* b = ui::button(*view, "online_download_cancel_short");
                return b != nullptr && b->isVisible();
            };
            expect(pumpMessagesUntil(cancelVisible, 5000), "la descarga en curso se puede cancelar");
            expect(ui::draws(*view));

            auto* editor = ui::find<juce::TextEditor>(*view);
            expect(editor != nullptr && editor->isEnabled());
            if (editor != nullptr) ui::setText(*editor, "esto no es un enlace");
            expect(ui::click(*view, "temp_link_download"));
            expect(editor != nullptr && editor->getText() == "esto no es un enlace", "se queda para corregirlo");

            expect(ui::click(*view, "online_download_cancel_short"));
            expect(pumpMessagesUntil([&] { return !cancelVisible(); }, 5000), "cancelada");
        }
    }
};

class UiViewTests : public juce::UnitTest
{
public:
    UiViewTests() : juce::UnitTest("Interfaz: cada vista se construye y se dibuja", "RAAADIO") {}

    // Una captura como la de --ui-snapshot, en un laboratorio propio.
    bool render(const juce::String& view, const juce::String& theme, const juce::String& language, juce::int64& pngSize)
    {
        TempLab lab;
        PersistenceManager::setLabRootOverride(lab.root);
        UiSnapshotRunner::Options options;
        options.output = lab.root.getChildFile(view + ".png");
        options.width = 1366;
        options.height = 768;
        options.view = view;
        options.theme = theme;
        options.language = language;

        bool finished = false, ok = false;
        {
            UiSnapshotRunner runner(options);
            runner.onFinished = [&](bool success) { finished = true; ok = success; };
            if (!pumpMessagesUntil([&] { return finished; }, 40000)) return false;
        }
        pngSize = options.output.getSize();
        return ok;
    }

    void runTest() override
    {
        UiEnvironment ui;
        beginTest("Todas las vistas de --ui-snapshot se construyen y se dibujan");
        for (const auto* view : { "main", "empty", "history", "ads", "cartwall", "settings", "settings-library",
                                  "settings-library-2", "settings-broadcast", "settings-broadcast-2", "settings-language",
                                  "settings-about", "generator", "generator-advanced", "generator-done",
                                  "generator-replace", "track", "import", "review", "review-library", "artists",
                                  "library-online", "online-search", "temporaries", "temporaries-link" })
        {
            juce::int64 size = 0;
            expect(render(view, "dark", "es", size), view);
            expect(size > 5000, juce::String(view) + ": la imagen no está vacía");
        }

        beginTest("El tema claro y el inglés también se dibujan");
        for (const auto* view : { "main", "settings", "review", "generator" })
        {
            juce::int64 size = 0;
            ThemeManager::getInstance().setTheme(ThemeManager::Theme::Light, false);
            LanguageManager::getInstance().setLanguage("en", false);
            expect(render(view, "light", "en", size), view);
            expect(size > 5000, view);
        }
    }
};

//==============================================================================

static PortablePathTests portablePathTests;
static PlayLogTests playLogTests;
static PlaylistTests playlistTests;
static PersistenceTests persistenceTests;
static BackupTests backupTests;
static MixerCueTests mixerCueTests;
static TrackAnalyzerTests trackAnalyzerTests;
static TagReaderTests tagReaderTests;
static TrackNamingTests trackNamingTests;
static ImportTests importTests;
static ImportReviewTests importReviewTests;
static AcousticFingerprintTests acousticFingerprintTests;
static CatalogToolsTests catalogToolsTests;
static OnlineIdentityTests onlineIdentityTests;
static SongSearchTests songSearchTests;
static SongDownloadTests songDownloadTests;
static ConversionTests conversionTests;
static DeckCueTests deckCueTests;
static GeneratorTests generatorTests;
static CategoryTests categoryTests;
static QueueTimingTests queueTimingTests;
static OnAirTests onAirTests;
static AutoMixTests autoMixTests;
static DeadAirTests deadAirTests;
static AirLogTests airLogTests;
static CartwallTests cartwallTests;
static StreamingTests streamingTests;
static StationSettingsTests stationSettingsTests;
static NowPlayingTests nowPlayingTests;
static StationDataTests stationDataTests;
static StationSafetyTests stationSafetyTests;
static PreviewTests previewTests;
static HttpTransportTests httpTransportTests;
static DeadAirGuardTests deadAirGuardTests;
static TemporaryAudioTests temporaryAudioTests;
// Al final: dejan el idioma y el aspecto como estaban.
static UiReviewTests uiReviewTests;
static UiSettingsTests uiSettingsTests;
static UiQueueTests uiQueueTests;
static UiLibraryTests uiLibraryTests;
static UiAdsTests uiAdsTests;
static UiTemporariesTests uiTemporariesTests;
static UiConsoleTests uiConsoleTests;
static UiDialogTests uiDialogTests;
static UiViewTests uiViewTests;

#if JUCE_WINDOWS
extern "C" __declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int codePage);
#endif

class ConsoleRunner : public juce::UnitTestRunner
{
    void logMessage(const juce::String& message) override
    {
        std::cout << message << '\n' << std::flush;  // a la vista aunque una prueba se cuelgue
    }
};

int main()
{
    installCrashReport();
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Ninguna prueba toca las preferencias del usuario (idioma, tema, dispositivos de audio y
    // carpeta de la emisora): todas van a una carpeta temporal.
    TempLab preferences;
    AppSettings::setFolderOverride(preferences.root);

    // Nada se abre ni se copia en el equipo: el navegador, el Explorador, el reproductor y el
    // portapapeles quedan anotados (ui::externalActions), y los diálogos de archivos
    // contestan "Cancelar" salvo en las pruebas que eligen algo.
    External::setHandler([](External::Action action, const juce::String& target) {
        ui::externalActions().add(juce::String((int) action) + ":" + target);
    });
    ui::cancelFileChoosers();

   #if JUCE_WINDOWS
    SetConsoleOutputCP(65001);  // UTF-8, para que las tildes se vean bien en la consola
   #endif

    ConsoleRunner runner;
    runner.setAssertOnFailure(false);
    // RAAADIO_TESTS=texto: solo las pruebas cuyo nombre lo contiene (para repetir una sin
    // esperar la suite completa).
    const auto only = juce::SystemStats::getEnvironmentVariable("RAAADIO_TESTS", {});
    if (only.isNotEmpty())
    {
        juce::Array<juce::UnitTest*> chosen;
        for (auto* test : juce::UnitTest::getTestsInCategory("RAAADIO"))
            if (test->getName().containsIgnoreCase(only)) chosen.add(test);
        std::cout << "Solo " << chosen.size() << " clase(s) de pruebas con \"" << only << "\"\n";
        runner.runTests(chosen);
    }
    else
    {
        runner.runTestsInCategory("RAAADIO");
    }

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i) {
        failures += runner.getResult(i)->failures;
        passes += runner.getResult(i)->passes;
    }

    std::cout << "\n" << passes << " comprobaciones correctas, " << failures << " fallidas\n";
    std::cout << (failures == 0 ? "TODAS LAS PRUEBAS PASARON" : "HAY PRUEBAS FALLIDAS") << '\n';
    return failures == 0 ? 0 : 1;
}
