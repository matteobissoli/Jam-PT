#include "DemucsProcessor.h"

namespace
{
constexpr std::array<DemucsProcessor::Stem, 4> stemOrder {
    DemucsProcessor::Stem::vocals,
    DemucsProcessor::Stem::drums,
    DemucsProcessor::Stem::bass,
    DemucsProcessor::Stem::other
};

constexpr double markerToleranceSeconds = 0.05;
constexpr double markerNavigationToleranceSeconds = 1.0;

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

juce::Array<juce::File> findDirectChildDirectories(const juce::File& directory)
{
    juce::Array<juce::File> children;
    if (! directory.isDirectory())
        return children;

    const auto entries = directory.findChildFiles(juce::File::findDirectories, false);
    for (const auto& entry : entries)
        children.add(entry);

    return children;
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
        selectedCacheDirectory = audioFile.getParentDirectory();
        selectedCacheEntryName = selectedCacheDirectory.getFileName();
        sourceAudioLoaded = true;
        currentPlaybackPositionSeconds = 0.0;
        ++currentGeneration;
        markers = loadMarkersFromMetadata(selectedCacheDirectory);
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

void DemucsProcessor::setStemSolo(Stem stem, bool shouldSolo)
{
    const juce::ScopedLock lock(stateLock);
    stemSoloStates[static_cast<size_t>(stem)] = shouldSolo;
}

bool DemucsProcessor::isStemSolo(Stem stem) const
{
    const juce::ScopedLock lock(stateLock);
    return stemSoloStates[static_cast<size_t>(stem)];
}

void DemucsProcessor::setStemMute(Stem stem, bool shouldMute)
{
    const juce::ScopedLock lock(stateLock);
    stemMuteStates[static_cast<size_t>(stem)] = shouldMute;
}

bool DemucsProcessor::isStemMuted(Stem stem) const
{
    const juce::ScopedLock lock(stateLock);
    return stemMuteStates[static_cast<size_t>(stem)];
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
    std::array<bool, static_cast<size_t>(Stem::count)> soloSnapshot {};
    std::array<bool, static_cast<size_t>(Stem::count)> muteSnapshot {};
    double outputSampleRate = 0.0;

    {
        const juce::ScopedLock lock(stateLock);
        currentPlaybackPositionSeconds = startSeconds;

        if (! isSeparatedAudioReady() || currentSampleRate <= 0.0)
            return false;

        separatedSnapshot = separatedAudioData;
        gainsSnapshot = stemGains;
        soloSnapshot = stemSoloStates;
        muteSnapshot = stemMuteStates;
        outputSampleRate = currentSampleRate;
    }

    const auto hasAnySolo = std::any_of(soloSnapshot.begin(), soloSnapshot.end(), [] (bool isSolo) { return isSolo; });

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
                if (muteSnapshot[stemIndex])
                    continue;

                if (hasAnySolo && ! soloSnapshot[stemIndex])
                    continue;

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

juce::File DemucsProcessor::getSelectedCacheDirectory() const
{
    const juce::ScopedLock lock(stateLock);
    return selectedCacheDirectory;
}

juce::File DemucsProcessor::getSpectrogramCacheFile() const
{
    const juce::ScopedLock lock(stateLock);
    return getSpectrogramCacheFile(selectedCacheDirectory);
}

juce::String DemucsProcessor::getSelectedCacheEntryName() const
{
    const juce::ScopedLock lock(stateLock);
    return selectedCacheEntryName;
}

juce::Array<double> DemucsProcessor::getMarkers() const
{
    const juce::ScopedLock lock(stateLock);
    return markers;
}

bool DemucsProcessor::hasMarkers() const
{
    const juce::ScopedLock lock(stateLock);
    return ! markers.isEmpty();
}

bool DemucsProcessor::hasMarkerNearPosition(double positionSeconds) const
{
    const juce::ScopedLock lock(stateLock);

    for (const auto markerPosition : markers)
    {
        if (std::abs(markerPosition - positionSeconds) <= markerToleranceSeconds)
            return true;
    }

    return false;
}

bool DemucsProcessor::canAddMarkerAt(double positionSeconds, double durationSeconds) const
{
    const auto clampedPosition = juce::jlimit(0.0, durationSeconds, positionSeconds);
    if (durationSeconds <= 0.0
        || clampedPosition <= markerToleranceSeconds
        || clampedPosition >= durationSeconds - markerToleranceSeconds)
    {
        return false;
    }

    return ! hasMarkerNearPosition(clampedPosition);
}

bool DemucsProcessor::addMarker(double positionSeconds, double durationSeconds)
{
    juce::Array<double> markersToSave;
    juce::File sourceDirectory;

    {
        const juce::ScopedLock lock(stateLock);
        if (! sourceAudioLoaded || selectedCacheDirectory == juce::File())
            return false;

        const auto clampedPosition = juce::jlimit(0.0, durationSeconds, positionSeconds);
        if (durationSeconds <= 0.0
            || clampedPosition <= markerToleranceSeconds
            || clampedPosition >= durationSeconds - markerToleranceSeconds)
        {
            return false;
        }

        for (const auto markerPosition : markers)
        {
            if (std::abs(markerPosition - clampedPosition) <= markerToleranceSeconds)
                return false;
        }

        markers.add(clampedPosition);
        markers.sort();
        markersToSave = markers;
        sourceDirectory = selectedCacheDirectory;
    }

    return saveMarkersToMetadata(sourceDirectory, markersToSave);
}

bool DemucsProcessor::removeMarkerNear(double positionSeconds)
{
    juce::Array<double> markersToSave;
    juce::File sourceDirectory;
    auto removed = false;

    {
        const juce::ScopedLock lock(stateLock);
        if (! sourceAudioLoaded || selectedCacheDirectory == juce::File())
            return false;

        for (int index = 0; index < markers.size(); ++index)
        {
            if (std::abs(markers.getReference(index) - positionSeconds) <= markerToleranceSeconds)
            {
                markers.remove(index);
                removed = true;
                break;
            }
        }

        if (! removed)
            return false;

        markersToSave = markers;
        sourceDirectory = selectedCacheDirectory;
    }

    return saveMarkersToMetadata(sourceDirectory, markersToSave);
}

bool DemucsProcessor::getPreviousMarker(double positionSeconds, double& markerPositionSeconds) const
{
    const juce::ScopedLock lock(stateLock);

    for (int index = markers.size(); --index >= 0;)
    {
        const auto markerPosition = markers.getReference(index);
        if (markerPosition < positionSeconds - markerNavigationToleranceSeconds)
        {
            markerPositionSeconds = markerPosition;
            return true;
        }
    }

    return false;
}

bool DemucsProcessor::getNextMarker(double positionSeconds, double& markerPositionSeconds) const
{
    const juce::ScopedLock lock(stateLock);

    for (const auto markerPosition : markers)
    {
        if (markerPosition > positionSeconds + markerNavigationToleranceSeconds)
        {
            markerPositionSeconds = markerPosition;
            return true;
        }
    }

    return false;
}

juce::StringArray DemucsProcessor::getCachedSourceEntryNames() const
{
    juce::StringArray entries;
    const auto cacheRoot = getCacheRootDirectory();
    for (const auto& sourceDirectory : findDirectChildDirectories(cacheRoot))
    {
        if (getCachedSourceFile(sourceDirectory).existsAsFile())
            entries.add(sourceDirectory.getFileName());
    }

    entries.sort(true);
    return entries;
}

bool DemucsProcessor::prepareSourceAudioFile(const juce::File& audioFile,
                                             juce::File& cachedSourceFile,
                                             juce::String& errorMessage) const
{
    if (! audioFile.existsAsFile())
    {
        errorMessage = "The selected audio file does not exist.";
        return false;
    }

    auto existingSourceDirectory = findExistingSourceDirectory(audioFile);
    if (existingSourceDirectory == juce::File())
    {
        const auto preferredName = audioFile.getFileName();
        existingSourceDirectory = getCacheRootDirectory().getChildFile(getUniqueSourceDirectoryName(preferredName));
    }

    if (! existingSourceDirectory.exists() && ! existingSourceDirectory.createDirectory())
    {
        errorMessage = "Unable to create cache directory for the selected file.";
        return false;
    }

    if (! writeSourceMetadata(existingSourceDirectory, audioFile))
    {
        errorMessage = "Unable to store cache metadata for the selected file.";
        return false;
    }

    return ensureCachedSourceFile(audioFile, cachedSourceFile, errorMessage);
}

bool DemucsProcessor::resolveCachedSourceEntry(const juce::String& entryName,
                                               juce::File& cachedSourceFile,
                                               juce::String& errorMessage) const
{
    errorMessage.clear();
    const auto sourceDirectory = getCacheRootDirectory().getChildFile(entryName);
    cachedSourceFile = getCachedSourceFile(sourceDirectory);

    if (! cachedSourceFile.existsAsFile())
    {
        errorMessage = "Cached source file not found for " + entryName;
        return false;
    }

    return true;
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
        juce::File sourceDirectory;
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
                sourceDirectory = selectedCacheDirectory;
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
        const auto stemDirectory = getStemCacheDirectory(modelName, sourceDirectory);
        std::shared_ptr<SeparatedAudioData> separatedResult;

        if (migrateLegacyStemCache(stemDirectory, errorMessage) && areStemFilesCached(stemDirectory))
        {
            separatedResult = loadSeparatedAudioFromCache(stemDirectory, generation, errorMessage);
        }
        else if (errorMessage.isEmpty())
        {
            juce::File stagedInputFile;
            if (ensureCachedSourceFile(audioFile, stagedInputFile, errorMessage)
                && runDemucsCli(modelName, stemDirectory, stagedInputFile, errorMessage))
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

juce::File DemucsProcessor::getCacheRootDirectory() const
{
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library")
        .getChildFile("Application Support")
        .getChildFile("Jam-PT")
        .getChildFile("DemucsCache");
}

juce::File DemucsProcessor::getSourceMetadataFile(const juce::File& sourceDirectory) const
{
    return sourceDirectory.getChildFile("cache-info.xml");
}

juce::File DemucsProcessor::getCachedSourceFile(const juce::File& sourceDirectory) const
{
    if (! sourceDirectory.isDirectory())
        return {};

    const auto files = sourceDirectory.findChildFiles(juce::File::findFiles, false, "source.*");
    if (files.isEmpty())
        return {};

    return files.getFirst();
}

juce::File DemucsProcessor::getSpectrogramCacheFile(const juce::File& sourceDirectory) const
{
    if (sourceDirectory == juce::File())
        return {};

    return sourceDirectory.getChildFile("spectrogram.thumb");
}

juce::File DemucsProcessor::getStemCacheDirectory(const juce::String& modelName, const juce::File& sourceDirectory) const
{
    return sourceDirectory.getChildFile(modelName);
}

juce::Array<double> DemucsProcessor::loadMarkersFromMetadata(const juce::File& sourceDirectory) const
{
    juce::Array<double> loadedMarkers;
    const auto metadataFile = getSourceMetadataFile(sourceDirectory);
    if (! metadataFile.existsAsFile())
        return loadedMarkers;

    auto xml = juce::XmlDocument::parse(metadataFile);
    if (xml == nullptr || ! xml->hasTagName("JamPTCachedSource"))
        return loadedMarkers;

    if (auto* markersElement = xml->getChildByName("Markers"))
    {
        for (auto* markerElement = markersElement->getFirstChildElement(); markerElement != nullptr; markerElement = markerElement->getNextElement())
        {
            if (! markerElement->hasTagName("Marker"))
                continue;

            const auto positionSeconds = markerElement->getDoubleAttribute("positionSeconds", -1.0);
            if (positionSeconds > markerToleranceSeconds)
                loadedMarkers.add(positionSeconds);
        }
    }

    loadedMarkers.sort();
    return loadedMarkers;
}

bool DemucsProcessor::saveMarkersToMetadata(const juce::File& sourceDirectory, const juce::Array<double>& markersToSave) const
{
    const auto metadataFile = getSourceMetadataFile(sourceDirectory);
    std::unique_ptr<juce::XmlElement> xml;

    if (metadataFile.existsAsFile())
        xml = juce::XmlDocument::parse(metadataFile);

    if (xml == nullptr || ! xml->hasTagName("JamPTCachedSource"))
        xml = std::make_unique<juce::XmlElement>("JamPTCachedSource");

    xml->removeChildElement(xml->getChildByName("Markers"), true);

    auto markersElement = std::make_unique<juce::XmlElement>("Markers");
    for (const auto markerPosition : markersToSave)
    {
        auto markerElement = std::make_unique<juce::XmlElement>("Marker");
        markerElement->setAttribute("positionSeconds", juce::String(markerPosition, 6));
        markersElement->addChildElement(markerElement.release());
    }

    xml->addChildElement(markersElement.release());
    return xml->writeTo(metadataFile);
}

juce::String DemucsProcessor::getUniqueSourceDirectoryName(const juce::String& preferredName) const
{
    auto candidate = preferredName;
    auto suffix = 2;
    const auto cacheRoot = getCacheRootDirectory();

    while (cacheRoot.getChildFile(candidate).exists())
    {
        candidate = preferredName + " (" + juce::String(suffix) + ")";
        ++suffix;
    }

    return candidate;
}

juce::File DemucsProcessor::findExistingSourceDirectory(const juce::File& audioFile) const
{
    const auto cacheRoot = getCacheRootDirectory();
    for (const auto& sourceDirectory : findDirectChildDirectories(cacheRoot))
    {
        if (matchesSourceMetadata(sourceDirectory, audioFile))
            return sourceDirectory;
    }

    return {};
}

bool DemucsProcessor::writeSourceMetadata(const juce::File& sourceDirectory, const juce::File& originalAudioFile) const
{
    std::unique_ptr<juce::XmlElement> metadata;
    const auto metadataFile = getSourceMetadataFile(sourceDirectory);

    if (metadataFile.existsAsFile())
        metadata = juce::XmlDocument::parse(metadataFile);

    if (metadata == nullptr || ! metadata->hasTagName("JamPTCachedSource"))
        metadata = std::make_unique<juce::XmlElement>("JamPTCachedSource");

    metadata->setAttribute("originalFileName", originalAudioFile.getFileName());
    metadata->setAttribute("originalSize", juce::String(originalAudioFile.getSize()));
    metadata->setAttribute("originalModified",
                           juce::String(originalAudioFile.getLastModificationTime().toMilliseconds()));
    metadata->setAttribute("cachedSourceFileName", "source" + originalAudioFile.getFileExtension());
    metadata->setAttribute("spectrogramFileName", "spectrogram.thumb");
    return metadata->writeTo(metadataFile);
}

bool DemucsProcessor::matchesSourceMetadata(const juce::File& sourceDirectory, const juce::File& audioFile) const
{
    const auto metadataFile = getSourceMetadataFile(sourceDirectory);
    if (! metadataFile.existsAsFile())
        return false;

    auto xml = juce::XmlDocument::parse(metadataFile);
    if (xml == nullptr || ! xml->hasTagName("JamPTCachedSource"))
        return false;

    return xml->getStringAttribute("originalFileName") == audioFile.getFileName()
        && xml->getStringAttribute("originalSize") == juce::String(audioFile.getSize())
        && xml->getStringAttribute("originalModified") == juce::String(audioFile.getLastModificationTime().toMilliseconds());
}

bool DemucsProcessor::migrateLegacyStemCache(const juce::File& stemDirectory, juce::String& errorMessage) const
{
    errorMessage.clear();

    if (! stemDirectory.isDirectory())
        return true;

    for (const auto stem : stemOrder)
    {
        const auto flacStemFile = stemDirectory.getChildFile(getStemFileName(stem));
        const auto legacyStemFile = stemDirectory.getChildFile(getLegacyStemFileName(stem));

        if (flacStemFile.existsAsFile())
        {
            if (legacyStemFile.existsAsFile() && ! legacyStemFile.deleteFile())
            {
                errorMessage = "Unable to remove legacy cached stem " + legacyStemFile.getFileName();
                return false;
            }

            continue;
        }

        if (legacyStemFile.existsAsFile() && ! convertStemToCacheFormat(legacyStemFile, flacStemFile, errorMessage))
            return false;
    }

    return true;
}

bool DemucsProcessor::areStemFilesCached(const juce::File& stemDirectory) const
{
    for (const auto stem : stemOrder)
    {
        if (! getCachedStemFile(stemDirectory, stem).existsAsFile())
            return false;
    }

    return true;
}

bool DemucsProcessor::ensureCachedSourceFile(const juce::File& audioFile,
                                             juce::File& cachedSourceFile,
                                             juce::String& errorMessage) const
{
    if (! audioFile.existsAsFile())
    {
        errorMessage = "The selected audio file does not exist.";
        return false;
    }

    auto sourceDirectory = audioFile.getParentDirectory();
    auto sourceFile = audioFile;

    if (! audioFile.getFileName().startsWith("source."))
    {
        sourceDirectory = findExistingSourceDirectory(audioFile);
        if (sourceDirectory == juce::File())
            sourceDirectory = getCacheRootDirectory().getChildFile(getUniqueSourceDirectoryName(audioFile.getFileName()));

        sourceFile = sourceDirectory.getChildFile("source" + audioFile.getFileExtension());
    }

    const auto parent = sourceFile.getParentDirectory();

    if (! parent.exists() && ! parent.createDirectory())
    {
        errorMessage = "Unable to create source cache directory.";
        return false;
    }

    if (! audioFile.getFileName().startsWith("source.") && ! writeSourceMetadata(parent, audioFile))
    {
        errorMessage = "Unable to update source cache metadata.";
        return false;
    }

    if (sourceFile.existsAsFile())
    {
        cachedSourceFile = sourceFile;
        return true;
    }

    if (! audioFile.copyFileTo(sourceFile))
    {
        errorMessage = "Unable to copy the source audio into the cache.";
        return false;
    }

    cachedSourceFile = sourceFile;
    return true;
}

bool DemucsProcessor::runDemucsCli(const juce::String& modelName,
                                   const juce::File& stemDirectory,
                                   const juce::File& stagedInputFile,
                                   juce::String& errorMessage)
{
    errorMessage.clear();
    const auto stagedSourceBaseName = stagedInputFile.getFileNameWithoutExtension();
    const auto demucsExecutable = resolveDemucsExecutable();
    if (demucsExecutable.isEmpty())
    {
        errorMessage = "The `demucs` command was not found. Install it with pipx or add it to PATH.";
        return false;
    }

    const auto sourceDirectory = stagedInputFile.getParentDirectory();
    if (! sourceDirectory.exists() && ! sourceDirectory.createDirectory())
    {
        errorMessage = "Unable to create demucs cache directory.";
        return false;
    }

    if (! stemDirectory.exists() && ! stemDirectory.createDirectory())
    {
        errorMessage = "Unable to create model cache directory.";
        return false;
    }

    auto process = std::make_unique<juce::ChildProcess>();
    const juce::StringArray command {
        demucsExecutable,
        "-n", modelName,
        "-o", sourceDirectory.getFullPathName(),
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
        bufferProgress = 0.02;
        bufferStatusText = "Separating stems with demucs";
        lastProcessLog = "Launching: " + formatCommandForShell(command);
    }

    juce::String processOutput;
    const auto processStartTimeMs = juce::Time::getMillisecondCounterHiRes();
    auto parseProgressFromLog = [](const juce::String& logText) -> double
    {
        auto bestPercent = -1.0;

        for (int index = 0; index < logText.length(); ++index)
        {
            if (logText[index] != '%')
                continue;

            auto start = index;
            while (start > 0)
            {
                const auto character = logText[start - 1];
                if (juce::CharacterFunctions::isDigit(character) || character == '.')
                    --start;
                else
                    break;
            }

            if (start == index)
                continue;

            const auto percentText = logText.substring(start, index).trim();
            const auto percentValue = percentText.getDoubleValue();
            if (percentValue >= 0.0 && percentValue <= 100.0)
                bestPercent = juce::jmax(bestPercent, percentValue);
        }

        return bestPercent;
    };

    auto updateProcessLog = [this, processStartTimeMs](const juce::String& logText, double parsedPercent)
    {
        const juce::ScopedLock lock(stateLock);
        lastProcessLog = logText.length() > 4000 ? logText.fromLastOccurrenceOf("\n", false, false).trim() : logText;

        double newProgress = bufferProgress;
        if (parsedPercent >= 0.0)
        {
            newProgress = juce::jmax(newProgress,
                                     juce::jmap(parsedPercent, 0.0, 100.0, 0.05, 0.98));
        }
        else
        {
            const auto elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - processStartTimeMs) / 1000.0;
            const auto estimatedProgress = juce::jlimit(0.05, 0.9, 0.05 + (elapsedSeconds / 60.0) * 0.85);
            newProgress = juce::jmax(newProgress, estimatedProgress);
        }

        bufferProgress = newProgress;
    };

    auto drainChildOutput = [&processOutput, &updateProcessLog, &parseProgressFromLog](juce::ChildProcess& child)
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
                updateProcessLog(trimmed, parseProgressFromLog(trimmed));
        }
        else
        {
            const auto trimmed = processOutput.trim();
            if (trimmed.isNotEmpty())
                updateProcessLog(trimmed, parseProgressFromLog(trimmed));
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
    const auto finalLog = processOutput.isNotEmpty() ? processOutput
                                                     : "Demucs finished without console output.";
    updateProcessLog(finalLog, parseProgressFromLog(finalLog));

    if (threadShouldExit())
    {
        errorMessage = "Demucs process interrupted.";
        cleanupDemucsTemporaryStemFiles(stemDirectory, stagedSourceBaseName);
        return false;
    }

    if (processOutput.containsIgnoreCase("No module named 'torchcodec'")
        || processOutput.containsIgnoreCase("TorchCodec is required"))
    {
        errorMessage = "Demucs runtime is missing Python package 'torchcodec'. "
                       "Install it into the pipx demucs environment, for example with: "
                       "`pipx inject demucs torchcodec`";
        cleanupDemucsTemporaryStemFiles(stemDirectory, stagedSourceBaseName);
        return false;
    }

    if (exitCode != 0)
    {
        errorMessage = processOutput.isNotEmpty() ? processOutput : "Demucs process failed.";
        cleanupDemucsTemporaryStemFiles(stemDirectory, stagedSourceBaseName);
        return false;
    }

    const auto normalised = normaliseDemucsStemLayout(stemDirectory, stagedSourceBaseName, errorMessage);
    cleanupDemucsTemporaryStemFiles(stemDirectory, stagedSourceBaseName);
    return normalised;
}

bool DemucsProcessor::normaliseDemucsStemLayout(const juce::File& stemDirectory,
                                                const juce::String& stagedSourceBaseName,
                                                juce::String& errorMessage) const
{
    errorMessage.clear();
    const auto nestedStemDirectory = stemDirectory.getChildFile(stagedSourceBaseName);

    if (! nestedStemDirectory.isDirectory())
        return areStemFilesCached(stemDirectory);

    for (const auto stem : stemOrder)
    {
        const auto sourceStemFile = nestedStemDirectory.getChildFile(getDemucsOutputStemFileName(stem));
        const auto targetStemFile = stemDirectory.getChildFile(getStemFileName(stem));

        if (! sourceStemFile.existsAsFile())
        {
            errorMessage = "Missing demucs output stem " + sourceStemFile.getFileName();
            return false;
        }

        if (targetStemFile.existsAsFile() && ! targetStemFile.deleteFile())
        {
            errorMessage = "Unable to replace cached stem " + targetStemFile.getFileName();
            return false;
        }

        const auto legacyStemFile = stemDirectory.getChildFile(getLegacyStemFileName(stem));
        if (legacyStemFile.existsAsFile() && ! legacyStemFile.deleteFile())
        {
            errorMessage = "Unable to remove legacy cached stem " + legacyStemFile.getFileName();
            return false;
        }

        if (! convertStemToCacheFormat(sourceStemFile, targetStemFile, errorMessage))
            return false;
    }

    nestedStemDirectory.deleteRecursively();
    return areStemFilesCached(stemDirectory);
}

bool DemucsProcessor::convertStemToCacheFormat(const juce::File& sourceStemFile,
                                               const juce::File& targetStemFile,
                                               juce::String& errorMessage) const
{
    errorMessage.clear();

    juce::AudioFormatManager conversionFormatManager;
    conversionFormatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(conversionFormatManager.createReaderFor(sourceStemFile));
    if (reader == nullptr)
    {
        errorMessage = "Unable to read demucs output stem " + sourceStemFile.getFileName();
        return false;
    }

    if (reader->sampleRate <= 0.0 || reader->numChannels <= 0)
    {
        errorMessage = "Invalid audio format for demucs output stem " + sourceStemFile.getFileName();
        return false;
    }

    auto outputStream = std::make_unique<juce::FileOutputStream>(targetStemFile);
    if (! outputStream->openedOk())
    {
        errorMessage = "Unable to create cached stem " + targetStemFile.getFileName();
        return false;
    }

    juce::FlacAudioFormat flacFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(flacFormat.createWriterFor(outputStream.get(),
                                                                               reader->sampleRate,
                                                                               static_cast<unsigned int>(reader->numChannels),
                                                                               static_cast<unsigned int>(reader->bitsPerSample > 0 ? reader->bitsPerSample : 24),
                                                                               {},
                                                                               0));
    if (writer == nullptr)
    {
        errorMessage = "Unable to create FLAC writer for " + targetStemFile.getFileName();
        return false;
    }

    outputStream.release();

    constexpr int samplesPerChunk = 32768;
    juce::AudioBuffer<float> tempBuffer(static_cast<int>(reader->numChannels), samplesPerChunk);

    juce::int64 samplesRemaining = reader->lengthInSamples;
    juce::int64 currentSample = 0;
    while (samplesRemaining > 0)
    {
        const auto samplesThisChunk = static_cast<int>(juce::jmin<juce::int64>(samplesRemaining, samplesPerChunk));
        tempBuffer.clear();

        if (! reader->read(&tempBuffer, 0, samplesThisChunk, currentSample, true, true))
        {
            errorMessage = "Unable to read audio while converting " + sourceStemFile.getFileName();
            return false;
        }

        if (! writer->writeFromAudioSampleBuffer(tempBuffer, 0, samplesThisChunk))
        {
            errorMessage = "Unable to write FLAC stem " + targetStemFile.getFileName();
            return false;
        }

        currentSample += samplesThisChunk;
        samplesRemaining -= samplesThisChunk;
    }

    writer.reset();

    if (! sourceStemFile.deleteFile())
    {
        errorMessage = "Converted stem to FLAC but could not remove temporary file " + sourceStemFile.getFileName();
        return false;
    }

    return true;
}

void DemucsProcessor::cleanupDemucsTemporaryStemFiles(const juce::File& stemDirectory,
                                                      const juce::String& stagedSourceBaseName) const
{
    const auto nestedStemDirectory = stemDirectory.getChildFile(stagedSourceBaseName);
    if (nestedStemDirectory.exists())
        nestedStemDirectory.deleteRecursively();

    for (const auto stem : stemOrder)
    {
        const auto legacyStemFile = stemDirectory.getChildFile(getLegacyStemFileName(stem));
        if (legacyStemFile.existsAsFile())
            legacyStemFile.deleteFile();
    }
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
        const auto stemFile = getCachedStemFile(stemDirectory, stem);

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
        case Stem::vocals: return "vocals.flac";
        case Stem::drums: return "drums.flac";
        case Stem::bass: return "bass.flac";
        case Stem::other: return "other.flac";
        case Stem::count: break;
    }

    jassertfalse;
    return {};
}

juce::String DemucsProcessor::getDemucsOutputStemFileName(Stem stem)
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

juce::String DemucsProcessor::getLegacyStemFileName(Stem stem)
{
    return getDemucsOutputStemFileName(stem);
}

juce::File DemucsProcessor::getCachedStemFile(const juce::File& stemDirectory, Stem stem)
{
    const auto flacFile = stemDirectory.getChildFile(getStemFileName(stem));
    if (flacFile.existsAsFile())
        return flacFile;

    return stemDirectory.getChildFile(getLegacyStemFileName(stem));
}
