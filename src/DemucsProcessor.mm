#include "DemucsProcessor.h"

namespace
{
constexpr std::array<DemucsProcessor::Stem, 4> stemOrder {
    DemucsProcessor::Stem::vocals,
    DemucsProcessor::Stem::drums,
    DemucsProcessor::Stem::bass,
    DemucsProcessor::Stem::other
};

juce::String shellQuote(const juce::String& argument)
{
    if (argument.isEmpty())
        return "''";

    auto needsQuotes = false;
    for (auto character : argument)
    {
        if (juce::CharacterFunctions::isWhitespace(character)
            || character == '\''
            || character == '"'
            || character == '\\'
            || character == '('
            || character == ')'
            || character == '['
            || character == ']')
        {
            needsQuotes = true;
            break;
        }
    }

    if (! needsQuotes)
        return argument;

    return "'" + argument.replace("'", "'\\''") + "'";
}

juce::String formatCommandForShell(const juce::StringArray& command)
{
    juce::StringArray quoted;
    for (const auto& argument : command)
        quoted.add(shellQuote(argument));

    return quoted.joinIntoString(" ");
}
}

DemucsProcessor::DemucsProcessor()
    : juce::Thread("DemucsCliWorker")
{
    formatManager.registerBasicFormats();
    startThread();
}

DemucsProcessor::~DemucsProcessor()
{
    signalThreadShouldExit();

    {
        const juce::ScopedLock lock(stateLock);
        if (activeChildProcess != nullptr)
            activeChildProcess->kill();
    }

    notify();
    stopThread(4000);
}

juce::StringArray DemucsProcessor::getAvailableModelNames()
{
    return { "htdemucs", "htdemucs_ft", "hdemucs_mmi", "mdx_extra", "mdx_extra_q" };
}

juce::String DemucsProcessor::getDefaultModelName()
{
    return "htdemucs";
}

bool DemucsProcessor::loadModel(const juce::String& modelName, juce::String& errorMessage)
{
    errorMessage.clear();
    const auto trimmedModel = modelName.trim();

    if (trimmedModel.isEmpty())
    {
        errorMessage = "Model name is empty.";
        return false;
    }

    juce::String demucsError;
    if (! isDemucsAvailable(demucsError))
    {
        errorMessage = demucsError;
        const juce::ScopedLock lock(stateLock);
        loaded = false;
        lastModelError = errorMessage;
        updateBufferStatus();
        return false;
    }

    {
        const juce::ScopedLock lock(stateLock);
        loadedModelName = trimmedModel;
        loaded = true;
        lastModelError.clear();
        lastInferenceError.clear();
        clearSeparatedAudio();
        ++currentGeneration;

        if (activeChildProcess != nullptr)
            activeChildProcess->kill();

        separationInProgress = false;
        updateBufferStatus();
    }

    notify();
    return true;
}

bool DemucsProcessor::isModelLoaded() const
{
    const juce::ScopedLock lock(stateLock);
    return loaded;
}

juce::String DemucsProcessor::getLoadedModelName() const
{
    const juce::ScopedLock lock(stateLock);
    return loadedModelName;
}

void DemucsProcessor::prepare(double sampleRate, int samplesPerBlock, int numChannels)
{
    juce::ignoreUnused(samplesPerBlock);
    const juce::ScopedLock lock(stateLock);
    currentSampleRate = sampleRate;
    currentNumChannels = numChannels;
}

void DemucsProcessor::reset()
{
    const juce::ScopedLock lock(stateLock);

    if (activeChildProcess != nullptr)
    {
        activeChildProcess->kill();
        activeChildProcess.reset();
    }

    clearSeparatedAudio();
    separationInProgress = false;
    autoResumePending = false;
    lastInferenceError.clear();
    currentPlaybackPositionSeconds = 0.0;
    currentSampleRate = 0.0;
    currentNumChannels = 0;
    bufferProgress = 0.0;
    updateBufferStatus();
}

bool DemucsProcessor::setSourceAudioFile(const juce::File& audioFile)
{
    if (audioFile == juce::File())
        return false;

    const auto cacheKey = buildSourceCacheKey(audioFile);
    juce::String cacheError;
    auto cacheRoot = getCacheRootDirectory();
    if (! cacheRoot.exists())
    {
        const auto result = cacheRoot.createDirectory();
        if (result.failed())
            cacheError = "Unable to create cache directory: " + result.getErrorMessage();
    }

    {
        const juce::ScopedLock lock(stateLock);
        sourceAudioFile = audioFile;
        sourceCacheKey = cacheKey;
        sourceAudioLoaded = true;
        currentPlaybackPositionSeconds = 0.0;
        ++currentGeneration;
        clearSeparatedAudio();
        separationInProgress = false;
        autoResumePending = false;
        lastInferenceError = cacheError;
        lastProcessLog.clear();

        if (activeChildProcess != nullptr)
            activeChildProcess->kill();

        updateBufferStatus();
    }

    notify();
    return true;
}

