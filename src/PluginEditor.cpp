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

juce::Font CacheSelectorLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return { juce::FontOptions(18.0f, juce::Font::bold) };
}

WaveformScrubber::WaveformScrubber()
{
    formatManager.registerBasicFormats();
    thumbnail.addChangeListener(this);
}

WaveformScrubber::~WaveformScrubber()
{
    thumbnail.removeChangeListener(this);
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

    if (thumbnail.getTotalLength() > 0.0 && ! markers.isEmpty())
    {
        g.setColour(juce::Colour::fromRGB(255, 224, 96).withAlpha(0.95f));
        for (const auto markerPosition : markers)
        {
            const auto markerProgress = juce::jlimit(0.0,
                                                     1.0,
                                                     markerPosition / thumbnail.getTotalLength());
            const auto markerX = waveformBounds.getX()
                               + static_cast<float>(markerProgress) * waveformBounds.getWidth();
            g.drawLine(markerX, waveformBounds.getY(), markerX, waveformBounds.getBottom(), 1.0f);
        }
    }

    if (showSeparationOverlay)
    {
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillRoundedRectangle(waveformBounds, 8.0f);

        const auto spinnerSize = juce::jmin(waveformBounds.getWidth(), waveformBounds.getHeight()) * 0.28f;
        if (spinnerSize > 1.0f)
        {
            const auto spinnerBounds = juce::Rectangle<float>(spinnerSize, spinnerSize).withCentre(waveformBounds.getCentre());
            const auto spinnerCentre = spinnerBounds.getCentre();
            const auto outerRadius = spinnerBounds.getWidth() * 0.5f;
            const auto innerRadius = outerRadius * 0.46f;
            const auto nowSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
            constexpr int spokeCount = 12;
            const auto leadIndex = static_cast<int>(std::floor(nowSeconds * 12.0)) % spokeCount;

            for (int spokeIndex = 0; spokeIndex < spokeCount; ++spokeIndex)
            {
                const auto angle = juce::MathConstants<float>::twoPi
                                 * (static_cast<float>(spokeIndex) / static_cast<float>(spokeCount))
                                 - juce::MathConstants<float>::halfPi;
                const auto distanceFromLead = (spokeIndex - leadIndex + spokeCount) % spokeCount;
                const auto alpha = juce::jmap(static_cast<float>(distanceFromLead),
                                              0.0f,
                                              static_cast<float>(spokeCount - 1),
                                              0.95f,
                                              0.12f);
                const auto innerPoint = spinnerCentre.getPointOnCircumference(innerRadius, angle);
                const auto outerPoint = spinnerCentre.getPointOnCircumference(outerRadius, angle);
                g.setColour(juce::Colours::white.withAlpha(alpha));
                g.drawLine(innerPoint.x, innerPoint.y, outerPoint.x, outerPoint.y, 3.5f);
            }
        }
    }

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

void WaveformScrubber::setAudioFile(const juce::File& file, const juce::File& newSpectrogramCacheFile)
{
    spectrogramCacheFile = newSpectrogramCacheFile;

    if (! file.existsAsFile())
    {
        clear();
        return;
    }

    thumbnail.clear();

    if (spectrogramCacheFile.existsAsFile())
    {
        juce::FileInputStream inputStream(spectrogramCacheFile);
        if (inputStream.openedOk() && thumbnail.loadFrom(inputStream))
        {
            repaint();
            return;
        }
    }

    thumbnail.setSource(new juce::FileInputSource(file));
    repaint();
}

void WaveformScrubber::clear()
{
    thumbnail.clear();
    spectrogramCacheFile = juce::File();
    markers.clear();
    playbackProgress = 0.0;
    repaint();
}

void WaveformScrubber::setPlaybackProgress(double newProgress)
{
    playbackProgress = juce::jlimit(0.0, 1.0, newProgress);
    repaint();
}

void WaveformScrubber::setSeparationOverlay(double progress, bool shouldShow)
{
    separationProgress = juce::jlimit(0.0, 1.0, progress);
    showSeparationOverlay = shouldShow;
    repaint();
}

void WaveformScrubber::setMarkers(const juce::Array<double>& newMarkers)
{
    markers = newMarkers;
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

void WaveformScrubber::saveThumbnailCacheIfReady()
{
    if (spectrogramCacheFile == juce::File()
        || thumbnail.getTotalLength() <= 0.0
        || ! thumbnail.isFullyLoaded())
        return;

    const auto parentDirectory = spectrogramCacheFile.getParentDirectory();
    if (! parentDirectory.exists())
        parentDirectory.createDirectory();

    juce::FileOutputStream outputStream(spectrogramCacheFile);
    if (! outputStream.openedOk())
        return;

    thumbnail.saveTo(outputStream);
}

void WaveformScrubber::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &thumbnail)
        saveThumbnailCacheIfReady();
}

