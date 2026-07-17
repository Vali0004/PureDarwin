# What we changed in xnu, and why

PureDarwin's vendored xnu tree is built directly from Apple's
xnu-7195.121.3 release. There is a .version file at the root of
src/Kernel/xnu that says exactly that, and diffing our whole tree against
a fresh checkout of that same tag confirms it: the two are almost entirely
identical. Out of several thousand files, only 105 actually differ in
content, 47 exist only on our side, and 8 exist only on Apple's side. This
document walks through what those real differences are and why each one is
there. It does not cover whitespace or tab-to-space reformatting noise,
which shows up in a few files (cpuid.c especially) but carries no meaning
on its own.

## The build system layer

Apple's xnu assumes it is being built on a real Mac, by real Apple tools,
signed by a real codesigning identity. None of that exists here, so a
handful of build-system files carry patches whose only job is making the
build possible on a Linux host with a cross toolchain.

The clearest example is makedefs/MakeInc.def and makedefs/MakeInc.cmd.
Upstream, these are full files with real content, architecture tables,
compiler flag definitions, and so on. In our tree they are each reduced to
a single include line pointing at a generated copy under OBJROOT. The real
content now lives as templates, MakeInc.def.in and MakeInc.cmd.in, under a
new cmake directory we added, along with a CMakeLists.txt and a small
preprocess_files.cmake that fill those templates in at configure time. This
is the same pattern the rest of PureDarwin uses to cross-build Apple
sources through CMake instead of Apple's own build tooling.

makedefs/MakeInc.top picks the memory size differently depending on host.
On a real Mac it shells out to sysctl. On Linux it reads /proc/meminfo
instead, since sysctl doesn't exist there.

makedefs/MakeInc.kernel and makedefs/MakeInc.rule strip out the
codesigning step (there is no codesign_allocate identity to sign with here)
and the plutil binary-plist conversion step (no plutil on Linux either),
and add idempotent symlink handling to the header-install rules so that
incremental builds don't try to dereference a symlink that a previous build
already created.

The same story repeats in miniature across several small standalone tools
under SETUP: decomment, setsegname, kextsymboltool, and installfile all
originally include libc.h and other Apple-only headers unconditionally.
Each one now wraps that include in an ifdef __APPLE__, with a small
mach_compat.h (a new file we added under SETUP) providing the handful of
Mach-specific types those tools need when building outside Apple's own SDK.

## Real CPU support: AMD, and newer Intel chips

This is the largest functional change by far, spread across cpuid.c,
cpuid.h, tsc.c, commpage.c, mtrr.c, cpu_threads.c, monotonic_x86_64.c, and
machine.h.

Stock xnu only ever expected to run on Intel silicon. cpuid.c now detects
CPU vendor explicitly (Intel vs AMD vs unknown) and branches on it in
several places: which CPUID leaf to use for cache and TLB descriptors
(leaf 4 on Intel, leaf 0x8000001D on AMD), how core and thread counts get
read back, and how far a search for cache topology should go. cpuid.h gains
the vendor constants themselves, a table of newer Intel model IDs that
didn't exist yet when 7195.121.3 shipped (Ice Lake, Tiger Lake, Rocket
Lake, Alder Lake, Raptor Lake, Sapphire and Emerald Rapids among them), and
a full table of AMD family and model IDs going back through the Bulldozer
and Zen generations. machine.h gets the matching CPUFAMILY_ constants for
both.

tsc.c gained real AMD time-stamp-counter synchronization logic, since AMD's
TSC behaves differently across cores than Intel's, including working
around AMD's own "Dual-Core Optimiser" quirk and a proper handling of
reserved P-state divisor values on Zen parts. commpage.c and mtrr.c both
gained a guard around MSR_IA32_MISC_ENABLE, which simply doesn't exist on
AMD hardware and would otherwise be read unconditionally.
monotonic_x86_64.c adds a small workaround for a hardware quirk on
Silvermont and Airmont era Intel chips around performance counter
configuration.

There's also a QEMU-specific addition here: cpuid.h now defines a vendor ID
string for QEMU's TCG software emulator, since this project runs primarily
under QEMU rather than on bare metal.

## Hardening against QEMU TCG specifically

A few patches exist purely because QEMU's TCG (the pure software,
instruction-by-instruction CPU emulator, as opposed to KVM hardware
acceleration) behaves differently from real silicon in ways that were
causing real boot failures.

