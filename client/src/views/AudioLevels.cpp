#include "views/AudioLevels.hpp"

#include <QAudioFormat>

#include <algorithm>
#include <cmath>

namespace omachat::client {

AudioLevels::AudioLevels(QObject* parent)
    : QObject(parent)
    , m_output(this)
{
    m_timer.setInterval(40);
    connect(&m_timer, &QTimer::timeout, this, &AudioLevels::advance);
    connect(&m_output, &QAudioBufferOutput::audioBufferReceived, this, &AudioLevels::consume);
}

QVariantList AudioLevels::bars() const
{
    QVariantList out;
    out.reserve(m_bars.size());
    for (qreal level : m_bars)
        out.append(level);
    return out;
}

void AudioLevels::reset()
{
    m_timer.stop();
    m_pending = 0;
    m_hasPending = false;
    m_bars.fill(0);
    emit barsChanged();
}

qreal AudioLevels::amplitude(const QAudioBuffer& buffer)
{
    if (!buffer.isValid() || buffer.sampleCount() == 0)
        return 0;
    const QAudioFormat format = buffer.format();
    const auto* data = buffer.constData<std::byte>();
    const int sampleBytes = format.bytesPerSample();
    if (!data || sampleBytes == 0)
        return 0;

    // Sample a bounded number of points even for a long decoder buffer.
    const qsizetype stride = std::max<qsizetype>(1, buffer.sampleCount() / 4096);
    qreal sum = 0;
    qsizetype count = 0;
    for (qsizetype i = 0; i < buffer.sampleCount(); i += stride) {
        const qreal sample = format.normalizedSampleValue(data + i * sampleBytes);
        sum += sample * sample;
        ++count;
    }
    return std::sqrt(sum / count);
}

void AudioLevels::consume(const QAudioBuffer& buffer)
{
    m_pending = std::max(m_pending, std::clamp(amplitude(buffer) * 2.5, qreal(0), qreal(1)));
    m_hasPending = true;
    if (!m_timer.isActive())
        m_timer.start();
}

void AudioLevels::advance()
{
    std::rotate(m_bars.begin(), m_bars.begin() + 1, m_bars.end());
    m_bars.back() = m_hasPending ? m_pending : 0;
    m_pending = 0;
    m_hasPending = false;
    emit barsChanged();
    if (std::all_of(m_bars.begin(), m_bars.end(), [](qreal level) { return level == 0; }))
        m_timer.stop();
}

} // namespace omachat::client