JamPTAudioProcessorEditor::JamPTAudioProcessorEditor(JamPTAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), valueTreeState(p.getValueTreeState())
{
    cachedAudioComboBox.setLookAndFeel(&cacheSelectorLookAndFeel);
    cachedAudioComboBox.setTextWhenNothingSelected("Select a cached file");
    cachedAudioComboBox.addListener(this);
    addAndMakeVisible(cachedAudioComboBox);

    addAudioFileButton.addListener(this);
    addAudioFileButton.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    addAudioFileButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    addAudioFileButton.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(96, 208, 160));
    addAudioFileButton.setColour(juce::TextButton::textColourOnId, juce::Colour::fromRGB(96, 208, 160));
    addAndMakeVisible(addAudioFileButton);

    openCacheFolderButton.addListener(this);
    addAndMakeVisible(openCacheFolderButton);
    openCacheFolderButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(52, 56, 62));

    prevButton.addListener(this);
    prevButton.setEnabled(false);
    addAndMakeVisible(prevButton);
    prevButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(52, 56, 62));

    playbackButton.addListener(this);
    addAndMakeVisible(playbackButton);
    playbackButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(64, 140, 110));
    playbackButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    plusButton.addListener(this);
    plusButton.setEnabled(false);
    addAndMakeVisible(plusButton);
    plusButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(52, 56, 62));

    auto configureStemStateButton = [this](juce::TextButton& button)
    {
        button.addListener(this);
        button.setClickingTogglesState(false);
        button.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(52, 56, 62));
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(button);
    };

    configureStemStateButton(vocalsSoloButton);
    configureStemStateButton(vocalsMuteButton);
    configureStemStateButton(drumsSoloButton);
    configureStemStateButton(drumsMuteButton);
    configureStemStateButton(bassSoloButton);
    configureStemStateButton(bassMuteButton);
    configureStemStateButton(otherSoloButton);
    configureStemStateButton(otherMuteButton);

    stopButton.addListener(this);
    addAndMakeVisible(stopButton);
    stopButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(146, 72, 72));
    stopButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);

    nextButton.addListener(this);
    nextButton.setEnabled(false);
    addAndMakeVisible(nextButton);
    nextButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(52, 56, 62));

    openModelButton.setButtonText("Select Demucs model");
    openModelButton.addListener(this);
    addAndMakeVisible(openModelButton);

    positionLabel.setJustificationType(juce::Justification::centredLeft);
    durationLabel.setJustificationType(juce::Justification::centredRight);
    footerLabel.setJustificationType(juce::Justification::centred);

    footerLabel.setText("Offline Demucs CLI separation with cached stems in Application Support", juce::dontSendNotification);

    configureStemKnob(drumsSlider, "Drums", DemucsProcessor::Stem::drums);
    configureStemKnob(bassSlider, "Bass", DemucsProcessor::Stem::bass);
    configureStemKnob(otherSlider, "Other", DemucsProcessor::Stem::other);
    configureStemKnob(vocalsSlider, "Vocals", DemucsProcessor::Stem::vocals);

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

    addAndMakeVisible(positionLabel);
    addAndMakeVisible(durationLabel);
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
        waveformScrubber.setAudioFile(loadedAudioFile, audioProcessor.getSpectrogramCacheFile());

    startTimerHz(10);
    refreshLabels();
    setSize(700, 500);
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
    cachedAudioComboBox.setLookAndFeel(nullptr);

    cachedAudioComboBox.removeListener(this);
    addAudioFileButton.removeListener(this);
    openCacheFolderButton.removeListener(this);
    prevButton.removeListener(this);
    playbackButton.removeListener(this);
    plusButton.removeListener(this);
    vocalsSoloButton.removeListener(this);
    vocalsMuteButton.removeListener(this);
    drumsSoloButton.removeListener(this);
    drumsMuteButton.removeListener(this);
    bassSoloButton.removeListener(this);
    bassMuteButton.removeListener(this);
    otherSoloButton.removeListener(this);
    otherMuteButton.removeListener(this);
    stopButton.removeListener(this);
    nextButton.removeListener(this);
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

    if (dividerY > 0)
    {
        const auto left = static_cast<float>(bounds.getX() + 8);
        const auto right = static_cast<float>(bounds.getRight() - 8);
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.drawLine(left, static_cast<float>(dividerY), right, static_cast<float>(dividerY), 1.0f);
    }
}

void JamPTAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(28);

    auto row1 = area.removeFromTop(34);
    openCacheFolderButton.setBounds(row1.removeFromRight(128));
    row1.removeFromRight(8);
    addAudioFileButton.setBounds(row1.removeFromRight(36));
    row1.removeFromRight(10);
    cachedAudioComboBox.setBounds(row1);

    area.removeFromTop(12);
    auto row2 = area.removeFromTop(30);
    playbackButton.setBounds(row2.removeFromLeft(120));
    row2.removeFromLeft(12);
    stopButton.setBounds(row2.removeFromLeft(120));

    area.removeFromTop(2);
    waveformScrubber.setBounds(area.removeFromTop(70));

    area.removeFromTop(10);
    auto row3 = area.removeFromTop(22);
    positionLabel.setBounds(row3.removeFromLeft(140));
    durationLabel.setBounds(row3.removeFromRight(140));

    area.removeFromTop(14);
    dividerY = area.getY() + 6;
    area.removeFromTop(10);
    auto knobsArea = area.removeFromTop(160);
    auto knobRow = knobsArea;
    auto knob1 = knobRow.removeFromLeft(knobRow.getWidth() / 4).reduced(8, 0);
    auto knob2 = knobRow.removeFromLeft(knobRow.getWidth() / 3).reduced(8, 0);
    auto knob3 = knobRow.removeFromLeft(knobRow.getWidth() / 2).reduced(8, 0);
    auto knob4 = knobRow.reduced(8, 0);

    auto placeKnob = [](juce::Rectangle<int> bounds,
                        juce::Label& label,
                        juce::Slider& knob,
                        juce::TextButton& soloButton,
                        juce::TextButton& muteButton)
    {
        label.setBounds(bounds.removeFromTop(22));
        auto buttonColumn = bounds.removeFromRight(24);
        const auto buttonHeight = 18;
        const auto buttonGap = 6;
        const auto totalButtonsHeight = (buttonHeight * 2) + buttonGap;
        auto buttonArea = buttonColumn.withSizeKeepingCentre(buttonColumn.getWidth(), totalButtonsHeight);
        soloButton.setBounds(buttonArea.removeFromTop(buttonHeight));
        buttonArea.removeFromTop(buttonGap);
        muteButton.setBounds(buttonArea.removeFromTop(buttonHeight));

        bounds.removeFromRight(8);
        const auto knobSide = juce::jmin(bounds.getWidth(), bounds.getHeight() - 4);
        auto knobBounds = bounds.removeFromTop(juce::jmax(0, knobSide + 24));
        knobBounds = knobBounds.withSizeKeepingCentre(knobSide, knobSide + 24);
        knob.setBounds(knobBounds);
    };

    placeKnob(knob1, drumsLabel, drumsSlider, drumsSoloButton, drumsMuteButton);
    placeKnob(knob2, bassLabel, bassSlider, bassSoloButton, bassMuteButton);
    placeKnob(knob3, otherLabel, otherSlider, otherSoloButton, otherMuteButton);
    placeKnob(knob4, vocalsLabel, vocalsSlider, vocalsSoloButton, vocalsMuteButton);

    area.removeFromTop(28);
    auto controlsRow = area.removeFromTop(44);
    const auto gap = 8;
    const auto totalGapWidth = gap * 4;
    const auto availableWidth = controlsRow.getWidth() - totalGapWidth;
    const auto squareButtonWidth = controlsRow.getHeight();
    const auto mainButtonWidth = juce::jmax(116, (availableWidth - (squareButtonWidth * 2) - 72) / 2);
    const auto plusButtonWidth = availableWidth - (squareButtonWidth * 2) - (mainButtonWidth * 2);

    prevButton.setBounds(controlsRow.removeFromLeft(squareButtonWidth));
    controlsRow.removeFromLeft(gap);
    playbackButton.setBounds(controlsRow.removeFromLeft(mainButtonWidth));
    controlsRow.removeFromLeft(gap);
    plusButton.setBounds(controlsRow.removeFromLeft(plusButtonWidth));
    controlsRow.removeFromLeft(gap);
    stopButton.setBounds(controlsRow.removeFromLeft(mainButtonWidth));
    controlsRow.removeFromLeft(gap);
    nextButton.setBounds(controlsRow.removeFromLeft(squareButtonWidth));

    area.removeFromTop(14);
    auto row4 = area.removeFromTop(30);
    openModelButton.setBounds(row4.removeFromLeft(210));
    row4.removeFromLeft(12);
    footerLabel.setBounds(row4);
}

