#include "UiSnapshot.h"
#include "PersistenceManager.h"
#include "../MainComponent.h"
#include "../ui/AudioSettingsDialog.h"
#include "../ui/PlaylistGeneratorDialog.h"
#include "../ui/TrackDetailsDialog.h"
#include "../ui/AdvancedImportDialog.h"
#include "../ui/LibraryView.h"
#include "../ui/AdsListView.h"
#include "../ui/ImportReviewView.h"
#include "../ui/OnlineSearchView.h"
#include "UiStyle.h"

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
// Búsqueda en internet sin conectarse: Deezer y YouTube Music responden con datos de ejemplo
// (la misma forma de sus respuestas de verdad) y la carátula es un degradado.

static juce::String demoMusicItem(const std::string& id, const std::string& title, const std::string& artist,
                                  const std::string& album, const std::string& duration, const std::string& plays)
{
    auto page = [](const std::string& type) {
        return R"("navigationEndpoint": {"browseEndpoint": {"browseEndpointContextSupportedConfigs": {"browseEndpointContextMusicConfig": {"pageType": ")"
             + type + "\"}}}}";
    };
    const std::string json = R"({"musicResponsiveListItemRenderer": {"playlistItemData": {"videoId": ")" + id + R"("}, "flexColumns": [
        {"musicResponsiveListItemFlexColumnRenderer": {"text": {"runs": [{"text": ")" + title + R"(", "navigationEndpoint": {"watchEndpoint": {"videoId": ")" + id
        + R"(", "watchEndpointMusicSupportedConfigs": {"watchEndpointMusicConfig": {"musicVideoType": "MUSIC_VIDEO_TYPE_ATV"}}}}}]}}},
        {"musicResponsiveListItemFlexColumnRenderer": {"text": {"runs": [{"text": ")" + artist + "\", " + page("MUSIC_PAGE_TYPE_ARTIST")
        + R"(}, {"text": " • "}, {"text": ")" + album + "\", " + page("MUSIC_PAGE_TYPE_ALBUM")
        + R"(}, {"text": " • "}, {"text": ")" + duration + R"("}]}}},
        {"musicResponsiveListItemFlexColumnRenderer": {"text": {"runs": [{"text": ")" + plays + R"("}]}}}]}})";
    return juce::String::fromUTF8(json.c_str());
}

