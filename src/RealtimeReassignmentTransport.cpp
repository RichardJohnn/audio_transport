#include "audio_transport/RealtimeReassignmentTransport.hpp"
#include "audio_transport/spectral.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <iostream>

using namespace audio_transport;

RealtimeReassignmentTransport::RealtimeReassignmentTransport(
    double sample_rate,
    double window_ms,
    int hop_divisor,
    int fft_padding
) : sample_rate_(sample_rate),
    window_size_(window_ms / 1000.0),
    hop_divisor_(hop_divisor),
    fft_padding_(fft_padding),
    input_write_pos_(0),
    output_read_pos_(0)
{
    // Calculate window size in samples (must be even for symmetry)
    window_samples_ = static_cast<int>(std::round(window_size_ * sample_rate));
    while (window_samples_ % (2 * hop_divisor_) != 0) {
        window_samples_++;
    }

    window_padded_ = window_samples_ * (1 + fft_padding_);
    hop_size_ = window_samples_ / (2 * hop_divisor_);
    fft_size_ = window_padded_ / 2 + 1;

    // Calculate latency: we need (2 * hop_divisor - 1) hops before first output
    int latency_hops = 2 * hop_divisor_ - 1;
    int latency_samples = latency_hops * hop_size_;

    // Allocate input buffers (need to accumulate samples for analysis)
    int input_buffer_size = window_samples_ + hop_size_;
    main_buffer_.resize(input_buffer_size, 0.0f);
    sidechain_buffer_.resize(input_buffer_size, 0.0f);

    // Allocate output buffer (stores latency + some extra for processing)
    output_buffer_.resize(latency_samples + hop_size_ * 4, 0.0f);

    // Allocate spectral analysis windows
    window_.resize(window_padded_, 0.0);
    window_t_.resize(window_padded_, 0.0);
    window_d_.resize(window_padded_, 0.0);

    // Allocate FFT buffers
    fft_ = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * fft_size_);
    fft_t_ = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * fft_size_);
    fft_d_ = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * fft_size_);
    ifft_ = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * fft_size_);

    // Create FFTW plans
    fft_plan_ = fftw_plan_dft_r2c_1d(window_padded_, window_.data(), fft_, FFTW_MEASURE);
    fft_plan_t_ = fftw_plan_dft_r2c_1d(window_padded_, window_t_.data(), fft_t_, FFTW_MEASURE);
    fft_plan_d_ = fftw_plan_dft_r2c_1d(window_padded_, window_d_.data(), fft_d_, FFTW_MEASURE);
    ifft_plan_ = fftw_plan_dft_c2r_1d(window_padded_, ifft_, window_.data(), FFTW_MEASURE);

    // Initialize phase tracking
    phases_.resize(fft_size_, 0.0);

    // Initialize overlap-add buffer
    overlap_buffer_.resize(window_samples_ + hop_size_, 0.0);
}

RealtimeReassignmentTransport::~RealtimeReassignmentTransport() {
    fftw_destroy_plan(fft_plan_);
    fftw_destroy_plan(fft_plan_t_);
    fftw_destroy_plan(fft_plan_d_);
    fftw_destroy_plan(ifft_plan_);
    fftw_free(fft_);
    fftw_free(fft_t_);
    fftw_free(fft_d_);
    fftw_free(ifft_);
}

void RealtimeReassignmentTransport::reset() {
    std::fill(main_buffer_.begin(), main_buffer_.end(), 0.0f);
    std::fill(sidechain_buffer_.begin(), sidechain_buffer_.end(), 0.0f);
    std::fill(output_buffer_.begin(), output_buffer_.end(), 0.0f);
    std::fill(phases_.begin(), phases_.end(), 0.0);
    std::fill(overlap_buffer_.begin(), overlap_buffer_.end(), 0.0);
    input_write_pos_ = 0;
    output_read_pos_ = 0;
}

int RealtimeReassignmentTransport::getLatencySamples() const {
    // Latency is (2 * hop_divisor - 1) hops
    return (2 * hop_divisor_ - 1) * hop_size_;
}

void RealtimeReassignmentTransport::analyzeWindow(
    const float* input,
    std::vector<spectral::point>& spectrum
) {
    int padding_samples = (window_padded_ - window_samples_) / 2;

    // Clear padded windows
    std::fill(window_.begin(), window_.end(), 0.0);
    std::fill(window_t_.begin(), window_t_.end(), 0.0);
    std::fill(window_d_.begin(), window_d_.end(), 0.0);

    // Apply windowing functions
    for (int i = 0; i < window_samples_; i++) {
        double n = i - (window_samples_ - 1) / 2.0;
        double a = input[i];

        window_[i + padding_samples] = a * spectral::hann(n, window_samples_);
        window_t_[i + padding_samples] = a * spectral::hann_t(n, window_samples_, sample_rate_);
        window_d_[i + padding_samples] = a * spectral::hann_d(n, window_samples_, sample_rate_);
    }

    // Execute FFT plans
    fftw_execute(fft_plan_);
    fftw_execute(fft_plan_t_);
    fftw_execute(fft_plan_d_);

    // Compute center time
    double center_time = 0.0; // Relative to window center

    // Build spectral points
    spectrum.resize(fft_size_);
    for (int i = 0; i < fft_size_; i++) {
        std::complex<double> X(fft_[i][0], fft_[i][1]);
        std::complex<double> X_t(fft_t_[i][0], fft_t_[i][1]);
        std::complex<double> X_d(fft_d_[i][0], fft_d_[i][1]);

        spectrum[i].value = X;

        // Compute frequency
        spectrum[i].freq = (2.0 * M_PI * i) / window_padded_ * sample_rate_;

        // Compute time (not used much in current algorithm)
        spectrum[i].time = center_time;

        // Compute reassigned frequency
        double mag = std::abs(X);
        if (mag > 1e-10) {
            double freq_offset = -std::imag(X_d / X) / (2.0 * M_PI);
            spectrum[i].freq_reassigned = spectrum[i].freq + freq_offset;
        } else {
            spectrum[i].freq_reassigned = spectrum[i].freq;
        }

        // Compute reassigned time
        if (mag > 1e-10) {
            double time_offset = std::real(X_t / X);
            spectrum[i].time_reassigned = center_time + time_offset;
        } else {
            spectrum[i].time_reassigned = center_time;
        }
    }
}

