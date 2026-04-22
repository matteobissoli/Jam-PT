#pragma once

#include <array>
#include <memory>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

class DemucsProcessor : public juce::Thread
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

    DemucsProcessor();
    ~DemucsProcessor() override;

    static juce::StringArray getAvailableModelNames();
    static juce::String getDefaultModelName();

    bool loadModel(const juce::String& modelName, juce::String& errorMessage);
    bool isModelLoaded() const;
    juce::String getLoadedModelName() const;

    void prepare(double sampleRate, int samplesPerBlock, int numChannels);
    void reset();
    bool setSourceAudioFile(const juce::File& audioFile);
    void setModelEnabled(bool shouldEnable);
    bool isModelEnabled() const;
    void setStemGain(Stem stem, float gainLinear);
    float getStemGain(Stem stem) const;
    void seekTo(double positionSeconds, bool shouldResumeWhenBuffered);
    bool consumeAutoResumeIfReady();
    double getBufferProgress() const;
    juce::String getBufferStatusText() const;
    bool isStemSeparationReady() const;
    bool hasSeparationFailed() const;
    bool renderBufferedAudio(juce::AudioBuffer<float>& output, double startSeconds);
    juce::File getSourceAudioFile() const;
    juce::String getCacheRootPath() const;
    juce::String getLastProcessLog() const;

private:
    struct SeparatedAudioData
    {
        std::array<juce::AudioBuffer<float>, static_cast<size_t>(Stem::count)> stems;
        double sampleRate = 0.0;
        int numChannels = 0;
        int numSamples = 0;
        int generation = 0;
    };

    void run() override;
    void clearSeparatedAudio();
    bool isSeparatedAudioReady() const;
    void updateBufferStatus();
    bool isDemucsAvailable(juce::String& errorMessage) const;
    juce::String resolveDemucsExecutable() const;
    juce::String buildSourceCacheKey(const juce::File& audioFile) const;
    juce::File getCacheRootDirectory() const;
    juce::File getStagedInputFile(const juce::File& audioFile, const juce::String& sourceKey) const;
    juce::File getStemCacheDirectory(const juce::String& modelName, const juce::String& sourceKey) const;
    bool areStemFilesCached(const juce::File& stemDirectory) const;
    bool ensureStagedInputFile(const juce::File& audioFile,
                               const juce::String& sourceKey,
                               juce::File& stagedInputFile,
                               juce::String& errorMessage) const;
    bool runDemucsCli(const juce::String& modelName,
                      const juce::File& stagedInputFile,
                      juce::String& errorMessage);
    std::shared_ptr<SeparatedAudioData> loadSeparatedAudioFromCache(const juce::File& stemDirectory,
                                                                    int generation,
                                                                    juce::String& errorMessage);
    float getSampleAt(const juce::AudioBuffer<float>& buffer, int channel, double sourceSamplePosition) const;
    static juce::String getStemFileName(Stem stem);

    mutable juce::CriticalSection stateLock;
    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::ChildProcess> activeChildProcess;
    juce::String loadedModelName { getDefaultModelName() };
    juce::String lastModelError;
    juce::File sourceAudioFile;
    juce::String sourceCacheKey;
    bool loaded = false;
    bool modelEnabled = true;
    bool separationInProgress = false;
    bool sourceAudioLoaded = false;
    double currentSampleRate = 0.0;
    int currentNumChannels = 0;
    int currentGeneration = 0;
    double currentPlaybackPositionSeconds = 0.0;
    bool autoResumePending = false;
    double bufferProgress = 0.0;
    juce::String bufferStatusText { "Select a Demucs model" };
    juce::String lastInferenceError;
    juce::String lastProcessLog;
    std::shared_ptr<const SeparatedAudioData> separatedAudioData;
    std::array<float, static_cast<size_t>(Stem::count)> stemGains { 1.0f, 1.0f, 1.0f, 1.0f };
};
