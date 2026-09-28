#include "AudioSettingsDialog.h"
#include "BinaryData.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"
#include "../network/Mp3Encoder.h"
#include "../helpers/StationHealth.h"

 
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

SettingsDialog::LibraryTab::LibraryTab(MusicLibrary& lib, AdvertisingManager& ads, StationConfig& st, BackupService* backups)
    : library(lib), adsManager(ads), station(st), backupService(backups)
{
    addAndMakeVisible(labGroup);
    addAndMakeVisible(dataGroup);
    addAndMakeVisible(migrationGroup);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &onlineGroup, &onlineToggle, &onlineKeyCaption,
                                                                       &onlineKeyEditor, &onlineStatusLabel, &onlineNoteLabel })
        addAndMakeVisible(c);
    onlineKeyCaption.setFont(UiStyle::caption(12.0f));
    onlineKeyCaption.setBorderSize({ 0, 0, 0, 0 });
    for (auto* label : { &onlineStatusLabel, &onlineNoteLabel }) {
        label->setFont(UiStyle::ui(12.5f));
        label->setJustificationType(juce::Justification::topLeft);
        label->setMinimumHorizontalScale(0.8f);
    }
    onlineKeyEditor.setFont(UiStyle::mono(13.0f));
    onlineKeyEditor.setIndents(10, 0);
    onlineKeyEditor.setJustification(juce::Justification::centredLeft);
    onlineToggle.setEnabled(false);
    onlineKeyEditor.setEnabled(false);
    onlineToggle.onClick = [this] { if (onlineIdentifier != nullptr) onlineIdentifier->setEnabled(onlineToggle.getToggleState()); updateOnlineStatus(); };
    auto saveKey = [this] {
        if (onlineIdentifier != nullptr && onlineKeyEditor.getText().trim() != onlineIdentifier->getApiKey())
            onlineIdentifier->setApiKey(onlineKeyEditor.getText());
        updateOnlineStatus();
    };
    onlineKeyEditor.onReturnKey = saveKey;
    onlineKeyEditor.onFocusLost = saveKey;
    addAndMakeVisible(dangerGroup);

    for (auto* label : { &labPathLabel, &externalPathLabel }) {
        addAndMakeVisible(*label);
        label->setJustificationType(juce::Justification::centredLeft);
        label->setFont(UiStyle::mono(13.0f));
        label->setMinimumHorizontalScale(0.6f);
    }

    for (auto* label : { &labNextLabel, &dataStatusLabel, &externalStatusLabel, &diskStatusLabel, &retentionLabel, &warningLabel }) {
        addAndMakeVisible(*label);
        label->setFont(UiStyle::ui(12.5f));
        label->setMinimumHorizontalScale(0.7f);
        label->setJustificationType(juce::Justification::centredLeft);
    }
    warningLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(externalCaption);
    externalCaption.setFont(UiStyle::caption(12.0f));
    externalCaption.setBorderSize({ 0, 0, 0, 0 });

    for (auto* button : { &changeLabButton, &chooseExternalButton, &clearExternalButton, &backupButton, &restoreButton,
                          &exportButton, &importButton, &resetButton })
        addAndMakeVisible(*button);
    addAndMakeVisible(autoBackupToggle);
    addAndMakeVisible(backupHour);
    addAndMakeVisible(mirrorToggle);
    addAndMakeVisible(uppercaseToggle);
    uppercaseToggle.setToggleState(library.isDisplayUppercase(), juce::dontSendNotification);
    uppercaseToggle.onClick = [this] {
        library.setDisplayUppercase(uppercaseToggle.getToggleState());
        // Todo lo que muestra canciones se vuelve a dibujar (la transmisión cambia con la próxima).
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = desktop.getComponent(i)) window->repaint();
    };

    UiStyle::setVariant(resetButton, "danger");
    UiStyle::setVariant(clearExternalButton, "ghost");
    UiStyle::setIcon(changeLabButton, "folder");
    UiStyle::setIcon(chooseExternalButton, "folder");

    const auto& s = station.get();
    autoBackupToggle.setToggleState(s.backupEnabled, juce::dontSendNotification);
    for (int hour = 0; hour < 24; ++hour)
        backupHour.addItem(juce::String(hour).paddedLeft('0', 2) + ":00", hour + 1);
    backupHour.setSelectedId(s.backupHour + 1, juce::dontSendNotification);
    mirrorToggle.setToggleState(s.backupMirrorLibrary, juce::dontSendNotification);

    autoBackupToggle.onClick = [this] { applyBackupSettings(); };
    backupHour.onChange = [this] { applyBackupSettings(); };
    mirrorToggle.onClick = [this] { applyBackupSettings(); };
    chooseExternalButton.onClick = [this] { chooseExternalFolder(); };
    clearExternalButton.onClick = [this] { setExternalFolder({}); };

    changeLabButton.onClick = [this] { changeLabRoot(); };
    backupButton.onClick = [this] { performBackup(); };
    restoreButton.onClick = [this] { performRestore(); };
    exportButton.onClick = [this] { performExport(); };
    importButton.onClick = [this] { performImport(); };
    resetButton.onClick = [this] { resetLibrary(); };

    if (backupService != nullptr) backupService->addChangeListener(this);
    lookAndFeelChanged();
    startTimer(2000);  // el espacio libre y un respaldo en curso
}

