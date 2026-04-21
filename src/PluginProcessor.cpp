#include "PluginProcessor.h"
#include "PluginEditor.h"

JamPTAudioProcessor::JamPTAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
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
    coreMLProcessor.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
}

void JamPTAudioProcessor::releaseResources()
{
    player.releaseResources();
    coreMLProcessor.reset();
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

    buffer.clear();

    juce::AudioSourceChannelInfo info(&buffer, 0, buffer.getNumSamples());
    player.getNextAudioBlock(info);
    coreMLProcessor.process(buffer);
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
    state.setProperty("audioFile", getLoadedAudioFileName(), nullptr);
    state.setProperty("modelFile", getLoadedModelName(), nullptr);

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

    // Intentional placeholder: at this stage we only persist displayed names.
    // Re-loading files should be added later with a safe path strategy.
}

bool JamPTAudioProcessor::loadAudioFile(const juce::File& file)
{
    return player.loadFile(file);
}

bool JamPTAudioProcessor::loadModelFile(const juce::File& file, juce::String& errorMessage)
{
    return coreMLProcessor.loadModel(file, errorMessage);
}

juce::String JamPTAudioProcessor::getLoadedAudioFileName() const
{
    return player.getLoadedFileName();
}

juce::String JamPTAudioProcessor::getLoadedModelName() const
{
    return coreMLProcessor.getLoadedModelName();
}

bool JamPTAudioProcessor::isModelLoaded() const
{
    return coreMLProcessor.isModelLoaded();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JamPTAudioProcessor();
}
