#include "TrackDetailsDialog.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

TrackDetailsDialog::TrackDetailsDialog(MusicLibrary& lib, const TrackInfo& t) 
    : library(lib), trackInfo(t) 
{
     
    addAndMakeVisible(infoSectionHeader);
    addAndMakeVisible(detailsSectionHeader);
    addAndMakeVisible(systemSectionHeader);

     
    addAndMakeVisible(artistLabel);
    addAndMakeVisible(artistEditor);
    artistEditor.setText(trackInfo.artist);
    
    addAndMakeVisible(titleLabel);
    addAndMakeVisible(titleEditor);
    titleEditor.setText(trackInfo.title);
    
     
    addAndMakeVisible(durationLabel);
    addAndMakeVisible(playCountLabel);
    addAndMakeVisible(lastPlayedLabel);
    addAndMakeVisible(createdAtLabel);
    addAndMakeVisible(updatedAtLabel);
    
    addAndMakeVisible(pathLabel);
    pathLabel.setFont(juce::Font(12.0f));
    pathLabel.setColour(juce::Label::textColourId, ThemeManager::getInstance().getSecondaryTextColour());
    
     
    allCategories = library.getCategories();
    selectedCategoryIds = library.getTrackCategories(trackInfo.id);
    
    categoryList.reset(new CategoryCheckList(allCategories, selectedCategoryIds));
    addAndMakeVisible(categoryList.get());
    categoryList->filter("");  
    
    addAndMakeVisible(categoryFilterLabel);
    addAndMakeVisible(categorySearch);
    categorySearch.onTextChange = [this] { categoryList->filter(categorySearch.getText()); };
     
    
     
    addAndMakeVisible(playLocalButton);
    addAndMakeVisible(refreshButton);
    addAndMakeVisible(deleteButton);
    deleteButton.setColour(juce::TextButton::buttonColourId, ThemeManager::getInstance().getErrorColour());  
    
    addAndMakeVisible(saveButton);
    addAndMakeVisible(cancelButton);
    
    playLocalButton.onClick = [this] { playLocal(); };
    refreshButton.onClick = [this] { refreshMetadata(); };
    deleteButton.onClick = [this] { deleteTrack(); };
    saveButton.onClick = [this] { saveData(); };
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };

    setSize(520, 640);
    lookAndFeelChanged();
}

void TrackDetailsDialog::lookAndFeelChanged() {
    auto& theme = ThemeManager::getInstance();
    const auto secondaryCol = theme.getSecondaryTextColour();
    const auto mutedCol = theme.getMutedTextColour();

    infoSectionHeader.setText(TRANS("header_info"), juce::dontSendNotification);
    detailsSectionHeader.setText(TRANS("header_details"), juce::dontSendNotification);
    systemSectionHeader.setText(TRANS("header_system"), juce::dontSendNotification);
    categoryFilterLabel.setText(TRANS("label_filter_categories"), juce::dontSendNotification);
    artistLabel.setText(TRANS("label_artist"), juce::dontSendNotification);
    titleLabel.setText(TRANS("label_title"), juce::dontSendNotification);

    for (auto* l : { &infoSectionHeader, &detailsSectionHeader, &systemSectionHeader, &categoryFilterLabel }) {
        l->setFont(UiStyle::caption(12.0f));
        l->setColour(juce::Label::textColourId, secondaryCol);
        l->setBorderSize({ 0, 0, 0, 0 });
    }

    for (auto* l : { &artistLabel, &titleLabel }) {
        l->setFont(UiStyle::ui(13.0f));
        l->setColour(juce::Label::textColourId, secondaryCol);
        l->setBorderSize({ 0, 0, 0, 0 });
    }

    for (auto* e : { &artistEditor, &titleEditor }) {
        e->setFont(UiStyle::semibold(17.0f));
        e->applyFontToAllText(UiStyle::semibold(17.0f));
        e->setIndents(10, 0);
        e->setJustification(juce::Justification::centredLeft);
    }
    categorySearch.setFont(UiStyle::ui(14.0f));
    categorySearch.setIndents(10, 0);
    categorySearch.setJustification(juce::Justification::centredLeft);
    categorySearch.setTextToShowWhenEmpty(TRANS("search_categories_placeholder"), mutedCol);

    for (auto* l : { &durationLabel, &playCountLabel, &lastPlayedLabel }) {
        l->setFont(UiStyle::ui(14.0f));
        l->setColour(juce::Label::textColourId, theme.getPrimaryTextColour());
    }
    for (auto* l : { &createdAtLabel, &updatedAtLabel, &pathLabel }) {
        l->setFont(UiStyle::ui(12.5f));
        l->setColour(juce::Label::textColourId, mutedCol);
    }
    pathLabel.setMinimumHorizontalScale(0.7f);

    // Acciones secundarias como iconos con ayuda: dejan sitio a Eliminar, Cancelar y Guardar.
    playLocalButton.setButtonText("");
    playLocalButton.setTooltip(TRANS("btn_play_local"));
    refreshButton.setButtonText("");
    refreshButton.setTooltip(TRANS("btn_refresh"));
    saveButton.setButtonText(TRANS("btn_save"));
    cancelButton.setButtonText(TRANS("btn_cancel"));
    deleteButton.setButtonText(TRANS("btn_delete"));

    UiStyle::setVariant(saveButton, "primary");
    UiStyle::setVariant(playLocalButton, "ghost");
    UiStyle::setIcon(playLocalButton, "headphones");
    UiStyle::setVariant(refreshButton, "ghost");
    UiStyle::setIcon(refreshButton, "refresh");
    UiStyle::setVariant(deleteButton, "ghost");
    deleteButton.getProperties().set("textColour", theme.getErrorColour().toString());
    deleteButton.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);

    refreshMetadata();
    resized();  // el ancho de los botones depende de sus textos
    repaint();
}
 

 

