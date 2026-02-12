#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

//==============================================================================
/**
    Audio Transport Plugin Editor

    Simple UI with morph knob, window size control, and bypass button.
*/
class AudioTransportEditor  : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    AudioTransportEditor (AudioTransportProcessor&);
    ~AudioTransportEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // Reference to processor
    AudioTransportProcessor& audioProcessor;

    // UI Components
    juce::Slider morphSlider;
    juce::Label morphLabel;

    juce::Slider windowSizeSlider;
    juce::Label windowSizeLabel;

    juce::TextButton bypassButton;

    juce::ComboBox morphModeCombo;
    juce::Label morphModeLabel;

    juce::Slider dryWetSlider;
    juce::Label dryWetLabel;

    juce::ComboBox algorithmCombo;
    juce::Label algorithmLabel;

    juce::Label titleLabel;
    juce::Label versionLabel;
    juce::Label latencyLabel;

    // No parameter attachments needed - we handle updates manually

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioTransportEditor)
};
