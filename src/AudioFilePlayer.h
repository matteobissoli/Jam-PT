#pragma once

#include <JuceHeader.h>

class AudioFilePlayer
{
public:
    AudioFilePlayer();
    ~AudioFilePlayer();

    bool loadFile(const juce::File& file);
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate);
    void releaseResources();
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill);

    bool isLoaded() const;
    juce::String getLoadedFileName() const;

private:
    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transport;
    juce::String loadedFileName;
};
