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
# FTS5: búsqueda de la biblioteca sin mayúsculas ni tildes.
target_compile_definitions(sqlite3 PRIVATE SQLITE_ENABLE_FTS5)
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

# LAME 3.100 (LGPL): codificador MP3 para la transmisión por internet. Se compila como
# biblioteca aparte (libmp3lame.dll, junto al ejecutable) y RAAADIO la carga al transmitir
# en MP3: así la LGPL se cumple (se puede reemplazar la DLL) y RAAADIO sigue siendo MIT.
# Sin la DLL, RAAADIO funciona igual y transmite en Ogg Vorbis.
FetchContent_Declare(
    lame
    URL https://deb.debian.org/debian/pool/main/l/lame/lame_3.100.orig.tar.gz
        https://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz
    URL_HASH SHA256=ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake
)
FetchContent_MakeAvailable(lame)

set(LAME_SOURCES
    libmp3lame/bitstream.c libmp3lame/encoder.c libmp3lame/fft.c libmp3lame/gain_analysis.c
    libmp3lame/id3tag.c libmp3lame/lame.c libmp3lame/newmdct.c libmp3lame/psymodel.c
    libmp3lame/quantize.c libmp3lame/quantize_pvt.c libmp3lame/vector/xmm_quantize_sub.c
    libmp3lame/set_get.c libmp3lame/vbrquantize.c libmp3lame/reservoir.c libmp3lame/tables.c
    libmp3lame/takehiro.c libmp3lame/util.c libmp3lame/mpglib_interface.c libmp3lame/VbrTag.c
    libmp3lame/presets.c libmp3lame/version.c
    mpglib/common.c mpglib/dct64_i386.c mpglib/decode_i386.c mpglib/layer1.c mpglib/layer2.c
    mpglib/layer3.c mpglib/tabinit.c mpglib/interface.c
)
list(TRANSFORM LAME_SOURCES PREPEND ${lame_SOURCE_DIR}/)

if(MSVC)
    # La configuración para Visual C++ que trae LAME, como config.h.
    configure_file(${lame_SOURCE_DIR}/configMS.h ${CMAKE_CURRENT_BINARY_DIR}/lame_config/config.h COPYONLY)
    add_library(mp3lame SHARED ${LAME_SOURCES} ${lame_SOURCE_DIR}/include/lame.def)
    target_include_directories(mp3lame PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/lame_config
        ${lame_SOURCE_DIR}/include ${lame_SOURCE_DIR}/libmp3lame ${lame_SOURCE_DIR}/mpglib)
    target_compile_definitions(mp3lame PRIVATE
        HAVE_CONFIG_H HAVE_MPGLIB TAKEHIRO_IEEE754_HACK NDEBUG _CRT_SECURE_NO_WARNINGS)
    target_compile_options(mp3lame PRIVATE /W0)
    set_target_properties(mp3lame PROPERTIES OUTPUT_NAME libmp3lame)
endif()

# Chromaprint 1.5.1 (LGPL 2.1 por el remuestreo de FFmpeg que trae; el resto es MIT): la huella
# que entiende AcoustID, para identificar canciones en línea. Como LAME, va como biblioteca
# aparte (chromaprint.dll) que RAAADIO carga si está; sin ella todo funciona menos la búsqueda
# en línea. Se compila con kissfft (incluido), sin FFmpeg.
FetchContent_Declare(
    chromaprint
    URL https://github.com/acoustid/chromaprint/releases/download/v1.5.1/chromaprint-1.5.1.tar.gz
    URL_HASH SHA256=a1aad8fa3b8b18b78d3755b3767faff9abb67242e01b478ec9a64e190f335e1c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR no-cmake
)
FetchContent_MakeAvailable(chromaprint)

