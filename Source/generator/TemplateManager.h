#pragma once
#include <JuceHeader.h>
#include "GeneratorSettings.h"

class TemplateManager {
public:
    TemplateManager();
    
    void saveTemplate(const juce::String& name, const GeneratorSettings& settings);
    GeneratorSettings loadTemplate(const juce::String& name);
    juce::StringArray getTemplateNames();
    void deleteTemplate(const juce::String& name);

private:
    juce::File templatesDir;
    void ensureTemplatesDirExists();
};
