 

#include "AdvertisingComponent.h"
#include "../helpers/ThemeManager.h"

AdvertisingComponent::AdvertisingComponent(MusicLibrary& lib, AdvertisingManager& ads)
    : adsList(lib, ads),
      scheduleView(lib, ads),
      resizerBar(&layout, 1, true)
{
    addAndMakeVisible(adsList);
    addAndMakeVisible(scheduleView);
    addAndMakeVisible(resizerBar);

     
     
     
     
    layout.setItemLayout(0, -0.3, -0.7, -0.5);
    layout.setItemLayout(1, 16, 16, 16);
    layout.setItemLayout(2, -0.3, -0.7, -0.5);
    
     
    adsList.onSelectionChanged = [this](int number) {
        scheduleView.setSelectedCueNumber(number);
    };
}

AdvertisingComponent::~AdvertisingComponent()
{
}

void AdvertisingComponent::refresh()
{
    adsList.refreshList();
}

// Al abrir la pestaña se recarga la lista: puede haber cuñas importadas desde la última vez.
void AdvertisingComponent::visibilityChanged()
{
    if (isVisible())
        adsList.refreshList();
}

void AdvertisingComponent::setDecks(Deck* dA, Deck* dB)
{
    adsList.setDecks(dA, dB);
    scheduleView.setDecks(dA, dB);
}

void AdvertisingComponent::setPlaylist(Playlist* p)
{
    adsList.setPlaylist(p);
    scheduleView.setPlaylist(p);
}

void AdvertisingComponent::paint(juce::Graphics&)
{
}

void AdvertisingComponent::resized()
{
    auto area = getLocalBounds();
    
     
    juce::Component* comps[] = { &scheduleView, &resizerBar, &adsList };
    
    layout.layOutComponents(comps, 3, 
                            area.getX(), area.getY(), area.getWidth(), area.getHeight(), 
                            false, true);  
}
