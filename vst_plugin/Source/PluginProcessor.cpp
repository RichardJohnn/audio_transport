#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>
#include <vector>
#include <mutex>
#include <chrono>

namespace { std::mutex fftwPlannerMutex; }

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
        juce::NormalisableRange<float>(1.0f, 500.0f, 1.0f),
        100.0f,
        "Window size in milliseconds (1ms = glitchy, 500ms = smooth)"
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
        juce::StringArray("CDF (Clean)", "Reassignment (Glitchy)"),
        0,  // Default to CDF (clean)
        "Transport algorithm"
    ));

    addParameter(hopDivisorParam = new juce::AudioParameterChoice(
        "hopDivisor",
        "Overlap",
        juce::StringArray("50% (2)", "75% (4)", "87.5% (8)", "93.75% (16)"),
        1,  // Default to 75% (4)
        "Window overlap amount (higher = smoother but more latency)"
    ));

    addParameter(fftMultiplierParam = new juce::AudioParameterChoice(
        "fftMultiplier",
        "FFT Size",
        juce::StringArray("2x", "4x", "8x", "16x", "32x", "64x", "128x"),
        0,  // Default to 2x
        "FFT zero-padding multiplier (higher = smoother spectrum but more CPU)"
    ));
    builder = std::thread([this] { buildLoop(); });
}

AudioTransportProcessor::~AudioTransportProcessor()
{
    stopping.store(true);
    builder.join();
    std::lock_guard<std::mutex> lock(fftwPlannerMutex);
    delete pending.exchange(nullptr);
    delete retired.exchange(nullptr);
    delete active;
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

    requestedSampleRate.store(sampleRate);
    ++prepareGeneration;
    requestProcessor();

    // Hosts read latency after prepareToPlay and may ignore changes made during
    // playback, so report it now instead of waiting for the background build.
    // Both formulas are plain arithmetic; they must match each class's
    // getLatencySamples().
    const double windowMs = windowSizeParam->get();
    int latency = 0;
    if (algorithmParam->getIndex() == 1)
    {
        const int hopDiv = 2 << hopDivisorParam->getIndex();
        const int fftMult = 2 << fftMultiplierParam->getIndex();
        latency = static_cast<int>(audio_transport::spectral::stft_params::from_python(
            sampleRate, windowMs, static_cast<unsigned int>(hopDiv),
            static_cast<unsigned int>(fftMult)).window);
    }
    else
    {
        latency = static_cast<int>(windowMs * sampleRate / 1000.0) / 2;
    }
    latency = std::max(1, latency);
    reportedLatency.store(latency);
    setLatencySamples(latency);
}

void AudioTransportProcessor::requestProcessor()
{
    lastWindowSize = windowSizeParam->get();
    lastAlgorithm = algorithmParam->getIndex();
    lastHopDivisor = hopDivisorParam->getIndex();
    lastFftMult = fftMultiplierParam->getIndex();
    // All exposed geometry choices are integral. Publish one coherent snapshot.
    // The prepare generation also forces a fresh state on each playback setup;
    // sample rate is kept separately to preserve non-integral rates.
    const uint64_t key = (prepareGeneration & ((1ULL << 20) - 1))
        | (static_cast<uint64_t>(lastWindowSize) << 20)
        | (static_cast<uint64_t>(lastHopDivisor) << 29)
        | (static_cast<uint64_t>(lastFftMult) << 31)
        | (static_cast<uint64_t>(lastAlgorithm) << 34);
    requestedKey.store(key);
}

