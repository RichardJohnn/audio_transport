#pragma once

// Parameters for `transport -f params.json`, matching audio_transport.py:
// the same JSON keys, k envelope interpolation and output file naming.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace transport_params {

struct keyframe {
  double percent;
  double k;
};

struct params {
  std::string source;
  std::string target;
  std::string output;          // empty = auto-generated
  double k = 0.5;
  bool has_envelope = false;   // k_envelope present and not null
  std::vector<keyframe> k_envelope; // as given in the file
  double window = 50;          // ms
  unsigned int hop_div = 2;
  unsigned int fft_mult = 4;
};

inline std::string usage_json() {
  return
    "  {\n"
    "    \"source\": \"/path/to/source.wav\",  # required (k=0)\n"
    "    \"target\": \"/path/to/target.wav\",  # required (k=1)\n"
    "    \"k\": 0.5,                          # constant interpolation (default 0.5)\n"
    "    \"k_envelope\": [                    # time-varying k, overrides \"k\"\n"
    "      {\"percent\": 0, \"k\": 0},\n"
    "      {\"percent\": 100, \"k\": 1}\n"
    "    ],\n"
    "    \"window\": 50,                      # window size in ms (default 50)\n"
    "    \"hop_div\": 2,                      # hop = window/hop_div, >= 2 (default 2)\n"
    "    \"fft_mult\": 4,                     # fft = nextpow2(window)*fft_mult (default 4)\n"
    "    \"output\": \"out.wav\"                # optional, auto-generated if omitted\n"
    "  }\n";
}

namespace detail {

inline double get_number(const nlohmann::json & j, const std::string & key) {
  if (!j.is_number()) throw std::runtime_error("\"" + key + "\" must be a number");
  return j.get<double>();
}

inline unsigned int get_unsigned(const nlohmann::json & j, const std::string & key, unsigned int min) {
  double v = get_number(j, key);
  if (v != std::floor(v) || v < min || v > 1e9) {
    throw std::runtime_error("\"" + key + "\" must be an integer >= " + std::to_string(min));
  }
  return (unsigned int) v;
}

inline std::string get_string(const nlohmann::json & j, const std::string & key) {
  if (!j.is_string()) throw std::runtime_error("\"" + key + "\" must be a string");
  return j.get<std::string>();
}

// Python's "%.1f" / "%.2f" formatting
inline std::string format_fixed(double v, int digits) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.*f", digits, v);
  return buf;
}

// Python's str() for a JSON number: integers print without a decimal point
inline std::string format_number(double v) {
  if (v == std::floor(v) && std::abs(v) < 1e15) return std::to_string((long long) v);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.15g", v);
  return buf;
}

inline std::string stem(const std::string & path) {
  size_t slash = path.find_last_of("/\\");
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot != 0) name = name.substr(0, dot);
  return name;
}

inline bool file_exists(const std::string & path) {
  std::ifstream f(path.c_str());
  return f.good();
}

inline bool by_percent(const keyframe & a, const keyframe & b) {
  return a.percent < b.percent;
}

}

/**
 * Read parameters from parsed JSON. Missing keys take the defaults in
 * `params`; unknown keys are reported to `warnings` and ignored.
 * Throws std::runtime_error on missing or malformed values.
 */
