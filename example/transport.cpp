#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <audiorw.hpp>

#include "audio_transport/spectral.hpp"
#include "audio_transport/audio_transport.hpp"
#include "audio_transport/equal_loudness.hpp"

#include "transport_params.hpp"

double window_size = 0.05; // seconds
unsigned int padding = 7; // multiplies window size

int run_legacy(char ** argv) {

  // Get the percents
  double start_fraction = std::atof(argv[3])/100.;
  double end_fraction = std::atof(argv[4])/100.;

  // Open the audio files
  double sample_rate_left;
  std::vector<std::vector<double>> audio_left =
    audiorw::read(argv[1], sample_rate_left);
  double sample_rate_right;
  std::vector<std::vector<double>> audio_right =
    audiorw::read(argv[2], sample_rate_right);

  if (sample_rate_left != sample_rate_right) {
    std::cout << "Sample rates are different! " << sample_rate_left << " != " << sample_rate_right << std::endl;
    std::cout << "Using correct sample rate for each file's spectral analysis." << std::endl;
  }
  // Use left sample rate for output
  double sample_rate_output = sample_rate_left;

  // Initialize the output audio
  size_t num_channels = std::min(audio_left.size(), audio_right.size());
  std::vector<std::vector<double>> audio_interpolated(num_channels);

  // Iterate over the channels
  for (size_t c = 0; c < num_channels; c++) {

    std::cout << "Processing channel " << c << std::endl;

    std::cout << "Converting left input to the spectral domain" << std::endl;
    std::vector<std::vector<audio_transport::spectral::point>> points_left =
      audio_transport::spectral::analysis(audio_left[c], sample_rate_left, window_size, padding);
    std::cout << "Converting right input to the spectral domain" << std::endl;
    std::vector<std::vector<audio_transport::spectral::point>> points_right =
      audio_transport::spectral::analysis(audio_right[c], sample_rate_right, window_size, padding);

    std::cout << "Applying equal loudness filters" << std::endl;
    audio_transport::equal_loudness::apply(points_left);
    audio_transport::equal_loudness::apply(points_right);

    // Initialize phases
    std::vector<double> phases(points_left[0].size(), 0);

    std::cout << "Performing optimal transport based interpolation" << std::endl;
    size_t num_windows = std::min(points_left.size(), points_right.size());
    std::vector<std::vector<audio_transport::spectral::point>> points_interpolated(num_windows);
    for (size_t w = 0; w < num_windows; w++) {
      double interpolation_factor = w/(double) num_windows;
      interpolation_factor = (interpolation_factor - start_fraction)/(end_fraction - start_fraction);
      interpolation_factor = std::min(1.,std::max(0.,interpolation_factor));

      // Compute input spectral energy for this window
      double left_energy = 0, right_energy = 0;
      for (size_t i = 0; i < points_left[w].size(); i++) {
        left_energy += std::abs(points_left[w][i].value);
      }
      for (size_t i = 0; i < points_right[w].size(); i++) {
        right_energy += std::abs(points_right[w][i].value);
      }
      double max_input_energy = std::max(left_energy, right_energy);

      points_interpolated[w] =
        audio_transport::interpolate(
          points_left[w],
          points_right[w],
          phases,
          window_size,
          interpolation_factor);

      // Compute output spectral energy
      double output_energy = 0;
      for (size_t i = 0; i < points_interpolated[w].size(); i++) {
        output_energy += std::abs(points_interpolated[w][i].value);
      }

      // Log if output energy significantly exceeds input
      double ratio = (max_input_energy > 0) ? output_energy / max_input_energy : 0;
      if (ratio > 2.0) {
        double time_sec = w * window_size / 2.0;  // approximate time
        std::cout << "BLOWUP: window " << w << " (t=" << time_sec << "s) "
                  << "ratio=" << ratio << " "
                  << "left_e=" << left_energy << " right_e=" << right_energy
                  << " out_e=" << output_energy
                  << " interp=" << interpolation_factor << std::endl;
      }
    }

    std::cout << "Removing equal loudness filters" << std::endl;
    audio_transport::equal_loudness::remove(points_interpolated);

    std::cout << "Converting the interpolation to the time domain" << std::endl;
    audio_interpolated[c] = 
      audio_transport::spectral::synthesis(points_interpolated, padding);
  }

  // Write the file
  std::cout << "Writing to file " << argv[5] << std::endl;
  audiorw::write(audio_interpolated, argv[5], sample_rate_output);
  return 0;
}

