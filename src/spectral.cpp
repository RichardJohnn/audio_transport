#include <vector>
#include <cmath>
#include <complex>
#include <ciso646>
#include <cassert>
#include <stdexcept>
#include <algorithm>

#include <fftw3.h>

#include "audio_transport/spectral.hpp"

using namespace audio_transport;

// FFTW planner flags. FFTW_ESTIMATE plans instantly and is reproducible.
// FFTW_MEASURE times candidate plans on this machine and keeps the
// fastest, which only pays off when plans are reused for many more
// transforms than one offline render does: planning a 1M-point FFT takes
// ~15 s, longer than the whole render. MEASURE output can also differ in
// the last bits between runs. Build with -D FFTW_MEASURE=ON to use it.
#ifndef AUDIO_TRANSPORT_FFTW_FLAGS
#define AUDIO_TRANSPORT_FFTW_FLAGS FFTW_ESTIMATE
#endif

spectral::stft_params audio_transport::spectral::stft_params::legacy(
    double sample_rate,
    double window_size,
    unsigned int padding,
    unsigned int overlap) {

  // Convert the window size to samples
  size_t N = std::round(window_size * sample_rate);
  // Make sure it is even for symmetry
  // Accounting for an overlap factor of 2 * overlap
  while (N % (2 * overlap) != 0) N += 1;

  stft_params params;
  params.window = N;
  params.hop = N/(2 * overlap);
  params.fft_size = N * (1 + padding);
  return params;
}

spectral::stft_params audio_transport::spectral::stft_params::from_python(
    double sample_rate,
    double window_ms,
    unsigned int hop_div,
    unsigned int fft_mult) {

  if (!(window_ms > 0)) throw std::invalid_argument("window must be > 0 ms");
  if (hop_div < 2) throw std::invalid_argument("hop_div must be >= 2");
  if (fft_mult < 1) throw std::invalid_argument("fft_mult must be >= 1");

  size_t N = std::floor(window_ms * sample_rate/1000.);
  if (N == 0) throw std::invalid_argument("window is shorter than one sample");
  // Make the hop exact so the Hann windows overlap-add evenly
  while (N % hop_div != 0) N += 1;

  size_t pow2 = 1;
  while (pow2 < N) pow2 *= 2;

  stft_params params;
  params.window = N;
  params.hop = N/hop_div;
  params.fft_size = pow2 * fft_mult;
  return params;
}

std::vector<double> audio_transport::spectral::synthesis(
    const std::vector<std::vector<spectral::point>> & points,
    unsigned int padding,
    unsigned int overlap) {

  if (points.empty()) return std::vector<double>();

  // Recover the window size from the FFT size
  size_t fft_size = 2 * (points[0].size() - 1);
  stft_params params;
  params.window = fft_size/(1 + padding);
  params.hop = params.window/(2 * overlap);
  params.fft_size = fft_size;
  return synthesis(points, params);
}

struct audio_transport::spectral::synthesizer::impl {
  stft_params params;
  size_t padding_samples;
  // Hann windows at this hop sum to window/(2 * hop)
  double overlap_gain;
  std::vector<double> window_padded;
  fftw_complex * fft;
  fftw_plan fft_plan;
};

audio_transport::spectral::synthesizer::synthesizer(const stft_params & params)
  : impl_(new impl) {

  impl_->params = params;
  impl_->window_padded.assign(params.fft_size, 0);
  impl_->padding_samples = (params.fft_size - params.window)/2;
  impl_->overlap_gain = params.window/(2. * params.hop);

  // Initialize FFT
  impl_->fft = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * (params.fft_size/2 + 1));
  impl_->fft_plan = fftw_plan_dft_c2r_1d(
      impl_->window_padded.size(),
      impl_->fft,
      impl_->window_padded.data(),
      AUDIO_TRANSPORT_FFTW_FLAGS);
}

audio_transport::spectral::synthesizer::~synthesizer() {
  fftw_destroy_plan(impl_->fft_plan);
  fftw_free(impl_->fft);
}

