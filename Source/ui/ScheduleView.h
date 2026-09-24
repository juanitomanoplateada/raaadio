#pragma once

#include <JuceHeader.h>
#include "../library/AdvertisingManager.h"
#include "../library/MusicLibrary.h"
#include "../library/Playlist.h"
#include "../audio/Deck.h"

// Matriz de horarios de la pauta: qué cuñas (por número) salen en cada franja de
// 30 minutos, las 24 horas; el botón para insertar a mano la tanda de una franja, y la
// opción de que AdScheduler las inserte solas.
class ScheduleView : public juce::Component,
                     public juce::ChangeListener,
                     public juce::TableListBoxModel
{
public:
    ScheduleView(MusicLibrary& library, AdvertisingManager& adsManager);
    ~ScheduleView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    void setSelectedCueNumber(int number);
    void setPlaylist(Playlist* p);
    void setDecks(Deck* dA, Deck* dB);

    // Inserta en la cola la tanda (identificación + cuñas) de la franja indicada.
    void generateBlock(int row);

    juce::Component* refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected, juce::Component* existingComponentToUpdate) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    void cellClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;

private:
    MusicLibrary& library;
    AdvertisingManager& adsManager;
    Playlist* playlist = nullptr;
    Deck* deckA = nullptr;
    Deck* deckB = nullptr;

    juce::ToggleButton autoToggle;
    juce::Label inputLabel;
    juce::TextEditor numberInput;
    juce::TableListBox table;
    juce::Rectangle<int> titleArea;
    bool compactInsert = false;  // panel angosto: "Insertar tanda" solo con su icono

    juce::StringArray timeSlots;

    int getInputNumber() const;
    void buildTimeSlots();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScheduleView)
};
