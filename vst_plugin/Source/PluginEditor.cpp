#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioTransportEditor::AudioTransportEditor (AudioTransportProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p)
{
    // Set window size (taller to accommodate new controls)
    setSize (500, 490);

    // Title
    titleLabel.setText("Audio Transport", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(28.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);

    // Version label
    #ifdef PLUGIN_VERSION_STRING
        versionLabel.setText("v" PLUGIN_VERSION_STRING, juce::dontSendNotification);
    #else
        versionLabel.setText("v1.1.0", juce::dontSendNotification);
    #endif
    versionLabel.setFont(juce::Font(11.0f));
    versionLabel.setJustificationType(juce::Justification::centred);
    versionLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(versionLabel);

    // Morph slider
    morphSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    morphSlider.setRange(0.0, 1.0, 0.01);
    morphSlider.setValue(p.getMorphParameter()->get());
    morphSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    morphSlider.setPopupDisplayEnabled(true, false, this);
    morphSlider.setTextValueSuffix("");
    morphSlider.onValueChange = [this] {
        audioProcessor.getMorphParameter()->setValueNotifyingHost(
            static_cast<float>(morphSlider.getValue())
        );
    };
    addAndMakeVisible(morphSlider);

    morphLabel.setText("Morph", juce::dontSendNotification);
    morphLabel.setFont(juce::Font(16.0f));
    morphLabel.setJustificationType(juce::Justification::centred);
    morphLabel.attachToComponent(&morphSlider, false);
    addAndMakeVisible(morphLabel);

    // Window size slider
    windowSizeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    windowSizeSlider.setRange(20.0, 200.0, 1.0);
    windowSizeSlider.setValue(p.getWindowSizeParameter()->get());
    windowSizeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    windowSizeSlider.setTextValueSuffix(" ms");
    windowSizeSlider.onValueChange = [this] {
        audioProcessor.getWindowSizeParameter()->setValueNotifyingHost(
            static_cast<float>(windowSizeSlider.getValue())
        );
    };
    addAndMakeVisible(windowSizeSlider);

    windowSizeLabel.setText("Window Size", juce::dontSendNotification);
    windowSizeLabel.setFont(juce::Font(14.0f));
    windowSizeLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(windowSizeLabel);

    // Bypass button
    bypassButton.setButtonText("Bypass");
    bypassButton.setClickingTogglesState(true);
    bypassButton.setToggleState(p.getBypassParameter()->get(), juce::dontSendNotification);
    bypassButton.onClick = [this] {
        audioProcessor.getBypassParameter()->setValueNotifyingHost(bypassButton.getToggleState() ? 1.0f : 0.0f);
    };
    addAndMakeVisible(bypassButton);

    // Morph Mode combo box
    morphModeCombo.addItem("Full Morph", 1);
    morphModeCombo.addItem("Dry at Extremes", 2);
    morphModeCombo.setSelectedItemIndex(p.getMorphModeParameter()->getIndex(), juce::dontSendNotification);
    morphModeCombo.onChange = [this] {
        int index = morphModeCombo.getSelectedItemIndex();
        float normalizedValue = static_cast<float>(index) / static_cast<float>(morphModeCombo.getNumItems() - 1);
        audioProcessor.getMorphModeParameter()->setValueNotifyingHost(normalizedValue);
    };
    addAndMakeVisible(morphModeCombo);

    morphModeLabel.setText("Morph Mode", juce::dontSendNotification);
    morphModeLabel.setFont(juce::Font(14.0f));
    morphModeLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(morphModeLabel);

    // Dry/Wet slider
    dryWetSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    dryWetSlider.setRange(0.0, 100.0, 1.0);
    dryWetSlider.setValue(p.getDryWetParameter()->get());
    dryWetSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    dryWetSlider.setTextValueSuffix(" %");
    dryWetSlider.onValueChange = [this] {
        audioProcessor.getDryWetParameter()->setValueNotifyingHost(
            static_cast<float>(dryWetSlider.getValue())
        );
    };
    addAndMakeVisible(dryWetSlider);

    dryWetLabel.setText("Dry/Wet", juce::dontSendNotification);
    dryWetLabel.setFont(juce::Font(14.0f));
    dryWetLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(dryWetLabel);

    // Algorithm combo box
    algorithmCombo.addItem("CDF (Fast)", 1);
    algorithmCombo.addItem("Reassignment (Quality)", 2);
    algorithmCombo.setSelectedItemIndex(p.getAlgorithmParameter()->getIndex(), juce::dontSendNotification);
    algorithmCombo.onChange = [this] {
        int index = algorithmCombo.getSelectedItemIndex();
        float normalizedValue = static_cast<float>(index) / static_cast<float>(algorithmCombo.getNumItems() - 1);
        audioProcessor.getAlgorithmParameter()->setValueNotifyingHost(normalizedValue);
    };
    addAndMakeVisible(algorithmCombo);

    algorithmLabel.setText("Algorithm", juce::dontSendNotification);
    algorithmLabel.setFont(juce::Font(14.0f));
    algorithmLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(algorithmLabel);

    // Latency label
    latencyLabel.setFont(juce::Font(12.0f));
    latencyLabel.setJustificationType(juce::Justification::centred);
    latencyLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(latencyLabel);

    // Start timer for updating latency display
    startTimerHz(10);
}

AudioTransportEditor::~AudioTransportEditor()
{
}

//==============================================================================
void AudioTransportEditor::paint (juce::Graphics& g)
{
    // Background gradient
    g.fillAll(juce::Colour(0xff1a1a1a));

    auto bounds = getLocalBounds();

    // Background gradient
    juce::ColourGradient gradient(
        juce::Colour(0xff2a2a2a), bounds.getTopLeft().toFloat(),
        juce::Colour(0xff1a1a1a), bounds.getBottomRight().toFloat(),
        false
    );
    g.setGradientFill(gradient);
    g.fillAll();

    // Border
    g.setColour(juce::Colour(0xff3a3a3a));
    g.drawRect(getLocalBounds(), 2);

    // Help text
    g.setColour(juce::Colours::grey);
    g.setFont(10.0f);
    juce::String helpText = audioProcessor.getMorphModeParameter()->getIndex() == 1 ?
        "Dry at Extremes: 0.0 = Dry Main | 0.0→0.5 Morph→SC | 0.5 FLIP | 0.5→1.0 SC→Morph | 1.0 = Dry SC" :
        "Full Morph: Always morphing - 0.0 = Main→SC | 0.5 = 50/50 | 1.0 = SC→Main";
    g.drawText(helpText, getLocalBounds().reduced(10).removeFromBottom(80).removeFromTop(20),
               juce::Justification::centred);

    // Algorithm info
    juce::String algoText = audioProcessor.getAlgorithmParameter()->getIndex() == 0 ?
        "Algorithm: CDF (Fast)" : "Algorithm: Reassignment (Quality - more CPU)";
    g.setFont(9.0f);
    g.drawText(algoText, getLocalBounds().reduced(10).removeFromBottom(60).removeFromTop(15),
               juce::Justification::centred);
}

void AudioTransportEditor::resized()
{
    auto bounds = getLocalBounds().reduced(20);

    // Title
    titleLabel.setBounds(bounds.removeFromTop(40));

    // Version
    versionLabel.setBounds(bounds.removeFromTop(15));

    bounds.removeFromTop(15); // Spacing

    // Morph slider (big knob in center)
    auto morphArea = bounds.removeFromTop(180);
    morphLabel.setBounds(morphArea.removeFromTop(20));
    morphSlider.setBounds(morphArea.withSizeKeepingCentre(160, 160));

    bounds.removeFromTop(10); // Spacing

    // Algorithm combo box
    auto algorithmArea = bounds.removeFromTop(30);
    algorithmLabel.setBounds(algorithmArea.removeFromLeft(120));
    algorithmCombo.setBounds(algorithmArea);

    bounds.removeFromTop(10); // Spacing

    // Morph Mode combo box
    auto morphModeArea = bounds.removeFromTop(30);
    morphModeLabel.setBounds(morphModeArea.removeFromLeft(120));
    morphModeCombo.setBounds(morphModeArea);

    bounds.removeFromTop(10); // Spacing

    // Dry/Wet slider
    auto dryWetArea = bounds.removeFromTop(30);
    dryWetLabel.setBounds(dryWetArea.removeFromLeft(120));
    dryWetSlider.setBounds(dryWetArea);

    bounds.removeFromTop(10); // Spacing

    // Window size slider
    auto windowArea = bounds.removeFromTop(30);
    windowSizeLabel.setBounds(windowArea.removeFromLeft(120));
    windowSizeSlider.setBounds(windowArea);

    bounds.removeFromTop(10); // Spacing

    // Bypass button
    bypassButton.setBounds(bounds.removeFromTop(30).withSizeKeepingCentre(120, 30));

    bounds.removeFromTop(10); // Spacing

    // Latency label at bottom
    latencyLabel.setBounds(bounds.removeFromTop(20));
}

void AudioTransportEditor::timerCallback()
{
    // Update latency display
    int latencySamples = audioProcessor.getLatencySamples();
    double latencyMs = (latencySamples / audioProcessor.getSampleRate()) * 1000.0;

    juce::String latencyText = juce::String::formatted("Latency: %d samples (%.1f ms)",
                                                       latencySamples, latencyMs);
    latencyLabel.setText(latencyText, juce::dontSendNotification);

    // Update controls from parameters (in case automation changed them)
    // Only update if not currently being edited by user
    if (!morphSlider.isMouseButtonDown() && !morphSlider.isMouseOverOrDragging())
    {
        float paramValue = audioProcessor.getMorphParameter()->get();
        if (std::abs(morphSlider.getValue() - paramValue) > 0.001)
            morphSlider.setValue(paramValue, juce::dontSendNotification);
    }

    if (!windowSizeSlider.isMouseButtonDown() && !windowSizeSlider.isMouseOverOrDragging())
    {
        float paramValue = audioProcessor.getWindowSizeParameter()->get();
        if (std::abs(windowSizeSlider.getValue() - paramValue) > 0.5)
            windowSizeSlider.setValue(paramValue, juce::dontSendNotification);
    }

    if (!dryWetSlider.isMouseButtonDown() && !dryWetSlider.isMouseOverOrDragging())
    {
        float paramValue = audioProcessor.getDryWetParameter()->get();
        if (std::abs(dryWetSlider.getValue() - paramValue) > 0.5)
            dryWetSlider.setValue(paramValue, juce::dontSendNotification);
    }

    // These don't need mouse checks
    bypassButton.setToggleState(audioProcessor.getBypassParameter()->get(), juce::dontSendNotification);

    int modeIndex = audioProcessor.getMorphModeParameter()->getIndex();
    if (morphModeCombo.getSelectedItemIndex() != modeIndex)
        morphModeCombo.setSelectedItemIndex(modeIndex, juce::dontSendNotification);

    int algoIndex = audioProcessor.getAlgorithmParameter()->getIndex();
    if (algorithmCombo.getSelectedItemIndex() != algoIndex)
        algorithmCombo.setSelectedItemIndex(algoIndex, juce::dontSendNotification);
}
