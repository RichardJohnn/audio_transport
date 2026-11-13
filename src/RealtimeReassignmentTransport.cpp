#include "audio_transport/RealtimeReassignmentTransport.hpp"
#include <algorithm>

using namespace audio_transport;

RealtimeReassignmentTransport::RealtimeReassignmentTransport(
    double sample_rate, double window_ms, int hop_divisor, int fft_mult)
    : sample_rate_(sample_rate),
      params_(spectral::stft_params::from_python(sample_rate, window_ms, hop_divisor, fft_mult)),
      analyzer_(sample_rate, params_), synthesizer_(params_),
      main_buffer_(params_.window, 0.0), sidechain_buffer_(params_.window, 0.0),
      overlap_buffer_(params_.window, 0.0) {}

RealtimeReassignmentTransport::~RealtimeReassignmentTransport() = default;

void RealtimeReassignmentTransport::reset() {
    std::fill(main_buffer_.begin(), main_buffer_.end(), 0.0);
    std::fill(sidechain_buffer_.begin(), sidechain_buffer_.end(), 0.0);
    std::fill(overlap_buffer_.begin(), overlap_buffer_.end(), 0.0);
    phases_.clear();
    input_pos_ = ready_pos_ = ready_count_ = frame_index_ = 0;
}

int RealtimeReassignmentTransport::getLatencySamples() const {
    // A full first window must arrive before its first hop can be emitted.
    // Subsequent windows arrive one hop apart, independent of host block size.
    return static_cast<int>(params_.window);
}

void RealtimeReassignmentTransport::processHop(float k) {
    const double hop_seconds = params_.hop / sample_rate_;
    const double time = (frame_index_ * params_.hop + (params_.window - 1) / 2.0) / sample_rate_;
    auto main = analyzer_.analyze(main_buffer_.data(), time);
    auto side = analyzer_.analyze(sidechain_buffer_.data(), time);
    if (phases_.empty())
        phases_ = initial_phases(main, hop_seconds);
    auto frame = synthesizer_.synthesize(interpolate_hop(main, side, phases_, hop_seconds, k));
    for (size_t i = 0; i < params_.window; ++i)
        overlap_buffer_[i] += frame[i];
    ready_pos_ = 0;
    ready_count_ = params_.hop;
    ++frame_index_;
}

void RealtimeReassignmentTransport::process(const float* main, const float* side,
                                           float* output, int count, float k) {
    for (int i = 0; i < count; ++i) {
        // Read inputs before writing output, permitting in-place processing.
        const double a = main[i], b = side[i];
        output[i] = ready_count_ ? static_cast<float>(overlap_buffer_[ready_pos_++]) : 0.0f;
        if (ready_count_ && --ready_count_ == 0) {
            std::move(overlap_buffer_.begin() + params_.hop, overlap_buffer_.end(), overlap_buffer_.begin());
            std::fill(overlap_buffer_.end() - params_.hop, overlap_buffer_.end(), 0.0);
        }
        main_buffer_[input_pos_] = a;
        sidechain_buffer_[input_pos_++] = b;
        if (input_pos_ == params_.window) {
            processHop(k);
            std::move(main_buffer_.begin() + params_.hop, main_buffer_.end(), main_buffer_.begin());
            std::move(sidechain_buffer_.begin() + params_.hop, sidechain_buffer_.end(), sidechain_buffer_.begin());
            input_pos_ = params_.window - params_.hop;
        }
    }
}
