#include <iostream>
#include <cmath>
#include <vector>
#include <stdexcept>

#include "audio_transport/spectral.hpp"
#include "audio_transport/audio_transport.hpp"

using audio_transport::spectral::stft_params;

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
      std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #cond << std::endl; \
      failures++; \
    } \
  } while (0)

const double SAMPLE_RATE = 44100.0;

std::vector<double> generate_sines(size_t samples) {
  std::vector<double> audio(samples);
  for (size_t i = 0; i < samples; i++) {
    audio[i] = 0.4 * std::sin(2.0 * M_PI * 440.0 * i / SAMPLE_RATE)
             + 0.2 * std::sin(2.0 * M_PI * 1234.5 * i / SAMPLE_RATE);
  }
  return audio;
}

// Max error between input and reconstruction, ignoring the edges where
// fewer than a full set of windows overlap.
double interior_error(const std::vector<double> & in, const std::vector<double> & out, size_t margin) {
  double err = 0;
  size_t end = std::min(in.size(), out.size());
  for (size_t i = margin; i + margin < end; i++) {
    err = std::max(err, std::abs(in[i] - out[i]));
  }
  return err;
}

void test_legacy_params() {
  // 50ms, padding 7, overlap 1: N rounded to even, hop N/2, fft N*8
  stft_params p = stft_params::legacy(SAMPLE_RATE, 0.05, 7, 1);
  CHECK(p.window == 2206);
  CHECK(p.hop == 1103);
  CHECK(p.fft_size == 2206 * 8);

  // overlap 4: N rounded up to a multiple of 8, hop N/8
  p = stft_params::legacy(SAMPLE_RATE, 0.05, 0, 4);
  CHECK(p.window == 2208);
  CHECK(p.hop == 276);
  CHECK(p.fft_size == 2208);
}

void test_from_python_params() {
  // window 500ms, hop_div 16, fft_mult 256
  stft_params p = stft_params::from_python(SAMPLE_RATE, 500, 16, 256);
  CHECK(p.window == 22064);  // 22050 rounded up to a multiple of 16
  CHECK(p.hop == 22064 / 16);
  CHECK(p.fft_size == 32768u * 256u);

  // Python defaults: 100ms, hop_div 4, fft_mult 2
  p = stft_params::from_python(SAMPLE_RATE, 100, 4, 2);
  CHECK(p.window == 4412);
  CHECK(p.hop == 1103);
  CHECK(p.fft_size == 8192u * 2u);

  // Rounding N up past a power of two must not leave fft_size < window
  p = stft_params::from_python(4096000.0, 1, 3, 1);  // N = 4096 -> 4098
  CHECK(p.window == 4098);
  CHECK(p.fft_size >= p.window);

  bool threw = false;
  try { stft_params::from_python(SAMPLE_RATE, 100, 1, 2); } catch (const std::invalid_argument &) { threw = true; }
  CHECK(threw);
  threw = false;
  try { stft_params::from_python(SAMPLE_RATE, 100, 4, 0); } catch (const std::invalid_argument &) { threw = true; }
  CHECK(threw);
  threw = false;
  try { stft_params::from_python(SAMPLE_RATE, 0, 4, 2); } catch (const std::invalid_argument &) { threw = true; }
  CHECK(threw);
}

void test_round_trip(unsigned int hop_div, unsigned int fft_mult) {
  std::vector<double> audio = generate_sines(SAMPLE_RATE);
  stft_params p = stft_params::from_python(SAMPLE_RATE, 50, hop_div, fft_mult);
  auto points = audio_transport::spectral::analysis(audio, SAMPLE_RATE, p);
  CHECK(points.size() == (audio.size() - p.window) / p.hop + 1);
  CHECK(points[0].size() == p.fft_size / 2 + 1);
  std::vector<double> out = audio_transport::spectral::synthesis(points, p);
  CHECK(out.size() == (points.size() - 1) * p.hop + p.window);
  double err = interior_error(audio, out, p.window);
  std::cout << "  round trip hop_div=" << hop_div << " fft_mult=" << fft_mult
            << " max interior error=" << err << std::endl;
  CHECK(err < 1e-3);
}

