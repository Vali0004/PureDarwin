/*
 * PureDarwin: dladdr() + the dyld public image-enumeration API it's built on.
 *
 * Real Darwin implements _dyld_image_count/_dyld_get_image_header/
 * _dyld_get_image_name/_dyld_get_image_vmaddr_slide/_dyld_get_image_slide in
 * dyld3/APIs.cpp, compiled into libdyld.dylib -- we don't compile that (huge,
 * closure-machinery-heavy) file, so these are missing from our libdyld even
 * though the loader itself (the `dyld` executable) has its own copies for
 * internal use. This file provides GENUINE implementations of the same public
 * ABI, built on `_dyld_get_all_image_infos()` (exported by our dyld -- see
 * mach-o/dyld_images.h's dyld_all_image_infos/dyld_image_info structs, which
 * dyld keeps up to date specifically so debuggers/tools like this can read it
 * without any private/internal dyld state).
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <mach-o/dyld.h>
#include <mach-o/dyld_images.h>
#include <mach-o/loader.h>
#include <dlfcn.h>

extern int _dyld_func_lookup(const char *name, void **address);

/*
 * _dyld_get_all_image_infos: no longer an exported dyld_priv.h symbol on real
 * Darwin either (per dyldAPIsInLibSystem.cpp's own comment: "this was in
 * dyld_priv.h but it is no longer exported") -- Apple's OWN dyld2-path
 * implementation resolves it via the exact same `_dyld_func_lookup(name,&p)`
 * mechanism our pd_libdyld_init.c already uses (successfully, confirmed at
 * boot) for "__dyld_register_thread_helpers". Mirrored verbatim here.
 */
static const struct dyld_all_image_infos *
_dyld_get_all_image_infos(void)
{
	typedef const struct dyld_all_image_infos *(*funcType)(void);
	static funcType p = NULL;
	if (p == NULL) {
		if (!_dyld_func_lookup("__dyld_get_all_image_infos", (void **)&p) || p == NULL)
			return NULL;
	}
	return p();
}

uint32_t
_dyld_image_count(void)
{
	const struct dyld_all_image_infos *infos = _dyld_get_all_image_infos();
	if (infos == NULL || infos->infoArray == NULL)
		return 0;
	return infos->infoArrayCount;
}

const struct mach_header *
_dyld_get_image_header(uint32_t image_index)
{
	const struct dyld_all_image_infos *infos = _dyld_get_all_image_infos();
	if (infos == NULL || infos->infoArray == NULL || image_index >= infos->infoArrayCount)
		return NULL;
	return infos->infoArray[image_index].imageLoadAddress;
}

const char *
_dyld_get_image_name(uint32_t image_index)
{
	const struct dyld_all_image_infos *infos = _dyld_get_all_image_infos();
	if (infos == NULL || infos->infoArray == NULL || image_index >= infos->infoArrayCount)
		return NULL;
	return infos->infoArray[image_index].imageFilePath;
}

static bool
addr_in_image(const struct mach_header *mh, intptr_t slide, const void *addr, const char **out_seg_end)
{
	bool is64 = (mh->magic == MH_MAGIC_64 || mh->magic == MH_CIGAM_64);
	const uint8_t *cmd = (const uint8_t *)mh + (is64 ? sizeof(struct mach_header_64) : sizeof(struct mach_header));
	uint32_t ncmds = mh->ncmds;
	for (uint32_t i = 0; i < ncmds; i++) {
		const struct load_command *lc = (const struct load_command *)cmd;
		if (is64 && lc->cmd == LC_SEGMENT_64) {
			const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
			uintptr_t start = (uintptr_t)sg->vmaddr + slide;
			uintptr_t end = start + sg->vmsize;
			if ((uintptr_t)addr >= start && (uintptr_t)addr < end) {
				if (out_seg_end)
					*out_seg_end = (const char *)end;
				return true;
			}
		} else if (!is64 && lc->cmd == LC_SEGMENT) {
			const struct segment_command *sg = (const struct segment_command *)lc;
			uintptr_t start = (uintptr_t)sg->vmaddr + slide;
			uintptr_t end = start + sg->vmsize;
			if ((uintptr_t)addr >= start && (uintptr_t)addr < end) {
				if (out_seg_end)
					*out_seg_end = (const char *)end;
				return true;
			}
		}
		cmd += lc->cmdsize;
	}
	return false;
}

/*
 * _dyld_get_image_vmaddr_slide: real Darwin computes this as
 * imageLoadAddress - (vmaddr of the image's own __TEXT segment as recorded on
 * disk). Since we already have to walk load commands for addr_in_image, reuse
 * that: the first LC_SEGMENT{,_64} named "__TEXT" gives us the on-disk vmaddr,
 * and imageLoadAddress is where the header actually landed -- the difference
 * is the slide.
 */
intptr_t
_dyld_get_image_vmaddr_slide(uint32_t image_index)
{
	const struct mach_header *mh = _dyld_get_image_header(image_index);
	if (mh == NULL)
		return 0;
	bool is64 = (mh->magic == MH_MAGIC_64 || mh->magic == MH_CIGAM_64);
	const uint8_t *cmd = (const uint8_t *)mh + (is64 ? sizeof(struct mach_header_64) : sizeof(struct mach_header));
	uint32_t ncmds = mh->ncmds;
	for (uint32_t i = 0; i < ncmds; i++) {
		const struct load_command *lc = (const struct load_command *)cmd;
		if (is64 && lc->cmd == LC_SEGMENT_64) {
			const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
			if (strcmp(sg->segname, "__TEXT") == 0)
				return (intptr_t)((uintptr_t)mh - (uintptr_t)sg->vmaddr);
		} else if (!is64 && lc->cmd == LC_SEGMENT) {
			const struct segment_command *sg = (const struct segment_command *)lc;
			if (strcmp(sg->segname, "__TEXT") == 0)
				return (intptr_t)((uintptr_t)mh - (uintptr_t)sg->vmaddr);
		}
		cmd += lc->cmdsize;
	}
	return 0;
}

