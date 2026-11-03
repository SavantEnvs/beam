// Fuzzes the BVM shader (WebAssembly) parser + compiler: bvm2::Processor::Compile.
// First byte selects the shader kind (contract vs. app/manager), the rest is the .wasm module.
#include "harness_common.h"
#include "bvm/bvm2.h"

#include <cstdlib>
#include <new>

// The section parsers size std::vectors from unvalidated 32-bit counts (type, function, export,
// locals, ...), so a few bytes request tens of GB. That is only an allocation-size abort and would hide
// everything behind it: reject such inputs by failing allocations over 64 MiB (no real shader needs
// that) with std::bad_alloc, which the harness already turns into -1. The whole new/delete family is
// replaced together so ASan never sees a new/free mismatch.
namespace {
constexpr size_t kMaxAlloc = size_t(64) << 20;

void* CappedAlloc(size_t n)
{
    if (n > kMaxAlloc)
        throw std::bad_alloc();
    void* p = std::malloc(n ? n : 1);
    if (!p)
        throw std::bad_alloc();
    return p;
}

void* CappedAllocNoThrow(size_t n) noexcept
{
    try {
        return CappedAlloc(n);
    } catch (...) {
        return nullptr;
    }
}
} // namespace

void* operator new(size_t n) { return CappedAlloc(n); }
void* operator new[](size_t n) { return CappedAlloc(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { return CappedAllocNoThrow(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return CappedAllocNoThrow(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    mayhem::InitBeam();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 2)
        return -1;

    const auto kind = (data[0] & 1) ? beam::bvm2::Processor::Kind::Manager : beam::bvm2::Processor::Kind::Contract;
    beam::ByteBuffer res;
    try {
        beam::bvm2::Processor::Compile(res, beam::Blob(data + 1, static_cast<uint32_t>(size - 1)), kind);
    } catch (const std::exception&) {
        return -1;
    }
    return 0;
}
