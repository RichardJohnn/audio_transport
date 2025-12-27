#!/usr/bin/env python3
"""
Audio Transport: A Generalized Portamento via Optimal Transport
Implementation based on Henderson & Solomon (DAFx 2019)

This implements the FULL optimal transport algorithm, not just spectral blending.
The key insight: treat magnitude spectra as probability distributions and compute
the optimal transport map to "move" frequencies from source to target.
"""

import os
import json
import numpy as np
from scipy import signal
from scipy.io import wavfile
from scipy.interpolate import interp1d


class AudioTransport:
    def __init__(self, sample_rate=44100, window_ms=100, hop_divisor=4, fft_mult=2):
        """
        Initialize the Audio Transport processor.

        Args:
            sample_rate: Audio sample rate in Hz
            window_ms: Window size in milliseconds (larger = better freq resolution,
                       but worse time resolution). Default 100ms for high fidelity.
            hop_divisor: Hop size as fraction of window (4 = 75% overlap, 8 = 87.5%)
            fft_mult: FFT size multiplier (2 = 2x zero-padding for smoother spectrum)
        """
        self.sr = sample_rate
        self.nperseg = int(window_ms * sample_rate / 1000)
        self.hop = self.nperseg // hop_divisor
        self.noverlap = self.nperseg - self.hop

        # Zero-pad FFT for smoother spectral interpolation
        self.nfft = int(2 ** np.ceil(np.log2(self.nperseg))) * fft_mult

        # Hann window - good for OLA reconstruction
        self.window = signal.windows.hann(self.nperseg, sym=False)

        # Precompute frequency bin centers
        self.freqs = np.fft.rfftfreq(self.nfft, 1 / self.sr)
        self.num_bins = len(self.freqs)

        freq_res = self.sr / self.nfft
        print(f"Spectral resolution: {freq_res:.1f} Hz per bin, {self.num_bins} bins")

    def _stft(self, audio):
        """Compute STFT."""
        f, t, Zxx = signal.stft(
            audio,
            fs=self.sr,
            window=self.window,
            nperseg=self.nperseg,
            noverlap=self.noverlap,
            nfft=self.nfft,
        )
        return Zxx

    def _istft(self, Zxx):
        """Compute inverse STFT."""
        _, audio = signal.istft(
            Zxx,
            fs=self.sr,
            window=self.window,
            nperseg=self.nperseg,
            noverlap=self.noverlap,
            nfft=self.nfft,
        )
        return audio

    def _compute_transport_map(self, mag_X, mag_Y):
        """
        Compute 1D optimal transport map from distribution X to Y.

        For 1D distributions, optimal transport has a closed-form solution:
        T(x) = F_Y^{-1}(F_X(x)) where F_X and F_Y are CDFs.

        Returns: transport_map where transport_map[i] is the target bin index
                 that bin i should move towards.
        """
        eps = 1e-10

        # Normalize to probability distributions
        p_X = mag_X / (mag_X.sum() + eps)
        p_Y = mag_Y / (mag_Y.sum() + eps)

        # Compute CDFs
        cdf_X = np.cumsum(p_X)
        cdf_Y = np.cumsum(p_Y)

        # For each bin in X, find where it maps to in Y
        # T(x) = F_Y^{-1}(F_X(x))
        # We need to invert F_Y: find the bin where cdf_Y >= cdf_X[i]
        transport_map = np.zeros(len(mag_X))

        for i in range(len(mag_X)):
            # Find smallest j where cdf_Y[j] >= cdf_X[i]
            target_indices = np.where(cdf_Y >= cdf_X[i] - eps)[0]
            if len(target_indices) > 0:
                transport_map[i] = target_indices[0]
            else:
                transport_map[i] = len(mag_Y) - 1

        return transport_map

    def _interpolate_spectrum(self, mag_X, phase_X, mag_Y, phase_Y, k):
        """
        Interpolate spectrum using optimal transport.

        Instead of just blending magnitudes, we:
        1. Compute transport map from X to Y
        2. Move each frequency bin along the transport path
        3. Accumulate energy at interpolated positions
        """
        num_bins = len(mag_X)
        bin_indices = np.arange(num_bins)

        # Compute transport map
        transport_map = self._compute_transport_map(mag_X, mag_Y)

        # Interpolated bin positions: each bin moves from its position toward its target
        interp_positions = (1 - k) * bin_indices + k * transport_map

        # Output spectrum - accumulate energy at interpolated positions
        mag_out = np.zeros(num_bins)
        phase_out = np.zeros(num_bins)
        weight_sum = np.zeros(num_bins) + 1e-10  # For weighted phase averaging

        for i in range(num_bins):
            # Where does bin i end up?
            target_pos = interp_positions[i]

            # Interpolate magnitude: blend source and target magnitudes
            interp_mag = (1 - k) * mag_X[i] + k * mag_Y[int(transport_map[i])]

            # Distribute energy to neighboring bins (linear interpolation)
            low_bin = int(np.floor(target_pos))
            high_bin = int(np.ceil(target_pos))
            frac = target_pos - low_bin

            if low_bin >= 0 and low_bin < num_bins:
                mag_out[low_bin] += (1 - frac) * interp_mag
                weight_sum[low_bin] += (1 - frac) * interp_mag
                # Phase: weighted combination
                phase_out[low_bin] += (1 - frac) * interp_mag * phase_X[i]

            if high_bin >= 0 and high_bin < num_bins and high_bin != low_bin:
                mag_out[high_bin] += frac * interp_mag
                weight_sum[high_bin] += frac * interp_mag
                phase_out[high_bin] += frac * interp_mag * phase_X[i]

        # Normalize phase by weights
        phase_out = phase_out / weight_sum

        # Where we have little energy from transport, blend in target phase
        low_energy_mask = weight_sum < 1e-8
        phase_out[low_energy_mask] = phase_Y[low_energy_mask]

        return mag_out, phase_out

    def _interpolate_spectrum_vectorized(self, mag_X, phase_X, mag_Y, phase_Y, k):
        """
        Vectorized version of spectrum interpolation using optimal transport.
        Faster than the loop version.
        """
        num_bins = len(mag_X)
        bin_indices = np.arange(num_bins, dtype=np.float64)
        eps = 1e-10

        # Normalize to probability distributions
        total_X = mag_X.sum() + eps
        total_Y = mag_Y.sum() + eps
        p_X = mag_X / total_X
        p_Y = mag_Y / total_Y

        # Compute CDFs
        cdf_X = np.cumsum(p_X)
        cdf_Y = np.cumsum(p_Y)

        # Compute transport map using searchsorted (vectorized)
        # For each bin i, find where cdf_Y first exceeds cdf_X[i]
        transport_map = np.searchsorted(cdf_Y, cdf_X, side='left')
        transport_map = np.clip(transport_map, 0, num_bins - 1).astype(np.float64)

        # Interpolated positions
        interp_positions = (1 - k) * bin_indices + k * transport_map

        # Interpolated magnitudes along transport
        target_indices = transport_map.astype(int)
        interp_mag = (1 - k) * mag_X + k * mag_Y[target_indices]

        # Scatter energy to output bins
        low_bins = np.floor(interp_positions).astype(int)
        high_bins = np.ceil(interp_positions).astype(int)
        fracs = interp_positions - low_bins

        # Clip to valid range
        low_bins = np.clip(low_bins, 0, num_bins - 1)
        high_bins = np.clip(high_bins, 0, num_bins - 1)

        # Accumulate using np.add.at for proper handling of duplicate indices
        mag_out = np.zeros(num_bins)
        phase_num = np.zeros(num_bins)  # Numerator for weighted phase
        weight_sum = np.zeros(num_bins)

        low_weights = (1 - fracs) * interp_mag
        high_weights = fracs * interp_mag

        np.add.at(mag_out, low_bins, low_weights)
        np.add.at(mag_out, high_bins, high_weights)

        np.add.at(weight_sum, low_bins, low_weights)
        np.add.at(weight_sum, high_bins, high_weights)

        # Weighted phase (circular mean would be better, but this works reasonably)
        np.add.at(phase_num, low_bins, low_weights * phase_X)
        np.add.at(phase_num, high_bins, high_weights * phase_X)

        # Compute output phase
        weight_sum = np.maximum(weight_sum, eps)
        phase_out = phase_num / weight_sum

        # Blend in target phase where transport didn't contribute much
        blend_factor = np.clip(weight_sum / (mag_Y + eps), 0, 1)
        phase_out = blend_factor * phase_out + (1 - blend_factor) * phase_Y

        return mag_out, phase_out

    def process(self, audio_X, audio_Y, k=None, k_envelope=None):
        """
        Apply audio transport effect using optimal transport.

        This is the FULL algorithm:
        1. Compute STFT of both signals
        2. For each frame, compute optimal transport map between magnitude spectra
        3. Interpolate frequencies along the transport path
        4. Reconstruct audio from transported spectra

        Args:
            audio_X: Source audio signal
            audio_Y: Target audio signal
            k: Constant interpolation parameter (0 = source, 1 = target)
            k_envelope: List of {"percent": x, "k": y} keyframes for time-varying k
                        (overrides k if provided)

        Returns:
            Interpolated audio signal
        """
        # Ensure equal length
        max_len = max(len(audio_X), len(audio_Y))
        audio_X = np.pad(audio_X, (0, max_len - len(audio_X)))
        audio_Y = np.pad(audio_Y, (0, max_len - len(audio_Y)))

        print("Computing STFTs...")
        X = self._stft(audio_X)
        Y = self._stft(audio_Y)

        num_bins, num_frames = X.shape

        # Sort envelope by percent if provided
        if k_envelope is not None:
            # Empty array means full crossfade from source to target
            if len(k_envelope) == 0:
                k_envelope = [{"percent": 0, "k": 0.0}, {"percent": 100, "k": 1.0}]
            else:
                k_envelope = sorted(k_envelope, key=lambda x: x["percent"])

                # Add default start point if not specified (defaults to k=0)
                if k_envelope[0]["percent"] > 0:
                    k_envelope.insert(0, {"percent": 0, "k": 0})

            print(f"Processing {num_frames} frames with time-varying k...")
            print(f"  Keyframes: {k_envelope}")
        else:
            print(f"Processing {num_frames} frames with k={k}...")

        # Get magnitudes and phases
        mag_X = np.abs(X)
        mag_Y = np.abs(Y)
        phase_X = np.angle(X)
        phase_Y = np.angle(Y)

        # Process each frame with optimal transport
        output_stft = np.zeros_like(X)

        for frame in range(num_frames):
            # Calculate k for this frame
            if k_envelope:
                percent = (frame / (num_frames - 1)) * 100 if num_frames > 1 else 0
                frame_k = interpolate_k(percent, k_envelope)
            else:
                frame_k = k

            if frame % 100 == 0:
                print(f"  Frame {frame}/{num_frames} (k={frame_k:.3f})")

            mag_out, phase_out = self._interpolate_spectrum_vectorized(
                mag_X[:, frame], phase_X[:, frame],
                mag_Y[:, frame], phase_Y[:, frame],
                frame_k
            )

            output_stft[:, frame] = mag_out * np.exp(1j * phase_out)

        print("Reconstructing audio...")
        output = self._istft(output_stft)

        return output


