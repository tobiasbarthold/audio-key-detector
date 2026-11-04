// Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

#include "MainComponent.h"

namespace
{
enum Columns { colFile = 1, colKey, colCamelot, colConfidence };

// One engine and format manager per worker thread.
struct Worker
{
    juce::AudioFormatManager formats;
    kd::KeyEngine engine;
    Worker() { formats.registerBasicFormats(); }
};

Worker& worker()
{
    thread_local Worker w;
    return w;
}
} // namespace

MainComponent::MainComponent()
{
    setLookAndFeel (&lookAndFeel);

    chooseButton.onClick = [this] { chooseFiles(); };
    addAndMakeVisible (chooseButton);
    clearButton.onClick = [this]
    {
        ++batch;
        pool.removeAllJobs (true, 0);
        rows.clear();
        table.updateContent();
        updateStatus();
        repaint();
    };
    addAndMakeVisible (clearButton);

    auto& header = table.getHeader();
    header.addColumn ("File", colFile, 300, 120, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Key", colKey, 120, 80, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Camelot", colCamelot, 70, 60, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Confidence", colConfidence, 110, 80, -1, juce::TableHeaderComponent::notSortable);
    header.setStretchToFitActive (true);
    table.setRowHeight (28);
    table.setHeaderHeight (26);
    table.setMultipleSelectionEnabled (true);
    table.setOutlineThickness (0);
    addAndMakeVisible (table);

    status.setColour (juce::Label::textColourId, theme::textFaint);
    status.setFont (theme::font (12.0f));
    addAndMakeVisible (status);
    updateStatus();

    setSize (680, 500);
}

MainComponent::~MainComponent()
{
    ++batch;
    pool.removeAllJobs (true, 10000);
    table.setModel (nullptr);
    setLookAndFeel (nullptr);
}

void MainComponent::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (background);

    auto zone = dropZone.toFloat();
    g.setColour (dragOver ? panelHi : panel);
    g.fillRoundedRectangle (zone, 10.0f);
    juce::Path border;
    border.addRoundedRectangle (zone.reduced (1.0f), 10.0f);
    juce::Path dashed;
    const float dashes[] = { 6.0f, 5.0f };
    juce::PathStrokeType (1.5f).createDashedStroke (dashed, border, dashes, 2);
    g.setColour (dragOver ? accent : outline.brighter (0.2f));
    g.fillPath (dashed);

    auto label = dropZone.reduced (20, 14).withTrimmedRight (chooseButton.getWidth() + 10);
    g.setColour (dragOver ? accent : theme::text);
    g.setFont (font (17.0f, true));
    g.drawText ("Drop audio files or folders here", label.removeFromTop (label.getHeight() / 2),
                juce::Justification::bottomLeft);
    g.setColour (textDim);
    g.setFont (font (12.5f));
    g.drawText ("wav, aiff, flac, ogg, mp3, m4a  -  folders are scanned recursively", label,
                juce::Justification::topLeft);
}

void MainComponent::paintOverChildren (juce::Graphics& g)
{
    if (rows.isEmpty())
    {
        auto empty = table.getBounds().withTrimmedTop (table.getHeaderHeight());
        g.setColour (theme::textFaint);
        g.setFont (theme::font (13.0f));
        g.drawText ("Results appear here. Double-click a row to copy its key.", empty,
                    juce::Justification::centred);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (16);
    dropZone = area.removeFromTop (84);
    chooseButton.setBounds (dropZone.withTrimmedRight (18).removeFromRight (140).withSizeKeepingCentre (140, 34));
    area.removeFromTop (14);
    auto footer = area.removeFromBottom (30);
    clearButton.setBounds (footer.removeFromRight (80).reduced (0, 2));
    status.setBounds (footer);
    area.removeFromBottom (8);
    table.setBounds (area);
}

void MainComponent::setDragOver (bool over)
{
    dragOver = over;
    repaint();
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    setDragOver (false);
    addPaths (files);
}

void MainComponent::chooseFiles()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose audio files or folders", juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg;*.caf;*.aac");
    const int flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                      | juce::FileBrowserComponent::canSelectDirectories
                      | juce::FileBrowserComponent::canSelectMultipleItems;
    chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        juce::StringArray paths;
        for (auto& f : fc.getResults())
            paths.add (f.getFullPathName());
        addPaths (paths);
    });
}

void MainComponent::addPaths (const juce::StringArray& paths)
{
    auto files = kd::collectAudioFiles (paths);
    const auto currentBatch = batch;
    for (auto& f : files)
    {
        auto* row = rows.add (new Row());
        row->file = f;
        juce::Component::SafePointer<MainComponent> safe (this);
        pool.addJob ([safe, row, f, currentBatch]
        {
            auto& w = worker();
            auto audio = kd::loadAudioFile (f, w.formats);
            kd::KeyResult r;
            if (audio.error.isEmpty())
                r = w.engine.analyze (audio.samples.data(), audio.samples.size());
            juce::MessageManager::callAsync ([safe, row, r, err = audio.error, currentBatch]
            {
                if (safe == nullptr || safe->batch != currentBatch || ! safe->rows.contains (row))
                    return;
                row->result = r;
                row->error = err;
                row->done = true;
                safe->table.repaint();
                safe->updateStatus();
            });
        });
    }
    table.updateContent();
    updateStatus();
    repaint();
}

