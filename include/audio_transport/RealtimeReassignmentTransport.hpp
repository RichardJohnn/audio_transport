#pragma once

#include <vector>
#include <complex>
#include <fftw3.h>
#include <memory>
#include "audio_transport/spectral.hpp"
#include "audio_transport/audio_transport.hpp"

namespace audio_transport {

/**
 * Real-time streaming audio transport processor using spectral reassignment.
 *
 * This uses the more complex Henderson & Solomon (DAFx 2019) algorithm with
 * spectral reassignment for better frequency localization. Potentially
 * higher quality but more computationally expensive than CDF-based approach.
 *
 * Usage:
 *   1. Create instance with desired sample rate and window parameters
 *   2. Call process() for each audio buffer with interpolation factor k
 */
class RealtimeReassignmentTransport {
public:
    /**
     * Constructor
     * @param sample_rate Audio sample rate in Hz
     * @param window_ms Window size in milliseconds
     * @param hop_divisor Hop divisor (4 = 75% overlap, 2 = 50% overlap)
     * @param fft_mult Multiplier of nextpow2(window samples) for FFT length
     */
    RealtimeReassignmentTransport(
        double sample_rate,
        double window_ms,
        int hop_divisor = 4,
        int fft_mult = 2
    );

    ~RealtimeReassignmentTransport();

    /**
     * Process a buffer of audio samples
     * @param input_main Main input buffer
     * @param input_sidechain Sidechain input buffer
     * @param output Output buffer (will be filled with morphed audio)
     * @param buffer_size Number of samples in each buffer
     * @param k Interpolation factor (0.0 = main, 1.0 = sidechain)
     */
    void process(
        const float* input_main,
        const float* input_sidechain,
        float* output,
        int buffer_size,
        float k
    );

    /**
     * Get the latency introduced by this processor in samples
     */
    int getLatencySamples() const;

    /**
     * Reset internal state (clear buffers)
     */
    void reset();

    int getHopSize() const { return static_cast<int>(params_.hop); }
    int getFFTSize() const { return static_cast<int>(params_.fft_size); }
    int getWindowSize() const { return static_cast<int>(params_.window); }

private:
    double sample_rate_;
    spectral::stft_params params_;
    spectral::analyzer analyzer_;
    spectral::synthesizer synthesizer_;
    std::vector<double> main_buffer_, sidechain_buffer_, overlap_buffer_;
    std::vector<double> phases_;
    size_t input_pos_ = 0;
    size_t ready_pos_ = 0;
    size_t ready_count_ = 0;
    size_t frame_index_ = 0;
    void processHop(float k);
};

} // namespace audio_transport
