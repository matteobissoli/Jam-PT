#include "PluginProcessor.h"
#include "PluginEditor.h"

JamPTAudioProcessor::JamPTAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      valueTreeState(*this, nullptr, "Parameters", createParameterLayout())
{
    syncStemGainsFromParameters();
    juce::String errorMessage;
    demucsProcessor.loadModel(DemucsProcessor::getDefaultModelName(), errorMessage);
}

JamPTAudioProcessor::~JamPTAudioProcessor() = default;

const juce::String JamPTAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool JamPTAudioProcessor::acceptsMidi() const { return false; }
bool JamPTAudioProcessor::producesMidi() const { return false; }
bool JamPTAudioProcessor::isMidiEffect() const { return false; }
double JamPTAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int JamPTAudioProcessor::getNumPrograms() { return 1; }
int JamPTAudioProcessor::getCurrentProgram() { return 0; }
void JamPTAudioProcessor::setCurrentProgram(int) {}
const juce::String JamPTAudioProcessor::getProgramName(int) { return {}; }
void JamPTAudioProcessor::changeProgramName(int, const juce::String&) {}

void JamPTAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    player.prepareToPlay(samplesPerBlock, sampleRate);
    demucsProcessor.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    hasPreparedPlayback = true;
    applyPendingPlaybackRestore();
}

void JamPTAudioProcessor::releaseResources()
{
    hasPreparedPlayback = false;
    player.releaseResources();
    demucsProcessor.reset();
}

bool JamPTAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    auto mainOut = layouts.getMainOutputChannelSet();
    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void JamPTAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;
    syncStemGainsFromParameters();
    refreshBackendStateFromLoadedFile();

    buffer.clear();

    if (player.getPlaybackState() != AudioFilePlayer::PlaybackState::playing)
        return;

    const auto playbackStartSeconds = player.getCurrentPositionSeconds();
    juce::AudioBuffer<float> playerBuffer(buffer.getNumChannels(), buffer.getNumSamples());
    playerBuffer.clear();

    juce::AudioSourceChannelInfo info(&playerBuffer, 0, playerBuffer.getNumSamples());
    player.getNextAudioBlock(info);

    if (demucsProcessor.renderBufferedAudio(buffer, playbackStartSeconds))
    {
        if (demucsProcessor.consumeAutoResumeIfReady())
            player.play();
    }
    else
    {
        buffer.makeCopyOf(playerBuffer, true);
    }
}

bool JamPTAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* JamPTAudioProcessor::createEditor()
{
    return new JamPTAudioProcessorEditor(*this);
}

void JamPTAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("JamPTState");
    state.appendChild(valueTreeState.copyState(), nullptr);
    state.setProperty("audioFilePath", getLoadedAudioFile().getFullPathName(), nullptr);
    state.setProperty("audioPositionSeconds", getPlaybackPositionSeconds(), nullptr);
    state.setProperty("modelName", getLoadedModelName(), nullptr);

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void JamPTAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState == nullptr)
        return;

    const auto state = juce::ValueTree::fromXml(*xmlState);
    if (! state.isValid())
        return;

    pendingPlaybackRestore = {};

    const auto audioFilePath = state.getProperty("audioFilePath").toString();
    if (audioFilePath.isNotEmpty())
    {
        pendingPlaybackRestore.audioFile = juce::File(audioFilePath);
        pendingPlaybackRestore.positionSeconds = static_cast<double>(state.getProperty("audioPositionSeconds", 0.0));
        pendingPlaybackRestore.playbackState = AudioFilePlayer::PlaybackState::stopped;
        pendingPlaybackRestore.isValid = pendingPlaybackRestore.audioFile.existsAsFile();
    }

    const auto modelName = state.getProperty("modelName", DemucsProcessor::getDefaultModelName()).toString();
    if (modelName.isNotEmpty())
    {
        juce::String errorMessage;
        demucsProcessor.loadModel(modelName, errorMessage);
    }

    if (const auto parameterState = state.getChildWithName(valueTreeState.state.getType()); parameterState.isValid())
        valueTreeState.replaceState(parameterState);

    syncStemGainsFromParameters();

    applyPendingPlaybackRestore();
}

