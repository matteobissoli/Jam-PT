#include "CoreMLProcessor.h"

#import <CoreML/CoreML.h>
#import <Foundation/Foundation.h>

CoreMLProcessor::CoreMLProcessor() = default;
CoreMLProcessor::~CoreMLProcessor() = default;

bool CoreMLProcessor::loadModel(const juce::File& modelFile, juce::String& errorMessage)
{
    errorMessage.clear();
    loaded = false;
    loadedModelName.clear();

    if (! modelFile.exists())
    {
        errorMessage = "Model file not found.";
        return false;
    }

    @autoreleasepool
    {
        NSString* modelPath = [NSString stringWithUTF8String: modelFile.getFullPathName().toRawUTF8()];
        NSURL* modelURL = [NSURL fileURLWithPath:modelPath];
        NSError* compileError = nil;
        NSURL* compiledURL = modelURL;

        const auto extension = modelFile.getFileExtension().toLowerCase();
        if (extension == ".mlmodel")
        {
            compiledURL = [MLModel compileModelAtURL:modelURL error:&compileError];
            if (compiledURL == nil)
            {
                NSString* description = compileError.localizedDescription != nil ? compileError.localizedDescription : @"Unknown error";
                errorMessage = "Unable to compile Core ML model: " + juce::String::fromUTF8([description UTF8String]);
                return false;
            }
        }

        NSError* loadError = nil;
        MLModelConfiguration* configuration = [[MLModelConfiguration alloc] init];
        configuration.computeUnits = MLComputeUnitsAll;
        MLModel* model = [MLModel modelWithContentsOfURL:compiledURL configuration:configuration error:&loadError];
        if (model == nil)
        {
            NSString* description = loadError.localizedDescription != nil ? loadError.localizedDescription : @"Unknown error";
            errorMessage = "Unable to load Core ML model: " + juce::String::fromUTF8([description UTF8String]);
            return false;
        }

        loaded = true;
        loadedModelName = modelFile.getFileName();
        (void) model;
    }

    return true;
}

bool CoreMLProcessor::isModelLoaded() const
{
    return loaded;
}

juce::String CoreMLProcessor::getLoadedModelName() const
{
    return loadedModelName;
}

void CoreMLProcessor::prepare(double sampleRate, int samplesPerBlock, int numChannels)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;
    currentNumChannels = numChannels;
}

void CoreMLProcessor::reset()
{
}

void CoreMLProcessor::process(juce::AudioBuffer<float>& buffer)
{
    if (! loaded)
        return;

    // Placeholder:
    // here you can transform the incoming JUCE audio buffer into the tensors or
    // MLMultiArray objects expected by the Core ML model, run prediction, then
    // write the processed output back into `buffer`.
    (void) buffer;
}