void JamPTAudioProcessorEditor::buttonClicked(juce::Button* button)
{
    if (button == &addAudioFileButton)
    {
        launchAudioFileChooser();
    }
    else if (button == &openCacheFolderButton)
    {
        const auto selectedCacheDirectory = audioProcessor.getSelectedCacheDirectory();
        if (selectedCacheDirectory.isDirectory())
            selectedCacheDirectory.revealToUser();
    }
    else if (button == &prevButton || button == &plusButton || button == &nextButton)
    {
        if (button == &prevButton)
            audioProcessor.jumpToPreviousMarker();
        else if (button == &nextButton)
            audioProcessor.jumpToNextMarker();
        else if (audioProcessor.isAtMarker())
            audioProcessor.removeMarkerAtCurrentPosition();
        else
            audioProcessor.addMarkerAtCurrentPosition();

        refreshLabels();
    }
    else if (button == &vocalsSoloButton || button == &vocalsMuteButton
          || button == &drumsSoloButton || button == &drumsMuteButton
          || button == &bassSoloButton || button == &bassMuteButton
          || button == &otherSoloButton || button == &otherMuteButton)
    {
        auto applyStemStateToggle = [this, button](DemucsProcessor::Stem stem, juce::TextButton& soloButton, juce::TextButton& muteButton)
        {
            if (button == &soloButton)
                audioProcessor.setStemSolo(stem, ! audioProcessor.isStemSolo(stem));
            else if (button == &muteButton)
                audioProcessor.setStemMute(stem, ! audioProcessor.isStemMuted(stem));
        };

        applyStemStateToggle(DemucsProcessor::Stem::vocals, vocalsSoloButton, vocalsMuteButton);
        applyStemStateToggle(DemucsProcessor::Stem::drums, drumsSoloButton, drumsMuteButton);
        applyStemStateToggle(DemucsProcessor::Stem::bass, bassSoloButton, bassMuteButton);
        applyStemStateToggle(DemucsProcessor::Stem::other, otherSoloButton, otherMuteButton);
        refreshLabels();
    }
    else if (button == &playbackButton)
    {
        handlePlaybackButton();
    }
    else if (button == &stopButton)
    {
        handleStopButton();
    }
    else if (button == &openModelButton)
    {
        launchModelFileChooser();
    }
}

