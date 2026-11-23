#pragma once
#include <JuceHeader.h>

// Los diálogos del sistema para elegir una carpeta o archivos. holder mantiene vivo el diálogo
// mientras está abierto, como un miembro de quien lo pide. Las pruebas cambian el diálogo por
// una respuesta (setHandler) para no abrir ventanas.
namespace FileChoosers
{
    enum class Kind { Folder, Files };

    // Lo elegido; vacío si se canceló.
    using Handler = std::function<void(Kind kind, const juce::String& title,
                                       std::function<void(const juce::Array<juce::File>&)> onChosen)>;

    inline Handler& handler()
    {
        static Handler current;
        return current;
    }

    // Vacío: vuelve a los diálogos del sistema.
    inline void setHandler(Handler newHandler) { handler() = std::move(newHandler); }

    // juce::File() si se cancela.
    inline void chooseFolder(std::unique_ptr<juce::FileChooser>& holder, const juce::String& title,
                             const juce::File& start, std::function<void(const juce::File&)> onChosen)
    {
        auto deliver = [onChosen = std::move(onChosen)](const juce::Array<juce::File>& chosen) {
            if (onChosen) onChosen(chosen.isEmpty() ? juce::File() : chosen.getFirst());
        };
        if (handler())
        {
            handler()(Kind::Folder, title, deliver);
            return;
        }
        holder = std::make_unique<juce::FileChooser>(title, start);
        holder->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                            [deliver](const juce::FileChooser& fc) { deliver(fc.getResults()); });
    }

    // Vacío si se cancela.
    inline void chooseFiles(std::unique_ptr<juce::FileChooser>& holder, const juce::String& title,
                            const juce::File& start, const juce::String& wildcard,
                            std::function<void(const juce::Array<juce::File>&)> onChosen)
    {
        if (handler())
        {
            handler()(Kind::Files, title, std::move(onChosen));
            return;
        }
        holder = std::make_unique<juce::FileChooser>(title, start, wildcard);
        holder->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectMultipleItems,
                            [onChosen = std::move(onChosen)](const juce::FileChooser& fc) {
                                if (onChosen) onChosen(fc.getResults());
                            });
    }
}
