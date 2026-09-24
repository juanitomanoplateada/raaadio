#include "UiSnapshot.h"
#include "PersistenceManager.h"
#include "../MainComponent.h"
#include "../ui/AudioSettingsDialog.h"
#include "../ui/PlaylistGeneratorDialog.h"
#include "../ui/TrackDetailsDialog.h"
#include "../ui/AdvancedImportDialog.h"
#include "../ui/LibraryView.h"
#include "../ui/AdsListView.h"

//==============================================================================

bool UiSnapshotRunner::parseCommandLine(const juce::StringArray& args, Options& result)
{
    const int index = args.indexOf("--ui-snapshot");
    if (index < 0 || index + 1 >= args.size())
        return false;

    result.output = juce::File::getCurrentWorkingDirectory().getChildFile(args[index + 1].unquoted());

    auto valueOf = [&args](const juce::String& flag) {
        const int i = args.indexOf(flag);
        return (i >= 0 && i + 1 < args.size()) ? args[i + 1].unquoted() : juce::String();
    };

    const auto size = valueOf("--size");
    if (size.containsChar('x'))
    {
        result.width  = juce::jmax(640, size.upToFirstOccurrenceOf("x", false, true).getIntValue());
        result.height = juce::jmax(480, size.fromFirstOccurrenceOf("x", false, true).getIntValue());
    }

    result.theme = valueOf("--theme");
    result.language = valueOf("--lang");
    if (valueOf("--view").isNotEmpty())
        result.view = valueOf("--view");

    return true;
}

void UiSnapshotRunner::useIsolatedLab()
{
    auto lab = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("raaadio_ui_snapshot");
    lab.deleteRecursively();
    lab.createDirectory();
    PersistenceManager::setLabRootOverride(lab);
}

//==============================================================================
// Audio sintético para los decks: ruido con una envolvente que imita secciones de
// una canción (entrada, estrofas, coros, salida), para que la forma de onda se vea real.

static juce::File writeDemoAudio(const juce::File& file, double seconds, int seed)
{
    file.getParentDirectory().createDirectory();

    const double sampleRate = 11025.0;
    juce::WavAudioFormat wav;
    auto stream = file.createOutputStream();
    if (stream == nullptr) return {};

    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), sampleRate, 1, 16, {}, 0));
    if (writer == nullptr) return {};
    stream.release();

    juce::Random rng(seed);
    const int total = (int) (seconds * sampleRate);
    const int blockSize = 4096;
    juce::AudioBuffer<float> buffer(1, blockSize);

    float phraseGain = 0.7f;
    int nextPhrase = 0;

    for (int pos = 0; pos < total; pos += blockSize)
    {
        const int n = juce::jmin(blockSize, total - pos);
        for (int i = 0; i < n; ++i)
        {
            if (pos + i >= nextPhrase)
            {
                phraseGain = 0.3f + rng.nextFloat() * 0.7f;
                nextPhrase += (int) (sampleRate * (1.5 + rng.nextFloat() * 5.0));
            }

            // Golpe de bombo a ~118 BPM sobre un colchón más bajo, para que la onda respire.
            const double t = (pos + i) / sampleRate;
            const double beatPhase = std::fmod(t * 1.97, 1.0);
            const double kick = std::exp(-beatPhase * 7.0);
            double env = phraseGain * (0.22 + 0.62 * kick);
            if (t < 5.0) env *= t / 5.0;
            if (t > seconds - 9.0) env *= juce::jmax(0.0, (seconds - t) / 9.0);

            buffer.setSample(0, i, (float) ((rng.nextFloat() * 2.0f - 1.0f) * env * 0.85));
        }
        writer->writeFromAudioSampleBuffer(buffer, 0, n);
    }

    return file;
}

template <typename ComponentType>
static ComponentType* findFirstChild(juce::Component* parent)
{
    if (parent == nullptr) return nullptr;
    if (auto* match = dynamic_cast<ComponentType*>(parent)) return match;

    for (auto* child : parent->getChildren())
        if (auto* found = findFirstChild<ComponentType>(child))
            return found;

    return nullptr;
}

//==============================================================================

UiSnapshotRunner::UiSnapshotRunner(const Options& o) : options(o)
{
    MainComponentOptions mainOptions;
    mainOptions.openAudioDevices = false;
    mainOptions.startApiServer = false;
    mainOptions.runAutoBackup = false;
    mainOptions.analyseLibrary = false;

    main = std::make_unique<MainComponent>(mainOptions);
    main->setSize(options.width, options.height);
    main->setVisible(true);

    populateDemoState();

    // Las vistas ya estaban creadas antes de los datos de demostración.
    if (auto* libraryView = findFirstChild<LibraryView>(main.get())) libraryView->refresh();
    if (auto* adsList = findFirstChild<AdsListView>(main.get())) adsList->refreshList();

    target = createView();

    startTime = juce::Time::getMillisecondCounter();
    startTimer(50);
}