void JamPTAudioProcessorEditor::comboBoxChanged(juce::ComboBox* comboBoxThatHasChanged)
{
    if (comboBoxThatHasChanged != &cachedAudioComboBox || suppressCacheSelectionCallback)
        return;

    const auto selectedEntryName = cachedAudioComboBox.getText();
    if (selectedEntryName.isEmpty())
        return;

    if (! audioProcessor.loadCachedSourceEntry(selectedEntryName))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "Jam-PT",
                                               "Unable to load the selected cached audio file.");
    }
    else
    {
        waveformScrubber.setAudioFile(audioProcessor.getLoadedAudioFile(),
                                      audioProcessor.getSpectrogramCacheFile());
    }

    refreshLabels();
}

void JamPTAudioProcessorEditor::timerCallback()
{
    refreshLabels();
}

void JamPTAudioProcessorEditor::refreshLabels()
{
    audioProcessor.refreshBackendStateFromLoadedFile();
    refreshCachedAudioSelector();

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

    juce::ignoreUnused(playbackStatus);
    openModelButton.setButtonText("Model: " + (modelName.isNotEmpty() ? modelName : "select"));
    playbackButton.setButtonText(audioProcessor.getPlaybackState() == AudioFilePlayer::PlaybackState::playing ? "PAUSE" : "PLAY");

    const auto hasAudioFile = audioFile.isNotEmpty();
    const auto stemsReady = audioProcessor.isStemSeparationReady();
    const auto transportEnabled = hasAudioFile && stemsReady;
    const auto hasMarkers = stemsReady && audioProcessor.hasMarkers();
    const auto isAtMarker = stemsReady && audioProcessor.isAtMarker();
    const auto canAddMarker = stemsReady && audioProcessor.canAddMarker();
    playbackButton.setEnabled(transportEnabled);
    stopButton.setEnabled(transportEnabled);
    prevButton.setEnabled(hasMarkers);
    nextButton.setEnabled(hasMarkers);
    plusButton.setEnabled(isAtMarker || canAddMarker);
    plusButton.setButtonText(isAtMarker ? "-" : "+");
    openCacheFolderButton.setEnabled(audioProcessor.getSelectedCacheDirectory().isDirectory());
    waveformScrubber.setPlaybackProgress(audioProcessor.getPlaybackProgress());
    waveformScrubber.setMarkers(audioProcessor.getMarkers());
    const auto separationStatus = audioProcessor.getModelBufferStatusText();
    const auto hasFailure = separationStatus.startsWithIgnoreCase("Failed:");
    const auto summaryStatus = hasFailure ? "Separation failed" : separationStatus;
    const auto progress = audioProcessor.getModelBufferProgress();
    const auto showOverlay = progress > 0.0 && progress < 1.0;
    waveformScrubber.setSeparationOverlay(progress, showOverlay);

    positionLabel.setText(stemsReady ? formatTime(audioProcessor.getPlaybackPositionSeconds())
                                     : summaryStatus,
                          juce::dontSendNotification);
    durationLabel.setText(hasAudioFile ? formatTime(audioProcessor.getPlaybackDurationSeconds()) : "--:--",
                          juce::dontSendNotification);
    waveformScrubber.setEnabled(hasAudioFile && stemsReady);
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
    auto updateStemStateButton = [enabledAlpha, stemsReady](juce::TextButton& button, bool active, juce::Colour activeColour)
    {
        button.setEnabled(stemsReady);
        button.setAlpha(enabledAlpha);
        button.setColour(juce::TextButton::buttonColourId, active ? activeColour : juce::Colour::fromRGB(52, 56, 62));
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    };

    updateStemStateButton(vocalsSoloButton, audioProcessor.isStemSolo(DemucsProcessor::Stem::vocals), juce::Colour::fromRGB(220, 190, 64));
    updateStemStateButton(vocalsMuteButton, audioProcessor.isStemMuted(DemucsProcessor::Stem::vocals), juce::Colour::fromRGB(72, 110, 220));
    updateStemStateButton(drumsSoloButton, audioProcessor.isStemSolo(DemucsProcessor::Stem::drums), juce::Colour::fromRGB(220, 190, 64));
    updateStemStateButton(drumsMuteButton, audioProcessor.isStemMuted(DemucsProcessor::Stem::drums), juce::Colour::fromRGB(72, 110, 220));
    updateStemStateButton(bassSoloButton, audioProcessor.isStemSolo(DemucsProcessor::Stem::bass), juce::Colour::fromRGB(220, 190, 64));
    updateStemStateButton(bassMuteButton, audioProcessor.isStemMuted(DemucsProcessor::Stem::bass), juce::Colour::fromRGB(72, 110, 220));
    updateStemStateButton(otherSoloButton, audioProcessor.isStemSolo(DemucsProcessor::Stem::other), juce::Colour::fromRGB(220, 190, 64));
    updateStemStateButton(otherMuteButton, audioProcessor.isStemMuted(DemucsProcessor::Stem::other), juce::Colour::fromRGB(72, 110, 220));
}

