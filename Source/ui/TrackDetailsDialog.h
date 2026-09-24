#pragma once
#include <JuceHeader.h>
#include "../generator/PlaylistGenerator.h"
#include "../library/MusicLibrary.h"
#include "../library/Playlist.h"
#include "../helpers/ThemeManager.h"

class TrackDetailsDialog : public juce::Component {
public:
    TrackDetailsDialog(MusicLibrary& lib, const TrackInfo& track);
    ~TrackDetailsDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    
    std::function<void()> onSave;
    std::function<void()> onCancel;
    
    bool isSaving = false;  

private:
    MusicLibrary& library;
    TrackInfo trackInfo;
    
     
    juce::Label infoSectionHeader;
    juce::Label detailsSectionHeader;
    juce::Label systemSectionHeader;

     
    juce::Label artistLabel;
    juce::TextEditor artistEditor;
    
    juce::Label titleLabel;
    juce::TextEditor titleEditor;
    
     
    juce::Label durationLabel;
    juce::Label playCountLabel;
    juce::Label lastPlayedLabel;
    juce::Label createdAtLabel;
    juce::Label updatedAtLabel;
    juce::Label pathLabel;
    
     
    juce::Label categoryFilterLabel;
    juce::TextEditor categorySearch;
    
     
    class CategoryCheckList : public juce::ListBox, public juce::ListBoxModel {
    public:
        CategoryCheckList(const std::vector<Category>& cats, std::vector<int>& selected)
            : allCats(cats), selectedIds(selected) {
            setModel(this);
            setRowHeight(28);
        }
        
        void filter(const juce::String& text) {
            filteredCats.clear();
            for (const auto& c : allCats) {
                if (text.isEmpty() || MusicLibrary::getDisplayName(c).containsIgnoreCase(text))
                    filteredCats.push_back(c);
            }
            // Las categorías que ya tiene la pista van primero.
            std::stable_partition(filteredCats.begin(), filteredCats.end(), [this](const Category& c) {
                return std::find(selectedIds.begin(), selectedIds.end(), c.id) != selectedIds.end();
            });
            updateContent();
            repaint();
        }
        
        int getNumRows() override { return (int)filteredCats.size(); }
        
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override {
            if (rowNumber >= filteredCats.size()) return;
            const auto& cat = filteredCats[rowNumber];
            
            const bool isSelected = std::find(selectedIds.begin(), selectedIds.end(), cat.id) != selectedIds.end();
            if (isSelected || rowIsSelected)
                g.fillAll(ThemeManager::getInstance().getSelectionColour().withMultipliedAlpha(isSelected ? 0.6f : 1.0f));

            getLookAndFeel().drawTickBox(g, *this, 8.0f, (float) (height - 18) * 0.5f, 18.0f, 18.0f,
                                         isSelected, true, false, false);

            g.setColour(ThemeManager::getInstance().getPrimaryTextColour());
            g.setFont(juce::Font(juce::FontOptions(14.0f)));
            g.drawText(MusicLibrary::getDisplayName(cat), 36, 0, width - 40, height, juce::Justification::centredLeft, true);
        }
        
        void listBoxItemClicked(int rowNumber, const juce::MouseEvent& e) override {
            if (rowNumber >= filteredCats.size()) return;
            int catId = filteredCats[rowNumber].id;
            
            auto it = std::find(selectedIds.begin(), selectedIds.end(), catId);
            if (it != selectedIds.end()) {
                selectedIds.erase(it);
            } else {
                selectedIds.push_back(catId);
            }
            repaint();
        }

    private:
        std::vector<Category> allCats;
        std::vector<Category> filteredCats;
        std::vector<int>& selectedIds;
    };
    
    std::unique_ptr<CategoryCheckList> categoryList;
    std::vector<Category> allCategories;
    std::vector<int> selectedCategoryIds;

     
    juce::TextButton playLocalButton;
    juce::TextButton refreshButton; 
    juce::TextButton deleteButton; 
    juce::TextButton saveButton;
    juce::TextButton cancelButton;
    
    void loadData();
    void saveData();
    void playLocal();
    void refreshMetadata();  
    void deleteTrack();  

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackDetailsDialog)
};