SettingsDialog::LibraryTab::~LibraryTab() {
    if (backupService != nullptr) backupService->removeChangeListener(this);
}

PersistenceManager::DatabaseSnapshot SettingsDialog::LibraryTab::databaseSnapshot() {
    return [&lib = library](const juce::File& target) { return lib.backupDatabaseTo(target); };
}

namespace {
    // Medidas de la pestaña, compartidas por resized() y getIdealHeight().
    constexpr int libMargin = 15, libGap = 15, labHeight = 164, migrationHeight = 90, dangerHeight = 120;
    constexpr int onlineHeight = 30 + 30 + 6 + 18 + 4 + 30 + 6 + 20 + 36 + 14;
    constexpr int backupHeight = 30 + 35 + 8 + 18 + 4 + 30 + 8 + 30 + 8 + 35 + 8 + 4 * 20 + 20;
}

int SettingsDialog::LibraryTab::getIdealHeight() {
    return libMargin * 2 + labHeight + libGap + backupHeight + libGap + onlineHeight + libGap + migrationHeight + libGap + dangerHeight;
}

void SettingsDialog::LibraryTab::resized() {
    auto area = getLocalBounds().reduced(libMargin);
    const int groupPadding = 20;
    const int btnH = 35;
    const int gap = 10;

    // 0. Carpeta de la emisora
    auto labArea = area.removeFromTop(labHeight);
    labGroup.setBounds(labArea);
    auto labContent = labArea.reduced(groupPadding);
    labContent.removeFromTop(10);

    labPathLabel.setBounds(labContent.removeFromTop(25));
    labNextLabel.setBounds(labContent.removeFromTop(18));
    labContent.removeFromTop(3);
    changeLabButton.setBounds(labContent.removeFromTop(30).reduced(50, 0));
    labContent.removeFromTop(8);
    uppercaseToggle.setBounds(labContent.removeFromTop(30));

    area.removeFromTop(libGap);

    // 1. Respaldos
    auto dataArea = area.removeFromTop(backupHeight);
    dataGroup.setBounds(dataArea);
    auto data = dataArea.withTrimmedTop(30).reduced(groupPadding, 0);

    auto row = data.removeFromTop(btnH);
    backupHour.setBounds(row.removeFromRight(100).withSizeKeepingCentre(100, 30));
    row.removeFromRight(8);
    autoBackupToggle.setBounds(row);
    data.removeFromTop(8);

    externalCaption.setBounds(data.removeFromTop(18));
    data.removeFromTop(4);
    row = data.removeFromTop(30);
    clearExternalButton.setBounds(row.removeFromRight(juce::jmax(80, clearExternalButton.getBestWidthForHeight(30) + 12)));
    row.removeFromRight(6);
    chooseExternalButton.setBounds(row.removeFromRight(juce::jmax(110, chooseExternalButton.getBestWidthForHeight(30) + 16)));
    row.removeFromRight(8);
    externalPathLabel.setBounds(row);
    data.removeFromTop(8);

    mirrorToggle.setBounds(data.removeFromTop(30));
    data.removeFromTop(8);

    row = data.removeFromTop(btnH);
    backupButton.setBounds(row.removeFromLeft(row.getWidth() / 2 - gap / 2));
    restoreButton.setBounds(row.removeFromRight(row.getWidth() - gap));
    data.removeFromTop(8);

    for (auto* label : { &dataStatusLabel, &externalStatusLabel, &diskStatusLabel, &retentionLabel })
        label->setBounds(data.removeFromTop(20));

    area.removeFromTop(libGap);

    // Identificar canciones en línea
    auto onlineArea = area.removeFromTop(onlineHeight);
    onlineGroup.setBounds(onlineArea);
    auto online = onlineArea.withTrimmedTop(30).reduced(groupPadding, 0);
    onlineToggle.setBounds(online.removeFromTop(30));
    online.removeFromTop(6);
    onlineKeyCaption.setBounds(online.removeFromTop(18));
    online.removeFromTop(4);
    onlineKeyEditor.setBounds(online.removeFromTop(30));
    online.removeFromTop(6);
    onlineStatusLabel.setBounds(online.removeFromTop(20));
    onlineNoteLabel.setBounds(online.removeFromTop(36));

    area.removeFromTop(libGap);

    // 2. Migración
    auto migArea = area.removeFromTop(migrationHeight);
    migrationGroup.setBounds(migArea);
    auto migContent = migArea.reduced(groupPadding);
    migContent.removeFromTop(10);

    auto r2 = migContent.removeFromTop(btnH);
    exportButton.setBounds(r2.removeFromLeft(migContent.getWidth() / 2 - gap/2));
    importButton.setBounds(r2.removeFromRight(migContent.getWidth() / 2 - gap/2));

    area.removeFromTop(libGap);

    // 3. Zona de peligro
    auto dangerArea = area.removeFromTop(dangerHeight);
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
    for (auto* label : { &labPathLabel, &externalPathLabel }) {
        label->setColour(juce::Label::backgroundColourId, t.getRaisedColour());
        label->setColour(juce::Label::outlineColourId, t.getStrongOutlineColour());
        label->setColour(juce::Label::textColourId, t.getSecondaryTextColour());
    }
    externalCaption.setColour(juce::Label::textColourId, t.getSecondaryTextColour());
    onlineKeyCaption.setColour(juce::Label::textColourId, t.getSecondaryTextColour());
    onlineNoteLabel.setColour(juce::Label::textColourId, t.getMutedTextColour());
    retentionLabel.setColour(juce::Label::textColourId, t.getMutedTextColour());
    updateTexts();
}