void JamPTAudioProcessorEditor::refreshCachedAudioSelector()
{
    auto cacheEntries = audioProcessor.getCachedSourceEntryNames();
    cacheEntries.sort(true);

    const auto selectedEntryName = audioProcessor.getSelectedCacheEntryName();
    const auto needsRefresh = cacheEntries != lastCacheEntries
                           || cachedAudioComboBox.getText() != selectedEntryName;

    if (! needsRefresh)
        return;

    suppressCacheSelectionCallback = true;
    cachedAudioComboBox.clear(juce::dontSendNotification);

    if (cacheEntries.isEmpty())
    {
        cachedAudioComboBox.setTextWhenNothingSelected("No cached files");
        cachedAudioComboBox.setSelectedId(0, juce::dontSendNotification);
        cachedAudioComboBox.setText({}, juce::dontSendNotification);
    }
    else
    {
        cachedAudioComboBox.setTextWhenNothingSelected("Select a cached file");
        auto itemId = 1;
        for (const auto& entryName : cacheEntries)
        {
            cachedAudioComboBox.addItem(entryName, itemId);

            if (entryName == selectedEntryName)
                cachedAudioComboBox.setSelectedId(itemId, juce::dontSendNotification);

            ++itemId;
        }

        if (selectedEntryName.isEmpty())
            cachedAudioComboBox.setTextWhenNothingSelected("Select a cached file");
        else if (! cacheEntries.contains(selectedEntryName))
            cachedAudioComboBox.setText(selectedEntryName, juce::dontSendNotification);
    }

    lastCacheEntries = cacheEntries;
    suppressCacheSelectionCallback = false;
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
        {
            safeThis->refreshLabels();
            return;
        }

        if (! safeThis->audioProcessor.loadAudioFile(result))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Jam-PT",
                                                   "Unable to load the selected audio file.");
            safeThis->waveformScrubber.clear();
        }
        else
        {
            safeThis->waveformScrubber.setAudioFile(safeThis->audioProcessor.getLoadedAudioFile(),
                                                    safeThis->audioProcessor.getSpectrogramCacheFile());
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
