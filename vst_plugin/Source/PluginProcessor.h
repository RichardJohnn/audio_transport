#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <audio_transport/RealtimeAudioTransport.hpp>
#include <audio_transport/RealtimeReassignmentTransport.hpp>
#include <memory>
#include <atomic>
#include <thread>
#include <cstdint>

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
    juce::AudioParameterChoice* getHopDivisorParameter() const { return hopDivisorParam; }
    juce::AudioParameterChoice* getFftMultiplierParameter() const { return fftMultiplierParam; }

    // Latency reporting
    int getLatencySamples() const;

private:
    //==============================================================================
    struct ProcessorState {
        uint64_t key = 0;
        std::unique_ptr<audio_transport::RealtimeAudioTransport> cdfProcessor;
        std::unique_ptr<audio_transport::RealtimeReassignmentTransport> reassignmentProcessor;
        std::vector<float> mainDelayBuffer, sidechainDelayBuffer;
        int delayBufferWritePos = 0;
        int delayBufferSize = 0;
    };
    ProcessorState* active = nullptr; // Owned exclusively by the audio thread until retired.
    std::atomic<ProcessorState*> pending { nullptr }, retired { nullptr };
    std::atomic<uint64_t> requestedKey { 0 };
    std::atomic<int> reportedLatency { 0 };
    std::atomic<bool> stopping { false };
    std::thread builder;
    void buildLoop();
    void requestProcessor();

    // Parameters
    juce::AudioParameterFloat* morphParam;
    juce::AudioParameterFloat* windowSizeParam;
    juce::AudioParameterBool* bypassParam;
    juce::AudioParameterChoice* morphModeParam;
    juce::AudioParameterFloat* dryWetParam;
    juce::AudioParameterChoice* algorithmParam;
    juce::AudioParameterChoice* hopDivisorParam;
    juce::AudioParameterChoice* fftMultiplierParam;

    // State
    std::atomic<double> requestedSampleRate { 44100.0 };
    uint64_t prepareGeneration = 0;
    float lastWindowSize = -1.0f;
    int lastAlgorithm = -1, lastHopDivisor = -1, lastFftMult = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioTransportProcessor)
};
