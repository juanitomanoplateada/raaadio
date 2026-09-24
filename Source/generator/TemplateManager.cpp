#include "TemplateManager.h"
#include "../helpers/PersistenceManager.h"

TemplateManager::TemplateManager() {
    templatesDir = PersistenceManager::getTemplatesFolder();
}

void TemplateManager::ensureTemplatesDirExists() {
    if (!templatesDir.exists()) {
        templatesDir.createDirectory();
    }
}

void TemplateManager::saveTemplate(const juce::String& name, const GeneratorSettings& settings) {
    ensureTemplatesDirExists();
    
    juce::File file = templatesDir.getChildFile(name + ".json");
    juce::var json = settings.toVar();
    juce::String jsonString = juce::JSON::toString(json);
    
    file.replaceWithText(jsonString);
}

GeneratorSettings TemplateManager::loadTemplate(const juce::String& name) {
    ensureTemplatesDirExists();
    
    juce::File file = templatesDir.getChildFile(name + ".json");
    if (!file.exists()) return GeneratorSettings();
    
    juce::var json = juce::JSON::parse(file);
    return GeneratorSettings::fromVar(json);
}

juce::StringArray TemplateManager::getTemplateNames() {
    ensureTemplatesDirExists();
    
    juce::StringArray names;
    auto files = templatesDir.findChildFiles(juce::File::findFiles, false, "*.json");
    
    for (auto& f : files) {
        names.add(f.getFileNameWithoutExtension());
    }
    
    return names;
}

void TemplateManager::deleteTemplate(const juce::String& name) {
    ensureTemplatesDirExists();
    juce::File file = templatesDir.getChildFile(name + ".json");
    if (file.exists()) file.deleteFile();
}
