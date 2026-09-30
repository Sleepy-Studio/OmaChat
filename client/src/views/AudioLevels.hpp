#pragma once

#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <array>

namespace omachat::client {

// Recent decoded audio levels for the attachment player. No timer runs until
// playback delivers a buffer; silence then drains the bars and stops it.
class AudioLevels : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(AudioLevels)
    Q_PROPERTY(QAudioBufferOutput* output READ output CONSTANT)
    Q_PROPERTY(QVariantList bars READ bars NOTIFY barsChanged)

public:
    explicit AudioLevels(QObject* parent = nullptr);

    QAudioBufferOutput* output() { return &m_output; }
    QVariantList bars() const;
    Q_INVOKABLE void reset();

    static qreal amplitude(const QAudioBuffer& buffer);

signals:
    void barsChanged();

private:
    void consume(const QAudioBuffer& buffer);
    void advance();

    QAudioBufferOutput m_output;
    QTimer m_timer;
    std::array<qreal, 32> m_bars{};
    qreal m_pending = 0;
    bool m_hasPending = false;
};

} // namespace omachat::client