UiSnapshotRunner::~UiSnapshotRunner()
{
    stopTimer();
    dialogView = nullptr;
    main = nullptr;
}

void UiSnapshotRunner::populateDemoState()
{
    auto& library = main->library;
    auto& db = library.getDatabase();
    const auto demoDir = PersistenceManager::getLibraryFolder().getChildFile("demo");

    auto categoryId = [&library](const juce::String& name) {
        library.addCategory(name);
        for (const auto& c : library.getCategories())
            if (juce::String(c.name) == name) return c.id;
        return -1;
    };

    auto addTrack = [&](const char* artist, const char* title, double duration,
                        const juce::String& category, juce::String path = {}) {
        if (path.isEmpty())
            path = demoDir.getChildFile(juce::String(db.searchTracks("", "").size()) + ".ogg").getFullPathName();

        TrackInfo t;
        t.path = path.toStdString();
        t.artist = juce::String::fromUTF8(artist).toStdString();
        t.title = juce::String::fromUTF8(title).toStdString();
        t.duration = duration;
        t.normalizationGain = 1.0;
        db.addTrack(t);

        auto stored = db.getTrackByPath(t.path);
        const int catId = categoryId(category);
        if (stored.id != -1 && catId != -1)
            library.setTrackCategories(stored.id, { catId });
        return stored;
    };

    // Decks: A al aire a mitad de canción, B preparado con la siguiente.
    const auto fileA = writeDemoAudio(demoDir.getChildFile("deck_a.wav"), 292.0, 7);
    const auto fileB = writeDemoAudio(demoDir.getChildFile("deck_b.wav"), 283.0, 11);
    addTrack("REY RUIZ", "NO ME ACOSTUMBRO", 292.0, "SALSA", fileA.getFullPathName());
    addTrack("GRUPO NICHE", "UNA AVENTURA", 283.0, "SALSA", fileB.getFullPathName());

    struct Demo { const char* artist; const char* title; double duration; const char* category; };
    const Demo catalogue[] = {
        { "RICHIE RAY Y BOBBY CRUZ", "SONIDO BESTIAL", 411, "SALSA" },
        { "LOS ADOLESCENTES", "ANHELO", 244, "SALSA" },
        { "CELIA CRUZ", "TUYA, MÁS QUE TUYA", 161, "SALSA" },
        { "GRUPO NICHE", "ANA MILÉ", 231, "SALSA" },
        { "PEDRO CONGA", "ME NIEGAS TANTO AMOR", 281, "SALSA" },
        { "ORQUESTA ARAGÓN", "QUIÉREME SIEMPRE", 180, "BOLERO" },
        { "GUAYACÁN ORQUESTA", "INVIERNO EN PRIMAVERA", 310, "SALSA" },
        { "LA SONORA MATANCERA", "MALA MUJER", 229, "TROPICAL" },
        { "HENRY FIOL", "PICOTEANDO POR AHÍ", 337, "SALSA" },
        { "HÉCTOR LAVOE", "EL DÍA DE MI SUERTE", 323, "SALSA" },
        { "WILLIE GONZÁLEZ", "QUIERO MORIR EN TU PIEL", 374, "BALADA" },
        { "JUAN MANUEL LEBRÓN", "AMOR FUGAZ", 324, "SALSA" },
        { "CHECHE MENDOZA", "PENSANDO EN TI", 194, "BALADA" },
        { "RUBÉN BLADES", "MAESTRA VIDA", 438, "SALSA" },
        { "MARC ANTHONY", "TRAGEDIA", 226, "SALSA" },
        { "WILLIE COLÓN", "OH QUÉ SERÁ", 367, "SALSA" },
        { "POLO MONTAÑEZ", "EL RINCÓN DE MIS ANSIAS", 284, "TROPICAL" },
        { "CARLOS VIVES", "LA GOTA FRÍA", 269, "VALLENATO" },
        { "BINOMIO DE ORO", "OLVÍDALA", 262, "VALLENATO" },
        { "DIOMEDES DÍAZ", "TÚ ES LA REINA", 289, "VALLENATO" },
        { "JUAN LUIS GUERRA", "OJALÁ QUE LLUEVA CAFÉ", 256, "MERENGUE" },
        { "WILFRIDO VARGAS", "EL JARDINERO", 241, "MERENGUE" },
        { "ROMEO SANTOS", "PROPUESTA INDECENTE", 237, "BACHATA" },
        { "AVENTURA", "OBSESIÓN", 250, "BACHATA" },
        { "LUCHO BERMÚDEZ", "SAN FERNANDO", 188, "CUMBIA" },
        { "LOS CORRALEROS DE MAJAGUAL", "LA CUMBIA SOLEDEÑA", 201, "CUMBIA" },
        { "ARMANDO MANZANERO", "ESTA TARDE VI LLOVER", 203, "BOLERO" },
        { "JULIO JARAMILLO", "NUESTRO JURAMENTO", 177, "BOLERO" },
        { "JOE ARROYO", "EN BARRANQUILLA ME QUEDO", 296, "SALSA" },
        { "FRUKO Y SUS TESOS", "EL PRESO", 311, "SALSA" },
        { "GALY GALIANO", "FRUTA FRESCA", 229, "TROPICAL" },
        { "SILVESTRE DANGOND", "LA GRINGA", 244, "VALLENATO" },
    };

    std::vector<TrackInfo> added;
    for (const auto& d : catalogue)
        added.push_back(addTrack(d.artist, d.title, d.duration, d.category));

    // Pauta: identificaciones y cuñas con número y horario.
    const juce::String spots(library.getSystemCategory(CategoryRole::Spots).name);
    const auto station1 = addTrack("RAAADIO", "IDENTIFICACIÓN ESTACIÓN 1", 12, "IDENTIFICACIONES");
    addTrack("RAAADIO", "IDENTIFICACIÓN ESTACIÓN 2", 9, "IDENTIFICACIONES");
    const auto ad1 = addTrack("FERRETERÍA EL MARTILLO", "CUÑA 30 SEGUNDOS", 30, spots);
    const auto ad2 = addTrack("DROGUERÍA LA SALUD", "CUÑA 20 SEGUNDOS", 20, spots);
    const auto ad3 = addTrack("PANADERÍA SAN JOSÉ", "CUÑA PROMOCIÓN", 25, spots);
    main->adsManager.setCueNumber(ad1.id, 1);
    main->adsManager.setCueNumber(ad2.id, 2);
    main->adsManager.setCueNumber(ad3.id, 3);
    for (auto slot : { "06:00", "07:30", "12:00", "18:00" }) main->adsManager.addSchedule(1, slot);
    for (auto slot : { "07:30", "13:00" }) main->adsManager.addSchedule(2, slot);
    main->adsManager.addSchedule(3, "12:00");

    // Botonera: tres cortinillas cortas (con audio real para que se puedan cargar).
    const Demo jingles[] = {
        { "RAAADIO", "CORTINILLA NOTICIAS", 8, "SEPARADORES" },
        { "RAAADIO", "EFECTO APLAUSOS", 6, "SEPARADORES" },
        { "RAAADIO", "IDENTIFICACIÓN CORTA", 5, "IDENTIFICACIONES" },
    };
    for (int i = 0; i < 3; ++i) {
        const auto& j = jingles[i];
        const auto file = writeDemoAudio(demoDir.getChildFile("jingle_" + juce::String(i) + ".wav"), j.duration, 20 + i);
        main->cartPlayer->assign(i == 2 ? 4 : i, addTrack(j.artist, j.title, j.duration, j.category, file.getFullPathName()));
    }

    // Vista "empty": la emisora recién abierta, con biblioteca pero sin nada cargado.
    if (options.view == "empty")
    {
        main->audioEngine.getMixer().prepareToPlay(512, 44100.0);
        return;
    }

    // Cola de reproducción con una identificación intercalada.
    std::vector<TrackInfo> queue(added.begin(), added.begin() + 22);
    queue.insert(queue.begin() + 4, station1);
    main->playlist.insertTracks(queue, 0);

    // Historial reciente.
    for (int i = 22; i < 27; ++i)
        main->historyManager.addEntry(added[(size_t) i], (i % 2) ? "Deck A" : "Deck B");

    auto& deckA = main->audioEngine.getDeck(0);
    auto& deckB = main->audioEngine.getDeck(1);
    deckA.loadTrack(fileA);
    deckB.loadTrack(fileB);
    deckA.setPosition(118.0);
    deckA.play();

    // Emisión: un servidor de ejemplo (en este modo nunca se conecta ni envía nada).
    auto station = main->stationConfig->get();
    station.streamHost = "stream.example.org";
    station.streamMount = "/raaadio";
    station.streamName = "RAAADIO FM";
    station.streamGenre = "Tropical";
    station.streamWebsite = "https://raaadio.example.org";
    station.streamDescription = juce::String::fromUTF8("Música tropical las 24 horas");
    station.nowPlayingPushUrl = "https://raaadio.example.org/ahora.php";
    main->stationConfig->set(station);

    // Sin dispositivo de audio, el snapshot alimenta el mezclador a mano para que
    // avancen los decks y se muevan los medidores.
    main->audioEngine.getMixer().prepareToPlay(512, 44100.0);

    if (options.view == "cartwall")
        main->cartPlayer->toggle(1);  // un cartucho sonando, para ver su progreso
}

