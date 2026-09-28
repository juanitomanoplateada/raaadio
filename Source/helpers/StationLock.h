#pragma once
#include <JuceHeader.h>
#include <optional>

// Evita que dos equipos abran la misma emisora a la vez (un USB o un disco de red), lo que
// dañaría la base de datos. En el mismo equipo ya lo impide la instancia única. Es un
// archivo raaadio.lock en la carpeta de la emisora con el equipo, el usuario y un latido
// cada 30 s: si el latido tiene más de 2 minutos, el equipo que la tenía se cerró mal y el
// candado se toma sin preguntar.
class StationLock : private juce::Timer {
public:
    explicit StationLock(juce::File labRoot);
    ~StationLock() override;

    struct Owner {
        juce::String computer, user;
        juce::Time heartbeat;
    };

    // Otro equipo con la emisora abierta ahora mismo; vacío si está libre.
    std::optional<Owner> otherOwner(juce::Time now = juce::Time::getCurrentTime()) const;

    void acquire();  // escribe el candado y empieza el latido
    void release();  // lo borra, si es de este equipo

    static constexpr int heartbeatSeconds = 30;
    static constexpr int staleSeconds = 120;

    // Partes separadas para las pruebas.
    static juce::String describe(const Owner& owner);
    static std::optional<Owner> parse(const juce::String& text);
    static juce::String thisComputer();

private:
    void timerCallback() override { write(); }
    void write();

    juce::File file;
    bool held = false;
};
