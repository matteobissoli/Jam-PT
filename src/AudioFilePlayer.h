#pragma once

#include <JuceHeader.h>

class AudioFilePlayer
{
public:
    enum class PlaybackState
    {
        stopped,
        playing,
        paused
    };

    AudioFilePlayer();
    ~AudioFilePlayer();

    bool loadFile(const juce::File& file);
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate);
    void releaseResources();
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill);
    bool play();
    bool pause();
    void stop();
    void setPositionSeconds(double seconds);

    bool isLoaded() const;
    juce::File getLoadedFile() const;
    juce::String getLoadedFileName() const;
    PlaybackState getPlaybackState() const;
    double getCurrentPositionSeconds() const;
    double getDurationSeconds() const;
    double getProgress() const;

private:
    void detachCurrentSource();

    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transport;
    juce::File loadedFile;
    juce::String loadedFileName;
    double durationSeconds { 0.0 };
    PlaybackState playbackState { PlaybackState::stopped };
};