inline params parse_params(const nlohmann::json & j, std::ostream & warnings) {
  if (!j.is_object()) throw std::runtime_error("parameter file must contain a JSON object");

  params p;
  for (auto it = j.begin(); it != j.end(); ++it) {
    const std::string & key = it.key();
    const nlohmann::json & v = it.value();
    if (key == "source") {
      p.source = detail::get_string(v, key);
    } else if (key == "target") {
      p.target = detail::get_string(v, key);
    } else if (key == "output") {
      if (!v.is_null()) p.output = detail::get_string(v, key);
    } else if (key == "k") {
      p.k = detail::get_number(v, key);
    } else if (key == "k_envelope") {
      if (v.is_null()) continue;
      if (!v.is_array()) throw std::runtime_error("\"k_envelope\" must be an array or null");
      p.has_envelope = true;
      for (const nlohmann::json & e : v) {
        if (!e.is_object() || !e.contains("percent") || !e.contains("k")) {
          throw std::runtime_error("each \"k_envelope\" entry needs \"percent\" and \"k\"");
        }
        keyframe f;
        f.percent = detail::get_number(e["percent"], "percent");
        f.k = detail::get_number(e["k"], "k");
        p.k_envelope.push_back(f);
      }
    } else if (key == "window") {
      p.window = detail::get_number(v, key);
      if (!(p.window > 0)) throw std::runtime_error("\"window\" must be > 0");
    } else if (key == "hop_div") {
      p.hop_div = detail::get_unsigned(v, key, 2);
    } else if (key == "fft_mult") {
      p.fft_mult = detail::get_unsigned(v, key, 1);
    } else {
      warnings << "Warning: ignoring unknown parameter \"" << key << "\"" << std::endl;
    }
  }

  if (p.source.empty()) throw std::runtime_error("\"source\" is required");
  if (p.target.empty()) throw std::runtime_error("\"target\" is required");
  return p;
}

/**
 * Empty means a full crossfade {0,0} -> {100,1}. Otherwise sort by
 * percent and start at {0,0} unless a keyframe is already at 0.
 */
inline std::vector<keyframe> normalize_envelope(std::vector<keyframe> envelope) {
  if (envelope.empty()) {
    keyframe start = {0, 0}, end = {100, 1};
    envelope.push_back(start);
    envelope.push_back(end);
    return envelope;
  }
  std::stable_sort(envelope.begin(), envelope.end(), detail::by_percent);
  if (envelope.front().percent > 0) {
    keyframe start = {0, 0};
    envelope.insert(envelope.begin(), start);
  }
  return envelope;
}

/**
 * Linearly interpolate k at `percent` (0-100) through sorted keyframes,
 * holding the first and last values outside them.
 */
inline double interpolate_k(double percent, const std::vector<keyframe> & envelope) {
  if (envelope.empty()) return 0.5;

  if (percent <= envelope.front().percent) return envelope.front().k;
  if (percent >= envelope.back().percent) return envelope.back().k;

  for (size_t i = 0; i + 1 < envelope.size(); i++) {
    double p0 = envelope[i].percent, k0 = envelope[i].k;
    double p1 = envelope[i + 1].percent, k1 = envelope[i + 1].k;
    if (p0 <= percent && percent <= p1) {
      if (p1 == p0) return k0;
      double t = (percent - p0)/(p1 - p0);
      return k0 + t * (k1 - k0);
    }
  }
  return envelope.back().k;
}

/**
 * The k part of the output filename: "k0.5" or "env01-09".
 */
inline std::string k_str(const params & p) {
  if (p.has_envelope) {
    if (p.k_envelope.empty()) return "env00-10";
    std::vector<keyframe> sorted = p.k_envelope;
    std::stable_sort(sorted.begin(), sorted.end(), detail::by_percent);
    std::string s = "env" + detail::format_fixed(sorted.front().k, 1) + "-" + detail::format_fixed(sorted.back().k, 1);
    s.erase(std::remove(s.begin(), s.end(), '.'), s.end());
    return s;
  }
  std::string s = "k" + detail::format_fixed(p.k, 2);
  s.erase(s.find_last_not_of('0') + 1);
  s.erase(s.find_last_not_of('.') + 1);
  return s;
}

inline std::string output_basename(const params & p) {
  return detail::stem(p.source) + "_" + detail::stem(p.target) + "_" + k_str(p) +
    "_w" + detail::format_number(p.window) +
    "_hd" + std::to_string(p.hop_div) +
    "_fftm" + std::to_string(p.fft_mult);
}

/**
 * The explicit output, or the auto-generated name with _2, _3, ...
 * appended until it doesn't clash with an existing file.
 */
inline std::string output_filename(const params & p) {
  if (!p.output.empty()) return p.output;
  std::string base = output_basename(p);
  std::string filename = base + ".wav";
  for (int counter = 2; detail::file_exists(filename); counter++) {
    filename = base + "_" + std::to_string(counter) + ".wav";
  }
  return filename;
}

}
