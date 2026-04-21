#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class JamPTAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Button::Listener
{
public:
    explicit JamPTAudioProcessorEditor(JamPTAudioProcessor&);
    ~JamPTAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked(juce::Button* button) override;
    void refreshLabels();
    void launchAudioFileChooser();
    void launchModelFileChooser();

    JamPTAudioProcessor& audioProcessor;
    std::unique_ptr<juce::FileChooser> activeFileChooser;

    juce::Label titleLabel;
    juce::TextButton openAudioButton { "Open audio file" };
    juce::TextButton openModelButton { "Open Core ML model" };
    juce::Label audioStatusLabel;
    juce::Label modelStatusLabel;
    juce::Label footerLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JamPTAudioProcessorEditor)
};