void test_legacy_overloads_match() {
  std::vector<double> audio = generate_sines(SAMPLE_RATE / 2);
  auto a = audio_transport::spectral::analysis(audio, SAMPLE_RATE, 0.05, 7, 1);
  auto b = audio_transport::spectral::analysis(audio, SAMPLE_RATE, stft_params::legacy(SAMPLE_RATE, 0.05, 7, 1));
  CHECK(a.size() == b.size());
  bool same = a.size() == b.size();
  for (size_t w = 0; same && w < a.size(); w++)
    for (size_t i = 0; same && i < a[w].size(); i++)
      same = a[w][i].value == b[w][i].value && a[w][i].freq_reassigned == b[w][i].freq_reassigned;
  CHECK(same);

  auto x = audio_transport::spectral::synthesis(a, 7, 1);
  auto y = audio_transport::spectral::synthesis(b, stft_params::legacy(SAMPLE_RATE, 0.05, 7, 1));
  CHECK(x == y);
}

void test_short_audio_is_empty() {
  std::vector<double> audio(100, 0.1);
  stft_params p = stft_params::from_python(SAMPLE_RATE, 50, 4, 2);
  auto points = audio_transport::spectral::analysis(audio, SAMPLE_RATE, p);
  CHECK(points.empty());
  CHECK(audio_transport::spectral::synthesis(points, p).empty());
  CHECK(audio_transport::spectral::analysis(audio, SAMPLE_RATE, 0.05, 7, 1).empty());
}

void test_interpolate_hop_matches_legacy() {
  std::vector<double> left_audio = generate_sines(SAMPLE_RATE / 2);
  std::vector<double> right_audio(left_audio.size());
  for (size_t i = 0; i < right_audio.size(); i++)
    right_audio[i] = 0.3 * std::sin(2.0 * M_PI * 660.0 * i / SAMPLE_RATE);

  auto left = audio_transport::spectral::analysis(left_audio, SAMPLE_RATE, 0.05, 7, 1);
  auto right = audio_transport::spectral::analysis(right_audio, SAMPLE_RATE, 0.05, 7, 1);

  std::vector<double> phases_a(left[0].size(), 0), phases_b(left[0].size(), 0);
  bool same = true;
  for (size_t w = 0; w < left.size(); w++) {
    double k = w / (double) left.size();
    auto a = audio_transport::interpolate(left[w], right[w], phases_a, 0.05, k);
    auto b = audio_transport::interpolate_hop(left[w], right[w], phases_b, 0.025, k);
    for (size_t i = 0; same && i < a.size(); i++) same = a[i].value == b[i].value;
  }
  CHECK(same);
  CHECK(phases_a == phases_b);
}

// With dense overlap the phase must advance by the real hop, otherwise
// neighbouring windows cancel in the overlap-add and the level collapses.
void test_interpolate_hop_keeps_level(unsigned int hop_div) {
  std::vector<double> audio = generate_sines(1.5 * SAMPLE_RATE);
  stft_params p = stft_params::from_python(SAMPLE_RATE, 100, hop_div, 2);
  auto points = audio_transport::spectral::analysis(audio, SAMPLE_RATE, p);

  std::vector<double> phases(points[0].size(), 0);
  std::vector<std::vector<audio_transport::spectral::point>> interpolated(points.size());
  for (size_t w = 0; w < points.size(); w++) {
    interpolated[w] = audio_transport::interpolate_hop(points[w], points[w], phases, p.hop/SAMPLE_RATE, 0.5);
  }
  std::vector<double> out = audio_transport::spectral::synthesis(interpolated, p);

  double in_energy = 0, out_energy = 0;
  for (size_t i = p.window; i + p.window < out.size(); i++) {
    in_energy += audio[i] * audio[i];
    out_energy += out[i] * out[i];
  }
  double level = std::sqrt(out_energy/in_energy);
  std::cout << "  level hop_div=" << hop_div << " out/in=" << level << std::endl;
  CHECK(std::abs(level - 1) < 0.05);
}

