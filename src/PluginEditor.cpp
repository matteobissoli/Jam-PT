#include "PluginEditor.h"

void StemKnobLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                           int x,
                                           int y,
                                           int width,
                                           int height,
                                           float sliderPosProportional,
                                           float rotaryStartAngle,
                                           float rotaryEndAngle,
                                           juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    const auto side = juce::jmin(bounds.getWidth(), bounds.getHeight());
    bounds = bounds.withSizeKeepingCentre(side, side).reduced(6.0f);
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    const auto arcRadius = radius - 5.0f;

    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillEllipse(bounds);

    juce::Path backgroundArc;
    backgroundArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.strokePath(backgroundArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto markerNormalised = static_cast<float>((1.0 - slider.getMinimum()) / (slider.getMaximum() - slider.getMinimum()));
    const auto markerAngle = rotaryStartAngle + markerNormalised * (rotaryEndAngle - rotaryStartAngle);
    juce::Line<float> markerLine(centre.getPointOnCircumference(arcRadius - 1.0f, markerAngle),
                                 centre.getPointOnCircumference(arcRadius + 7.0f, markerAngle));
    g.setColour(juce::Colour::fromRGB(255, 208, 96).withAlpha(0.95f));
    g.drawLine(markerLine, 2.0f);

    juce::Path valueArc;
    valueArc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, angle, true);
    g.setColour(juce::Colour::fromRGB(96, 208, 160).withAlpha(0.95f));
    g.strokePath(valueArc, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(juce::Colour::fromRGB(56, 56, 62));
    g.fillEllipse(bounds.reduced(12.0f));

    juce::Line<float> pointerLine(centre,
                                  centre.getPointOnCircumference(radius - 18.0f, angle));
    g.setColour(juce::Colours::white.withAlpha(0.95f));
    g.drawLine(pointerLine, 3.0f);
}

WaveformScrubber::WaveformScrubber()
{
    formatManager.registerBasicFormats();
}

void WaveformScrubber::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.fillRoundedRectangle(bounds, 8.0f);

    if (thumbnail.getTotalLength() <= 0.0)
    {
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.drawFittedText("Load an audio file to view the waveform", getLocalBounds(), juce::Justification::centred, 1);
        return;
    }

    auto waveformBounds = bounds.reduced(8.0f, 6.0f);
    const auto waveformBoundsInt = waveformBounds.getSmallestIntegerContainer();
    g.reduceClipRegion(getLocalBounds());
    g.setColour(juce::Colours::white.withAlpha(0.2f));
    thumbnail.drawChannels(g, waveformBoundsInt, 0.0, thumbnail.getTotalLength(), 1.0f);

    const auto playedWidth = waveformBounds.getWidth() * static_cast<float>(playbackProgress);
    if (playedWidth > 0.0f)
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(waveformBounds.withWidth(playedWidth).getSmallestIntegerContainer());
        g.setColour(juce::Colour::fromRGB(96, 208, 160).withAlpha(0.9f));
        thumbnail.drawChannels(g, waveformBoundsInt, 0.0, thumbnail.getTotalLength(), 1.0f);
    }

    const auto playheadX = waveformBounds.getX() + playedWidth;
    g.setColour(juce::Colours::white.withAlpha(0.95f));
    g.drawLine(playheadX, waveformBounds.getY(), playheadX, waveformBounds.getBottom(), 2.0f);

    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
}

void WaveformScrubber::mouseDown(const juce::MouseEvent& event)
{
    handleSeek(event.position.x);
}

void WaveformScrubber::mouseDrag(const juce::MouseEvent& event)
{
    handleSeek(event.position.x);
}

void WaveformScrubber::setAudioFile(const juce::File& file)
{
    if (! file.existsAsFile())
    {
        clear();
        return;
    }

    thumbnail.clear();
    thumbnail.setSource(new juce::FileInputSource(file));
    repaint();
}

void WaveformScrubber::clear()
{
    thumbnail.clear();
    playbackProgress = 0.0;
    repaint();
}

void WaveformScrubber::setPlaybackProgress(double newProgress)
{
    playbackProgress = juce::jlimit(0.0, 1.0, newProgress);
    repaint();
}

void WaveformScrubber::handleSeek(float xPosition)
{
    if (thumbnail.getTotalLength() <= 0.0 || onSeek == nullptr)
        return;

    const auto bounds = getLocalBounds().toFloat().reduced(8.0f, 6.0f);
    if (bounds.getWidth() <= 0.0f)
        return;

    const auto normalised = juce::jlimit(0.0f, 1.0f, (xPosition - bounds.getX()) / bounds.getWidth());
    onSeek(static_cast<double>(normalised) * thumbnail.getTotalLength());
}

