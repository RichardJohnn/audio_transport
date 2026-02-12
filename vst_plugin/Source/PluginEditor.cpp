#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioTransportEditor::AudioTransportEditor (AudioTransportProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      morphParamAttachment(*p.getMorphParameter(),
                          [this](float newValue) { morphSlider.setValue(newValue, juce::dontSendNotification); }),
      windowSizeParamAttachment(*p.getWindowSizeParameter(),
                               [this](float newValue) { windowSizeSlider.setValue(newValue, juce::dontSendNotification); }),
      bypassParamAttachment(*p.getBypassParameter(),
                           [this](float newValue) { bypassButton.setToggleState(newValue > 0.5f, juce::dontSendNotification); })
{
    // Set window size
    setSize (500, 350);

    // Title
    titleLabel.setText("Audio Transport", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(28.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);

    // Morph slider
    morphSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    morphSlider.setRange(0.0, 1.0, 0.01);
    morphSlider.setValue(p.getMorphParameter()->get());
    morphSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    morphSlider.setPopupDisplayEnabled(true, false, this);
    morphSlider.setTextValueSuffix("");
    morphSlider.onValueChange = [this] {
        audioProcessor.getMorphParameter()->setValueNotifyingHost(morphSlider.getValue());
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
        audioProcessor.getWindowSizeParameter()->setValueNotifyingHost(windowSizeSlider.getValue());
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
    g.setFont(11.0f);
    juce::String helpText = "0.0 = Main Input | 1.0 = Sidechain Input | 0.5 = 50/50 Morph";
    g.drawText(helpText, getLocalBounds().reduced(10).removeFromBottom(60).removeFromTop(20),
               juce::Justification::centred);
}

void AudioTransportEditor::resized()
{
    auto bounds = getLocalBounds().reduced(20);

    // Title
    titleLabel.setBounds(bounds.removeFromTop(50));

    bounds.removeFromTop(20); // Spacing

    // Morph slider (big knob in center)
    auto morphArea = bounds.removeFromTop(180);
    morphLabel.setBounds(morphArea.removeFromTop(20));
    morphSlider.setBounds(morphArea.withSizeKeepingCentre(160, 160));

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

    // Update sliders from parameters (in case automation changed them)
    morphSlider.setValue(audioProcessor.getMorphParameter()->get(), juce::dontSendNotification);
    windowSizeSlider.setValue(audioProcessor.getWindowSizeParameter()->get(), juce::dontSendNotification);
    bypassButton.setToggleState(audioProcessor.getBypassParameter()->get(), juce::dontSendNotification);
}
