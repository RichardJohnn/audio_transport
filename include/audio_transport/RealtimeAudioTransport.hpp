#pragma once

#include <vector>
#include <complex>
#include <memory>
#include <fftw3.h>

namespace audio_transport {

/**
 * Real-time Audio Transport processor for VST3 plugins
 *
 * This implementation uses a simpler CDF-based optimal transport approach
 * suitable for real-time processing with sidechain input.
 *
 * Based on Henderson & Solomon (DAFx 2019) - simplified Python implementation
 */
class RealtimeAudioTransport {
public:
    /**
     * Constructor
     *
     * @param sample_rate Sample rate in Hz
     * @param window_ms Window size in milliseconds (default: 100ms)
     * @param hop_divisor Hop size as fraction of window (4 = 75% overlap)
     * @param fft_mult FFT size multiplier for zero-padding (2 = 2x zero-padding)
     */
    RealtimeAudioTransport(
        double sample_rate = 44100.0,
        double window_ms = 100.0,
        int hop_divisor = 4,
        int fft_mult = 2
    );

    ~RealtimeAudioTransport();

    // Prevent copying (due to FFTW plans)
    RealtimeAudioTransport(const RealtimeAudioTransport&) = delete;
    RealtimeAudioTransport& operator=(const RealtimeAudioTransport&) = delete;

    /**
     * Process a buffer of audio samples
     *
     * @param input_main Main input buffer
     * @param input_sidechain Sidechain input buffer (same size as input_main)
     * @param output Output buffer (same size as input_main)
     * @param buffer_size Number of samples to process
     * @param k_value Interpolation factor (0.0 = main, 1.0 = sidechain)
     */
    void process(
        const float* input_main,
        const float* input_sidechain,
        float* output,
        int buffer_size,
        float k_value = 0.5f
    );

    /**
     * Reset the processor state (clear buffers, reset phase tracking)
     */
    void reset();

    /**
     * Set sample rate (requires reconstruction of FFTW plans)
     */
    void setSampleRate(double sample_rate);

    /**
     * Get current latency in samples
     */
    int getLatencySamples() const { return window_size_ / 2; }

private:
    // Parameters
    double sample_rate_;
    int window_size_;      // Window size in samples
    int hop_size_;         // Hop size in samples
    int fft_size_;         // FFT size (with zero-padding)
    int num_bins_;         // Number of frequency bins (fft_size/2 + 1)

    // Buffers for input accumulation
    std::vector<double> main_buffer_;
    std::vector<double> sidechain_buffer_;
    std::vector<double> output_buffer_;
    int buffer_write_pos_;
    int buffer_read_pos_;
    int samples_in_buffer_;

    // Hann window
    std::vector<double> window_;

    // FFTW plans and buffers
    fftw_plan fft_plan_;
    fftw_plan ifft_plan_;
    double* fft_input_;
    fftw_complex* fft_output_;
    fftw_complex* ifft_input_;
    double* ifft_output_;

    // Spectral buffers for processing
    std::vector<std::complex<double>> spectrum_main_;
    std::vector<std::complex<double>> spectrum_sidechain_;
    std::vector<std::complex<double>> spectrum_output_;

    // Phase tracking for coherence
    std::vector<double> phases_;

    // Overlap-add buffer for output
    std::vector<double> ola_buffer_;
    int ola_write_pos_;

    // Helper functions
    void initializeFFTW();
    void destroyFFTW();
    void computeSTFT(const std::vector<double>& input_buffer,
                     std::vector<std::complex<double>>& spectrum);
    void computeISTFT(const std::vector<std::complex<double>>& spectrum,
                      std::vector<double>& output_frame);

    /**
     * Compute 1D optimal transport map from X to Y using CDFs
     *
     * @param mag_X Source magnitude spectrum
     * @param mag_Y Target magnitude spectrum
     * @param transport_map Output: transport_map[i] is the target bin for source bin i
     */
    void computeTransportMap(
        const std::vector<double>& mag_X,
        const std::vector<double>& mag_Y,
        std::vector<int>& transport_map
    );

    /**
     * Interpolate spectrum using optimal transport
     *
     * @param mag_X Source magnitude spectrum
     * @param phase_X Source phase spectrum
     * @param mag_Y Target magnitude spectrum
     * @param phase_Y Target phase spectrum
     * @param k Interpolation factor (0.0 = source, 1.0 = target)
     * @param mag_out Output magnitude spectrum
     * @param phase_out Output phase spectrum
     */
    void interpolateSpectrum(
        const std::vector<double>& mag_X,
        const std::vector<double>& phase_X,
        const std::vector<double>& mag_Y,
        const std::vector<double>& phase_Y,
        double k,
        std::vector<double>& mag_out,
        std::vector<double>& phase_out
    );
};

} // namespace audio_transport
