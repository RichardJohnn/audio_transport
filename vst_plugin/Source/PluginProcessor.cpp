#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>
#include <vector>

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

    addParameter(morphModeParam = new juce::AudioParameterChoice(
        "morphMode",
        "Morph Mode",
        juce::StringArray("Full Morph", "Dry at Extremes"),
        1,  // Default to "Dry at Extremes"
        "Morph behavior"
    ));

    addParameter(dryWetParam = new juce::AudioParameterFloat(
        "dryWet",
        "Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f),
        100.0f,
        "Dry/wet mix percentage"
    ));

    addParameter(algorithmParam = new juce::AudioParameterChoice(
        "algorithm",
        "Algorithm",
        juce::StringArray("CDF (Fast)", "Reassignment (Quality)"),
        0,  // Default to CDF (faster)
        "Transport algorithm"
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

    // Initialize delay buffers
    updateDelayBuffers();
}

void AudioTransportProcessor::rebuildProcessor()
{
    double windowSize = windowSizeParam->get();

    // Build both processors
    cdfProcessor = std::make_unique<audio_transport::RealtimeAudioTransport>(
        currentSampleRate,
        windowSize,
        4,  // 75% overlap
        2   // 2x FFT zero-padding
    );

    reassignmentProcessor = std::make_unique<audio_transport::RealtimeReassignmentTransport>(
        currentSampleRate,
        windowSize,
        4,  // 75% overlap
        2   // 2x FFT zero-padding
    );

    needsProcessorRebuild = false;

    // Update latency reporting (use current algorithm's latency)
    setLatencySamples(getLatencySamples());

    // Update delay buffers to match new latency
    updateDelayBuffers();
}

void AudioTransportProcessor::updateDelayBuffers()
{
    // Get the current latency
    int latency = getLatencySamples();

    if (latency < 1)
        latency = 1;  // Minimum buffer size

    if (latency != delayBufferSize)
    {
        delayBufferSize = latency;
        // Buffer size exactly matches latency for circular buffer
        mainDelayBuffer.resize(delayBufferSize, 0.0f);
        sidechainDelayBuffer.resize(delayBufferSize, 0.0f);
        delayBufferWritePos = 0;

        // Clear buffers
        std::fill(mainDelayBuffer.begin(), mainDelayBuffer.end(), 0.0f);
        std::fill(sidechainDelayBuffer.begin(), sidechainDelayBuffer.end(), 0.0f);
    }
}

void AudioTransportProcessor::releaseResources()
{
    if (cdfProcessor)
        cdfProcessor->reset();
    if (reassignmentProcessor)
        reassignmentProcessor->reset();

    // Clear delay buffers
    std::fill(mainDelayBuffer.begin(), mainDelayBuffer.end(), 0.0f);
    std::fill(sidechainDelayBuffer.begin(), sidechainDelayBuffer.end(), 0.0f);
    delayBufferWritePos = 0;
}

int AudioTransportProcessor::getLatencySamples() const
{
    // Return latency based on current algorithm selection
    int algorithmIndex = algorithmParam->getIndex();

    if (algorithmIndex == 0 && cdfProcessor)
        return cdfProcessor->getLatencySamples();
    else if (algorithmIndex == 1 && reassignmentProcessor)
        return reassignmentProcessor->getLatencySamples();

    return 0;
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

    // Check if window size or algorithm changed
    static float lastWindowSize = windowSizeParam->get();
    static int lastAlgorithm = algorithmParam->getIndex();

    if (std::abs(windowSizeParam->get() - lastWindowSize) > 0.5f)
    {
        lastWindowSize = windowSizeParam->get();
        needsProcessorRebuild = true;
    }

    if (algorithmParam->getIndex() != lastAlgorithm)
    {
        lastAlgorithm = algorithmParam->getIndex();
        // When algorithm changes, update latency and delay buffers
        setLatencySamples(getLatencySamples());
        updateDelayBuffers();
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

    // Latency-compensated dry signals
    // Delay dry signals to match the transport processor latency
    // This prevents slap-delay artifacts when mixing dry with processed signals
    std::vector<float> dryMain(numSamples);
    std::vector<float> drySidechain(numSamples);

    if (delayBufferSize > 0)
    {
        // Process delay buffers sample-by-sample
        for (int i = 0; i < numSamples; ++i)
        {
            // For circular buffer: read the oldest sample (at current write position)
            // then write the new sample, overwriting the old one
            dryMain[i] = mainDelayBuffer[delayBufferWritePos];
            drySidechain[i] = sidechainDelayBuffer[delayBufferWritePos];

            // Write new samples
            mainDelayBuffer[delayBufferWritePos] = mainInput[i];
            sidechainDelayBuffer[delayBufferWritePos] = sidechainInput[i];

            // Advance position (circular)
            delayBufferWritePos = (delayBufferWritePos + 1) % delayBufferSize;
        }
    }
    else
    {
        // No latency compensation needed
        std::memcpy(dryMain.data(), mainInput, numSamples * sizeof(float));
        std::memcpy(drySidechain.data(), sidechainInput, numSamples * sizeof(float));
    }

    // Get parameters
    float morphValue = morphParam->get();
    int morphMode = morphModeParam->getIndex();
    float dryWetPercent = dryWetParam->get() / 100.0f;

    // Calculate k value, input routing, and dry/wet blend based on morph mode
    float k = 0.5f;
    float mainDryBlend = 0.0f;
    float sidechainDryBlend = 0.0f;
    float morphedBlend = 1.0f;
    bool flipInputs = false;

    if (morphMode == 0)  // Full Morph mode - always processing through transport
    {
        // k goes from 0.0 to 1.0
        // Always using the transport algorithm (no dry passthrough)
        k = morphValue;
        morphedBlend = 1.0f;
        flipInputs = false;
    }
    else  // Dry at Extremes mode (default) - continuous morphing with flip at 0.5
    {
        // 0.0 = dry main
        // 0.0→0.5 = morph main→sidechain (k: 0→1)
        // 0.5 = flip point
        // 0.5→1.0 = morph sidechain→main (k: 1→0 with flipped inputs)
        // 1.0 = dry sidechain

        if (morphValue <= 0.5f)
        {
            // First half: main→sidechain, k goes 0→1
            float t = morphValue * 2.0f;  // 0.0→1.0

            // k ramps from 0 to 1
            k = t;

            // Dry blend at the very beginning
            if (morphValue < 0.05f)
            {
                mainDryBlend = 1.0f - (morphValue / 0.05f);
                morphedBlend = morphValue / 0.05f;
            }
            else
            {
                mainDryBlend = 0.0f;
                morphedBlend = 1.0f;
            }

            flipInputs = false;
        }
        else
        {
            // Second half: sidechain→main (with flipped inputs), k goes 1→0
            float t = (morphValue - 0.5f) * 2.0f;  // 0.0→1.0

            // k ramps from 1 back down to 0
            k = 1.0f - t;

            // Dry blend at the very end
            if (morphValue > 0.95f)
            {
                sidechainDryBlend = (morphValue - 0.95f) / 0.05f;
                morphedBlend = 1.0f - sidechainDryBlend;
            }
            else
            {
                sidechainDryBlend = 0.0f;
                morphedBlend = 1.0f;
            }

            flipInputs = true;  // Swap inputs for second half
        }
    }

    // Set up input pointers (swap if needed)
    const float* processMain = flipInputs ? sidechainInput : mainInput;
    const float* processSidechain = flipInputs ? mainInput : sidechainInput;

    // Process with optimal transport (using potentially swapped inputs)
    int algorithmIndex = algorithmParam->getIndex();
    bool hasProcessor = (algorithmIndex == 0 && cdfProcessor) ||
                        (algorithmIndex == 1 && reassignmentProcessor);

    if (hasProcessor && morphedBlend > 0.001f)
    {
        // Copy swapped inputs to temporary buffers for processing
        std::vector<float> tempMain(numSamples);
        std::vector<float> tempSidechain(numSamples);
        std::memcpy(tempMain.data(), processMain, numSamples * sizeof(float));
        std::memcpy(tempSidechain.data(), processSidechain, numSamples * sizeof(float));

        // Use selected algorithm
        if (algorithmIndex == 0)
        {
            cdfProcessor->process(
                tempMain.data(),
                tempSidechain.data(),
                mainInput,  // Output to main buffer
                numSamples,
                k
            );
        }
        else
        {
            reassignmentProcessor->process(
                tempMain.data(),
                tempSidechain.data(),
                mainInput,  // Output to main buffer
                numSamples,
                k
            );
        }

        // Apply gain compensation to prevent loudness increase at k=0.5
        // The transport algorithm can increase energy when blending maximally
        float gainCompensation = 1.0f;
        if (morphMode == 1)  // Dry at Extremes mode
        {
            // Apply -3dB compensation around the flip point (k near 1.0)
            // This prevents the "extra loud" issue at morphValue=0.5
            if (k > 0.7f)
            {
                // Smoothly ramp compensation from 1.0 at k=0.7 to ~0.707 at k=1.0
                float compensation = 1.0f - ((k - 0.7f) / 0.3f) * 0.293f;  // -3dB = 0.707
                gainCompensation = compensation;
            }
        }

        // Blend with dry signals
        for (int i = 0; i < numSamples; ++i)
        {
            float morphed = mainInput[i] * gainCompensation;
            float output = morphed * morphedBlend
                         + dryMain[i] * mainDryBlend
                         + drySidechain[i] * sidechainDryBlend;

            // Apply dry/wet mix
            mainInput[i] = dryMain[i] * (1.0f - dryWetPercent) + output * dryWetPercent;
        }
    }
    else
    {
        // Just blend dry signals
        for (int i = 0; i < numSamples; ++i)
        {
            float output = dryMain[i] * mainDryBlend + drySidechain[i] * sidechainDryBlend;
            mainInput[i] = dryMain[i] * (1.0f - dryWetPercent) + output * dryWetPercent;
        }
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
    stream.writeInt(morphModeParam->getIndex());
    stream.writeFloat(dryWetParam->get());
    stream.writeInt(algorithmParam->getIndex());
}

void AudioTransportProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Restore parameter state
    juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);

    morphParam->setValueNotifyingHost(stream.readFloat());
    windowSizeParam->setValueNotifyingHost(stream.readFloat());
    bypassParam->setValueNotifyingHost(stream.readBool());

    // Check if we have new parameters (for backward compatibility)
    if (stream.getPosition() < sizeInBytes)
    {
        morphModeParam->setValueNotifyingHost(stream.readInt() / (float)(morphModeParam->choices.size() - 1));

        if (stream.getPosition() < sizeInBytes)
            dryWetParam->setValueNotifyingHost(stream.readFloat());

        if (stream.getPosition() < sizeInBytes)
            algorithmParam->setValueNotifyingHost(stream.readInt() / (float)(algorithmParam->choices.size() - 1));
    }

    needsProcessorRebuild = true;
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioTransportProcessor();
}
