#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace omachat::voice {

// Capture-side DSP for one 20 ms mono frame at 48 kHz. Every stage is
// optional and adds no lookahead latency beyond RNNoise's own 10 ms frame
// processing (performed in-place on two 480-sample halves).
class AudioProcessor {
public:
    struct Settings {
        bool highPass = true; // 2nd-order Butterworth at 80 Hz
        bool noiseSuppression = true; // RNNoise, when compiled in
        bool automaticGain = false;
        double inputGain = 1.0;
        double vadThresholdDb = -50.0;
        int vadHangoverMs = 300;
    };

    struct Result {
        float levelDb = -120.0f; // RMS after processing, dBFS
        bool voice = false; // VAD decision including hangover
    };

    AudioProcessor();
    ~AudioProcessor();

    void configure(const Settings& s);
    const Settings& settings() const { return m_settings; }
    static bool noiseSuppressionAvailable();

    Result process(std::span<float> frame);
    void reset();

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        float step(float x);
    };
    struct Denoiser;

    Settings m_settings;
    Biquad m_hpf;
    std::unique_ptr<Denoiser> m_denoiser;
    int m_hangoverFrames = 0;
    double m_agcGain = 1.0;
};

} // namespace omachat::voice
