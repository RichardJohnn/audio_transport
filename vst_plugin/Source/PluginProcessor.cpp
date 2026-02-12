#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioTransportProcessor::AudioTransportProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::mono(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::mono(), true)
                     #endif
                       // Add sidechain input
                       .withInput  ("Sidechain", juce::AudioChannelSet::mono(), true)
                       )
#endif
{
    // Add parameters
    addParameter(morphParam = new juce::AudioParameterFloat(
        "morph",
        "Morph",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f,
        "Morph amount (0=main, 1=sidechain)"
    ));

    addParameter(windowSizeParam = new juce::AudioParameterFloat(
        "windowSize",
        "Window Size",
        juce::NormalisableRange<float>(20.0f, 200.0f, 1.0f),
        100.0f,
        "Window size in milliseconds"
    ));

    addParameter(bypassParam = new juce::AudioParameterBool(
        "bypass",
        "Bypass",
        false,
        "Bypass processing"
    ));
}

AudioTransportProcessor::~AudioTransportProcessor()
{
}

//==============================================================================
const juce::String AudioTransportProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AudioTransportProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool AudioTransportProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool AudioTransportProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double AudioTransportProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AudioTransportProcessor::getNumPrograms()
{
    return 1;
}

int AudioTransportProcessor::getCurrentProgram()
{
    return 0;
}

void AudioTransportProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused(index);
}

const juce::String AudioTransportProcessor::getProgramName (int index)
{
    juce::ignoreUnused(index);
    return {};
}

void AudioTransportProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================
void AudioTransportProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);

    currentSampleRate = sampleRate;
    rebuildProcessor();

    // Report latency to host
    setLatencySamples(getLatencySamples());
}

void AudioTransportProcessor::rebuildProcessor()
{
    double windowSize = windowSizeParam->get();

    transportProcessor = std::make_unique<audio_transport::RealtimeAudioTransport>(
        currentSampleRate,
        windowSize,
        4,  // 75% overlap
        2   // 2x FFT zero-padding
    );

    needsProcessorRebuild = false;

    // Update latency reporting
    setLatencySamples(getLatencySamples());
}

void AudioTransportProcessor::releaseResources()
{
    if (transportProcessor)
        transportProcessor->reset();
}

int AudioTransportProcessor::getLatencySamples() const
{
    return transportProcessor ? transportProcessor->getLatencySamples() : 0;
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool AudioTransportProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // We only support mono for now
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;

    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::disabled())
        return false;

    // Sidechain must be mono
    if (layouts.getChannelSet(true, 1) != juce::AudioChannelSet::mono()
        && layouts.getChannelSet(true, 1) != juce::AudioChannelSet::disabled())
        return false;

    return true;
}
#endif

void AudioTransportProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    // Check if window size changed
    static float lastWindowSize = windowSizeParam->get();
    if (std::abs(windowSizeParam->get() - lastWindowSize) > 0.5f)
    {
        lastWindowSize = windowSizeParam->get();
        needsProcessorRebuild = true;
    }

    if (needsProcessorRebuild)
        rebuildProcessor();

    // Get main input/output buffer
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    auto numSamples = buffer.getNumSamples();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);

    // Check for bypass
    if (bypassParam->get())
    {
        // In bypass, just pass through the main input
        return;
    }

    // Get sidechain buffer
    auto* mainInput = buffer.getWritePointer(0);
    auto sidechainBuffer = getBusBuffer(buffer, true, 1);

    // Check if sidechain is connected
    if (sidechainBuffer.getNumChannels() == 0)
    {
        // No sidechain - just pass through or process with self
        // For now, just pass through
        return;
    }

    auto* sidechainInput = sidechainBuffer.getReadPointer(0);

    // Get morph parameter
    float k = morphParam->get();

    // Process
    if (transportProcessor)
    {
        transportProcessor->process(
            mainInput,
            sidechainInput,
            mainInput,  // Output overwrites main input
            numSamples,
            k
        );
    }
}

//==============================================================================
bool AudioTransportProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* AudioTransportProcessor::createEditor()
{
    return new AudioTransportEditor (*this);
}

//==============================================================================
void AudioTransportProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Store parameter state
    juce::MemoryOutputStream stream(destData, true);

    stream.writeFloat(morphParam->get());
    stream.writeFloat(windowSizeParam->get());
    stream.writeBool(bypassParam->get());
}

void AudioTransportProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Restore parameter state
    juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);

    morphParam->setValueNotifyingHost(stream.readFloat());
    windowSizeParam->setValueNotifyingHost(stream.readFloat());
    bypassParam->setValueNotifyingHost(stream.readBool());

    needsProcessorRebuild = true;
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioTransportProcessor();
}
