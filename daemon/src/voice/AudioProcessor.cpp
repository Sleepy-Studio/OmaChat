#include "voice/AudioProcessor.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#if OMACHAT_HAVE_RNNOISE
#include <rnnoise.h>
#endif

namespace omachat::voice {

struct AudioProcessor::Denoiser {
#if OMACHAT_HAVE_RNNOISE
    DenoiseState* state = rnnoise_create(nullptr);
    ~Denoiser() { rnnoise_destroy(state); }
    float scratch[480];

    // Returns the voice probability of the louder half.
    float process(std::span<float> frame)
    {
        float prob = 0;
        for (std::size_t half = 0; half + 480 <= frame.size(); half += 480) {
            // RNNoise expects 16-bit sample scale.
            for (int i = 0; i < 480; ++i)
                scratch[i] = frame[half + static_cast<std::size_t>(i)] * 32768.0f;
            prob = std::max(prob, rnnoise_process_frame(state, scratch, scratch));
            for (int i = 0; i < 480; ++i)
                frame[half + static_cast<std::size_t>(i)] = scratch[i] / 32768.0f;
        }
        return prob;
    }
#else
    float process(std::span<float>) { return 1.0f; }
#endif
};

float AudioProcessor::Biquad::step(float x)
{
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return static_cast<float>(y);
}

AudioProcessor::AudioProcessor()
{
    // RBJ cookbook high-pass, fc = 80 Hz, Q = 1/sqrt(2), fs = 48 kHz.
    const double fs = 48000.0, fc = 80.0, q = std::numbers::sqrt2 / 2.0;
    const double w0 = 2.0 * std::numbers::pi * fc / fs;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double cw = std::cos(w0);
    const double a0 = 1.0 + alpha;
    m_hpf.b0 = ((1.0 + cw) / 2.0) / a0;
    m_hpf.b1 = (-(1.0 + cw)) / a0;
    m_hpf.b2 = ((1.0 + cw) / 2.0) / a0;
    m_hpf.a1 = (-2.0 * cw) / a0;
    m_hpf.a2 = (1.0 - alpha) / a0;
    configure(m_settings);
}

AudioProcessor::~AudioProcessor() = default;

bool AudioProcessor::noiseSuppressionAvailable()
{
#if OMACHAT_HAVE_RNNOISE
    return true;
#else
    return false;
#endif
}

void AudioProcessor::configure(const Settings& s)
{
    m_settings = s;
    if (s.noiseSuppression && noiseSuppressionAvailable()) {
        if (!m_denoiser)
            m_denoiser = std::make_unique<Denoiser>();
    } else {
        m_denoiser.reset();
    }
}

void AudioProcessor::reset()
{
    m_hpf.z1 = m_hpf.z2 = 0;
    m_hangoverFrames = 0;
    m_agcGain = 1.0;
    if (m_denoiser) {
        m_denoiser.reset();
        m_denoiser = std::make_unique<Denoiser>();
    }
}

AudioProcessor::Result AudioProcessor::process(std::span<float> frame)
{
    Result r;
    const float gain = static_cast<float>(m_settings.inputGain);
    for (float& x : frame) {
        x *= gain;
        if (m_settings.highPass)
            x = m_hpf.step(x);
    }

    float voiceProbability = 1.0f;
    if (m_denoiser)
        voiceProbability = m_denoiser->process(frame);

    double energy = 0;
    for (float x : frame)
        energy += static_cast<double>(x) * x;
    const double rms = std::sqrt(energy / static_cast<double>(std::max<std::size_t>(frame.size(), 1)));

    if (m_settings.automaticGain && rms > 1e-5) {
        // Slow AGC toward -20 dBFS RMS, limited to +/- 20 dB, never boosting
        // during silence so the noise floor is not amplified.
        const double targetRms = 0.1;
        const double desired = std::clamp(targetRms / rms, 0.1, 10.0);
        const bool loud = 20.0 * std::log10(rms) > m_settings.vadThresholdDb;
        if (loud || desired < m_agcGain)
            m_agcGain += (desired - m_agcGain) * 0.05;
        const float g = static_cast<float>(m_agcGain);
        for (float& x : frame)
            x = std::clamp(x * g, -1.0f, 1.0f);
    }

    double post = 0;
    for (float x : frame)
        post += static_cast<double>(x) * x;
    const double postRms = std::sqrt(post / static_cast<double>(std::max<std::size_t>(frame.size(), 1)));
    r.levelDb = postRms > 1e-9 ? static_cast<float>(20.0 * std::log10(postRms)) : -120.0f;

    const bool onset = r.levelDb > m_settings.vadThresholdDb && voiceProbability > 0.25f;
    if (onset)
        m_hangoverFrames = std::max(1, m_settings.vadHangoverMs / 20);
    else if (m_hangoverFrames > 0)
        --m_hangoverFrames;
    r.voice = onset || m_hangoverFrames > 0;
    return r;
}

} // namespace omachat::voice
