#include "AudioSettingsDialog.h"
#include "BinaryData.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

 
SettingsDialog::AudioTab::AudioTab(juce::AudioDeviceManager& m, juce::AudioDeviceManager& c)
    : mainMgr(m), cueMgr(c)
{
     
    mainGroup.setTextLabelPosition(juce::Justification::centredLeft);
    addAndMakeVisible(mainGroup);
    
    mainSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
         mainMgr, 0, 0, 2, 2, false, false, false, false
    );
    addAndMakeVisible(mainSelector.get());
    
     
    cueGroup.setTextLabelPosition(juce::Justification::centredLeft);
    addAndMakeVisible(cueGroup);

    cueSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        cueMgr, 0, 0, 2, 2, false, false, false, false
    );
    addAndMakeVisible(cueSelector.get());
    
     
    lookAndFeelChanged(); 
}

void SettingsDialog::AudioTab::resized() {
    auto area = getLocalBounds().reduced(16, 12);
    auto mainArea = area.removeFromTop((area.getHeight() - 12) / 2);
    area.removeFromTop(12);
    auto cueArea = area;

    // El título del grupo va encima del recuadro (22 px); el selector va dentro.
    mainGroup.setBounds(mainArea);
    mainSelector->setBounds(mainArea.withTrimmedTop(26).reduced(8, 6));

    cueGroup.setBounds(cueArea);
    cueSelector->setBounds(cueArea.withTrimmedTop(26).reduced(8, 6));
}

void SettingsDialog::AudioTab::lookAndFeelChanged() {
    mainGroup.setText(TRANS("main_output_label")); 
    cueGroup.setText(TRANS("cue_output_label"));
}

namespace
{
    // Lo que se prepara ahora (restaurar, cambiar de carpeta) se aplica al abrir RAAADIO:
    // se ofrece cerrarlo ya. Si hay algo al aire, cerrar pide confirmación.
    void offerRestart(const juce::String& message)
    {
        juce::AlertWindow::showOkCancelBox(juce::AlertWindow::InfoIcon, TRANS("restart_title"),
            message + "\n\n" + TRANS("restart_question"), TRANS("restart_now"), TRANS("restart_later"), nullptr,
            juce::ModalCallbackFunction::create([](int result) {
                if (result == 1)
                    if (auto* app = juce::JUCEApplication::getInstance())
                        app->systemRequestedQuit();
            }));
    }

    void showError(const juce::String& message)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, TRANS("error_title"), message);
    }

    // Copia la emisora a otra carpeta en un hilo, con avance y botón para cancelar. Se
    // borra sola al terminar.
    class LabCopyJob : public juce::ThreadWithProgressWindow
    {
    public:
        LabCopyJob(juce::File targetFolder, PersistenceManager::DatabaseSnapshot snapshot, std::function<void()> whenDone)
            // Cancelar espera a que termine el archivo en curso (-1): cortar el hilo a la
            // fuerza podría dejar la copia rota.
            : juce::ThreadWithProgressWindow(TRANS("lab_copying"), true, true, -1, TRANS("cancel")),
              target(std::move(targetFolder)), snapshotDatabase(std::move(snapshot)), onDone(std::move(whenDone)) {}

        void run() override
        {
            result = PersistenceManager::copyLabTo(target, snapshotDatabase, [this](double progress) {
                setProgress(progress);
                setStatusMessage(juce::String(juce::roundToInt(progress * 100.0)) + " %");
                return !threadShouldExit();
            }, skipped);
        }

        void threadComplete(bool) override
        {
            if (result.wasOk()) {
                PersistenceManager::setLabRoot(target);
                auto message = TRANS("lab_copy_done").replace("%1", target.getFullPathName());
                if (!skipped.isEmpty())
                    message << "\n\n" << TRANS("lab_copy_skipped").replace("%1", juce::String(skipped.size()))
                            << "\n" << skipped.joinIntoString("\n").substring(0, 600);
                offerRestart(message);
            } else {
                showError(result.getErrorMessage());
            }
            if (onDone) onDone();
            delete this;
        }

    private:
        juce::File target;
        PersistenceManager::DatabaseSnapshot snapshotDatabase;
        std::function<void()> onDone;
        juce::Result result { juce::Result::ok() };
        juce::StringArray skipped;
    };
}

