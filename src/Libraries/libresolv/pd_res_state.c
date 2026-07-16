/*
 * Storage for the legacy global `_res` resolver-state struct that
 * resolv.h declares `extern` (see res_9_init()'s "_res_static = &_res"
 * path in res_data.c). Needs the real macro-renamed __res_9_state layout,
 * so this (unlike pd_res_compat.c) does include resolv.h - it doesn't
 * define any of the function names resolv.h's macros rename, so there's
 * no naming collision here.
 */
#include <resolv.h>

struct __res_state _res;
