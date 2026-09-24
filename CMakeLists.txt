cmake_minimum_required(VERSION 3.15)
project(RAAADIO VERSION 1.0.0)

include(FetchContent)

# JUCE
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.3
)
FetchContent_MakeAvailable(JUCE)

# SQLite - Using a robust source
FetchContent_Declare(
    SQLite3
    URL https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip
)
FetchContent_MakeAvailable(SQLite3)

add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})
# Linux needs dl and pthread usually, but we are on Windows. 
if(UNIX)
    target_link_libraries(sqlite3 PRIVATE dl pthread)
endif()

# libebur128 (MIT): sonoridad EBU R 128 / ITU-R BS.1770 y pico real. Se compila su único
# archivo fuente, como SQLite; SOURCE_SUBDIR inexistente evita que se cargue su CMake.
FetchContent_Declare(
    ebur128
    URL https://github.com/jiixyj/libebur128/archive/refs/tags/v1.2.6.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake
)
FetchContent_MakeAvailable(ebur128)

add_library(ebur128 STATIC ${ebur128_SOURCE_DIR}/ebur128/ebur128.c)
target_include_directories(ebur128 PUBLIC ${ebur128_SOURCE_DIR}/ebur128)
if(MSVC)
    # MSVC no trae <sys/queue.h> ni define M_PI por defecto.
    target_include_directories(ebur128 PRIVATE ${ebur128_SOURCE_DIR}/ebur128/queue)
    target_compile_definitions(ebur128 PRIVATE _USE_MATH_DEFINES)
endif()
if(UNIX)
    target_link_libraries(ebur128 PRIVATE m)
endif()

juce_add_gui_app(RAAADIO
    PRODUCT_NAME "RAAADIO"
    VERSION 1.0.0
    ICON_BIG "Assets/RAAADIO ICON.png"
)

target_sources(RAAADIO PRIVATE
    Source/Main.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
    Source/audio/AudioEngine.h
    Source/audio/AudioEngine.cpp
    Source/audio/Deck.h
    Source/audio/Deck.cpp
    Source/audio/Mixer.h
    Source/audio/Mixer.cpp
    Source/audio/RecordingManager.h
    Source/audio/RecordingManager.cpp
    Source/audio/TrackAnalyzer.h
    Source/audio/TrackAnalyzer.cpp
    Source/audio/OutputTap.h
    Source/audio/AirLogRecorder.h
    Source/audio/AirLogRecorder.cpp
    Source/audio/CartPlayer.h
    Source/audio/CartPlayer.cpp
    Source/autodj/AutoDJ.h
    Source/autodj/AutoDJ.cpp
    Source/autodj/QueueTiming.h
    Source/autodj/QueueTiming.cpp
    Source/autodj/OnAir.h
    Source/autodj/OnAir.cpp
    Source/autodj/AdScheduler.h
    Source/autodj/AdScheduler.cpp
    Source/autodj/DeadAirGuard.h
    Source/autodj/DeadAirGuard.cpp
    Source/ui/DeckComponent.h
    Source/ui/DeckComponent.cpp
    Source/ui/MixerComponent.h
    Source/ui/MixerComponent.cpp
    Source/ui/RecordingPanel.h
    Source/ui/RecordingPanel.cpp
    Source/ui/StatusBar.h
    Source/ui/StatusBar.cpp
    Source/ui/CartwallView.h
    Source/ui/CartwallView.cpp
    Source/ui/DragFormats.h
    Source/ui/WaveformComponent.h
    Source/ui/WaveformComponent.cpp
    Source/ui/PlaylistComponent.h
    Source/ui/PlaylistComponent.cpp
    Source/ui/LibraryComponent.h
    Source/ui/LibraryComponent.cpp
    Source/ui/CategorySidebarComponent.h
    Source/ui/CategorySidebarComponent.cpp
    Source/ui/LibraryView.h
    Source/ui/LibraryView.cpp
    Source/ui/HistoryView.h
    Source/ui/HistoryView.cpp
    Source/library/HistoryManager.h
    Source/library/HistoryManager.cpp
    Source/library/AdvertisingManager.h
    Source/library/AdvertisingManager.cpp
    Source/ui/AdsListView.h
    Source/ui/AdsListView.cpp
    Source/ui/ScheduleView.h
    Source/ui/ScheduleView.cpp
    Source/ui/AdvertisingComponent.h
    Source/ui/AdvertisingComponent.cpp
    Source/ui/TrackDetailsDialog.h
    Source/ui/TrackDetailsDialog.cpp
    Source/ui/AudioSettingsDialog.h
    Source/ui/AudioSettingsDialog.cpp
    Source/network/ApiServer.h
    Source/network/ApiServer.cpp
    Source/network/StreamEncoder.h
    Source/network/StreamEncoder.cpp
    Source/library/MusicLibrary.h
    Source/library/MusicLibrary.cpp
    Source/library/TagReader.h
    Source/library/TagReader.cpp
    Source/library/Playlist.h
    Source/library/Playlist.cpp
    Source/library/PlaylistStore.h
    Source/library/PlaylistStore.cpp
    Source/database/Database.h
    Source/database/Database.cpp
    Source/helpers/LanguageManager.h
    Source/helpers/LanguageManager.cpp
    Source/helpers/PersistenceManager.h
    Source/helpers/PersistenceManager.cpp
    Source/helpers/ThemeManager.h
    Source/helpers/ThemeManager.cpp
    Source/helpers/RaaadioLookAndFeel.h
    Source/helpers/RaaadioLookAndFeel.cpp
    Source/helpers/UiStyle.h
    Source/helpers/UiStyle.cpp
    Source/helpers/UiSnapshot.h
    Source/helpers/UiSnapshot.cpp
    Source/helpers/StationSettings.h
    Source/helpers/StationSettings.cpp
    Source/helpers/SecretStore.h
    Source/helpers/SecretStore.cpp
    Source/generator/GeneratorSettings.h
    Source/generator/TemplateManager.h
    Source/generator/TemplateManager.cpp
    Source/generator/PlaylistGenerator.h
    Source/generator/PlaylistGenerator.cpp
    Source/ui/PlaylistGeneratorDialog.h
    Source/ui/PlaylistGeneratorDialog.cpp
    Source/ui/AdvancedImportDialog.h
    Source/ui/AdvancedImportDialog.cpp
)