JamPTAudioProcessorEditor::JamPTAudioProcessorEditor(JamPTAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), valueTreeState(p.getValueTreeState())
{
    titleLabel.setText("Jam-PT", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    openAudioButton.addListener(this);
    addAndMakeVisible(openAudioButton);

    playbackButton.addListener(this);
    addAndMakeVisible(playbackButton);

    stopButton.addListener(this);
    addAndMakeVisible(stopButton);

    openModelButton.setButtonText("Select Demucs model");
    openModelButton.addListener(this);
    addAndMakeVisible(openModelButton);

    audioStatusLabel.setJustificationType(juce::Justification::centredLeft);
    positionLabel.setJustificationType(juce::Justification::centredLeft);
    durationLabel.setJustificationType(juce::Justification::centredRight);
    bufferStatusLabel.setJustificationType(juce::Justification::centredLeft);
    footerLabel.setJustificationType(juce::Justification::centred);

    footerLabel.setText("Offline Demucs CLI separation with cached stems in Application Support", juce::dontSendNotification);

    configureStemKnob(vocalsSlider, "Vocals", DemucsProcessor::Stem::vocals);
    configureStemKnob(drumsSlider, "Drums", DemucsProcessor::Stem::drums);
    configureStemKnob(bassSlider, "Bass", DemucsProcessor::Stem::bass);
    configureStemKnob(otherSlider, "Other", DemucsProcessor::Stem::other);

    vocalsAttachment = std::make_unique<SliderAttachment>(valueTreeState,
                                                          JamPTAudioProcessor::getStemParameterId(DemucsProcessor::Stem::vocals),
                                                          vocalsSlider);
    drumsAttachment = std::make_unique<SliderAttachment>(valueTreeState,
                                                         JamPTAudioProcessor::getStemParameterId(DemucsProcessor::Stem::drums),
                                                         drumsSlider);
    bassAttachment = std::make_unique<SliderAttachment>(valueTreeState,
                                                        JamPTAudioProcessor::getStemParameterId(DemucsProcessor::Stem::bass),
                                                        bassSlider);
    otherAttachment = std::make_unique<SliderAttachment>(valueTreeState,
                                                         JamPTAudioProcessor::getStemParameterId(DemucsProcessor::Stem::other),
                                                         otherSlider);

    addAndMakeVisible(audioStatusLabel);
    addAndMakeVisible(positionLabel);
    addAndMakeVisible(durationLabel);
    addAndMakeVisible(bufferStatusLabel);
    addAndMakeVisible(modelBufferProgressBar);
    addAndMakeVisible(vocalsLabel);
    addAndMakeVisible(drumsLabel);
    addAndMakeVisible(bassLabel);
    addAndMakeVisible(otherLabel);
    addAndMakeVisible(vocalsSlider);
    addAndMakeVisible(drumsSlider);
    addAndMakeVisible(bassSlider);
    addAndMakeVisible(otherSlider);
    addAndMakeVisible(waveformScrubber);
    addAndMakeVisible(footerLabel);

    waveformScrubber.onSeek = [this](double positionSeconds)
    {
        audioProcessor.setPlaybackPositionSeconds(positionSeconds);
        refreshLabels();
    };

    if (const auto loadedAudioFile = audioProcessor.getLoadedAudioFile(); loadedAudioFile.existsAsFile())
        waveformScrubber.setAudioFile(loadedAudioFile);

    startTimerHz(10);
    refreshLabels();
    setSize(620, 470);
}

JamPTAudioProcessorEditor::~JamPTAudioProcessorEditor()
{
    stopTimer();
    activeFileChooser.reset();
    vocalsAttachment.reset();
    drumsAttachment.reset();
    bassAttachment.reset();
    otherAttachment.reset();
    waveformScrubber.onSeek = nullptr;

    vocalsSlider.setLookAndFeel(nullptr);
    drumsSlider.setLookAndFeel(nullptr);
    bassSlider.setLookAndFeel(nullptr);
    otherSlider.setLookAndFeel(nullptr);

    openAudioButton.removeListener(this);
    playbackButton.removeListener(this);
    stopButton.removeListener(this);
    openModelButton.removeListener(this);
}

void JamPTAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(24, 24, 28));

    auto bounds = getLocalBounds().reduced(16);
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.fillRoundedRectangle(bounds.toFloat(), 12.0f);

    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.drawRoundedRectangle(bounds.toFloat(), 12.0f, 1.0f);
}

void JamPTAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(28);
    titleLabel.setBounds(area.removeFromTop(36));
    area.removeFromTop(14);

    auto row1 = area.removeFromTop(30);
    openAudioButton.setBounds(row1.removeFromLeft(180));
    row1.removeFromLeft(12);
    audioStatusLabel.setBounds(row1);

    area.removeFromTop(12);
    auto row2 = area.removeFromTop(30);
    playbackButton.setBounds(row2.removeFromLeft(120));
    row2.removeFromLeft(12);
    stopButton.setBounds(row2.removeFromLeft(120));

    area.removeFromTop(12);
    auto row3 = area.removeFromTop(22);
    positionLabel.setBounds(row3.removeFromLeft(72));
    durationLabel.setBounds(row3.removeFromRight(72));

    area.removeFromTop(8);
    waveformScrubber.setBounds(area.removeFromTop(56));

    area.removeFromTop(12);
    bufferStatusLabel.setBounds(area.removeFromTop(22));

    area.removeFromTop(6);
    modelBufferProgressBar.setBounds(area.removeFromTop(18));

    area.removeFromTop(16);
    auto knobsArea = area.removeFromTop(160);
    auto knobRow = knobsArea;
    auto knob1 = knobRow.removeFromLeft(knobRow.getWidth() / 4).reduced(8, 0);
    auto knob2 = knobRow.removeFromLeft(knobRow.getWidth() / 3).reduced(8, 0);
    auto knob3 = knobRow.removeFromLeft(knobRow.getWidth() / 2).reduced(8, 0);
    auto knob4 = knobRow.reduced(8, 0);

    auto placeKnob = [](juce::Rectangle<int> bounds, juce::Label& label, juce::Slider& knob)
    {
        label.setBounds(bounds.removeFromTop(22));
        const auto knobSide = juce::jmin(bounds.getWidth(), bounds.getHeight() - 24);
        auto knobBounds = bounds.removeFromTop(juce::jmax(0, knobSide + 24));
        knobBounds = knobBounds.withSizeKeepingCentre(knobSide, knobSide + 24);
        knob.setBounds(knobBounds);
    };

    placeKnob(knob1, vocalsLabel, vocalsSlider);
    placeKnob(knob2, drumsLabel, drumsSlider);
    placeKnob(knob3, bassLabel, bassSlider);
    placeKnob(knob4, otherLabel, otherSlider);

    area.removeFromTop(12);
    auto row4 = area.removeFromTop(30);
    openModelButton.setBounds(row4.removeFromLeft(180));
    row4.removeFromLeft(12);
    footerLabel.setBounds(row4);
}

void JamPTAudioProcessorEditor::buttonClicked(juce::Button* button)
{
    if (button == &openAudioButton)
        launchAudioFileChooser();
    else if (button == &playbackButton)
        handlePlaybackButton();
    else if (button == &stopButton)
        handleStopButton();
    else if (button == &openModelButton)
        launchModelFileChooser();
}

void JamPTAudioProcessorEditor::timerCallback()
{
    refreshLabels();
}

void JamPTAudioProcessorEditor::refreshLabels()
{
    audioProcessor.refreshBackendStateFromLoadedFile();

    const auto audioFile = audioProcessor.getLoadedAudioFileName();
    const auto modelName = audioProcessor.getLoadedModelName();
    juce::String playbackStatus = "stopped";

    switch (audioProcessor.getPlaybackState())
    {
        case AudioFilePlayer::PlaybackState::playing:
            playbackStatus = "playing";
            break;
        case AudioFilePlayer::PlaybackState::paused:
            playbackStatus = "paused";
            break;
        case AudioFilePlayer::PlaybackState::stopped:
            break;
    }

    audioStatusLabel.setText(audioFile.isNotEmpty() ? "Audio: " + audioFile + " (" + playbackStatus + ")" : "Audio: no file loaded",
                             juce::dontSendNotification);
    openModelButton.setButtonText("Model: " + (modelName.isNotEmpty() ? modelName : "select"));
    playbackButton.setButtonText(audioProcessor.getPlaybackState() == AudioFilePlayer::PlaybackState::playing ? "PAUSE" : "PLAY");

    const auto hasAudioFile = audioFile.isNotEmpty();
    const auto stemsReady = audioProcessor.isStemSeparationReady();
    const auto transportEnabled = hasAudioFile && stemsReady;
    playbackButton.setEnabled(transportEnabled);
    stopButton.setEnabled(transportEnabled);
    waveformScrubber.setPlaybackProgress(audioProcessor.getPlaybackProgress());
    positionLabel.setText(formatTime(audioProcessor.getPlaybackPositionSeconds()), juce::dontSendNotification);
    durationLabel.setText(formatTime(audioProcessor.getPlaybackDurationSeconds()), juce::dontSendNotification);
    waveformScrubber.setEnabled(hasAudioFile && stemsReady);
    modelBufferProgressValue = audioProcessor.getModelBufferProgress();
    const auto separationStatus = audioProcessor.getModelBufferStatusText();
    const auto hasFailure = separationStatus.startsWithIgnoreCase("Failed:");
    const auto summaryStatus = hasFailure ? "Separation failed" : separationStatus;
    bufferStatusLabel.setText("Separation: " + summaryStatus, juce::dontSendNotification);
    vocalsSlider.setEnabled(stemsReady);
    drumsSlider.setEnabled(stemsReady);
    bassSlider.setEnabled(stemsReady);
    otherSlider.setEnabled(stemsReady);

    const auto enabledAlpha = stemsReady ? 1.0f : 0.4f;
    vocalsLabel.setAlpha(enabledAlpha);
    drumsLabel.setAlpha(enabledAlpha);
    bassLabel.setAlpha(enabledAlpha);
    otherLabel.setAlpha(enabledAlpha);
    vocalsSlider.setAlpha(enabledAlpha);
    drumsSlider.setAlpha(enabledAlpha);
    bassSlider.setAlpha(enabledAlpha);
    otherSlider.setAlpha(enabledAlpha);
}

