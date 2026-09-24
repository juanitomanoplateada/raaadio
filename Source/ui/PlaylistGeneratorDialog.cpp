#include "PlaylistGeneratorDialog.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

PlaylistGeneratorDialog::PlaylistGeneratorDialog(MusicLibrary& lib, Playlist& pl)
    : library(lib), playlist(pl)
{
    categoryList.setRowHeight(UiStyle::rowHeight);
    templatesDisplay.setRowHeight(UiStyle::rowHeight);
    categoryList.setColour(juce::ListBox::backgroundColourId, ThemeManager::getInstance().getRaisedColour().withAlpha(0.5f));
    templatesDisplay.setColour(juce::ListBox::backgroundColourId, ThemeManager::getInstance().getRaisedColour().withAlpha(0.5f));
     
    
     
    configGroup.setText(TRANS("gen_config_group"));
    addAndMakeVisible(configGroup);
    
    catsLabel.setText(TRANS("gen_cats_label"), juce::dontSendNotification);
    addAndMakeVisible(catsLabel);
    
    addAndMakeVisible(categoryList);
    categoryList.setModel(this);
    categoryList.setMultipleSelectionEnabled(false);
    
     
    rulesLabel.setText(TRANS("gen_rules_label"), juce::dontSendNotification);
    addAndMakeVisible(rulesLabel);
    
    artistSepLabel.setText(TRANS("gen_artist_sep"), juce::dontSendNotification);
    addAndMakeVisible(artistSepLabel);
    addAndMakeVisible(artistSepEditor);
    artistSepEditor.setInputRestrictions(3, "0123456789");
    artistSepEditor.setTooltip(TRANS("gen_artist_sep"));  
    
    titleSepLabel.setText(TRANS("gen_title_sep"), juce::dontSendNotification);
    addAndMakeVisible(titleSepLabel);
    addAndMakeVisible(titleSepEditor);
    titleSepEditor.setInputRestrictions(3, "0123456789");
    titleSepEditor.setTooltip(TRANS("gen_title_sep"));
    
    catSepLabel.setText(TRANS("gen_cat_sep"), juce::dontSendNotification);
    addAndMakeVisible(catSepLabel);
    addAndMakeVisible(catSepEditor);
    catSepEditor.setInputRestrictions(3, "0123456789");
    catSepEditor.setTooltip(TRANS("gen_cat_sep"));
    
    lastPlayLabel.setText(TRANS("gen_last_play_sep"), juce::dontSendNotification);
    addAndMakeVisible(lastPlayLabel);
    addAndMakeVisible(lastPlayEditor);
    lastPlayEditor.setInputRestrictions(4, "0123456789");
    lastPlayEditor.setTooltip(TRANS("gen_last_play_sep"));
    
     
    durLabel.setText(TRANS("gen_target_dur"), juce::dontSendNotification);
    addAndMakeVisible(durLabel);
    addAndMakeVisible(durEditor);
    durEditor.setInputRestrictions(4, "0123456789");
    
    addAndMakeVisible(durUnitCombo);
    durUnitCombo.addItem(TRANS("gen_unit_min"), 1);
    durUnitCombo.addItem(TRANS("gen_unit_hours"), 2);
    durUnitCombo.setSelectedId(1);
    
     
    sepGroupLabel.setText(TRANS("gen_sep_group"), juce::dontSendNotification);
    addAndMakeVisible(sepGroupLabel);
    addAndMakeVisible(sepCatCombo);
    
    sepIntLabel.setText(TRANS("gen_sep_interval"), juce::dontSendNotification);
    addAndMakeVisible(sepIntLabel);
    addAndMakeVisible(sepIntEditor);
    sepIntEditor.setInputRestrictions(3, "0123456789");
    
     
    templateGroup.setText(TRANS("gen_templates_group"));
    addAndMakeVisible(templateGroup);
    
    addAndMakeVisible(templatesDisplay);
    templatesDisplay.setModel(&templateModel);
    templateModel.onSelect = [this](const juce::String& name) {
        templateNameEditor.setText(name);
    };
    
    addAndMakeVisible(templateNameEditor);
    templateNameEditor.setText(TRANS("new_template_placeholder"));
    
    saveTemplateButton.setButtonText(TRANS("gen_btn_save"));
    addAndMakeVisible(saveTemplateButton);
    saveTemplateButton.addListener(this);
    
    loadTemplateButton.setButtonText(TRANS("gen_btn_load"));
    addAndMakeVisible(loadTemplateButton);
    loadTemplateButton.addListener(this);
    
    deleteTemplateButton.setButtonText(TRANS("gen_btn_delete"));
    UiStyle::setVariant(deleteTemplateButton, "ghost");
    deleteTemplateButton.getProperties().set("textColour", ThemeManager::getInstance().getErrorColour().toString());
    addAndMakeVisible(deleteTemplateButton);
    deleteTemplateButton.addListener(this);
    
     
    insertModeLabel.setText(TRANS("gen_insertion_mode"), juce::dontSendNotification);
    addAndMakeVisible(insertModeLabel);
    addAndMakeVisible(insertModeCombo);
    insertModeCombo.addItem(TRANS("gen_mode_end"), 1);
    insertModeCombo.addItem(TRANS("gen_mode_start"), 2);
    insertModeCombo.addItem(TRANS("gen_mode_replace"), 3);
    insertModeCombo.setSelectedId(1);
    
    generateButton.setButtonText(TRANS("gen_btn_generate"));
    addAndMakeVisible(generateButton);
    UiStyle::setVariant(generateButton, "primary");
    UiStyle::setIcon(generateButton, "sparkle");
    generateButton.addListener(this);
    
    cancelButton.setButtonText(TRANS("cancel"));
    addAndMakeVisible(cancelButton);
    cancelButton.addListener(this);
    
    addAndMakeVisible(statusLabel);
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setFont(UiStyle::ui(13.5f));

    // Rótulos de sección en mayúsculas pequeñas; el resto, texto secundario.
    for (auto* l : { &catsLabel, &rulesLabel, &sepGroupLabel }) {
        l->setFont(UiStyle::caption(11.5f));
        l->setColour(juce::Label::textColourId, ThemeManager::getInstance().getSecondaryTextColour());
        l->setBorderSize({ 0, 0, 0, 0 });
    }
    for (auto* l : { &artistSepLabel, &titleSepLabel, &catSepLabel, &lastPlayLabel, &durLabel, &sepIntLabel, &insertModeLabel }) {
        l->setFont(UiStyle::ui(14.0f));
        l->setColour(juce::Label::textColourId, ThemeManager::getInstance().getSecondaryTextColour());
    }
    for (auto* e : { &artistSepEditor, &titleSepEditor, &catSepEditor, &lastPlayEditor, &durEditor, &sepIntEditor }) {
        e->setJustification(juce::Justification::centred);
        e->setFont(UiStyle::mono(14.0f, true));
    }
    templateNameEditor.setIndents(10, 0);
    templateNameEditor.setJustification(juce::Justification::centredLeft);

    loadData();
    refreshTemplateList();
    syncUIFromSettings();

    setSize(900, 620);
}

