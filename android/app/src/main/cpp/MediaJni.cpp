#include "omachat/media/MediaPacket.hpp"
#include <algorithm>
#include <array>
#include <jni.h>
#include <sodium.h>

namespace {
using namespace omachat::media;
struct Secret {
    Key value{};
    ~Secret() { sodium_memzero(value.data(), value.size()); }
};
bool copy(JNIEnv* env, jbyteArray bytes, std::span<std::uint8_t> out)
{
    if (!bytes || env->GetArrayLength(bytes) != static_cast<jsize>(out.size()))
        return false;
    env->GetByteArrayRegion(bytes, 0, static_cast<jsize>(out.size()), reinterpret_cast<jbyte*>(out.data()));
    return !env->ExceptionCheck();
}
jbyteArray result(JNIEnv* env, std::span<const std::uint8_t> bytes)
{
    auto out = env->NewByteArray(static_cast<jsize>(bytes.size()));
    if (out)
        env->SetByteArrayRegion(out, 0, static_cast<jsize>(bytes.size()), reinterpret_cast<const jbyte*>(bytes.data()));
    return out;
}
} // namespace
extern "C" JNIEXPORT jbyteArray JNICALL Java_org_omachat_android_NativeMedia_seal(
    JNIEnv* env, jobject, jbyteArray key, jbyteArray header, jbyteArray payload, jint direction)
{
    using namespace omachat::media;
    Secret secret;
    std::array<std::uint8_t, kMaxDatagramBytes> packet{};
    if (!initializeCrypto() || !copy(env, key, secret.value)
        || !copy(env, header, std::span(packet).first(kHeaderBytes)) || !payload || (direction != 1 && direction != 2))
        return nullptr;
    const auto h = parseHeader(std::span(packet).first(kHeaderBytes + kTagBytes));
    const auto size = env->GetArrayLength(payload);
    if (!h || size < 0 || size > static_cast<jsize>(kMaxPayloadBytes))
        return nullptr;
    std::array<std::uint8_t, kMaxPayloadBytes> plain{};
    env->GetByteArrayRegion(payload, 0, size, reinterpret_cast<jbyte*>(plain.data()));
    if (env->ExceptionCheck())
        return nullptr;
    const auto n = sealInto(*h, static_cast<Direction>(direction), secret.value,
        std::span(plain).first(static_cast<std::size_t>(size)), packet);
    sodium_memzero(plain.data(), plain.size());
    return n ? result(env, std::span(packet).first(n)) : nullptr;
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_org_omachat_android_NativeMedia_open(
    JNIEnv* env, jobject, jbyteArray key, jbyteArray datagram, jint direction)
{
    using namespace omachat::media;
    Secret secret;
    if (!initializeCrypto() || !copy(env, key, secret.value) || !datagram || (direction != 1 && direction != 2))
        return nullptr;
    const auto size = env->GetArrayLength(datagram);
    if (size < static_cast<jsize>(kHeaderBytes + kTagBytes) || size > static_cast<jsize>(kMaxDatagramBytes))
        return nullptr;
    std::array<std::uint8_t, kMaxDatagramBytes> packet{};
    if (!copy(env, datagram, std::span(packet).first(static_cast<std::size_t>(size))))
        return nullptr;
    const auto bytes = std::span(packet).first(static_cast<std::size_t>(size));
    const auto h = parseHeader(bytes);
    if (!h)
        return nullptr;
    std::array<std::uint8_t, kMaxPayloadBytes> plain{};
    const auto n = open(*h, static_cast<Direction>(direction), secret.value, bytes, plain);
    auto out = n ? result(env, std::span(plain).first(*n)) : nullptr;
    sodium_memzero(plain.data(), plain.size());
    return out;
}