bool JamPTAudioProcessor::loadAudioFile(const juce::File& file)
{
    player.stop();
    const auto loadedAudio = player.loadFile(file);
    if (! loadedAudio)
        return false;

    const auto backendAudioFile = player.getLoadedFile();
    if (! demucsProcessor.setSourceAudioFile(backendAudioFile))
        return false;

    return true;
}

bool JamPTAudioProcessor::startPlayback()
{
    if (! demucsProcessor.isStemSeparationReady())
        return false;

    demucsProcessor.seekTo(player.getCurrentPositionSeconds(), false);
    return player.play();
}

bool JamPTAudioProcessor::pausePlayback()
{
    return player.pause();
}

void JamPTAudioProcessor::stopPlayback()
{
    player.stop();
    demucsProcessor.seekTo(0.0, false);
}

void JamPTAudioProcessor::setPlaybackPositionSeconds(double seconds)
{
    player.setPositionSeconds(seconds);
    demucsProcessor.seekTo(seconds, false);
}

bool JamPTAudioProcessor::selectModel(const juce::String& modelName, juce::String& errorMessage)
{
    const auto loadedModel = demucsProcessor.loadModel(modelName, errorMessage);

    if (loadedModel)
    {
        demucsProcessor.setModelEnabled(true);

        if (const auto audioFile = getLoadedAudioFile(); audioFile.existsAsFile())
            demucsProcessor.setSourceAudioFile(audioFile);
    }

    return loadedModel;
}

void JamPTAudioProcessor::setModelEnabled(bool shouldEnable)
{
    demucsProcessor.setModelEnabled(shouldEnable);
}

bool JamPTAudioProcessor::isModelEnabled() const
{
    return demucsProcessor.isModelEnabled();
}

void JamPTAudioProcessor::setStemGain(DemucsProcessor::Stem stem, float gainLinear)
{
    if (auto* parameter = valueTreeState.getParameter(getStemParameterId(stem)))
        parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.2f, gainLinear) / 1.2f);

    demucsProcessor.setStemGain(stem, gainLinear);
}

float JamPTAudioProcessor::getStemGain(DemucsProcessor::Stem stem) const
{
    return demucsProcessor.getStemGain(stem);
}

double JamPTAudioProcessor::getModelBufferProgress() const
{
    return demucsProcessor.getBufferProgress();
}

juce::String JamPTAudioProcessor::getModelBufferStatusText() const
{
    return demucsProcessor.getBufferStatusText();
}

bool JamPTAudioProcessor::isStemSeparationReady() const
{
    return demucsProcessor.isStemSeparationReady();
}

bool JamPTAudioProcessor::hasSeparationFailed() const
{
    return demucsProcessor.hasSeparationFailed();
}

juce::File JamPTAudioProcessor::getLoadedAudioFile() const
{
    return player.getLoadedFile();
}

juce::String JamPTAudioProcessor::getLoadedAudioFileName() const
{
    return player.getLoadedFileName();
}

AudioFilePlayer::PlaybackState JamPTAudioProcessor::getPlaybackState() const
{
    return player.getPlaybackState();
}

double JamPTAudioProcessor::getPlaybackPositionSeconds() const
{
    return player.getCurrentPositionSeconds();
}

double JamPTAudioProcessor::getPlaybackDurationSeconds() const
{
    return player.getDurationSeconds();
}

double JamPTAudioProcessor::getPlaybackProgress() const
{
    return player.getProgress();
}

juce::String JamPTAudioProcessor::getLoadedModelName() const
{
    return demucsProcessor.getLoadedModelName();
}

juce::StringArray JamPTAudioProcessor::getAvailableModelNames() const
{
    return DemucsProcessor::getAvailableModelNames();
}

bool JamPTAudioProcessor::isModelLoaded() const
{
    return demucsProcessor.isModelLoaded();
}

juce::String JamPTAudioProcessor::getCacheRootPath() const
{
    return demucsProcessor.getCacheRootPath();
}

