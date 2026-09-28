#pragma once

#include <cstddef>
#include <cstdint>

// Entry points shared by the libFuzzer harnesses and the always-on smoke
// test. Each must accept arbitrary bytes without crashing or leaking.
namespace omachat::fuzz {

void framing(const std::uint8_t* data, std::size_t size);
void mediaHeader(const std::uint8_t* data, std::size_t size);
void envelope(const std::uint8_t* data, std::size_t size);
void ipcLine(const std::uint8_t* data, std::size_t size);
// A sequence of video fragments: [flags:1][len:2][payload:len]...
void videoFragments(const std::uint8_t* data, std::size_t size);
// An end-to-end payload as another user's client could send it.
void e2ePayload(const std::uint8_t* data, std::size_t size);

} // namespace omachat::fuzz
