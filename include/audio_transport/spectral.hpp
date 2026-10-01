#pragma once

#include <vector>
#include <complex>
#include <memory>

namespace audio_transport {
namespace spectral {

struct point {
  std::complex<double> value;

  double time;
  double freq;
  
  double time_reassigned;
  double freq_reassigned;
};

/**
 * STFT geometry, all in samples.
 */
struct stft_params {
  size_t window;   // Hann window length N
  size_t hop;      // hop size, N must be a multiple of it
  size_t fft_size; // zero-padded FFT length, >= window

  /**
   * The original geometry: N rounded up to a multiple of 2 * overlap,
   * hop = N/(2 * overlap), fft_size = N * (1 + padding).
   */
  static stft_params legacy(
      double sample_rate,
      double window_size, // seconds
      unsigned int padding = 0,
      unsigned int overlap = 1);

  /**
   * The geometry of audio_transport.py: N = window_ms of samples rounded
   * up to a multiple of hop_div, hop = N/hop_div,
   * fft_size = nextpow2(N) * fft_mult.
   * Throws std::invalid_argument on window_ms <= 0, hop_div < 2 or fft_mult < 1.
   */
  static stft_params from_python(
      double sample_rate,
      double window_ms,
      unsigned int hop_div,
      unsigned int fft_mult);
};

/**
 * Window-at-a-time analysis with FFTW plans and buffers created once.
 * FFTW planning is not thread-safe, so construct analyzers and
 * synthesizers on one thread; separate instances can then be used
 * concurrently.
 */
class analyzer {
 public:
  analyzer(double sample_rate, const stft_params & params);
  ~analyzer();

  /**
   * Analyze params.window samples starting at `audio`.
   * `time` is the window's center in seconds.
   */
  std::vector<point> analyze(const double * audio, double time);

 private:
  struct impl;
  std::unique_ptr<impl> impl_;
  analyzer(const analyzer &);
  analyzer & operator=(const analyzer &);
};

/**
 * Window-at-a-time synthesis, see analyzer.
 */
class synthesizer {
 public:
  explicit synthesizer(const stft_params & params);
  ~synthesizer();

  /**
   * Synthesize one window: params.window samples, scaled so that
   * overlap-adding consecutive windows params.hop apart reconstructs
   * the signal.
   */
  std::vector<double> synthesize(const std::vector<point> & spectrum);

 private:
  struct impl;
  std::unique_ptr<impl> impl_;
  synthesizer(const synthesizer &);
  synthesizer & operator=(const synthesizer &);
};

/**
 * Analyze an audio signal to produce an array of spectral points.
 * Points are reduced to mono.
 * Returns no windows if the audio is shorter than one window.
 */
std::vector<std::vector<point>> analysis(
    const std::vector<double> & audio,
    double sample_rate,
    const stft_params & params
    );

std::vector<std::vector<point>> analysis(
    const std::vector<double> & audio,
    double sample_rate,
    double window_size = 0.05, // seconds
    unsigned int padding = 0,
    unsigned int overlap = 1
    );

/**
 * Synthesize an audio signal from an array of spectral points.
 */
std::vector<double> synthesis(
    const std::vector<std::vector<point>> & points,
    const stft_params & params
    );

std::vector<double> synthesis(
    const std::vector<std::vector<point>> & points,
    unsigned int padding = 0,
    unsigned int overlap = 1
    );

/**
 * A Hamming window, chosen because it is COLA
 * and easy to compute
 *
 * -(N + 1)/2 < n < (N + 1)/2
 */
double hann(double n, double N);

// Derivatives and time-weightings of the hamming window
double hann_t (double n, double N, double sample_rate);
double hann_d (double n, double N, double sample_rate);

}}
