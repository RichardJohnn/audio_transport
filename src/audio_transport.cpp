#include <cmath>
#include <vector>
#include <tuple>
#include <map>
#include <iostream>

#include "audio_transport/spectral.hpp"
#include "audio_transport/audio_transport.hpp"

// Minimum mass threshold to avoid division by zero/near-zero
static const double MIN_MASS_THRESHOLD = 1e-10;

std::vector<audio_transport::spectral::point> audio_transport::interpolate(
    const std::vector<audio_transport::spectral::point> & left,
    const std::vector<audio_transport::spectral::point> & right,
    std::vector<double> & phases,
    double window_size,
    double interpolation) {

  // Group the left and right spectra
  std::vector<spectral_mass> left_masses = group_spectrum(left);
  std::vector<spectral_mass> right_masses = group_spectrum(right);

  // Get the transport matrix
  std::vector<std::tuple<size_t, size_t, double>> T =
    transport_matrix(left_masses, right_masses);

  // Initialize the output spectral masses
  std::vector<audio_transport::spectral::point> interpolated(left.size());
  for (unsigned int i = 0; i < left.size(); i++) {
    interpolated[i].freq = left[i].freq;
  }

  // Initialize new phases
  std::vector<double> new_amplitudes(phases.size(), 0);
  std::vector<double> new_phases(phases.size(), 0);

  // Perform the interpolation
  for (auto t : T) {
    spectral_mass left_mass  =  left_masses[std::get<0>(t)];
    spectral_mass right_mass = right_masses[std::get<1>(t)];

    // Calculate the new bin and frequency
    int interpolated_bin = std::round(
      (1 - interpolation) * left_mass.center_bin +
      interpolation * right_mass.center_bin
      );

    // Compute the actual interpolation factor given the new bin
    double interpolation_rounded = interpolation;
    if (left_mass.center_bin != right_mass.center_bin) {
      interpolation_rounded = 
        ((double)interpolated_bin - (double)left_mass.center_bin)/
        ((double)right_mass.center_bin - (double)left_mass.center_bin);
    }
    // Interpolate the frequency appropriately
    double interpolated_freq = 
      (1 - interpolation_rounded) * left[left_mass.center_bin].freq_reassigned +
      interpolation_rounded * right[right_mass.center_bin].freq_reassigned;

    double center_phase =
      phases[interpolated_bin] + (interpolated_freq * window_size/2.)/2. - (M_PI * interpolated_bin);
    double new_phase = 
      center_phase + (interpolated_freq * window_size/2.)/2. + (M_PI * interpolated_bin);

    // Uncomment this for HORIZONTAL INCOHERENCE
    // center_phase = std::arg(left[interpolated_bin].value);

    // Place the left and right masses
    // Guard against division by zero/near-zero mass
    double left_scale = 0;
    double right_scale = 0;

    if (left_mass.mass > MIN_MASS_THRESHOLD) {
      left_scale = (1 - interpolation) * std::get<2>(t) / left_mass.mass;
    } else if (left_mass.mass > 0) {
      // Very small mass - log warning and clamp scale
      std::cerr << "[audio_transport] Warning: Very small left_mass.mass = "
                << left_mass.mass << " at bin " << left_mass.center_bin
                << ", clamping scale" << std::endl;
      left_scale = (1 - interpolation);  // Use transport mass directly as scale
    }

    if (right_mass.mass > MIN_MASS_THRESHOLD) {
      right_scale = interpolation * std::get<2>(t) / right_mass.mass;
    } else if (right_mass.mass > 0) {
      // Very small mass - log warning and clamp scale
      std::cerr << "[audio_transport] Warning: Very small right_mass.mass = "
                << right_mass.mass << " at bin " << right_mass.center_bin
                << ", clamping scale" << std::endl;
      right_scale = interpolation;  // Use transport mass directly as scale
    }

    place_mass(
        left_mass,
        interpolated_bin,
        left_scale,
        interpolated_freq,
        center_phase,
        left,
        interpolated,
        new_phase,
        new_phases,
        new_amplitudes
        );
    place_mass(
        right_mass,
        interpolated_bin,
        right_scale,
        interpolated_freq,
        center_phase,
        right,
        interpolated,
        new_phase,
        new_phases,
        new_amplitudes
        );

  }

  // Fill the phases with the new phases
  for (size_t i = 0; i < phases.size(); i++) {
    phases[i] = new_phases[i];
  }

  return interpolated;
}

