#include "LanguageManager.h"
#include "AppSettings.h"
#include "BinaryData.h"

LanguageManager& LanguageManager::getInstance()
{
    static LanguageManager instance;
    return instance;
}

void LanguageManager::initialise()
{
     
    juce::ApplicationProperties appProperties;
    appProperties.setStorageParameters(AppSettings::options());

    auto* props = appProperties.getUserSettings();
    juce::String savedLang = props->getValue("ui.language", "en");

    setLanguage(savedLang);
}

void LanguageManager::setLanguage(const juce::String& languageCode, bool persist)
{
    currentLanguageCode = languageCode;
    loadLanguageFile(languageCode);

    if (persist)
    {
        juce::ApplicationProperties appProperties;
        appProperties.setStorageParameters(AppSettings::options());

        if (auto* props = appProperties.getUserSettings())
        {
            props->setValue("ui.language", currentLanguageCode);
            props->saveIfNeeded();
        }
    }

     
    if (auto* app = juce::JUCEApplication::getInstance())
    {
         
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
        {
            if (auto* c = desktop.getComponent(i))
                c->sendLookAndFeelChange();
        }
    }
}

juce::String LanguageManager::getCurrentLanguage() const
{
    return currentLanguageCode;
}

std::vector<std::pair<juce::String, juce::String>> LanguageManager::getAvailableLanguages() const
{
    return {
        { "English", "en" },
        { juce::String(juce::CharPointer_UTF8("Español")), "es" }
    };
}


// Los textos de cada idioma van dentro del ejecutable (Assets/lang_*.txt, ver CMakeLists).
void LanguageManager::loadLanguageFile(const juce::String& code)
{
    int size = 0;
    const auto resource = "lang_" + code + "_txt";
    const char* data = BinaryData::getNamedResource(resource.toRawUTF8(), size);

    if (data == nullptr || size <= 0)
    {
        juce::Logger::writeToLog("LanguageManager: no texts for language " + code);
        if (code != "en")
            loadLanguageFile("en");
        else
            juce::LocalisedStrings::setCurrentMappings(nullptr);
        return;
    }

    juce::LocalisedStrings::setCurrentMappings(new juce::LocalisedStrings(juce::String::fromUTF8(data, size), false));
    juce::Logger::writeToLog("LanguageManager: loaded language " + code);
}
