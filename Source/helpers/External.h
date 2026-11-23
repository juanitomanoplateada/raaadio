#pragma once
#include <JuceHeader.h>

// Lo que la interfaz hace fuera de RAAADIO: abrir una dirección en el navegador, mostrar un
// archivo o una carpeta en el Explorador, abrir un archivo con el programa del sistema y
// copiar texto al portapapeles. Las pruebas cambian estas acciones (setHandler) para que
// nada se abra ni se copie en el equipo.
namespace External
{
    enum class Action { OpenUrl, Reveal, OpenFile, CopyText };

    using Handler = std::function<void(Action action, const juce::String& target)>;

    inline Handler& handler()
    {
        static Handler current;
        return current;
    }

    // Vacío: vuelve a las acciones del sistema.
    inline void setHandler(Handler newHandler) { handler() = std::move(newHandler); }

    inline void openUrl(const juce::String& url)
    {
        if (handler()) handler()(Action::OpenUrl, url);
        else juce::URL(url).launchInDefaultBrowser();
    }

    inline void reveal(const juce::File& file)
    {
        if (handler()) handler()(Action::Reveal, file.getFullPathName());
        else file.revealToUser();
    }

    inline void openFile(const juce::File& file)
    {
        if (handler()) handler()(Action::OpenFile, file.getFullPathName());
        else file.startAsProcess();
    }

    inline void copyText(const juce::String& text)
    {
        if (handler()) handler()(Action::CopyText, text);
        else juce::SystemClipboard::copyTextToClipboard(text);
    }
}
