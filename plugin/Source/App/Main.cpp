// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#include "MainComponent.h"

class KeyDetectorApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override { window = std::make_unique<MainWindow> (getApplicationName()); }
    void shutdown() override { window = nullptr; }
    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        // files dropped on the Dock icon while running
        if (window != nullptr && commandLine.isNotEmpty())
            window->content->addPaths (juce::StringArray::fromTokens (commandLine, true));
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, theme::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            content = new MainComponent();
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (480, 320, 4000, 4000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
        MainComponent* content = nullptr;
    };

private:
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (KeyDetectorApplication)
