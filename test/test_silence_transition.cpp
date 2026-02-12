#include <iostream>
#include <fstream>
#include <cmath>
#include <vector>

#include "audio_transport/spectral.hpp"
#include "audio_transport/audio_transport.hpp"

const double SAMPLE_RATE = 44100.0;
const double WINDOW_SIZE = 0.05;
const unsigned int PADDING = 7;

// Generate audio with fade in
std::vector<double> generate_fade_in(double freq, double max_amp, size_t samples) {
    std::vector<double> audio(samples);
    for (size_t i = 0; i < samples; i++) {
        double envelope = (double)i / samples;  // linear fade in
        audio[i] = envelope * max_amp * std::sin(2.0 * M_PI * freq * i / SAMPLE_RATE);
    }
    return audio;
}

// Analyze a single window pair in detail
void analyze_window_detail(
    const std::vector<audio_transport::spectral::point>& left,
    const std::vector<audio_transport::spectral::point>& right,
    double interpolation) {

    std::cout << "\n--- Detailed window analysis ---" << std::endl;

    // Check left spectrum
    double left_mass_sum = 0;
    size_t left_nonzero = 0;
    double left_max = 0;
    size_t left_max_bin = 0;

    for (size_t i = 0; i < left.size(); i++) {
        double mag = std::abs(left[i].value);
        left_mass_sum += mag;
        if (mag > 1e-15) left_nonzero++;
        if (mag > left_max) {
            left_max = mag;
            left_max_bin = i;
        }
    }

    std::cout << "Left spectrum: mass_sum=" << left_mass_sum
              << " nonzero_bins=" << left_nonzero
              << " max=" << left_max << " at bin " << left_max_bin << std::endl;

    // Check right spectrum
    double right_mass_sum = 0;
    size_t right_nonzero = 0;
    double right_max = 0;
    size_t right_max_bin = 0;

    for (size_t i = 0; i < right.size(); i++) {
        double mag = std::abs(right[i].value);
        right_mass_sum += mag;
        if (mag > 1e-15) right_nonzero++;
        if (mag > right_max) {
            right_max = mag;
            right_max_bin = i;
        }
    }

    std::cout << "Right spectrum: mass_sum=" << right_mass_sum
              << " nonzero_bins=" << right_nonzero
              << " max=" << right_max << " at bin " << right_max_bin << std::endl;

    // Group spectra and check masses
    auto left_masses = audio_transport::group_spectrum(left);
    auto right_masses = audio_transport::group_spectrum(right);

    std::cout << "Left groups: " << left_masses.size() << std::endl;
    for (size_t i = 0; i < std::min((size_t)5, left_masses.size()); i++) {
        std::cout << "  [" << i << "] bins " << left_masses[i].left_bin
                  << "-" << left_masses[i].right_bin
                  << " center=" << left_masses[i].center_bin
                  << " mass=" << left_masses[i].mass << std::endl;
    }

    std::cout << "Right groups: " << right_masses.size() << std::endl;
    for (size_t i = 0; i < std::min((size_t)5, right_masses.size()); i++) {
        std::cout << "  [" << i << "] bins " << right_masses[i].left_bin
                  << "-" << right_masses[i].right_bin
                  << " center=" << right_masses[i].center_bin
                  << " mass=" << right_masses[i].mass << std::endl;
    }

    // Get transport matrix
    auto T = audio_transport::transport_matrix(left_masses, right_masses);
    std::cout << "Transport matrix entries: " << T.size() << std::endl;
    for (size_t i = 0; i < std::min((size_t)10, T.size()); i++) {
        std::cout << "  T[" << i << "]: left_idx=" << std::get<0>(T[i])
                  << " right_idx=" << std::get<1>(T[i])
                  << " mass=" << std::get<2>(T[i]) << std::endl;
    }

    // Do interpolation
    std::vector<double> phases(left.size(), 0);
    auto interp = audio_transport::interpolate(left, right, phases, WINDOW_SIZE, interpolation);

    // Check output
    double out_mass_sum = 0;
    double out_max = 0;
    size_t out_max_bin = 0;
    size_t out_nonzero = 0;

    for (size_t i = 0; i < interp.size(); i++) {
        double mag = std::abs(interp[i].value);
        out_mass_sum += mag;
        if (mag > 1e-15) out_nonzero++;
        if (mag > out_max) {
            out_max = mag;
            out_max_bin = i;
        }
    }

    std::cout << "Output spectrum: mass_sum=" << out_mass_sum
              << " nonzero_bins=" << out_nonzero
              << " max=" << out_max << " at bin " << out_max_bin << std::endl;

    // Check for anomalies
    double expected_mass = (1 - interpolation) * left_mass_sum + interpolation * right_mass_sum;
    double mass_ratio = (expected_mass > 0) ? out_mass_sum / expected_mass : 0;
    std::cout << "Expected mass: " << expected_mass << " Actual: " << out_mass_sum
              << " Ratio: " << mass_ratio << std::endl;

    if (mass_ratio > 2.0 || mass_ratio < 0.5) {
        std::cout << "*** ANOMALY: Mass ratio out of range! ***" << std::endl;
    }
}

