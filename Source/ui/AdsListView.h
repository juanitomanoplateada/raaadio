 

#pragma once

#include <JuceHeader.h>
#include "../library/MusicLibrary.h"
#include "../library/MusicLibrary.h"
#include "../library/AdvertisingManager.h"
#include "../audio/Deck.h"
#include "../library/Playlist.h"

class CartPlayer;

class AdsListView : public juce::Component, 
                    public juce::TableListBoxModel,
                    public juce::ChangeListener
{
public:
    AdsListView(MusicLibrary& library, AdvertisingManager& adsManager);
    ~AdsListView() override;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;  
    void visibilityChanged() override;  
    
     
    void setDecks(Deck* dA, Deck* dB);
    void setPlaylist(Playlist* p);
    void setCartPlayer(CartPlayer* player) { cartPlayer = player; }  // clic derecho: asignar a la botonera
    
     
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    
     
    void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;
    void cellClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;  
    
     
    juce::var getDragSourceDescription(const juce::SparseSet<int>& selectedRows) override;
    
     
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

     
    std::function<void(int)> onSelectionChanged;  
    void refreshList();

private:
    MusicLibrary& library;
    AdvertisingManager& adsManager;

    Deck* deckA = nullptr;
    Deck* deckB = nullptr;
    Playlist* playlist = nullptr;
    CartPlayer* cartPlayer = nullptr;

    juce::TableListBox table;
    juce::TextButton refreshButton;
    juce::Rectangle<int> titleArea;
    std::vector<TrackInfo> cachedTracks;
    std::set<int> identificationIds;

    void editCueNumber(int row);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdsListView)
};
