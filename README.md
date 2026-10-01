# Audio Transport

> This is a fork of [sportdeath/audio_transport](https://github.com/sportdeath/audio_transport). It adds a JSON parameter file mode to `transport` (`-f params.json`), configurable window/hop/FFT sizes, fixes for low-frequency rumble and a first-window click, a Python implementation, and a real-time VST3 plugin.

This code implements a novel effect for transitioning between audio signals that we call "audio transport". As an interpolation parameter is changed, the pitches in one signal slide to the pitches in the other, producing a portamento, or musical glide. The assignment of pitches in one sound to pitches in the other is accomplished by solving a 1-dimensional optimal transport problem.

The effect is described in [this paper](https://arxiv.org/abs/1906.06763) which won "Best Student Paper" at the [22nd International Conference on Digital Audio Effects](http://dafx2019.bcu.ac.uk/). Audio examples can be found [here](https://soundcloud.com/audio_transport), or click on the image below for a video demonstration:

[![video](https://live.staticflickr.com/65535/49050087898_a81680c7cb_o_d.png)](https://www.youtube.com/watch?v=LXqZMKXSPJo)

This code provides the *static* effect, meaning it provides an executable that takes two audio files as input and combines the two using the effect to produce and output audio file. The code demonstrated in the video that uses the effect *live* uses the functions provided by this library on [portaudio](http://www.portaudio.com/) streams. The live code is super hacky and you could probably make something much better, which is why I haven't included it here. But if you really want it, I can send it to you. All of the important stuff is in this library.

## Usage

### Static Use

Using the static effect does not require writing any more code but it does require several dependencies. You will need [```fftw3```](http://fftw.org/), [```ffmpeg```](https://ffmpeg.org/) and [```audiorw```](https://github.com/sportdeath/audiorw).

Install ```fftw3``` and ```ffmpeg``` via your system package manager. This code has been tested on versions 3.3.8 and 4.2.3 respectively on a machine running Arch Linux. Hopefully things don't break on your system but if they do, please make a pull request :-) !

 Then install the ```audiorw``` library with ```cmake```:

    git clone https://github.com/sportdeath/audiorw
    mkdir audiorw/build
    cd audiorw/build
    cmake ..
    make
    sudo make install

Once the dependencies are installed, install ```audio_transport``` with ```cmake```:

    git clone https://github.com/RichardJohnn/audio_transport
    mkdir audio_transport/build
    cd audio_transport/build
    cmake .. -D BUILD_EXAMPLES=ON
    make

#### macOS (Homebrew)

`audiorw` only builds against the ffmpeg 4.x API, which Homebrew ships as the keg-only `ffmpeg@4`. Install the dependencies:

    brew install fftw ffmpeg ffmpeg@4

then run the build script from the repository root:

    ./rebuild.sh

It builds and installs `audiorw` against `ffmpeg@4` if it isn't already installed (this step uses `sudo`), links the `ffmpeg@4` headers into `/usr/local/include`, and builds the library and examples into `build/`. To configure by hand instead, point CMake at Homebrew's `fftw` so it doesn't pick up the newer ffmpeg headers in `/opt/homebrew/include`:

    mkdir build && cd build
    cmake .. -D BUILD_EXAMPLES=ON -D CMAKE_PREFIX_PATH="$(brew --prefix fftw);/usr/local"
    make

After changing code, rebuild just the binary you need with `make transport` inside `build/`.

#### Running

Then, to apply the effect use the ```transport``` binary. For example, below we tranform piano audio into guitar audio. The output starts by sounding like the piano audio for the first 20% of the duration. Then between 20% and 70% of the duration, the piano is transformed into a guitar. In the last 30% the audio is simply the guitar.

    ./transport piano.wav guitar.mp3 20 70 out.flac

For more control, pass a JSON parameter file with `-f`. It uses the same keys as the Python script `audio_transport.py`, so one file works with both:

    ./transport -f params.json

    {
      "source": "piano.wav",          # required (k = 0)
      "target": "guitar.mp3",         # required (k = 1)
      "k": 0.5,                       # constant interpolation, default 0.5
      "k_envelope": [                 # time-varying k, overrides "k"
        {"percent": 20, "k": 0},
        {"percent": 70, "k": 1}
      ],
      "window": 50,                   # window size in ms, default 50
      "hop_div": 2,                   # hop = window/hop_div (>= 2), default 2
      "fft_mult": 4,                  # FFT size = nextpow2(window)*fft_mult, default 4
      "output": "out.wav"             # optional
    }

- `k_envelope` keyframes are sorted by `percent` and interpolated linearly. A `{"percent": 0, "k": 0}` keyframe is added if none starts at 0, and the last `k` holds to the end. An empty array means a full crossfade from 0 to 1.
- Without `output`, the file is named after the inputs and parameters, *e.g.* `piano_guitar_env00-10_w50_hd2_fftm4.wav`, written to the current directory, with `_2`, `_3`, ... added rather than overwriting.
- The shorter input is padded with silence, channels are processed in parallel, and the output is normalized to a peak of 0.95.
- Larger `window`, `hop_div` and `fft_mult` give finer frequency resolution and smoother glides but take longer. Each window is processed on its own, so even very large FFTs (*e.g.* `"window": 500, "fft_mult": 128`) fit in memory.

Running `./transport` with no arguments prints this schema.

You can also apply the effect to a single file with the ```glide``` binary. The input file serves as one input to the "transport" and the output of the effect is fed back into the second input. This slurs all of the frequencies in the input like the glide/lag/portamento knob found on some synthesizers ... however it works on any audio input.

In this example we apply the glide effect to a piano with a time constant of 1 millisecond:

    ./glide piano.wav 1 piano_glide.ogg

### Python Version

`audio_transport.py` is a standalone Python implementation of a simpler variant of the effect (CDF-based transport of magnitude spectra, without reassignment or equal-loudness weighting). It needs `numpy` and `scipy`:

    python3 -m venv venv
    venv/bin/pip install -r requirements.txt
    venv/bin/python audio_transport.py -f params.json

Run it with `--help` for the full set of options. A real-time C++ port of this variant used by the VST3 plugin is described in [README_REALTIME.md](README_REALTIME.md) and [VST3_PLUGIN_SUMMARY.md](VST3_PLUGIN_SUMMARY.md).

### Tests

The tests only need `fftw3`:

    mkdir build && cd build
    cmake .. -D BUILD_TESTS=ON
    make
    ./test_stft_params && ./test_transport_params && ./test_equal_loudness && ./test_edge_cases

### External Use

If you want to use the audio transport functions provided by this library in another project (*e.g.* to make a live effect) then this library only requires [```fftw3```](http://fftw.org/). Once you have it, install ```audio_transport``` with ```cmake```:

    git clone https://github.com/RichardJohnn/audio_transport
    mkdir audio_transport/build
    cd audio_transport/build
    cmake ..
    sudo make install

Then in your own project, include these headers:

    #include <audio_transport/spectral.hpp>
    #include <audio_transport/audio_transport.hpp>
    #include <audio_transport/equal_loudness.hpp>

```spectral.hpp``` provides functions that turn vectors of audio into spectral objects, incapsulating time and frequency as well as their [reassigned counterparts](https://en.wikipedia.org/wiki/Reassignment_method) which are necessary for the effect. It also provides the inverse.

```audio_tranport.hpp``` provides an ```interpolate``` function that takes windows of audio (that are in the ```spectral``` format) and combines them according the effect.

```equal_loudness.hpp``` applies and removes A-weighting so the interpolation is perceptually uniform. Apply it to both inputs before interpolating and remove it from the result.

The STFT geometry can be set with ```spectral::stft_params```, either ```stft_params::legacy(sample_rate, window_seconds, padding, overlap)``` or ```stft_params::from_python(sample_rate, window_ms, hop_div, fft_mult)```, passed to ```analysis```/```synthesis```. When the hop isn't half the window, use ```interpolate_hop``` with the hop in seconds, and start its phase buffer with ```initial_phases``` so the first window doesn't click. A typical loop:

    using namespace audio_transport;
    spectral::stft_params p = spectral::stft_params::from_python(sample_rate, 100, 4, 2);
    double hop_seconds = p.hop/sample_rate;
    auto left = spectral::analysis(audio_left, sample_rate, p);
    auto right = spectral::analysis(audio_right, sample_rate, p);
    equal_loudness::apply(left);
    equal_loudness::apply(right);

    std::vector<double> phases = initial_phases(left[0], hop_seconds);
    std::vector<std::vector<spectral::point>> out(left.size());
    for (size_t w = 0; w < left.size(); w++) {
      out[w] = interpolate_hop(left[w], right[w], phases, hop_seconds, k);
    }
    equal_loudness::remove(out);
    std::vector<double> audio_out = spectral::synthesis(out, p);

To process a window at a time (for long inputs, very large FFTs or live use), ```spectral::analyzer``` and ```spectral::synthesizer``` create their FFTW plans once and handle one window per call; overlap-add each synthesized window ```p.hop``` samples apart. FFTW planning isn't thread-safe, so construct them on one thread; separate instances can then run concurrently.

FFTW plans are chosen with ```FFTW_MEASURE```, so results can differ in the last bits between runs. Compile the library with ```-DAUDIO_TRANSPORT_FFTW_FLAGS=FFTW_ESTIMATE``` for reproducible output.