int main() {
    std::cout << "Silence Transition Analysis" << std::endl;
    std::cout << "============================" << std::endl;

    // Create silence and sine wave
    size_t num_samples = 8820;  // 200ms
    std::vector<double> silence(num_samples, 0.0);
    std::vector<double> sine(num_samples);
    for (size_t i = 0; i < num_samples; i++) {
        sine[i] = 0.5 * std::sin(2.0 * M_PI * 440.0 * i / SAMPLE_RATE);
    }

    // Analyze both
    auto silent_points = audio_transport::spectral::analysis(silence, SAMPLE_RATE, WINDOW_SIZE, PADDING);
    auto sine_points = audio_transport::spectral::analysis(sine, SAMPLE_RATE, WINDOW_SIZE, PADDING);

    std::cout << "Silent windows: " << silent_points.size() << std::endl;
    std::cout << "Sine windows: " << sine_points.size() << std::endl;

    if (silent_points.empty() || sine_points.empty()) {
        std::cout << "ERROR: No windows generated" << std::endl;
        return 1;
    }

    // Analyze first window pair in detail
    std::cout << "\n=== SILENCE -> SINE (interpolation=0.5) ===" << std::endl;
    analyze_window_detail(silent_points[0], sine_points[0], 0.5);

    std::cout << "\n=== SINE -> SILENCE (interpolation=0.5) ===" << std::endl;
    analyze_window_detail(sine_points[0], silent_points[0], 0.5);

    // Test various interpolation values
    std::cout << "\n=== Testing interpolation range ===" << std::endl;
    std::vector<double> phases(silent_points[0].size(), 0);

    for (double interp = 0.0; interp <= 1.0; interp += 0.1) {
        auto result = audio_transport::interpolate(
            silent_points[0], sine_points[0], phases, WINDOW_SIZE, interp);

        double out_sum = 0;
        for (const auto& p : result) {
            out_sum += std::abs(p.value);
        }

        std::cout << "interp=" << interp << " -> output_mass=" << out_sum << std::endl;
    }

    // Now test with near-silence (very low amplitude)
    std::cout << "\n=== NEAR-SILENCE -> SINE ===" << std::endl;
    std::vector<double> near_silence(num_samples);
    for (size_t i = 0; i < num_samples; i++) {
        near_silence[i] = 1e-8 * std::sin(2.0 * M_PI * 100.0 * i / SAMPLE_RATE);
    }
    auto near_silent_points = audio_transport::spectral::analysis(near_silence, SAMPLE_RATE, WINDOW_SIZE, PADDING);
    analyze_window_detail(near_silent_points[0], sine_points[0], 0.5);

    return 0;
}
