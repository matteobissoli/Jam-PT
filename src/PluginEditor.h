#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class WaveformScrubber final : public juce::Component
{
public:
    WaveformScrubber();

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;

    void setAudioFile(const juce::File& file);
    void clear();
    void setPlaybackProgress(double newProgress);
    std::function<void(double)> onSeek;

private:
    void handleSeek(float xPosition);

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 1 };
    juce::AudioThumbnail thumbnail { 512, formatManager, thumbnailCache };
    double playbackProgress { 0.0 };
};

class StemKnobLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;
};

class JamPTAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Button::Listener,
                                        private juce::Timer
{
public:
    explicit JamPTAudioProcessorEditor(JamPTAudioProcessor&);
    ~JamPTAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked(juce::Button* button) override;
    void timerCallback() override;
    void refreshLabels();
    void launchAudioFileChooser();
    void launchModelFileChooser();
    void handlePlaybackButton();
    void handleStopButton();
    void configureStemKnob(juce::Slider& slider, const juce::String& name, DemucsProcessor::Stem stem);
    static juce::String formatTime(double seconds);

    JamPTAudioProcessor& audioProcessor;
    JamPTAudioProcessor::APVTS& valueTreeState;
    std::unique_ptr<juce::FileChooser> activeFileChooser;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    double modelBufferProgressValue { 0.0 };
    StemKnobLookAndFeel stemKnobLookAndFeel;

    juce::Label titleLabel;
    juce::TextButton openAudioButton { "Open audio file" };
    juce::TextButton playbackButton { "PLAY" };
    juce::TextButton stopButton { "STOP" };
    juce::TextButton openModelButton { "Select Demucs model" };
    juce::Label audioStatusLabel;
    juce::Label positionLabel;
    juce::Label durationLabel;
    juce::Label bufferStatusLabel;
    juce::ProgressBar modelBufferProgressBar { modelBufferProgressValue };
    juce::Label vocalsLabel;
    juce::Label drumsLabel;
    juce::Label bassLabel;
    juce::Label otherLabel;
    juce::Slider vocalsSlider;
    juce::Slider drumsSlider;
    juce::Slider bassSlider;
    juce::Slider otherSlider;
    WaveformScrubber waveformScrubber;
    juce::Label footerLabel;
    std::unique_ptr<SliderAttachment> vocalsAttachment;
    std::unique_ptr<SliderAttachment> drumsAttachment;
    std::unique_ptr<SliderAttachment> bassAttachment;
    std::unique_ptr<SliderAttachment> otherAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JamPTAudioProcessorEditor)
};
