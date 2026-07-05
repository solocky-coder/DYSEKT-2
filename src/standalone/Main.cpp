//==============================================================================
//  DYSEKT-SF Standalone — Main.cpp
//
//  JUCE application entry point.  Only compiled into the DYSEKT-SF_Standalone
//  target (guarded by DYSEKT_STANDALONE define in CMakeLists.txt).
//
//  Responsibilities:
//    - Create the AudioDeviceManager
//    - Instantiate DysektProcessor and DysektEditor
//    - Create and show MainWindow
//    - Handle system quit / re-open on macOS
//==============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "MainWindow.h"

//==============================================================================
class DysektApplication : public juce::JUCEApplication
{
public:
    DysektApplication() = default;

    const juce::String getApplicationName()    override { return "DYSEKT-SF"; }
    const juce::String getApplicationVersion() override { return "1.0.0";  }
    bool moreThanOneInstanceAllowed()          override { return true;      }

    //==========================================================================
    void initialise (const juce::String& /*commandLine*/) override
    {
        // Apply OS-level DPI awareness before creating any windows
        juce::Desktop::getInstance().setGlobalScaleFactor (1.0f);

        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    //==========================================================================
    void systemRequestedQuit() override
    {
        // Could add "save before quit?" dialog here
        quit();
    }

    void anotherInstanceStarted (const juce::String& /*commandLine*/) override
    {
        // Bring window to front if user double-clicks the app again
        if (mainWindow != nullptr)
        {
            mainWindow->toFront (true);
            mainWindow->grabKeyboardFocus();
        }
    }

    //==========================================================================
    //  macOS: re-open when user clicks the dock icon with no windows open
    void resumed() override
    {
        if (mainWindow == nullptr)
            mainWindow = std::make_unique<MainWindow> (getApplicationName());
        mainWindow->setVisible (true);
        mainWindow->toFront (true);
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION (DysektApplication)