void TrackDetailsDialog::refreshMetadata() {
    trackInfo = library.getTrackById(trackInfo.id);
    
    artistEditor.setText(trackInfo.artist);
    titleEditor.setText(trackInfo.title);
    
    int mins = (int)trackInfo.duration / 60;
    int secs = (int)trackInfo.duration % 60;
    durationLabel.setText(TRANS("label_duration") + " " + juce::String::formatted("%02d:%02d", mins, secs), juce::NotificationType::dontSendNotification);
    
    playCountLabel.setText(TRANS("label_plays") + " " + juce::String(trackInfo.playCount), juce::NotificationType::dontSendNotification);
    lastPlayedLabel.setText(TRANS("label_last_played") + " " + (trackInfo.lastPlayed.empty() ? TRANS("value_never") : trackInfo.lastPlayed), juce::NotificationType::dontSendNotification);
    
    createdAtLabel.setText(TRANS("label_imported_at") + " " + trackInfo.createdAt, juce::NotificationType::dontSendNotification);
    updatedAtLabel.setText(TRANS("label_modified_at") + " " + trackInfo.updatedAt, juce::NotificationType::dontSendNotification);
    pathLabel.setText(TRANS("label_path") + juce::String(trackInfo.path), juce::NotificationType::dontSendNotification);
    pathLabel.setTooltip(juce::String(trackInfo.path));

    repaint();
}

void TrackDetailsDialog::deleteTrack() {
    auto* w = new juce::AlertWindow(TRANS("dialog_delete_track"), 
        TRANS("dialog_delete_confirm"),
        juce::AlertWindow::WarningIcon, this);
        
    w->addButton(TRANS("btn_delete_permanent"), 1);
    w->addButton(TRANS("btn_cancel"), 0);
    
    w->enterModalState(true, juce::ModalCallbackFunction::create([this](int result) {
        if (result == 1) { 
             library.deleteTrack(trackInfo.id, true);

             // La pista ya no existe: se refresca quien abrió el diálogo y se cierra.
             auto callback = onSave;
             auto closer = onCancel;
             juce::MessageManager::callAsync([callback, closer]() {
                 if (callback) callback();
                 if (closer) closer();
             });
        }
    }), true);  
}



TrackDetailsDialog::~TrackDetailsDialog() {
    categoryList = nullptr;
}

void TrackDetailsDialog::paint(juce::Graphics& g) {
    auto baseBg = ThemeManager::getInstance().getBaseBackgroundColour();
    auto primaryText = ThemeManager::getInstance().getPrimaryTextColour();
    auto highlightCol = ThemeManager::getInstance().getHighlightColour();

    g.fillAll(baseBg);
    
    if (isSaving) {
        g.fillAll(ThemeManager::getInstance().getBaseBackgroundColour().withAlpha(0.6f));
        g.setColour(highlightCol);
        g.drawText(TRANS("saving"), getLocalBounds(), juce::Justification::centred, true);
        
        auto bounds = getLocalBounds().toFloat();
        auto centre = bounds.getCentre();
        auto radius = 20.0f;
        auto angle = (float)juce::Time::getMillisecondCounter() / 100.0f;
        
        juce::Path p;
        p.addArc(centre.x - radius, centre.y - radius, radius * 2, radius * 2, angle, angle + 4.0f, true);
        g.strokePath(p, juce::PathStrokeType(3.0f));
    }
}

