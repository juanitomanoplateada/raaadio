 

#pragma once

#include <JuceHeader.h>
#include "AdsListView.h"
#include "ScheduleView.h"
#include "../library/AdvertisingManager.h"
#include "../library/MusicLibrary.h"

class AdvertisingComponent : public juce::Component
{
public:
    AdvertisingComponent(MusicLibrary& library, AdvertisingManager& adsManager);
    ~AdvertisingComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    
    void refresh();
    
     
    void setDecks(Deck* dA, Deck* dB);
    void setPlaylist(Playlist* p);
    void setCartPlayer(CartPlayer* player) { adsList.setCartPlayer(player); }

private:
    AdsListView adsList;
    ScheduleView scheduleView;
    
     
    juce::StretchableLayoutManager layout;
    juce::StretchableLayoutResizerBar resizerBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdvertisingComponent)
};
