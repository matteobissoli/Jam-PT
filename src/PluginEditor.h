#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class WaveformScrubber final : public juce::Component,
                               private juce::ChangeListener
{
public:
    WaveformScrubber();
    ~WaveformScrubber() override;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;

    void setAudioFile(const juce::File& file, const juce::File& spectrogramCacheFile);
    void clear();
    void setPlaybackProgress(double newProgress);
    void setSeparationOverlay(double progress, bool shouldShow);
    void setMarkers(const juce::Array<double>& newMarkers);
    std::function<void(double)> onSeek;

private:
    void handleSeek(float xPosition);
    void saveThumbnailCacheIfReady();
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 1 };
    juce::AudioThumbnail thumbnail { 512, formatManager, thumbnailCache };
    juce::File spectrogramCacheFile;
    juce::Array<double> markers;
    double playbackProgress { 0.0 };
    double separationProgress { 0.0 };
    bool showSeparationOverlay { false };
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

class CacheSelectorLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    juce::Font getComboBoxFont(juce::ComboBox&) override;
};

class JamPTAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Button::Listener,
                                        private juce::ComboBox::Listener,
                                        private juce::Timer
{
public:
    explicit JamPTAudioProcessorEditor(JamPTAudioProcessor&);
    ~JamPTAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked(juce::Button* button) override;
    void comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged) override;
    void timerCallback() override;
    void refreshLabels();
    void refreshCachedAudioSelector();
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
    StemKnobLookAndFeel stemKnobLookAndFeel;
    CacheSelectorLookAndFeel cacheSelectorLookAndFeel;
    juce::StringArray lastCacheEntries;
    bool suppressCacheSelectionCallback { false };
    int dividerY { 0 };

    juce::ComboBox cachedAudioComboBox;
    juce::TextButton addAudioFileButton { "+" };
    juce::TextButton openCacheFolderButton { "Show Folder" };
    juce::TextButton prevButton { "Prev" };
    juce::TextButton playbackButton { "PLAY" };
    juce::TextButton plusButton { "+" };
    juce::TextButton stopButton { "STOP" };
    juce::TextButton nextButton { "Next" };
    juce::TextButton openModelButton { "Select Demucs model" };
    juce::Label positionLabel;
    juce::Label durationLabel;
    juce::Label vocalsLabel;
    juce::Label drumsLabel;
    juce::Label bassLabel;
    juce::Label otherLabel;
    juce::TextButton vocalsSoloButton { "S" };
    juce::TextButton vocalsMuteButton { "M" };
    juce::TextButton drumsSoloButton { "S" };
    juce::TextButton drumsMuteButton { "M" };
    juce::TextButton bassSoloButton { "S" };
    juce::TextButton bassMuteButton { "M" };
    juce::TextButton otherSoloButton { "S" };
    juce::TextButton otherMuteButton { "M" };
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
