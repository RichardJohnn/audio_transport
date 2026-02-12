#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <audio_transport/RealtimeAudioTransport.hpp>
#include <audio_transport/RealtimeReassignmentTransport.hpp>
#include <memory>

//==============================================================================
/**
    Audio Transport VST3 Plugin

    Morphs between main input and sidechain input using optimal transport.
*/
class AudioTransportProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    AudioTransportProcessor();
    ~AudioTransportProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Parameter accessors
    juce::AudioParameterFloat* getMorphParameter() const { return morphParam; }
    juce::AudioParameterFloat* getWindowSizeParameter() const { return windowSizeParam; }
    juce::AudioParameterBool* getBypassParameter() const { return bypassParam; }
    juce::AudioParameterChoice* getMorphModeParameter() const { return morphModeParam; }
    juce::AudioParameterFloat* getDryWetParameter() const { return dryWetParam; }
    juce::AudioParameterChoice* getAlgorithmParameter() const { return algorithmParam; }

    // Latency reporting
    int getLatencySamples() const;

private:
    //==============================================================================
    // Audio Transport processors (CDF-based and Reassignment-based)
    std::unique_ptr<audio_transport::RealtimeAudioTransport> cdfProcessor;
    std::unique_ptr<audio_transport::RealtimeReassignmentTransport> reassignmentProcessor;

    // Parameters
    juce::AudioParameterFloat* morphParam;
    juce::AudioParameterFloat* windowSizeParam;
    juce::AudioParameterBool* bypassParam;
    juce::AudioParameterChoice* morphModeParam;
    juce::AudioParameterFloat* dryWetParam;
    juce::AudioParameterChoice* algorithmParam;

    // State
    double currentSampleRate = 44100.0;
    bool needsProcessorRebuild = false;

    // Delay buffers for latency compensation of dry signals
    std::vector<float> mainDelayBuffer;
    std::vector<float> sidechainDelayBuffer;
    int delayBufferWritePos = 0;
    int delayBufferSize = 0;

    // Helper methods
    void rebuildProcessor();
    void updateDelayBuffers();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioTransportProcessor)
};