std::vector<double> audio_transport::spectral::synthesizer::synthesize(
    const std::vector<spectral::point> & spectrum) {

  assert(spectrum.size() == impl_->params.fft_size/2 + 1);
  fftw_complex * fft = impl_->fft;
  const std::vector<double> & window_padded = impl_->window_padded;

  // Fill the FFT
  for (size_t i = 0; i < spectrum.size(); i++) {
    fft[i][0] = std::real(spectrum[i].value);
    fft[i][1] = std::imag(spectrum[i].value);
  }

  // Execute the plans
  fftw_execute(impl_->fft_plan);

  std::vector<double> frame(impl_->params.window);
  for (size_t i = 0; i < frame.size(); i++) {
    // Scale down to correct for FFT and overlap sizes
    double value = window_padded[i + impl_->padding_samples]/(impl_->overlap_gain * window_padded.size());

    // Safety check: clamp NaN/Inf values to prevent audio corruption
    if (!std::isfinite(value)) {
      value = 0;
    }
    frame[i] = value;
  }
  return frame;
}

std::vector<double> audio_transport::spectral::synthesis(
    const std::vector<std::vector<spectral::point>> & points,
    const stft_params & params) {

  if (points.empty()) return std::vector<double>();

  synthesizer synth(params);

  // Initialize the audio
  std::vector<double> audio((points.size() - 1) * params.hop + params.window, 0);

  // Iterate over the windows and apply the weighted overlap add
  for (size_t w = 0; w < points.size(); w++) {
    std::vector<double> frame = synth.synthesize(points[w]);
    for (size_t i = 0; i < frame.size(); i++) {
      audio[i + w * params.hop] += frame[i];
    }
  }

  return audio;
}

std::vector<std::vector<audio_transport::spectral::point>> audio_transport::spectral::analysis(
    const std::vector<double> & audio,
    double sample_rate,
    double window_size,
    unsigned int padding,
    unsigned int overlap) {

  // Make sure inputs are positive
  assert(sample_rate > 0);
  assert(window_size > 0);

  return analysis(audio, sample_rate, stft_params::legacy(sample_rate, window_size, padding, overlap));
}

struct audio_transport::spectral::analyzer::impl {
  double sample_rate;
  stft_params params;
  size_t padding_samples;
  std::vector<double> window, window_t, window_d;
  fftw_complex * fft, * fft_t, * fft_d;
  fftw_plan fft_plan, fft_plan_t, fft_plan_d;
};

audio_transport::spectral::analyzer::analyzer(double sample_rate, const stft_params & params)
  : impl_(new impl) {

  // Make sure inputs are positive
  assert(sample_rate > 0);
  assert(params.hop > 0);
  assert(params.window % params.hop == 0);
  assert(params.fft_size >= params.window);

  impl_->sample_rate = sample_rate;
  impl_->params = params;

  // Determine samples used for padding
  impl_->padding_samples = (params.fft_size - params.window)/2;

  // Initialize the windows
  impl_->window.assign(params.fft_size, 0);
  impl_->window_t.assign(params.fft_size, 0);
  impl_->window_d.assign(params.fft_size, 0);

  // Initialize FFT
  size_t fft_size = params.fft_size/2 + 1;
  impl_->fft   = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * fft_size);
  impl_->fft_t = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * fft_size);
  impl_->fft_d = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * fft_size);
  impl_->fft_plan   = fftw_plan_dft_r2c_1d(
      impl_->window.size(),
      impl_->window.data(),
      impl_->fft,
      AUDIO_TRANSPORT_FFTW_FLAGS);
  impl_->fft_plan_t = fftw_plan_dft_r2c_1d(
      impl_->window_t.size(),
      impl_->window_t.data(),
      impl_->fft_t,
      AUDIO_TRANSPORT_FFTW_FLAGS);
  impl_->fft_plan_d = fftw_plan_dft_r2c_1d(
      impl_->window_d.size(),
      impl_->window_d.data(),
      impl_->fft_d,
      AUDIO_TRANSPORT_FFTW_FLAGS);

  // FFTW_MEASURE may scribble on the arrays while planning; only the
  // middle is rewritten per window, so the padding must be zeroed again
  std::fill(impl_->window.begin(), impl_->window.end(), 0);
  std::fill(impl_->window_t.begin(), impl_->window_t.end(), 0);
  std::fill(impl_->window_d.begin(), impl_->window_d.end(), 0);
}

