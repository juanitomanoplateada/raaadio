#pragma once
#include <JuceHeader.h>
#include "../library/TrackNaming.h"

// Campos de una canción: intérprete principal (con los artistas que ya están en la biblioteca
// como sugerencias mientras se escribe), invitados, título, versión, año y compositores. Lo
// usan la revisión de lo importado y el diálogo de una pista. Sin créditos (cuñas,
// identificaciones, separadores) quedan solo artista y título.
class CreditsEditor : public juce::Component {
public:
    CreditsEditor();

    void setValues(const TrackNaming::Proposal& values);
    TrackNaming::Proposal getValues() const;
    bool hasChanges() const { return edited; }
    void clearChanges() { edited = false; }

    void setCreditsVisible(bool visible);
    // Artistas conocidos para sugerir mientras se escribe (puede ser nullptr).
    void setContext(const TrackNaming::Context* context) { namingContext = context; }
    void focusArtist();

    std::function<void()> onCommit;  // Enter en cualquier campo
    std::function<void()> onEdited;  // el operador cambió algo

    int getIdealHeight() const;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    juce::Label artistLabel, featuredLabel, titleLabel, versionLabel, yearLabel, composersLabel;
    juce::TextEditor artist, featured, title, version, year, composers;
    juce::OwnedArray<juce::TextButton> suggestionButtons;
    const TrackNaming::Context* namingContext = nullptr;
    bool creditsVisible = true;
    bool edited = false;

    void updateSuggestions();
    juce::TextEditor* fields[6] = { &artist, &featured, &title, &version, &year, &composers };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CreditsEditor)
};