void SettingsDialog::LibraryTab::updateTexts() {
    labGroup.setText(TRANS("group_lab_root"));
    dataGroup.setText(TRANS("group_persistence"));
    migrationGroup.setText(TRANS("group_migration"));
    onlineGroup.setText(TRANS("group_online"));
    onlineToggle.setButtonText(TRANS("online_toggle"));
    onlineKeyCaption.setText(TRANS("online_key_caption"), juce::dontSendNotification);
    onlineKeyEditor.setTextToShowWhenEmpty(TRANS("online_key_placeholder"), ThemeManager::getInstance().getMutedTextColour());
    onlineNoteLabel.setText(TRANS("online_note"), juce::dontSendNotification);
    dangerGroup.setText(TRANS("group_danger_zone"));

    const auto current = PersistenceManager::getLabRoot();
    const auto next = PersistenceManager::getConfiguredLabRoot();
    labPathLabel.setText(current.getFullPathName(), juce::dontSendNotification);
    labNextLabel.setText(next != current ? TRANS("lab_next_start").replace("%1", next.getFullPathName()) : juce::String(),
                         juce::dontSendNotification);
    changeLabButton.setButtonText(TRANS("btn_change_lab_root"));
    uppercaseToggle.setButtonText(TRANS("label_display_uppercase"));
    uppercaseToggle.setTooltip(TRANS("tip_display_uppercase"));

    autoBackupToggle.setButtonText(TRANS("label_auto_backup"));
    externalCaption.setText(TRANS("backup_external_caption"), juce::dontSendNotification);
    chooseExternalButton.setButtonText(TRANS("backup_external_choose"));
    clearExternalButton.setButtonText(TRANS("backup_external_clear"));
    mirrorToggle.setButtonText(TRANS("backup_mirror_music"));
    mirrorToggle.setTooltip(TRANS("tip_backup_mirror_music"));
    retentionLabel.setText(TRANS("backup_retention").replace("%1", juce::String(PersistenceManager::keepDays))
                                                    .replace("%2", juce::String(PersistenceManager::keepWeeks))
                                                    .replace("%3", juce::String(PersistenceManager::keepMonths)),
                           juce::dontSendNotification);

    resetButton.setButtonText(TRANS("db_reset"));
    warningLabel.setText(TRANS("reset_hint"), juce::dontSendNotification);

    backupButton.setButtonText(TRANS("btn_backup"));
    restoreButton.setButtonText(TRANS("btn_restore"));
    exportButton.setButtonText(TRANS("btn_export"));
    importButton.setButtonText(TRANS("btn_import"));
    updateStatus();
}

