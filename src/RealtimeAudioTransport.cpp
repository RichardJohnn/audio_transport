#include "audio_transport/RealtimeAudioTransport.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cstring>

namespace audio_transport {

RealtimeAudioTransport::RealtimeAudioTransport(
    double sample_rate,
    double window_ms,
    int hop_divisor,
    int fft_mult)
    : sample_rate_(sample_rate)
    , buffer_write_pos_(0)
    , buffer_read_pos_(0)
    , samples_in_buffer_(0)
    , ola_write_pos_(0)
{
    // Calculate sizes
    window_size_ = static_cast<int>(window_ms * sample_rate / 1000.0);
    hop_size_ = window_size_ / hop_divisor;

    // FFT size: next power of 2, multiplied by fft_mult
    int next_pow2 = static_cast<int>(std::pow(2, std::ceil(std::log2(window_size_))));
    fft_size_ = next_pow2 * fft_mult;
    num_bins_ = fft_size_ / 2 + 1;

    std::cout << "[RealtimeAudioTransport] Initialized:" << std::endl;
    std::cout << "  Sample rate: " << sample_rate_ << " Hz" << std::endl;
    std::cout << "  Window size: " << window_size_ << " samples (" << window_ms << " ms)" << std::endl;
    std::cout << "  Hop size: " << hop_size_ << " samples" << std::endl;
    std::cout << "  FFT size: " << fft_size_ << " samples" << std::endl;
    std::cout << "  Frequency bins: " << num_bins_ << std::endl;
    std::cout << "  Latency: " << getLatencySamples() << " samples" << std::endl;

    // Initialize buffers
    main_buffer_.resize(window_size_, 0.0);
    sidechain_buffer_.resize(window_size_, 0.0);
    output_buffer_.resize(window_size_, 0.0);

    spectrum_main_.resize(num_bins_);
    spectrum_sidechain_.resize(num_bins_);
    spectrum_output_.resize(num_bins_);

    phases_.resize(num_bins_, 0.0);

    // Overlap-add buffer needs to store at least window_size samples
    ola_buffer_.resize(window_size_ * 2, 0.0);

    // Create Hann window
    window_.resize(window_size_);
    for (int i = 0; i < window_size_; ++i) {
        window_[i] = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (window_size_ - 1)));
    }

    // Initialize FFTW
    initializeFFTW();
}

RealtimeAudioTransport::~RealtimeAudioTransport() {
    destroyFFTW();
}

void RealtimeAudioTransport::initializeFFTW() {
    // Allocate FFTW buffers
    fft_input_ = fftw_alloc_real(fft_size_);
    fft_output_ = fftw_alloc_complex(num_bins_);
    ifft_input_ = fftw_alloc_complex(num_bins_);
    ifft_output_ = fftw_alloc_real(fft_size_);

    // Create FFTW plans
    fft_plan_ = fftw_plan_dft_r2c_1d(fft_size_, fft_input_, fft_output_, FFTW_MEASURE);
    ifft_plan_ = fftw_plan_dft_c2r_1d(fft_size_, ifft_input_, ifft_output_, FFTW_MEASURE);
}

void RealtimeAudioTransport::destroyFFTW() {
    if (fft_plan_) fftw_destroy_plan(fft_plan_);
    if (ifft_plan_) fftw_destroy_plan(ifft_plan_);
    if (fft_input_) fftw_free(fft_input_);
    if (fft_output_) fftw_free(fft_output_);
    if (ifft_input_) fftw_free(ifft_input_);
    if (ifft_output_) fftw_free(ifft_output_);
}

void RealtimeAudioTransport::reset() {
    // Clear all buffers
    std::fill(main_buffer_.begin(), main_buffer_.end(), 0.0);
    std::fill(sidechain_buffer_.begin(), sidechain_buffer_.end(), 0.0);
    std::fill(output_buffer_.begin(), output_buffer_.end(), 0.0);
    std::fill(ola_buffer_.begin(), ola_buffer_.end(), 0.0);
    std::fill(phases_.begin(), phases_.end(), 0.0);

    buffer_write_pos_ = 0;
    buffer_read_pos_ = 0;
    samples_in_buffer_ = 0;
    ola_write_pos_ = 0;
}