// Starting from zero phases lines every partial up in the first window,
// which synthesizes an impulse (a click). Seeding the phases from the
// first spectrum must keep the first window's peak near the input's.
void test_initial_phases_avoid_click() {
  // Many partials with scattered phases: low crest factor, but a large
  // impulse if all of them are forced into phase.
  size_t n = SAMPLE_RATE / 2;
  std::vector<double> audio(n, 0);
  for (int h = 1; h <= 60; h++) {
    double phase = std::fmod(h * h * 2.39996, 2 * M_PI);
    for (size_t i = 0; i < n; i++) audio[i] += 0.01 * std::sin(2 * M_PI * 110.0 * h * i / SAMPLE_RATE + phase);
  }
  double input_peak = 0;
  for (double v : audio) input_peak = std::max(input_peak, std::abs(v));

  stft_params p = stft_params::from_python(SAMPLE_RATE, 100, 8, 4);
  auto points = audio_transport::spectral::analysis(audio, SAMPLE_RATE, p);
  double hop_seconds = p.hop/SAMPLE_RATE;

  for (int seeded = 0; seeded < 2; seeded++) {
    std::vector<double> phases = seeded ?
      audio_transport::initial_phases(points[0], hop_seconds) :
      std::vector<double>(points[0].size(), 0);
    std::vector<std::vector<audio_transport::spectral::point>> first(1);
    first[0] = audio_transport::interpolate_hop(points[0], points[0], phases, hop_seconds, 0);
    std::vector<double> out = audio_transport::spectral::synthesis(first, p);
    double peak = 0;
    for (double v : out) peak = std::max(peak, std::abs(v));
    // One window on its own is scaled down by the overlap gain N/(2 hop)
    double expected_peak = input_peak/(p.window/(2. * p.hop));
    std::cout << "  first window peak " << (seeded ? "seeded" : "zeros ") << " = " << peak
              << " (input peak / overlap gain = " << expected_peak << ")" << std::endl;
    if (seeded) {
      CHECK(peak < 1.5 * expected_peak);
    } else {
      CHECK(peak > 3 * expected_peak);  // the click this guards against
    }
  }
}

// The reusable analyzer/synthesizer must give exactly what the
// whole-signal functions give, window by window.
void test_analyzer_synthesizer_match() {
  std::vector<double> audio = generate_sines(SAMPLE_RATE / 2);
  stft_params p = stft_params::from_python(SAMPLE_RATE, 50, 8, 2);
  auto points = audio_transport::spectral::analysis(audio, SAMPLE_RATE, p);
  std::vector<double> whole = audio_transport::spectral::synthesis(points, p);

  audio_transport::spectral::analyzer analyzer(SAMPLE_RATE, p);
  audio_transport::spectral::synthesizer synthesizer(p);
  std::vector<double> out(whole.size(), 0);
  bool same = true;
  for (size_t w = 0; w < points.size(); w++) {
    double t = ((p.window - 1)/2. + w * p.hop)/SAMPLE_RATE;
    std::vector<audio_transport::spectral::point> spectrum = analyzer.analyze(audio.data() + w * p.hop, t);
    for (size_t i = 0; same && i < spectrum.size(); i++) {
      same = spectrum[i].value == points[w][i].value &&
             spectrum[i].freq_reassigned == points[w][i].freq_reassigned &&
             spectrum[i].time == points[w][i].time;
    }
    std::vector<double> frame = synthesizer.synthesize(spectrum);
    for (size_t i = 0; i < frame.size(); i++) out[w * p.hop + i] += frame[i];
  }
  CHECK(same);
  CHECK(out == whole);
}

int main() {
  std::cout << "test_stft_params" << std::endl;
  test_legacy_params();
  test_from_python_params();
  test_round_trip(2, 1);
  test_round_trip(4, 2);
  test_round_trip(3, 1);
  test_round_trip(16, 4);
  test_legacy_overloads_match();
  test_short_audio_is_empty();
  test_interpolate_hop_matches_legacy();
  test_interpolate_hop_keeps_level(2);
  test_interpolate_hop_keeps_level(4);
  test_interpolate_hop_keeps_level(16);
  test_initial_phases_avoid_click();
  test_analyzer_synthesizer_match();
  std::cout << (failures ? "FAILED: " : "OK, failures: ") << failures << std::endl;
  return failures;
}