void SettingsDialog::LibraryTab::setOnlineIdentifier(OnlineIdentifier* identifier) {
    onlineIdentifier = identifier;
    const bool available = identifier != nullptr;
    onlineToggle.setEnabled(available);
    onlineKeyEditor.setEnabled(available);
    if (available) {
        onlineToggle.setToggleState(identifier->isEnabled(), juce::dontSendNotification);
        onlineKeyEditor.setText(identifier->getApiKey(), false);
    }
    updateOnlineStatus();
}

void SettingsDialog::LibraryTab::updateOnlineStatus() {
    auto& t = ThemeManager::getInstance();
    juce::String text;
    auto colour = t.getSecondaryTextColour();
    if (onlineIdentifier == nullptr || !OnlineIdentifier::isChromaprintAvailable()) {
        text = TRANS("online_status_no_chromaprint");
        colour = t.getWarningColour();
    } else if (onlineKeyEditor.getText().trim().isEmpty()) {
        text = TRANS("online_status_no_key");
        colour = t.getWarningColour();
    } else {
        const auto s = onlineIdentifier->getStatus();
        text = TRANS("online_status").replace("%1", juce::String(s.queued + (s.working ? 1 : 0)))
                                     .replace("%2", juce::String(s.found)).replace("%3", juce::String(s.notFound));
        if (s.lastError.isNotEmpty()) {
            text << "  ·  " << TRANS("online_status_error").replace("%1", s.lastError);
            colour = t.getWarningColour();
        }
    }
    onlineStatusLabel.setText(text, juce::dontSendNotification);
    onlineStatusLabel.setColour(juce::Label::textColourId, colour);
}