void DemucsProcessor::setModelEnabled(bool shouldEnable)
{
    {
        const juce::ScopedLock lock(stateLock);
        modelEnabled = shouldEnable;
        if (! modelEnabled && activeChildProcess != nullptr)
            activeChildProcess->kill();

        if (! modelEnabled)
            separationInProgress = false;

        updateBufferStatus();
    }

    notify();
}

bool DemucsProcessor::isModelEnabled() const
{
    const juce::ScopedLock lock(stateLock);
    return modelEnabled;
}

void DemucsProcessor::setStemGain(Stem stem, float gainLinear)
{
    const juce::ScopedLock lock(stateLock);
    stemGains[static_cast<size_t>(stem)] = gainLinear;
}

float DemucsProcessor::getStemGain(Stem stem) const
{
    const juce::ScopedLock lock(stateLock);
    return stemGains[static_cast<size_t>(stem)];
}

void DemucsProcessor::seekTo(double positionSeconds, bool shouldResumeWhenBuffered)
{
    const juce::ScopedLock lock(stateLock);
    currentPlaybackPositionSeconds = juce::jmax(0.0, positionSeconds);
    autoResumePending = shouldResumeWhenBuffered && ! isSeparatedAudioReady();
}

bool DemucsProcessor::consumeAutoResumeIfReady()
{
    const juce::ScopedLock lock(stateLock);
    if (! autoResumePending || ! isSeparatedAudioReady())
        return false;

    autoResumePending = false;
    return true;
}

double DemucsProcessor::getBufferProgress() const
{
    const juce::ScopedLock lock(stateLock);
    return bufferProgress;
}

juce::String DemucsProcessor::getBufferStatusText() const
{
    const juce::ScopedLock lock(stateLock);
    return bufferStatusText;
}

bool DemucsProcessor::isStemSeparationReady() const
{
    const juce::ScopedLock lock(stateLock);
    return isSeparatedAudioReady();
}

bool DemucsProcessor::hasSeparationFailed() const
{
    const juce::ScopedLock lock(stateLock);
    return ! separationInProgress && lastInferenceError.isNotEmpty();
}

bool DemucsProcessor::renderBufferedAudio(juce::AudioBuffer<float>& output, double startSeconds)
{
    output.clear();

    std::shared_ptr<const SeparatedAudioData> separatedSnapshot;
    std::array<float, static_cast<size_t>(Stem::count)> gainsSnapshot {};
    double outputSampleRate = 0.0;

    {
        const juce::ScopedLock lock(stateLock);
        currentPlaybackPositionSeconds = startSeconds;

        if (! isSeparatedAudioReady() || currentSampleRate <= 0.0)
            return false;

        separatedSnapshot = separatedAudioData;
        gainsSnapshot = stemGains;
        outputSampleRate = currentSampleRate;
    }

    const auto outputChannels = output.getNumChannels();
    const auto outputSamples = output.getNumSamples();

    for (int sample = 0; sample < outputSamples; ++sample)
    {
        const auto timeSeconds = startSeconds + (static_cast<double>(sample) / outputSampleRate);
        const auto stemSamplePosition = timeSeconds * separatedSnapshot->sampleRate;

        for (int channel = 0; channel < outputChannels; ++channel)
        {
            float mixedSample = 0.0f;

            for (size_t stemIndex = 0; stemIndex < gainsSnapshot.size(); ++stemIndex)
            {
                const auto& stemBuffer = separatedSnapshot->stems[stemIndex];
                const auto sourceChannel = juce::jmin(channel, stemBuffer.getNumChannels() - 1);
                mixedSample += getSampleAt(stemBuffer, sourceChannel, stemSamplePosition) * gainsSnapshot[stemIndex];
            }

            output.setSample(channel, sample, mixedSample);
        }
    }

    return true;
}

juce::File DemucsProcessor::getSourceAudioFile() const
{
    const juce::ScopedLock lock(stateLock);
    return sourceAudioFile;
}

juce::String DemucsProcessor::getCacheRootPath() const
{
    return getCacheRootDirectory().getFullPathName();
}