// `transport -f params.json`: the options of audio_transport.py
// applied to the reassignment-based transport.
int run_params(const char * path) {
  std::ifstream file(path);
  if (!file) {
    std::cerr << "Error: cannot open parameter file " << path << std::endl;
    return 1;
  }

  transport_params::params p;
  try {
    p = transport_params::parse_params(nlohmann::json::parse(file), std::cerr);
  } catch (const std::exception & e) {
    std::cerr << "Error in " << path << ": " << e.what() << std::endl;
    return 1;
  }
  std::cout << "Loaded parameters from " << path << std::endl;

  std::vector<transport_params::keyframe> envelope;
  if (p.has_envelope) {
    envelope = transport_params::normalize_envelope(p.k_envelope);
    std::cout << "Keyframes:";
    for (const transport_params::keyframe & f : envelope) {
      std::cout << " {" << f.percent << "%, k=" << f.k << "}";
    }
    std::cout << std::endl;
  } else {
    std::cout << "k = " << p.k << std::endl;
  }

  // Open the audio files
  double sample_rate, sample_rate_right;
  std::vector<std::vector<double>> audio_left, audio_right;
  audio_transport::spectral::stft_params stft;
  try {
    audio_left = audiorw::read(p.source, sample_rate);
    audio_right = audiorw::read(p.target, sample_rate_right);
    stft = audio_transport::spectral::stft_params::from_python(sample_rate, p.window, p.hop_div, p.fft_mult);
  } catch (const std::exception & e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  if (sample_rate != sample_rate_right) {
    std::cout << "Warning: Sample rates differ (" << sample_rate << " vs " << sample_rate_right
              << "), using source rate" << std::endl;
  }

  double hop_seconds = stft.hop/sample_rate;
  std::cout << "Window " << stft.window << " samples, hop " << stft.hop
            << ", FFT " << stft.fft_size << " ("
            << sample_rate/stft.fft_size << " Hz per bin)" << std::endl;

  size_t num_channels = std::min(audio_left.size(), audio_right.size());
  std::vector<std::vector<double>> audio_interpolated(num_channels);

  // Pad the shorter input with silence
  size_t length = 0;
  for (size_t c = 0; c < num_channels; c++) {
    length = std::max(length, std::max(audio_left[c].size(), audio_right[c].size()));
  }
  for (size_t c = 0; c < num_channels; c++) {
    audio_left[c].resize(length, 0);
    audio_right[c].resize(length, 0);
  }

  if (length < stft.window) {
    std::cerr << "Error: audio is shorter than one " << p.window << " ms window" << std::endl;
    return 1;
  }
  size_t num_windows = (length - stft.window)/stft.hop + 1;
  std::cout << "Interpolating " << num_windows << " windows in "
            << num_channels << " channel(s) in parallel" << std::endl;

  // FFTW planning isn't thread-safe: create every channel's plans here,
  // then run the channels concurrently
  std::vector<std::unique_ptr<audio_transport::spectral::analyzer>> analyzers;
  std::vector<std::unique_ptr<audio_transport::spectral::synthesizer>> synthesizers;
  for (size_t c = 0; c < num_channels; c++) {
    analyzers.emplace_back(new audio_transport::spectral::analyzer(sample_rate, stft));
    synthesizers.emplace_back(new audio_transport::spectral::synthesizer(stft));
  }

  std::mutex log_mutex;

  // Process one window at a time: with large windows and fft_mult a
  // single spectrum can be hundreds of MB, too many to hold at once.
  // Overlap-adding each window's synthesis gives the same result as
  // whole-file analysis and synthesis.
  auto process_channel = [&](size_t c) {
    audio_transport::spectral::analyzer & analyzer = *analyzers[c];
    audio_transport::spectral::synthesizer & synthesizer = *synthesizers[c];
    std::vector<double> phases;
    std::vector<double> & out = audio_interpolated[c];
    out.assign((num_windows - 1) * stft.hop + stft.window, 0);

    for (size_t w = 0; w < num_windows; w++) {
      double k = p.k;
      if (p.has_envelope) {
        double percent = num_windows > 1 ? w/(double) (num_windows - 1) * 100 : 0;
        k = transport_params::interpolate_k(percent, envelope);
      }
      if (w % 10 == 0) {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::cout << "  Channel " << c << ": window " << w << "/" << num_windows
                  << " (k=" << k << ")" << std::endl;
      }

      size_t offset = w * stft.hop;
      double time = ((stft.window - 1)/2. + offset)/sample_rate;
      std::vector<std::vector<audio_transport::spectral::point>> points_left(1), points_right(1);
      points_left[0] = analyzer.analyze(audio_left[c].data() + offset, time);
      points_right[0] = analyzer.analyze(audio_right[c].data() + offset, time);

      audio_transport::equal_loudness::apply(points_left);
      audio_transport::equal_loudness::apply(points_right);

      // Start from the phases of whichever input dominates the first window
      if (w == 0) {
        phases = audio_transport::initial_phases(k <= 0.5 ? points_left[0] : points_right[0], hop_seconds);
      }

      std::vector<std::vector<audio_transport::spectral::point>> points_interpolated(1);
      points_interpolated[0] =
        audio_transport::interpolate_hop(
          points_left[0],
          points_right[0],
          phases,
          hop_seconds,
          k);

      audio_transport::equal_loudness::remove(points_interpolated);

      std::vector<double> frame = synthesizer.synthesize(points_interpolated[0]);
      for (size_t i = 0; i < frame.size(); i++) {
        out[offset + i] += frame[i];
      }
    }
  };

  std::vector<std::thread> threads;
  for (size_t c = 0; c < num_channels; c++) {
    threads.emplace_back(process_channel, c);
  }
  for (std::thread & t : threads) {
    t.join();
  }

  // Normalize, leaving some headroom
  double peak = 0;
  for (const std::vector<double> & channel : audio_interpolated) {
    for (double v : channel) peak = std::max(peak, std::abs(v));
  }
  if (peak > 0) {
    for (std::vector<double> & channel : audio_interpolated) {
      for (double & v : channel) v *= 0.95/peak;
    }
  }

  std::string output = transport_params::output_filename(p);
  std::cout << "Writing to file " << output << std::endl;
  audiorw::write(audio_interpolated, output, sample_rate);
  return 0;
}

void usage(const char * program) {
  std::cout <<
    "Usage: " << program << " left_file right_file start_percent end_percent output_file\n"
    "       " << program << " -f params.json\n"
    "\n"
    "params.json:\n" << transport_params::usage_json();
}

int main(int argc, char ** argv) {

  std::cout << "transport version: RichardJohnn-fork-v1.6-debug" << std::endl;

  if (argc == 3 && (std::strcmp(argv[1], "-f") == 0 || std::strcmp(argv[1], "--file") == 0)) {
    return run_params(argv[2]);
  }
  if (argc == 6) {
    return run_legacy(argv);
  }
  usage(argv[0]);
  return 1;
}