tsc.c's frequency calculation used to divide by values read from CPUID leaf
0x15, which real hardware always populates but which TCG leaves at zero.
That produced a divide-by-zero kernel panic on every boot under TCG. The
fix falls back to a value of one for both the numerator and denominator
when either comes back zero.

cpu_threads.c had a very similar problem in its CPU topology math: several
divisions assumed core and thread counts read back from CPUID would never
be zero, which is true on real hardware but not always true under TCG. A
small nonzero_u32 helper now substitutes a safe default anywhere a zero
would otherwise reach a division.

commpage.c has a subtler one. Between allocating the comm page and
finishing the writes that populate it, there is a small window where the
page pointer is already non-null but the page itself is only lazily
faulted in on first touch. On real hardware that fault happens fast enough
that nobody has ever hit this window from another CPU. Under TCG's much
slower emulation, the periodic timer interrupt reliably lands inside that
window on another core, touches the comm page before it's finished being
set up, and faults on a mapping the first CPU is still in the middle of
establishing, panicking the boot. The fix disables interrupts for just that
narrow publish-and-fault-in window, not around the larger allocation call
that precedes it.

model_dep.c has one more of this shape: x86_topo_lock is normally only
initialized later, during smp_init(), but any panic that happens before
that point enters the debugger path, which tries to lock it anyway and
panics a second time because the lock was never initialized, turning every
early panic into an invisible nested-panic storm that hides the real
message. The fix just initializes the lock a little earlier, which is
harmless since the normal later initialization simply re-initializes an
already-unlocked lock.

## Diagnostics for a machine with no attached debugger

Several patches exist purely to make failures visible when running headless
under QEMU with no way to attach a real debugger.

trap.c now prints a labelled diagnostic line plus, where possible, a
backtrace walked by hand through the userland rbp frame-pointer chain,
whenever a user-mode program dies from an illegal instruction, a general
protection fault, or an unhandled page fault. This is the same information
a real debugger would show, just written to the serial console instead.

kdp_machdep.c's kernel-trap diagnostic was expanded from a single summary
line into a full register dump (all the general-purpose registers, flags,
segment selectors, the works) for the same reason: no debugger means the
kprintf output on serial is the only record of what happened.

pe_kprintf.c and video_console.c both address the same underlying problem
from different angles: with certain boot-arg combinations, kernel log
output was going to only one of the video console or the serial port, never
both, which meant a hang partway through boot with an unreadable screen and
nothing on serial either. Both files now mirror kprintf output to serial
regardless of which console boot-args select, scoped carefully so it
doesn't double up with the existing interactive tty path (an earlier
attempt that mirrored the interactive path too caused doubled characters,
and was reverted).