void audio_transport::place_mass(
    const spectral_mass & mass,
    int center_bin,
    double scale,
    double interpolated_freq,
    double center_phase,
    const std::vector<audio_transport::spectral::point> & input,
    std::vector<audio_transport::spectral::point> & output,
    double next_phase,
    std::vector<double> & phases,
    std::vector<double> & amplitudes) {

  // Validate scale to prevent NaN/Inf propagation
  if (!std::isfinite(scale) || scale < 0) {
    std::cerr << "[audio_transport] Warning: Invalid scale = " << scale
              << " at center_bin = " << center_bin << ", skipping mass placement" << std::endl;
    return;
  }

  // Validate interpolated_freq
  if (!std::isfinite(interpolated_freq)) {
    std::cerr << "[audio_transport] Warning: Invalid interpolated_freq = " << interpolated_freq
              << " at center_bin = " << center_bin << ", skipping mass placement" << std::endl;
    return;
  }

  // Detect extremely low frequency that would cause crackling (DC-like)
  if (interpolated_freq < 20.0 && interpolated_freq > -20.0 && scale > 0.01) {
    std::cerr << "[audio_transport] Warning: Very low interpolated_freq = " << interpolated_freq
              << " Hz with scale = " << scale << " at center_bin = " << center_bin
              << " (potential crackling source)" << std::endl;
  }

  // Compute how the phase changes in each bin
  double phase_shift = center_phase - std::arg(input[mass.center_bin].value);

  for (size_t i = mass.left_bin; i < mass.right_bin; i++) {
    // Compute the location in the new array
    int new_i = i + center_bin - mass.center_bin;
    if (new_i < 0) continue;
    if (new_i >= (int) output.size()) continue;

    // Rotate the output by the phase offset
    // plus the frequency
    double phase = phase_shift + std::arg(input[i].value);
    double mag = scale * std::abs(input[i].value);

    // Skip if magnitude is invalid
    if (!std::isfinite(mag)) {
      std::cerr << "[audio_transport] Warning: Invalid magnitude = " << mag
                << " at bin " << new_i << ", skipping" << std::endl;
      continue;
    }

    output[new_i].value += std::polar(mag, phase);

    if (mag > amplitudes[new_i]) {
      amplitudes[new_i] = mag;
      phases[new_i] = next_phase;
      output[new_i].freq_reassigned = interpolated_freq;
    }
  }
}

std::vector<std::tuple<size_t, size_t, double>> audio_transport::transport_matrix(
    const std::vector<audio_transport::spectral_mass> & left,
    const std::vector<audio_transport::spectral_mass> & right) {

  // Initialize the algorithm
  std::vector<std::tuple<size_t, size_t, double>> T;
  size_t left_index = 0, right_index = 0;
  double left_mass  = left[0].mass;
  double right_mass = right[0].mass;

  while (true) {
    if (left_mass < right_mass) {
      T.emplace_back(
          left_index,
          right_index,
          left_mass);

      right_mass -= left_mass;

      left_index += 1;
      if (left_index >= left.size()) break;
      left_mass = left[left_index].mass;
    } else {
      T.emplace_back(
          left_index,
          right_index,
          right_mass);

      left_mass -= right_mass;

      right_index += 1;
      if (right_index >= right.size()) break;
      right_mass = right[right_index].mass;
    }
  }

  return T;
}

std::vector<audio_transport::spectral_mass> audio_transport::group_spectrum(
   const std::vector<audio_transport::spectral::point> & spectrum
   ) {

  // Keep track of the total mass
  double mass_sum = 0;
  for (size_t i = 0; i < spectrum.size(); i++) {
    mass_sum += std::abs(spectrum[i].value);
  }

  // Guard against silent/near-silent spectrum
  if (mass_sum < MIN_MASS_THRESHOLD) {
    std::cerr << "[audio_transport] Warning: Near-silent spectrum detected (mass_sum = "
              << mass_sum << "), returning single mass covering entire spectrum" << std::endl;
    // Return a single mass covering the entire spectrum with uniform distribution
    spectral_mass single_mass;
    single_mass.left_bin = 0;
    single_mass.center_bin = spectrum.size() / 2;
    single_mass.right_bin = spectrum.size();
    single_mass.mass = 1.0;  // Full normalized mass
    return {single_mass};
  }

  // Initialize the first mass
  std::vector<spectral_mass> masses;
  audio_transport::spectral_mass initial_mass;
  initial_mass.left_bin = 0;
  initial_mass.center_bin = 0;
  masses.push_back(initial_mass);

  bool sign;
  bool first = true;
  for (size_t i = 0; i < spectrum.size(); i++) {
    bool current_sign = (spectrum[i].freq_reassigned > spectrum[i].freq);

    // Uncomment this for VERTICAL INCOHERENCE
    //sign = false;
    //current_sign = not sign;

    if (first) {
      first = false;
      sign = current_sign;
      continue;
    }

    if (current_sign == sign) continue;

    if (sign) {
      // We are falling 
      // This is the center bin
      // Choose the one closest to the right

      // These should both be positive
      double left_dist = spectrum[i - 1].freq_reassigned - spectrum[i - 1].freq;
      double right_dist = spectrum[i].freq - spectrum[i].freq_reassigned;

      // Go to the closer side
      if (left_dist < right_dist) {
        masses[masses.size() - 1].center_bin = i - 1;
      } else {
        masses[masses.size() - 1].center_bin = i;
      }
    } else {
      // We are rising
      // This is the end

      // Compute the actual mass
      masses[masses.size() - 1].mass = 0;
      for (size_t j = masses[masses.size() - 1].left_bin; j < i; j++) {
        masses[masses.size() - 1].mass += std::abs(spectrum[j].value);
      }

      if (masses[masses.size() - 1].mass > 0) {
        // Normalize
        masses[masses.size() - 1].mass /= mass_sum;

        // Set the end of the mass
        masses[masses.size() - 1].right_bin = i;

        // Construct a new mass
        spectral_mass mass;
        mass.left_bin = i;
        mass.center_bin = i;
        masses.push_back(mass);
      }
    }
    sign = current_sign;
  }

  // Finish the last mass
  masses[masses.size() - 1].right_bin = spectrum.size();
  masses[masses.size() - 1].mass = 0;
  for (size_t j = masses[masses.size() - 1].left_bin; j < spectrum.size(); j++) {
    masses[masses.size() - 1].mass += std::abs(spectrum[j].value);
  }
  masses[masses.size() - 1].mass /= mass_sum;

  return masses;
} 
