// libFuzzer harness: clang with -DOMACHAT_ENABLE_FUZZERS=ON, then
//   ./build/tests/fuzz_e2e_payload -max_total_time=300
#include "FuzzTargets.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    omachat::fuzz::e2ePayload(data, size);
    return 0;
}
