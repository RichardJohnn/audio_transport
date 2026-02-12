#include <iostream>
#include <fstream>
#include <cmath>
#include <vector>
#include <cassert>

#include "audio_transport/spectral.hpp"
#include "audio_transport/audio_transport.hpp"

// Test parameters
const double SAMPLE_RATE = 44100.0;
const double WINDOW_SIZE = 0.05;  // 50ms windows
const unsigned int PADDING = 7;
const size_t BUFFER_SIZE = 4410;  // 100ms of audio

// Generate a sine wave
std::vector<double> generate_sine(double freq, double amplitude, size_t samples) {
    std::vector<double> audio(samples);
    for (size_t i = 0; i < samples; i++) {
        audio[i] = amplitude * std::sin(2.0 * M_PI * freq * i / SAMPLE_RATE);
    }
    return audio;
}

// Generate DC offset
std::vector<double> generate_dc(double offset, size_t samples) {
    return std::vector<double>(samples, offset);
}

// Generate impulse
std::vector<double> generate_impulse(size_t position, double amplitude, size_t samples) {
    std::vector<double> audio(samples, 0.0);
    if (position < samples) {
        audio[position] = amplitude;
    }
    return audio;
}

// Generate very low frequency (subsonic)
std::vector<double> generate_subsonic(double freq, double amplitude, size_t samples) {
    return generate_sine(freq, amplitude, samples);
}

// Compute RMS of signal
double compute_rms(const std::vector<double>& audio) {
    double sum = 0;
    for (double s : audio) {
        sum += s * s;
    }
    return std::sqrt(sum / audio.size());
}

// Compute peak of signal
double compute_peak(const std::vector<double>& audio) {
    double peak = 0;
    for (double s : audio) {
        peak = std::max(peak, std::abs(s));
    }
    return peak;
}

// Count discontinuities (potential clicks/pops)
size_t count_discontinuities(const std::vector<double>& audio, double threshold) {
    size_t count = 0;
    for (size_t i = 1; i < audio.size(); i++) {
        double diff = std::abs(audio[i] - audio[i-1]);
        if (diff > threshold) {
            count++;
        }
    }
    return count;
}

// Run transport interpolation and analyze results
struct TestResult {
    double input_rms;
    double output_rms;
    double input_peak;
    double output_peak;
    double energy_ratio;
    size_t discontinuities;
    bool has_nan;
    bool has_inf;
};

TestResult run_transport_test(
    const std::vector<double>& left,
    const std::vector<double>& right,
    double interpolation_factor,
    const std::string& test_name) {

    std::cout << "\n=== " << test_name << " ===" << std::endl;
    std::cout << "Interpolation factor: " << interpolation_factor << std::endl;

    TestResult result = {};
    result.input_rms = (compute_rms(left) + compute_rms(right)) / 2.0;
    result.input_peak = std::max(compute_peak(left), compute_peak(right));

    // Spectral analysis
    auto points_left = audio_transport::spectral::analysis(left, SAMPLE_RATE, WINDOW_SIZE, PADDING);
    auto points_right = audio_transport::spectral::analysis(right, SAMPLE_RATE, WINDOW_SIZE, PADDING);

    if (points_left.empty() || points_right.empty()) {
        std::cout << "  ERROR: Empty spectral analysis result" << std::endl;
        return result;
    }

    std::cout << "  Windows: " << points_left.size() << " left, " << points_right.size() << " right" << std::endl;
    std::cout << "  Bins per window: " << points_left[0].size() << std::endl;

    // Initialize phases
    std::vector<double> phases(points_left[0].size(), 0);

    // Interpolate each window
    size_t num_windows = std::min(points_left.size(), points_right.size());
    std::vector<std::vector<audio_transport::spectral::point>> points_interpolated(num_windows);

    for (size_t w = 0; w < num_windows; w++) {
        points_interpolated[w] = audio_transport::interpolate(
            points_left[w],
            points_right[w],
            phases,
            WINDOW_SIZE,
            interpolation_factor);

        // Check for NaN/Inf in output
        for (const auto& p : points_interpolated[w]) {
            if (!std::isfinite(std::abs(p.value))) {
                result.has_nan = true;
            }
            if (std::isinf(std::abs(p.value))) {
                result.has_inf = true;
            }
        }
    }

    // Synthesize output
    auto output = audio_transport::spectral::synthesis(points_interpolated, PADDING);

    result.output_rms = compute_rms(output);
    result.output_peak = compute_peak(output);
    result.energy_ratio = (result.input_rms > 0) ? result.output_rms / result.input_rms : 0;
    result.discontinuities = count_discontinuities(output, 0.1);  // threshold for clicks

    // Check output for NaN/Inf
    for (double s : output) {
        if (std::isnan(s)) result.has_nan = true;
        if (std::isinf(s)) result.has_inf = true;
    }

    std::cout << "  Input  RMS: " << result.input_rms << ", Peak: " << result.input_peak << std::endl;
    std::cout << "  Output RMS: " << result.output_rms << ", Peak: " << result.output_peak << std::endl;
    std::cout << "  Energy ratio: " << result.energy_ratio << std::endl;
    std::cout << "  Discontinuities (>0.1): " << result.discontinuities << std::endl;

    if (result.has_nan) std::cout << "  WARNING: Output contains NaN!" << std::endl;
    if (result.has_inf) std::cout << "  WARNING: Output contains Inf!" << std::endl;
    if (result.energy_ratio > 2.0) std::cout << "  WARNING: Energy blowup detected!" << std::endl;
    if (result.discontinuities > 10) std::cout << "  WARNING: Many discontinuities (clicks)!" << std::endl;

    return result;
}

