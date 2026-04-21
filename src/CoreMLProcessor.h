#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

class CoreMLProcessor
{
public:
    CoreMLProcessor();
    ~CoreMLProcessor();

    bool loadModel(const juce::File& modelFile, juce::String& errorMessage);
    bool isModelLoaded() const;
    juce::String getLoadedModelName() const;

    void prepare(double sampleRate, int samplesPerBlock, int numChannels);
    void reset();
    void process(juce::AudioBuffer<float>& buffer);

private:
    juce::String loadedModelName;
    bool loaded = false;
    double currentSampleRate = 0.0;
    int currentBlockSize = 0;
    int currentNumChannels = 0;
};
