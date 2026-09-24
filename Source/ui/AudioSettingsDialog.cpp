#include "AudioSettingsDialog.h"
#include "BinaryData.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"
#include "../network/Mp3Encoder.h"

 
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
SettingsDialog::BroadcastTab::BroadcastTab(StationConfig& config, StreamEncoder& encoder, NowPlayingPublisher* publisher,
                                           juce::File logFolder)
    : station(config), streamEncoder(encoder), nowPlaying(publisher), airLogFolder(std::move(logFolder))
{
    const auto& s = station.get();

    for (auto* group : { &deadAirGroup, &airLogGroup, &streamGroup, &nowPlayingGroup }) {
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
        { &user, s.streamUser }, { &password, s.streamPassword }, { &name, s.streamName },
        { &genre, s.streamGenre }, { &website, s.streamWebsite }, { &description, s.streamDescription },
        { &pushUrl, s.nowPlayingPushUrl }
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

    for (auto* choice : { &format, &quality }) {
        addAndMakeVisible(choice->label);
        addAndMakeVisible(choice->combo);
    }
    format.combo.addItem("MP3", 1);
    format.combo.addItem("Ogg Vorbis", 2);
    format.combo.setSelectedId(s.streamFormat == StationSettings::StreamFormat::Mp3 ? 1 : 2, juce::dontSendNotification);
    format.combo.onChange = [this] {
        fillQualityOptions();
        apply();
    };
    fillQualityOptions();
    quality.combo.onChange = [this] { apply(); };

    addAndMakeVisible(publicToggle);
    publicToggle.setToggleState(s.streamPublic, juce::dontSendNotification);
    publicToggle.onClick = [this] { apply(); };

    addAndMakeVisible(metadataDelayLabel);
    addAndMakeVisible(metadataDelay);
    for (int seconds : { 0, 2, 4, 6, 8, 10, 15, 20, 30 })
        metadataDelay.addItem(juce::String(seconds) + " s", seconds + 1);
    metadataDelay.setSelectedId(s.streamMetadataDelay + 1, juce::dontSendNotification);
    if (metadataDelay.getSelectedId() == 0) metadataDelay.setSelectedId(1, juce::dontSendNotification);
    metadataDelay.onChange = [this] { apply(); };

    addAndMakeVisible(streamToggle);
    streamToggle.setToggleState(s.streamEnabled, juce::dontSendNotification);
    streamToggle.onClick = [this] { apply(); };

    addAndMakeVisible(streamStatusLabel);

    // Lo que suena
    addAndMakeVisible(nowPlayingHint);
    nowPlayingHint.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(viewDataButton);
    UiStyle::setVariant(viewDataButton, "ghost");
    viewDataButton.onClick = [] { juce::URL("http://127.0.0.1:8080/nowplaying.json").launchInDefaultBrowser(); };
    addAndMakeVisible(pushStatusLabel);

    lookAndFeelChanged();
    startTimer(500);
}

void SettingsDialog::BroadcastTab::fillQualityOptions() {
    const auto& s = station.get();
    const bool mp3 = format.combo.getSelectedId() == 1;
    quality.combo.clear(juce::dontSendNotification);
    if (mp3) {
        for (int kbps : Mp3Encoder::bitrates())
            quality.combo.addItem(juce::String(kbps) + " kbps", kbps);
        quality.combo.setSelectedId(s.streamBitrate, juce::dontSendNotification);
        if (quality.combo.getSelectedId() == 0) quality.combo.setSelectedId(Mp3Encoder::defaultBitrate, juce::dontSendNotification);
    } else {
        const auto qualities = juce::OggVorbisAudioFormat().getQualityOptions();
        for (int i = 0; i < qualities.size(); ++i)
            quality.combo.addItem(qualities[i], i + 1);
        quality.combo.setSelectedId(s.streamQuality + 1, juce::dontSendNotification);
    }
    // El título retrasado es solo para MP3: en Ogg viaja pegado al audio.
    metadataDelay.setEnabled(mp3);
    metadataDelayLabel.setEnabled(mp3);
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
    s.streamFormat = format.combo.getSelectedId() == 1 ? StationSettings::StreamFormat::Mp3 : StationSettings::StreamFormat::OggVorbis;
    if (quality.combo.getSelectedId() > 0) {
        if (s.streamFormat == StationSettings::StreamFormat::Mp3) s.streamBitrate = quality.combo.getSelectedId();
        else s.streamQuality = quality.combo.getSelectedId() - 1;
    }
    s.streamGenre = genre.editor.getText().trim();
    s.streamWebsite = website.editor.getText().trim();
    s.streamDescription = description.editor.getText().trim();
    s.streamPublic = publicToggle.getToggleState();
    if (metadataDelay.getSelectedId() > 0) s.streamMetadataDelay = metadataDelay.getSelectedId() - 1;
    s.nowPlayingPushUrl = pushUrl.editor.getText().trim();

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

    // Último envío a la página web.
    const auto url = pushUrl.editor.getText().trim();
    text = {};
    colour = t.getSecondaryTextColour();
    if (url.isNotEmpty() && !NowPlayingPublisher::isValidPushUrl(url)) {
        text = TRANS("now_playing_push_invalid");
        colour = t.getWarningColour();
    } else if (url.isNotEmpty() && nowPlaying != nullptr) {
        const auto status = nowPlaying->getPushStatus();
        if (!status.attempted) {
            text = TRANS("now_playing_push_waiting");
        } else if (status.ok) {
            text = TRANS("now_playing_push_ok") + " " + status.time.formatted("%H:%M:%S");
            colour = t.getSuccessColour();
        } else {
            text = TRANS("now_playing_push_failed") + " ("
                   + (status.httpStatus > 0 ? "HTTP " + juce::String(status.httpStatus) : TRANS("now_playing_push_no_response"))
                   + ") " + status.time.formatted("%H:%M:%S");
            colour = t.getErrorColour();
        }
    }
    pushStatusLabel.setText(text, juce::dontSendNotification);
    pushStatusLabel.setColour(juce::Label::textColourId, colour);
}

namespace {
    // Medidas de la pestaña Emisión, compartidas por resized() y getIdealHeight().
    constexpr int margin = 12, groupGap = 12, deadAirHeight = 112, airLogHeight = 78;
    constexpr int labelHeight = 18, fieldRowHeight = 18 + 4 + UiStyle::controlHeight + 8;
    constexpr int streamHeight = 26 + 8 + 6 * fieldRowHeight + (UiStyle::controlHeight + 8)
                               + (UiStyle::controlHeight + 6) + 22 + 8;
    constexpr int nowPlayingHintHeight = 50;
    constexpr int nowPlayingHeight = 26 + 8 + nowPlayingHintHeight + 6 + (18 + 4 + UiStyle::controlHeight) + 6 + 22 + 8;
}

int SettingsDialog::BroadcastTab::getIdealHeight() {
    return margin + deadAirHeight + groupGap + airLogHeight + groupGap + streamHeight + groupGap + nowPlayingHeight + margin;
}

void SettingsDialog::BroadcastTab::resized() {
    auto area = getLocalBounds().reduced(16, margin);
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
    // Etiqueta arriba, control abajo.
    auto stacked = [](juce::Label& label, juce::Component& control, juce::Rectangle<int> cell) {
        label.setBounds(cell.removeFromTop(labelHeight));
        cell.removeFromTop(4);
        control.setBounds(cell.removeFromTop(UiStyle::controlHeight));
    };

    // Protección contra silencio
    auto deadAir = groupContent(deadAirGroup, area.removeFromTop(deadAirHeight));
    auto row = deadAir.removeFromTop(rowH);
    deadAirToggle.setBounds(row.removeFromLeft(row.getWidth() / 2));
    labelledRow(deadAirSecondsLabel, deadAirSeconds, row, 110);
    deadAir.removeFromTop(4);
    deadAirHint.setBounds(deadAir);

    area.removeFromTop(groupGap);

    // Registro de lo emitido
    auto airLog = groupContent(airLogGroup, area.removeFromTop(airLogHeight));
    row = airLog.removeFromTop(rowH);
    openLogFolderButton.setBounds(row.removeFromRight(juce::jmax(110, openLogFolderButton.getBestWidthForHeight(rowH) + 16)));
    row.removeFromRight(8);
    airLogToggle.setBounds(row.removeFromLeft(row.getWidth() - 170));
    labelledRow(retentionLabel, retention, row, 50);

    area.removeFromTop(groupGap);

    // Transmisión
    auto stream = groupContent(streamGroup, area.removeFromTop(streamHeight));
    auto twoCells = [&](juce::Label& leftLabel, juce::Component& left, juce::Label& rightLabel, juce::Component& right, int rightWidth) {
        auto r = stream.removeFromTop(fieldRowHeight);
        auto rightArea = r.removeFromRight(rightWidth);
        r.removeFromRight(12);
        stacked(leftLabel, left, r);
        stacked(rightLabel, right, rightArea);
    };
    const int half = (stream.getWidth() - 12) / 2;
    twoCells(host.label, host.editor, port.label, port.editor, 110);
    twoCells(mount.label, mount.editor, user.label, user.editor, half);
    twoCells(password.label, password.editor, name.label, name.editor, half);
    twoCells(format.label, format.combo, quality.label, quality.combo, half);
    twoCells(genre.label, genre.editor, website.label, website.editor, half);
    stacked(description.label, description.editor, stream.removeFromTop(fieldRowHeight));

    row = stream.removeFromTop(rowH);
    stream.removeFromTop(8);
    metadataDelay.setBounds(row.removeFromRight(90));
    row.removeFromRight(8);
    metadataDelayLabel.setBounds(row.removeFromRight(150));
    publicToggle.setBounds(row);

    row = stream.removeFromTop(rowH);
    stream.removeFromTop(6);
    streamToggle.setBounds(row);
    streamStatusLabel.setBounds(stream.removeFromTop(22));

    area.removeFromTop(groupGap);

    // Lo que suena
    auto published = groupContent(nowPlayingGroup, area.removeFromTop(nowPlayingHeight));
    nowPlayingHint.setBounds(published.removeFromTop(nowPlayingHintHeight));
    published.removeFromTop(6);
    auto urlRow = published.removeFromTop(labelHeight + 4 + rowH);
    auto buttonArea = urlRow.removeFromRight(juce::jmax(110, viewDataButton.getBestWidthForHeight(rowH) + 16));
    urlRow.removeFromRight(8);
    stacked(pushUrl.label, pushUrl.editor, urlRow);
    viewDataButton.setBounds(buttonArea.removeFromBottom(rowH));
    published.removeFromTop(6);
    pushStatusLabel.setBounds(published.removeFromTop(22));
}

void SettingsDialog::BroadcastTab::lookAndFeelChanged() {
    auto& t = ThemeManager::getInstance();

    deadAirGroup.setText(TRANS("group_dead_air"));
    airLogGroup.setText(TRANS("group_air_log"));
    streamGroup.setText(TRANS("group_stream"));
    nowPlayingGroup.setText(TRANS("group_now_playing"));

    deadAirToggle.setButtonText(TRANS("dead_air_enable"));
    deadAirSecondsLabel.setText(TRANS("dead_air_after"), juce::dontSendNotification);
    deadAirHint.setText(TRANS("dead_air_hint"), juce::dontSendNotification);

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
                                           t.getWarningColour());
    name.label.setText(TRANS("stream_name"), juce::dontSendNotification);
    format.label.setText(TRANS("stream_format"), juce::dontSendNotification);
    format.combo.setTooltip(TRANS("tip_stream_format"));
    quality.label.setText(TRANS("stream_quality"), juce::dontSendNotification);
    genre.label.setText(TRANS("stream_genre"), juce::dontSendNotification);
    website.label.setText(TRANS("stream_website"), juce::dontSendNotification);
    website.editor.setTextToShowWhenEmpty("https://", t.getSecondaryTextColour());
    description.label.setText(TRANS("stream_description"), juce::dontSendNotification);
    publicToggle.setButtonText(TRANS("stream_public"));
    publicToggle.setTooltip(TRANS("tip_stream_public"));
    metadataDelayLabel.setText(TRANS("stream_metadata_delay"), juce::dontSendNotification);
    metadataDelayLabel.setTooltip(TRANS("tip_stream_metadata_delay"));
    metadataDelay.setTooltip(TRANS("tip_stream_metadata_delay"));
    streamToggle.setButtonText(TRANS("stream_enable"));
    streamToggle.setTooltip(TRANS("tip_stream_enable"));

    nowPlayingHint.setText(TRANS("now_playing_hint"), juce::dontSendNotification);
    pushUrl.label.setText(TRANS("now_playing_push_url"), juce::dontSendNotification);
    pushUrl.editor.setTextToShowWhenEmpty("https://", t.getSecondaryTextColour());
    viewDataButton.setButtonText(TRANS("now_playing_view"));
    viewDataButton.setTooltip(TRANS("tip_now_playing_view"));

    for (auto* hint : { &deadAirHint, &nowPlayingHint }) {
        hint->setFont(UiStyle::ui(12.5f));
        hint->setColour(juce::Label::textColourId, t.getSecondaryTextColour());
    }
    for (auto* label : { &host.label, &port.label, &mount.label, &user.label, &password.label, &name.label,
                         &format.label, &quality.label, &genre.label, &website.label, &description.label, &pushUrl.label }) {
        label->setFont(UiStyle::caption(12.0f));
        label->setColour(juce::Label::textColourId, t.getSecondaryTextColour());
        label->setBorderSize({ 0, 0, 0, 0 });
    }
    for (auto* label : { &deadAirSecondsLabel, &retentionLabel, &metadataDelayLabel }) {
        label->setFont(UiStyle::ui(13.5f));
        label->setColour(juce::Label::textColourId, t.getSecondaryTextColour());
        label->setJustificationType(juce::Justification::centredRight);
    }
    for (auto* label : { &streamStatusLabel, &pushStatusLabel })
        label->setFont(UiStyle::ui(13.0f));
    timerCallback();
}

