#pragma once
#include <JuceHeader.h>
#include <vector>

enum class InsertionMode {
    AddToEnd,
    AddToStart,
    Replace
};

// Lo que se le pide al generador de listas. Los valores por defecto sirven tal cual: una hora
// de toda la música, al final de la cola, con las reglas de separación habituales.
struct GeneratorSettings {
    // Categorías: sin ninguna incluida vale toda la música; las excluidas nunca entran.
    std::vector<int> includedCategories;
    std::vector<int> excludedCategories;

    // Reglas de separación (0 = sin esa regla).
    int artistSeparation = 5;       // canciones antes de repetir un artista
    int titleSeparation = 10;       // canciones antes de repetir un título
    int categorySeparation = 3;     // canciones antes de repetir una categoría
    int lastPlayedSeparation = 60;  // minutos desde que sonó por última vez

    // Cuánta música.
    int targetDurationVal = 60;
    bool targetDurationIsHours = false;

    // Separadores: una pieza de esa categoría cada separatorInterval canciones (0 = ninguno).
    int separatorCategoryId = -1;
    int separatorInterval = 0;

    InsertionMode insertionMode = InsertionMode::AddToEnd;

    double targetSeconds() const { return targetDurationVal * (targetDurationIsHours ? 3600.0 : 60.0); }
    bool usesSeparators() const { return separatorCategoryId != -1 && separatorInterval > 0; }
    // Las reglas de separación son las de fábrica.
    bool hasDefaultRules() const {
        const GeneratorSettings d;
        return artistSeparation == d.artistSeparation && titleSeparation == d.titleSeparation
            && categorySeparation == d.categorySeparation && lastPlayedSeparation == d.lastPlayedSeparation;
    }

    juce::var toVar() const {
        juce::DynamicObject* obj = new juce::DynamicObject();

        juce::Array<juce::var> inc, exc;
        for (auto i : includedCategories) inc.add(i);
        for (auto i : excludedCategories) exc.add(i);

        obj->setProperty("includedCategories", inc);
        obj->setProperty("excludedCategories", exc);
        obj->setProperty("artistSeparation", artistSeparation);
        obj->setProperty("titleSeparation", titleSeparation);
        obj->setProperty("categorySeparation", categorySeparation);
        obj->setProperty("lastPlayedSeparation", lastPlayedSeparation);
        obj->setProperty("targetDurationVal", targetDurationVal);
        obj->setProperty("targetDurationIsHours", targetDurationIsHours);
        obj->setProperty("separatorCategoryId", separatorCategoryId);
        obj->setProperty("separatorInterval", separatorInterval);
        obj->setProperty("insertionMode", static_cast<int>(insertionMode));

        return obj;
    }

    // Lo que falte (una plantilla de otra versión, un archivo a medias) queda con su valor por
    // defecto, no en cero.
    static GeneratorSettings fromVar(const juce::var& v) {
        GeneratorSettings s;
        if (!v.isObject()) return s;

        auto* obj = v.getDynamicObject();
        auto number = [obj](const char* name, int fallback, int lowest, int highest) {
            return obj->hasProperty(name) ? juce::jlimit(lowest, highest, (int) obj->getProperty(name)) : fallback;
        };

        if (auto* inc = obj->getProperty("includedCategories").getArray())
            for (auto& i : *inc) s.includedCategories.push_back(static_cast<int>(i));
        if (auto* exc = obj->getProperty("excludedCategories").getArray())
            for (auto& i : *exc) s.excludedCategories.push_back(static_cast<int>(i));

        s.artistSeparation = number("artistSeparation", s.artistSeparation, 0, 999);
        s.titleSeparation = number("titleSeparation", s.titleSeparation, 0, 999);
        s.categorySeparation = number("categorySeparation", s.categorySeparation, 0, 999);
        s.lastPlayedSeparation = number("lastPlayedSeparation", s.lastPlayedSeparation, 0, 99999);
        s.targetDurationVal = number("targetDurationVal", s.targetDurationVal, 1, 9999);
        if (obj->hasProperty("targetDurationIsHours")) s.targetDurationIsHours = obj->getProperty("targetDurationIsHours");
        s.separatorCategoryId = number("separatorCategoryId", s.separatorCategoryId, -1, std::numeric_limits<int>::max());
        s.separatorInterval = number("separatorInterval", s.separatorInterval, 0, 999);
        s.insertionMode = static_cast<InsertionMode>(number("insertionMode", 0, 0, 2));

        return s;
    }
};