def create_test_signals(duration=2.0, sr=44100):
    """Create test signals: sine waves at different frequencies."""
    t = np.linspace(0, duration, int(duration * sr))
    signal_A4 = np.sin(2 * np.pi * 440 * t)
    signal_Cs5 = np.sin(2 * np.pi * 554.37 * t)
    return signal_A4, signal_Cs5, sr


def interpolate_k(percent, k_envelope):
    """
    Interpolate k value based on position in file using keyframes.

    Args:
        percent: Current position as percentage (0-100)
        k_envelope: List of {"percent": x, "k": y} keyframes, sorted by percent

    Returns:
        Interpolated k value
    """
    if not k_envelope:
        return 0.5

    # Handle edge cases
    if percent <= k_envelope[0]["percent"]:
        return k_envelope[0]["k"]
    if percent >= k_envelope[-1]["percent"]:
        return k_envelope[-1]["k"]

    # Find surrounding keyframes
    for i in range(len(k_envelope) - 1):
        p0, k0 = k_envelope[i]["percent"], k_envelope[i]["k"]
        p1, k1 = k_envelope[i + 1]["percent"], k_envelope[i + 1]["k"]

        if p0 <= percent <= p1:
            # Linear interpolation
            if p1 == p0:
                return k0
            t = (percent - p0) / (p1 - p0)
            return k0 + t * (k1 - k0)

    return k_envelope[-1]["k"]