void RealtimeAudioTransport::setSampleRate(double sample_rate) {
    if (sample_rate == sample_rate_) return;

    sample_rate_ = sample_rate;

    // Recalculate window size to maintain same time duration
    double window_ms = (window_size_ * 1000.0) / sample_rate_;
    window_size_ = static_cast<int>(window_ms * sample_rate / 1000.0);
    hop_size_ = window_size_ / 4; // Keep same overlap ratio

    // Recalculate FFT size
    int next_pow2 = static_cast<int>(std::pow(2, std::ceil(std::log2(window_size_))));
    fft_size_ = next_pow2 * 2;
    num_bins_ = fft_size_ / 2 + 1;

    // Resize buffers
    main_buffer_.resize(window_size_, 0.0);
    sidechain_buffer_.resize(window_size_, 0.0);
    output_buffer_.resize(window_size_, 0.0);
    spectrum_main_.resize(num_bins_);
    spectrum_sidechain_.resize(num_bins_);
    spectrum_output_.resize(num_bins_);
    phases_.resize(num_bins_, 0.0);
    ola_buffer_.resize(window_size_ * 2, 0.0);

    // Recreate window
    window_.resize(window_size_);
    for (int i = 0; i < window_size_; ++i) {
        window_[i] = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (window_size_ - 1)));
    }

    // Recreate FFTW plans
    destroyFFTW();
    initializeFFTW();

    reset();
}

void RealtimeAudioTransport::computeSTFT(
    const std::vector<double>& input_buffer,
    std::vector<std::complex<double>>& spectrum)
{
    // Apply window and zero-padding
    std::memset(fft_input_, 0, fft_size_ * sizeof(double));

    int padding_offset = (fft_size_ - window_size_) / 2;
    for (int i = 0; i < window_size_; ++i) {
        fft_input_[padding_offset + i] = input_buffer[i] * window_[i];
    }

    // Execute FFT
    fftw_execute(fft_plan_);

    // Copy to complex spectrum
    for (int i = 0; i < num_bins_; ++i) {
        spectrum[i] = std::complex<double>(fft_output_[i][0], fft_output_[i][1]);
    }
}

void RealtimeAudioTransport::computeISTFT(
    const std::vector<std::complex<double>>& spectrum,
    std::vector<double>& output_frame)
{
    // Copy spectrum to FFTW buffer
    for (int i = 0; i < num_bins_; ++i) {
        ifft_input_[i][0] = spectrum[i].real();
        ifft_input_[i][1] = spectrum[i].imag();
    }

    // Execute inverse FFT
    fftw_execute(ifft_plan_);

    // Extract windowed samples and normalize
    output_frame.resize(window_size_);
    int padding_offset = (fft_size_ - window_size_) / 2;
    double norm = 1.0 / fft_size_;

    for (int i = 0; i < window_size_; ++i) {
        // Apply window again for overlap-add
        output_frame[i] = ifft_output_[padding_offset + i] * window_[i] * norm;
    }
}

void RealtimeAudioTransport::computeTransportMap(
    const std::vector<double>& mag_X,
    const std::vector<double>& mag_Y,
    std::vector<int>& transport_map)
{
    const double eps = 1e-10;
    transport_map.resize(num_bins_);

    // Normalize to probability distributions
    double sum_X = 0.0, sum_Y = 0.0;
    for (int i = 0; i < num_bins_; ++i) {
        sum_X += mag_X[i];
        sum_Y += mag_Y[i];
    }
    sum_X = std::max(sum_X, eps);
    sum_Y = std::max(sum_Y, eps);

    // Compute CDFs
    std::vector<double> cdf_X(num_bins_);
    std::vector<double> cdf_Y(num_bins_);

    double cumsum_X = 0.0, cumsum_Y = 0.0;
    for (int i = 0; i < num_bins_; ++i) {
        cumsum_X += mag_X[i] / sum_X;
        cumsum_Y += mag_Y[i] / sum_Y;
        cdf_X[i] = cumsum_X;
        cdf_Y[i] = cumsum_Y;
    }

    // Compute transport map: for each bin in X, find where it maps to in Y
    // T(x) = F_Y^{-1}(F_X(x))
    for (int i = 0; i < num_bins_; ++i) {
        // Binary search for smallest j where cdf_Y[j] >= cdf_X[i]
        int left = 0, right = num_bins_ - 1;
        int result = num_bins_ - 1;

        while (left <= right) {
            int mid = (left + right) / 2;
            if (cdf_Y[mid] >= cdf_X[i] - eps) {
                result = mid;
                right = mid - 1;
            } else {
                left = mid + 1;
            }
        }

        transport_map[i] = result;
    }
}

