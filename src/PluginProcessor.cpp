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
    processMarkerActionParameters();

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
    state.setProperty("cachedSourceEntryName", getSelectedCacheEntryName(), nullptr);
    state.setProperty("audioPositionSeconds", getPlaybackPositionSeconds(), nullptr);
    state.setProperty("modelName", getLoadedModelName(), nullptr);
    state.setProperty("vocalsSolo", isStemSolo(DemucsProcessor::Stem::vocals), nullptr);
    state.setProperty("drumsSolo", isStemSolo(DemucsProcessor::Stem::drums), nullptr);
    state.setProperty("bassSolo", isStemSolo(DemucsProcessor::Stem::bass), nullptr);
    state.setProperty("otherSolo", isStemSolo(DemucsProcessor::Stem::other), nullptr);
    state.setProperty("vocalsMute", isStemMuted(DemucsProcessor::Stem::vocals), nullptr);
    state.setProperty("drumsMute", isStemMuted(DemucsProcessor::Stem::drums), nullptr);
    state.setProperty("bassMute", isStemMuted(DemucsProcessor::Stem::bass), nullptr);
    state.setProperty("otherMute", isStemMuted(DemucsProcessor::Stem::other), nullptr);

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
    const auto cachedSourceEntryName = state.getProperty("cachedSourceEntryName").toString();
    if (cachedSourceEntryName.isNotEmpty())
    {
        const auto cachedEntries = getCachedSourceEntryNames();
        if (cachedEntries.contains(cachedSourceEntryName))
        {
            pendingPlaybackRestore.cachedSourceEntryName = cachedSourceEntryName;
            pendingPlaybackRestore.positionSeconds = static_cast<double>(state.getProperty("audioPositionSeconds", 0.0));
            pendingPlaybackRestore.playbackState = AudioFilePlayer::PlaybackState::stopped;
            pendingPlaybackRestore.isValid = true;
        }
    }
    else if (audioFilePath.isNotEmpty())
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
    setStemSolo(DemucsProcessor::Stem::vocals, static_cast<bool>(state.getProperty("vocalsSolo", false)));
    setStemSolo(DemucsProcessor::Stem::drums, static_cast<bool>(state.getProperty("drumsSolo", false)));
    setStemSolo(DemucsProcessor::Stem::bass, static_cast<bool>(state.getProperty("bassSolo", false)));
    setStemSolo(DemucsProcessor::Stem::other, static_cast<bool>(state.getProperty("otherSolo", false)));
    setStemMute(DemucsProcessor::Stem::vocals, static_cast<bool>(state.getProperty("vocalsMute", false)));
    setStemMute(DemucsProcessor::Stem::drums, static_cast<bool>(state.getProperty("drumsMute", false)));
    setStemMute(DemucsProcessor::Stem::bass, static_cast<bool>(state.getProperty("bassMute", false)));
    setStemMute(DemucsProcessor::Stem::other, static_cast<bool>(state.getProperty("otherMute", false)));

    applyPendingPlaybackRestore();
}

bool JamPTAudioProcessor::loadAudioFile(const juce::File& file)
{
    juce::String errorMessage;
    juce::File cachedSourceFile;
    if (! demucsProcessor.prepareSourceAudioFile(file, cachedSourceFile, errorMessage))
        return false;

    player.stop();
    const auto loadedAudio = player.loadFile(cachedSourceFile);
    if (! loadedAudio)
        return false;

    const auto backendAudioFile = player.getLoadedFile();
    if (! demucsProcessor.setSourceAudioFile(backendAudioFile))
        return false;

    return true;
}

