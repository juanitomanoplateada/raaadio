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
#include "../Source/library/AdvertisingManager.h"
#include "../Source/generator/PlaylistGenerator.h"
#include "../Source/autodj/QueueTiming.h"
#include "../Source/autodj/AdScheduler.h"
#include "../Source/autodj/OnAir.h"
#include "../Source/autodj/DeadAirGuard.h"
#include "../Source/autodj/AutoDJ.h"
#include "../Source/audio/AudioEngine.h"
#include "../Source/audio/AirLogRecorder.h"
#include "../Source/audio/CartPlayer.h"
#include "../Source/network/StreamEncoder.h"
#include "../Source/helpers/StationSettings.h"
#include "../Source/helpers/SecretStore.h"
#include "../Source/ui/DragFormats.h"
#include <thread>

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

    juce::File writeAudio(juce::AudioFormat& format, const juce::File& file, double seconds, const Signal& signal,
                          const juce::StringPairArray& metadata = {}, int bits = 24, int quality = 0)
    {
        const double sampleRate = 44100.0;
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        auto stream = file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), sampleRate, 2, bits, metadata, quality));
        if (writer == nullptr) return {};
        stream.release();

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
}

//==============================================================================
class PortablePathTests : public juce::UnitTest
{
public:
    PortablePathTests() : juce::UnitTest("Rutas portables", "RAAADIO") {}

