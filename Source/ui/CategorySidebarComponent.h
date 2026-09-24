#pragma once
#include <JuceHeader.h>
#include "../library/MusicLibrary.h"

class CategorySidebarComponent : public juce::Component, 
                                 public juce::ListBoxModel,
                                 public juce::DragAndDropContainer,
                                 public juce::DragAndDropTarget {
public:
    CategorySidebarComponent(MusicLibrary& lib);
    ~CategorySidebarComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    
     
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int rowNumber, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked(int rowNumber, const juce::MouseEvent& e) override;
    void deleteKeyPressed(int lastRowSelected) override;
    void returnKeyPressed(int lastRowSelected) override;
    juce::String getTooltipForRow(int row) override;
    
     
    juce::var getDragSourceDescription(const juce::SparseSet<int>& selectedRows) override;

     
    bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;
    void itemDragMove(const SourceDetails& dragSourceDetails) override;  
    void itemDragExit(const SourceDetails& dragSourceDetails) override;  
    void itemDropped(const SourceDetails& dragSourceDetails) override;
    
    void paintOverChildren(juce::Graphics& g) override;  

    std::function<void(const juce::String&)> onCategorySelected;

    void refreshCategories();
    void setSelectedCategory(const juce::String& categoryName);

private:
    MusicLibrary& library;
    juce::ListBox categoryList;
    
    juce::TextButton addCategoryButton{"+"};
    juce::TextEditor newCategoryName;       
    juce::TextEditor categorySearchBox;     
    
    std::vector<Category> allCategories;
    std::vector<Category> displayCategories;
    int numSystemCategories = 0;       // van primero en allCategories
    juce::String selectedCategory;     // nombre guardado, "All Categories" o "Uncategorized"
    int insertionIndex = -1;

    // Fila 0 = todas, 1 = sin categoría, 2... = displayCategories; nullptr en las dos primeras.
    const Category* categoryAt(int row) const;
    // Fila donde se suelta una categoría arrastrada: nunca por encima de las del sistema.
    int dropRowFor(const SourceDetails& dragSourceDetails);
    void selectCategory(const juce::String& name);

    void createNewCategory();
    void renameCategory(const juce::String& oldName);
    void confirmDeleteCategory(const juce::String& name);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CategorySidebarComponent)
};