bool JamPTAudioProcessor::loadCachedSourceEntry(const juce::String& entryName)
{
    juce::String errorMessage;
    juce::File cachedSourceFile;
    if (! demucsProcessor.resolveCachedSourceEntry(entryName, cachedSourceFile, errorMessage))
        return false;

    player.stop();
    const auto loadedAudio = player.loadFile(cachedSourceFile);
    if (! loadedAudio)
        return false;

    return demucsProcessor.setSourceAudioFile(player.getLoadedFile());
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

void JamPTAudioProcessor::setStemSolo(DemucsProcessor::Stem stem, bool shouldSolo)
{
    demucsProcessor.setStemSolo(stem, shouldSolo);
}

bool JamPTAudioProcessor::isStemSolo(DemucsProcessor::Stem stem) const
{
    return demucsProcessor.isStemSolo(stem);
}

void JamPTAudioProcessor::setStemMute(DemucsProcessor::Stem stem, bool shouldMute)
{
    demucsProcessor.setStemMute(stem, shouldMute);
}

bool JamPTAudioProcessor::isStemMuted(DemucsProcessor::Stem stem) const
{
    return demucsProcessor.isStemMuted(stem);
}

juce::Array<double> JamPTAudioProcessor::getMarkers() const
{
    return demucsProcessor.getMarkers();
}

bool JamPTAudioProcessor::hasMarkers() const
{
    return demucsProcessor.hasMarkers();
}

bool JamPTAudioProcessor::isAtMarker() const
{
    return demucsProcessor.hasMarkerNearPosition(getPlaybackPositionSeconds());
}

bool JamPTAudioProcessor::canAddMarker() const
{
    if (! isStemSeparationReady())
        return false;

    return demucsProcessor.canAddMarkerAt(getPlaybackPositionSeconds(), getPlaybackDurationSeconds());
}

bool JamPTAudioProcessor::addMarkerAtCurrentPosition()
{
    if (! isStemSeparationReady())
        return false;

    return demucsProcessor.addMarker(getPlaybackPositionSeconds(), getPlaybackDurationSeconds());
}

bool JamPTAudioProcessor::removeMarkerAtCurrentPosition()
{
    if (! isStemSeparationReady())
        return false;

    return demucsProcessor.removeMarkerNear(getPlaybackPositionSeconds());
}

bool JamPTAudioProcessor::jumpToPreviousMarker()
{
    if (! isStemSeparationReady())
        return false;

    double markerPositionSeconds = 0.0;
    if (! demucsProcessor.getPreviousMarker(getPlaybackPositionSeconds(), markerPositionSeconds))
        return false;

    setPlaybackPositionSeconds(markerPositionSeconds);
    return true;
}

bool JamPTAudioProcessor::jumpToNextMarker()
{
    if (! isStemSeparationReady())
        return false;

    double markerPositionSeconds = 0.0;
    if (! demucsProcessor.getNextMarker(getPlaybackPositionSeconds(), markerPositionSeconds))
        return false;

    setPlaybackPositionSeconds(markerPositionSeconds);
    return true;
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

juce::StringArray JamPTAudioProcessor::getCachedSourceEntryNames() const
{
    return demucsProcessor.getCachedSourceEntryNames();
}

juce::String JamPTAudioProcessor::getSelectedCacheEntryName() const
{
    return demucsProcessor.getSelectedCacheEntryName();
}

juce::File JamPTAudioProcessor::getSelectedCacheDirectory() const
{
    return demucsProcessor.getSelectedCacheDirectory();
}

juce::File JamPTAudioProcessor::getSpectrogramCacheFile() const
{
    return demucsProcessor.getSpectrogramCacheFile();
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
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID(getMarkerActionParameterId("prev"), 1),
                                                          "Previous Marker",
                                                          false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID(getMarkerActionParameterId("toggle"), 1),
                                                          "Toggle Marker",
                                                          false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID(getMarkerActionParameterId("next"), 1),
                                                          "Next Marker",
                                                          false));
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

juce::String JamPTAudioProcessor::getMarkerActionParameterId(const juce::String& actionName)
{
    return "marker_" + actionName;
}

void JamPTAudioProcessor::syncStemGainsFromParameters()
{
    applyStemGainFromParameter(DemucsProcessor::Stem::vocals);
    applyStemGainFromParameter(DemucsProcessor::Stem::drums);
    applyStemGainFromParameter(DemucsProcessor::Stem::bass);
    applyStemGainFromParameter(DemucsProcessor::Stem::other);
}

void JamPTAudioProcessor::processMarkerActionParameters()
{
    auto handleMarkerAction = [this](const juce::String& parameterId,
                                     bool& previousPressedState,
                                     const std::function<void()>& action)
    {
        auto* rawValue = valueTreeState.getRawParameterValue(parameterId);
        if (rawValue == nullptr)
            return;

        const auto isPressed = rawValue->load() >= 0.5f;
        if (isPressed && ! previousPressedState)
            action();

        previousPressedState = isPressed;
    };

    handleMarkerAction(getMarkerActionParameterId("prev"),
                       previousMarkerActionPressed,
                       [this]() { jumpToPreviousMarker(); });
    handleMarkerAction(getMarkerActionParameterId("toggle"),
                       toggleMarkerActionPressed,
                       [this]()
                       {
                           if (isAtMarker())
                               removeMarkerAtCurrentPosition();
                           else
                               addMarkerAtCurrentPosition();
                       });
    handleMarkerAction(getMarkerActionParameterId("next"),
                       nextMarkerActionPressed,
                       [this]() { jumpToNextMarker(); });
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

    const auto restored = pendingPlaybackRestore.cachedSourceEntryName.isNotEmpty()
                            ? loadCachedSourceEntry(pendingPlaybackRestore.cachedSourceEntryName)
                            : loadAudioFile(pendingPlaybackRestore.audioFile);

    if (! restored)
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