void SettingsDialog::LibraryTab::updateStatus() {
    updateOnlineStatus();
    auto& t = ThemeManager::getInstance();
    const auto& s = station.get();
    const auto status = backupService != nullptr ? backupService->getStatus() : BackupService::Status();

    // Destino externo elegido
    externalPathLabel.setText(s.backupExternalFolder.isNotEmpty() ? s.backupExternalFolder : TRANS("backup_external_none"),
                              juce::dontSendNotification);
    clearExternalButton.setEnabled(s.backupExternalFolder.isNotEmpty());
    mirrorToggle.setEnabled(s.backupExternalFolder.isNotEmpty());
    backupButton.setEnabled(!status.running);

    // Respaldo local
    if (PersistenceManager::hasPendingRestore()) {
        dataStatusLabel.setText(TRANS("restore_pending_status"), juce::dontSendNotification);
        dataStatusLabel.setColour(juce::Label::textColourId, t.getWarningColour());
    } else if (status.running) {
        dataStatusLabel.setText(status.step, juce::dontSendNotification);
        dataStatusLabel.setColour(juce::Label::textColourId, t.getWarningColour());
    } else if (!status.lastOk) {
        dataStatusLabel.setText(TRANS("health_backup_failed") + ": " + status.lastError.upToFirstOccurrenceOf("\n", false, false),
                                juce::dontSendNotification);
        dataStatusLabel.setColour(juce::Label::textColourId, t.getErrorColour());
    } else {
        const auto latest = PersistenceManager::getLatestBackupTime();
        dataStatusLabel.setText(latest.toMilliseconds() == 0 ? TRANS("backup_status_none")
                                                             : TRANS("backup_status").replace("%1", latest.formatted("%d/%m/%Y %H:%M")),
                                juce::dontSendNotification);
        dataStatusLabel.setColour(juce::Label::textColourId, t.getMutedTextColour());
    }

    // Destino externo y música
    juce::String external;
    auto externalColour = t.getMutedTextColour();
    if (s.backupExternalFolder.isEmpty()) {
        external = TRANS("backup_external_off");
        externalColour = t.getWarningColour();
    } else if (!status.externalAttempted) {
        external = TRANS("backup_external_pending");
    } else if (!status.externalOk) {
        external = TRANS("health_external_failed") + ": " + status.externalError.upToFirstOccurrenceOf("\n", false, false);
        externalColour = t.getErrorColour();
    } else {
        external = TRANS("backup_external_ok").replace("%1", status.lastExternalOk.formatted("%d/%m/%Y %H:%M"));
        if (s.backupMirrorLibrary)
            external << "  \xc2\xb7  " << TRANS("backup_mirror_status").replace("%1", juce::String(status.mirror.copied + status.mirror.unchanged))
                                                                        .replace("%2", juce::String(status.mirror.copied));
        externalColour = t.getSuccessColour();
    }
    externalStatusLabel.setText(external, juce::dontSendNotification);
    externalStatusLabel.setColour(juce::Label::textColourId, externalColour);

    // Disco de la emisora
    const auto lab = PersistenceManager::getLabRoot();
    const auto free = lab.getBytesFreeOnVolume();
    const auto total = lab.getVolumeTotalSize();
    const auto level = StationHealth::diskLevel(free, total);
    diskStatusLabel.setText(TRANS("backup_disk_status").replace("%1", juce::File::descriptionOfSizeInBytes(free))
                                                       .replace("%2", juce::File::descriptionOfSizeInBytes(total)),
                            juce::dontSendNotification);
    diskStatusLabel.setColour(juce::Label::textColourId, level == StationHealth::Level::Critical ? t.getErrorColour()
                                                         : level == StationHealth::Level::Warning ? t.getWarningColour()
                                                                                                  : t.getMutedTextColour());
}

void SettingsDialog::LibraryTab::applyBackupSettings() {
    auto s = station.get();
    s.backupEnabled = autoBackupToggle.getToggleState();
    if (backupHour.getSelectedId() > 0) s.backupHour = backupHour.getSelectedId() - 1;
    s.backupMirrorLibrary = mirrorToggle.getToggleState();
    station.set(s);
    updateStatus();
}