    void runTest() override
    {
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
            const auto queueFile = fx.lab.root.getChildFile("Config/queue.json");

            {
                Playlist p;
                PlaylistStore store(p, fx.library, queueFile);
                auto inBreak = b;
                inBreak.breakKey = "2026-09-22 12:00";
                p.insertTracks({ c, a, inBreak }, 0);
                store.saveIfChanged();
            }
            expect(queueFile.existsAsFile());

            // Entre el cierre y el reinicio se borra una pista: se omite sin romper la cola.
            fx.library.deleteTrack(a.id, false);

            Playlist restored;
            PlaylistStore store(restored, fx.library, queueFile);
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
            stream.release();
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
            expectWithinAbsoluteError(gainDb(-8.0, -0.1), -8.0, 0.01, "una pista fuerte se baja a -16 LUFS");
            expectWithinAbsoluteError(gainDb(-22.0, -10.0), 6.0, 0.01, "una suave se sube");
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
        expectEquals(juce::String(track.artist), juce::String("CARLOS VIVES"));
        expectEquals(juce::String::fromUTF8(track.title.c_str()), utf8("LA GOTA FRÍA"));
        expectEquals(copied.getFileName(), utf8("CARLOS VIVES - LA GOTA FRÍA.wav"));
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

        auto import = [&](const juce::String& name, double seconds, Signal signal) {
            const auto file = writeWav(outside.root.getChildFile(name + ".wav"), seconds, std::move(signal));
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
            expect(!deck.isReadyToMix());
            pump(mixer, 1.0, { &deck });
            expect(deck.isReadyToMix(), "la siguiente entra 3.5 s antes del punto de salida");
            expect(!deck.hasFinished());
            pump(mixer, 3.2, { &deck });
            expect(deck.hasFinished(), "al llegar al punto de salida termina, sin esperar el silencio final");
            deck.unload();
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
            expectEquals(names(db.getCategories()),
                         juce::String::fromUTF8("IDENTIFICACIONES,CUÑAS,SEPARADORES,VALLENATO,MIS ÉXITOS"),
                         "se borran los géneros y EMERGENCIA vacíos; los que tienen pistas y los del usuario se quedan");

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
                         juce::String::fromUTF8("IDENTIFICACIONES,CUÑAS,SEPARADORES,VALLENATO,MIS ÉXITOS,SALSA"),
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
        beginTest("La siguiente pista entra 3.5 s antes del punto de salida; las cortas terminan antes");
        {
            expectWithinAbsoluteError(QueueTiming::secondsUntilNext(song(1, "A", "A", 200.0)), 196.5, 1e-9);
            expectWithinAbsoluteError(QueueTiming::secondsUntilNext(song(2, "ID", "ID", 8.0)), 8.0, 1e-9);
            auto trimmed = song(3, "B", "B", 200.0);
            trimmed.cueIn = 1.0;
            trimmed.cueOut = 191.0;
            expectWithinAbsoluteError(QueueTiming::secondsUntilNext(trimmed), 186.5, 1e-9, "cuenta de entrada a salida");
        }

        beginTest("Una tanda se inserta donde empieza más cerca de su hora");
        {
            const std::vector<TrackInfo> queue = { song(1, "A", "A", 180.0), song(2, "B", "B", 180.0), song(3, "C", "C", 180.0) };
            const auto offsets = QueueTiming::startOffsets(queue);
            expectEquals((int) offsets.size(), 3);
            expectWithinAbsoluteError(offsets[2], 353.0, 1e-9);

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
            ads.initialise(fx.lab.root.getChildFile("Config/ads.json"));
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
            ads.initialise(fx.lab.root.getChildFile("Config/ads.json"));
            ads.setCueNumber(ad.id, 7);
            ads.addSchedule(7, "12:00");

            Playlist playlist;
            std::vector<TrackInfo> queue;
            for (int i = 0; i < 10; ++i) queue.push_back(song(1000 + i, "A" + juce::String(i), "T", 180.0));
            playlist.insertTracks(queue, 0);

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            juce::AudioThumbnailCache cache(4);
            Deck deckA(1, fx.library, formats, cache), deckB(2, fx.library, formats, cache);
            const auto stateFile = fx.lab.root.getChildFile("Config/ad_breaks_done.json");

            const juce::Time now(2026, 8, 22, 11, 50, 0, 0, true);
            {
                AdScheduler scheduler(fx.library, ads, playlist, deckA, deckB, stateFile);
                scheduler.checkNow(now);
                expectEquals(playlist.getQueue().size(), queue.size(), "sin la pauta automática activada no hace nada");

                ads.setAutoScheduleEnabled(true);
                scheduler.checkNow(now);
                const auto withBreak = playlist.getQueue();
                expectEquals((int) withBreak.size(), (int) queue.size() + 1);
                // Nada suena: la cola empieza ya; cada pista abre 176.5 s después de la anterior,
                // así que la posición 3 (a las 11:58:49) es la más cercana a las 12:00.
                expectEquals(withBreak[3].id, ad.id);

                scheduler.checkNow(now + juce::RelativeTime::minutes(1));
                expectEquals((int) playlist.getQueue().size(), (int) queue.size() + 1, "no se inserta dos veces");
            }
            AdScheduler restarted(fx.library, ads, playlist, deckA, deckB, stateFile);
            restarted.checkNow(now + juce::RelativeTime::minutes(2));
            expectEquals((int) playlist.getQueue().size(), (int) queue.size() + 1, "ni después de reiniciar la emisora");
        }

        beginTest("Si la cola se vacía o se reemplaza antes de que suene la tanda, la tanda vuelve a insertarse");
        {
            LibraryFixture fx;
            const auto ad = fx.add("Library/AD.wav", "CUNA");
            AdvertisingManager ads;
            ads.initialise(fx.lab.root.getChildFile("Config/ads.json"));
            ads.setCueNumber(ad.id, 7);
            ads.addSchedule(7, "12:00");
            ads.setAutoScheduleEnabled(true);

            Playlist playlist;
            std::vector<TrackInfo> queue;
            for (int i = 0; i < 10; ++i) queue.push_back(song(1000 + i, "A" + juce::String(i), "T", 180.0));
            playlist.insertTracks(queue, 0);

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            juce::AudioThumbnailCache cache(4);
            Deck deckA(1, fx.library, formats, cache), deckB(2, fx.library, formats, cache);
            AdScheduler scheduler(fx.library, ads, playlist, deckA, deckB, fx.lab.root.getChildFile("Config/done.json"));
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
            for (auto* folder : { "Database", "Config", "Library", "Templates", "Recordings", "Backups" })
                lab.root.getChildFile(folder).createDirectory();
            open();
        }

        void open()
        {
            library = std::make_unique<MusicLibrary>();
            library->initialise(dbFile());
        }

        juce::File dbFile() const { return lab.root.getChildFile("Database/raaadio.db"); }

        void addTrack(const juce::String& title)
        {
            const auto file = lab.touch("Library/GENERAL/" + title + ".ogg");
            library->getDatabase().addTrack(makeTrack(file.getFullPathName(), "ARTISTA", title));
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

    void runTest() override
    {
        beginTest("Un respaldo guarda una copia de la base abierta, los ajustes y las plantillas");
        {
            Station station;
            station.addTrack("A");
            station.lab.root.getChildFile("Config/station.json").replaceWithText("{}");
            station.lab.root.getChildFile("Config/cartwall.json").replaceWithText("[]");
            station.lab.root.getChildFile("Templates/MAÑANA.json").replaceWithText("{}");

            juce::File first, second;
            expect(PersistenceManager::createBackup(station.snapshot(), &first).wasOk());
            expect(first.isAChildOf(station.lab.root.getChildFile("Backups")));
            expect(titlesIn(first.getChildFile("raaadio.db")) == juce::StringArray { "A" },
                   "la copia se abre y tiene lo que había, aunque la base estaba en uso");
            expect(first.getChildFile("Config/station.json").existsAsFile() && first.getChildFile("Config/cartwall.json").existsAsFile(),
                   "incluye los ajustes de emisión y la botonera, no solo la pauta");
            expect(first.getChildFile(utf8("Templates/MAÑANA.json")).existsAsFile(), "y las plantillas del generador");

            expect(PersistenceManager::createBackup(station.snapshot(), &second).wasOk());
            expect(second != first, "dos respaldos en el mismo segundo no se pisan");

            expect(PersistenceManager::createBackup({}, nullptr).failed(), "sin copia de la base no hay respaldo");
        }

        beginTest("Se borran los respaldos de más de 30 días, dejando siempre los 10 más recientes");
        {
            Station station;
            const auto backups = station.lab.root.getChildFile("Backups");
            const juce::Time now(2026, 8, 23, 12, 0, 0, 0, true);
            auto make = [&backups](juce::Time t) {
                const auto folder = backups.getChildFile(t.formatted("%Y/%m/%d/%H_%M_%S"));
                folder.getChildFile("raaadio.db").create();
                return folder;
            };

            juce::Array<juce::File> recent, old;
            for (int d = 1; d <= 5; ++d) recent.add(make(now - juce::RelativeTime::days(d)));
            for (int d = 40; d <= 47; ++d) old.add(make(now - juce::RelativeTime::days(d)));
            const auto other = backups.getChildFile("copia manual");
            other.getChildFile("raaadio.db").create();

            PersistenceManager::pruneBackups(now);

            bool recentKept = true;
            for (const auto& f : recent) recentKept = recentKept && f.isDirectory();
            expect(recentKept);
            expect(old[0].isDirectory() && old[4].isDirectory(), "los 5 viejos más nuevos completan los 10 que se guardan");
            expect(!old[5].exists() && !old[6].exists() && !old[7].exists(), "los más viejos se borran");
            expect(!old[7].getParentDirectory().exists(), "sin dejar carpetas de día vacías");
            expect(other.isDirectory(), "lo que no es un respaldo de RAAADIO no se toca");
            expect(PersistenceManager::getLatestBackupTime() == now - juce::RelativeTime::days(1));
        }

        beginTest("Restaurar se prepara ahora y se aplica al abrir la emisora, después de respaldar lo que había");
        {
            Station station;
            const auto ads = station.lab.root.getChildFile("Config/ads_data.json");
            const auto queue = station.lab.root.getChildFile("Config/queue.json");
            station.addTrack("ANTES");
            ads.replaceWithText("pauta del respaldo");
            queue.replaceWithText("cola del respaldo");
            juce::File backup;
            expect(PersistenceManager::createBackup(station.snapshot(), &backup).wasOk());

            // Después del respaldo la emisora siguió.
            station.addTrack("DESPUES");
            ads.replaceWithText("pauta nueva");
            queue.replaceWithText("cola actual");

            expect(PersistenceManager::stageRestore(backup).wasOk());
            expect(PersistenceManager::hasPendingRestore());
            expect(station.titles() == juce::StringArray { "ANTES", "DESPUES" }, "la emisora en marcha no cambia");

            // Al cerrar, la app guarda la pauta que tiene en memoria: antes esto deshacía la restauración.
            ads.replaceWithText("pauta guardada al cerrar");
            station.library = nullptr;

            expect(PersistenceManager::applyPendingRestore().wasOk());
            expect(!PersistenceManager::hasPendingRestore());
            station.open();
            expect(station.titles() == juce::StringArray { "ANTES" });
            expectEquals(ads.loadFileAsString(), juce::String("pauta del respaldo"), "la pauta restaurada queda");
            expectEquals(queue.loadFileAsString(), juce::String("cola actual"), "la cola es del momento: no se restaura");

            // Lo que había antes de restaurar quedó en un respaldo.
            bool previousSaved = false;
            for (const auto& db : station.lab.root.getChildFile("Backups").findChildFiles(juce::File::findFiles, true, "raaadio.db"))
                previousSaved = previousSaved || titlesIn(db) == juce::StringArray { "ANTES", "DESPUES" };
            expect(previousSaved);
        }

        beginTest("Solo se prepara una restauración desde un respaldo válido; los de versiones anteriores sirven");
        {
            Station station;
            TempLab outside;
            expect(PersistenceManager::stageRestore(outside.root).failed(), "una carpeta sin raaadio.db");
            outside.root.getChildFile("raaadio.db").replaceWithText("no es una base de datos");
            expect(PersistenceManager::stageRestore(outside.root).failed(), "un archivo que no es SQLite");
            expect(!PersistenceManager::hasPendingRestore());

            // Formato anterior: raaadio.db y ads_data.json juntos, sin carpeta Config.
            station.addTrack("VIEJA");
            outside.root.getChildFile("raaadio.db").deleteFile();
            expect(station.library->backupDatabaseTo(outside.root.getChildFile("raaadio.db")));
            outside.root.getChildFile("ads_data.json").replaceWithText("pauta vieja");
            expect(PersistenceManager::stageRestore(outside.root).wasOk());
            station.library = nullptr;
            expect(PersistenceManager::applyPendingRestore().wasOk());
            expectEquals(station.lab.root.getChildFile("Config/ads_data.json").loadFileAsString(), juce::String("pauta vieja"));
        }

        beginTest("Mover la emisora copia todo a la carpeta nueva; si no está vacía o se cancela, no se usa");
        {
            Station station;
            station.addTrack("A");
            station.lab.touch("Recordings/2026/09/23/programa.wav");
            station.lab.touch("Config/station.json");
            station.lab.touch("Backups/2026/09/22/08_00_00/raaadio.db");
            station.lab.touch("Logs/raaadio.log");

            TempLab destination;
            const auto target = PersistenceManager::labFolderFor(destination.root);
            juce::StringArray skipped;
            double lastProgress = 0.0;
            expect(PersistenceManager::copyLabTo(target, station.snapshot(), [&lastProgress](double p) { lastProgress = p; return true; },
                                                 skipped).wasOk());
            expect(PersistenceManager::containsStation(target));
            expect(titlesIn(target.getChildFile("Database/raaadio.db")) == juce::StringArray { "A" });
            expect(target.getChildFile("Library/GENERAL/A.ogg").existsAsFile() && target.getChildFile("Recordings/2026/09/23/programa.wav").existsAsFile()
                   && target.getChildFile("Config/station.json").existsAsFile() && target.getChildFile("Backups/2026/09/22/08_00_00/raaadio.db").existsAsFile(),
                   "música, grabaciones, ajustes y respaldos");
            expect(!target.getChildFile("Logs/raaadio.log").exists(), "el registro de la app no hace falta");
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
        const auto stateFile = fx.lab.root.getChildFile("Config/cartwall.json");

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        beginTest("Un cartucho suena directo al aire, por encima de los decks");
        {
            AudioEngine engine(fx.library, false);
            CartPlayer carts(fx.library, formats, stateFile);
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

            CartPlayer carts(fx.library, formats, fx.lab.root.getChildFile("Config/otra_botonera.json"));
            expect(carts.assignFromLibrary(0, legacy));
            expectWithinAbsoluteError(carts.getRemainingSeconds(0), 3.04, 0.05, "se analiza al asignarla: arranca en su punto de entrada");
            expectEquals(fx.library.getTrackByPath(legacyFile.getFullPathName()).analysisVersion, TrackAnalyzer::version);
        }

        beginTest("La botonera se recuerda al reiniciar");
        {
            CartPlayer restored(fx.library, formats, stateFile);
            expect(restored.isAssigned(3));
            expectEquals(restored.getTrack(3).id, jingle.id);
            expect(!restored.isAssigned(0));

            restored.clear(3);
            CartPlayer cleared(fx.library, formats, stateFile);
            expect(!cleared.isAssigned(3), "quitar también se guarda");
        }
    }
};

//==============================================================================
class StreamingTests : public juce::UnitTest
{
public:
    StreamingTests() : juce::UnitTest("Transmisión Icecast", "RAAADIO") {}

    // Servidor Icecast de mentira en 127.0.0.1: lee la petición, responde y guarda lo
    // que llega hasta ver untilText (o agotar el tiempo).
    struct FakeIcecast {
        explicit FakeIcecast(bool accept, juce::String until = {}) : accepting(accept), untilText(std::move(until)) {
            listening = server.createListener(0, "127.0.0.1");
            thread = std::thread([this] { serve(); });
        }
        ~FakeIcecast() {
            server.close();
            if (thread.joinable()) thread.join();
        }
        int port() const { return server.getBoundPort(); }

        void serve() {
            std::unique_ptr<juce::StreamingSocket> client(server.waitForNextConnection());
            if (client == nullptr) return;
            char c = 0;
            while (!request.endsWith("\r\n\r\n") && client->waitUntilReady(true, 5000) == 1 && client->read(&c, 1, false) == 1)
                request += juce::String::charToString((juce::juce_wchar) (juce::uint8) c);

            const char* reply = accepting ? "HTTP/1.0 200 OK\r\n\r\n" : "HTTP/1.0 401 Unauthorized\r\n\r\n";
            client->write(reply, (int) strlen(reply));
            if (!accepting) return;

            const auto deadline = juce::Time::getMillisecondCounter() + 8000;
            char buffer[8192];
            while (juce::Time::getMillisecondCounter() < deadline) {
                if (client->waitUntilReady(true, 200) != 1) continue;
                const int n = client->read(buffer, (int) sizeof(buffer), false);
                if (n <= 0) break;
                {
                    const juce::ScopedLock sl(lock);
                    received.append(buffer, (size_t) n);
                }
                if (untilText.isNotEmpty() && contains(untilText)) break;
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

        bool accepting;
        juce::String untilText;
        juce::StreamingSocket server;
        bool listening = false;
        std::thread thread;
        juce::String request;
        juce::CriticalSection lock;
        juce::MemoryBlock received;
    };

    void runTest() override
    {
        StreamEncoder::Server server;
        server.host = "127.0.0.1";
        server.mount = "prueba.ogg";
        server.user = "source";
        server.password = "secreto";
        server.name = "Radio Prueba";

        beginTest("La petición SOURCE lleva el punto de montaje, la clave y el tipo");
        {
            const auto request = StreamEncoder::buildSourceRequest(server);
            expect(request.startsWith("SOURCE /prueba.ogg HTTP/1.0\r\n"));
            expect(request.contains("Authorization: Basic " + juce::Base64::toBase64("source:secreto") + "\r\n"));
            expect(request.contains("Content-Type: application/ogg\r\n") && request.endsWith("\r\n\r\n"));
            expect(StreamEncoder::isAccepted("HTTP/1.0 200 OK") && StreamEncoder::isAccepted("ICY 200 OK"));
            expect(!StreamEncoder::isAccepted("HTTP/1.0 401 Unauthorized") && !StreamEncoder::isAccepted(""));
        }

        juce::AudioBuffer<float> block(2, 512);
        for (int i = 0; i < 512; ++i)
            for (int ch = 0; ch < 2; ++ch)
                block.setSample(ch, i, tone(i / 44100.0, 0.3f, 441.0));

        beginTest("Transmite Ogg Vorbis con el título, y lo cambia en cada canción");
        {
            FakeIcecast icecast(true, "TITLE=SIGUIENTE");
            expect(icecast.listening);
            server.port = icecast.port();

            StreamEncoder encoder;
            encoder.prepare(44100.0);
            encoder.setNowPlaying("ARTISTA", juce::String::fromUTF8("CANCIÓN"));
            encoder.start(server);

            bool changedTitle = false;
            for (int i = 0; i < 1500 && icecast.thread.joinable() && !icecast.contains("TITLE=SIGUIENTE"); ++i) {
                encoder.pushBlock(block, 0, 512);
                if (!changedTitle && icecast.size() > 8000) {
                    encoder.setNowPlaying("OTRO", "SIGUIENTE");
                    changedTitle = true;
                }
                juce::Thread::sleep(3);
            }
            expect(encoder.getStatus() == StreamEncoder::Status::Connected);
            encoder.stop();

            expect(icecast.request.startsWith("SOURCE /prueba.ogg HTTP/1.0"), icecast.request);
            expect(icecast.contains("OggS"), "llegan páginas Ogg");
            expect(icecast.contains(juce::String::fromUTF8("TITLE=CANCIÓN")) && icecast.contains("ARTIST=ARTISTA"),
                   "el título y el artista con tildes");
            expect(icecast.contains("TITLE=SIGUIENTE"), "al cambiar de canción empieza un flujo encadenado con el título nuevo");
        }

        beginTest("Una clave equivocada queda como error, con el motivo");
        {
            FakeIcecast icecast(false);
            server.port = icecast.port();
            StreamEncoder encoder;
            encoder.start(server);
            for (int i = 0; i < 100 && encoder.getStatus() != StreamEncoder::Status::Error; ++i)
                juce::Thread::sleep(20);
            expect(encoder.getStatus() == StreamEncoder::Status::Error);
            juce::Thread::sleep(2500);  // pasa el primer reintento
            expect(encoder.getStatus() == StreamEncoder::Status::Error, "al reintentar sigue mostrando el error");
            expect(encoder.getLastError().contains("401"), encoder.getLastError());
            encoder.stop();
            expect(encoder.getStatus() == StreamEncoder::Status::Off);
        }
    }
};

//==============================================================================
class StationSettingsTests : public juce::UnitTest
{
public:
    StationSettingsTests() : juce::UnitTest("Ajustes de emisión", "RAAADIO") {}

    void runTest() override
    {
        beginTest("Se guardan y se leen; los valores fuera de rango se corrigen");
        TempLab lab;
        const auto file = lab.root.getChildFile("Config/station.json");
        {
            StationConfig config(file);
            auto s = config.get();
            expect(s.deadAirEnabled && s.deadAirSeconds == 20 && !s.streamEnabled, "valores por defecto");
            s.deadAirSeconds = 45;
            s.airLogEnabled = true;
            s.streamHost = "radio.example.org";
            s.streamPassword = juce::String::fromUTF8("contraseña");
            config.set(s);
        }
        StationConfig reopened(file);
        expectEquals(reopened.get().deadAirSeconds, 45);
        expect(reopened.get().airLogEnabled);
        expectEquals(reopened.get().streamHost, juce::String("radio.example.org"));
        expectEquals(reopened.get().streamPassword, juce::String::fromUTF8("contraseña"));

       #if JUCE_WINDOWS
        expect(!file.loadFileAsString().contains(juce::String::fromUTF8("contraseña")), "la clave no queda en claro en el archivo");
        expect(!file.loadFileAsString().contains("streamPassword\""), "ni con el nombre de antes");
       #endif

        beginTest("Clave: la de versiones anteriores se cifra al abrir; la de otro equipo se conserva hasta reescribirla");
        {
            TempLab other;
            const auto legacy = other.root.getChildFile("legacy.json");
            legacy.replaceWithText(R"({ "streamHost": "radio.example.org", "streamPassword": "vieja" })");
            StationConfig fromLegacy(legacy);
            expectEquals(fromLegacy.get().streamPassword, juce::String("vieja"));
           #if JUCE_WINDOWS
            expect(!legacy.loadFileAsString().contains("vieja"), "se reescribe cifrada enseguida");
           #endif

            const auto moved = other.root.getChildFile("moved.json");
            moved.replaceWithText(R"({ "streamHost": "radio.example.org", "streamPasswordProtected": "dpapi:AAAAAAAA" })");
            StationConfig fromOtherPc(moved);
            expect(fromOtherPc.get().streamPassword.isEmpty() && fromOtherPc.get().isStreamPasswordUnreadable(),
                   "cifrada en otro equipo: hay que volver a escribirla");
            auto changed = fromOtherPc.get();
            changed.deadAirSeconds = 30;
            fromOtherPc.set(changed);
            expect(moved.loadFileAsString().contains("dpapi:AAAAAAAA"), "guardar otro ajuste no la borra");
            changed.streamPassword = "nueva";
            fromOtherPc.set(changed);
            StationConfig reread(moved);
            expectEquals(reread.get().streamPassword, juce::String("nueva"));
            expect(!reread.get().isStreamPasswordUnreadable());

            juce::String secret;
            expect(SecretStore::protect({}).isEmpty() && SecretStore::unprotect({}, secret) && secret.isEmpty());
            expect(!SecretStore::unprotect("dpapi:%%%", secret));
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

static PortablePathTests portablePathTests;
static PlayLogTests playLogTests;
static PlaylistTests playlistTests;
static PersistenceTests persistenceTests;
static BackupTests backupTests;
static MixerCueTests mixerCueTests;
static TrackAnalyzerTests trackAnalyzerTests;
static TagReaderTests tagReaderTests;
static ImportTests importTests;
static DeckCueTests deckCueTests;
static GeneratorTests generatorTests;
static CategoryTests categoryTests;
static QueueTimingTests queueTimingTests;
static OnAirTests onAirTests;
static DeadAirTests deadAirTests;
static AirLogTests airLogTests;
static CartwallTests cartwallTests;
static StreamingTests streamingTests;
static StationSettingsTests stationSettingsTests;

#if JUCE_WINDOWS
extern "C" __declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int codePage);
#endif

class ConsoleRunner : public juce::UnitTestRunner
{
    void logMessage(const juce::String& message) override
    {
        std::cout << message << std::endl;
    }
};

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

   #if JUCE_WINDOWS
    SetConsoleOutputCP(65001);  // UTF-8, para que las tildes se vean bien en la consola
   #endif

    ConsoleRunner runner;
    runner.setAssertOnFailure(false);
    runner.runTestsInCategory("RAAADIO");

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i) {
        failures += runner.getResult(i)->failures;
        passes += runner.getResult(i)->passes;
    }

    std::cout << "\n" << passes << " comprobaciones correctas, " << failures << " fallidas" << std::endl;
    std::cout << (failures == 0 ? "TODAS LAS PRUEBAS PASARON" : "HAY PRUEBAS FALLIDAS") << std::endl;
    return failures == 0 ? 0 : 1;
}