audio_transport::spectral::analyzer::~analyzer() {
  fftw_destroy_plan(impl_->fft_plan);
  fftw_destroy_plan(impl_->fft_plan_t);
  fftw_destroy_plan(impl_->fft_plan_d);
  fftw_free(impl_->fft);
  fftw_free(impl_->fft_t);
  fftw_free(impl_->fft_d);
}

std::vector<audio_transport::spectral::point> audio_transport::spectral::analyzer::analyze(
    const double * audio,
    double time) {

  size_t N = impl_->params.window;
  size_t N_padded = impl_->params.fft_size;
  size_t fft_size = N_padded/2 + 1;
  double sample_rate = impl_->sample_rate;
  std::vector<double> & window = impl_->window;
  std::vector<double> & window_t = impl_->window_t;
  std::vector<double> & window_d = impl_->window_d;

  // Apply the various windows
  for (size_t i = 0; i < N; i++) {
    // The sample index of with window
    // if the center of the window has n = 0
    double n = i - (N - 1)/2.;

    double a = audio[i];

    // Apply the various windows
    window  [i + impl_->padding_samples] = a * hann  (n, N);
    window_t[i + impl_->padding_samples] = a * hann_t(n, N, sample_rate);
    window_d[i + impl_->padding_samples] = a * hann_d(n, N, sample_rate);
  }

  // Execute the plans
  fftw_execute(impl_->fft_plan);
  fftw_execute(impl_->fft_plan_t);
  fftw_execute(impl_->fft_plan_d);

  // Reserve space for each spectral point
  std::vector<spectral::point> points;
  points.reserve(fft_size);

  for (size_t i = 0; i < fft_size; i++) {
    // Convert to C++ complex
    std::complex<double> X   (impl_->fft   [i][0], impl_->fft   [i][1]);
    std::complex<double> X_t (impl_->fft_t [i][0], impl_->fft_t [i][1]);
    std::complex<double> X_d (impl_->fft_d [i][0], impl_->fft_d [i][1]);

    // Begin to construct a spectral point
    spectral::point p;
    p.value = X;
    p.time = time;
    p.freq = (2 * M_PI * i * sample_rate)/(double) N_padded;

    // Compute how the frequency and time changed
    // Guard against division by zero when X is very small (silent bins)
    double norm_X = std::norm(X);
    if (norm_X > 1e-20) {
      std::complex<double> conj_over_norm = std::conj(X)/norm_X;
      double dphase_domega =  std::real(X_t * conj_over_norm);
      double dphase_dt     = -std::imag(X_d * conj_over_norm);

      // Compute the reassigned time and frequency
      p.time_reassigned = p.time + dphase_domega;
      p.freq_reassigned = p.freq + dphase_dt;
    } else {
      // For silent/near-silent bins, don't reassign
      p.time_reassigned = p.time;
      p.freq_reassigned = p.freq;
    }

    // Add the point
    points.push_back(p);
  }

  return points;
}

std::vector<std::vector<audio_transport::spectral::point>> audio_transport::spectral::analysis(
    const std::vector<double> & audio,
    double sample_rate,
    const stft_params & params) {

  size_t N = params.window;
  size_t hop = params.hop;

  // Compute the number of windows
  if (audio.size() < N) return std::vector<std::vector<spectral::point>>();
  size_t num_windows = (audio.size() - N)/hop + 1;

  analyzer analyze(sample_rate, params);

  // Initialize the spectral points
  std::vector<std::vector<spectral::point>> points(num_windows);

  // Iterate over the windows
  for (size_t w = 0; w < num_windows; w++) {
    // Compute the center time
    double t = ((N - 1)/2. + w * hop)/sample_rate;
    points[w] = analyze.analyze(audio.data() + w * hop, t);
  }

  return points;
}

double audio_transport::spectral::hann(
    double n,
    double N) {
  return 0.5 + 0.5 * std::cos(2 * M_PI * n/(N - 1));
}

double audio_transport::spectral::hann_t(
    double n,
    double N,
    double sample_rate) {
  return (n/sample_rate) * hann(n, N);
}

double audio_transport::spectral::hann_d(
    double n,
    double N,
    double sample_rate) {
  return - (M_PI * sample_rate)/(N - 1) * std::sin(2 * M_PI * n/(N - 1));
}
