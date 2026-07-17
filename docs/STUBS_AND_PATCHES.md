# PureDarwin stubs and patches: what, where, why

This is a rundown of the compatibility shims (the pd_ files
scattered through src/Libraries) and the patches we've made to vendored
Apple source (xnu, libdispatch, libsystem_kernel) so they'll actually build
and link without a real macOS host underneath them. First part is a tour of
every pd_ file in the tree. Second part walks through the libdispatch work
in detail, since that was the biggest and most recent chunk of work.

## Part 1: the pd_ files, and why each one exists

Basically every file in this list exists for one of three reasons. Either a
real macOS-only service isn't there (configd, mDNSResponder, notifyd, XPC),
or a vendored Apple source file leans on internals PureDarwin hasn't built
yet, or some weak-alias/common-storage linking trick Apple's own build
system handles fine doesn't survive our cross-build toolchain intact.

### dyld (src/Libraries/dyld/)

pd_dyld_internal_stubs.cpp covers dyld internals that have no PureDarwin
backing yet. pd_fileutils.cpp is a minimal runtime-side FileUtils for the
dyld target. pd_libc_common_storage.c works around a real linker quirk:
Apple's ld doesn't reliably pull in archive members that only satisfy
common-storage symbols once dyld's libc_internal objects reference them, so
this forces the issue. pd_stubs.c stands in for Apple's source-available
policy hooks that dyld calls but that have no public implementation
anywhere.

### libcxxabi (src/Libraries/libcxxabi/src/)

pd_bootstrap_runtime.cpp fixes a posix_memalign/free pairing bug: plain
free() has to work on memory from posix_memalign(), and an earlier version
of this shim broke that.

### libresolv (src/Libraries/libresolv/)

pd_dnsinfo_stub.c is a no-op dnsinfo client, since the real one talks to
configd and SCDynamicStore, neither of which exist here. Callers already
handle that fallback gracefully. pd_dns_sd_stub.c stands in for Apple's
dns_sd.h client API, normally backed by mDNSResponder over Mach IPC or a
Unix socket. pd_getaddrinfo.c and pd_net_compat.c fill in small real
networking helpers that res_send.c and ns_print.c need but that don't exist
anywhere else yet. pd_notify_stub.c is a no-op notifyd client.
pd_res_compat.c exists because resolv.h unconditionally redefines the
modern resolver API names (res_init, res_query, res_search, and so on) down
to the legacy res_9 equivalents, and this supplies what that redirect
actually needs. pd_res_state.c holds storage for the legacy global _res
resolver-state struct that resolv.h declares extern.

### libc (src/Libraries/libSystem/libc/)

pd_arc4random.c is a minimal arc4random_uniform. pd_ffs.c is a real ffs(3)
implemented via __builtin_ffs, not really a stub at all, just kept separate
because Apple's own version is platform assembly. pd_fpclassify.c provides
__fpclassifyf, __fpclassifyd, and __fpclassifyl, the functions real
math.h's fpclassify(x) macro expands into. pd_os_crash.c is a minimal
_os_crash. pd_getgrent.c reads /etc/group to back getgrnam, getgrgid, and
getgrouplist. pd_getpwent.c reads /etc/passwd to back getpwnam and
getpwuid. pd_getservbyname.c reads /etc/services to back getservbyname.
pd_libm_builtins.c is a minimal libm surface that was needed for early
userland bring-up (BusyBox and friends) before the from-scratch libm
covered everything. pd_netdb_stub.c is a minimal gethostbyname, h_errno,
and hstrerror. pd_notify_stub.c is a minimal notify_post. pd_popen.c is a
real popen and pclose. pd_wait.c exists because the FreeBSD-derived wait.c
publishes wait as a weak alias of __wait, and that weak-alias macro doesn't
produce a usable symbol in our toolchain, so this just provides a real wait
directly. pd_libm_pow.c, pd_libm_trig.c, and pd_libm_priv.h are the real,
from-scratch libm implementation (log, exp, pow, cbrt, hypot, sin, cos,
atan, atan2, asin, acos) since PureDarwin has no ported Libm yet.
pd_legacy_rune.c covers sgetrune and sputrune, the old pre-multibyte-locale
rune conversion API, deprecated even on real macOS but still referenced
somewhere. pd_in6_addr.c holds storage for the in6addr_any and
in6addr_loopback globals that netinet6/in6.h declares extern.
pd_strnlen.c is a real, standalone strnlen, strlcpy, and strlcat.
pd_strtok_r.c has the same weak-alias problem as pd_wait.c, this time for
strtok_r and __strtok_r. And gen/pd_dispatch_once.c, which used to stub out
dispatch_once, was removed this session, covered in part two below.