target_compile_features(RAAADIO PRIVATE cxx_std_17)

# Los MP3 se copian tal cual a la biblioteca y se decodifican con el lector de JUCE
# (sin él los leería Windows Media, dentro del hilo de audio).
target_compile_definitions(RAAADIO PRIVATE JUCE_USE_MP3AUDIOFORMAT=1)

if(MSVC)
    # /utf-8: las fuentes están en UTF-8 (textos con tildes y eñes).
    target_compile_options(RAAADIO PRIVATE /FS /utf-8)
endif()

# Recursos incrustados en el ejecutable: logo, imagen de "Acerca de" (una copia reducida
# de RAAADIO.png, que queda como arte original) y textos de la interfaz. Así basta copiar
# el .exe a otro equipo; no depende de la carpeta Assets.
juce_add_binary_data(RAAADIO_BinaryData SOURCES
    "Assets/RAAADIO ICON.png"
    "Assets/about-banner.png"
    "Assets/lang_en.txt"
    "Assets/lang_es.txt"
)

target_link_libraries(RAAADIO PRIVATE
    RAAADIO_BinaryData
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_events
    juce::juce_core
    juce::juce_data_structures
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_cryptography
    sqlite3
    ebur128
)

juce_generate_juce_header(RAAADIO)

# Pruebas: persistencia (rutas portables, registro de emisión, cola), mezclador,
# análisis de audio (LUFS, puntos de entrada y salida), etiquetas, importación, generador,
# horarios y pauta, protección contra silencio, registro por hora, botonera y transmisión.
# Ejecutar con: ctest -C Release --test-dir build   (o el .exe directamente)
option(RAAADIO_BUILD_TESTS "Compilar las pruebas de RAAADIO" ON)
if(RAAADIO_BUILD_TESTS)
    juce_add_console_app(RAAADIO_Tests PRODUCT_NAME "RAAADIO_Tests")
    juce_generate_juce_header(RAAADIO_Tests)

    target_sources(RAAADIO_Tests PRIVATE
        Tests/RaaadioTests.cpp
        Source/database/Database.cpp
        Source/library/MusicLibrary.cpp
        Source/library/Playlist.cpp
        Source/library/PlaylistStore.cpp
        Source/library/HistoryManager.cpp
        Source/helpers/PersistenceManager.cpp
        Source/audio/Deck.cpp
        Source/audio/Mixer.cpp
        Source/audio/RecordingManager.cpp
        Source/audio/TrackAnalyzer.cpp
        Source/library/TagReader.cpp
        Source/library/AdvertisingManager.cpp
        Source/generator/PlaylistGenerator.cpp
        Source/autodj/QueueTiming.cpp
        Source/autodj/OnAir.cpp
        Source/autodj/AdScheduler.cpp
        Source/autodj/DeadAirGuard.cpp
        Source/autodj/AutoDJ.cpp
        Source/audio/AudioEngine.cpp
        Source/audio/AirLogRecorder.cpp
        Source/audio/CartPlayer.cpp
        Source/network/StreamEncoder.cpp
        Source/helpers/StationSettings.cpp
        Source/helpers/SecretStore.cpp
    )

    target_compile_features(RAAADIO_Tests PRIVATE cxx_std_17)
    target_compile_definitions(RAAADIO_Tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    if(MSVC)
        target_compile_options(RAAADIO_Tests PRIVATE /FS /utf-8)
    endif()

    target_link_libraries(RAAADIO_Tests PRIVATE
        juce::juce_audio_utils
        juce::juce_data_structures
        juce::juce_cryptography
        juce::juce_dsp
        sqlite3
        ebur128
    )

    enable_testing()
    add_test(NAME RAAADIO_Tests COMMAND RAAADIO_Tests)
endif()