SettingsDialog::LibraryTab::LibraryTab(MusicLibrary& lib, AdvertisingManager& ads) : library(lib), adsManager(ads) {
    addAndMakeVisible(labGroup);
    addAndMakeVisible(dataGroup);
    addAndMakeVisible(migrationGroup);
    addAndMakeVisible(dangerGroup);

    addAndMakeVisible(labPathLabel);
    labPathLabel.setJustificationType(juce::Justification::centredLeft);
    labPathLabel.setFont(UiStyle::mono(13.0f));
    labPathLabel.setMinimumHorizontalScale(0.6f);

    for (auto* label : { &labNextLabel, &dataStatusLabel, &warningLabel }) {
        addAndMakeVisible(*label);
        label->setFont(UiStyle::ui(12.5f));
        label->setMinimumHorizontalScale(0.7f);
    }
    labNextLabel.setJustificationType(juce::Justification::centredLeft);
    dataStatusLabel.setJustificationType(juce::Justification::centredLeft);
    warningLabel.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(changeLabButton);
    addAndMakeVisible(autoBackupToggle);
    addAndMakeVisible(backupButton);
    addAndMakeVisible(restoreButton);
    addAndMakeVisible(exportButton);
    addAndMakeVisible(importButton);
    addAndMakeVisible(resetButton);

    UiStyle::setVariant(resetButton, "danger");
    UiStyle::setIcon(changeLabButton, "folder");

    changeLabButton.onClick = [this] { changeLabRoot(); };

    autoBackupToggle.setToggleState(PersistenceManager::isAutoBackupEnabled(), juce::dontSendNotification);
    autoBackupToggle.onClick = [this] {
        PersistenceManager::setAutoBackupEnabled(autoBackupToggle.getToggleState());
    };

    backupButton.onClick = [this] { performBackup(); };
    restoreButton.onClick = [this] { performRestore(); };
    exportButton.onClick = [this] { performExport(); };
    importButton.onClick = [this] { performImport(); };
    resetButton.onClick = [this] { resetLibrary(); };

    lookAndFeelChanged();
}

PersistenceManager::DatabaseSnapshot SettingsDialog::LibraryTab::databaseSnapshot() {
    return [&lib = library](const juce::File& target) { return lib.backupDatabaseTo(target); };
}

void SettingsDialog::LibraryTab::resized() {
    auto area = getLocalBounds().reduced(15);
    auto groupPadding = 20;
    auto btnH = 35;
    auto gap = 10;

    // 0. Lab Root Group
    auto labArea = area.removeFromTop(118);
    labGroup.setBounds(labArea);
    auto labContent = labArea.reduced(groupPadding);
    labContent.removeFromTop(10);

    labPathLabel.setBounds(labContent.removeFromTop(25));
    labNextLabel.setBounds(labContent.removeFromTop(18));
    labContent.removeFromTop(3);
    changeLabButton.setBounds(labContent.removeFromTop(30).reduced(50, 0));

    area.removeFromTop(15);

    // 1. Data Group
    auto dataArea = area.removeFromTop(140);
    dataGroup.setBounds(dataArea);
    auto dataContent = dataArea.reduced(groupPadding);
    dataContent.removeFromTop(10);

    autoBackupToggle.setBounds(dataContent.removeFromTop(btnH));
    dataContent.removeFromTop(gap/2);

    auto r1 = dataContent.removeFromTop(btnH);
    backupButton.setBounds(r1.removeFromLeft(dataContent.getWidth() / 2 - gap/2));
    restoreButton.setBounds(r1.removeFromRight(dataContent.getWidth() / 2 - gap/2));
    dataStatusLabel.setBounds(dataContent.removeFromTop(20));

    area.removeFromTop(15);

    // 2. Migration Group
    auto migArea = area.removeFromTop(90);
    migrationGroup.setBounds(migArea);
    auto migContent = migArea.reduced(groupPadding);
    migContent.removeFromTop(10);

    auto r2 = migContent.removeFromTop(btnH);
    exportButton.setBounds(r2.removeFromLeft(migContent.getWidth() / 2 - gap/2));
    importButton.setBounds(r2.removeFromRight(migContent.getWidth() / 2 - gap/2));

    area.removeFromTop(15);

    // 3. Danger Zone
    auto dangerArea = area.removeFromTop(120);
    dangerGroup.setBounds(dangerArea);
    auto dangerContent = dangerArea.reduced(groupPadding);
    dangerContent.removeFromTop(10);

    resetButton.setBounds(dangerContent.removeFromTop(btnH).reduced(40, 0));
    warningLabel.setBounds(dangerContent.removeFromTop(25));
}

