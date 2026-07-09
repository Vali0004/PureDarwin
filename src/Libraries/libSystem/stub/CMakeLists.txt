# libSystem_B_stub: a placeholder /usr/lib/libSystem.B.dylib.
#
# NOT a real libSystem -- PD does not yet build one (the libsystem_kernel/libc/
# pthread/libcxxabi/libunwind pieces built elsewhere in this tree are static
# archives linked INTO dyld itself, not a standalone runtime dylib product).
#
# ld64 requires every dynamic MH_EXECUTE to carry an LC_LOAD_DYLIB on
# libSystem.dylib (a hardcoded policy check, not a symbol-resolution one), so
# any PD userspace binary that links dynamically -- e.g. launchd-dummy, which
# calls no libSystem function at all, just raw syscalls -- ends up with a real
# runtime dependency on a file that has to actually exist and be loadable for
# dyld's LC_LOAD_DYLIB resolution to succeed, even though nothing in the binary
# calls into it. This target is exactly enough to satisfy that: an empty
# dylib with the install name and version dyld's dependent-library check
# compares against. Delete this once PD has a real libSystem.dylib.
add_darwin_shared_library(libSystem_B_stub
    MACOSX_VERSION_MIN 11.0
    INSTALL_NAME /usr/lib/libSystem.B.dylib)
set_property(TARGET libSystem_B_stub PROPERTY OUTPUT_NAME "System.B")
set_property(TARGET libSystem_B_stub PROPERTY EXCLUDE_FROM_ALL TRUE)

target_sources(libSystem_B_stub PRIVATE empty.c)

# Match launchd-dummy's LC_LOAD_DYLIB expectation (from otool -L) so dyld's
# compatibility-version check accepts this as a satisfying match.
target_link_options(libSystem_B_stub PRIVATE
    -compatibility_version 1.0.0
    -current_version 1292.100.5)

# cctools' ld64 applies its "dynamic main executables must link with
# libSystem.dylib" policy check to ANY dynamic output with zero declared
# dependencies -- dylibs included, not just MH_EXECUTEs. Empirically (isolated
# repro outside CMake), neither -umbrella/-all_load nor genuinely-unresolved
# symbols avoid it; the only thing that does is an actual LC_LOAD_DYLIB, which
# means linking -lSystem against the SDK's stub .tbd (build-time only, no code
# pulled in -- matches -nostdlib). Since THIS target's own install name IS
# /usr/lib/libSystem.B.dylib, that produces a literal self-referential
# LC_LOAD_DYLIB (ld64 warns "linking with itself", not an error). This is safe:
# PD's dyld is the real dyld-832 source tree, which already dedups already-
# loaded images by path (required for ordinary diamond dependencies), so
# resolving a self-reference at runtime is not a new code path.
target_compile_options(libSystem_B_stub PRIVATE -isysroot ${PUREDARWIN_MACOSX_SDK})
target_link_options(libSystem_B_stub PRIVATE -isysroot ${PUREDARWIN_MACOSX_SDK} -lSystem)

install(TARGETS libSystem_B_stub LIBRARY DESTINATION usr/lib COMPONENT BaseSystem)
