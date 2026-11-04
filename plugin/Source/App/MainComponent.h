// Key Detector app: drop audio files or folders, get key / Camelot / confidence per file.
//
// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#pragma once

#include "../Engine/AudioPrep.h"
#include "../Engine/KeyEngine.h"
#include "../Ui/Theme.h"

class MainComponent : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::TableListBoxModel
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { setDragOver (true); }
    void fileDragExit (const juce::StringArray&) override { setDragOver (false); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    void addPaths (const juce::StringArray& paths);

private:
    struct Row
    {
        juce::File file;
        bool done = false;
        juce::String error;
        kd::KeyResult result;
    };

    int getNumRows() override { return rows.size(); }
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int w, int h, bool selected) override;
    juce::String getCellTooltip (int row, int column) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;

    void setDragOver (bool);
    void chooseFiles();
    void updateStatus();

    theme::LookAndFeel lookAndFeel;
    juce::TextButton chooseButton { juce::CharPointer_UTF8 ("Choose files\xe2\x80\xa6") };
    juce::TextButton clearButton { "Clear" };
    juce::TableListBox table { "results", this };
    juce::Label status;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<juce::FileChooser> chooser;

    juce::OwnedArray<Row> rows;
    juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("Key analysis").withNumberOfThreads (juce::jlimit (1, 4, juce::SystemStats::getNumCpus() - 1)) };
    juce::uint32 batch = 0;   // jobs of a cleared batch drop their results
    bool dragOver = false;
    juce::Rectangle<int> dropZone;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
