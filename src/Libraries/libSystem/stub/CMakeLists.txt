# libSystem_B_stub: /usr/lib/libSystem.B.dylib.
#
# STILL NOT the real Apple libSystem.dylib (no libsystem_c.dylib/libsystem_
# malloc.dylib/etc sub-dylib products exist in PD -- those pieces are built
# only as static archives, e.g. liblibc_static.a, liblibmalloc_static.a...),
# but no longer an empty placeholder either: this target now links those
# archives directly and exports the specific symbol surface real dependents
# (currently: libdyld.dylib) need, so it functions as a genuine umbrella
# providing real code -- see the -u/-exported_symbols_list block below and
# libSystem.exports for exactly what's covered. It also carries the dyld<->
# libSystem handshake initializer (pd_libSystem_init.c) -- dyld hardcodes
# that only the image whose install path is exactly libSystem.B.dylib's may
# run an initializer before libSystemInitialized is set, so that constructor
# has to live here, not in libdyld.dylib. Expand libSystem.exports (and the
# archives linked below) as more of userland needs real libSystem symbols.
#
# ld64 requires every dynamic MH_EXECUTE to carry an LC_LOAD_DYLIB on
# libSystem.dylib (a hardcoded policy check, not a symbol-resolution one), so
# any PD userspace binary that links dynamically -- e.g. launchd-dummy, which
# calls no libSystem function at all, just raw syscalls -- ends up with a real
# runtime dependency on a file that has to actually exist and be loadable for
# dyld's LC_LOAD_DYLIB resolution to succeed, even though nothing in the binary
# calls into it. This target's install name and version match what dyld's
# dependent-library check compares against.
add_darwin_shared_library(libSystem_B_stub
    MACOSX_VERSION_MIN 11.0
    INSTALL_NAME /usr/lib/libSystem.B.dylib)
set_property(TARGET libSystem_B_stub PROPERTY OUTPUT_NAME "System.B")

target_sources(libSystem_B_stub PRIVATE
    empty.c
    pd_libSystem_init.c
    pd_libSystem_compat.c
    pd_libSystem_process.c)

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
# Like dyld/libdyld: compile pd_libSystem_init.c against the SDK sysroot (it
# pulls mach/mach.h, pthread.h, malloc/malloc.h) instead of PD's -nostdinc
# header set. Drop the -nostdinc that add_darwin_shared_library injects; PD's
# own -I dirs still win.
get_target_property(_libsystem_b_copts libSystem_B_stub COMPILE_OPTIONS)
if(_libsystem_b_copts)
    list(REMOVE_ITEM _libsystem_b_copts -nostdinc)
    set_target_properties(libSystem_B_stub PROPERTIES COMPILE_OPTIONS "${_libsystem_b_copts}")
endif()
target_compile_options(libSystem_B_stub PRIVATE -isysroot ${PUREDARWIN_MACOSX_SDK})
target_link_options(libSystem_B_stub PRIVATE -isysroot ${PUREDARWIN_MACOSX_SDK})

# libSystem.B.dylib is no longer empty: /usr/lib/system/libdyld.dylib imports a
# handful of libc/pthread/kernel/malloc symbols (___assert_rtn, _malloc/_free/
# _malloc_size, _pthread_*, _strcmp/_strlen/_strncmp/_strrchr, ___cxa_*,
# _mmap/_vm_allocate, _abort/_exit/_getpid, ___memmove_chk) and, with two-level
# namespacing, expects to resolve them specifically HERE. On real Darwin those
# live in the libsystem_* sub-dylibs that libSystem.B re-exports; PD builds them
# only as static archives, so we instead link those archives directly and export
# exactly the needed symbols. The exports list (libSystem.exports) names them as
# required roots, so ld64 pulls their defining members + transitive closure out
# of the archives -- the umbrella genuinely provides them. Same archive SET and
# order dyld itself links (so transitive deps resolve), plus libmalloc for
# _malloc_size.
set(LIBSYSTEM_EXPORTS ${CMAKE_CURRENT_SOURCE_DIR}/libSystem.exports)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LIBSYSTEM_EXPORTS})
# -exported_symbols_list only FILTERS what is exported; it does not force archive
# members to load. empty.c references nothing, so without help ld64 would pull no
# members and every listed symbol would stay an undefined import. Turn each
# exported symbol into an undefined root (-u) so ld64 pulls the member defining
# it (and its transitive closure) out of the static archives below.
file(STRINGS ${LIBSYSTEM_EXPORTS} _libsystem_export_lines)
set(_libsystem_u_flags)
foreach(_line ${_libsystem_export_lines})
    string(STRIP "${_line}" _line)
    if(_line AND NOT _line MATCHES "^#")
        list(APPEND _libsystem_u_flags "-Wl,-u,${_line}")
    endif()
