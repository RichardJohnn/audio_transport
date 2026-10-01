#include <iostream>
#include <sstream>
#include <cmath>
#include <fstream>
#include <cstdio>
#include <stdexcept>

#include "transport_params.hpp"

using namespace transport_params;

static int failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
      std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #cond << std::endl; \
      failures++; \
    } \
  } while (0)

static bool near(double a, double b) { return std::abs(a - b) < 1e-12; }

static params parse(const std::string & text) {
  std::ostringstream warnings;
  return parse_params(nlohmann::json::parse(text), warnings);
}

static bool parse_throws(const std::string & text) {
  try { parse(text); } catch (const std::runtime_error &) { return true; }
  return false;
}

void test_defaults() {
  params p = parse(R"({"source": "a.wav", "target": "b.wav"})");
  CHECK(p.source == "a.wav");
  CHECK(p.target == "b.wav");
  CHECK(near(p.k, 0.5));
  CHECK(!p.has_envelope);
  CHECK(near(p.window, 50));
  CHECK(p.hop_div == 2);
  CHECK(p.fft_mult == 4);
  CHECK(p.output.empty());
}

void test_full_file() {
  params p = parse(R"({
    "source": "/x/nois.wav", "target": "/y/nois2.wav",
    "k_envelope": [{"percent": 90, "k": 0.9}, {"percent": 10, "k": 0.1}],
    "window": 500, "hop_div": 16, "fft_mult": 256, "output": "o.wav"
  })");
  CHECK(p.has_envelope);
  CHECK(p.k_envelope.size() == 2);
  CHECK(near(p.window, 500));
  CHECK(p.hop_div == 16);
  CHECK(p.fft_mult == 256);
  CHECK(p.output == "o.wav");
}

void test_null_envelope_means_constant_k() {
  params p = parse(R"({"source": "a", "target": "b", "k": 0.25, "k_envelope": null})");
  CHECK(!p.has_envelope);
  CHECK(near(p.k, 0.25));
}

void test_errors() {
  CHECK(parse_throws(R"({"target": "b"})"));
  CHECK(parse_throws(R"({"source": "a"})"));
  CHECK(parse_throws(R"({"source": 1, "target": "b"})"));
  CHECK(parse_throws(R"({"source": "a", "target": "b", "k": "x"})"));
  CHECK(parse_throws(R"({"source": "a", "target": "b", "hop_div": 1})"));
  CHECK(parse_throws(R"({"source": "a", "target": "b", "hop_div": 2.5})"));
  CHECK(parse_throws(R"({"source": "a", "target": "b", "fft_mult": 0})"));
  CHECK(parse_throws(R"({"source": "a", "target": "b", "window": 0})"));
  CHECK(parse_throws(R"({"source": "a", "target": "b", "k_envelope": [{"percent": 10}]})"));
  CHECK(parse_throws(R"({"source": "a", "target": "b", "k_envelope": {}})"));
  CHECK(parse_throws(R"(["a", "b"])"));
}

void test_unknown_keys_warn() {
  std::ostringstream warnings;
  parse_params(nlohmann::json::parse(R"({"source": "a", "target": "b", "demo": true})"), warnings);
  CHECK(warnings.str().find("demo") != std::string::npos);
}

void test_normalize_envelope() {
  // Empty means a full crossfade
  std::vector<keyframe> e = normalize_envelope({});
  CHECK(e.size() == 2);
  CHECK(near(e[0].percent, 0) && near(e[0].k, 0));
  CHECK(near(e[1].percent, 100) && near(e[1].k, 1));

  // Sorted, and a {0, 0} start inserted when missing
  e = normalize_envelope({{90, 0.9}, {10, 0.1}});
  CHECK(e.size() == 3);
  CHECK(near(e[0].percent, 0) && near(e[0].k, 0));
  CHECK(near(e[1].percent, 10) && near(e[1].k, 0.1));
  CHECK(near(e[2].percent, 90) && near(e[2].k, 0.9));

  // No insertion when a percent:0 keyframe exists
  e = normalize_envelope({{0, 0.3}, {100, 0.7}});
  CHECK(e.size() == 2);
  CHECK(near(e[0].k, 0.3));
}

