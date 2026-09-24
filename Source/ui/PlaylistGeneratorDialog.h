#pragma once
#include <JuceHeader.h>
#include "../generator/GeneratorSettings.h"
#include "../generator/TemplateManager.h"
#include "../generator/PlaylistGenerator.h"
#include "../library/MusicLibrary.h"
#include "../library/Playlist.h"
#include "../helpers/ThemeManager.h"

class PlaylistGeneratorDialog : public juce::Component, 
                                public juce::Button::Listener,
                                private juce::ListBoxModel {
public:
    PlaylistGeneratorDialog(MusicLibrary& lib, Playlist& playlist);
    ~PlaylistGeneratorDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    
     
    std::function<void()> onCancel;
    std::function<void()> onGenerateSuccess;
    
    void buttonClicked(juce::Button* b) override;

     
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int rowNumber, const juce::MouseEvent& e) override;
    
private:
    MusicLibrary& library;
    Playlist& playlist;
    
    TemplateManager templateManager;
    GeneratorSettings currentSettings;
    
     
    void loadData();
    void syncSettingsFromUI();
    void syncUIFromSettings();
    void generate();
    void saveTemplate();
    void loadTemplate();
    void deleteTemplate();  
    void refreshTemplateList();
    
     
    juce::GroupComponent configGroup{"config", "Configuration"};
    
    juce::Label catsLabel{"cats", "Categories (Click to Includes / Right Click to Exclude)"};
    juce::ListBox categoryList;
    std::vector<Category> allCategories;
    
     
    juce::Label rulesLabel{"rules", "Separation Rules"};
    juce::Label artistSepLabel{"artist", "Artist Sep (items):"};
    juce::TextEditor artistSepEditor;
    
    juce::Label titleSepLabel{"title", "Title Sep (items):"};
    juce::TextEditor titleSepEditor;
    
    juce::Label catSepLabel{"cat", "Category Sep (items):"};
    juce::TextEditor catSepEditor;
    
    juce::Label lastPlayLabel{"lp", "Last Play Sep (min):"};
    juce::TextEditor lastPlayEditor;
    
     
    juce::Label durLabel{"dur", "Target Duration:"};
    juce::TextEditor durEditor;
    juce::ComboBox durUnitCombo; 
    
     
    juce::Label sepGroupLabel{"sep", "Separators / IDs"};
    juce::ComboBox sepCatCombo;
    juce::Label sepIntLabel{"sepInt", "Interval (items):"};
    juce::TextEditor sepIntEditor;
    
     
    juce::GroupComponent templateGroup{"temp", "Templates"};
    
    juce::ListBox templateList;  
    class TemplateListModel : public juce::ListBoxModel {
    public:
        juce::StringArray templates;
        std::function<void(const juce::String&)> onSelect;
        int getNumRows() override { return templates.size(); }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool sel) override {
            auto& t = ThemeManager::getInstance();
            if (sel) {
                g.fillAll(t.getSelectionColour());
                g.setColour(t.getHighlightColour());
                g.fillRect(0, 0, 3, h);
            }
            g.setColour(t.getPrimaryTextColour());
            g.setFont(juce::Font(juce::FontOptions(14.0f)));
            g.drawText(templates[row], 10, 0, w - 14, h, juce::Justification::centredLeft, true);
        }
        void listBoxItemClicked(int row, const juce::MouseEvent&) override {
            if(onSelect && row < templates.size()) onSelect(templates[row]);
        }
    };
    TemplateListModel templateModel;
    juce::ListBox templatesDisplay;
    
    juce::TextEditor templateNameEditor;
    juce::TextButton saveTemplateButton{"Save Template"};
    juce::TextButton loadTemplateButton{"Load Selected"};
    juce::TextButton deleteTemplateButton{"Delete Selected"};  
    
     
    juce::Label insertModeLabel{"ins", "Insertion Mode:"};
    juce::ComboBox insertModeCombo;
    
    juce::TextButton generateButton{"GENERATE PLAYLIST"};
    juce::TextButton cancelButton{"Cancel"};
    
    juce::Label statusLabel; 
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaylistGeneratorDialog)
};