PlaylistGeneratorDialog::~PlaylistGeneratorDialog() {}

void PlaylistGeneratorDialog::loadData() {
    allCategories = library.getCategories();
    categoryList.updateContent();
    
    sepCatCombo.clear();
    sepCatCombo.addItem(TRANS("gen_disabled"), 9999); 
    
    for (const auto& c : allCategories) {
         
         
        if (c.id > 0) sepCatCombo.addItem(MusicLibrary::getDisplayName(c), c.id);
    }
    
    sepCatCombo.setSelectedId(9999);
}

void PlaylistGeneratorDialog::refreshTemplateList() {
    templateModel.templates = templateManager.getTemplateNames();
    templatesDisplay.updateContent();
}

void PlaylistGeneratorDialog::syncSettingsFromUI() {
     
    
    currentSettings.artistSeparation = artistSepEditor.getText().getIntValue();
    currentSettings.titleSeparation = titleSepEditor.getText().getIntValue();
    currentSettings.categorySeparation = catSepEditor.getText().getIntValue();
    currentSettings.lastPlayedSeparation = lastPlayEditor.getText().getIntValue();
    
    currentSettings.targetDurationVal = durEditor.getText().getIntValue();
    currentSettings.targetDurationIsHours = (durUnitCombo.getSelectedId() == 2);
    
    int sepId = sepCatCombo.getSelectedId();
    currentSettings.separatorCategoryId = (sepId == 9999) ? -1 : sepId;
    currentSettings.separatorInterval = sepIntEditor.getText().getIntValue();
    
    int modeId = insertModeCombo.getSelectedId();
    if(modeId == 1) currentSettings.insertionMode = InsertionMode::AddToEnd;
    else if(modeId == 2) currentSettings.insertionMode = InsertionMode::AddToStart;
    else currentSettings.insertionMode = InsertionMode::Replace;
}