void TrackDetailsDialog::resized() {
    auto area = getLocalBounds().reduced(20, 16);
    const int fieldH = 36;

    // Botones: acciones secundarias a la izquierda, confirmar/cancelar a la derecha.
    auto buttonRow = area.removeFromBottom(36);
    area.removeFromBottom(16);
    auto placeLeft = [&](juce::TextButton& b) {
        const int w = b.getButtonText().isEmpty() ? 36 : b.getBestWidthForHeight(36) + 22;
        b.setBounds(buttonRow.removeFromLeft(w));
        buttonRow.removeFromLeft(4);
    };
    auto placeRight = [&](juce::TextButton& b, int minWidth) {
        const int w = juce::jmax(minWidth, b.getBestWidthForHeight(36) + 26);
        b.setBounds(buttonRow.removeFromRight(w));
        buttonRow.removeFromRight(8);
    };
    placeRight(saveButton, 110);
    placeRight(cancelButton, 100);
    placeLeft(playLocalButton);
    placeLeft(refreshButton);
    placeLeft(deleteButton);

    // Datos editables
    infoSectionHeader.setBounds(area.removeFromTop(18));
    area.removeFromTop(8);
    titleLabel.setBounds(area.removeFromTop(18));
    titleEditor.setBounds(area.removeFromTop(fieldH));
    area.removeFromTop(10);
    artistLabel.setBounds(area.removeFromTop(18));
    artistEditor.setBounds(area.removeFromTop(fieldH));
    area.removeFromTop(18);

    // Detalles y sistema al pie; las categorías ocupan el espacio restante.
    auto systemArea = area.removeFromBottom(18 + 6 + 20 * 3);
    area.removeFromBottom(14);
    auto detailsArea = area.removeFromBottom(18 + 6 + 22 * 2);
    area.removeFromBottom(18);

    categoryFilterLabel.setBounds(area.removeFromTop(18));
    area.removeFromTop(6);
    categorySearch.setBounds(area.removeFromTop(UiStyle::controlHeight));
    area.removeFromTop(6);
    categoryList->setBounds(area);

    detailsSectionHeader.setBounds(detailsArea.removeFromTop(18));
    detailsArea.removeFromTop(6);
    auto detailsRow = detailsArea.removeFromTop(22);
    durationLabel.setBounds(detailsRow.removeFromLeft(detailsRow.getWidth() / 2));
    playCountLabel.setBounds(detailsRow);
    lastPlayedLabel.setBounds(detailsArea.removeFromTop(22));

    systemSectionHeader.setBounds(systemArea.removeFromTop(18));
    systemArea.removeFromTop(6);
    auto systemRow = systemArea.removeFromTop(20);
    createdAtLabel.setBounds(systemRow.removeFromLeft(systemRow.getWidth() / 2));
    updatedAtLabel.setBounds(systemRow);
    pathLabel.setBounds(systemArea.removeFromTop(20));
}

void TrackDetailsDialog::loadData() {
     
}

void TrackDetailsDialog::playLocal() {
    juce::File file(trackInfo.path);
    if (file.existsAsFile()) {
        file.startAsProcess();
    } else {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, TRANS("error_title"), TRANS("error_file_not_found") + " " + trackInfo.path);
    }
}

void TrackDetailsDialog::saveData() {
    if (isSaving) return;

    juce::String newArtist = artistEditor.getText();
    juce::String newTitle = titleEditor.getText();
    
    if (newArtist.trim().isEmpty() || newTitle.trim().isEmpty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, TRANS("error_validation"), TRANS("error_validation_msg"));
        return;
    }

    isSaving = true;
    repaint();
    
     
    artistEditor.setEnabled(false);
    titleEditor.setEnabled(false);
    saveButton.setEnabled(false);
    cancelButton.setEnabled(false);
    
     
    int trackId = trackInfo.id;
    std::vector<int> catIds = selectedCategoryIds;
    juce::Component::SafePointer<TrackDetailsDialog> safeThis(this);

     
    if (library.updateTrackMetadata(trackId, newArtist, newTitle)) {
         
        artistEditor.setText(library.forceUpperCase(newArtist.trim()));
        titleEditor.setText(library.forceUpperCase(newTitle.trim()));
    } else {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, TRANS("error_title"), TRANS("error_title"));
        isSaving = false;
        artistEditor.setEnabled(true);
        titleEditor.setEnabled(true);
        saveButton.setEnabled(true);
        cancelButton.setEnabled(true);
        repaint();
        return;
    }
    
     
    library.setTrackCategories(trackId, catIds);

    if (onSave) {
        onSave();
    }

    isSaving = false;
    artistEditor.setEnabled(true);
    titleEditor.setEnabled(true);
    saveButton.setEnabled(true);
    cancelButton.setEnabled(true);
    repaint();

    // Guardar cierra el diálogo; se hace de forma asíncrona porque cerrar la
    // ventana destruye este componente.
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis != nullptr && safeThis->onCancel)
            safeThis->onCancel();
    });
}