juce::String DemucsProcessor::getLastProcessLog() const
{
    const juce::ScopedLock lock(stateLock);
    return lastProcessLog;
}

void DemucsProcessor::run()
{
    while (! threadShouldExit())
    {
        juce::String modelName;
        juce::File audioFile;
        juce::String sourceKey;
        int generation = 0;
        bool shouldWait = false;

        {
            const juce::ScopedLock lock(stateLock);

            if (! loaded || ! modelEnabled || ! sourceAudioLoaded)
            {
                shouldWait = true;
            }
            else if (lastInferenceError.isNotEmpty())
            {
                shouldWait = true;
            }
            else if (isSeparatedAudioReady() || separationInProgress)
            {
                shouldWait = true;
            }
            else
            {
                modelName = loadedModelName;
                audioFile = sourceAudioFile;
                sourceKey = sourceCacheKey;
                generation = currentGeneration;
                separationInProgress = true;
                bufferProgress = 0.0;
                bufferStatusText = "Checking cache";
            }
        }

        if (shouldWait)
        {
            wait(100);
            continue;
        }

        juce::String errorMessage;
        const auto stemDirectory = getStemCacheDirectory(modelName, sourceKey);
        std::shared_ptr<SeparatedAudioData> separatedResult;

        if (areStemFilesCached(stemDirectory))
        {
            separatedResult = loadSeparatedAudioFromCache(stemDirectory, generation, errorMessage);
        }
        else
        {
            juce::File stagedInputFile;
            if (ensureStagedInputFile(audioFile, sourceKey, stagedInputFile, errorMessage)
                && runDemucsCli(modelName, stagedInputFile, errorMessage))
            {
                separatedResult = loadSeparatedAudioFromCache(stemDirectory, generation, errorMessage);
            }
        }

        {
            const juce::ScopedLock lock(stateLock);
            separationInProgress = false;

            if (generation == currentGeneration && separatedResult != nullptr)
            {
                separatedAudioData = separatedResult;
                lastInferenceError.clear();
                bufferProgress = 1.0;
            }
            else if (generation == currentGeneration)
            {
                clearSeparatedAudio();
                lastInferenceError = errorMessage.isNotEmpty() ? errorMessage : "Demucs separation failed";
                bufferProgress = 0.0;
            }

            updateBufferStatus();
        }
    }
}

void DemucsProcessor::clearSeparatedAudio()
{
    separatedAudioData.reset();
}

bool DemucsProcessor::isSeparatedAudioReady() const
{
    return separatedAudioData != nullptr && separatedAudioData->generation == currentGeneration;
}

void DemucsProcessor::updateBufferStatus()
{
    if (! loaded)
    {
        bufferProgress = 0.0;
        bufferStatusText = lastModelError.isNotEmpty() ? lastModelError : "Select a Demucs model";
        return;
    }

    if (! modelEnabled)
    {
        bufferProgress = 0.0;
        bufferStatusText = "Demucs disabled";
        return;
    }

    if (! sourceAudioLoaded)
    {
        bufferProgress = 0.0;
        bufferStatusText = "Load an audio file";
        return;
    }

    if (separationInProgress)
    {
        if (bufferStatusText.isEmpty())
            bufferStatusText = "Separating stems";
        return;
    }

    if (isSeparatedAudioReady())
    {
        bufferProgress = 1.0;
        bufferStatusText = "Stems ready (" + loadedModelName + ")";
        return;
    }

    if (lastInferenceError.isNotEmpty())
    {
        bufferProgress = 0.0;
        bufferStatusText = "Failed: " + lastInferenceError;
        return;
    }

    bufferProgress = 0.0;
    bufferStatusText = "Ready to separate with " + loadedModelName;
}

bool DemucsProcessor::isDemucsAvailable(juce::String& errorMessage) const
{
    errorMessage.clear();
    const auto demucsExecutable = resolveDemucsExecutable();
    if (demucsExecutable.isEmpty())
    {
        errorMessage = "The `demucs` command was not found. Install it with pipx or add it to PATH.";
        return false;
    }

    juce::ChildProcess child;
    if (! child.start(juce::StringArray { demucsExecutable, "--help" }))
    {
        errorMessage = "Unable to launch demucs at " + demucsExecutable;
        return false;
    }

    if (! child.waitForProcessToFinish(10000))
    {
        child.kill();
        errorMessage = "`demucs --help` timed out.";
        return false;
    }

    if (child.getExitCode() != 0)
    {
        errorMessage = child.readAllProcessOutput().trim();
        if (errorMessage.isEmpty())
            errorMessage = "`demucs --help` failed.";
        return false;
    }

    return true;
}