void PlaylistGeneratorDialog::syncUIFromSettings() {
     
    categoryList.repaint();
    
    artistSepEditor.setText(juce::String(currentSettings.artistSeparation));
    titleSepEditor.setText(juce::String(currentSettings.titleSeparation));
    catSepEditor.setText(juce::String(currentSettings.categorySeparation));
    lastPlayEditor.setText(juce::String(currentSettings.lastPlayedSeparation));
    
    durEditor.setText(juce::String(currentSettings.targetDurationVal));
    durUnitCombo.setSelectedId(currentSettings.targetDurationIsHours ? 2 : 1);
    
    if (currentSettings.separatorCategoryId == -1) sepCatCombo.setSelectedId(9999);
    else sepCatCombo.setSelectedId(currentSettings.separatorCategoryId);
    
    sepIntEditor.setText(juce::String(currentSettings.separatorInterval));
    
    if (currentSettings.insertionMode == InsertionMode::AddToEnd) insertModeCombo.setSelectedId(1);
    else if (currentSettings.insertionMode == InsertionMode::AddToStart) insertModeCombo.setSelectedId(2);
    else insertModeCombo.setSelectedId(3);
}

void PlaylistGeneratorDialog::buttonClicked(juce::Button* b) {
    if (b == &cancelButton) {
        if (onCancel) onCancel();
    }
    else if (b == &generateButton) {
        generate();
    }
    else if (b == &saveTemplateButton) {
        saveTemplate();
    }
    else if (b == &loadTemplateButton) {
        loadTemplate();
    }
    else if (b == &deleteTemplateButton) {
        deleteTemplate();
    }
}

void PlaylistGeneratorDialog::generate() {
    syncSettingsFromUI();
    
    statusLabel.setText(TRANS("gen_generating"), juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, ThemeManager::getInstance().getAccentColour());
    
     
     
     
    
    PlaylistGenerator generator(library);
    auto result = generator.generate(currentSettings, playlist);
    
    if (result.success) {
        statusLabel.setText(result.message, juce::dontSendNotification);  
         
         
         
        statusLabel.setText(TRANS("gen_success") + ": " + juce::String(result.tracksGenerated), juce::dontSendNotification);
        
        statusLabel.setColour(juce::Label::textColourId, ThemeManager::getInstance().getAutoDjOnColour());

        if (onGenerateSuccess) onGenerateSuccess();
    } else {
        statusLabel.setText(TRANS("gen_error") + ": " + result.message, juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, ThemeManager::getInstance().getErrorColour());
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, TRANS("gen_error"), result.message);
    }
}

void PlaylistGeneratorDialog::saveTemplate() {
    syncSettingsFromUI();
    juce::String name = templateNameEditor.getText().trim();
    if (name.isEmpty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, TRANS("gen_btn_save"), TRANS("gen_save_warning"));
        return;
    }
    
    templateManager.saveTemplate(name, currentSettings);
    refreshTemplateList();
    statusLabel.setText(TRANS("gen_template_saved") + " " + name, juce::dontSendNotification);
}