### libdispatch (src/Libraries/libSystem/libdispatch/compat-include/)

pd_libdispatch_link_gaps.c and pd_work_interval_instance.h were both added
this session. See part two.

### libdyld (src/Libraries/libSystem/libdyld/)

pd_dladdr.c is dladdr() plus the dyld public image-enumeration API it's
built on. pd_libdyld_exports.c bridges libdyld.dylib's private, deliberately
hidden-visibility ABI surface (see start_glue.s and dyldLibSystemGlue.c) out
to symbols that are actually exported.

### libmalloc (src/Libraries/libSystem/libmalloc/src/)

pd_nanov2_fallback.c is a fallback for the nanov2 (nano allocator v2) path.

### libplatform (src/Libraries/libSystem/libplatform/)

pd_bridgeos_compat.h gets force-included into libplatform_static and covers
a handful of bridgeOS-conditional compat shims.

### pthread (src/Libraries/libSystem/pthread/)

pd_dyld_variant_extras.c supplies the extra pieces pthread_static's reduced
VARIANT_DYLD build needs, including _pthread_atomic_xchg_ptr and
_pthread_qos_class_encode_workqueue. pd_main_qos.c holds
_pthread_set_main_qos, a narrow bit of QoS storage. Part two explains why
the rest of the real qos.c still isn't wired in.

### libSystem stub (src/Libraries/libSystem/stub/)

pd_chkstk_darwin.s is ____chkstk_darwin, the stack-probe helper clang calls
before any function whose stack frame is bigger than a page.
pd_libSystem_compat.c is a grab bag of small libSystem-level compat
functions (dlerror, atol, atoll, the $UNIX2003 and $DARWIN_EXTSN symbol
variants, and so on). Its dispatch_semaphore block was removed this
session, also covered below. pd_libSystem_init.c is the real dyld to
libSystem handshake initializer. pd_libSystem_process.c handles
process-level libSystem bring-up.

## Part 2: getting real libdispatch working

The goal was to replace the pthread-based dispatch_once and
dispatch_semaphore stubs with real, upstream libdispatch, fully linked into
libSystem.B.dylib. That's done now. We confirmed it by running nm on the
built dylib and seeing dispatch_apply, dispatch_once, dispatch_queue_create,
and dispatch_semaphore_create all show up as real compiled code, not stubs.

### Why this came up

fastfetch's CFSortFunctions.c needed a symbol called
dispatch_queue_attr_concurrent for its parallel-sort code path, and pulling
on that thread showed that libdispatch, 174 real vendored source files
sitting under src/Libraries/libSystem/libdispatch, had never actually been
wired into the build. There wasn't even a CMakeLists.txt for it.

### What we changed, and why

We wrote a real CMakeLists.txt for libdispatch, building a proper
libdispatch_static target. The first and biggest piece was MIG code
generation for protocol.defs, producing protocol.h and protocolServer.h,
using the project's existing mig() CMake helper that's already used for
libsystem_kernel's mach headers. Nothing else in the target would compile
until init.c and queue.c had those headers available. The source list is
pure C only, no block.cpp, no Objective-C, no Swift, since those need a
working libc++ and libobjc we don't have, but it does include
event_kevent.c for the Darwin kqueue backend.

A handful of compile definitions had to be set, each for a specific reason.
HAVE_OBJC and USE_OBJC are both off since there's no real Objective-C
runtime here. DISPATCH_USE_DTRACE is off because the DTrace probe-point
glue in provider.h is normally generated from provider.d at build time via
dtrace -h, and we don't have DTrace. VOUCHER_USE_MACH_VOUCHER is off
because the real path needs firehose activity-tracing internals from a
separate, unvendored Apple project; upstream already wrote a firehose-free
fallback inside voucher.c's else branch, so we just route there instead.
PRIVATE is on because it unlocks mach_get_times's userspace declaration in
osfmk/mach/mach_time.h, which is gated behind that macro.
__PTHREAD_EXPOSE_INTERNALS__ is on because it unlocks
pthread/priority_private.h's internal helpers like
_pthread_priority_relpri, which real Apple libdispatch also defines when
building against pthread internals.

