#include <vector>
#include <cmath>
#include <algorithm>
#include <ciso646>
#include <iostream>
#include <fftw3.h>

#include "audio_transport/equal_loudness.hpp"

using namespace audio_transport;

double audio_transport::equal_loudness::a_weighting_amp(double freq) {
  // Convert to hertz
  freq /= 2 * M_PI;
  double freq_squared = freq * freq;
  double top  = 12194 * 12194 * freq_squared * freq_squared;
  double bot1 =  20.6 *  20.6 + freq_squared;
  double bot2 = 107.7 * 107.7 + freq_squared;
  double bot3 = 737.9 * 737.9 + freq_squared;
  double bot4 = 12194 * 12194 + freq_squared;
  return top/(bot1 * std::sqrt(bot2 * bot3) * bot4);
}

// The weighting applied to the spectrum: A-weighting, held flat below
// 20 Hz. A-weighting falls off as f^4 toward DC, so removing it would
// multiply near-DC bins by up to ~1e15. Interpolation leaves a trace of
// energy in those bins that apply() never attenuated, and with fine FFT
// resolution that trace becomes overwhelming rumble.
static double weighting(double freq) {
  const double min_freq = 2 * M_PI * 20; // rad/s
  return equal_loudness::a_weighting_amp(std::max(std::abs(freq), min_freq));
}

void audio_transport::equal_loudness::apply(
    std::vector<std::vector<spectral::point>> & points) {
  for (size_t w = 0; w < points.size(); w++) {
    for (size_t i = 0; i < points[w].size(); i++) {
      points[w][i].value *= weighting(points[w][i].freq);
    }
  }
}

void audio_transport::equal_loudness::remove(
    std::vector<std::vector<spectral::point>> & points) {
  for (size_t w = 0; w < points.size(); w++) {
    for (size_t i = 0; i < points[w].size(); i++) {
      double value = weighting(points[w][i].freq);
      if (value > 0) {
        points[w][i].value /= value;
      }
    }
  }
}
