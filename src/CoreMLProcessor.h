#pragma once

#include <array>
#include <map>
#include <memory>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

class CoreMLProcessor : public juce::Thread
{
public:
    enum class Stem
    {
        vocals = 0,
        drums,
        bass,
        other,
        count
    };

    CoreMLProcessor();
    ~CoreMLProcessor();

    bool loadModel(const juce::File& modelFile, juce::String& errorMessage);
    void requestModelLoad(const juce::File& modelFile);
    bool isModelLoaded() const;
    juce::String getLoadedModelName() const;
    juce::File getLoadedModelFile() const;

    void prepare(double sampleRate, int samplesPerBlock, int numChannels);
    void reset();
    bool setSourceAudioFile(const juce::File& audioFile);
    void setModelEnabled(bool shouldEnable);
    bool isModelEnabled() const;
    void setStemGain(Stem stem, float gainLinear);
    float getStemGain(Stem stem) const;
    void setSoloStem(int stemIndex);
    int getSoloStem() const;
    void seekTo(double positionSeconds, bool shouldResumeWhenBuffered);
    bool needsBufferingAt(double positionSeconds) const;
    bool consumeAutoResumeIfReady();
    double getBufferProgress() const;
    juce::String getBufferStatusText() const;
    bool renderBufferedAudio(juce::AudioBuffer<float>& output, double startSeconds);

private:
    struct CachedSegment
    {
        int index = 0;
        int generation = 0;
        juce::AudioBuffer<float> audio;
        bool usedModelOutput = false;
        juce::String renderStatus;
    };

    struct SourceAudioData
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 0.0;
        int numChannels = 0;
    };

    void run() override;
    void clearCachedSegments();
    bool loadSourceAudioIntoMemory(const juce::File& audioFile);
    int getSegmentIndexForTime(double positionSeconds) const;
    bool hasSegmentForTime(double positionSeconds) const;
    void updateBufferStatus();
    CachedSegment renderSegment(int segmentIndex, int generation, std::array<float, static_cast<size_t>(Stem::count)> gainsSnapshot);
    float getSampleAt(const juce::AudioBuffer<float>& buffer, int channel, double sourceSamplePosition) const;

    static constexpr double segmentDurationSeconds = 7.8;
    static constexpr int prebufferSegmentCount = 3;

    mutable juce::CriticalSection stateLock;
    juce::String loadedModelName;
    juce::File loadedModelFile;
    juce::String lastModelError;
    juce::File pendingModelFile;
    void* loadedModelHandle = nullptr;
    bool loaded = false;
    bool modelLoadInProgress = false;
    double currentSampleRate = 0.0;
    int currentBlockSize = 0;
    int currentNumChannels = 0;
    bool modelEnabled = false;
    bool sourceAudioLoaded = false;
    juce::File sourceAudioFile;
    juce::AudioFormatManager formatManager;
    std::shared_ptr<const SourceAudioData> sourceAudioData;
    int requestedSegmentIndex = 0;
    int currentGeneration = 0;
    double currentPlaybackPositionSeconds = 0.0;
    bool autoResumePending = false;
    double bufferProgress = 0.0;
    juce::String bufferStatusText { "Buffer idle" };
    bool lastInferenceSucceeded = false;
    juce::String lastInferenceError;
    std::map<int, CachedSegment> cachedSegments;
    std::array<float, static_cast<size_t>(Stem::count)> stemGains { 1.0f, 1.0f, 1.0f, 1.0f };
    int soloStemIndex = -1;
};
