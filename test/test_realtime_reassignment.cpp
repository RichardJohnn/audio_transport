#include "audio_transport/RealtimeReassignmentTransport.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

using namespace audio_transport;

int main() {
    int failures = 0;
    const double rate = 8000;
    struct Config { double ms; int div, mult; };
    const Config configs[] = {{17, 2, 2}, {31, 4, 4}, {50, 8, 8}, {23, 16, 2}};
    for (const auto& c : configs) {
        const auto p = spectral::stft_params::from_python(rate, c.ms, c.div, c.mult);
        RealtimeReassignmentTransport realtime(rate, c.ms, c.div, c.mult);
        const size_t n = static_cast<size_t>(std::ceil(rate * c.ms / 1000.0 / c.div)) * c.div;
        size_t pow2 = 1;
        while (pow2 < n) pow2 *= 2;
        if (realtime.getWindowSize() != static_cast<int>(n)
            || realtime.getHopSize() != static_cast<int>(n / c.div)
            || realtime.getFFTSize() != static_cast<int>(pow2 * c.mult)) ++failures;

        // Float-quantize the reference input too: the streaming API accepts floats.
        const size_t length = 1600;
        std::vector<double> a(length), b(length);
        for (size_t i = 0; i < length; ++i) {
            a[i] = static_cast<float>(0.3 * std::sin(2 * M_PI * 230 * i / rate)
                                   + 0.12 * std::sin(2 * M_PI * 510 * i / rate + 0.4));
            b[i] = static_cast<float>(0.25 * std::sin(2 * M_PI * 370 * i / rate + 0.7)
                                   + 0.1 * std::sin(2 * M_PI * 680 * i / rate));
        }
        const float k = 0.37f;
        spectral::analyzer analyzer(rate, p);
        spectral::synthesizer synth(p);
        std::vector<double> reference(length, 0), phases;
        size_t frames = 0;
        for (size_t pos = 0; pos + p.window <= length; pos += p.hop) {
            const double time = (pos + (p.window - 1) / 2.0) / rate;
            auto left = analyzer.analyze(a.data() + pos, time);
            auto right = analyzer.analyze(b.data() + pos, time);
            if (phases.empty()) phases = initial_phases(left, p.hop / rate);
            auto frame = synth.synthesize(interpolate_hop(left, right, phases, p.hop / rate, k));
            for (size_t j = 0; j < p.window; ++j) reference[pos + j] += frame[j];
            ++frames;
        }
        const size_t latency = realtime.getLatencySamples();
        std::vector<float> inputA(length + latency, 0), inputB(inputA.size(), 0), out(inputA.size());
        std::copy(a.begin(), a.end(), inputA.begin());
        std::copy(b.begin(), b.end(), inputB.begin());
        // Include blocks smaller than a hop and larger than the output queue/window.
        for (int block : {1, 37, 1024}) {
            realtime.reset();
            for (size_t pos = 0; pos < out.size();) {
                const int count = static_cast<int>(std::min(size_t(block), out.size() - pos));
                realtime.process(inputA.data() + pos, inputB.data() + pos, out.data() + pos, count, k);
                pos += count;
            }
            double error = 0, energy = 0;
            for (size_t i = 0; i < latency; ++i)
                if (out[i] != 0) ++failures;
            // Only compare finalized hops; later offline tail samples lack future windows.
            for (size_t i = 0; i < frames * p.hop; ++i) {
                error = std::max(error, std::abs(out[latency + i] - reference[i]));
                energy += reference[i] * reference[i];
            }
            // Same shared FFT/math; 2e-6 allows float output rounding and FFT plan roundoff,
            // while remaining far below audible amplitude or a phase/gain/latency mismatch.
            if (!(error < 2e-6) || !(energy > 0.01)) ++failures;
            std::cout << c.ms << "ms / " << c.div << " / " << c.mult
                      << ", block " << block << ": max error " << error << '\n';
        }
    }
    std::cout << "failures: " << failures << '\n';
    return failures ? 1 : 0;
}
