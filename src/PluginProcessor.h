#pragma once

#include <JuceHeader.h>
#include "AudioFilePlayer.h"
#include "CoreMLProcessor.h"

class JamPTAudioProcessor final : public juce::AudioProcessor
{
public:
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
    bool loadModelFile(const juce::File& file, juce::String& errorMessage);
    juce::String getLoadedAudioFileName() const;
    juce::String getLoadedModelName() const;
    bool isModelLoaded() const;

private:
    AudioFilePlayer player;
    CoreMLProcessor coreMLProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JamPTAudioProcessor)
};
