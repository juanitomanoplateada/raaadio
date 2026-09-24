#pragma once
#include <JuceHeader.h>

class LanguageManager
{
public:
    static LanguageManager& getInstance();

    void initialise();
    // persist = false cambia el idioma solo en memoria (lo usa el modo de captura de UI).
    void setLanguage(const juce::String& languageCode, bool persist = true);
    juce::String getCurrentLanguage() const;



    std::vector<std::pair<juce::String, juce::String>> getAvailableLanguages() const;

private:
    LanguageManager() = default;
    ~LanguageManager() = default;

    void loadLanguageFile(const juce::String& code);

    juce::String currentLanguageCode = "en";
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LanguageManager)
};
