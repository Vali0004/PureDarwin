/*
 * Minimal real dispatch_once/dispatch_once_f.
 *
 * PureDarwin does not build libdispatch yet (its whole subdirectory is
 * commented out of the CMake tree - no CMakeLists.txt exists for it), so
 * callers that reference dispatch_once (Xvfb, among others) link against
 * this instead: a genuinely thread-safe once-gate built on pthread mutex and
 * condition variable, matching the predicate encoding <dispatch/once.h>'s
 * inline fastpath expects (0 = not started, ~0 = done, anything else = in
 * progress).
 *
 * Types are declared locally rather than by including <dispatch/dispatch.h>
 * to avoid pulling in the rest of libdispatch's header tree, which is not
 * wired into this build. dispatch_block_t is invoked through the raw Block
 * ABI (isa/flags/reserved/invoke, per BlocksRuntime/Block_private.h) rather
 * than block-call syntax, so this file itself does not need -fblocks.
 */
#include <pthread.h>
#include <stdint.h>

typedef intptr_t dispatch_once_t;
typedef void (*dispatch_function_t)(void *);
typedef void *dispatch_block_t;

struct pd_block_layout {
	void *isa;
	int flags;
	int reserved;
	void (*invoke)(void *, ...);
};

static pthread_mutex_t pd_once_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pd_once_cond = PTHREAD_COND_INITIALIZER;

void
dispatch_once_f(dispatch_once_t *predicate, void *context,
    dispatch_function_t function)
{
	pthread_mutex_lock(&pd_once_lock);
	while (*predicate != 0 && *predicate != ~(intptr_t)0) {
		pthread_cond_wait(&pd_once_cond, &pd_once_lock);
	}
	if (*predicate == 0) {
		*predicate = 1;
		pthread_mutex_unlock(&pd_once_lock);

		function(context);

		pthread_mutex_lock(&pd_once_lock);
		*predicate = ~(intptr_t)0;
		pthread_cond_broadcast(&pd_once_cond);
	}
	pthread_mutex_unlock(&pd_once_lock);
}

static void
pd_dispatch_once_invoke(void *ctx)
{
	struct pd_block_layout *block = (struct pd_block_layout *)ctx;
	((void (*)(void *))block->invoke)(block);
}

void
dispatch_once(dispatch_once_t *predicate, dispatch_block_t block)
{
	dispatch_once_f(predicate, (void *)block, pd_dispatch_once_invoke);
}