def generate_output_filename(source, target, k, window, hop_div, fft_mult, k_envelope=None):
    """Generate output filename from parameters."""
    src_name = os.path.splitext(os.path.basename(source))[0]
    tgt_name = os.path.splitext(os.path.basename(target))[0]

    # Format k part of filename
    if k_envelope is not None:
        # Empty array means full crossfade
        if len(k_envelope) == 0:
            k_str = "env00-10"
        else:
            # Sort by percent to get correct start/end
            sorted_env = sorted(k_envelope, key=lambda x: x["percent"])
            k_start = sorted_env[0]["k"]
            k_end = sorted_env[-1]["k"]
            k_str = f"env{k_start:.1f}-{k_end:.1f}".replace(".", "")
    else:
        # Format k nicely (remove trailing zeros)
        k_str = f"k{k:.2f}".rstrip('0').rstrip('.')

    base = f"{src_name}_{tgt_name}_{k_str}_w{window}_hd{hop_div}_fftm{fft_mult}"

    # Check for existing files and add suffix if needed
    filename = f"{base}.wav"
    counter = 2
    while os.path.exists(filename):
        filename = f"{base}_{counter}.wav"
        counter += 1

    return filename


if __name__ == "__main__":
    import argparse
    import time

    epilog = """
JSON Parameter File (-f):
  All CLI parameters can be specified in a JSON file. CLI args override file values.

  Schema:
  {
    "source": "/path/to/source.wav",     # Source audio file
    "target": "/path/to/target.wav",     # Target audio file
    "k": 0.5,                            # Constant interpolation (0=source, 1=target)
    "k_envelope": [                      # Time-varying k (overrides "k" if present)
      {"percent": 0, "k": 0},            #   percent: position in output (0-100)
      {"percent": 50, "k": 0.75},        #   k: interpolation value at that point
      {"percent": 100, "k": 1.0}         #   (linearly interpolated between points)
    ],
    "window": 100,                       # Window size in ms (default: 100)
    "hop_div": 4,                        # Hop divisor (default: 4 = 75%% overlap)
    "fft_mult": 2,                       # FFT multiplier (default: 2, use powers of 2)
    "output": "output.wav"               # Output filename (optional, auto-generated)
  }

  k_envelope notes:
    - Empty array [] = full crossfade [{"percent":0,"k":0},{"percent":100,"k":1}]
    - Array is auto-sorted by percent
    - If no percent:0 entry, defaults to {"percent": 0, "k": 0}
    - If no percent:100 entry, holds last k value until end
    - k values can increase or decrease between keyframes

Examples:
  # Basic usage with constant k
  %(prog)s source.wav target.wav -k 0.5

  # High quality settings
  %(prog)s source.wav target.wav -k 0.75 --window 200 --fft-mult 4

  # Using a parameter file
  %(prog)s -f params.json

  # Override k from parameter file
  %(prog)s -f params.json -k 0.25

  # Crossfade from source to target
  echo '{"source":"a.wav","target":"b.wav","k_envelope":[{"percent":0,"k":0},{"percent":100,"k":1}]}' > crossfade.json
  %(prog)s -f crossfade.json
"""

    parser = argparse.ArgumentParser(
        description="Audio Transport: Spectral morphing between two audio files using optimal transport. "
                    "Based on Henderson & Solomon (DAFx 2019).",
        epilog=epilog,
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("source", nargs="?", metavar="SOURCE",
                        help="Source WAV file (k=0)")
    parser.add_argument("target", nargs="?", metavar="TARGET",
                        help="Target WAV file (k=1)")
    parser.add_argument("-k", type=float, default=None, metavar="VALUE",
                        help="Interpolation 0-1 (default: 0.5). 0=source, 1=target, "
                             "0.5=halfway morph. Overrides k_envelope from -f file.")
    parser.add_argument("-o", "--output", default=None, metavar="FILE",
                        help="Output WAV file. Default: auto-generated from params, "
                             "e.g., source_target_k0.5_w100_hd4_fftm2.wav")
    parser.add_argument("-f", "--file", default=None, metavar="JSON",
                        help="JSON parameter file (see schema below). "
                             "CLI args override values from file.")
    parser.add_argument("--window", type=int, default=None, metavar="MS",
                        help="STFT window size in milliseconds (default: 100). "
                             "Larger = better frequency resolution, worse time resolution. "
                             "Try 200 for higher quality.")
    parser.add_argument("--hop-div", type=int, default=None, metavar="N",
                        help="Hop divisor (default: 4). Window/N = hop size. "
                             "4=75%% overlap, 8=87.5%% overlap. Higher = smoother but slower.")
    parser.add_argument("--fft-mult", type=int, default=None, metavar="N",
                        help="FFT size multiplier for zero-padding (default: 2). "
                             "Use powers of 2 (1,2,4,8). Higher = smoother spectral interpolation.")
    parser.add_argument("--demo", action="store_true",
                        help="Run demo with sine waves (A4=440Hz to C#5=554Hz)")

    args = parser.parse_args()

    # Default values
    defaults = {
        "k": 0.5,
        "k_envelope": None,  # e.g. [{"percent": 0, "k": 0}, {"percent": 100, "k": 1}]
        "window": 100,
        "hop_div": 4,
        "fft_mult": 2,
        "output": None,
        "source": None,
        "target": None,
        "demo": False,
    }

    # Load from JSON file if provided
    if args.file:
        with open(args.file, 'r') as f:
            file_params = json.load(f)
        defaults.update(file_params)
        print(f"Loaded parameters from {args.file}")

    # CLI args override file/defaults (only if explicitly set)
    if args.source is not None:
        defaults["source"] = args.source
    if args.target is not None:
        defaults["target"] = args.target
    if args.k is not None:
        defaults["k"] = args.k
    if args.output is not None:
        defaults["output"] = args.output
    if args.window is not None:
        defaults["window"] = args.window
    if args.hop_div is not None:
        defaults["hop_div"] = args.hop_div
    if args.fft_mult is not None:
        defaults["fft_mult"] = args.fft_mult
    if args.demo:
        defaults["demo"] = True

    # Update args with merged values
    args.source = defaults["source"]
    args.target = defaults["target"]
    args.k = defaults["k"]
    args.k_envelope = defaults["k_envelope"]
    args.output = defaults["output"]
    args.window = defaults["window"]
    args.hop_div = defaults["hop_div"]
    args.fft_mult = defaults["fft_mult"]
    args.demo = defaults["demo"]

    # CLI -k overrides k_envelope from file
    if args.k is not None and args.file and "k_envelope" in defaults and defaults["k_envelope"]:
        # User explicitly set -k on CLI, so ignore k_envelope from file
        if any(arg.startswith('-k') for arg in __import__('sys').argv):
            args.k_envelope = None

    print("Audio Transport Implementation")
    print("Based on Henderson & Solomon (DAFx 2019)")
    print("FULL Optimal Transport Algorithm")
    print("=" * 50)

    if args.demo or (args.source is None and args.target is None):
        print("\nRunning demo with sine waves (A4=440Hz to C#5=554Hz)...")
        audio_X, audio_Y, sr = create_test_signals()

        transport = AudioTransport(
            sample_rate=sr,
            window_ms=args.window,
            hop_divisor=args.hop_div,
            fft_mult=args.fft_mult
        )

        if args.k_envelope is not None:
            print(f"\nProcessing with k_envelope...")
            output = transport.process(audio_X, audio_Y, k_envelope=args.k_envelope)
            filename = "audio_transport_envelope.wav"
            output_normalized = np.int16(output / np.max(np.abs(output)) * 32767)
            wavfile.write(filename, sr, output_normalized)
            print(f"Saved: {filename}")
        else:
            for k in [0.0, 0.25, 0.5, 0.75, 1.0]:
                print(f"\nProcessing with k={k}...")
                output = transport.process(audio_X, audio_Y, k=k)
                filename = f"audio_transport_k{int(k*100):03d}.wav"
                output_normalized = np.int16(output / np.max(np.abs(output)) * 32767)
                wavfile.write(filename, sr, output_normalized)
                print(f"Saved: {filename}")
    else:
        if not args.source or not args.target:
            parser.error("Both source and target files are required (or use --demo)")

        print(f"\nLoading {args.source}...")
        sr_x, audio_X = wavfile.read(args.source)
        print(f"Loading {args.target}...")
        sr_y, audio_Y = wavfile.read(args.target)

        if sr_x != sr_y:
            print(f"Warning: Sample rates differ ({sr_x} vs {sr_y}), using source rate")
        sr = sr_x

        # Convert to float
        if audio_X.dtype == np.int16:
            audio_X = audio_X.astype(np.float64) / 32768
        elif audio_X.dtype == np.int32:
            audio_X = audio_X.astype(np.float64) / 2147483648
        elif audio_X.dtype == np.uint8:
            audio_X = (audio_X.astype(np.float64) - 128) / 128

        if audio_Y.dtype == np.int16:
            audio_Y = audio_Y.astype(np.float64) / 32768
        elif audio_Y.dtype == np.int32:
            audio_Y = audio_Y.astype(np.float64) / 2147483648
        elif audio_Y.dtype == np.uint8:
            audio_Y = (audio_Y.astype(np.float64) - 128) / 128

        # Convert to mono if stereo
        if len(audio_X.shape) > 1:
            audio_X = audio_X.mean(axis=1)
        if len(audio_Y.shape) > 1:
            audio_Y = audio_Y.mean(axis=1)

        transport = AudioTransport(
            sample_rate=sr,
            window_ms=args.window,
            hop_divisor=args.hop_div,
            fft_mult=args.fft_mult
        )

        start = time.time()
        if args.k_envelope is not None:
            print(f"\nProcessing with k_envelope...")
            output = transport.process(audio_X, audio_Y, k_envelope=args.k_envelope)
        else:
            print(f"\nProcessing with k={args.k}...")
            output = transport.process(audio_X, audio_Y, k=args.k)
        elapsed = time.time() - start
        print(f"Processing took {elapsed:.2f}s")

        # Normalize output
        peak = np.max(np.abs(output))
        if peak > 0:
            output = output / peak * 0.95  # Leave some headroom

        # Generate output filename if not specified
        if args.output:
            output_file = args.output
        else:
            output_file = generate_output_filename(
                args.source, args.target, args.k,
                args.window, args.hop_div, args.fft_mult,
                k_envelope=args.k_envelope
            )

        output_int16 = np.int16(output * 32767)
        wavfile.write(output_file, sr, output_int16)
        print(f"\nSaved: {output_file}")

    print("\nDone!")