static HttpTransport::Response demoOnline(const HttpTransport::Request& request)
{
    HttpTransport::Response r;
    r.status = 200;
    if (request.url.contains("api.deezer.com/search"))
    {
        struct Row { int id; const char* title; const char* album; int duration; const char* isrc; };
        const Row rows[] = {
            { 1001, "Burbujas de Amor", "Bachata Rosa", 250, "USJ3V1862529" },
            { 1002, "Ojalá Que Llueva Café", "Ojalá Que Llueva Café", 256, "USJ3V1862530" },
            { 1003, "La Bilirrubina", "Bachata Rosa", 214, "USJ3V1862531" },
            { 1004, "Bachata Rosa", "Bachata Rosa", 258, "USJ3V1862532" },
            { 1005, "Frío, Frío (En Vivo)", "Asondeguerra Tour", 236, "USJ3V1862533" },
            { 1006, "Como Abeja al Panal", "Colección Romántica", 247, "USJ3V1862534" },
            { 1007, "La Llave de Mi Corazón", "La Llave de Mi Corazón", 222, "USJ3V1862535" },
            { 1008, "Visa para un Sueño", "Ojalá Que Llueva Café", 255, "USJ3V1862536" },
            { 1009, "Estrellitas y Duendes", "Mudanza y Acarreo", 239, "USJ3V1862537" },
            { 1010, "A Pedir Su Mano", "Areíto", 226, "USJ3V1862538" },
        };
        juce::StringArray items;
        for (const auto& row : rows)
        {
            const auto title = juce::String::fromUTF8(row.title);
            const auto version = title.fromFirstOccurrenceOf(" (", true, false).trim();
            items.add("{\"id\": " + juce::String(row.id) + ", \"type\": \"track\", \"title\": \"" + title + "\", \"title_short\": \""
                      + title.upToFirstOccurrenceOf(" (", false, false) + "\", \"title_version\": \"" + version + "\", \"duration\": "
                      + juce::String(row.duration) + ", \"isrc\": \"" + row.isrc + "\", \"link\": \"https://www.deezer.com/track/"
                      + juce::String(row.id) + "\", \"artist\": {\"name\": \"Juan Luis Guerra\"}, \"album\": {\"title\": \""
                      + juce::String::fromUTF8(row.album) + "\", \"cover_medium\": \"demo://cover\"}}");
        }
        r.body = "{\"data\": [" + items.joinIntoString(", ") + "], \"total\": 10}";
    }
    else if (request.url.contains("api.deezer.com/track/"))
    {
        r.body = R"({"id": 1001, "title": "Burbujas de Amor", "release_date": "1990-12-11",
                     "contributors": [{"name": "Juan Luis Guerra", "role": "Main"}]})";
    }
    else if (request.url.startsWith("demo://cover"))
    {
        juce::Image cover(juce::Image::RGB, 250, 250, true);
        {
            juce::Graphics g(cover);
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe0457b), 0.0f, 0.0f, juce::Colour(0xff3a1c71), 250.0f, 250.0f, false));
            g.fillAll();
            UiStyle::drawIcon(g, "music", { 75.0f, 75.0f, 100.0f, 100.0f }, juce::Colours::white.withAlpha(0.85f));
        }
        juce::MemoryOutputStream out(r.data, false);
        juce::PNGImageFormat().writeImageToStream(cover, out);
    }
    else if (request.url.contains("music.youtube.com"))
    {
        juce::StringArray items {
            demoMusicItem("v0ckuv1xBm0", "Burbujas De Amor", "Juan Luis Guerra 4.40", "Romance Rosa", "4:09", "267 M reproducciones"),
            demoMusicItem("G6fJreMyIks", "Burbujas de Amor", "Juan Luis Guerra 4.40", "Bachata Rosa", "4:11", "267 M reproducciones"),
            demoMusicItem("c6HhrpF09OI", "Burbujas de Amor", "J.Pedro Luis y J.Luis Guerra", "Hello! Dominican Republic Music", "4:23", "39 K reproducciones"),
            demoMusicItem("lT5Pz3os9q8", "Burbujas de amor", "Caracol Televisión y Catalexia", "Canciones La Descarga 2024", "3:15", "788 reproducciones"),
            demoMusicItem("yovV2KnbGLw", "Burbujas de Amor", "Banda Sinaloense Los Recoditos", "Ritmo de Fiesta", "4:33", "53 K reproducciones"),
        };
        r.body = R"({"contents": {"tabbedSearchResultsRenderer": {"tabs": [{"tabRenderer": {"content": {"sectionListRenderer": {"contents": [{"musicShelfRenderer": {"contents": [)"
               + items.joinIntoString(", ") + "]}}]}}}}]}}}";
    }
    else
    {
        r.body = "{}";
    }
    return r;
}

//==============================================================================

