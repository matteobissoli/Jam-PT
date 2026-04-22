#include "AudioFilePlayer.h"

AudioFilePlayer::AudioFilePlayer()
{
    formatManager.registerBasicFormats();
}

AudioFilePlayer::~AudioFilePlayer()
{
    detachCurrentSource();
}

bool AudioFilePlayer::loadFile(const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    auto* reader = formatManager.createReaderFor(file);
    if (reader == nullptr)
        return false;

    detachCurrentSource();

    readerSource.reset(new juce::AudioFormatReaderSource(reader, true));
    transport.setSource(readerSource.get(), 0, nullptr, reader->sampleRate);
    transport.setPosition(0.0);
    transport.stop();
    loadedFile = file;
    loadedFileName = file.getFileName();
    durationSeconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    playbackState = PlaybackState::stopped;
    return true;
}

void AudioFilePlayer::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    transport.prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void AudioFilePlayer::releaseResources()
{
    transport.releaseResources();
}

void AudioFilePlayer::detachCurrentSource()
{
    transport.stop();
    transport.setSource(nullptr);
    readerSource.reset();
    loadedFile = juce::File();
    loadedFileName.clear();
    durationSeconds = 0.0;
    playbackState = PlaybackState::stopped;
}

void AudioFilePlayer::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    transport.getNextAudioBlock(bufferToFill);

    const auto currentPosition = transport.getCurrentPosition();
    const auto hasReachedEnd = durationSeconds > 0.0
                            && currentPosition >= juce::jmax(0.0, durationSeconds - 0.05);

    if (playbackState == PlaybackState::playing && hasReachedEnd && ! transport.isPlaying())
    {
        transport.setPosition(0.0);
        playbackState = PlaybackState::stopped;
    }
}

bool AudioFilePlayer::isLoaded() const
{
    return readerSource != nullptr;
}

juce::String AudioFilePlayer::getLoadedFileName() const
{
    return loadedFileName;
}

bool AudioFilePlayer::play()
{
    if (! isLoaded())
        return false;

    transport.start();
    playbackState = PlaybackState::playing;
    return true;
}

bool AudioFilePlayer::pause()
{
    if (! isLoaded() || playbackState != PlaybackState::playing)
        return false;

    transport.stop();
    playbackState = PlaybackState::paused;
    return true;
}

void AudioFilePlayer::stop()
{
    transport.stop();
    transport.setPosition(0.0);
    playbackState = PlaybackState::stopped;
}

void AudioFilePlayer::setPositionSeconds(double seconds)
{
    if (! isLoaded())
        return;

    transport.setPosition(juce::jlimit(0.0, durationSeconds, seconds));

    if (playbackState == PlaybackState::stopped && transport.getCurrentPosition() > 0.0)
        playbackState = PlaybackState::paused;
}

juce::File AudioFilePlayer::getLoadedFile() const
{
    return loadedFile;
}

AudioFilePlayer::PlaybackState AudioFilePlayer::getPlaybackState() const
{
    return playbackState;
}

double AudioFilePlayer::getCurrentPositionSeconds() const
{
    return transport.getCurrentPosition();
}

double AudioFilePlayer::getDurationSeconds() const
{
    return durationSeconds;
}

double AudioFilePlayer::getProgress() const
{
    if (durationSeconds <= 0.0)
        return 0.0;

    return juce::jlimit(0.0, 1.0, transport.getCurrentPosition() / durationSeconds);
}
