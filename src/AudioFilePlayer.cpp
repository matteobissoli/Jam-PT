#include "AudioFilePlayer.h"

AudioFilePlayer::AudioFilePlayer()
{
    formatManager.registerBasicFormats();
}

AudioFilePlayer::~AudioFilePlayer()
{
    transport.setSource(nullptr);
}

bool AudioFilePlayer::loadFile(const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    auto* reader = formatManager.createReaderFor(file);
    if (reader == nullptr)
        return false;

    readerSource.reset(new juce::AudioFormatReaderSource(reader, true));
    transport.setSource(readerSource.get(), 0, nullptr, reader->sampleRate);
    transport.setPosition(0.0);
    transport.start();
    loadedFileName = file.getFileName();
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

void AudioFilePlayer::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    transport.getNextAudioBlock(bufferToFill);
}

bool AudioFilePlayer::isLoaded() const
{
    return readerSource != nullptr;
}

juce::String AudioFilePlayer::getLoadedFileName() const
{
    return loadedFileName;
}
