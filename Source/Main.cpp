#include <JuceHeader.h>
#include "MainComponent.h"
#include "helpers/LanguageManager.h"
#include "helpers/ThemeManager.h"
#include "helpers/PersistenceManager.h"
#include "helpers/RaaadioLookAndFeel.h"
#include "helpers/UiSnapshot.h"
#include "helpers/StationLock.h"

class RaaadioApplication  : public juce::JUCEApplication
{
public:
    RaaadioApplication() {}

    const juce::String getApplicationName() override       { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }

    // Dos instancias se disputarían el dispositivo de audio, la base de datos y el puerto de la API.
    // El modo de captura de UI es la excepción: no abre audio ni toca los datos de la emisora.
    bool moreThanOneInstanceAllowed() override
    {
        return getCommandLineParameters().contains("--ui-snapshot");
    }

    void initialise (const juce::String& commandLine) override
    {
        UiSnapshotRunner::Options snapshotOptions;
        const bool snapshotMode = UiSnapshotRunner::parseCommandLine(getCommandLineParameterArray(), snapshotOptions);
        if (snapshotMode)
            UiSnapshotRunner::useIsolatedLab();

        // El log vive en la carpeta del laboratorio y se recorta al arrancar si supera 5 MB.
        auto logsFolder = PersistenceManager::getLogsFolder();
        logsFolder.createDirectory();
        fileLogger = std::make_unique<juce::FileLogger>(logsFolder.getChildFile("raaadio.log"),
                                                        "RAAADIO " + getApplicationVersion(),
                                                        5 * 1024 * 1024);
        juce::Logger::setCurrentLogger(fileLogger.get());

        juce::Logger::writeToLog("Step 1: Application Initialise called");

        juce::Logger::writeToLog("Step 1.1: Initialising LanguageManager");
        LanguageManager::getInstance().initialise();

        juce::Logger::writeToLog("Step 1.2: Initialising ThemeManager");
        ThemeManager::getInstance().initialise();

        if (snapshotMode)
        {
            if (snapshotOptions.language.isNotEmpty())
                LanguageManager::getInstance().setLanguage(snapshotOptions.language, false);
            if (snapshotOptions.theme.isNotEmpty())
                ThemeManager::getInstance().setTheme(snapshotOptions.theme == "light" ? ThemeManager::Theme::Light
                                                                                      : ThemeManager::Theme::Dark, false);
        }

        lookAndFeel = std::make_unique<RaaadioLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel.get());

        if (snapshotMode)
        {
            snapshotRunner = std::make_unique<UiSnapshotRunner>(snapshotOptions);
            snapshotRunner->onFinished = [this](bool ok) {
                setApplicationReturnValue(ok ? 0 : 1);
                quit();
            };
            return;
        }

        // Otro equipo con esta emisora abierta (un USB o un disco de red): abrirla aquí al
        // mismo tiempo dañaría la base de datos. Se pregunta antes de abrir nada.
        stationLock = std::make_unique<StationLock>(PersistenceManager::getLabRoot());
        if (const auto other = stationLock->otherOwner())
        {
            juce::Logger::writeToLog("Station is open on " + other->computer + " (" + other->user + ")");
            // Enter y Escape cierran: abrir igual tiene que ser una decisión explícita.
            auto* dialog = new juce::AlertWindow(TRANS("lock_title"),
                TRANS("lock_message").replace("%1", other->computer).replace("%2", other->user),
                juce::AlertWindow::WarningIcon);
            dialog->addButton(TRANS("lock_quit"), 0, juce::KeyPress(juce::KeyPress::returnKey), juce::KeyPress(juce::KeyPress::escapeKey));
            dialog->addButton(TRANS("lock_open_anyway"), 1);
            dialog->enterModalState(true, juce::ModalCallbackFunction::create([this](int result) {
                if (result == 1) openStation();
                else quit();
            }), true);
            return;
        }
        openStation();
    }

    void openStation()
    {
        stationLock->acquire();

        try {
            juce::Logger::writeToLog("Step 2: Creating MainWindow");
            mainWindow.reset (new MainWindow (getApplicationName()));
            juce::Logger::writeToLog("Step 3: MainWindow created");
        } catch (const std::exception& e) {
             juce::Logger::writeToLog("FATAL ERROR: " + juce::String(e.what()));
             quit();
        } catch (...) {
             juce::Logger::writeToLog("FATAL ERROR: Unknown exception");
             quit();
        }
    }

    void shutdown() override
    {
        snapshotRunner = nullptr;
        mainWindow = nullptr;
        stationLock = nullptr;  // la emisora queda libre para otro equipo
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        lookAndFeel.reset();
        juce::Logger::setCurrentLogger(nullptr);
        fileLogger.reset();
    }

    void systemRequestedQuit() override
    {
        auto* main = mainWindow != nullptr ? dynamic_cast<MainComponent*>(mainWindow->getContentComponent()) : nullptr;

        if (main == nullptr || !main->isOnAir())
        {
            quit();
            return;
        }

        if (quitConfirmationOpen)
            return;

        quitConfirmationOpen = true;
        juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon,
            TRANS("quit_confirm_title"), TRANS("quit_confirm_msg"),
            TRANS("quit_confirm_btn"), TRANS("cancel"), nullptr,
            juce::ModalCallbackFunction::create([this](int result) {
                quitConfirmationOpen = false;
                if (result == 1)
                    quit();
            }));
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (mainWindow != nullptr)
            mainWindow->toFront(true);
    }

    class MainWindow    : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name)  : DocumentWindow (name,
                                                          juce::Desktop::getInstance().getDefaultLookAndFeel()
                                                          .findColour (juce::ResizableWindow::backgroundColourId),
                                                          DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);

            #if JUCE_IOS || JUCE_ANDROID
             setFullScreen (false);
            #else
             setResizable (true, true);
             setResizeLimits (1180, 680, 10000, 10000);
             setFullScreen(true);
            #endif

            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<juce::FileLogger> fileLogger;
    std::unique_ptr<RaaadioLookAndFeel> lookAndFeel;
    std::unique_ptr<UiSnapshotRunner> snapshotRunner;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<StationLock> stationLock;
    bool quitConfirmationOpen = false;
};

START_JUCE_APPLICATION (RaaadioApplication)