void test_interpolate_k() {
  std::vector<keyframe> e = normalize_envelope({{10, 0.1}, {90, 0.9}});
  CHECK(near(interpolate_k(0, e), 0));
  CHECK(near(interpolate_k(5, e), 0.05));
  CHECK(near(interpolate_k(50, e), 0.5));
  CHECK(near(interpolate_k(90, e), 0.9));
  CHECK(near(interpolate_k(95, e), 0.9));   // holds last k
  CHECK(near(interpolate_k(100, e), 0.9));

  // Decreasing k, and duplicate percents
  e = normalize_envelope({{0, 1}, {50, 0.2}, {50, 0.6}, {100, 0}});
  CHECK(near(interpolate_k(25, e), 0.6));
  CHECK(near(interpolate_k(50, e), 1.0 - 0.8));
  CHECK(near(interpolate_k(75, e), 0.3));

  CHECK(near(interpolate_k(50, {}), 0.5));
}

void test_k_str() {
  params p = parse(R"({"source": "a", "target": "b", "k": 0.5})");
  CHECK(k_str(p) == "k0.5");
  p.k = 1.0;   CHECK(k_str(p) == "k1");
  p.k = 0.0;   CHECK(k_str(p) == "k0");
  p.k = 0.25;  CHECK(k_str(p) == "k0.25");
  p.k = 10.0;  CHECK(k_str(p) == "k10");

  p = parse(R"({"source": "a", "target": "b", "k_envelope": [{"percent": 90, "k": 0.9}, {"percent": 10, "k": 0.1}]})");
  CHECK(k_str(p) == "env01-09");
  p = parse(R"({"source": "a", "target": "b", "k_envelope": []})");
  CHECK(k_str(p) == "env00-10");
  p = parse(R"({"source": "a", "target": "b", "k_envelope": [{"percent": 0, "k": 0}, {"percent": 100, "k": 1}]})");
  CHECK(k_str(p) == "env00-10");
}

void test_output_basename() {
  params p = parse(R"({
    "source": "/Users/rich/Desktop/nois.wav", "target": "/Users/rich/Desktop/nois2.wav",
    "k_envelope": [{"percent": 10, "k": 0.1}, {"percent": 90, "k": 0.9}],
    "window": 500, "hop_div": 16, "fft_mult": 256
  })");
  CHECK(output_basename(p) == "nois_nois2_env01-09_w500_hd16_fftm256");

  p = parse(R"({"source": "dir/pad.flac", "target": "wewww.wav", "k": 0.5, "window": 200, "hop_div": 8, "fft_mult": 32})");
  CHECK(output_basename(p) == "pad_wewww_k0.5_w200_hd8_fftm32");

  p.window = 12.5;
  CHECK(output_basename(p) == "pad_wewww_k0.5_w12.5_hd8_fftm32");
}

void test_unique_output_filename() {
  params p = parse(R"({"source": "tp_a.wav", "target": "tp_b.wav", "k": 0.5})");
  std::string base = output_basename(p);
  std::remove((base + ".wav").c_str());
  std::remove((base + "_2.wav").c_str());

  CHECK(output_filename(p) == base + ".wav");
  std::ofstream(base + ".wav").put('x');
  CHECK(output_filename(p) == base + "_2.wav");
  std::ofstream(base + "_2.wav").put('x');
  CHECK(output_filename(p) == base + "_3.wav");

  std::remove((base + ".wav").c_str());
  std::remove((base + "_2.wav").c_str());

  p.output = "explicit.wav";
  CHECK(output_filename(p) == "explicit.wav");
}

int main() {
  std::cout << "test_transport_params" << std::endl;
  test_defaults();
  test_full_file();
  test_null_envelope_means_constant_k();
  test_errors();
  test_unknown_keys_warn();
  test_normalize_envelope();
  test_interpolate_k();
  test_k_str();
  test_output_basename();
  test_unique_output_filename();
  std::cout << (failures ? "FAILED: " : "OK, failures: ") << failures << std::endl;
  return failures;
}