atm.c disables the kernel-side firehose logging path (ATM_TRACE_DISABLE)
because nothing in this OS ever consumes it (there's no logd), and because
it wasn't just unused, it actively crashed the kernel the first time a
kprintf ran from a freshly spawned kernel thread's power-management
workqueue context.

## Small, targeted real bug fixes

A handful of patches fix bugs that would matter on any platform, not just
this one.

asm_help.h had a genuine bug in its BRANCH_EXTERN and CALL_EXTERN assembly
macros: they loaded the target symbol's value where they should have
loaded its address, so when the callee turned out to be locally resolved
(as opposed to going through the usual indirect symbol stub), the code
would jump through the first eight bytes of the target function's own
machine code instead of to its address. That produces a non-canonical
instruction pointer and an immediate fault. The fix changes the load from
one that dereferences to one that just takes the address (leaq instead of
the old call/pop/movl trick used to compute position-independent
addresses).

kern_exec.c had an integer underflow: when a process's stack rlimit is set
to unlimited, which is this OS's default, the code computing how much of
the stack region to protect would subtract the (larger) allocated stack
size from the (smaller, in this case actually unlimited-but-represented-
differently) limit, wrap around to a huge number, and end up marking
essentially the entire stack region as inaccessible. Any newly grown,
never-yet-faulted stack page then took an immediate fatal fault. The fix
just skips the protect call entirely when the limit already covers the
whole allocation.

km.c guards against a null console tty pointer: IOHIDSystem can forward a
keystroke here before the console tty has actually been allocated, if a
key gets pressed early enough in boot, and the fix simply drops the
character instead of dereferencing null.

_endian.h had a subtler bug: __DARWIN_BYTE_ORDER needs to be defined before
the file's own #elif chain runs, and it wasn't always guaranteed to be. On
a little-endian target, an undefined macro just evaluates as zero, which
happened to equal __DARWIN_BIG_ENDIAN, silently turning htonl and ntohl
into identity functions. In practice this showed up as inet_aton returning
addresses in host byte order instead of network byte order, purely because
of an unlucky header include order elsewhere in the tree. The fix includes
machine/endian.h up front, the same way modern Apple SDKs do, and falls
back to the compiler's own __BYTE_ORDER__ if that still leaves it
undefined. wait.h picked up the same defensive __DARWIN_BYTE_ORDER
definition for the same underlying reason.

IOUserClient.cpp had two matching-service entry points, the ones backing
IOServiceGetMatchingServices and IOServiceGetMatchingService from
userspace, that were stubbed out to always return kIOReturnUnsupported.
They now call through to the real internal implementations. Without this,
IOKit's own service matching is unusable from userspace, which a lot of
higher-level code assumes works.

bootstrap.cpp had a real, if minor, bug: the return value of
loadKextWithIdentifier was being silently discarded instead of stored,
meaning the code could never actually detect a load failure there. It's
now captured in a proper local variable.

mach_kernelrpc.c just reorders two blocks, moving an early check ahead of
the code it's meant to guard, correcting what looks like a copy-paste
ordering slip in the original.

devfs.h and devfs_tree.c together add one small real feature: a
devfs_is_ready() accessor exposing whether devfs can accept device nodes
yet, which didn't exist before.

kern_mman.c tightens two feature checks so they only apply on macOS
10.15 and later, matching the deployment target this build actually
targets, instead of applying unconditionally to every XNU_TARGET_OS_OSX
build regardless of minimum version.

IOMemoryDescriptor.cpp adds a boot-arg gate, iomap_debug, around a chunk of
otherwise-unconditional debug logging, so it can be toggled at boot instead
of needing a rebuild.

## Tooling and test scripts

Most of the remaining differences are in Python tooling under
tools/lldbmacros and a scattering of standalone C test and setup tools
under tools/tests and SETUP. The lldbmacros changes are mostly API surface
adjustments, GetEnumValue's signature in utils.py is a good example, it
used to accept either a combined "type::member" string or a type and
member passed separately, and now only accepts the plain name form,
suggesting these scripts were updated to track a different (likely newer)
version of the underlying lldb Python API than the one 7195.121.3 shipped
against. zero-to-n.c, a scheduler test tool, has a couple of QEMU-related
simplifications, notably removing an SMT-aware core count branch that
doesn't apply under emulation. None of these affect the kernel itself.

## Files that exist only on one side

A set of IOKit DriverKit headers and their matching .iig.cpp files
(IOMemoryDescriptor, IOUserClient, IODMACommand, OSAction, and a dozen
others) exist only in our tree. These are the pre-generated output of
Apple's iig tool, which turns .iig interface-definition files into real
C++. We don't reliably have iig available at build time in this
environment, so these generated outputs were checked directly into source
once, rather than regenerated on every build. There's an earlier commit in
this repository's history literally titled "Vendor iig outputs into source
tree" that did exactly this.

A second small group, osfmk/kern/counters.c and counters.h, task_swap.c and
task_swap.h, and zcache.c and zcache_internal.h, along with a few ARM-only
files (caches_macros.s, amcc_rorgn.c, the arm64/tunables directory) exist
in our tree but not in this exact tag of upstream xnu. These read like
files that existed in a nearby xnu-7195.x point release our tree may have
originally been assembled from, rather than anything invented here, but
that's worth double-checking against a wider range of tags before
concluding anything more definite. The ARM ones in particular are almost
certainly just dead weight, since this project only targets x86_64.

Going the other direction, a handful of things exist in upstream
7195.121.3 but not in our tree: a make_symbol_aliasing.sh script,
kxld_copyright.c, upstream's own libsyscall and libkern/kmod directories,
a tests directory, and a MakeInc.color file for colorized build output.
None of these look load-bearing for a cross-build; they're either
Apple-tooling conveniences (colored output, a generated copyright banner)
or things PureDarwin builds through its own separate CMake targets instead
(libsyscall in particular is handled elsewhere in this repository, not
through xnu's own copy).
