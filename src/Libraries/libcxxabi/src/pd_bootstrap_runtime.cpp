#include <cstddef>
#include <cstdlib>
#include <cstdint>

extern "C" void
pd_bootstrap_aligned_free(void *ptr)
{
    if (ptr == nullptr) {
        return;
    }

    void **slot = reinterpret_cast<void **>(ptr) - 1;
    std::free(*slot);
}

extern "C" int
posix_memalign(void **memptr, size_t alignment, size_t size)
{
    if (memptr == nullptr || alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0) {
        return 22;
    }

    size_t extra = alignment - 1 + sizeof(void *);
    if (size > static_cast<size_t>(-1) - extra) {
        return 12;
    }

    void *base = std::malloc(size + extra);
    if (base == nullptr) {
        return 12;
    }

    uintptr_t start = reinterpret_cast<uintptr_t>(base) + sizeof(void *);
    uintptr_t aligned = (start + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
    reinterpret_cast<void **>(aligned)[-1] = base;
    *memptr = reinterpret_cast<void *>(aligned);
    return 0;
}

namespace libunwind {

bool
checkKeyMgrRegisteredFDEs(unsigned long, void *&)
{
    return false;
}

}
