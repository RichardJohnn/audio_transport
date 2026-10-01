#pragma once

#include <vector>
#include <tuple>
#include <map>

#include "audio_transport/spectral.hpp"

namespace audio_transport {

struct spectral_mass {
  size_t left_bin;
  size_t right_bin;
  size_t center_bin;
  double mass;
};

/**
 * Interpolate between two spectra. Phases advance by hop_seconds,
 * the time between consecutive analysis windows.
 */
std::vector<audio_transport::spectral::point> interpolate_hop(
    const std::vector<audio_transport::spectral::point> & left,
    const std::vector<audio_transport::spectral::point> & right,
    std::vector<double> & phases,
    double hop_seconds,
    double interpolation_factor);

/**
 * Phases to start interpolate_hop() with, so the first window keeps the
 * phase of `spectrum`. Starting from zeros lines every partial up in the
 * first window, which synthesizes a loud click.
 */
std::vector<double> initial_phases(
    const std::vector<audio_transport::spectral::point> & spectrum,
    double hop_seconds);

/**
 * Legacy form assuming a hop of window_size/2 (overlap = 1).
 */
std::vector<audio_transport::spectral::point> interpolate(
    const std::vector<audio_transport::spectral::point> & left,
    const std::vector<audio_transport::spectral::point> & right,
    std::vector<double> & phases,
    double window_size,
    double interpolation_factor);

std::vector<std::tuple<size_t, size_t, double>> transport_matrix(
    const std::vector<spectral_mass> & left,
    const std::vector<spectral_mass> & right);

std::vector<spectral_mass> group_spectrum(
    const std::vector<audio_transport::spectral::point> & spectrum);

void place_mass(
    const spectral_mass & mass,
    int center_bin,
    double scale,
    double interpolated_freq,
    double center_phase,
    const std::vector<double> & magnitude, // |input| per bin
    const std::vector<double> & phase,     // arg(input) per bin
    std::vector<audio_transport::spectral::point> & output,
    double next_phase,
    std::vector<double> & phases,
    std::vector<double> & amplitudes);

}
