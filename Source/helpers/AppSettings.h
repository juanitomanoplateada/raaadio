#pragma once
#include <JuceHeader.h>

// Dónde guarda RAAADIO las preferencias del equipo (raaadio.settings en la carpeta de datos de
// la aplicación): idioma, tema, dispositivos de audio y ubicación de la carpeta de la emisora.
// Las pruebas usan otra carpeta para no tocar las del usuario.
namespace AppSettings
{
    inline juce::File& folderOverride()
    {
        static juce::File folder;
        return folder;
    }

    // Una carpeta absoluta en lugar de la habitual; juce::File() vuelve a la habitual.
    inline void setFolderOverride(const juce::File& folder) { folderOverride() = folder; }

    inline juce::PropertiesFile::Options options()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "raaadio";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        // Con una ruta absoluta, JUCE usa esa carpeta en vez de la de datos de la aplicación.
        options.folderName = folderOverride() == juce::File() ? juce::String("raaadio")
                                                              : folderOverride().getFullPathName();
        return options;
    }
}