void PlaylistGeneratorDialog::loadTemplate() {
    int row = templatesDisplay.getSelectedRow();
    if (row < 0 || row >= templateModel.templates.size()) return;
    
    juce::String name = templateModel.templates[row];
    currentSettings = templateManager.loadTemplate(name);
    syncUIFromSettings();
    templateNameEditor.setText(name);
    templateNameEditor.setText(name);
    statusLabel.setText(TRANS("gen_template_loaded") + " " + name, juce::dontSendNotification);
}

void PlaylistGeneratorDialog::deleteTemplate() {
    int row = templatesDisplay.getSelectedRow();
    if (row < 0 || row >= templateModel.templates.size()) return;
    
    juce::String name = templateModel.templates[row];
    
    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon, TRANS("gen_btn_delete"), 
        TRANS("delete_template_confirm") + " '" + name + "'?",
        TRANS("ok"), TRANS("cancel"),
        nullptr,
        juce::ModalCallbackFunction::create([this, name](int result) {
            if (result != 0) {  
                templateManager.deleteTemplate(name);
                refreshTemplateList();
                statusLabel.setText(TRANS("gen_template_deleted") + " " + name, juce::dontSendNotification);
                templateNameEditor.setText("");
            }
        }));
}

 
int PlaylistGeneratorDialog::getNumRows() { return allCategories.size(); }

void PlaylistGeneratorDialog::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool sel) {
    if (row >= allCategories.size()) return;
    
    const auto& cat = allCategories[row];
    bool isInc = false;
    for(int id : currentSettings.includedCategories) if(id == cat.id) isInc = true;
    
    bool isExc = false; 
    for(int id : currentSettings.excludedCategories) if(id == cat.id) isExc = true;
    
    auto& t = ThemeManager::getInstance();
    auto rowArea = juce::Rectangle<float>((float) w, (float) h);

    // Incluida (clic) en verde, excluida (clic derecho) en rojo, con su etiqueta a la derecha.
    juce::Colour stateColour;
    juce::String stateText;
    if (isInc) { stateColour = t.getSuccessColour(); stateText = TRANS("gen_state_included"); }
    else if (isExc) { stateColour = t.getErrorColour(); stateText = TRANS("gen_state_excluded"); }

    if (!stateColour.isTransparent()) {
        g.setColour(stateColour.withAlpha(0.12f));
        g.fillRect(rowArea);
        g.setColour(stateColour);
        g.fillRect(rowArea.withWidth(3.0f));

        const float chipH = 18.0f;
        const float chipW = UiStyle::getChipWidth(stateText, chipH);
        UiStyle::drawChip(g, rowArea.removeFromRight(chipW + 10.0f).withSizeKeepingCentre(chipW, chipH),
                          stateText, stateColour.withAlpha(0.22f), stateColour.interpolatedWith(t.getPrimaryTextColour(), 0.15f));
    }

    g.setColour(isExc ? t.getSecondaryTextColour() : t.getPrimaryTextColour());
    g.setFont(isInc ? UiStyle::semibold(14.0f) : UiStyle::ui(14.0f));
    g.drawText(MusicLibrary::getDisplayName(cat), rowArea.withTrimmedLeft(12.0f), juce::Justification::centredLeft, true);
}

void PlaylistGeneratorDialog::listBoxItemClicked(int row, const juce::MouseEvent& e) {
    if (row >= allCategories.size()) return;
    int catId = allCategories[row].id;
    
    if (e.mods.isLeftButtonDown()) {
         
         
        auto& inc = currentSettings.includedCategories;
        auto& exc = currentSettings.excludedCategories;
        
         
        exc.erase(std::remove(exc.begin(), exc.end(), catId), exc.end());
        
         
        auto it = std::find(inc.begin(), inc.end(), catId);
        if (it != inc.end()) inc.erase(it);
        else inc.push_back(catId);
    }
    else if (e.mods.isRightButtonDown()) {
         
        auto& inc = currentSettings.includedCategories;
        auto& exc = currentSettings.excludedCategories;
        
         
        inc.erase(std::remove(inc.begin(), inc.end(), catId), inc.end());
        
        auto it = std::find(exc.begin(), exc.end(), catId);
        if (it != exc.end()) exc.erase(it);
        else exc.push_back(catId);
    }
    
    categoryList.repaint();
}

