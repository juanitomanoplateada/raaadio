#include "TemplateManager.h"
#include "../library/MusicLibrary.h"

TemplateManager::TemplateManager(MusicLibrary& lib) : library(lib) {}

void TemplateManager::saveTemplate(const juce::String& name, const GeneratorSettings& settings) {
    const auto json = juce::JSON::toString(settings.toVar(), true).toStdString();
    const auto key = name.trim().toStdString();
    if (key.empty()) return;
    if (!library.withDatabase([&](Database& db) { return db.saveTemplate(key, json); }))
        juce::Logger::writeToLog("TemplateManager: could not save template " + name);
}

GeneratorSettings TemplateManager::loadTemplate(const juce::String& name) {
    const auto key = name.trim().toStdString();
    const auto json = library.withDatabase([&key](Database& db) { return db.loadTemplate(key); });
    if (json.empty()) return GeneratorSettings();
    return GeneratorSettings::fromVar(juce::JSON::parse(juce::String(json)));
}

juce::StringArray TemplateManager::getTemplateNames() {
    juce::StringArray names;
    for (const auto& name : library.withDatabase([](Database& db) { return db.getTemplateNames(); }))
        names.add(juce::String(name));
    return names;
}

void TemplateManager::deleteTemplate(const juce::String& name) {
    const auto key = name.trim().toStdString();
    library.withDatabase([&key](Database& db) { return db.deleteTemplate(key); });
}