The most interesting one was OS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY. This
turned out to be the actual root cause behind a whole family of undeclared
atomic functions, os_atomic_inject_dependency,
os_atomic_load_with_dependency_on, and os_atomic_dependency_t among them.
That whole API in xnu's atomic_private.h is gated behind this macro, which
correctly defaults to whatever XNU_KERNEL_PRIVATE is set to, meaning off
for anything that isn't the kernel. Since real Apple libdispatch is a
completely legitimate userspace consumer of this API, we just turn the
macro on directly rather than pretending to be kernel code. Last one,
DISPATCH_VARIANT_STATIC, exists because dispatch_queue_attr_concurrent is
normally supposed to come from an ld64 alias list (a file called
libdispatch.aliases) pointing it at the first entry of
dispatch_queue_attrs, and we don't wire that alias mechanism up at all.
This macro, which upstream added specifically for static-linking scenarios
like ours, compiles a real, separate definition instead.

Beyond the compile definitions, there were three separate cases where the
wrong header was quietly winning on the include path, and each one took a
while to track down because the symptom looked like a plain missing
declaration rather than a shadowing problem. The first was
libsystem_kernel/mach/string.h, which is a deliberate Apple header meant
only for MIG-generated code, and only declares memcpy, memset, and bzero,
which it says right in its own comment. It was earlier on the include path
than the real libc/include, so it was quietly killing strlen, strcmp,
strdup, and everything else in string.h for the whole target. We moved it
to come after libc/include instead of removing it, since MIG-generated code
in the same target still needs it. The second was xnu's own
osfmk/mach/mach.h, a much thinner kernel-side header that only pulls in
mach_types.h, message.h, thread_switch.h, and mach_interface.h, with no
path to mach_error.h at all. That header was shadowing the real userspace
mach.h and is why mach_error_t stayed undefined even after we'd fixed a
separate, unrelated set of broken symlinks (described below). We reordered
things so the real userspace headers win, while keeping xnu/osfmk on the
path afterward since it's still needed for mach_time.h and libproc.h.

The third case wasn't really a shadowing problem so much as a missing
directory: os/atomic_private.h and os/base_private.h live under
xnu/libkern, and that directory was never actually reachable through the
xnu_headers and xnu_private_headers link targets, because those only expose
xnu's installed header tree, and these particular headers are xnu-internal
and were never marked for installation. We added xnu/libkern as the very
last entry on the include path, which matches the same safe ordering
libmalloc_static already uses. Its own CMakeLists has a comment warning
about exactly this trap: adding xnu/libkern too early shadows
libplatform's real OSAtomic.h and MacTypes.h.

We also force-include a couple more headers on top of the existing
pd_bridgeos_compat.h, dyld_sdk_compat.h, and kdebug_private.h. One is
os/base_private.h, needed for os_unlikely and os_likely, which
event_kevent.c calls directly but that nothing else in libdispatch's own
include chain happens to pull in. The other is
pd_work_interval_instance.h, described below.

In internal.h we added an extern declaration for mach_msg_destroy, placed
right after mach/message.h gets included so mach_msg_header_t is already
defined by that point. The real implementation already exists in
libsystem_kernel/mach/mach_msg.c, the declaration just wasn't reaching us
through mach.h as resolved on our include path, and this was the cleanest
way to close that gap without disturbing anything around it.

In voucher_internal.h we added _voucher_release_no_dispose to the
VOUCHER_USE_MACH_VOUCHER off branch, as a no-op, matching every other
function in that branch. Upstream had only ever written this function for
the real mach-voucher branch. That's a genuine gap in the fallback path
upstream left behind, not something we broke ourselves.

In shims/atomic.h we filled in two more atomic macro gaps between our
vendored xnu atomic_private.h and what libdispatch's own shim expects.
_os_atomic_mo_dependency is the version without the _smp suffix, needed by
atomic_signal_fence, and it's a separate gap from the _smp-suffixed one we
had already fixed earlier in the session. _os_atomic_auto_dependency is
used by both os_atomic_inject_dependency and
os_atomic_load_with_dependency_on, and it wraps a raw value into a
dependency token using _Generic, unless the value is already a token.

We added a new file, pd_work_interval_instance.h, to stub out
work_interval_instance and its eleven related functions (alloc, free,
clear, set_start, set_deadline, set_finish, start, update, finish). This is
a real macOS frame-pacing and QoS API that was never vendored into this
tree at all, no declarations and no implementation anywhere. We stub it as
an always-degraded no-op: alloc returns a non-null sentinel so callers
don't treat every workgroup interval as an allocation failure, and every
other operation succeeds without doing anything, so work intervals just
never get real kernel-side timing hints. One thing worth remembering here:
the handle type, work_interval_instance_t, already existed in
sys/work_interval.h, so we reuse it rather than redeclaring it. Redeclaring
it the first time around caused a real typedef redefinition error.