void AudioTransportProcessor::buildLoop()
{
    uint64_t builtKey = 0;
    while (!stopping.load())
    {
        {
            // FFTW planner and plan destruction are serialized across plugin instances.
            std::lock_guard<std::mutex> lock(fftwPlannerMutex);
            delete retired.exchange(nullptr);
        }
        const auto key = requestedKey.load();
        if (key != 0 && key != builtKey)
        {
            std::lock_guard<std::mutex> lock(fftwPlannerMutex);
            std::unique_ptr<ProcessorState> next(new ProcessorState);
            next->key = key;
            const double rate = requestedSampleRate.load();
            const double window = (key >> 20) & 511;
            const int hop = 2 << ((key >> 29) & 3);
            const int fft = 2 << ((key >> 31) & 7);
            try
            {
                if ((key >> 34) & 1)
                {
                    next->reassignmentProcessor.reset(new audio_transport::RealtimeReassignmentTransport(rate, window, hop, fft));
                    next->delayBufferSize = next->reassignmentProcessor->getLatencySamples();
                }
                else
                {
                    next->cdfProcessor.reset(new audio_transport::RealtimeAudioTransport(rate, window, hop, fft));
                    next->delayBufferSize = next->cdfProcessor->getLatencySamples();
                }
                next->delayBufferSize = std::max(1, next->delayBufferSize);
                next->mainDelayBuffer.assign(next->delayBufferSize, 0.0f);
                next->sidechainDelayBuffer.assign(next->delayBufferSize, 0.0f);
                if (requestedKey.load() == key && !stopping.load())
                    delete pending.exchange(next.release());
            }
            catch (const std::exception&)
            {
                // Keep output silent until a new valid configuration is requested.
            }
            builtKey = key;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void AudioTransportProcessor::releaseResources()
{
    if (active)
    {
        if (active->cdfProcessor) active->cdfProcessor->reset();
        if (active->reassignmentProcessor) active->reassignmentProcessor->reset();
        std::fill(active->mainDelayBuffer.begin(), active->mainDelayBuffer.end(), 0.0f);
        std::fill(active->sidechainDelayBuffer.begin(), active->sidechainDelayBuffer.end(), 0.0f);
        active->delayBufferWritePos = 0;
    }
}

int AudioTransportProcessor::getLatencySamples() const
{
    return reportedLatency.load();
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

    if (windowSizeParam->get() != lastWindowSize
        || algorithmParam->getIndex() != lastAlgorithm
        || hopDivisorParam->getIndex() != lastHopDivisor
        || fftMultiplierParam->getIndex() != lastFftMult)
        requestProcessor();

    // The single retirement slot prevents destruction on the audio thread.
    if (retired.load() == nullptr)
    {
        if (auto* next = pending.exchange(nullptr))
        {
            if (next->key == requestedKey.load())
            {
                auto* old = active;
                active = next;
                retired.store(old);
                reportedLatency.store(active->delayBufferSize);
                setLatencySamples(active->delayBufferSize);
            }
            else
                retired.store(next);
        }
    }
    if (!active || active->key != requestedKey.load())
    {
        buffer.clear();
        return;
    }

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

    if (active->delayBufferSize > 0)
    {
        // Process delay buffers sample-by-sample
        for (int i = 0; i < numSamples; ++i)
        {
            // For circular buffer: read the oldest sample (at current write position)
            // then write the new sample, overwriting the old one
            dryMain[i] = active->mainDelayBuffer[active->delayBufferWritePos];
            drySidechain[i] = active->sidechainDelayBuffer[active->delayBufferWritePos];

            // Write new samples
            active->mainDelayBuffer[active->delayBufferWritePos] = mainInput[i];
            active->sidechainDelayBuffer[active->delayBufferWritePos] = sidechainInput[i];

            // Advance position (circular)
            active->delayBufferWritePos = (active->delayBufferWritePos + 1) % active->delayBufferSize;
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
    bool hasProcessor = (algorithmIndex == 0 && active->cdfProcessor) ||
                        (algorithmIndex == 1 && active->reassignmentProcessor);

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
            active->cdfProcessor->process(
                tempMain.data(),
                tempSidechain.data(),
                mainInput,  // Output to main buffer
                numSamples,
                k
            );
        }
        else
        {
            active->reassignmentProcessor->process(
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
    stream.writeInt(hopDivisorParam->getIndex());
    stream.writeInt(fftMultiplierParam->getIndex());
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

        if (stream.getPosition() < sizeInBytes)
            hopDivisorParam->setValueNotifyingHost(stream.readInt() / (float)(hopDivisorParam->choices.size() - 1));

        if (stream.getPosition() < sizeInBytes)
            fftMultiplierParam->setValueNotifyingHost(stream.readInt() / (float)(fftMultiplierParam->choices.size() - 1));
    }

    // Geometry changes are observed at the next audio block.
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioTransportProcessor();
}
