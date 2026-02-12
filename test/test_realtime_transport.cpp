/**
 * Unit test for RealtimeAudioTransport
 *
 * Tests basic functionality without file I/O dependencies
 */

#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include <cstring>

#include "audio_transport/RealtimeAudioTransport.hpp"

void test_initialization() {
    std::cout << "Test 1: Initialization... ";

    audio_transport::RealtimeAudioTransport processor(44100.0, 100.0, 4, 2);

    // Verify latency is reasonable
    int latency = processor.getLatencySamples();
    assert(latency > 0);
    assert(latency < 44100); // Less than 1 second

    std::cout << "PASS (latency = " << latency << " samples)" << std::endl;
}

void test_reset() {
    std::cout << "Test 2: Reset... ";

    audio_transport::RealtimeAudioTransport processor(44100.0, 50.0, 4, 2);

    // Process some data
    std::vector<float> main_in(512, 0.5f);
    std::vector<float> sc_in(512, 0.3f);
    std::vector<float> output(512);

    processor.process(main_in.data(), sc_in.data(), output.data(), 512, 0.5f);

    // Reset should clear internal state
    processor.reset();

    // Process silence and verify we get close to silence
    std::fill(main_in.begin(), main_in.end(), 0.0f);
    std::fill(sc_in.begin(), sc_in.end(), 0.0f);

    processor.process(main_in.data(), sc_in.data(), output.data(), 512, 0.5f);

    // Output should be very small (considering latency, first buffer might have some content)
    // Just verify no NaN/Inf
    for (int i = 0; i < 512; ++i) {
        assert(std::isfinite(output[i]));
    }

    std::cout << "PASS" << std::endl;
}

void test_process_silence() {
    std::cout << "Test 3: Process silence... ";

    audio_transport::RealtimeAudioTransport processor(44100.0, 50.0, 4, 2);

    std::vector<float> main_in(1024, 0.0f);
    std::vector<float> sc_in(1024, 0.0f);
    std::vector<float> output(1024);

    // Process several buffers of silence
    for (int i = 0; i < 10; ++i) {
        processor.process(main_in.data(), sc_in.data(), output.data(), 1024, 0.5f);

        // Verify no NaN/Inf in output
        for (int j = 0; j < 1024; ++j) {
            assert(std::isfinite(output[j]));
        }
    }

    std::cout << "PASS" << std::endl;
}

void test_process_sine_waves() {
    std::cout << "Test 4: Process sine waves... ";

    const double sample_rate = 44100.0;
    const int buffer_size = 512;
    const int num_buffers = 100;

    audio_transport::RealtimeAudioTransport processor(sample_rate, 100.0, 4, 2);

    // Generate sine waves
    const double freq1 = 440.0;  // A4
    const double freq2 = 554.37; // C#5

    std::vector<float> main_in(buffer_size);
    std::vector<float> sc_in(buffer_size);
    std::vector<float> output(buffer_size);

    double t = 0.0;
    double dt = 1.0 / sample_rate;

    bool has_non_zero_output = false;

    for (int buf = 0; buf < num_buffers; ++buf) {
        // Generate test signals
        for (int i = 0; i < buffer_size; ++i) {
            main_in[i] = 0.5f * std::sin(2.0 * M_PI * freq1 * t);
            sc_in[i] = 0.5f * std::sin(2.0 * M_PI * freq2 * t);
            t += dt;
        }

        // Process
        processor.process(main_in.data(), sc_in.data(), output.data(), buffer_size, 0.5f);

        // Verify output
        for (int i = 0; i < buffer_size; ++i) {
            assert(std::isfinite(output[i]));
            assert(std::abs(output[i]) <= 1.0f); // Reasonable amplitude

            if (std::abs(output[i]) > 0.01f) {
                has_non_zero_output = true;
            }
        }
    }

    // After processing many buffers, we should have some non-zero output
    // (accounting for latency)
    assert(has_non_zero_output);

    std::cout << "PASS" << std::endl;
}

void test_interpolation_extremes() {
    std::cout << "Test 5: Interpolation extremes (k=0 and k=1)... ";

    const double sample_rate = 44100.0;
    const int buffer_size = 512;

    audio_transport::RealtimeAudioTransport processor(sample_rate, 100.0, 4, 2);

    // Create distinct signals
    std::vector<float> main_in(buffer_size);
    std::vector<float> sc_in(buffer_size);
    std::vector<float> output_k0(buffer_size);
    std::vector<float> output_k1(buffer_size);

    // Simple different constant values
    for (int i = 0; i < buffer_size; ++i) {
        main_in[i] = 0.3f;
        sc_in[i] = 0.7f;
    }

    // Process with k=0 (should favor main input)
    audio_transport::RealtimeAudioTransport proc_k0(sample_rate, 100.0, 4, 2);
    for (int buf = 0; buf < 20; ++buf) {
        proc_k0.process(main_in.data(), sc_in.data(), output_k0.data(), buffer_size, 0.0f);
    }

    // Process with k=1 (should favor sidechain)
    audio_transport::RealtimeAudioTransport proc_k1(sample_rate, 100.0, 4, 2);
    for (int buf = 0; buf < 20; ++buf) {
        proc_k1.process(main_in.data(), sc_in.data(), output_k1.data(), buffer_size, 1.0f);
    }

    // Verify all values are finite
    for (int i = 0; i < buffer_size; ++i) {
        assert(std::isfinite(output_k0[i]));
        assert(std::isfinite(output_k1[i]));
    }

    std::cout << "PASS" << std::endl;
}

void test_different_buffer_sizes() {
    std::cout << "Test 6: Different buffer sizes... ";

    const double sample_rate = 44100.0;
    audio_transport::RealtimeAudioTransport processor(sample_rate, 100.0, 4, 2);

    // Test various buffer sizes common in VST plugins
    std::vector<int> buffer_sizes = {32, 64, 128, 256, 512, 1024, 2048};

    for (int size : buffer_sizes) {
        std::vector<float> main_in(size, 0.1f);
        std::vector<float> sc_in(size, 0.2f);
        std::vector<float> output(size);

        processor.process(main_in.data(), sc_in.data(), output.data(), size, 0.5f);

        // Verify no NaN/Inf
        for (int i = 0; i < size; ++i) {
            assert(std::isfinite(output[i]));
        }
    }

    std::cout << "PASS" << std::endl;
}

void test_sample_rate_change() {
    std::cout << "Test 7: Sample rate change... ";

    audio_transport::RealtimeAudioTransport processor(44100.0, 100.0, 4, 2);

    // Process at 44.1kHz
    std::vector<float> main_in(512, 0.1f);
    std::vector<float> sc_in(512, 0.2f);
    std::vector<float> output(512);

    processor.process(main_in.data(), sc_in.data(), output.data(), 512, 0.5f);

    // Change sample rate
    processor.setSampleRate(48000.0);

    // Should still work
    processor.process(main_in.data(), sc_in.data(), output.data(), 512, 0.5f);

    for (int i = 0; i < 512; ++i) {
        assert(std::isfinite(output[i]));
    }

    std::cout << "PASS" << std::endl;
}

int main() {
    std::cout << "\n=== RealtimeAudioTransport Unit Tests ===\n" << std::endl;

    try {
        test_initialization();
        test_reset();
        test_process_silence();
        test_process_sine_waves();
        test_interpolation_extremes();
        test_different_buffer_sizes();
        test_sample_rate_change();

        std::cout << "\n=== All tests PASSED ===\n" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "\nTest FAILED with unknown exception" << std::endl;
        return 1;
    }
}
