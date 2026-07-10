/*
 * pd_libdyld_exports.c -- bridges libdyld.dylib's PRIVATE (hidden-visibility,
 * by deliberate design -- see start_glue.s/dyldLibSystemGlue.c) ABI surface to
 * libSystem.B.dylib, which now carries the dyld<->libSystem handshake
 * constructor (pd_libSystem_init.c, moved there because dyld only permits an
 * image's initializer to run before libSystemInitialized if that image's
 * install path is libSystem.B.dylib's own -- see that file's own comment).
 *
 * `start` (start_glue.s) is `.private_extern` -- do NOT change that visibility
 * directly: it is shared with the `dyld` executable target too, and the last
 * time this project renamed/touched `_start` there it broke dyld's own build.
 * Instead expose its address via an ordinary (default-visibility) data symbol
 * defined HERE, inside libdyld.dylib, where referencing the hidden `start`
 * symbol directly is legal (same image).
 */
extern void start(void);
void *pd_libdyld_getStartGlueToCallExit(void) { return (void *)&start; }

extern int _dyld_func_lookup(const char *name, void **address);

__attribute__((visibility("hidden")))
void *pd_dyld_fast_stub_entry(void *loadercache, long lazyinfo)
    __asm__("__Z21_dyld_fast_stub_entryPvl");

void *pd_dyld_fast_stub_entry(void *loadercache, long lazyinfo)
{
	typedef void *(*func_t)(void *, long);
	static func_t fast_stub_entry;

	if (fast_stub_entry == 0) {
		void *address = 0;
		if (_dyld_func_lookup("__dyld_fast_stub_entry", &address) != 0)
			fast_stub_entry = (func_t)address;
	}

	if (fast_stub_entry == 0)
		return 0;

	return fast_stub_entry(loadercache, lazyinfo);
}