endforeach()
add_dependencies(libSystem_B_stub
    libsystem_kernel_static libplatform_static pthread_static
    libc_static libc_internal_static libmalloc_static
    corecrypto_static CrashReporterClient)
if(PUREDARWIN_NIX_TOOLCHAIN)
    # ld64.lld resolves archive roots before its duplicate-dead-strip pass can
    # discard libsystem_kernel's _libc_funcptr dispatch wrappers. Let the real
    # pthread/platform/libc archives satisfy their public symbols first, then
    # pull libsystem_kernel for Mach/syscall pieces.
    target_link_libraries(libSystem_B_stub PRIVATE
        $<TARGET_FILE:libplatform_static>
        $<TARGET_FILE:pthread_static>
        $<TARGET_FILE:libc_static>
        $<TARGET_FILE:libc_internal_static>
        $<TARGET_FILE:libmalloc_static>
        $<TARGET_FILE:libsystem_kernel_static>
        ${CMAKE_BINARY_DIR}/src/Libraries/libSystem/libsystem_kernel/syscalls.a
        $<TARGET_FILE:corecrypto_static>
        $<TARGET_FILE:CrashReporterClient>)
else()
    target_link_libraries(libSystem_B_stub PRIVATE
        $<TARGET_FILE:libsystem_kernel_static>
        $<TARGET_FILE:libplatform_static>
        $<TARGET_FILE:pthread_static>
        $<TARGET_FILE:libc_static>
        $<TARGET_FILE:libc_internal_static>
        $<TARGET_FILE:libmalloc_static>
        ${CMAKE_BINARY_DIR}/src/Libraries/libSystem/libsystem_kernel/syscalls.a
        $<TARGET_FILE:corecrypto_static>
        $<TARGET_FILE:CrashReporterClient>)
endif()
if(TARGET libcxxabi_static)
    add_dependencies(libSystem_B_stub libcxxabi_static_build)
    target_link_libraries(libSystem_B_stub PRIVATE $<TARGET_FILE:libcxxabi_static>)
endif()
if(TARGET libunwind_static)
    add_dependencies(libSystem_B_stub libunwind_static_build)
    target_link_libraries(libSystem_B_stub PRIVATE $<TARGET_FILE:libunwind_static>)
endif()
target_link_options(libSystem_B_stub PRIVATE
    ${_libsystem_u_flags}
    # -dead_strip (as dyld's own link uses): resolve the unavoidable duplicate
    # definitions that overlapping static archives carry (mem/str primitives in
    # both libplatform and libc/libsystem_kernel; pthread funcptr wrappers in both
    # libsystem_kernel and pthread_static) by keeping only atoms reachable from
    # the exported roots and stripping the rest, instead of erroring on the dup.
    -Wl,-dead_strip
    # Without -lSystem there is no dyld_stub_binder for lazy binding (some pulled
    # code, e.g. __tlv_exit, would need it). Use chained fixups so ALL binds are
    # eager -- no lazy stubs, no dyld_stub_binder -- exactly as libdyld.dylib does.
    -Wl,-fixup_chains
    -Wl,-exported_symbols_list,${LIBSYSTEM_EXPORTS})
if(PUREDARWIN_NIX_TOOLCHAIN)
    # ld64.lld diagnoses duplicate archive members before ordinary dead_strip
    # can discard them; Apple's ld64 accepts this link. Keep the compatibility
    # switch scoped to the Nix/LLVM linker path.
    target_link_options(libSystem_B_stub PRIVATE -Wl,--dead-strip-duplicates)
endif()
set_property(TARGET libSystem_B_stub APPEND PROPERTY LINK_DEPENDS ${LIBSYSTEM_EXPORTS})

# Reexport libdyld (/usr/lib/system/libdyld.dylib) so that any binary depending
# on libSystem.B.dylib -- e.g. launchd -- pulls libdyld into its load graph and
# runs its initializer, which registers the dyld thread-helpers (required for
# LC_MAIN launch). This mirrors real Darwin, where libSystem.B.dylib is an
# umbrella that reexports libdyld and the other libsystem_* sub-libraries.
# NB: -reexport_library alone did NOT resolve pd_libSystem_init.c's own
# references to libdyld's `start` (start_glue.s) and `_dyld_func_lookup`
# (dyldLibSystemGlue.c) -- link libdyld directly too so those resolve normally;
# -reexport_library still marks it LC_REEXPORT_DYLIB for dependents.
add_dependencies(libSystem_B_stub libdyld)
target_link_libraries(libSystem_B_stub PRIVATE $<TARGET_FILE:libdyld>)
target_link_options(libSystem_B_stub PRIVATE
    -Wl,-reexport_library,$<TARGET_FILE:libdyld>)

install(TARGETS libSystem_B_stub LIBRARY DESTINATION usr/lib COMPONENT BaseSystem)