/*
 * dladdr(): walk each loaded image's segments to find which one contains
 * `addr`, filling dli_fname/dli_fbase. Nearest-symbol resolution (dli_sname/
 * dli_saddr) is left unset -- we have no symbol table walker here -- which is
 * a standard partial-success dladdr result, not a lie: libc's only caller
 * (atexit.c, to tag which image registered a handler) only reads dli_fbase.
 */
int
dladdr(const void *addr, Dl_info *info)
{
	uint32_t count = _dyld_image_count();
	for (uint32_t i = 0; i < count; i++) {
		const struct mach_header *mh = _dyld_get_image_header(i);
		if (mh == NULL)
			continue;
		intptr_t slide = _dyld_get_image_vmaddr_slide(i);
		if (addr_in_image(mh, slide, addr, NULL)) {
			info->dli_fname = _dyld_get_image_name(i);
			info->dli_fbase = (void *)mh;
			info->dli_sname = NULL;
			info->dli_saddr = NULL;
			return 1;
		}
	}
	return 0;
}

/*
 * dyld_image_path_containing_address (mach-o/dyld_priv.h): CoreFoundation's
 * CFBundle_Binary.c uses this to figure out which loaded image a function
 * pointer came from. Same image-enumeration walk as dladdr() above, just
 * returning the path directly instead of filling a Dl_info.
 */
const char *
dyld_image_path_containing_address(const void *addr)
{
	uint32_t count = _dyld_image_count();
	for (uint32_t i = 0; i < count; i++) {
		const struct mach_header *mh = _dyld_get_image_header(i);
		if (mh == NULL)
			continue;
		intptr_t slide = _dyld_get_image_vmaddr_slide(i);
		if (addr_in_image(mh, slide, addr, NULL))
			return _dyld_get_image_name(i);
	}
	return NULL;
}

/*
 * _dyld_get_image_slide (mach-o/dyld_priv.h): same image-enumeration walk,
 * matching by mach_header pointer identity instead of a contained address.
 * libmalloc's vm.c/magazine_malloc.c (mvm_aslr_enabled) call this on their own
 * image's header to detect whether ASLR actually slid anything (slide==0 =>
 * static/no-PIE image => treat as "ASLR disabled" for nano-zone heuristics).
 */
intptr_t
_dyld_get_image_slide(const struct mach_header *mh)
{
	uint32_t count = _dyld_image_count();
	for (uint32_t i = 0; i < count; i++) {
		if (_dyld_get_image_header(i) == mh)
			return _dyld_get_image_vmaddr_slide(i);
	}
	return 0;
}

/*
 * dyld_process_is_restricted: real Apple resolves this via the exact same
 * _dyld_func_lookup("__dyld_process_is_restricted", &p) pattern as
 * _dyld_get_all_image_infos above (dyldAPIsInLibSystem.cpp:1992-2003).
 * Restriction (SIP-style: strip DYLD_* env vars, etc) is a security policy
 * decision the LOADER (our `dyld` executable) already makes and exposes this
 * way; mirrored verbatim.
 */
bool
dyld_process_is_restricted(void)
{
	typedef bool (*funcType)(void);
	static funcType p = NULL;
	if (p == NULL) {
		if (!_dyld_func_lookup("__dyld_process_is_restricted", (void **)&p) || p == NULL)
			return false;
	}
	return p();
}

/*
 * NSVersionOfLinkTimeLibrary: real Apple (dyld2 path) walks the MAIN
 * executable's LC_LOAD_DYLIB commands for one whose install-name basename
 * matches `libraryName`, returning its recorded current_version. We use our
 * own _dyld_get_image_header(0) (the main executable is always image index 0)
 * instead of _NSGetMachExecuteHeader() -- equivalent, and we don't carry that
 * separate symbol in libdyld. Returns -1 if not found, matching real Darwin.
 */
int32_t
NSVersionOfLinkTimeLibrary(const char *libraryName)
{
	const struct mach_header *mh = _dyld_get_image_header(0);
	if (mh == NULL || libraryName == NULL)
		return -1;
	bool is64 = (mh->magic == MH_MAGIC_64 || mh->magic == MH_CIGAM_64);
	const uint8_t *cmd = (const uint8_t *)mh + (is64 ? sizeof(struct mach_header_64) : sizeof(struct mach_header));
	uint32_t ncmds = mh->ncmds;
	size_t namelen = strlen(libraryName);
	for (uint32_t i = 0; i < ncmds; i++) {
		const struct load_command *lc = (const struct load_command *)cmd;
		if (lc->cmd == LC_LOAD_DYLIB || lc->cmd == LC_LOAD_WEAK_DYLIB
		    || lc->cmd == LC_REEXPORT_DYLIB || lc->cmd == LC_LOAD_UPWARD_DYLIB) {
			const struct dylib_command *dc = (const struct dylib_command *)lc;
			const char *installName = (const char *)lc + dc->dylib.name.offset;
			/* match on the trailing path component, like real Darwin's basename compare */
			const char *base = strrchr(installName, '/');
			base = base ? base + 1 : installName;
			if (strncmp(base, libraryName, namelen) == 0)
				return (int32_t)dc->dylib.current_version;
		}
		cmd += lc->cmdsize;
	}
	return -1;
}