void SettingsDialog::LibraryTab::lookAndFeelChanged() {
    auto& t = ThemeManager::getInstance();
    warningLabel.setColour(juce::Label::textColourId, t.getMutedTextColour());
    labNextLabel.setColour(juce::Label::textColourId, t.getWarningColour());
    labPathLabel.setColour(juce::Label::backgroundColourId, t.getRaisedColour());
    labPathLabel.setColour(juce::Label::outlineColourId, t.getStrongOutlineColour());
    labPathLabel.setColour(juce::Label::textColourId, t.getSecondaryTextColour());
    updateTexts();
}

void SettingsDialog::LibraryTab::updateTexts() {
    labGroup.setText(TRANS("group_lab_root"));
    dataGroup.setText(TRANS("group_persistence"));
    migrationGroup.setText(TRANS("group_migration"));
    dangerGroup.setText(TRANS("group_danger_zone"));

    const auto current = PersistenceManager::getLabRoot();
    const auto next = PersistenceManager::getConfiguredLabRoot();
    labPathLabel.setText(current.getFullPathName(), juce::dontSendNotification);
    labNextLabel.setText(next != current ? TRANS("lab_next_start").replace("%1", next.getFullPathName()) : juce::String(),
                         juce::dontSendNotification);
    changeLabButton.setButtonText(TRANS("btn_change_lab_root"));
    autoBackupToggle.setButtonText(TRANS("label_auto_backup").replace("%1", juce::String(PersistenceManager::backupKeepDays)));

    auto& t = ThemeManager::getInstance();
    if (PersistenceManager::hasPendingRestore()) {
        dataStatusLabel.setText(TRANS("restore_pending_status"), juce::dontSendNotification);
        dataStatusLabel.setColour(juce::Label::textColourId, t.getWarningColour());
    } else {
        const auto latest = PersistenceManager::getLatestBackupTime();
        dataStatusLabel.setText(latest.toMilliseconds() == 0 ? TRANS("backup_status_none")
                                                             : TRANS("backup_status").replace("%1", latest.formatted("%d/%m/%Y %H:%M")),
                                juce::dontSendNotification);
        dataStatusLabel.setColour(juce::Label::textColourId, t.getMutedTextColour());
    }

    resetButton.setButtonText(TRANS("db_reset"));
    warningLabel.setText(TRANS("reset_hint"), juce::dontSendNotification);

    backupButton.setButtonText(TRANS("btn_backup"));
    restoreButton.setButtonText(TRANS("btn_restore"));
    exportButton.setButtonText(TRANS("btn_export"));
    importButton.setButtonText(TRANS("btn_import"));
}

void SettingsDialog::LibraryTab::performBackup()
{
    juce::File folder;
    const auto res = PersistenceManager::createBackup(databaseSnapshot(), &folder);
    if (res.wasOk())
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, TRANS("backup_title"),
                                               TRANS("backup_success") + "\n" + folder.getFullPathName());
    else
        showError(res.getErrorMessage());
    updateTexts();
}

void SettingsDialog::LibraryTab::performRestore()
{
    fileChooser = std::make_unique<juce::FileChooser>(TRANS("btn_restore"), PersistenceManager::getBackupsFolder());
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& fc) {
            const auto dir = fc.getResult();
            if (!dir.isDirectory()) return;
            const auto res = PersistenceManager::stageRestore(dir);
            updateTexts();
            if (res.wasOk())
                offerRestart(TRANS("restore_staged"));
            else
                showError(res.getErrorMessage());
        });
}

void SettingsDialog::LibraryTab::performExport()
{
    fileChooser = std::make_unique<juce::FileChooser>(TRANS("btn_export"), juce::File::getSpecialLocation(juce::File::userHomeDirectory));
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& fc) {
            const auto dir = fc.getResult();
            if (!dir.isDirectory()) return;
            juce::File folder;
            const auto res = PersistenceManager::exportPersistence(dir, databaseSnapshot(), &folder);
            if (res.wasOk())
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, TRANS("export_title"),
                                                       TRANS("export_success") + "\n" + folder.getFullPathName());
            else
                showError(res.getErrorMessage());
        });
}

void SettingsDialog::LibraryTab::performImport()
{
    fileChooser = std::make_unique<juce::FileChooser>(TRANS("btn_import"), juce::File::getSpecialLocation(juce::File::userHomeDirectory));
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& fc) {
            const auto dir = fc.getResult();
            if (!dir.isDirectory()) return;

            juce::Component::SafePointer<LibraryTab> safeThis(this);
            juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon, TRANS("confirm_import_title"), TRANS("confirm_import_text"),
                TRANS("yes"), TRANS("no"), nullptr,
                juce::ModalCallbackFunction::create([safeThis, dir](int result) {
                    if (result != 1) return;
                    const auto res = PersistenceManager::stageRestore(dir);
                    if (safeThis != nullptr) safeThis->updateTexts();
                    if (res.wasOk())
                        offerRestart(TRANS("restore_staged"));
                    else
                        showError(res.getErrorMessage());
                }));
        });
}