void MainComponent::updateStatus()
{
    int done = 0;
    for (auto* r : rows)
        done += r->done ? 1 : 0;
    if (rows.isEmpty())
        status.setText ("Essentia KeyExtractor (shaath profile)", juce::dontSendNotification);
    else if (done < rows.size())
        status.setText ("Analysing " + juce::String (done) + " / " + juce::String (rows.size()) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")),
                        juce::dontSendNotification);
    else
        status.setText (juce::String (rows.size()) + (rows.size() == 1 ? " file" : " files"), juce::dontSendNotification);
}

void MainComponent::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    if (selected)
        g.fillAll (theme::panelHi);
    else if (row % 2)
        g.fillAll (theme::panel.withAlpha (0.55f));
}

void MainComponent::paintCell (juce::Graphics& g, int rowIndex, int column, int w, int h, bool)
{
    if (! juce::isPositiveAndBelow (rowIndex, rows.size()))
        return;
    const auto& row = *rows[rowIndex];
    const auto& r = row.result;
    auto bounds = juce::Rectangle<int> (0, 0, w, h).reduced (8, 0);
    g.setFont (theme::font (13.5f));

    switch (column)
    {
        case colFile:
            g.setColour (theme::text);
            g.drawText (row.file.getFileName(), bounds, juce::Justification::centredLeft, true);
            break;
        case colKey:
            if (! row.done)
            {
                g.setColour (theme::textFaint);
                g.drawText (juce::CharPointer_UTF8 ("\xe2\x80\xa6"), bounds, juce::Justification::centredLeft);
            }
            else if (row.error.isNotEmpty())
            {
                g.setColour (juce::Colour (0xffe07a6a));
                g.drawText ("error", bounds, juce::Justification::centredLeft);
            }
            else if (! r.clear)
            {
                g.setColour (theme::textDim);
                g.drawText ("no clear key", bounds, juce::Justification::centredLeft);
            }
            else
            {
                g.setColour (theme::text);
                g.setFont (theme::font (13.5f, true));
                g.drawText (r.key, bounds, juce::Justification::centredLeft);
            }
            break;
        case colCamelot:
            if (row.done && r.clear)
            {
                auto pill = bounds.withSizeKeepingCentre (40, 20).withX (bounds.getX()).toFloat();
                g.setColour (theme::camelotColour (r.camelot));
                g.fillRoundedRectangle (pill, 10.0f);
                g.setColour (theme::background);
                g.setFont (theme::font (12.5f, true));
                g.drawText (r.camelot, pill, juce::Justification::centred);
            }
            break;
        case colConfidence:
            if (row.done && r.clear)
            {
                auto label = bounds.removeFromRight (40);
                g.setColour (theme::textDim);
                g.setFont (theme::font (12.0f));
                g.drawText (juce::String (juce::roundToInt (r.confidence * 100.0f)) + "%", label,
                            juce::Justification::centredRight);
                auto track = bounds.withSizeKeepingCentre (bounds.getWidth(), 6).toFloat();
                g.setColour (theme::panelHi);
                g.fillRoundedRectangle (track, 3.0f);
                g.setColour (theme::accent);
                g.fillRoundedRectangle (track.withWidth (juce::jmax (6.0f, track.getWidth() * r.confidence)), 3.0f);
            }
            break;
        default:
            break;
    }
}

juce::String MainComponent::getCellTooltip (int rowIndex, int)
{
    if (! juce::isPositiveAndBelow (rowIndex, rows.size()))
        return {};
    const auto& row = *rows[rowIndex];
    if (! row.done)
        return row.file.getFullPathName();
    if (row.error.isNotEmpty())
        return row.file.getFullPathName() + "\n" + row.error;
    const auto& r = row.result;
    juce::String t = row.file.getFullPathName();
    if (r.clear)
        t << "\nsecond best: " << r.secondKey << " (" << r.secondCamelot << ")";
    else
        t << "\n" << r.reason << (r.bestGuess.empty() ? "" : "; best guess " + r.bestGuess);
    return t;
}

void MainComponent::cellDoubleClicked (int rowIndex, int, const juce::MouseEvent&)
{
    if (juce::isPositiveAndBelow (rowIndex, rows.size()) && rows[rowIndex]->result.clear)
    {
        const auto& r = rows[rowIndex]->result;
        juce::SystemClipboard::copyTextToClipboard (juce::String (r.key) + " (" + r.camelot + ")");
        status.setText ("Copied: " + juce::String (r.key) + " (" + r.camelot + ")", juce::dontSendNotification);
    }
}