void PlaylistGeneratorDialog::paint(juce::Graphics& g) {
    g.fillAll(ThemeManager::getInstance().getBaseBackgroundColour());  
}

void PlaylistGeneratorDialog::resized() {
    auto area = getLocalBounds().reduced(16);

    // Pie: modo de inserción, estado y acciones
    auto bottomArea = area.removeFromBottom(38);
    area.removeFromBottom(14);
    insertModeLabel.setBounds(bottomArea.removeFromLeft(140));
    insertModeCombo.setBounds(bottomArea.removeFromLeft(190).withSizeKeepingCentre(190, 34));
    bottomArea.removeFromLeft(12);
    cancelButton.setBounds(bottomArea.removeFromRight(110));
    bottomArea.removeFromRight(8);
    generateButton.setBounds(bottomArea.removeFromRight(juce::jmax(190, generateButton.getBestWidthForHeight(38) + 30)));
    bottomArea.removeFromRight(8);
    statusLabel.setBounds(bottomArea);

    // Plantillas a la derecha
    auto rightArea = area.removeFromRight(230);
    area.removeFromRight(14);
    templateGroup.setBounds(rightArea);
    auto tempContent = rightArea.withTrimmedTop(22).reduced(12);

    deleteTemplateButton.setBounds(tempContent.removeFromBottom(32));
    tempContent.removeFromBottom(6);
    auto templateButtons = tempContent.removeFromBottom(32);
    loadTemplateButton.setBounds(templateButtons.removeFromLeft((templateButtons.getWidth() - 6) / 2));
    templateButtons.removeFromLeft(6);
    saveTemplateButton.setBounds(templateButtons);
    tempContent.removeFromBottom(6);
    templateNameEditor.setBounds(tempContent.removeFromBottom(UiStyle::controlHeight));
    tempContent.removeFromBottom(10);
    templatesDisplay.setBounds(tempContent);

    // Configuración: categorías a la izquierda, reglas a la derecha
    configGroup.setBounds(area);
    auto c = area.withTrimmedTop(22).reduced(12);

    auto catArea = c.removeFromLeft((int) (c.getWidth() * 0.46f));
    catsLabel.setBounds(catArea.removeFromTop(20));
    catArea.removeFromTop(6);
    categoryList.setBounds(catArea);

    c.removeFromLeft(16);

    const int lh = 32;
    const int labelWidth = c.getWidth() - 86;
    auto ruleRow = [&](juce::Label& label, juce::Component& field) {
        auto r = c.removeFromTop(lh);
        label.setBounds(r.removeFromLeft(labelWidth));
        field.setBounds(r.removeFromRight(80));
        c.removeFromTop(6);
    };

    rulesLabel.setBounds(c.removeFromTop(20));
    c.removeFromTop(6);
    ruleRow(artistSepLabel, artistSepEditor);
    ruleRow(titleSepLabel, titleSepEditor);
    ruleRow(catSepLabel, catSepEditor);
    ruleRow(lastPlayLabel, lastPlayEditor);

    c.removeFromTop(10);
    auto r5 = c.removeFromTop(lh);
    durLabel.setBounds(r5.removeFromLeft(r5.getWidth() - 186));
    durUnitCombo.setBounds(r5.removeFromRight(100));
    r5.removeFromRight(6);
    durEditor.setBounds(r5);

    c.removeFromTop(18);
    sepGroupLabel.setBounds(c.removeFromTop(20));
    c.removeFromTop(6);
    sepCatCombo.setBounds(c.removeFromTop(lh));
    c.removeFromTop(6);
    ruleRow(sepIntLabel, sepIntEditor);
}
