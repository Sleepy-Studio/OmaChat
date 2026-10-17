#include "FileCrypto.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <sodium.h>
namespace omachat::portable {
namespace {
constexpr std::size_t chunkSize = 65536;
struct Output {
    std::string path;
    std::ofstream stream;
    bool complete = false;
    explicit Output(const std::string& p)
        : path(p)
        , stream(p, std::ios::binary | std::ios::trunc)
    {
    }
    ~Output()
    {
        stream.close();
        if (!complete) {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    }
    bool finish()
    {
        stream.flush();
        if (!stream)
            return false;
        stream.close();
        complete = !stream.fail();
        return complete;
    }
};
struct State {
    crypto_secretstream_xchacha20poly1305_state value{};
    ~State() { sodium_memzero(&value, sizeof value); }
};
} // namespace
std::optional<std::string> encryptFile(const std::string& input, const std::string& output, std::uint64_t limit)
{
    if (sodium_init() < 0 || input == output || limit > kMaxFileBytes)
        return std::nullopt;
    std::ifstream in(input, std::ios::binary | std::ios::ate);
    if (!in)
        return std::nullopt;
    const auto size = in.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > limit)
        return std::nullopt;
    in.seekg(0);
    Output out(output);
    if (!out.stream)
        return std::nullopt;
    std::string key(32, '\0');
    crypto_secretstream_xchacha20poly1305_keygen(reinterpret_cast<unsigned char*>(key.data()));
    State st;
    std::array<unsigned char, 24> header{};
    crypto_secretstream_xchacha20poly1305_init_push(
        &st.value, header.data(), reinterpret_cast<const unsigned char*>(key.data()));
    out.stream.write(reinterpret_cast<char*>(header.data()), header.size());
    std::array<unsigned char, chunkSize> plain{};
    std::array<unsigned char, chunkSize + 17> cipher{};
    std::uint64_t done = 0;
    do {
        in.read(reinterpret_cast<char*>(plain.data()), plain.size());
        const auto n = in.gcount();
        done += static_cast<std::uint64_t>(n);
        if (in.bad() || done > static_cast<std::uint64_t>(size) || (n == 0 && done < static_cast<std::uint64_t>(size)))
            break;
        const bool last = done == static_cast<std::uint64_t>(size);
        unsigned long long len = 0;
        crypto_secretstream_xchacha20poly1305_push(&st.value, cipher.data(), &len, plain.data(), n, nullptr, 0,
            last ? crypto_secretstream_xchacha20poly1305_TAG_FINAL : crypto_secretstream_xchacha20poly1305_TAG_MESSAGE);
        sodium_memzero(plain.data(), plain.size());
        out.stream.write(reinterpret_cast<char*>(cipher.data()), static_cast<std::streamsize>(len));
        if (!out.stream)
            break;
        if (last && in.peek() == std::char_traits<char>::eof() && out.finish())
            return key;
        if (last)
            break;
    } while (true);
    sodium_memzero(plain.data(), plain.size());
    sodium_memzero(key.data(), key.size());
    return std::nullopt;
}
bool decryptFile(const std::string& input, const std::string& output, const std::string& key, std::uint64_t limit)
{
    if (sodium_init() < 0 || input == output || limit > kMaxFileBytes || key.size() != 32)
        return false;
    std::ifstream in(input, std::ios::binary | std::ios::ate);
    if (!in)
        return false;
    const auto size = in.tellg();
    if (size < 41 || static_cast<std::uint64_t>(size) > limit + 24 + (limit / chunkSize + 1) * 17)
        return false;
    in.seekg(0);
    State st;
    std::array<unsigned char, 24> header{};
    in.read(reinterpret_cast<char*>(header.data()), header.size());
    if (!in
        || crypto_secretstream_xchacha20poly1305_init_pull(
               &st.value, header.data(), reinterpret_cast<const unsigned char*>(key.data()))
            != 0)
        return false;
    Output out(output);
    if (!out.stream)
        return false;
    std::array<unsigned char, chunkSize + 17> cipher{};
    std::array<unsigned char, chunkSize> plain{};
    std::uint64_t total = 0;
    for (;;) {
        in.read(reinterpret_cast<char*>(cipher.data()), cipher.size());
        const auto n = in.gcount();
        unsigned long long len = 0;
        unsigned char tag = 0;
        if (in.bad() || n < 17
            || crypto_secretstream_xchacha20poly1305_pull(
                   &st.value, plain.data(), &len, &tag, cipher.data(), n, nullptr, 0)
                != 0)
            break;
        total += len;
        if (total > limit)
            break;
        out.stream.write(reinterpret_cast<char*>(plain.data()), static_cast<std::streamsize>(len));
        sodium_memzero(plain.data(), plain.size());
        if (!out.stream)
            break;
        if (tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL) {
            if (in.peek() != std::char_traits<char>::eof())
                break;
            return out.finish();
        }
        if (tag != crypto_secretstream_xchacha20poly1305_TAG_MESSAGE
            || n != static_cast<std::streamsize>(cipher.size()))
            break;
    }
    sodium_memzero(plain.data(), plain.size());
    return false;
}
} // namespace omachat::portable