//==============================================================================
SettingsDialog::ScrollingTab::ScrollingTab(juce::Component& content, int height) : contentHeight(height) {
    setViewedComponent(&content, false);
    setScrollBarsShown(true, false);
    setScrollBarThickness(10);
}

void SettingsDialog::ScrollingTab::resized() {
    const bool scrolls = getHeight() < contentHeight;
    if (auto* content = getViewedComponent())
        content->setSize(getWidth() - (scrolls ? getScrollBarThickness() : 0), contentHeight);
    juce::Viewport::resized();
}

//==============================================================================
SettingsDialog::SettingsDialog(juce::AudioDeviceManager& m, juce::AudioDeviceManager& c, MusicLibrary& lib, AdvertisingManager& ads,
                               StationConfig& station, StreamEncoder& streamEncoder, NowPlayingPublisher* nowPlaying, juce::File airLogFolder)
{
    audioTab = std::make_unique<AudioTab>(m, c);
    libraryTab = std::make_unique<LibraryTab>(lib, ads);
    libraryTab->onLibraryReset = [this] { if (onLibraryReset) onLibraryReset(); };
    broadcastTab = std::make_unique<BroadcastTab>(station, streamEncoder, nowPlaying, airLogFolder);
    broadcastScroller = std::make_unique<ScrollingTab>(*broadcastTab, BroadcastTab::getIdealHeight());
    languageTab = std::make_unique<LanguageTab>();
    updatesTab = std::make_unique<UpdatesTab>();



    tabs.addTab(TRANS("audio_settings"), juce::Colour(0x00000000), audioTab.get(), false);
    tabs.addTab(TRANS("library_settings"), juce::Colour(0x00000000), libraryTab.get(), false);
    tabs.addTab(TRANS("broadcast_settings"), juce::Colour(0x00000000), broadcastScroller.get(), false);
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