void UiSnapshotRunner::pumpAudio(int blocks)
{
    juce::AudioBuffer<float> buffer(2, 512);
    for (int i = 0; i < blocks; ++i)
    {
        buffer.clear();
        juce::AudioSourceChannelInfo info(&buffer, 0, buffer.getNumSamples());
        main->audioEngine.getMixer().getNextAudioBlock(info);
    }
}

juce::Component* UiSnapshotRunner::createView()
{
    const auto& view = options.view;

    if (view == "main" || view == "empty" || view == "history" || view == "ads" || view == "cartwall")
    {
        if (view != "main")
            if (auto* tabs = findFirstChild<juce::TabbedComponent>(main->libraryComp.get()))
                tabs->setCurrentTabIndex(view == "history" ? 1 : view == "ads" ? 2 : 3);
        return main.get();
    }

    int width = 600, height = 600;

    if (view.startsWith("settings"))
    {
        dialogView = std::make_unique<SettingsDialog>(main->audioEngine.getDeviceManager(),
                                                      main->audioEngine.getCueDeviceManager(),
                                                      main->library, main->adsManager, *main->stationConfig,
                                                      main->streamEncoder, main->nowPlaying.get(), PersistenceManager::getRecordingsFolder().getChildFile("Registro"));
        height = 640;
    }
    else if (view == "generator")
    {
        dialogView = std::make_unique<PlaylistGeneratorDialog>(main->library, main->playlist);
        width = 900; height = 620;
    }
    else if (view == "track")
    {
        dialogView = std::make_unique<TrackDetailsDialog>(main->library, main->audioEngine.getDeck(0).getCurrentTrack());
        width = 520; height = 640;
    }
    else if (view == "import")
    {
        dialogView = std::make_unique<AdvancedImportDialog>(main->library);
        width = 520; height = 240;
    }
    else
    {
        juce::Logger::writeToLog("UiSnapshot: unknown view '" + view + "', using main");
        return main.get();
    }

    dialogView->setSize(width, height);
    dialogView->setVisible(true);

    if (view.startsWith("settings-"))
        if (auto* tabs = findFirstChild<juce::TabbedComponent>(dialogView.get()))
            tabs->setCurrentTabIndex(view == "settings-library" ? 1 : view.startsWith("settings-broadcast") ? 2
                                    : view == "settings-about" ? 4 : 3);

    // La pestaña Emisión se desplaza: settings-broadcast-2 muestra su parte de abajo.
    if (view == "settings-broadcast-2")
        if (auto* tabs = findFirstChild<juce::TabbedComponent>(dialogView.get()))
            if (auto* scroller = dynamic_cast<juce::Viewport*>(tabs->getCurrentContentComponent()))
                scroller->setViewPositionProportionately(0.0, 1.0);

    return dialogView.get();
}

void UiSnapshotRunner::timerCallback()
{
    pumpAudio(4);

    bool thumbnailsReady = true;
    for (int i = 0; i < 2; ++i)
    {
        auto& deck = main->audioEngine.getDeck(i);
        if (deck.isLoaded() && !deck.getThumbnail().isFullyLoaded())
            thumbnailsReady = false;
    }

    const auto elapsed = juce::Time::getMillisecondCounter() - startTime;
    if ((thumbnailsReady && elapsed > 1800) || elapsed > 20000)
    {
        stopTimer();
        const bool ok = captureAndSave();
        if (onFinished) onFinished(ok);
    }
}

bool UiSnapshotRunner::captureAndSave()
{
    if (target == nullptr) return false;

    auto image = target->createComponentSnapshot(target->getLocalBounds(), true, 1.0f);

    options.output.getParentDirectory().createDirectory();
    options.output.deleteFile();

    juce::FileOutputStream out(options.output);
    if (!out.openedOk()) return false;

    juce::PNGImageFormat png;
    const bool ok = png.writeImageToStream(image, out);
    juce::Logger::writeToLog("UiSnapshot: wrote " + options.output.getFullPathName() + (ok ? "" : " (FAILED)"));
    return ok;
}