int main() {
    std::cout << "Audio Transport Edge Case Tests" << std::endl;
    std::cout << "================================" << std::endl;
    std::cout << "Sample rate: " << SAMPLE_RATE << " Hz" << std::endl;
    std::cout << "Window size: " << WINDOW_SIZE << " s" << std::endl;
    std::cout << "Buffer size: " << BUFFER_SIZE << " samples" << std::endl;

    int failures = 0;

    // Test 1: Simple sine wave transport (should work fine)
    {
        auto left = generate_sine(440.0, 0.5, BUFFER_SIZE);
        auto right = generate_sine(880.0, 0.5, BUFFER_SIZE);
        auto result = run_transport_test(left, right, 0.5, "Sine 440Hz -> 880Hz");
        if (result.energy_ratio > 2.0 || result.has_nan) failures++;
    }

    // Test 2: DC offset (this is a problem case!)
    {
        auto left = generate_dc(0.5, BUFFER_SIZE);
        auto right = generate_dc(0.3, BUFFER_SIZE);
        auto result = run_transport_test(left, right, 0.5, "DC offset 0.5 -> 0.3");
        if (result.energy_ratio > 2.0 || result.has_nan) failures++;
    }

    // Test 3: Subsonic frequency (potential problem)
    {
        auto left = generate_subsonic(5.0, 0.5, BUFFER_SIZE);  // 5 Hz
        auto right = generate_subsonic(10.0, 0.5, BUFFER_SIZE); // 10 Hz
        auto result = run_transport_test(left, right, 0.5, "Subsonic 5Hz -> 10Hz");
        if (result.energy_ratio > 2.0 || result.has_nan) failures++;
    }

    // Test 4: Very low frequency
    {
        auto left = generate_sine(20.0, 0.5, BUFFER_SIZE);
        auto right = generate_sine(30.0, 0.5, BUFFER_SIZE);
        auto result = run_transport_test(left, right, 0.5, "Low freq 20Hz -> 30Hz");
        if (result.energy_ratio > 2.0 || result.has_nan) failures++;
    }

    // Test 5: Impulse (transient - clicks)
    {
        auto left = generate_impulse(BUFFER_SIZE/2, 1.0, BUFFER_SIZE);
        auto right = generate_impulse(BUFFER_SIZE/2, 1.0, BUFFER_SIZE);
        auto result = run_transport_test(left, right, 0.5, "Impulse");
        if (result.has_nan) failures++;
    }

    // Test 6: Silence to sine (edge case)
    {
        auto left = std::vector<double>(BUFFER_SIZE, 0.0);
        auto right = generate_sine(440.0, 0.5, BUFFER_SIZE);
        auto result = run_transport_test(left, right, 0.5, "Silence -> Sine 440Hz");
        if (result.has_nan || result.has_inf) failures++;
    }

    // Test 7: Sine with DC offset (common in real audio)
    {
        auto left = generate_sine(440.0, 0.5, BUFFER_SIZE);
        auto right = generate_sine(440.0, 0.5, BUFFER_SIZE);
        // Add DC offset to right
        for (auto& s : right) s += 0.1;
        auto result = run_transport_test(left, right, 0.5, "Sine 440Hz with DC offset");
        if (result.energy_ratio > 2.0 || result.has_nan) failures++;
    }

    // Test 8: Different interpolation factors
    {
        auto left = generate_sine(440.0, 0.5, BUFFER_SIZE);
        auto right = generate_sine(880.0, 0.5, BUFFER_SIZE);

        run_transport_test(left, right, 0.0, "Sine 440Hz->880Hz @ 0%");
        run_transport_test(left, right, 0.25, "Sine 440Hz->880Hz @ 25%");
        run_transport_test(left, right, 0.75, "Sine 440Hz->880Hz @ 75%");
        run_transport_test(left, right, 1.0, "Sine 440Hz->880Hz @ 100%");
    }

    // Test 9: Near-zero amplitude (numerical stability)
    {
        auto left = generate_sine(440.0, 1e-10, BUFFER_SIZE);
        auto right = generate_sine(880.0, 1e-10, BUFFER_SIZE);
        auto result = run_transport_test(left, right, 0.5, "Near-zero amplitude");
        if (result.has_nan || result.has_inf) failures++;
    }

    std::cout << "\n================================" << std::endl;
    std::cout << "Tests completed. Potential issues: " << failures << std::endl;

    return failures;
}