if(MSVC)
    set(CHROMAPRINT_SOURCES
        audio_processor.cpp chroma.cpp chroma_resampler.cpp chroma_filter.cpp spectrum.cpp fft.cpp
        fingerprinter.cpp image_builder.cpp simhash.cpp silence_remover.cpp fingerprint_calculator.cpp
        fingerprint_compressor.cpp fingerprint_decompressor.cpp fingerprinter_configuration.cpp
        fingerprint_matcher.cpp utils/base64.cpp avresample/resample2.c fft_lib_kissfft.cpp chromaprint.cpp
        3rdparty/kissfft/kiss_fft.c 3rdparty/kissfft/tools/kiss_fftr.c
    )
    list(TRANSFORM CHROMAPRINT_SOURCES PREPEND ${chromaprint_SOURCE_DIR}/src/)
    # Lo que su CMake averigua al configurar, escrito a mano para Visual C++.
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/chromaprint_config/config.h
         "#define HAVE_ROUND 1\n#define HAVE_LRINTF 1\n#define USE_KISSFFT 1\n")
    add_library(chromaprint SHARED ${CHROMAPRINT_SOURCES})
    target_include_directories(chromaprint PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/chromaprint_config
        ${chromaprint_SOURCE_DIR}/src ${chromaprint_SOURCE_DIR}/src/3rdparty/kissfft)
    target_compile_definitions(chromaprint PRIVATE
        HAVE_CONFIG_H CHROMAPRINT_API_EXPORTS _SCL_SECURE_NO_WARNINGS _CRT_SECURE_NO_WARNINGS
        _USE_MATH_DEFINES __STDC_LIMIT_MACROS __STDC_CONSTANT_MACROS)
    target_compile_features(chromaprint PRIVATE cxx_std_11)
    target_compile_options(chromaprint PRIVATE /W0)
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
    Source/audio/PreviewPlayer.h
    Source/audio/PreviewPlayer.cpp
    Source/audio/AcousticFingerprint.h
    Source/audio/AcousticFingerprint.cpp
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
    Source/network/StreamSocket.h
    Source/network/StreamSocket.cpp
    Source/network/Mp3Encoder.h
    Source/network/Mp3Encoder.cpp
    Source/network/NowPlaying.h
    Source/network/NowPlaying.cpp
    Source/network/NowPlayingPublisher.h
    Source/network/NowPlayingPublisher.cpp
    Source/network/Chromaprint.h
    Source/network/Chromaprint.cpp
    Source/network/OnlineMetadata.h
    Source/network/OnlineMetadata.cpp
    Source/network/OnlineIdentifier.h
    Source/network/OnlineIdentifier.cpp
    Source/network/HttpTransport.h
    Source/network/HttpTransport.cpp
    Source/network/SongSearch.h
    Source/network/SongSearch.cpp
    Source/network/SongSearcher.h
    Source/network/SongSearcher.cpp
    Source/network/DownloadTools.h
    Source/network/DownloadTools.cpp
    Source/network/SongDownload.h
    Source/network/SongDownload.cpp
    Source/network/SongDownloader.h
    Source/network/SongDownloader.cpp
    Source/helpers/ProcessRunner.h
    Source/helpers/ProcessRunner.cpp
    Source/library/TagWriter.h
    Source/library/TagWriter.cpp
    Source/library/MusicLibrary.h
    Source/library/AudioConverter.h
    Source/library/AudioConverter.cpp
    Source/library/MusicLibrary.cpp
    Source/library/TagReader.h
    Source/library/TagReader.cpp
    Source/library/TrackNaming.h
    Source/library/TrackNaming.cpp
    Source/library/ImportReview.h
    Source/library/ImportReview.cpp
    Source/library/Playlist.h
    Source/library/Playlist.cpp
    Source/library/PlaylistStore.h
    Source/library/PlaylistStore.cpp
    Source/database/Database.h
    Source/database/Database.cpp
    Source/database/StationState.cpp
    Source/database/Catalog.cpp
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
    Source/helpers/BackupService.h
    Source/helpers/BackupService.cpp
    Source/helpers/StationHealth.h
    Source/helpers/StationHealth.cpp
    Source/helpers/StationLock.h
    Source/helpers/StationLock.cpp
    Source/generator/GeneratorSettings.h
    Source/generator/TemplateManager.h
    Source/generator/TemplateManager.cpp
    Source/generator/PlaylistGenerator.h
    Source/generator/PlaylistGenerator.cpp
    Source/ui/PlaylistGeneratorDialog.h
    Source/ui/PlaylistGeneratorDialog.cpp
    Source/ui/AdvancedImportDialog.h
    Source/ui/AdvancedImportDialog.cpp
    Source/ui/CreditsEditor.h
    Source/ui/CreditsEditor.cpp
    Source/ui/ImportReviewView.h
    Source/ui/ImportReviewView.cpp
    Source/ui/ArtistsView.h
    Source/ui/ArtistsView.cpp
    Source/ui/OnlineSearchView.h
    Source/ui/OnlineSearchView.cpp
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

# libmp3lame.dll y su licencia van junto a cada ejecutable que transmite.
function(raaadio_copy_lame target)
    if(TARGET mp3lame)
        add_dependencies(${target} mp3lame)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:mp3lame> $<TARGET_FILE_DIR:${target}>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${lame_SOURCE_DIR}/COPYING $<TARGET_FILE_DIR:${target}>/libmp3lame-COPYING.txt)
    endif()
endfunction()
raaadio_copy_lame(RAAADIO)

# chromaprint.dll y su licencia, junto a cada ejecutable que identifica canciones en línea.
function(raaadio_copy_chromaprint target)
    if(TARGET chromaprint)
        add_dependencies(${target} chromaprint)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:chromaprint> $<TARGET_FILE_DIR:${target}>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${chromaprint_SOURCE_DIR}/LICENSE.md $<TARGET_FILE_DIR:${target}>/chromaprint-LICENSE.md)
    endif()
endfunction()
raaadio_copy_chromaprint(RAAADIO)

