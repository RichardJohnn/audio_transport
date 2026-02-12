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

    juce::Label titleLabel;
    juce::Label latencyLabel;

    // Parameter attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> morphAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> windowSizeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // Or manual attachments
    juce::ParameterAttachment morphParamAttachment;
    juce::ParameterAttachment windowSizeParamAttachment;
    juce::ParameterAttachment bypassParamAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioTransportEditor)
};
