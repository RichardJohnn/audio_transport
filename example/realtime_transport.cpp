/**
 * Example: Real-time Audio Transport for VST3-style processing
 *
 * Demonstrates processing audio in small buffers (like a VST3 plugin)
 * with main input and sidechain morphing.
 */

#include <iostream>
#include <vector>
#include <cmath>
#include <audiorw/audiorw.hpp>

#include "audio_transport/RealtimeAudioTransport.hpp"

void create_test_tone(std::vector<float>& buffer, double frequency,
                      double sample_rate, double duration) {
    int num_samples = static_cast<int>(duration * sample_rate);
    buffer.resize(num_samples);

    for (int i = 0; i < num_samples; ++i) {
        double t = i / sample_rate;
        buffer[i] = 0.5f * std::sin(2.0 * M_PI * frequency * t);
    }
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cout << "Usage: " << argv[0] << " <main_input.wav> <sidechain_input.wav> <output.wav> [k_value]" << std::endl;
        std::cout << "\nAlternatively, use --demo to generate test tones:" << std::endl;
        std::cout << "  " << argv[0] << " --demo <output.wav> [k_value]" << std::endl;
        std::cout << "\nk_value: interpolation factor (0.0 = main, 1.0 = sidechain, default: 0.5)" << std::endl;
        return 1;
    }

    bool demo_mode = (std::string(argv[1]) == "--demo");
    float k_value = 0.5f;

    std::vector<float> main_audio;
    std::vector<float> sidechain_audio;
    double sample_rate = 44100.0;
    std::string output_file;

    if (demo_mode) {
        // Demo mode: generate test tones
        output_file = argv[2];
        if (argc >= 4) {
            k_value = std::atof(argv[3]);
        }

        std::cout << "Demo mode: Generating test tones" << std::endl;
        std::cout << "  Main: 440 Hz (A4)" << std::endl;
        std::cout << "  Sidechain: 554.37 Hz (C#5)" << std::endl;

        create_test_tone(main_audio, 440.0, sample_rate, 2.0);
        create_test_tone(sidechain_audio, 554.37, sample_rate, 2.0);

    } else {
        // Load audio files
        output_file = argv[3];
        if (argc >= 5) {
            k_value = std::atof(argv[4]);
        }

        std::cout << "Loading main input: " << argv[1] << std::endl;
        audiorw::AudioFile main_file(argv[1]);
        sample_rate = main_file.sample_rate();
        main_audio.resize(main_file.num_samples());
        main_file.read_samples(main_audio.data(), main_file.num_samples());

        std::cout << "Loading sidechain input: " << argv[2] << std::endl;
        audiorw::AudioFile sidechain_file(argv[2]);
        if (sidechain_file.sample_rate() != sample_rate) {
            std::cerr << "Warning: Sample rate mismatch!" << std::endl;
            std::cerr << "  Main: " << sample_rate << " Hz" << std::endl;
            std::cerr << "  Sidechain: " << sidechain_file.sample_rate() << " Hz" << std::endl;
        }
        sidechain_audio.resize(sidechain_file.num_samples());
        sidechain_file.read_samples(sidechain_audio.data(), sidechain_file.num_samples());

        // Ensure equal length
        size_t max_len = std::max(main_audio.size(), sidechain_audio.size());
        main_audio.resize(max_len, 0.0f);
        sidechain_audio.resize(max_len, 0.0f);
    }

    std::cout << "\nProcessing parameters:" << std::endl;
    std::cout << "  Sample rate: " << sample_rate << " Hz" << std::endl;
    std::cout << "  Duration: " << main_audio.size() / sample_rate << " seconds" << std::endl;
    std::cout << "  k value: " << k_value << " (0=main, 1=sidechain)" << std::endl;

    // Create processor with default settings (100ms window, 75% overlap)
    audio_transport::RealtimeAudioTransport processor(
        sample_rate,
        100.0,  // 100ms window
        4,      // hop divisor (4 = 75% overlap)
        2       // FFT multiplier (2x zero-padding)
    );

    // Prepare output buffer
    std::vector<float> output(main_audio.size());

    // Simulate VST3-style processing with small buffers
    const int vst_buffer_size = 512; // Typical VST buffer size
    int num_buffers = (main_audio.size() + vst_buffer_size - 1) / vst_buffer_size;

    std::cout << "\nProcessing " << num_buffers << " buffers of " << vst_buffer_size << " samples..." << std::endl;

    for (int buf = 0; buf < num_buffers; ++buf) {
        int start_sample = buf * vst_buffer_size;
        int samples_to_process = std::min(vst_buffer_size,
                                          static_cast<int>(main_audio.size()) - start_sample);

        // Process this buffer
        processor.process(
            &main_audio[start_sample],
            &sidechain_audio[start_sample],
            &output[start_sample],
            samples_to_process,
            k_value
        );

        // Progress indicator
        if (buf % 100 == 0) {
            float progress = 100.0f * buf / num_buffers;
            std::cout << "  Progress: " << progress << "%" << std::endl;
        }
    }

    std::cout << "Processing complete!" << std::endl;

    // Normalize output
    float max_val = 0.0f;
    for (float sample : output) {
        max_val = std::max(max_val, std::abs(sample));
    }
    if (max_val > 0.0f) {
        float scale = 0.95f / max_val;
        for (float& sample : output) {
            sample *= scale;
        }
    }

    // Write output
    std::cout << "\nWriting output: " << output_file << std::endl;
    audiorw::AudioFile output_audio_file(
        output_file,
        audiorw::AudioFile::WRITE,
        1,  // mono
        sample_rate
    );
    output_audio_file.write_samples(output.data(), output.size());

    std::cout << "Done! Output written to " << output_file << std::endl;
    std::cout << "\nLatency: " << processor.getLatencySamples() << " samples ("
              << (processor.getLatencySamples() / sample_rate * 1000.0) << " ms)" << std::endl;

    return 0;
}
