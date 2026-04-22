#pragma once

#include <JuceHeader.h>
#include "AudioFilePlayer.h"
#include "DemucsProcessor.h"

class JamPTAudioProcessor final : public juce::AudioProcessor
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    JamPTAudioProcessor();
    ~JamPTAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    bool loadAudioFile(const juce::File& file);
    bool startPlayback();
    bool pausePlayback();
    void stopPlayback();
    void setPlaybackPositionSeconds(double seconds);
    bool selectModel(const juce::String& modelName, juce::String& errorMessage);
    void setModelEnabled(bool shouldEnable);
    bool isModelEnabled() const;
    void setStemGain(DemucsProcessor::Stem stem, float gainLinear);
    float getStemGain(DemucsProcessor::Stem stem) const;
    double getModelBufferProgress() const;
    juce::String getModelBufferStatusText() const;
    bool isStemSeparationReady() const;
    bool hasSeparationFailed() const;
    juce::File getLoadedAudioFile() const;
    juce::String getLoadedAudioFileName() const;
    AudioFilePlayer::PlaybackState getPlaybackState() const;
    double getPlaybackPositionSeconds() const;
    double getPlaybackDurationSeconds() const;
    double getPlaybackProgress() const;
    juce::StringArray getAvailableModelNames() const;
    juce::String getLoadedModelName() const;
    bool isModelLoaded() const;
    juce::String getCacheRootPath() const;
    juce::String getLastDemucsLog() const;
    APVTS& getValueTreeState();
    static APVTS::ParameterLayout createParameterLayout();
    static juce::String getStemParameterId(DemucsProcessor::Stem stem);
    void refreshBackendStateFromLoadedFile();

private:
    void syncStemGainsFromParameters();
    void applyStemGainFromParameter(DemucsProcessor::Stem stem);
    struct PendingPlaybackRestore
    {
        juce::File audioFile;
        double positionSeconds { 0.0 };
        AudioFilePlayer::PlaybackState playbackState { AudioFilePlayer::PlaybackState::stopped };
        bool isValid { false };
    };

    void applyPendingPlaybackRestore();

    AudioFilePlayer player;
    DemucsProcessor demucsProcessor;
    APVTS valueTreeState;
    PendingPlaybackRestore pendingPlaybackRestore;
    bool hasPreparedPlayback { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JamPTAudioProcessor)
};