void RealtimeAudioTransport::interpolateSpectrum(
    const std::vector<double>& mag_X,
    const std::vector<double>& phase_X,
    const std::vector<double>& mag_Y,
    const std::vector<double>& phase_Y,
    double k,
    std::vector<double>& mag_out,
    std::vector<double>& phase_out)
{
    const double eps = 1e-10;

    mag_out.resize(num_bins_, 0.0);
    phase_out.resize(num_bins_, 0.0);

    // Compute transport map
    std::vector<int> transport_map;
    computeTransportMap(mag_X, mag_Y, transport_map);

    // Interpolated bin positions
    std::vector<double> interp_positions(num_bins_);
    for (int i = 0; i < num_bins_; ++i) {
        interp_positions[i] = (1.0 - k) * i + k * transport_map[i];
    }

    // Accumulate energy at interpolated positions
    std::vector<double> weight_sum(num_bins_, eps);
    std::vector<double> phase_num(num_bins_, 0.0);

    for (int i = 0; i < num_bins_; ++i) {
        double target_pos = interp_positions[i];
        int target_idx = transport_map[i];

        // Interpolate magnitude
        double interp_mag = (1.0 - k) * mag_X[i] + k * mag_Y[target_idx];

        // Distribute energy to neighboring bins (linear interpolation)
        int low_bin = static_cast<int>(std::floor(target_pos));
        int high_bin = static_cast<int>(std::ceil(target_pos));
        double frac = target_pos - low_bin;

        // Clamp to valid range
        low_bin = std::max(0, std::min(low_bin, num_bins_ - 1));
        high_bin = std::max(0, std::min(high_bin, num_bins_ - 1));

        // Accumulate magnitude
        if (low_bin < num_bins_) {
            double weight = (1.0 - frac) * interp_mag;
            mag_out[low_bin] += weight;
            weight_sum[low_bin] += weight;
            phase_num[low_bin] += weight * phase_X[i];
        }

        if (high_bin < num_bins_ && high_bin != low_bin) {
            double weight = frac * interp_mag;
            mag_out[high_bin] += weight;
            weight_sum[high_bin] += weight;
            phase_num[high_bin] += weight * phase_X[i];
        }
    }

    // Normalize phase
    for (int i = 0; i < num_bins_; ++i) {
        if (weight_sum[i] > eps) {
            phase_out[i] = phase_num[i] / weight_sum[i];
        } else {
            // Low energy - use target phase
            phase_out[i] = phase_Y[i];
        }
    }
}

void RealtimeAudioTransport::process(
    const float* input_main,
    const float* input_sidechain,
    float* output,
    int buffer_size,
    float k_value)
{
    // Process sample by sample
    for (int i = 0; i < buffer_size; ++i) {
        // Write to input buffers (circular)
        main_buffer_[buffer_write_pos_] = static_cast<double>(input_main[i]);
        sidechain_buffer_[buffer_write_pos_] = static_cast<double>(input_sidechain[i]);

        buffer_write_pos_ = (buffer_write_pos_ + 1) % window_size_;
        samples_in_buffer_++;

        // Process when we have enough samples for a hop
        if (samples_in_buffer_ >= hop_size_) {
            samples_in_buffer_ -= hop_size_;

            // Extract linear buffers for processing
            std::vector<double> main_frame(window_size_);
            std::vector<double> sidechain_frame(window_size_);

            for (int j = 0; j < window_size_; ++j) {
                int idx = (buffer_read_pos_ + j) % window_size_;
                main_frame[j] = main_buffer_[idx];
                sidechain_frame[j] = sidechain_buffer_[idx];
            }

            buffer_read_pos_ = (buffer_read_pos_ + hop_size_) % window_size_;

            // Compute STFTs
            computeSTFT(main_frame, spectrum_main_);
            computeSTFT(sidechain_frame, spectrum_sidechain_);

            // Extract magnitude and phase
            std::vector<double> mag_X(num_bins_), mag_Y(num_bins_);
            std::vector<double> phase_X(num_bins_), phase_Y(num_bins_);

            for (int j = 0; j < num_bins_; ++j) {
                mag_X[j] = std::abs(spectrum_main_[j]);
                mag_Y[j] = std::abs(spectrum_sidechain_[j]);
                phase_X[j] = std::arg(spectrum_main_[j]);
                phase_Y[j] = std::arg(spectrum_sidechain_[j]);
            }

            // Interpolate spectrum using optimal transport
            std::vector<double> mag_out, phase_out;
            interpolateSpectrum(mag_X, phase_X, mag_Y, phase_Y,
                              static_cast<double>(k_value), mag_out, phase_out);

            // Reconstruct complex spectrum
            for (int j = 0; j < num_bins_; ++j) {
                spectrum_output_[j] = std::polar(mag_out[j], phase_out[j]);
            }

            // Inverse STFT
            std::vector<double> output_frame;
            computeISTFT(spectrum_output_, output_frame);

            // Add to overlap-add buffer
            for (int j = 0; j < window_size_; ++j) {
                int ola_idx = (ola_write_pos_ + j) % ola_buffer_.size();
                ola_buffer_[ola_idx] += output_frame[j];
            }
        }

        // Read from overlap-add buffer
        output[i] = static_cast<float>(ola_buffer_[ola_write_pos_]);
        ola_buffer_[ola_write_pos_] = 0.0; // Clear after reading
        ola_write_pos_ = (ola_write_pos_ + 1) % ola_buffer_.size();
    }
}

} // namespace audio_transport
