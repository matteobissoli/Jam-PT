#include "PluginEditor.h"

JamPTAudioProcessorEditor::JamPTAudioProcessorEditor(JamPTAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    titleLabel.setText("Jam-PT", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    openAudioButton.addListener(this);
    addAndMakeVisible(openAudioButton);

    openModelButton.addListener(this);
    addAndMakeVisible(openModelButton);

    audioStatusLabel.setJustificationType(juce::Justification::centredLeft);
    modelStatusLabel.setJustificationType(juce::Justification::centredLeft);
    footerLabel.setJustificationType(juce::Justification::centred);

    footerLabel.setText("Audio playback + Core ML processing scaffold", juce::dontSendNotification);

    addAndMakeVisible(audioStatusLabel);
    addAndMakeVisible(modelStatusLabel);
    addAndMakeVisible(footerLabel);

    refreshLabels();
    setSize(560, 220);
}

JamPTAudioProcessorEditor::~JamPTAudioProcessorEditor()
{
    openAudioButton.removeListener(this);
    openModelButton.removeListener(this);
}

void JamPTAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(24, 24, 28));

    auto bounds = getLocalBounds().reduced(16);
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.fillRoundedRectangle(bounds.toFloat(), 12.0f);

    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.drawRoundedRectangle(bounds.toFloat(), 12.0f, 1.0f);
}

void JamPTAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(28);
    titleLabel.setBounds(area.removeFromTop(36));
    area.removeFromTop(14);

    auto row1 = area.removeFromTop(30);
    openAudioButton.setBounds(row1.removeFromLeft(180));
    row1.removeFromLeft(12);
    audioStatusLabel.setBounds(row1);

    area.removeFromTop(12);
    auto row2 = area.removeFromTop(30);
    openModelButton.setBounds(row2.removeFromLeft(180));
    row2.removeFromLeft(12);
    modelStatusLabel.setBounds(row2);

    area.removeFromTop(20);
    footerLabel.setBounds(area.removeFromTop(24));
}

void JamPTAudioProcessorEditor::buttonClicked(juce::Button* button)
{
    if (button == &openAudioButton)
        launchAudioFileChooser();
    else if (button == &openModelButton)
        launchModelFileChooser();
}

void JamPTAudioProcessorEditor::refreshLabels()
{
    const auto audioFile = audioProcessor.getLoadedAudioFileName();
    audioStatusLabel.setText(audioFile.isNotEmpty() ? "Audio: " + audioFile : "Audio: no file loaded",
                             juce::dontSendNotification);

    const auto modelFile = audioProcessor.getLoadedModelName();
    modelStatusLabel.setText(modelFile.isNotEmpty() ? "Model: " + modelFile : "Model: no Core ML model loaded",
                             juce::dontSendNotification);
}

void JamPTAudioProcessorEditor::launchAudioFileChooser()
{
    activeFileChooser = std::make_unique<juce::FileChooser>("Select an audio file");

    auto* chooser = activeFileChooser.get();
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [safeThis = juce::Component::SafePointer<JamPTAudioProcessorEditor>(this)](const juce::FileChooser& fileChooser)
    {
        if (safeThis == nullptr)
            return;

        safeThis->activeFileChooser.reset();

        const auto result = fileChooser.getResult();
        if (result == juce::File())
            return;

        if (! safeThis->audioProcessor.loadAudioFile(result))
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Jam-PT",
                                                   "Unable to load the selected audio file.");

        safeThis->refreshLabels();
    });
}

void JamPTAudioProcessorEditor::launchModelFileChooser()
{
    activeFileChooser = std::make_unique<juce::FileChooser>("Select a Core ML model", juce::File(), "*.mlmodel;*.mlpackage;*.mlmodelc");

    auto* chooser = activeFileChooser.get();
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [safeThis = juce::Component::SafePointer<JamPTAudioProcessorEditor>(this)](const juce::FileChooser& fileChooser)
    {
        if (safeThis == nullptr)
            return;

        safeThis->activeFileChooser.reset();

        const auto result = fileChooser.getResult();
        if (result == juce::File())
            return;

        juce::String error;
        if (! safeThis->audioProcessor.loadModelFile(result, error))
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Jam-PT",
                                                   error);

        safeThis->refreshLabels();
    });
}
