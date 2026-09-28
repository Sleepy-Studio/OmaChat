// libFuzzer harness: clang with -DOMACHAT_ENABLE_FUZZERS=ON, then
//   ./build/tests/fuzz_video_fragments -max_total_time=300
#include "FuzzTargets.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    omachat::fuzz::videoFragments(data, size);
    return 0;
}