void SettingsDialog::LibraryTab::changeLabRoot()
{
    fileChooser = std::make_unique<juce::FileChooser>(TRANS("select_folder"), juce::File::getSpecialLocation(juce::File::userHomeDirectory));
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& fc) {
            const auto chosen = fc.getResult();
            if (!chosen.isDirectory()) return;

            const auto target = PersistenceManager::labFolderFor(chosen);
            const auto current = PersistenceManager::getLabRoot();
            juce::Component::SafePointer<LibraryTab> safeThis(this);

            if (target == current) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, TRANS("group_lab_root"), TRANS("lab_same"));
                return;
            }

            // Ya hay una emisora ahí (otro equipo, una copia): se usa esa, sin copiar nada.
            if (PersistenceManager::containsStation(target)) {
                juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon, TRANS("group_lab_root"),
                    TRANS("lab_use_existing").replace("%1", target.getFullPathName()),
                    TRANS("lab_use_existing_btn"), TRANS("cancel"), nullptr,
                    juce::ModalCallbackFunction::create([safeThis, target](int result) {
                        if (result != 1) return;
                        PersistenceManager::setLabRoot(target);
                        if (safeThis != nullptr) safeThis->updateTexts();
                        offerRestart(TRANS("lab_switch_done"));
                    }));
                return;
            }

            if (target == current || target.isAChildOf(current)) {
                showError(TRANS("lab_err_inside"));
                return;
            }
            if (target.existsAsFile()
                || (target.isDirectory() && target.getNumberOfChildFiles(juce::File::findFilesAndDirectories) > 0)) {
                showError(TRANS("lab_err_not_empty").replace("%1", target.getFullPathName()));
                return;
            }

            const auto needed = PersistenceManager::getLabSizeToCopy();
            const auto available = chosen.getBytesFreeOnVolume();
            if (available > 0 && available < needed + (64 << 20)) {
                showError(TRANS("lab_err_space").replace("%1", juce::File::descriptionOfSizeInBytes(needed))
                                                .replace("%2", juce::File::descriptionOfSizeInBytes(available)));
                return;
            }

            // No hay emisora: se copia la actual, completa.
            juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon, TRANS("group_lab_root"),
                TRANS("lab_copy_confirm").replace("%1", target.getFullPathName())
                                         .replace("%2", juce::File::descriptionOfSizeInBytes(needed)),
                TRANS("lab_copy_btn"), TRANS("cancel"), nullptr,
                juce::ModalCallbackFunction::create([safeThis, target](int result) {
                    if (result == 1 && safeThis != nullptr) safeThis->copyLabTo(target);
                }));
        });
}

void SettingsDialog::LibraryTab::copyLabTo(const juce::File& target)
{
    juce::Component::SafePointer<LibraryTab> safeThis(this);
    auto* job = new LabCopyJob(target, databaseSnapshot(), [safeThis] {
        if (safeThis != nullptr) safeThis->updateTexts();
    });
    job->launchThread(juce::Thread::Priority::low);  // no compite con los decks por el disco
}

