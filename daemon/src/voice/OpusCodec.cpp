#include "voice/OpusCodec.hpp"

#include <opus/opus.h>

#include <algorithm>

namespace omachat::voice {

OpusVoiceEncoder::OpusVoiceEncoder()
{
    int err = 0;
    m_enc = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK) {
        m_enc = nullptr;
        return;
    }
    opus_encoder_ctl(m_enc, OPUS_SET_BITRATE(40000));
    opus_encoder_ctl(m_enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(m_enc, OPUS_SET_COMPLEXITY(8));
    opus_encoder_ctl(m_enc, OPUS_SET_VBR(1));
    opus_encoder_ctl(m_enc, OPUS_SET_DTX(0));
    opus_encoder_ctl(m_enc, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(m_enc, OPUS_SET_PACKET_LOSS_PERC(5));
}

OpusVoiceEncoder::~OpusVoiceEncoder()
{
    if (m_enc)
        opus_encoder_destroy(m_enc);
}

void OpusVoiceEncoder::setBitrate(int bps)
{
    if (m_enc)
        opus_encoder_ctl(m_enc, OPUS_SET_BITRATE(std::clamp(bps, 6000, 128000)));
}

void OpusVoiceEncoder::setFec(bool enabled, int expectedLossPercent)
{
    if (!m_enc)
        return;
    opus_encoder_ctl(m_enc, OPUS_SET_INBAND_FEC(enabled ? 1 : 0));
    opus_encoder_ctl(m_enc, OPUS_SET_PACKET_LOSS_PERC(std::clamp(expectedLossPercent, 0, 100)));
}

std::size_t OpusVoiceEncoder::encode(std::span<const float> pcm, std::span<std::uint8_t> out)
{
    if (!m_enc || pcm.size() < static_cast<std::size_t>(kFrameSamples))
        return 0;
    const int n = opus_encode_float(m_enc, pcm.data(), kFrameSamples, out.data(),
        static_cast<opus_int32>(std::min<std::size_t>(out.size(), kMaxPacketBytes)));
    return n > 0 ? static_cast<std::size_t>(n) : 0;
}

OpusVoiceDecoder::OpusVoiceDecoder()
{
    int err = 0;
    m_dec = opus_decoder_create(kSampleRate, kChannels, &err);
    if (err != OPUS_OK)
        m_dec = nullptr;
}

OpusVoiceDecoder::~OpusVoiceDecoder()
{
    if (m_dec)
        opus_decoder_destroy(m_dec);
}

int OpusVoiceDecoder::decode(std::span<const std::uint8_t> packet, std::span<float> out)
{
    if (!m_dec || out.size() < static_cast<std::size_t>(kFrameSamples))
        return 0;
    const int n
        = opus_decode_float(m_dec, packet.data(), static_cast<opus_int32>(packet.size()), out.data(), kFrameSamples, 0);
    return std::max(n, 0);
}

int OpusVoiceDecoder::decodeFec(std::span<const std::uint8_t> nextPacket, std::span<float> out)
{
    if (!m_dec || out.size() < static_cast<std::size_t>(kFrameSamples))
        return 0;
    const int n = opus_decode_float(
        m_dec, nextPacket.data(), static_cast<opus_int32>(nextPacket.size()), out.data(), kFrameSamples, 1);
    return std::max(n, 0);
}

int OpusVoiceDecoder::conceal(std::span<float> out)
{
    if (!m_dec || out.size() < static_cast<std::size_t>(kFrameSamples))
        return 0;
    const int n = opus_decode_float(m_dec, nullptr, 0, out.data(), kFrameSamples, 0);
    return std::max(n, 0);
}

} // namespace omachat::voice
