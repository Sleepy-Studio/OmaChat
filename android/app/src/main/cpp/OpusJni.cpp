#include "voice/OpusCodec.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <jni.h>
#include <limits>
#include <map>
#include <mutex>
#include <opus.h>

namespace {
using namespace omachat::voice;
// IDs, never pointers, cross JNI. Serialized lookup/use/destruction prevents UAF,
// including calls racing close, stale handles and hostile JNI arguments.
std::mutex mutex;
struct Codec {
    std::unique_ptr<OpusVoiceEncoder> encoder;
    std::unique_ptr<OpusVoiceDecoder> decoder;
};
std::map<jlong, Codec> codecs;
jlong nextId = 1;
} // namespace
extern "C" JNIEXPORT jlong JNICALL Java_org_omachat_android_NativeOpus_create(JNIEnv*, jobject, jboolean encode)
{
    std::lock_guard guard(mutex);
    if (codecs.size() >= 256 || nextId == std::numeric_limits<jlong>::max())
        return 0;
    Codec codec;
    if (encode) {
        codec.encoder = std::make_unique<OpusVoiceEncoder>();
        if (!codec.encoder->valid())
            return 0;
    } else {
        codec.decoder = std::make_unique<OpusVoiceDecoder>();
        if (!codec.decoder->valid())
            return 0;
    }
    const auto id = nextId++;
    codecs.emplace(id, std::move(codec));
    return id;
}
extern "C" JNIEXPORT void JNICALL Java_org_omachat_android_NativeOpus_destroy(JNIEnv*, jobject, jlong id)
{
    std::lock_guard guard(mutex);
    codecs.erase(id);
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_org_omachat_android_NativeOpus_encode(
    JNIEnv* env, jobject, jlong id, jfloatArray samples)
{
    if (!samples || env->GetArrayLength(samples) != kFrameSamples)
        return nullptr;
    std::array<float, kFrameSamples> pcm{};
    env->GetFloatArrayRegion(samples, 0, kFrameSamples, pcm.data());
    if (env->ExceptionCheck())
        return nullptr;
    for (auto sample : pcm)
        if (!std::isfinite(sample) || std::abs(sample) > 1.0F)
            return nullptr;
    std::lock_guard guard(mutex);
    auto it = codecs.find(id);
    if (it == codecs.end() || !it->second.encoder)
        return nullptr;
    std::array<std::uint8_t, kMaxPacketBytes> packet{};
    const auto n = it->second.encoder->encode(pcm, packet);
    if (!n)
        return nullptr;
    auto out = env->NewByteArray(static_cast<jsize>(n));
    if (out)
        env->SetByteArrayRegion(out, 0, static_cast<jsize>(n), reinterpret_cast<const jbyte*>(packet.data()));
    return out;
}
extern "C" JNIEXPORT jfloatArray JNICALL Java_org_omachat_android_NativeOpus_decode(
    JNIEnv* env, jobject, jlong id, jbyteArray bytes)
{
    std::array<std::uint8_t, kMaxPacketBytes> packet{};
    jsize size = 0;
    if (bytes) {
        size = env->GetArrayLength(bytes);
        if (size < 1 || size > kMaxPacketBytes)
            return nullptr;
        env->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte*>(packet.data()));
        if (env->ExceptionCheck())
            return nullptr;
        // Only one desktop-compatible 20 ms frame. Reject oversized duration
        // before altering decoder state; null deliberately means PLC.
        if (opus_packet_get_nb_samples(packet.data(), size, kSampleRate) != kFrameSamples)
            return nullptr;
    }
    std::lock_guard guard(mutex);
    auto it = codecs.find(id);
    if (it == codecs.end() || !it->second.decoder)
        return nullptr;
    std::array<float, kFrameSamples> pcm{};
    const auto n
        = bytes ? it->second.decoder->decode(std::span(packet).first(size), pcm) : it->second.decoder->conceal(pcm);
    if (n != kFrameSamples)
        return nullptr;
    for (auto& sample : pcm)
        sample = std::isfinite(sample) ? std::clamp(sample, -1.0F, 1.0F) : 0.0F;
    auto out = env->NewFloatArray(kFrameSamples);
    if (out)
        env->SetFloatArrayRegion(out, 0, kFrameSamples, pcm.data());
    return out;
}
