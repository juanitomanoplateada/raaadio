#pragma once
#include <JuceHeader.h>
#include "GeneratorSettings.h"

class MusicLibrary;

// Plantillas del generador: ajustes guardados con un nombre, en la base de datos de la
// emisora (cualquier nombre sirve; ya no es un nombre de archivo).
class TemplateManager {
public:
    explicit TemplateManager(MusicLibrary& library);

    void saveTemplate(const juce::String& name, const GeneratorSettings& settings);
    GeneratorSettings loadTemplate(const juce::String& name);
    juce::StringArray getTemplateNames();
    void deleteTemplate(const juce::String& name);

private:
    MusicLibrary& library;
};