void JamPTAudioProcessorEditor::handlePlaybackButton()
{
    const auto playbackState = audioProcessor.getPlaybackState();
    const bool success = playbackState == AudioFilePlayer::PlaybackState::playing
                           ? audioProcessor.pausePlayback()
                           : audioProcessor.startPlayback();

    if (! success)
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "Jam-PT",
                                               audioProcessor.hasSeparationFailed()
                                                   ? "Stem separation failed. Load another model or audio file and try again."
                                                   : "Wait for stem separation to complete before using playback controls.");

    refreshLabels();
}

void JamPTAudioProcessorEditor::handleStopButton()
{
    audioProcessor.stopPlayback();
    refreshLabels();
}

void JamPTAudioProcessorEditor::configureStemKnob(juce::Slider& slider,
                                                  const juce::String& name,
                                                  DemucsProcessor::Stem stem)
{
    auto* label = &vocalsLabel;

    if (stem == DemucsProcessor::Stem::drums)
        label = &drumsLabel;
    else if (stem == DemucsProcessor::Stem::bass)
        label = &bassLabel;
    else if (stem == DemucsProcessor::Stem::other)
        label = &otherLabel;

    label->setText(name, juce::dontSendNotification);
    label->setJustificationType(juce::Justification::centred);

    slider.setLookAndFeel(&stemKnobLookAndFeel);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                               juce::MathConstants<float>::pi * 2.8f,
                               true);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 20);
    slider.setRange(0.0, 120.0, 1.0);
    slider.setDoubleClickReturnValue(true, 100.0);
    slider.setNumDecimalPlacesToDisplay(0);
}

juce::String JamPTAudioProcessorEditor::formatTime(double seconds)
{
    if (seconds <= 0.0)
        return "00:00";

    const auto totalSeconds = static_cast<int>(std::round(seconds));
    const auto minutes = totalSeconds / 60;
    const auto remainingSeconds = totalSeconds % 60;
    return juce::String::formatted("%02d:%02d", minutes, remainingSeconds);
}

void JamPTAudioProcessorEditor::launchAudioFileChooser()
{
    activeFileChooser = std::make_unique<juce::FileChooser>("Select an audio file");

    auto* chooser = activeFileChooser.get();
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [safeThis = juce::Component::SafePointer<JamPTAudioProcessorEditor>(this)](const juce::FileChooser& fileChooser)
    {
        if (safeThis == nullptr)
            return;

        const auto result = fileChooser.getResult();
        safeThis->activeFileChooser.reset();

        if (result == juce::File())
            return;

        if (! safeThis->audioProcessor.loadAudioFile(result))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Jam-PT",
                                                   "Unable to load the selected audio file.");
            safeThis->waveformScrubber.clear();
        }
        else
        {
            safeThis->waveformScrubber.setAudioFile(result);
        }

        safeThis->refreshLabels();
    });
}

void JamPTAudioProcessorEditor::launchModelFileChooser()
{
    juce::PopupMenu modelMenu;
    int itemId = 1;
    const auto currentModel = audioProcessor.getLoadedModelName();
    for (const auto& modelName : audioProcessor.getAvailableModelNames())
    {
        modelMenu.addItem(itemId, modelName, true, modelName == currentModel);
        ++itemId;
    }

    modelMenu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&openModelButton),
                            [safeThis = juce::Component::SafePointer<JamPTAudioProcessorEditor>(this)](int selectedId)
    {
        if (safeThis == nullptr || selectedId <= 0)
            return;

        const auto models = safeThis->audioProcessor.getAvailableModelNames();
        const auto modelIndex = selectedId - 1;
        if (! juce::isPositiveAndBelow(modelIndex, models.size()))
            return;

        juce::String error;
        if (! safeThis->audioProcessor.selectModel(models[modelIndex], error))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Jam-PT",
                                                   error);
        }

        safeThis->refreshLabels();
    });
}