# Pruebas: persistencia (rutas portables, registro de emisión, cola), mezclador,
# análisis de audio (LUFS, puntos de entrada y salida), etiquetas, importación, generador,
# horarios y pauta, protección contra silencio, registro por hora, botonera, transmisión
# (Ogg Vorbis y MP3) y los datos de lo que suena.
# Ejecutar con: ctest -C Release --test-dir build   (o el .exe directamente)
option(RAAADIO_BUILD_TESTS "Compilar las pruebas de RAAADIO" ON)
if(RAAADIO_BUILD_TESTS)
    juce_add_console_app(RAAADIO_Tests PRODUCT_NAME "RAAADIO_Tests")
    juce_generate_juce_header(RAAADIO_Tests)

    target_sources(RAAADIO_Tests PRIVATE
        Tests/RaaadioTests.cpp
        Tests/CrashReport.cpp
        Source/database/Database.cpp
        Source/database/StationState.cpp
        Source/database/Catalog.cpp
        Source/library/MusicLibrary.cpp
        Source/library/AudioConverter.cpp
        Source/library/Playlist.cpp
        Source/library/PlaylistStore.cpp
        Source/library/HistoryManager.cpp
        Source/helpers/PersistenceManager.cpp
        Source/audio/Deck.cpp
        Source/audio/Mixer.cpp
        Source/audio/RecordingManager.cpp
        Source/audio/TrackAnalyzer.cpp
        Source/library/TagReader.cpp
        Source/library/TrackNaming.cpp
        Source/library/ImportReview.cpp
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
        Source/audio/PreviewPlayer.cpp
        Source/audio/AcousticFingerprint.cpp
        Source/network/StreamEncoder.cpp
        Source/network/StreamSocket.cpp
        Source/network/Mp3Encoder.cpp
        Source/network/NowPlaying.cpp
        Source/network/ApiServer.cpp
        Source/network/NowPlayingPublisher.cpp
        Source/network/Chromaprint.cpp
        Source/network/OnlineMetadata.cpp
        Source/network/OnlineIdentifier.cpp
        Source/network/HttpTransport.cpp
        Source/network/SongSearch.cpp
        Source/network/SongSearcher.cpp
        Source/network/DownloadTools.cpp
        Source/network/SongDownload.cpp
        Source/network/SongDownloader.cpp
        Source/helpers/ProcessRunner.cpp
        Source/library/TagWriter.cpp
        Source/helpers/StationSettings.cpp
        Source/helpers/SecretStore.cpp
        Source/helpers/BackupService.cpp
        Source/helpers/StationHealth.cpp
        Source/helpers/StationLock.cpp
        Source/generator/TemplateManager.cpp
        # La interfaz también se prueba: se construye, se dibuja y se opera sin ventana.
        Source/MainComponent.cpp
        Source/helpers/LanguageManager.cpp
        Source/helpers/RaaadioLookAndFeel.cpp
        Source/helpers/ThemeManager.cpp
        Source/helpers/UiStyle.cpp
        Source/helpers/UiSnapshot.cpp
        Source/ui/AdsListView.cpp
        Source/ui/AdvancedImportDialog.cpp
        Source/ui/AdvertisingComponent.cpp
        Source/ui/ArtistsView.cpp
        Source/ui/AudioSettingsDialog.cpp
        Source/ui/CartwallView.cpp
        Source/ui/CategorySidebarComponent.cpp
        Source/ui/CreditsEditor.cpp
        Source/ui/DeckComponent.cpp
        Source/ui/HistoryView.cpp
        Source/ui/ImportReviewView.cpp
        Source/ui/LibraryComponent.cpp
        Source/ui/LibraryView.cpp
        Source/ui/MixerComponent.cpp
        Source/ui/OnlineSearchView.cpp
        Source/ui/PlaylistComponent.cpp
        Source/ui/PlaylistGeneratorDialog.cpp
        Source/ui/RecordingPanel.cpp
        Source/ui/ScheduleView.cpp
        Source/ui/StatusBar.cpp
        Source/ui/TrackDetailsDialog.cpp
        Source/ui/WaveformComponent.cpp
    )

    target_compile_features(RAAADIO_Tests PRIVATE cxx_std_17)
    target_compile_definitions(RAAADIO_Tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    if(MSVC)
        # Con .pdb también en Release: si una prueba se cae, Tests/CrashReport.cpp dice dónde.
        target_compile_options(RAAADIO_Tests PRIVATE $<$<CONFIG:Release>:/Zi>)
        target_link_options(RAAADIO_Tests PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
    endif()
    if(MSVC)
        target_compile_options(RAAADIO_Tests PRIVATE /FS /utf-8)
    endif()

    target_link_libraries(RAAADIO_Tests PRIVATE
        RAAADIO_BinaryData
        juce::juce_audio_utils
        juce::juce_data_structures
        juce::juce_cryptography
        juce::juce_dsp
        sqlite3
        ebur128
    )

    raaadio_copy_lame(RAAADIO_Tests)
    raaadio_copy_chromaprint(RAAADIO_Tests)

    enable_testing()
    add_test(NAME RAAADIO_Tests COMMAND RAAADIO_Tests)
endif()