void SettingsDialog::LibraryTab::resetLibrary()
{
    juce::Component::SafePointer<LibraryTab> safeThis(this);
    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon, TRANS("reset_confirm_title"), TRANS("reset_confirm_msg"),
        TRANS("reset_confirm_btn"), TRANS("cancel"), nullptr,
        juce::ModalCallbackFunction::create([safeThis](int result) {
            if (result != 1 || safeThis == nullptr) return;
            auto& tab = *safeThis;

            // Primero el respaldo: si no se puede hacer, no se borra nada.
            juce::File folder;
            const auto backup = PersistenceManager::createBackup(tab.databaseSnapshot(), &folder);
            if (backup.failed()) {
                showError(TRANS("reset_err_backup") + "\n" + backup.getErrorMessage());
                return;
            }

            tab.library.resetLibrary();
            tab.adsManager.resetData();
            if (tab.onLibraryReset) tab.onLibraryReset();
            tab.updateTexts();

            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, TRANS("reset_confirm_title"),
                                                   TRANS("reset_done") + "\n" + folder.getFullPathName());
        }));
}

 
SettingsDialog::LanguageTab::LanguageTab() {
    addAndMakeVisible(languageLabel);
    languageLabel.setText(TRANS("language"), juce::dontSendNotification);
    languageLabel.setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(languageSelector);
    
    auto langs = LanguageManager::getInstance().getAvailableLanguages();
    int index = 1;
    int selectedId = 1;
    juce::String currentCode = LanguageManager::getInstance().getCurrentLanguage();

    for (const auto& pair : langs) {
        languageSelector.addItem(pair.first, index);
        if (pair.second == currentCode) {
            selectedId = index;
        }
        index++;
    }
    
    languageSelector.setSelectedId(selectedId, juce::dontSendNotification);
    
    languageSelector.onChange = [this] {
        int id = languageSelector.getSelectedId();
        if (id <= 0) return;
        
        auto langs = LanguageManager::getInstance().getAvailableLanguages();
        if (id <= (int)langs.size()) {
             juce::String code = langs[id - 1].second;
             LanguageManager::getInstance().setLanguage(code);
        }
    };

     
    addAndMakeVisible(themeLabel);
    themeLabel.setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(themeSelector);
    themeSelector.addItem(TRANS("theme_light"), 1);
    themeSelector.addItem(TRANS("theme_dark"), 2);
    themeSelector.setSelectedId(ThemeManager::getInstance().getTheme() == ThemeManager::Theme::Light ? 1 : 2, juce::dontSendNotification);
    themeSelector.onChange = [this] {
        ThemeManager::getInstance().setTheme(themeSelector.getSelectedId() == 1 ? ThemeManager::Theme::Light : ThemeManager::Theme::Dark);
    };

    lookAndFeelChanged();  
}

void SettingsDialog::LanguageTab::resized() {
    auto area = getLocalBounds().reduced(24, 20);

    // Rótulo encima de cada selector, con ancho limitado para que no se estire de más.
    auto column = area.withWidth(juce::jmin(area.getWidth(), 360));
    languageLabel.setBounds(column.removeFromTop(20));
    column.removeFromTop(6);
    languageSelector.setBounds(column.removeFromTop(UiStyle::controlHeight + 4));
    column.removeFromTop(20);
    themeLabel.setBounds(column.removeFromTop(20));
    column.removeFromTop(6);
    themeSelector.setBounds(column.removeFromTop(UiStyle::controlHeight + 4));
}

void SettingsDialog::LanguageTab::lookAndFeelChanged() {
    const auto secondary = ThemeManager::getInstance().getSecondaryTextColour();

    languageLabel.setText(TRANS("language"), juce::dontSendNotification);
    themeLabel.setText(TRANS("theme"), juce::dontSendNotification);

    for (auto* l : { &languageLabel, &themeLabel }) {
        l->setJustificationType(juce::Justification::centredLeft);
        l->setFont(UiStyle::caption(12.0f));
        l->setColour(juce::Label::textColourId, secondary);
        l->setBorderSize({ 0, 0, 0, 0 });
    }

    themeSelector.changeItemText(1, TRANS("theme_light"));
    themeSelector.changeItemText(2, TRANS("theme_dark"));
    repaint();
}


 
SettingsDialog::UpdatesTab::UpdatesTab() {
    addAndMakeVisible(bannerImage);
    
     
     
    bannerImage.setImage(juce::ImageCache::getFromMemory(BinaryData::aboutbanner_png, BinaryData::aboutbanner_pngSize),
                         juce::RectanglePlacement::centred);
    
    addAndMakeVisible(appInfoLabel);
    appInfoLabel.setJustificationType(juce::Justification::centred);
    appInfoLabel.setColour(juce::Label::textColourId, ThemeManager::getInstance().getPrimaryTextColour());
    
    lookAndFeelChanged();
}

void SettingsDialog::UpdatesTab::resized() {
    auto area = getLocalBounds();
     
    auto bannerArea = area.removeFromTop(200); 
    bannerImage.setBounds(bannerArea.reduced(10));
    
     
    appInfoLabel.setBounds(area.reduced(10));
}

void SettingsDialog::UpdatesTab::paint(juce::Graphics& g) {
    
}

