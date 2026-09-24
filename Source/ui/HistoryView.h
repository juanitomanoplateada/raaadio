 

#pragma once

#include <JuceHeader.h>
#include "../library/HistoryManager.h"

class HistoryView : public juce::Component, 
                    public juce::TableListBoxModel,
                    public juce::ChangeListener
{
public:
    HistoryView(HistoryManager& historyManager);
    ~HistoryView();

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    
     
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

     
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;

private:
    HistoryManager& historyManager;
    juce::TableListBox table;
    std::vector<HistoryEntry> cachedHistory;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HistoryView)
};