We also added a second new file, pd_libdispatch_link_gaps.c, covering
everything else that turned out to be genuinely missing rather than merely
shadowed. dispatch_block_special_invoke is a sentinel function pointer
object, not a function itself, that real Apple's block.c normally defines.
We don't compile block.c at all since it needs libc++ and libobjc, so no
code path can ever actually produce a block whose invoke pointer equals
that address anyway. It just needs to exist and be distinct from every real
invoke function so the identity comparison in inline_internal.h keeps
working correctly, which it does, since the comparison is always false as
intended.

Then there's a whole cluster of pthread QoS and workqueue-override
functions: _pthread_attr_get_qos_class_np, _pthread_attr_set_qos_class_np,
_pthread_qos_class_encode, pthread_qos_max_parallelism,
_pthread_qos_override_start_direct, _pthread_qos_override_end_direct,
_pthread_set_properties_self, _pthread_workqueue_override_reset,
_pthread_workqueue_override_start_direct,
_pthread_workqueue_override_start_direct_check_owner, and qos_class_main.
All of these are stubbed as safe no-ops. The reason they were missing in
the first place is that pthread_static is deliberately built as the
reduced VARIANT_DYLD variant, meant for dyld's own static-link bootstrap
needs rather than being a full pthread implementation, and it never
compiles the real pthread/src/qos.c, which itself would need real kernel
workqueue support that this project doesn't have yet. Building that for
real felt out of scope for this pass. Queues still run correctly without
it, just without real QoS-based scheduling behind them.

Two more small pieces rounded this file out. os_assert_log's real
signature turned out to be char pointer, taking a single uint64_t code, not
the varargs logging function the name might suggest, so we wrote a small,
cheap real implementation that formats the code into a static buffer.
And strerror dollar UNIX2003 needed asm aliasing since a dollar sign isn't
a valid character in a plain C identifier, so it's a thin wrapper around
the real strerror.

Separately, and unrelated to anything we ourselves broke, we found and
fixed thirteen broken symlinks under
libsystem_kernel/include/mach and libsystem_kernel/include/servers. All
thirteen pointed at a stale target path, something like
../../src/mach/name.h, that simply doesn't exist. The real files live one
level differently, under ../../mach/mach/name.h and
../../mach/servers/name.h. This was quietly shadowing mach_error_t through
a completely different mechanism than the osfmk mach.h issue above, both
had to be fixed independently, and it was also cascading into duplicate MIG
notify-message typedef errors elsewhere in the build.

With real libdispatch working, we removed the two pthread-based stand-ins
it replaces. gen/pd_dispatch_once.c is gone entirely, since real
libdispatch's once.c now provides dispatch_once and dispatch_once_f. And
the dispatch_semaphore block inside pd_libSystem_compat.c, roughly ninety
lines including the pd_dispatch_semaphore struct and its helper macros, is
gone too, since real semaphore.c now provides
dispatch_semaphore_create, dispatch_semaphore_signal, and
dispatch_semaphore_wait.

Finally, we wired libdispatch_static into libSystem_B_stub's actual link.
It's now in add_dependencies, and it's linked in both the
PUREDARWIN_USE_LD64_LLD branch and the default branch, positioned so it
resolves before libsystem_kernel_static, which matches the existing
archive-ordering reasoning already documented there for how ld64.lld
handles duplicate symbols during its dead-strip pass.

We also expanded libSystem.exports with the real dispatch API surface,
covering queues, sources, groups, semaphores, apply, and once. Two naming
traps came up while doing this, both worth remembering if more symbols get
added later. The first is that dispatch_main_q, which has one leading
underscore in the source, needed two leading underscores in the exports
list, written as double underscore dispatch_main_q. Apple's C symbol
mangling always adds exactly one more leading underscore on top of
whatever the source name already has, so a source name that already starts
with an underscore ends up needing two in the exports file. The second is
that dispatch_queue_create_with_target is declared with a macro called
DISPATCH_ALIAS_V2, which expands to an asm rename tacking a dollar V2 suffix
onto the symbol. The real compiled and exported name is
dispatch_queue_create_with_target followed by dollar V2, not the plain
name by itself.

### How we actually found these bugs

For the three header-shadowing issues, just grepping the error text was a
dead end for a long time, the same mach_error_t symptom kept coming back
even after fixes that should have worked. What actually worked was pulling
the exact failed clang command straight out of the Nix build log, then
going and looking directly inside the Nix store copy of the source tree
that build actually used (you can find its store path by running
nix show-derivation on the failed drv and grepping for the source input),
to see literally which file a given include path entry resolves a given
include statement to. That was a lot faster than trying to reason about
include order from memory or guesswork.