void SettingsDialog::UpdatesTab::lookAndFeelChanged() {
    appInfoLabel.setText(juce::String(juce::CharPointer_UTF8("RAAADIO  \xc2\xb7  ")) + TRANS("app_info_text")
                             + " " + juce::String(ProjectInfo::versionString),
                         juce::dontSendNotification);
    appInfoLabel.setFont(UiStyle::semibold(15.0f));
    appInfoLabel.setColour(juce::Label::textColourId, ThemeManager::getInstance().getSecondaryTextColour());
}
//==============================================================================
SettingsDialog::BroadcastTab::BroadcastTab(StationConfig& config, StreamEncoder& encoder, juce::File logFolder)
    : station(config), streamEncoder(encoder), airLogFolder(std::move(logFolder))
{
    const auto& s = station.get();

    for (auto* group : { &deadAirGroup, &airLogGroup, &streamGroup }) {
        group->setTextLabelPosition(juce::Justification::centredLeft);
        addAndMakeVisible(group);
    }

    // Protección contra silencio
    addAndMakeVisible(deadAirToggle);
    deadAirToggle.setToggleState(s.deadAirEnabled, juce::dontSendNotification);
    deadAirToggle.onClick = [this] { apply(); };

    addAndMakeVisible(deadAirSecondsLabel);
    addAndMakeVisible(deadAirSeconds);
    for (int seconds : { 10, 15, 20, 30, 45, 60 })
        deadAirSeconds.addItem(juce::String(seconds) + " s", seconds);
    deadAirSeconds.setSelectedId(s.deadAirSeconds, juce::dontSendNotification);
    if (deadAirSeconds.getSelectedId() == 0) deadAirSeconds.setSelectedId(20, juce::dontSendNotification);
    deadAirSeconds.onChange = [this] { apply(); };

    addAndMakeVisible(deadAirHint);
    deadAirHint.setJustificationType(juce::Justification::topLeft);

    // Registro de lo emitido
    addAndMakeVisible(airLogToggle);
    airLogToggle.setToggleState(s.airLogEnabled, juce::dontSendNotification);
    airLogToggle.onClick = [this] { apply(); };

    addAndMakeVisible(retentionLabel);
    addAndMakeVisible(retention);
    for (int days : { 7, 15, 30, 60, 90, 180, 365 })
        retention.addItem(juce::String(days), days);
    retention.setSelectedId(s.airLogRetentionDays, juce::dontSendNotification);
    if (retention.getSelectedId() == 0) retention.setSelectedId(30, juce::dontSendNotification);
    retention.onChange = [this] { apply(); };

    addAndMakeVisible(openLogFolderButton);
    UiStyle::setVariant(openLogFolderButton, "ghost");
    UiStyle::setIcon(openLogFolderButton, "folder");
    openLogFolderButton.onClick = [this] {
        airLogFolder.createDirectory();
        airLogFolder.revealToUser();
    };

    // Transmisión: los campos se guardan al salir de ellos o con Enter.
    const std::pair<Field*, juce::String> fields[] = {
        { &host, s.streamHost }, { &port, juce::String(s.streamPort) }, { &mount, s.streamMount },
        { &user, s.streamUser }, { &password, s.streamPassword }, { &name, s.streamName }
    };
    for (const auto& [field, text] : fields) {
        addAndMakeVisible(field->label);
        addAndMakeVisible(field->editor);
        field->editor.setText(text, juce::dontSendNotification);
        field->editor.onFocusLost = [this] { apply(); };
        field->editor.onReturnKey = [this] { apply(); };
    }
    port.editor.setInputRestrictions(5, "0123456789");
    password.editor.setPasswordCharacter((juce::juce_wchar) 0x2022);

    addAndMakeVisible(qualityLabel);
    addAndMakeVisible(quality);
    const auto qualities = juce::OggVorbisAudioFormat().getQualityOptions();
    for (int i = 0; i < qualities.size(); ++i)
        quality.addItem(qualities[i], i + 1);
    quality.setSelectedId(s.streamQuality + 1, juce::dontSendNotification);
    quality.onChange = [this] { apply(); };

    addAndMakeVisible(streamToggle);
    streamToggle.setToggleState(s.streamEnabled, juce::dontSendNotification);
    streamToggle.onClick = [this] { apply(); };

    addAndMakeVisible(streamStatusLabel);

    lookAndFeelChanged();
    startTimer(500);
}

void SettingsDialog::BroadcastTab::apply() {
    auto s = station.get();
    s.deadAirEnabled = deadAirToggle.getToggleState();
    if (deadAirSeconds.getSelectedId() > 0) s.deadAirSeconds = deadAirSeconds.getSelectedId();
    s.airLogEnabled = airLogToggle.getToggleState();
    if (retention.getSelectedId() > 0) s.airLogRetentionDays = retention.getSelectedId();

    s.streamHost = host.editor.getText().trim();
    s.streamPort = juce::jlimit(1, 65535, port.editor.getText().getIntValue());
    s.streamMount = mount.editor.getText().trim();
    s.streamUser = user.editor.getText().trim();
    s.streamPassword = password.editor.getText();
    s.streamName = name.editor.getText().trim();
    if (quality.getSelectedId() > 0) s.streamQuality = quality.getSelectedId() - 1;

    // Sin servidor no hay a dónde transmitir.
    if (s.streamHost.isEmpty()) streamToggle.setToggleState(false, juce::dontSendNotification);
    s.streamEnabled = streamToggle.getToggleState();

    station.set(s);
}

