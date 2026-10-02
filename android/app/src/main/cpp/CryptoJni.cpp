#include "omachat/crypto/FileCrypto.hpp"
#include "omachat/crypto/MessageCrypto.hpp"
#include <jni.h>
#include <sodium.h>
#include <stdexcept>
using namespace omachat::portable;
namespace {
std::string bytes(JNIEnv* env, jbyteArray value, std::size_t limit = kMaxBody + 16)
{
    if (!value || static_cast<std::size_t>(env->GetArrayLength(value)) > limit)
        throw std::runtime_error("invalid crypto input");
    std::string out(env->GetArrayLength(value), '\0');
    env->GetByteArrayRegion(value, 0, static_cast<jsize>(out.size()), reinterpret_cast<jbyte*>(out.data()));
    return out;
}
jbyteArray array(JNIEnv* env, const std::string& value)
{
    auto out = env->NewByteArray(static_cast<jsize>(value.size()));
    if (out)
        env->SetByteArrayRegion(out, 0, static_cast<jsize>(value.size()), reinterpret_cast<const jbyte*>(value.data()));
    return out;
}
std::vector<std::string> arrays(JNIEnv* env, jobjectArray values, std::size_t max, bool payload = false)
{
    if (!values || static_cast<std::size_t>(env->GetArrayLength(values)) > max)
        throw std::runtime_error("too many devices");
    std::vector<std::string> out;
    std::size_t total = 0;
    for (jsize i = 0; i < env->GetArrayLength(values); ++i) {
        auto value = static_cast<jbyteArray>(env->GetObjectArrayElement(values, i));
        auto v = bytes(env, value, payload && i == 2 ? kMaxBody + 16 : (payload ? 80 : 32));
        env->DeleteLocalRef(value);
        total += v.size();
        if (total > kMaxBody + 32768)
            throw std::runtime_error("crypto input too large");
        out.push_back(std::move(v));
    }
    return out;
}
std::optional<Identity> identity(JNIEnv* env, jbyteArray value)
{
    auto secret = bytes(env, value, 32);
    auto id = Identity::fromSecret(secret);
    sodium_memzero(secret.data(), secret.size());
    return id;
}
void fail(JNIEnv* env)
{
    env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), "Invalid crypto input");
}
} // namespace
extern "C" JNIEXPORT jbyteArray JNICALL Java_org_omachat_android_NativeCrypto_generate(JNIEnv* env, jobject)
{
    try {
        auto id = Identity::generate();
        return array(env, std::string(reinterpret_cast<char*>(id.secretKey.data()), 32));
    } catch (...) {
        fail(env);
        return nullptr;
    }
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_org_omachat_android_NativeCrypto_publicKey(
    JNIEnv* env, jobject, jbyteArray secret)
{
    try {
        auto id = identity(env, secret);
        if (!id)
            return nullptr;
        return array(env, id->publicBytes());
    } catch (...) {
        fail(env);
        return nullptr;
    }
}
extern "C" JNIEXPORT jobjectArray JNICALL Java_org_omachat_android_NativeCrypto_seal(
    JNIEnv* env, jobject, jbyteArray body, jlong channel, jlong author, jbyteArray secret, jobjectArray recipients)
{
    try {
        auto id = identity(env, secret);
        if (!id)
            return nullptr;
        auto plain = bytes(env, body, kMaxBody);
        auto p = seal(plain, {static_cast<std::uint64_t>(channel), static_cast<std::uint64_t>(author)}, *id,
            arrays(env, recipients, kMaxDevices));
        sodium_memzero(plain.data(), plain.size());
        if (!p)
            return nullptr;
        auto out = env->NewObjectArray(static_cast<jsize>(3 + 3 * p->wraps.size()), env->FindClass("[B"), nullptr);
        if (!out)
            return nullptr;
        jsize i = 0;
        auto put = [&](const std::string& v) {
            auto a = array(env, v);
            env->SetObjectArrayElement(out, i++, a);
            env->DeleteLocalRef(a);
        };
        put(p->sender);
        put(p->nonce);
        put(p->ciphertext);
        for (const auto& w : p->wraps) {
            put(w.recipient);
            put(w.nonce);
            put(w.box);
        }
        return out;
    } catch (...) {
        fail(env);
        return nullptr;
    }
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_org_omachat_android_NativeCrypto_open(
    JNIEnv* env, jobject, jobjectArray values, jlong channel, jlong author, jbyteArray secret)
{
    try {
        auto id = identity(env, secret);
        if (!id)
            return nullptr;
        auto v = arrays(env, values, 3 + 3 * kMaxDevices, true);
        if (v.size() < 6 || v.size() % 3)
            return nullptr;
        Payload p{v[0], v[1], v[2], {}};
        for (std::size_t i = 3; i < v.size(); i += 3)
            p.wraps.push_back({v[i], v[i + 1], v[i + 2]});
        auto plain = open(p, {static_cast<std::uint64_t>(channel), static_cast<std::uint64_t>(author)}, *id);
        if (!plain)
            return nullptr;
        auto out = array(env, *plain);
        sodium_memzero(plain->data(), plain->size());
        return out;
    } catch (...) {
        fail(env);
        return nullptr;
    }
}

extern "C" JNIEXPORT jstring JNICALL Java_org_omachat_android_NativeCrypto_safety(
    JNIEnv* env, jobject, jlong userA, jobjectArray keysA, jlong userB, jobjectArray keysB)
{
    try {
        const auto value = safetyNumber(static_cast<std::uint64_t>(userA), arrays(env, keysA, kMaxDevices),
            static_cast<std::uint64_t>(userB), arrays(env, keysB, kMaxDevices));
        return env->NewStringUTF(value.c_str());
    } catch (...) {
        fail(env);
        return nullptr;
    }
}

namespace {
std::string path(JNIEnv* env, jstring value)
{
    if (!value || env->GetStringUTFLength(value) > 4096)
        throw std::runtime_error("invalid path");
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (!chars)
        throw std::runtime_error("invalid path");
    std::string out(chars);
    env->ReleaseStringUTFChars(value, chars);
    return out;
}
} // namespace
extern "C" JNIEXPORT jbyteArray JNICALL Java_org_omachat_android_NativeCrypto_encryptFile(
    JNIEnv* env, jobject, jstring input, jstring output)
{
    try {
        auto key = encryptFile(path(env, input), path(env, output), 100 * 1024 * 1024);
        if (!key)
            return nullptr;
        auto out = array(env, *key);
        sodium_memzero(key->data(), key->size());
        return out;
    } catch (...) {
        fail(env);
        return nullptr;
    }
}
extern "C" JNIEXPORT jboolean JNICALL Java_org_omachat_android_NativeCrypto_decryptFile(
    JNIEnv* env, jobject, jstring input, jstring output, jbyteArray key)
{
    try {
        auto secret = bytes(env, key, 32);
        const bool ok = decryptFile(path(env, input), path(env, output), secret, 100 * 1024 * 1024);
        sodium_memzero(secret.data(), secret.size());
        return ok;
    } catch (...) {
        fail(env);
        return false;
    }
}
