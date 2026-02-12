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
     * @param fft_padding FFT padding multiplier (2 = 2x padding)
     */
    RealtimeReassignmentTransport(
        double sample_rate,
        double window_ms,
        int hop_divisor = 4,
        int fft_padding = 2
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

private:
    // Audio parameters
    double sample_rate_;
    double window_size_; // in seconds
    int window_samples_;
    int window_padded_;
    int hop_size_;
    int hop_divisor_;
    int fft_padding_;
    int fft_size_;

    // Input buffers (accumulate samples until we have a full hop)
    std::vector<float> main_buffer_;
    std::vector<float> sidechain_buffer_;
    int input_write_pos_;

    // Output buffer (stores processed samples waiting to be output)
    std::vector<float> output_buffer_;
    int output_read_pos_;

    // Spectral analysis windows
    std::vector<double> window_;
    std::vector<double> window_t_;
    std::vector<double> window_d_;

    // FFT plans
    fftw_plan fft_plan_;
    fftw_plan fft_plan_t_;
    fftw_plan fft_plan_d_;
    fftw_plan ifft_plan_;

    // FFT buffers
    fftw_complex* fft_;
    fftw_complex* fft_t_;
    fftw_complex* fft_d_;
    fftw_complex* ifft_;

    // Phase continuity tracking
    std::vector<double> phases_;

    // Overlap-add state
    std::vector<double> overlap_buffer_;

    // Helper methods
    void analyzeWindow(
        const float* input,
        std::vector<spectral::point>& spectrum
    );

    void synthesizeWindow(
        const std::vector<spectral::point>& spectrum,
        float* output
    );

    void processHop(float k);
};

} // namespace audio_transport