void SettingsDialog::BroadcastTab::timerCallback() {
    auto& t = ThemeManager::getInstance();
    juce::String text;
    juce::Colour colour = t.getSecondaryTextColour();
    switch (streamEncoder.getStatus()) {
        case StreamEncoder::Status::Off:        text = TRANS("stream_state_off"); break;
        case StreamEncoder::Status::Connecting: text = TRANS("stream_state_connecting"); colour = t.getWarningColour(); break;
        case StreamEncoder::Status::Connected:  text = TRANS("stream_state_connected"); colour = t.getSuccessColour(); break;
        case StreamEncoder::Status::Error:      text = streamEncoder.getLastError(); colour = t.getErrorColour(); break;
    }
    streamStatusLabel.setText(text, juce::dontSendNotification);
    streamStatusLabel.setColour(juce::Label::textColourId, colour);
}

void SettingsDialog::BroadcastTab::resized() {
    auto area = getLocalBounds().reduced(16, 12);
    const int rowH = UiStyle::controlHeight;

    // El título de cada grupo va encima del recuadro; el contenido, dentro.
    auto groupContent = [](juce::GroupComponent& group, juce::Rectangle<int> groupArea) {
        group.setBounds(groupArea);
        return groupArea.withTrimmedTop(26).reduced(14, 8);
    };
    auto labelledRow = [rowH](juce::Label& label, juce::Component& control, juce::Rectangle<int> row, int labelWidth) {
        label.setBounds(row.removeFromLeft(labelWidth));
        row.removeFromLeft(8);
        const int width = juce::jmin(row.getWidth(), 120);
        control.setBounds(row.removeFromLeft(width).withSizeKeepingCentre(width, rowH));
    };

    // Protección contra silencio
    auto deadAir = groupContent(deadAirGroup, area.removeFromTop(112));
    auto row = deadAir.removeFromTop(rowH);
    deadAirToggle.setBounds(row.removeFromLeft(row.getWidth() / 2));
    labelledRow(deadAirSecondsLabel, deadAirSeconds, row, 110);
    deadAir.removeFromTop(4);
    deadAirHint.setBounds(deadAir);

    area.removeFromTop(12);

    // Registro de lo emitido
    auto airLog = groupContent(airLogGroup, area.removeFromTop(78));
    row = airLog.removeFromTop(rowH);
    openLogFolderButton.setBounds(row.removeFromRight(juce::jmax(110, openLogFolderButton.getBestWidthForHeight(rowH) + 16)));
    row.removeFromRight(8);
    airLogToggle.setBounds(row.removeFromLeft(row.getWidth() - 170));
    labelledRow(retentionLabel, retention, row, 50);

    area.removeFromTop(12);

    // Transmisión
    auto stream = groupContent(streamGroup, area.removeFromTop(juce::jmin(area.getHeight(), 300)));
    auto fieldRow = [&](Field& left, Field& right, int rightWidth) {
        auto r = stream.removeFromTop(18 + 4 + rowH);
        auto rightArea = r.removeFromRight(rightWidth);
        r.removeFromRight(12);
        for (auto [field, cell] : { std::make_pair(&left, r), std::make_pair(&right, rightArea) }) {
            field->label.setBounds(cell.removeFromTop(18));
            cell.removeFromTop(4);
            field->editor.setBounds(cell);
        }
        stream.removeFromTop(8);
    };
    const int half = (stream.getWidth() - 12) / 2;
    fieldRow(host, port, 110);
    fieldRow(mount, user, half);
    fieldRow(password, name, half);

    row = stream.removeFromTop(rowH);
    qualityLabel.setBounds(row.removeFromLeft(70));
    quality.setBounds(row.removeFromLeft(130));
    row.removeFromLeft(16);
    streamToggle.setBounds(row);
    stream.removeFromTop(6);
    streamStatusLabel.setBounds(stream.removeFromTop(22));
}

