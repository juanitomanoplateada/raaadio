 

#include <JuceHeader.h>
#include "AdvancedImportDialog.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

AdvancedImportDialog::AdvancedImportDialog(MusicLibrary& lib) : library(lib) {
    addAndMakeVisible(categoryLabel);
    categoryLabel.setText(TRANS("label_select_category"), juce::dontSendNotification);
    
    addAndMakeVisible(categoryCombo);

    // GENERAL (sin categoría), las categorías de la emisora en su orden y las demás por nombre.
    auto addOption = [this](const juce::String& text, const juce::String& storedName) {
        categoryNames.add(storedName);
        categoryCombo.addItem(text, categoryNames.size());
    };
    addOption("GENERAL", {});

    juce::StringArray userNames;
    categoryCombo.addSeparator();
    for (const auto& cat : library.getCategories()) {
        if (cat.isSystem()) addOption(MusicLibrary::getDisplayName(cat), cat.name);
        else if (cat.name != "GENERAL") userNames.add(cat.name);
    }
    userNames.sort(true);
    if (!userNames.isEmpty()) categoryCombo.addSeparator();
    for (const auto& name : userNames) addOption(name, name);

    categoryCombo.setSelectedId(1);
    
    addAndMakeVisible(importFileButton);
    UiStyle::setVariant(importFileButton, "primary");
    UiStyle::setIcon(importFileButton, "music");
    importFileButton.onClick = [this] { importFiles(); };

    addAndMakeVisible(importFolderButton);
    UiStyle::setVariant(importFolderButton, "primary");
    UiStyle::setIcon(importFolderButton, "folder");
    importFolderButton.onClick = [this] { importFolder(); };

    lookAndFeelChanged();
}

AdvancedImportDialog::~AdvancedImportDialog() {}

void AdvancedImportDialog::paint(juce::Graphics& g) {
    auto& t = ThemeManager::getInstance();
    g.fillAll(t.getBaseBackgroundColour());

    g.setColour(t.getMutedTextColour());
    g.setFont(UiStyle::ui(13.0f));
    g.drawFittedText(TRANS("import_hint"), hintArea, juce::Justification::centredLeft, 2);
}

void AdvancedImportDialog::resized() {
    auto area = getLocalBounds().reduced(24, 20);

    categoryLabel.setBounds(area.removeFromTop(20));
    area.removeFromTop(6);
    categoryCombo.setBounds(area.removeFromTop(UiStyle::controlHeight + 4));
    area.removeFromTop(8);
    hintArea = area.removeFromTop(36);

    auto buttons = area.removeFromBottom(40);
    importFileButton.setBounds(buttons.removeFromLeft((buttons.getWidth() - 10) / 2));
    buttons.removeFromLeft(10);
    importFolderButton.setBounds(buttons);
}

void AdvancedImportDialog::lookAndFeelChanged() {
    categoryLabel.setText(TRANS("label_select_category"), juce::dontSendNotification);
    categoryLabel.setFont(UiStyle::caption(12.0f));
    categoryLabel.setColour(juce::Label::textColourId, ThemeManager::getInstance().getSecondaryTextColour());
    categoryLabel.setBorderSize({ 0, 0, 0, 0 });
    importFileButton.setButtonText(TRANS("label_files"));
    importFolderButton.setButtonText(TRANS("label_folder"));
    repaint();
}

void AdvancedImportDialog::importFiles() {
    const auto cat = categoryNames[categoryCombo.getSelectedId() - 1];

    fileChooser.reset(new juce::FileChooser(TRANS("select_files"), juce::File::getSpecialLocation(juce::File::userHomeDirectory), library.getSupportedFileWildcard()));
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectMultipleItems,
        [this, cat](const juce::FileChooser& fc) {
            auto files = fc.getResults();
            if (!files.isEmpty()) {
                library.importFiles(files, cat);
                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
                    dw->exitModalState(0);
                }
            }
        });
}

void AdvancedImportDialog::importFolder() {
    const auto cat = categoryNames[categoryCombo.getSelectedId() - 1];

    fileChooser.reset(new juce::FileChooser(TRANS("select_folder"), juce::File::getSpecialLocation(juce::File::userHomeDirectory)));
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this, cat](const juce::FileChooser& fc) {
            auto res = fc.getResult();
            if (res.exists()) {
                library.importFiles({res}, cat);
                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) {
                    dw->exitModalState(0);
                }
            }
        });
}