juce::String DemucsProcessor::resolveDemucsExecutable() const
{
    auto isExecutableFile = [] (const juce::File& file)
    {
        return file.existsAsFile() && ! file.isDirectory();
    };

    const juce::StringArray explicitCandidates {
        juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".local/bin/demucs").getFullPathName(),
        "/opt/homebrew/bin/demucs",
        "/usr/local/bin/demucs",
        "/usr/bin/demucs"
    };

    for (const auto& candidate : explicitCandidates)
    {
        juce::File executable(candidate);
        if (isExecutableFile(executable))
            return executable.getFullPathName();
    }

    const auto pathEntries = juce::StringArray::fromTokens(juce::SystemStats::getEnvironmentVariable("PATH", {}),
                                                           ":",
                                                           {});
    for (const auto& entry : pathEntries)
    {
        juce::File executable(juce::File(entry).getChildFile("demucs"));
        if (isExecutableFile(executable))
            return executable.getFullPathName();
    }

    return {};
}

juce::String DemucsProcessor::buildSourceCacheKey(const juce::File& audioFile) const
{
    const auto keyMaterial = audioFile.getFullPathName()
                           + "|"
                           + juce::String(audioFile.getSize())
                           + "|"
                           + audioFile.getLastModificationTime().toISO8601(true);

    const auto hashValue = static_cast<juce::int64>(keyMaterial.hashCode64());
    return juce::String::toHexString(hashValue).paddedLeft('0', 16);
}

juce::File DemucsProcessor::getCacheRootDirectory() const
{
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library")
        .getChildFile("Application Support")
        .getChildFile("Jam-PT")
        .getChildFile("DemucsCache");
}

juce::File DemucsProcessor::getStagedInputFile(const juce::File& audioFile, const juce::String& sourceKey) const
{
    return getCacheRootDirectory()
        .getChildFile("_inputs")
        .getChildFile(sourceKey + audioFile.getFileExtension());
}

juce::File DemucsProcessor::getStemCacheDirectory(const juce::String& modelName, const juce::String& sourceKey) const
{
    return getCacheRootDirectory()
        .getChildFile(modelName)
        .getChildFile(sourceKey);
}

bool DemucsProcessor::areStemFilesCached(const juce::File& stemDirectory) const
{
    for (const auto stem : stemOrder)
    {
        if (! stemDirectory.getChildFile(getStemFileName(stem)).existsAsFile())
            return false;
    }

    return true;
}

bool DemucsProcessor::ensureStagedInputFile(const juce::File& audioFile,
                                            const juce::String& sourceKey,
                                            juce::File& stagedInputFile,
                                            juce::String& errorMessage) const
{
    stagedInputFile = getStagedInputFile(audioFile, sourceKey);
    const auto parent = stagedInputFile.getParentDirectory();

    if (! parent.exists() && ! parent.createDirectory())
    {
        errorMessage = "Unable to create cache input directory.";
        return false;
    }

    if (stagedInputFile.existsAsFile())
        return true;

    if (! audioFile.copyFileTo(stagedInputFile))
    {
        errorMessage = "Unable to stage source audio for demucs.";
        return false;
    }

    return true;
}

