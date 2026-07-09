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