void SettingsDialog::LibraryTab::setExternalFolder(const juce::File& folder) {
    auto s = station.get();
    s.backupExternalFolder = folder == juce::File() ? juce::String() : folder.getFullPathName();
    station.set(s);
    updateStatus();
    if (folder != juce::File() && backupService != nullptr)
        backupService->runNow();  // la primera copia, enseguida
}

void SettingsDialog::LibraryTab::chooseExternalFolder() {
    fileChooser = std::make_unique<juce::FileChooser>(TRANS("backup_external_choose"),
                                                      juce::File::getSpecialLocation(juce::File::userHomeDirectory));
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& fc) {
            const auto folder = fc.getResult();
            if (!folder.isDirectory()) return;

            const auto lab = PersistenceManager::getLabRoot();
            if (folder == lab || folder.isAChildOf(lab) || lab.isAChildOf(folder)) {
                showError(TRANS("backup_err_external_inside"));
                return;
            }

            // En el mismo disco no protege si el disco falla: se avisa, pero se permite.
            const bool sameDisk = folder.getVolumeSerialNumber() != 0
                               && folder.getVolumeSerialNumber() == lab.getVolumeSerialNumber();
            if (!sameDisk) {
                setExternalFolder(folder);
                return;
            }
            juce::Component::SafePointer<LibraryTab> safeThis(this);
            juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon, TRANS("backup_title"),
                TRANS("backup_external_same_disk"), TRANS("backup_external_use_anyway"), TRANS("cancel"), nullptr,
                juce::ModalCallbackFunction::create([safeThis, folder](int result) {
                    if (result == 1 && safeThis != nullptr) safeThis->setExternalFolder(folder);
                }));
        });
}

void SettingsDialog::LibraryTab::performBackup()
{
    // En segundo plano: la base, el destino externo y la música. El avance queda a la vista.
    if (backupService != nullptr) {
        backupService->runNow();
        updateStatus();
        return;
    }

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
                               StationConfig& station, StreamEncoder& streamEncoder, NowPlayingPublisher* nowPlaying,
                               BackupService* backups, juce::File airLogFolder)
{
    audioTab = std::make_unique<AudioTab>(m, c);
    libraryTab = std::make_unique<LibraryTab>(lib, ads, station, backups);
    libraryScroller = std::make_unique<ScrollingTab>(*libraryTab, LibraryTab::getIdealHeight());
    libraryTab->onLibraryReset = [this] { if (onLibraryReset) onLibraryReset(); };
    broadcastTab = std::make_unique<BroadcastTab>(station, streamEncoder, nowPlaying, airLogFolder);
    broadcastScroller = std::make_unique<ScrollingTab>(*broadcastTab, BroadcastTab::getIdealHeight());
    languageTab = std::make_unique<LanguageTab>();
    updatesTab = std::make_unique<UpdatesTab>();



    tabs.addTab(TRANS("audio_settings"), juce::Colour(0x00000000), audioTab.get(), false);
    tabs.addTab(TRANS("library_settings"), juce::Colour(0x00000000), libraryScroller.get(), false);
    tabs.addTab(TRANS("broadcast_settings"), juce::Colour(0x00000000), broadcastScroller.get(), false);
    tabs.addTab(TRANS("language"), juce::Colour(0x00000000), languageTab.get(), false);
    tabs.addTab(TRANS("tab_updates"), juce::Colour(0x00000000), updatesTab.get(), false);
    tabs.setTabBarDepth(42);
    tabs.setOutline(0);

    addAndMakeVisible(tabs);
    setSize(640, 660);
}

SettingsDialog::~SettingsDialog() {}

void SettingsDialog::setOnlineIdentifier(OnlineIdentifier* identifier) {
    if (libraryTab != nullptr) libraryTab->setOnlineIdentifier(identifier);
}

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