bool DemucsProcessor::runDemucsCli(const juce::String& modelName,
                                   const juce::File& stagedInputFile,
                                   juce::String& errorMessage)
{
    errorMessage.clear();
    const auto demucsExecutable = resolveDemucsExecutable();
    if (demucsExecutable.isEmpty())
    {
        errorMessage = "The `demucs` command was not found. Install it with pipx or add it to PATH.";
        return false;
    }

    const auto cacheRoot = getCacheRootDirectory();
    if (! cacheRoot.exists() && ! cacheRoot.createDirectory())
    {
        errorMessage = "Unable to create demucs cache directory.";
        return false;
    }

    auto process = std::make_unique<juce::ChildProcess>();
    const juce::StringArray command {
        demucsExecutable,
        "-n", modelName,
        "-o", cacheRoot.getFullPathName(),
        stagedInputFile.getFullPathName()
    };

    if (! process->start(command))
    {
        errorMessage = "Unable to launch demucs.";
        return false;
    }

    {
        const juce::ScopedLock lock(stateLock);
        activeChildProcess = std::move(process);
        bufferProgress = 0.1;
        bufferStatusText = "Separating stems with demucs";
        lastProcessLog = "Launching: " + formatCommandForShell(command);
    }

    juce::String processOutput;
    auto updateProcessLog = [this](const juce::String& logText)
    {
        const juce::ScopedLock lock(stateLock);
        lastProcessLog = logText.length() > 4000 ? logText.fromLastOccurrenceOf("\n", false, false).trim() : logText;
    };

    auto drainChildOutput = [&processOutput, &updateProcessLog](juce::ChildProcess& child)
    {
        char buffer[512];
        bool appended = false;

        for (;;)
        {
            const auto bytesRead = child.readProcessOutput(buffer, static_cast<int>(sizeof(buffer)));
            if (bytesRead <= 0)
                break;

            processOutput += juce::String::fromUTF8(buffer, bytesRead);
            appended = true;
        }

        if (appended)
        {
            const auto trimmed = processOutput.trim();
            if (trimmed.isNotEmpty())
                updateProcessLog(trimmed);
        }
    };

    while (! threadShouldExit())
    {
        juce::ChildProcess* child = nullptr;
        {
            const juce::ScopedLock lock(stateLock);
            child = activeChildProcess.get();
        }

        if (child == nullptr)
            break;

        drainChildOutput(*child);

        if (! child->isRunning())
            break;

        wait(200);
    }

    int exitCode = -1;
    {
        const juce::ScopedLock lock(stateLock);
        if (activeChildProcess != nullptr)
        {
            drainChildOutput(*activeChildProcess);
            exitCode = static_cast<int>(activeChildProcess->getExitCode());
            activeChildProcess.reset();
        }
    }

    processOutput = processOutput.trim();
    updateProcessLog(processOutput.isNotEmpty() ? processOutput
                                                : "Demucs finished without console output.");

    if (threadShouldExit())
    {
        errorMessage = "Demucs process interrupted.";
        return false;
    }

    if (processOutput.containsIgnoreCase("No module named 'torchcodec'")
        || processOutput.containsIgnoreCase("TorchCodec is required"))
    {
        errorMessage = "Demucs runtime is missing Python package 'torchcodec'. "
                       "Install it into the pipx demucs environment, for example with: "
                       "`pipx inject demucs torchcodec`";
        return false;
    }

    if (exitCode != 0)
    {
        errorMessage = processOutput.isNotEmpty() ? processOutput : "Demucs process failed.";
        return false;
    }

    return true;
}

std::shared_ptr<DemucsProcessor::SeparatedAudioData> DemucsProcessor::loadSeparatedAudioFromCache(const juce::File& stemDirectory,
                                                                                                   int generation,
                                                                                                   juce::String& errorMessage)
{
    errorMessage.clear();
    auto separatedResult = std::make_shared<SeparatedAudioData>();

    for (const auto stem : stemOrder)
    {
        const auto stemIndex = static_cast<size_t>(stem);
        const auto stemFile = stemDirectory.getChildFile(getStemFileName(stem));

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(stemFile));
        if (reader == nullptr)
        {
            errorMessage = "Unable to read cached stem " + stemFile.getFileName();
            return nullptr;
        }

        auto& buffer = separatedResult->stems[stemIndex];
        buffer.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
        reader->read(&buffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);

        if (stem == Stem::vocals)
        {
            separatedResult->sampleRate = reader->sampleRate;
            separatedResult->numChannels = static_cast<int>(reader->numChannels);
            separatedResult->numSamples = static_cast<int>(reader->lengthInSamples);
        }
    }

    separatedResult->generation = generation;
    return separatedResult;
}

float DemucsProcessor::getSampleAt(const juce::AudioBuffer<float>& buffer,
                                   int channel,
                                   double sourceSamplePosition) const
{
    if (buffer.getNumSamples() <= 0)
        return 0.0f;

    const auto clampedPosition = juce::jlimit(0.0,
                                              static_cast<double>(juce::jmax(0, buffer.getNumSamples() - 1)),
                                              sourceSamplePosition);
    const auto baseIndex = static_cast<int>(clampedPosition);
    const auto nextIndex = juce::jmin(baseIndex + 1, buffer.getNumSamples() - 1);
    const auto fraction = static_cast<float>(clampedPosition - baseIndex);
    const auto sampleA = buffer.getSample(channel, baseIndex);
    const auto sampleB = buffer.getSample(channel, nextIndex);
    return sampleA + ((sampleB - sampleA) * fraction);
}

juce::String DemucsProcessor::getStemFileName(Stem stem)
{
    switch (stem)
    {
        case Stem::vocals: return "vocals.wav";
        case Stem::drums: return "drums.wav";
        case Stem::bass: return "bass.wav";
        case Stem::other: return "other.wav";
        case Stem::count: break;
    }

    jassertfalse;
    return {};
}
