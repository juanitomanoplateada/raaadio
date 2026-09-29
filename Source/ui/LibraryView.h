#pragma once

#include <JuceHeader.h>
#include "../library/MusicLibrary.h"
#include "CategorySidebarComponent.h"

class Deck;
class Playlist;
class CartPlayer;

class LibraryView : public juce::Component,
                    public juce::TableListBoxModel
{
public:
    LibraryView(MusicLibrary& library);
    ~LibraryView() override;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    void lookAndFeelChanged() override;

    void refresh();

    void setDecks(Deck* dA, Deck* dB);
    void setPlaylist(Playlist* p);
    void setCartPlayer(CartPlayer* player) { cartPlayer = player; }  // clic derecho: asignar a la botonera
    // Buscar en internet (Deezer y YouTube) lo escrito en el buscador.
    std::function<void(const juce::String& query)> onSearchOnline;

    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;
    void cellClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>& selectedRows) override;
    juce::String getCellTooltip(int rowNumber, int columnId) override;

private:
    MusicLibrary& library;

    CategorySidebarComponent categorySidebar;
    juce::TableListBox table;
    juce::TextButton refreshButton;
    juce::TextButton onlineButton;        // globo junto al buscador
    juce::TextButton onlineEmptyButton;   // "Buscar «...» en internet", si la biblioteca no la tiene
    juce::TextEditor searchBox;
    juce::Rectangle<int> countArea;

    juce::String currentCategoryFilter;
    std::vector<TrackInfo> cachedTracks;

    Deck* deckA = nullptr;
    Deck* deckB = nullptr;
    Playlist* playlist = nullptr;
    CartPlayer* cartPlayer = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryView)
};
