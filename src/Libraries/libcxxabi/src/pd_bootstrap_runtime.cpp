#include <cstddef>
#include <cstdlib>
#include <cstdint>

/* POSIX requires that memory from posix_memalign() can be released with
 * plain free(). The previous shim returned an interior aligned pointer with
 * the malloc base stashed one slot before it - fine for the paired
 * pd_bootstrap_aligned_free(), but libcxxabi's exception allocator
 * (__aligned_free_with_fallback) and any other standard caller release with
 * free(), which then aborts with "pointer being freed was not allocated".
 * That was the SIGILL on every caught dyld error path (e.g. any failed
 * dlopen: dyld2's dlopen_internal catch block ends the catch, the exception
 * object's refcount hits zero, and the interior pointer hit free()).
 *
 * malloc() in both contexts this shim links into (dyld's pool allocator and
 * libmalloc via the LibSystemHelpers forwarding) returns 16-byte-aligned
 * memory, so alignments up to 16 - which covers alignof(max_align_t), i.e.
 * everything libcxxabi's exception machinery asks for - can be satisfied
 * with plain malloc(). Larger alignments cannot be provided free()-compatibly
 * on top of plain malloc(), so fail them cleanly (callers fall back:
 * libcxxabi's fallback pool for exceptions, bad_alloc for aligned new). */
extern "C" void
pd_bootstrap_aligned_free(void *ptr)
{
    std::free(ptr);
}

extern "C" int
posix_memalign(void **memptr, size_t alignment, size_t size)
{
    if (memptr == nullptr || alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0) {
        return 22; /* EINVAL */
    }

    if (alignment > 16) {
        return 12; /* ENOMEM - cannot honor free() contract above malloc alignment */
    }

    void *base = std::malloc(size);
    if (base == nullptr) {
        return 12;
    }

    *memptr = base;
    return 0;
}

namespace libunwind {

bool
checkKeyMgrRegisteredFDEs(unsigned long, void *&)
{
    return false;
}

}
