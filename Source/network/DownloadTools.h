#pragma once
#include <JuceHeader.h>

// Los programas que usa la descarga de canciones: yt-dlp (baja el audio de YouTube), FFmpeg
// (lo convierte a MP3) y un motor de JavaScript (YouTube lo exige a yt-dlp desde 2025: Deno, o
// Node si ya está instalado). Van en una carpeta propia de RAAADIO, fuera del ejecutable y de
// la emisora, cada uno con su licencia; se bajan de sus publicaciones oficiales en GitHub y
// se verifica su SHA-256 antes de usarlos. Lógica pura: quien descarga es SongDownloader.
namespace DownloadTools {

struct Paths {
    juce::File ytDlp;                 // yt-dlp.exe
    juce::File ffmpegFolder;          // la carpeta con ffmpeg.exe y ffprobe.exe
    juce::File jsRuntime;             // deno.exe o node.exe; vacío si no hay
    juce::String jsRuntimeName;       // "deno" o "node"
    bool managedYtDlp = false;        // yt-dlp está en la carpeta de RAAADIO (se puede actualizar solo)

    bool ready() const { return ytDlp.existsAsFile() && ffmpegFolder.getChildFile(exe("ffmpeg")).existsAsFile(); }
    static juce::String exe(const juce::String& name);  // "ffmpeg.exe" en Windows
};

// %LOCALAPPDATA%\RAAADIO\Herramientas en Windows.
juce::File defaultFolder();

// Lo de la carpeta de RAAADIO primero; si falta algo, lo que haya en el PATH del sistema.
Paths find(const juce::File& folder, const juce::String& pathVariable);

// Un programa por instalar: de dónde se baja, dónde está su SHA-256 y qué se extrae.
struct Asset {
    juce::String name;          // "yt-dlp", "FFmpeg", "Deno"
    juce::String url;
    juce::String checksumUrl;
    juce::String checksumName;  // el nombre del archivo en la lista de checksums
    bool zip = false;
    juce::String zipFolder;     // del zip se extrae lo que está en esta carpeta ("bin/"); vacío: todo
    juce::StringArray skip;     // archivos del zip que no hacen falta (ffplay.exe)
    juce::File destination;     // el archivo (si no es zip) o la carpeta donde se extrae
    int approxMegabytes = 0;
};

// Lo que falta instalar en folder, en orden (lo que ya se encontró en el PATH no se baja).
std::vector<Asset> missing(const Paths& found, const juce::File& folder);
int totalMegabytes(const std::vector<Asset>& assets);

// El SHA-256 de fileName en una lista de checksums ("<hex>  archivo", o "Hash : <HEX>" de las
// de Deno); vacío si no está. En minúsculas.
juce::String findSha256(const juce::String& checksums, const juce::String& fileName);

// Extrae de zipFile las entradas de asset.zipFolder (sin sus carpetas) a asset.destination.
// Devuelve cuántos archivos extrajo (0 si el zip no se pudo leer).
int extract(const juce::File& zipFile, const Asset& asset);

}
