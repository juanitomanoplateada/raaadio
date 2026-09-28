#pragma once
#include <JuceHeader.h>
#include "../library/MusicLibrary.h"
#include "../library/AdvertisingManager.h"  
#include "../helpers/LanguageManager.h" 
#include "../helpers/PersistenceManager.h"
#include "../helpers/StationSettings.h"
#include "../network/StreamEncoder.h"
#include "../network/NowPlayingPublisher.h"
#include "../helpers/BackupService.h"

class SettingsDialog : public juce::Component {
public:
    SettingsDialog(juce::AudioDeviceManager& mainDevice, juce::AudioDeviceManager& cueDevice, MusicLibrary& lib, AdvertisingManager& ads,
                   StationConfig& station, StreamEncoder& streamEncoder, NowPlayingPublisher* nowPlaying,
                   BackupService* backups, juce::File airLogFolder);
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
    
     
    // Biblioteca y datos: carpeta de la emisora, respaldos (diario, destino externo, copia
    // de la música), migración y borrado. Es más alta que el diálogo: va en un Viewport.
    class LibraryTab : public juce::Component, private juce::ChangeListener, private juce::Timer {
    public:
        LibraryTab(MusicLibrary& lib, AdvertisingManager& ads, StationConfig& station, BackupService* backups);
        ~LibraryTab() override;
        void resized() override;
        void lookAndFeelChanged() override;
        static int getIdealHeight();

        std::function<void()> onLibraryReset;

    private:
        void changeListenerCallback(juce::ChangeBroadcaster*) override { updateStatus(); }
        void timerCallback() override { updateStatus(); }

        MusicLibrary& library;
        AdvertisingManager& adsManager;
        StationConfig& station;
        BackupService* backupService;

        // Visual Sections
        juce::GroupComponent labGroup;
        juce::GroupComponent dataGroup;
        juce::GroupComponent migrationGroup;
        juce::GroupComponent dangerGroup;

        // Lab Root management
        juce::Label labPathLabel;
        juce::Label labNextLabel;  // la carpeta que regirá desde el próximo inicio, si cambió
        juce::TextButton changeLabButton { "CHANGE LAB ROOT" };

        // Respaldos
        juce::ToggleButton autoBackupToggle;
        juce::ComboBox backupHour;
        juce::Label externalCaption;
        juce::Label externalPathLabel;
        juce::TextButton chooseExternalButton, clearExternalButton;
        juce::ToggleButton mirrorToggle;
        juce::TextButton backupButton { "BACKUP" };
        juce::TextButton restoreButton { "RESTORE" };
        juce::Label dataStatusLabel;      // último respaldo local, o la restauración pendiente
        juce::Label externalStatusLabel;  // destino externo y música
        juce::Label diskStatusLabel;      // espacio libre en el disco de la emisora
        juce::Label retentionLabel;

        // Migration
        juce::TextButton exportButton { "EXPORT" };
        juce::TextButton importButton { "IMPORT" };

        // Danger Zone
        juce::TextButton resetButton { "RESET DATABASE" };
        juce::Label warningLabel;

        void updateTexts();
        void updateStatus();
        void applyBackupSettings();
        void chooseExternalFolder();
        void setExternalFolder(const juce::File& folder);
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

    // Emisión: protección contra silencio, registro de lo emitido, transmisión por internet
    // y publicación de lo que suena. Es más alta que el diálogo: va dentro de un Viewport.
    class BroadcastTab : public juce::Component, private juce::Timer {
    public:
        BroadcastTab(StationConfig& station, StreamEncoder& streamEncoder, NowPlayingPublisher* nowPlaying, juce::File airLogFolder);
        void resized() override;
        void lookAndFeelChanged() override;
        static int getIdealHeight();
    private:
        void timerCallback() override;  // estado de la transmisión y del último envío
        void apply();                   // guarda lo que dicen los controles
        void fillQualityOptions();      // según el formato: kbps de MP3 o calidades de Ogg

        StationConfig& station;
        StreamEncoder& streamEncoder;
        NowPlayingPublisher* nowPlaying;
        juce::File airLogFolder;

        juce::GroupComponent deadAirGroup, airLogGroup, streamGroup, nowPlayingGroup;

        juce::ToggleButton deadAirToggle;
        juce::Label deadAirSecondsLabel;
        juce::ComboBox deadAirSeconds;
        juce::Label deadAirHint;

        juce::ToggleButton airLogToggle;
        juce::Label retentionLabel;
        juce::ComboBox retention;
        juce::TextButton openLogFolderButton;

        struct Field { juce::Label label; juce::TextEditor editor; };
        struct Choice { juce::Label label; juce::ComboBox combo; };
        Field host, port, mount, user, password, name, genre, website, description;
        Choice format, quality;
        juce::ToggleButton publicToggle;
        juce::Label metadataDelayLabel;
        juce::ComboBox metadataDelay;
        juce::ToggleButton streamToggle;
        juce::Label streamStatusLabel;

        juce::Label nowPlayingHint;
        Field pushUrl;
        juce::TextButton viewDataButton;
        juce::Label pushStatusLabel;
    };

    // Una pestaña más alta que el diálogo, con barra de desplazamiento.
    class ScrollingTab : public juce::Viewport {
    public:
        ScrollingTab(juce::Component& content, int contentHeight);
        void resized() override;
    private:
        int contentHeight;
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
    std::unique_ptr<ScrollingTab> libraryScroller;
    std::unique_ptr<BroadcastTab> broadcastTab;
    std::unique_ptr<ScrollingTab> broadcastScroller;
    std::unique_ptr<LanguageTab> languageTab;
    std::unique_ptr<UpdatesTab> updatesTab;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsDialog)
};
