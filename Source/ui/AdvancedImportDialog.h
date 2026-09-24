 

#pragma once

#include <JuceHeader.h>
#include "../library/MusicLibrary.h"

class AdvancedImportDialog : public juce::Component {
public:
    AdvancedImportDialog(MusicLibrary& lib);
    ~AdvancedImportDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    MusicLibrary& library;
    
    juce::Label categoryLabel;
    juce::ComboBox categoryCombo;
    juce::StringArray categoryNames;  // nombre guardado de cada opción (id del combo - 1); "" = GENERAL

    juce::TextButton importFileButton;
    juce::TextButton importFolderButton;
    juce::Rectangle<int> hintArea;
    
    std::unique_ptr<juce::FileChooser> fileChooser;
    
    void importFiles();
    void importFolder();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdvancedImportDialog)
};