juce::String JamPTAudioProcessor::getLastDemucsLog() const
{
    return demucsProcessor.getLastProcessLog();
}

JamPTAudioProcessor::APVTS& JamPTAudioProcessor::getValueTreeState()
{
    return valueTreeState;
}

void JamPTAudioProcessor::refreshBackendStateFromLoadedFile()
{
    const auto playerFile = player.getLoadedFile();
    if (! playerFile.existsAsFile())
        return;

    const auto backendFile = demucsProcessor.getSourceAudioFile();
    const auto backendStatus = demucsProcessor.getBufferStatusText();
    const auto backendNeedsSourceReload = (backendStatus == "Load an audio file");

    if (! backendNeedsSourceReload && backendFile == playerFile)
        return;

    demucsProcessor.setSourceAudioFile(playerFile);
}

JamPTAudioProcessor::APVTS::ParameterLayout JamPTAudioProcessor::createParameterLayout()
{
    APVTS::ParameterLayout layout;

    auto addStemParameter = [&layout](const juce::String& id, const juce::String& name)
    {
        auto attributes = juce::AudioParameterFloatAttributes()
                            .withLabel("%")
                            .withStringFromValueFunction([](float value, int)
                            {
                                return juce::String::formatted("%.0f%%", static_cast<double>(value * 100.0f));
                            })
                            .withValueFromStringFunction([](const juce::String& text)
                            {
                                return text.upToFirstOccurrenceOf("%", false, false).getFloatValue() / 100.0f;
                            });

        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(id, 1),
                                                               name,
                                                               juce::NormalisableRange<float>(0.0f, 1.2f, 0.01f),
                                                               1.0f,
                                                               attributes));
    };

    addStemParameter(getStemParameterId(DemucsProcessor::Stem::vocals), "Vocals");
    addStemParameter(getStemParameterId(DemucsProcessor::Stem::drums), "Drums");
    addStemParameter(getStemParameterId(DemucsProcessor::Stem::bass), "Bass");
    addStemParameter(getStemParameterId(DemucsProcessor::Stem::other), "Other");
    return layout;
}

juce::String JamPTAudioProcessor::getStemParameterId(DemucsProcessor::Stem stem)
{
    switch (stem)
    {
        case DemucsProcessor::Stem::vocals: return "vocals_gain";
        case DemucsProcessor::Stem::drums: return "drums_gain";
        case DemucsProcessor::Stem::bass: return "bass_gain";
        case DemucsProcessor::Stem::other: return "other_gain";
        case DemucsProcessor::Stem::count: break;
    }

    jassertfalse;
    return {};
}

void JamPTAudioProcessor::syncStemGainsFromParameters()
{
    applyStemGainFromParameter(DemucsProcessor::Stem::vocals);
    applyStemGainFromParameter(DemucsProcessor::Stem::drums);
    applyStemGainFromParameter(DemucsProcessor::Stem::bass);
    applyStemGainFromParameter(DemucsProcessor::Stem::other);
}

void JamPTAudioProcessor::applyStemGainFromParameter(DemucsProcessor::Stem stem)
{
    if (auto* rawValue = valueTreeState.getRawParameterValue(getStemParameterId(stem)))
        demucsProcessor.setStemGain(stem, rawValue->load());
}

void JamPTAudioProcessor::applyPendingPlaybackRestore()
{
    if (! hasPreparedPlayback || ! pendingPlaybackRestore.isValid)
        return;

    if (! loadAudioFile(pendingPlaybackRestore.audioFile))
    {
        pendingPlaybackRestore = {};
        return;
    }

    setPlaybackPositionSeconds(pendingPlaybackRestore.positionSeconds);

    if (pendingPlaybackRestore.playbackState == AudioFilePlayer::PlaybackState::playing)
        startPlayback();
    else if (pendingPlaybackRestore.playbackState == AudioFilePlayer::PlaybackState::stopped)
        stopPlayback();

    pendingPlaybackRestore = {};
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JamPTAudioProcessor();
}