void RealtimeReassignmentTransport::synthesizeWindow(
    const std::vector<spectral::point>& spectrum,
    float* output
) {
    // Fill IFFT buffer
    for (size_t i = 0; i < spectrum.size(); i++) {
        ifft_[i][0] = std::real(spectrum[i].value);
        ifft_[i][1] = std::imag(spectrum[i].value);
    }

    // Execute IFFT
    fftw_execute(ifft_plan_);

    // Extract windowed samples (with overlap-add)
    int padding_samples = (window_padded_ - window_samples_) / 2;

    for (int i = 0; i < window_samples_; i++) {
        // Scale down to correct for FFT and overlap
        double value = window_[i + padding_samples] / (hop_divisor_ * window_padded_);

        // Safety check: clamp NaN/Inf values
        if (!std::isfinite(value)) {
            value = 0.0;
        }

        // Add to overlap buffer
        overlap_buffer_[i] += value;
    }

    // Copy out the ready samples (one hop's worth)
    for (int i = 0; i < hop_size_; i++) {
        output[i] = static_cast<float>(overlap_buffer_[i]);
    }

    // Shift overlap buffer
    std::memmove(overlap_buffer_.data(), overlap_buffer_.data() + hop_size_,
                 (overlap_buffer_.size() - hop_size_) * sizeof(double));
    std::fill(overlap_buffer_.end() - hop_size_, overlap_buffer_.end(), 0.0);
}

void RealtimeReassignmentTransport::processHop(float k) {
    // Analyze main and sidechain inputs
    std::vector<spectral::point> main_spectrum;
    std::vector<spectral::point> sidechain_spectrum;

    analyzeWindow(main_buffer_.data(), main_spectrum);
    analyzeWindow(sidechain_buffer_.data(), sidechain_spectrum);

    // Perform optimal transport interpolation
    std::vector<spectral::point> morphed_spectrum =
        interpolate(main_spectrum, sidechain_spectrum, phases_, window_size_, k);

    // Synthesize output
    std::vector<float> hop_output(hop_size_);
    synthesizeWindow(morphed_spectrum, hop_output.data());

    // Write to output buffer
    for (int i = 0; i < hop_size_; i++) {
        int write_idx = (output_read_pos_ + getLatencySamples() + i) % output_buffer_.size();
        output_buffer_[write_idx] = hop_output[i];
    }
}

void RealtimeReassignmentTransport::process(
    const float* input_main,
    const float* input_sidechain,
    float* output,
    int buffer_size,
    float k
) {
    int samples_processed = 0;

    while (samples_processed < buffer_size) {
        // How many samples can we accept before the next hop?
        int samples_until_hop = hop_size_ - input_write_pos_;
        int samples_to_copy = std::min(samples_until_hop, buffer_size - samples_processed);

        // Copy input samples to buffers
        std::memcpy(main_buffer_.data() + window_samples_ - hop_size_ + input_write_pos_,
                    input_main + samples_processed,
                    samples_to_copy * sizeof(float));
        std::memcpy(sidechain_buffer_.data() + window_samples_ - hop_size_ + input_write_pos_,
                    input_sidechain + samples_processed,
                    samples_to_copy * sizeof(float));

        input_write_pos_ += samples_to_copy;
        samples_processed += samples_to_copy;

        // If we've filled a hop, process it
        if (input_write_pos_ >= hop_size_) {
            processHop(k);

            // Shift input buffers
            std::memmove(main_buffer_.data(), main_buffer_.data() + hop_size_,
                        (main_buffer_.size() - hop_size_) * sizeof(float));
            std::memmove(sidechain_buffer_.data(), sidechain_buffer_.data() + hop_size_,
                        (sidechain_buffer_.size() - hop_size_) * sizeof(float));
            std::fill(main_buffer_.end() - hop_size_, main_buffer_.end(), 0.0f);
            std::fill(sidechain_buffer_.end() - hop_size_, sidechain_buffer_.end(), 0.0f);

            input_write_pos_ = 0;
        }
    }

    // Copy output samples
    for (int i = 0; i < buffer_size; i++) {
        output[i] = output_buffer_[output_read_pos_];
        output_buffer_[output_read_pos_] = 0.0f;
        output_read_pos_ = (output_read_pos_ + 1) % output_buffer_.size();
    }
}
