#include "TrackDetailsDialog.h"
#include "../helpers/External.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

TrackDetailsDialog::TrackDetailsDialog(MusicLibrary& lib, const TrackInfo& t) 
    : library(lib), trackInfo(t) 
{
     
    addAndMakeVisible(infoSectionHeader);
    addAndMakeVisible(detailsSectionHeader);
    addAndMakeVisible(systemSectionHeader);

    // Las cuñas, identificaciones y separadores no llevan créditos ni se ordenan en Musica.
    systemTrack = library.getSystemRolesByTrack().count(trackInfo.id) > 0;
    namingContext = library.makeNamingContext({});
    addAndMakeVisible(creditsEditor);
    creditsEditor.setContext(&namingContext);
    creditsEditor.setCreditsVisible(!systemTrack);
    creditsEditor.onCommit = [this] { saveData(); };

     
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

    setSize(540, systemTrack ? 600 : 700);
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

    for (auto* l : { &infoSectionHeader, &detailsSectionHeader, &systemSectionHeader, &categoryFilterLabel }) {
        l->setFont(UiStyle::caption(12.0f));
        l->setColour(juce::Label::textColourId, secondaryCol);
        l->setBorderSize({ 0, 0, 0, 0 });
    }
    creditsEditor.sendLookAndFeelChange();
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
    // Una canción que nadie ha confirmado (de versiones anteriores, todo en mayúsculas) se
    // propone en Tipo Título; al guardar queda confirmada y ordenada en Musica.
    auto values = library.getTrackCredits(trackInfo);
    if (trackInfo.confirmedAt.empty() && !systemTrack) {
        values.artist = TrackNaming::smartCase(values.artist);
        values.title = TrackNaming::smartCase(values.title);
    }
    creditsEditor.setValues(values);

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
    creditsEditor.setBounds(area.removeFromTop(creditsEditor.getIdealHeight()));
    area.removeFromTop(8);

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
        External::openFile(file);
    } else {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, TRANS("error_title"), TRANS("error_file_not_found") + " " + trackInfo.path);
    }
}

void TrackDetailsDialog::saveData() {
    if (isSaving) return;

    const auto values = creditsEditor.getValues();
    if (values.title.isEmpty() || (!systemTrack && values.artist.isEmpty())) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, TRANS("error_validation"), TRANS("error_validation_msg"));
        return;
    }

    isSaving = true;
    repaint();

    // Primero las categorías: dicen si es música (se ordena en Musica) o una cuña.
    library.setTrackCategories(trackInfo.id, selectedCategoryIds);
    const auto result = library.confirmTrack(trackInfo.id, values);
    isSaving = false;

    if (!result.ok) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, TRANS("error_title"), TRANS("error_track_save"));
        repaint();
        return;
    }
    if (result.moveDeferred)
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, TRANS("track_saved_title"), TRANS("track_move_deferred"));

    if (onSave) onSave();

    // Guardar cierra el diálogo; se hace de forma asíncrona porque cerrar la
    // ventana destruye este componente.
    juce::Component::SafePointer<TrackDetailsDialog> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis != nullptr && safeThis->onCancel)
            safeThis->onCancel();
    });
}
