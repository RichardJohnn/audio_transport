#include <iostream>
#include <cmath>
#include <complex>
#include <vector>

#include "audio_transport/spectral.hpp"
#include "audio_transport/equal_loudness.hpp"

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
      std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #cond << std::endl; \
      failures++; \
    } \
  } while (0)

typedef std::vector<std::vector<audio_transport::spectral::point>> frames;

static frames one_point(double freq_hz, std::complex<double> value) {
  frames f(1, std::vector<audio_transport::spectral::point>(1));
  f[0][0].freq = 2 * M_PI * freq_hz;
  f[0][0].value = value;
  return f;
}

// Interpolation can leave a little energy in near-DC bins that was never
// attenuated by apply(). remove() must not amplify it more than it would
// at 20 Hz, or fine FFT resolution turns it into overwhelming rumble.
void test_remove_gain_is_bounded_below_20hz() {
  frames at_20 = one_point(20, 1);
  audio_transport::equal_loudness::remove(at_20);
  double gain_20 = std::abs(at_20[0][0].value);

  for (double hz : {0.0114, 0.5, 2.7, 10.0, 19.9}) {
    frames f = one_point(hz, 1);
    audio_transport::equal_loudness::remove(f);
    double gain = std::abs(f[0][0].value);
    std::cout << "  remove gain at " << hz << " Hz = " << gain << " (20 Hz: " << gain_20 << ")" << std::endl;
    CHECK(std::isfinite(gain));
    CHECK(gain <= gain_20 * (1 + 1e-12));
  }
}

// apply() and remove() must stay exact inverses at every frequency.
void test_apply_remove_round_trip() {
  for (double hz : {0.0, 0.0114, 5.0, 19.9, 20.0, 100.0, 1000.0, 15000.0}) {
    std::complex<double> v(0.3, -0.7);
    frames f = one_point(hz, v);
    audio_transport::equal_loudness::apply(f);
    audio_transport::equal_loudness::remove(f);
    if (hz > 0) {
      CHECK(std::abs(f[0][0].value - v) < 1e-12);
    }
  }
}

// Above 20 Hz the weighting is the plain A-weighting curve.
void test_unchanged_above_20hz() {
  for (double hz : {20.0, 100.0, 1000.0, 15000.0}) {
    frames f = one_point(hz, 1);
    audio_transport::equal_loudness::apply(f);
    CHECK(std::abs(f[0][0].value) == audio_transport::equal_loudness::a_weighting_amp(2 * M_PI * hz));
  }
}

int main() {
  std::cout << "test_equal_loudness" << std::endl;
  test_remove_gain_is_bounded_below_20hz();
  test_apply_remove_round_trip();
  test_unchanged_above_20hz();
  std::cout << (failures ? "FAILED: " : "OK, failures: ") << failures << std::endl;
  return failures;
}