UiSnapshotRunner::UiSnapshotRunner(const Options& o) : options(o)
{
    MainComponentOptions mainOptions;
    mainOptions.openAudioDevices = false;
    mainOptions.startApiServer = false;
    mainOptions.runAutoBackup = false;
    mainOptions.analyseLibrary = false;
    mainOptions.songDownloads = false;

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
    demoDownloader = nullptr;
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
        // Un archivo vacío en su lugar: el generador no usa pistas cuyo archivo falta.
        if (!juce::File(path).existsAsFile()) juce::File(path).create();

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

    // Revisión: un lote de merengue con los nombres tal como llegan ("TÍTULO - ARTISTA
    // (compositores) año"), con un duplicado y un artista mal escrito.
    if (options.view.startsWith("review"))
    {
        // Artistas que la emisora ya confirmó.
        for (const auto& t : added)
        {
            const auto artist = juce::String::fromUTF8(t.artist.c_str());
            if (artist == "WILFRIDO VARGAS" || artist == "JUAN LUIS GUERRA" || artist == "CARLOS VIVES")
            {
                TrackNaming::Proposal p;
                p.artist = TrackNaming::titleCase(artist);
                p.title = TrackNaming::titleCase(juce::String::fromUTF8(t.title.c_str()));
                library.confirmTrack(t.id, p);
            }
        }

        struct Pending { const char* name; double duration; };
        const Pending batch[] = {
            { "BURBUJAS DE AMOR - JUAN LUIS GERRA", 251 },
            { "VOLVERE - WILFRIDO VARGAS", 248 }, { "CUPIDO ME FLECHO - WILFRIDO VARGAS", 262 },
            { "EL JARDINERO - WILFRIDO VARGAS", 241 }, { "EL PADRINO - FULANITO", 236 },
            { "GUALLANDO - FULANITO", 229 }, { "CALLATE - FULANITO_521290686", 244 },
            { "LA FLACA - POCHY Y SU COCOBAND (ALFONSO VASQUEZ)", 271 },
            { "LA GRAN MANZANA - FRENESI ORQUESTA, CANTA EL FANTASMA", 283 },
            { "A DORMIR JUNTITOS - EDDY HERRERA Y LIZ", 259 }, { "DEMASIADO NIÑA - EDDY HERRERA_521204329", 247 },
            { "LA BAILADORA - EDDY HERRERA (ALFONSO VALLEJO)", 266 },
            { "AMARTE A TI - SIN FRONTERAS - MERENGUE (D. FREIGBERG - W. A. RENZON) 1997", 252 },
            { "HOLA - LOS HERMANOS ROSARIO", 238 }, { "ESA MORENA - LOS HERMANOS ROSARIO - 1997", 243 },
            { "LA DUEÑA DEL SWING - LOS HERMANOS ROSARIO", 255 }, 
            { "LA MOVIDA - SANDY Y PAPO", 240 }, { "LA HORA DE BAILAR - SANDY Y PAPO", 233 },
        };
        const juce::String folder = "D:\\Musica\\MERENGUE";
        int merengue = categoryId("MERENGUE");
        for (const auto& p : batch)
        {
            const auto name = juce::String::fromUTF8(p.name);
            TrackInfo t {};
            t.path = PersistenceManager::getLibraryFolder().getChildFile("_Entrada/1/" + name + ".mp3").getFullPathName().toStdString();
            t.artist = name.upToFirstOccurrenceOf(" - ", false, false).toStdString();
            t.title = name.fromFirstOccurrenceOf(" - ", false, false).toStdString();
            t.duration = p.duration;
            t.normalizationGain = 1.0;
            t.status = 1;
            db.addTrack(t);
            const auto stored = db.getTrackByPath(t.path);
            Database::ImportSource source;
            source.name = p.name;
            source.folder = folder.toStdString();
            source.batch = 1;
            db.setImportSource(stored.id, source);
            library.setTrackCategories(stored.id, { merengue });

            // Lo que habría devuelto la búsqueda en línea (sin conectarse).
            Database::OnlineMatch online;
            online.status = Database::OnlineStatus::Found;
            if (name.startsWith("BURBUJAS")) {
                online.score = 0.96;
                online.artist = "Juan Luis Guerra";
                online.title = "Burbujas de amor";
                online.year = 1990;
                online.composers = { "Juan Luis Guerra" };
            } else if (name.startsWith("LA GRAN MANZANA")) {
                online.score = 0.93;
                online.artist = juce::String::fromUTF8("Frenesí Orquesta").toStdString();
                online.featured = { "El Fantasma" };
                online.title = "La gran manzana";
                online.year = 1995;
            } else if (name.startsWith("VOLVERE")) {
                online.score = 0.98;
                online.artist = "Wilfrido Vargas";
                online.title = juce::String::fromUTF8("Volveré").toStdString();
                online.year = 1986;
                online.composers = { "Wilfrido Vargas" };
            } else {
                continue;
            }
            db.saveOnlineMatch(stored.id, online);
        }
    }

    // Artistas: la biblioteca confirmada, con un artista escrito de dos formas.
    if (options.view == "artists")
    {
        ImportReview all(library);
        all.load(ImportReview::Scope::Library);
        all.confirm(all.readyRows());

        const auto typo = addTrack("ARMANDO MANZANER", "SOMOS NOVIOS", 187, "BOLERO");
        TrackNaming::Proposal p;
        p.artist = "Armando Manzaner";
        p.title = "Somos Novios";
        p.composers.add("Armando Manzanero");
        library.confirmTrack(typo.id, p);
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

    // Una canción que la biblioteca no tiene: el botón para buscarla en internet.
    if (view == "library-online")
    {
        // El buscador es hijo directo de la vista (la barra de categorías tiene los suyos).
        if (auto* libraryView = findFirstChild<LibraryView>(main.get()))
            for (auto* child : libraryView->getChildren())
                if (auto* box = dynamic_cast<juce::TextEditor*>(child))
                    box->setText("Burbujas de amor", true);
        return main.get();
    }

    int width = 600, height = 600;

    if (view.startsWith("settings"))
    {
        dialogView = std::make_unique<SettingsDialog>(main->audioEngine.getDeviceManager(),
                                                      main->audioEngine.getCueDeviceManager(),
                                                      main->library, main->adsManager, *main->stationConfig,
                                                      main->streamEncoder, main->nowPlaying.get(), main->backupService.get(), PersistenceManager::getAirLogFolder());
        static_cast<SettingsDialog*>(dialogView.get())->setOnlineIdentifier(main->onlineIdentifier.get());
        height = 640;
    }
    else if (view.startsWith("generator"))
    {
        // generator, generator-advanced, generator-done (después de generar) y
        // generator-replace (reemplazar la cola, con separadores y solo salsa).
        auto generatorDialog = std::make_unique<PlaylistGeneratorDialog>(main->library, main->playlist);
        generatorDialog->showAdvanced(view == "generator-advanced");
        if (view == "generator-done")
            generatorDialog->generate();
        if (view == "generator-replace")
        {
            auto& s = generatorDialog->settings;
            s.insertionMode = InsertionMode::Replace;
            s.targetDurationVal = 2;
            s.targetDurationIsHours = true;
            if (!generatorDialog->musicCategories.empty()) s.includedCategories = { generatorDialog->musicCategories.front().id };
            s.separatorCategoryId = generatorDialog->sweepersCategoryId;
            s.separatorInterval = 4;
            generatorDialog->syncControlsFromSettings();
            generatorDialog->refresh();
        }
        dialogView = std::move(generatorDialog);
        width = PlaylistGeneratorDialog::defaultWidth;
        height = PlaylistGeneratorDialog::defaultHeight;
    }
    else if (view == "track")
    {
        dialogView = std::make_unique<TrackDetailsDialog>(main->library, main->audioEngine.getDeck(0).getCurrentTrack());
        width = dialogView->getWidth(); height = dialogView->getHeight();
    }
    else if (view == "artists")
    {
        auto artists = std::make_unique<ArtistsView>(main->library);
        artists->setScopeCounts(main->library.getPendingCount(), main->library.getUnreviewedCount());
        artists->reload();
        dialogView = std::move(artists);
        width = options.width;
        height = options.height;
    }
    else if (view == "review" || view == "review-library")
    {
        auto review = std::make_unique<ImportReviewView>(main->library, nullptr,
                                                         view == "review" ? ImportReview::Scope::NewImports : ImportReview::Scope::Library);
        review->setArtistCount((int) main->library.getArtists().size());
        dialogView = std::move(review);
        width = options.width;
        height = options.height;
    }
    else if (view == "import")
    {
        dialogView = std::make_unique<AdvancedImportDialog>(main->library);
        width = 520; height = 240;
    }
    else if (view == "online-search")
    {
        // Una descarga a medias y otra en cola: yt-dlp de mentira que llega al 45 % y espera.
        const auto lab = PersistenceManager::getLibraryFolder().getParentDirectory();
        SongDownloader::Services services;
        services.toolsFolder = lab.getChildFile("DemoTools");
        services.toolsFolder.getChildFile("yt-dlp.exe").create();
        services.toolsFolder.getChildFile("ffmpeg/ffmpeg.exe").create();
        services.workFolder = lab.getChildFile("DemoDownloads");
        services.pathVariable = juce::String();
        services.minGapSeconds = 0;
        services.maxJitterSeconds = 0;
        services.run = [](const juce::StringArray& command, const ProcessRunner::LineCallback& onLine, const ProcessRunner::StopCheck& shouldStop) {
            if (command.contains("-U")) return 0;  // la actualización diaria
            onLine("[raaadio]  12.0%");
            onLine("[raaadio]  45.0%");
            while (!shouldStop()) juce::Thread::sleep(20);
            return (int) ProcessRunner::stopped;
        };
        demoDownloader = std::make_unique<SongDownloader>(main->library, services);

        auto demoTrack = [](juce::int64 id, const char* title, int duration) {
            SongSearch::Track t;
            t.id = id;
            t.title = t.titleShort = juce::String::fromUTF8(title);
            t.artist = "Juan Luis Guerra";
            t.duration = duration;
            return t;
        };
        auto demoVideo = [](const char* id) {
            SongSearch::Video v;
            v.id = id;
            v.score = 1.0;
            return v;
        };
        demoDownloader->enqueue(demoTrack(1001, "Burbujas de Amor", 250), demoVideo("v0ckuv1xBm0"));
        demoDownloader->enqueue(demoTrack(1003, "La Bilirrubina", 214), demoVideo("BF3yLhIgkXU"));

        auto search = std::make_unique<OnlineSearchView>(main->library, demoDownloader.get(), &demoOnline);
        search->search("Juan Luis Guerra");
        dialogView = std::move(search);
        width = options.width;
        height = options.height;
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
            tabs->setCurrentTabIndex(view.startsWith("settings-library") ? 1 : view.startsWith("settings-broadcast") ? 2
                                    : view == "settings-about" ? 4 : 3);

    // Las pestañas Biblioteca y datos y Emisión se desplazan: con -2 se ve su parte de abajo.
    if (view.startsWith("settings-") && view.endsWith("-2"))
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