void SettingsDialog::BroadcastTab::lookAndFeelChanged() {
    auto& t = ThemeManager::getInstance();

    deadAirGroup.setText(TRANS("group_dead_air"));
    airLogGroup.setText(TRANS("group_air_log"));
    streamGroup.setText(TRANS("group_stream"));

    deadAirToggle.setButtonText(TRANS("dead_air_enable"));
    deadAirSecondsLabel.setText(TRANS("dead_air_after"), juce::dontSendNotification);
    deadAirHint.setText(TRANS("dead_air_hint"), juce::dontSendNotification);
    deadAirHint.setFont(UiStyle::ui(12.5f));
    deadAirHint.setColour(juce::Label::textColourId, t.getSecondaryTextColour());

    airLogToggle.setButtonText(TRANS("air_log_enable"));
    airLogToggle.setTooltip(TRANS("tip_air_log"));
    retentionLabel.setText(TRANS("air_log_keep_days"), juce::dontSendNotification);
    openLogFolderButton.setButtonText(TRANS("air_log_open"));

    host.label.setText(TRANS("stream_host"), juce::dontSendNotification);
    port.label.setText(TRANS("stream_port"), juce::dontSendNotification);
    mount.label.setText(TRANS("stream_mount"), juce::dontSendNotification);
    user.label.setText(TRANS("stream_user"), juce::dontSendNotification);
    password.label.setText(TRANS("stream_password"), juce::dontSendNotification);
    // La clave está cifrada para otro equipo o usuario de Windows (la emisora se movió).
    password.editor.setTextToShowWhenEmpty(station.get().isStreamPasswordUnreadable() ? TRANS("stream_password_reenter") : juce::String(),
                                           ThemeManager::getInstance().getWarningColour());
    name.label.setText(TRANS("stream_name"), juce::dontSendNotification);
    qualityLabel.setText(TRANS("stream_quality"), juce::dontSendNotification);
    streamToggle.setButtonText(TRANS("stream_enable"));
    streamToggle.setTooltip(TRANS("tip_stream_enable"));

    for (auto* label : { &host.label, &port.label, &mount.label, &user.label, &password.label, &name.label }) {
        label->setFont(UiStyle::caption(12.0f));
        label->setColour(juce::Label::textColourId, t.getSecondaryTextColour());
        label->setBorderSize({ 0, 0, 0, 0 });
    }
    for (auto* label : { &deadAirSecondsLabel, &retentionLabel, &qualityLabel }) {
        label->setFont(UiStyle::ui(13.5f));
        label->setColour(juce::Label::textColourId, t.getSecondaryTextColour());
        label->setJustificationType(juce::Justification::centredRight);
    }
    streamStatusLabel.setFont(UiStyle::ui(13.0f));
    timerCallback();
}

//==============================================================================
SettingsDialog::SettingsDialog(juce::AudioDeviceManager& m, juce::AudioDeviceManager& c, MusicLibrary& lib, AdvertisingManager& ads,
                               StationConfig& station, StreamEncoder& streamEncoder, juce::File airLogFolder)
{
    audioTab = std::make_unique<AudioTab>(m, c);
    libraryTab = std::make_unique<LibraryTab>(lib, ads);
    libraryTab->onLibraryReset = [this] { if (onLibraryReset) onLibraryReset(); };
    broadcastTab = std::make_unique<BroadcastTab>(station, streamEncoder, airLogFolder);
    languageTab = std::make_unique<LanguageTab>();
    updatesTab = std::make_unique<UpdatesTab>();



    tabs.addTab(TRANS("audio_settings"), juce::Colour(0x00000000), audioTab.get(), false);
    tabs.addTab(TRANS("library_settings"), juce::Colour(0x00000000), libraryTab.get(), false);
    tabs.addTab(TRANS("broadcast_settings"), juce::Colour(0x00000000), broadcastTab.get(), false);
    tabs.addTab(TRANS("language"), juce::Colour(0x00000000), languageTab.get(), false);
    tabs.addTab(TRANS("tab_updates"), juce::Colour(0x00000000), updatesTab.get(), false);
    tabs.setTabBarDepth(42);
    tabs.setOutline(0);

    addAndMakeVisible(tabs);
    setSize(640, 660);
}

SettingsDialog::~SettingsDialog() {}

void SettingsDialog::resized() {
    tabs.setBounds(getLocalBounds().reduced(12, 6));
}

void SettingsDialog::paint(juce::Graphics& g) {
    g.fillAll(ThemeManager::getInstance().getBaseBackgroundColour()); 
}

void SettingsDialog::lookAndFeelChanged() {
     
    tabs.setTabName(0, TRANS("audio_settings"));
    tabs.setTabName(1, TRANS("library_settings"));
    tabs.setTabName(2, TRANS("broadcast_settings"));
    tabs.setTabName(3, TRANS("language"));
    tabs.setTabName(4, TRANS("tab_updates"));
    
     
     
     
}
