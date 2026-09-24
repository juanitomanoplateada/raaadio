#pragma once
#include <JuceHeader.h>
#include "../library/MusicLibrary.h"
#include "../library/AdvertisingManager.h"  
#include "../helpers/LanguageManager.h" 
#include "../helpers/PersistenceManager.h"
#include "../helpers/StationSettings.h"
#include "../network/StreamEncoder.h"

class SettingsDialog : public juce::Component {
public:
    SettingsDialog(juce::AudioDeviceManager& mainDevice, juce::AudioDeviceManager& cueDevice, MusicLibrary& lib, AdvertisingManager& ads,
                   StationConfig& station, StreamEncoder& streamEncoder, juce::File airLogFolder);
    ~SettingsDialog();
    
    void resized() override;
    void paint(juce::Graphics& g) override;

     
    void lookAndFeelChanged() override;

    // Se llama después de borrar la biblioteca (vaciar la cola, la botonera...).
    std::function<void()> onLibraryReset;

private:
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    
     
    class AudioTab : public juce::Component {
    public:
        AudioTab(juce::AudioDeviceManager& m, juce::AudioDeviceManager& c);
        void resized() override;
        void lookAndFeelChanged() override;  
    private:
        juce::AudioDeviceManager& mainMgr;
        juce::AudioDeviceManager& cueMgr;
        juce::GroupComponent mainGroup;
        std::unique_ptr<juce::AudioDeviceSelectorComponent> mainSelector;
        juce::GroupComponent cueGroup;
        std::unique_ptr<juce::AudioDeviceSelectorComponent> cueSelector;
    };
    
     
    class LibraryTab : public juce::Component {
    public:
        LibraryTab(MusicLibrary& lib, AdvertisingManager& ads);
        void resized() override;
        void lookAndFeelChanged() override;

        std::function<void()> onLibraryReset;

    private:
        MusicLibrary& library;
        AdvertisingManager& adsManager;

        // Visual Sections
        juce::GroupComponent labGroup;
        juce::GroupComponent dataGroup;
        juce::GroupComponent migrationGroup;
        juce::GroupComponent dangerGroup;

        // Lab Root management
        juce::Label labPathLabel;
        juce::Label labNextLabel;  // la carpeta que regirá desde el próximo inicio, si cambió
        juce::TextButton changeLabButton { "CHANGE LAB ROOT" };

        // Data Management
        juce::ToggleButton autoBackupToggle { "DAILY AUTO-BACKUP" };
        juce::TextButton backupButton { "BACKUP" };
        juce::TextButton restoreButton { "RESTORE" };
        juce::Label dataStatusLabel;  // último respaldo, o la restauración pendiente

        // Migration
        juce::TextButton exportButton { "EXPORT" };
        juce::TextButton importButton { "IMPORT" };

        // Danger Zone
        juce::TextButton resetButton { "RESET DATABASE" };
        juce::Label warningLabel;

        void updateTexts();
        PersistenceManager::DatabaseSnapshot databaseSnapshot();
        void performBackup();
        void performRestore();
        void performImport();
        void performExport();
        void changeLabRoot();
        void copyLabTo(const juce::File& target);
        void resetLibrary();

        std::unique_ptr<juce::FileChooser> fileChooser;
    };

    // Emisión: protección contra silencio, registro de lo emitido y transmisión Icecast.
    class BroadcastTab : public juce::Component, private juce::Timer {
    public:
        BroadcastTab(StationConfig& station, StreamEncoder& streamEncoder, juce::File airLogFolder);
        void resized() override;
        void lookAndFeelChanged() override;
    private:
        void timerCallback() override;  // estado de la transmisión
        void apply();                   // guarda lo que dicen los controles

        StationConfig& station;
        StreamEncoder& streamEncoder;
        juce::File airLogFolder;

        juce::GroupComponent deadAirGroup, airLogGroup, streamGroup;

        juce::ToggleButton deadAirToggle;
        juce::Label deadAirSecondsLabel;
        juce::ComboBox deadAirSeconds;
        juce::Label deadAirHint;

        juce::ToggleButton airLogToggle;
        juce::Label retentionLabel;
        juce::ComboBox retention;
        juce::TextButton openLogFolderButton;

        struct Field { juce::Label label; juce::TextEditor editor; };
        Field host, port, mount, user, password, name;
        juce::Label qualityLabel;
        juce::ComboBox quality;
        juce::ToggleButton streamToggle;
        juce::Label streamStatusLabel;
    };


    class LanguageTab : public juce::Component {
    public:
        LanguageTab();
        void resized() override;
        void lookAndFeelChanged() override;
    private:
        juce::Label languageLabel;
        juce::ComboBox languageSelector;

        juce::Label themeLabel;
        juce::ComboBox themeSelector;
    };
    
     
    class UpdatesTab : public juce::Component {
    public:
        UpdatesTab();
        void resized() override;
        void paint(juce::Graphics& g) override;
        void lookAndFeelChanged() override;
    private:
        juce::ImageComponent bannerImage;
        juce::Label appInfoLabel;
    };
    
    std::unique_ptr<AudioTab> audioTab;
    std::unique_ptr<LibraryTab> libraryTab;
    std::unique_ptr<BroadcastTab> broadcastTab;
    std::unique_ptr<LanguageTab> languageTab;
    std::unique_ptr<UpdatesTab> updatesTab;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsDialog)
};
