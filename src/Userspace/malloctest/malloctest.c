/*
 * malloctest: isolate PureDarwin's "pointer being freed was not allocated"
 * abort (seen in both tcc and Xorg, never in helloapp).
 *
 * Each phase is announced BEFORE it runs and confirmed after, so whichever
 * line is last on the console when libmalloc aborts names the exact operation
 * that produced the bad pointer. Phases are ordered cheapest-to-nastiest and
 * deliberately cross the allocator boundaries we suspect:
 *
 *   1-3  plain malloc/free at sizes that straddle libmalloc's zone cutovers
 *        (nano zone -> tiny -> small -> large). A bad free here means the bug
 *        is squarely in libmalloc/its zone lookup, nothing to do with dyld.
 *   4    realloc growth across those same cutovers - realloc is where a zone
 *        change actually migrates a pointer, the classic place a stale zone
 *        association blows up.
 *   5    strdup/free - memory allocated inside libc, freed by the caller.
 *   6    getenv+strdup - strings that originated in the kernel/dyld-provided
 *        environment block rather than the heap.
 *   7    free of a pointer handed to us by dyld-adjacent APIs, which is the
 *        boundary we could not rule out by reading source.
 *
 * Deliberately no fancy output: printf+fflush only, because the abort is
 * asynchronous and anything buffered would be lost.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>

static void
step(const char *what)
{
	printf("[malloctest] %s ... ", what);
	fflush(stdout);
}

static void
ok(void)
{
	printf("ok\n");
	fflush(stdout);
}

int
main(void)
{
	/* Sizes chosen to land in each libmalloc zone class. */
	static const size_t sizes[] = {
		8, 16, 32, 64, 128, 240, 256, 512, 1008, 1024,
		4096, 15360, 16384, 32768, 131072, 1048576
	};
	const size_t nsizes = sizeof(sizes) / sizeof(sizes[0]);
	size_t i;

	printf("[malloctest] start\n");
	fflush(stdout);

	/* 1: malloc/free, one size at a time. */
	for (i = 0; i < nsizes; i++) {
		char buf[64];
		void *p;

		snprintf(buf, sizeof(buf), "malloc(%zu)+free", sizes[i]);
		step(buf);
		p = malloc(sizes[i]);
		if (p == NULL) {
			printf("FAILED (malloc returned NULL)\n");
			return 1;
		}
		memset(p, 0xA5, sizes[i]);
		free(p);
		ok();
	}

	/* 2: many live allocations at once, then free in order. */
	step("100 concurrent allocs, free in order");
	{
		void *ps[100];
		for (i = 0; i < 100; i++) {
			ps[i] = malloc((i % 32) * 37 + 1);
			if (ps[i] == NULL) {
				printf("FAILED (malloc returned NULL)\n");
				return 1;
			}
		}
		for (i = 0; i < 100; i++) {
			free(ps[i]);
		}
	}
	ok();

	/* 3: same, freed in reverse order. */
	step("100 concurrent allocs, free in reverse");
	{
		void *ps[100];
		for (i = 0; i < 100; i++) {
			ps[i] = malloc((i % 32) * 37 + 1);
			if (ps[i] == NULL) {
				printf("FAILED (malloc returned NULL)\n");
				return 1;
			}
		}
		for (i = 100; i > 0; i--) {
			free(ps[i - 1]);
		}
	}
	ok();

	/* 4: realloc growing across zone cutovers - prime suspect. */
	step("realloc growth chain 8 -> 1MiB");
	{
		void *p = malloc(8);
		if (p == NULL) {
			printf("FAILED (malloc returned NULL)\n");
			return 1;
		}
		for (i = 0; i < nsizes; i++) {
			void *q = realloc(p, sizes[i]);
			if (q == NULL) {
				printf("FAILED (realloc returned NULL at %zu)\n",
				    sizes[i]);
				free(p);
				return 1;
			}
			p = q;
			memset(p, 0x5A, sizes[i]);
		}
		free(p);
	}
	ok();

	/* 5: calloc/free. */
	step("calloc+free");
	{
		void *p = calloc(256, 64);
		if (p == NULL) {
			printf("FAILED (calloc returned NULL)\n");
			return 1;
		}
		free(p);
	}
	ok();

	/* 6: strdup - allocated inside libc, freed by us. */
	step("strdup+free");
	{
		char *s = strdup("puredarwin malloc boundary check");
		if (s == NULL) {
			printf("FAILED (strdup returned NULL)\n");
			return 1;
		}
		free(s);
	}
	ok();

	/* 7: strdup of an environment string (kernel/dyld-provided block). */
	step("strdup(getenv(\"PATH\"))+free");
	{
		const char *path = getenv("PATH");
		char *s = strdup(path != NULL ? path : "(unset)");
		if (s == NULL) {
			printf("FAILED (strdup returned NULL)\n");
			return 1;
		}
		free(s);
	}
	ok();

	/* 8: churn - alloc/free interleaved, the pattern a compiler/server hits. */
	step("10000 interleaved alloc/free");
	{
		void *keep[16];
		memset(keep, 0, sizeof(keep));
		for (i = 0; i < 10000; i++) {
			size_t slot = i % 16;
			size_t sz = sizes[i % nsizes];

			if (keep[slot] != NULL) {
				free(keep[slot]);
			}
			keep[slot] = malloc(sz);
			if (keep[slot] == NULL) {
				printf("FAILED (malloc returned NULL at %zu)\n", i);
				return 1;
			}
			memset(keep[slot], 0x3C, sz);
		}
		for (i = 0; i < 16; i++) {
			free(keep[i]);
		}
	}
	ok();

	/*
	 * Phases 1-8 above proved libmalloc itself is sound: every ordinary
	 * malloc/realloc/calloc/strdup/churn pattern round-trips cleanly. So the
	 * "pointer being freed was not allocated" abort in tcc and Xorg must come
	 * from a pointer libmalloc never handed out - i.e. a libc function that
	 * returns memory from somewhere else (static buffer, mmap/vm_allocate, or
	 * a second allocator) on an API whose contract says the CALLER must free.
	 *
	 * These are exactly those APIs. Each is isolated so the last line printed
	 * names the offending function. Note scandir/alphasort is high on the list:
	 * Xorg scans its config dirs with it, and PD was just missing
	 * _alphasort$INODE64 entirely - a sign that whole family is shaky.
	 */
	printf("[malloctest] --- caller-frees libc APIs ---\n");
	fflush(stdout);

	/* 9: strndup. */
	step("strndup+free");
	{
		char *s = strndup("puredarwin strndup boundary check", 12);
		if (s == NULL) {
			printf("FAILED (strndup returned NULL)\n");
			return 1;
		}
		free(s);
	}
	ok();

	/* 10: asprintf - libc mallocs the buffer for us. */
	step("asprintf+free");
	{
		char *s = NULL;
		int n = asprintf(&s, "pid=%d value=%s", (int)getpid(), "check");
		if (n < 0 || s == NULL) {
			printf("FAILED (asprintf failed)\n");
			return 1;
		}
		free(s);
	}
	ok();

	/* 11: getcwd(NULL, 0) - POSIX says this mallocs; caller frees. */
	step("getcwd(NULL,0)+free");
	{
		char *cwd = getcwd(NULL, 0);
		if (cwd == NULL) {
			printf("FAILED (getcwd returned NULL)\n");
			return 1;
		}
		printf("[cwd=%s] ", cwd);
		fflush(stdout);
		free(cwd);
	}
	ok();

	/* 12: realpath(p, NULL) - mallocs the result; caller frees. */
	step("realpath(\"/\", NULL)+free");
	{
		char *rp = realpath("/", NULL);
		if (rp == NULL) {
			printf("FAILED (realpath returned NULL)\n");
			return 1;
		}
		printf("[rp=%s] ", rp);
		fflush(stdout);
		free(rp);
	}
	ok();

	/* 13: scandir + alphasort - THE prime suspect. libc mallocs the namelist
	 * AND every entry in it; caller frees each entry and then the list. This
	 * is what Xorg does to enumerate its config/module dirs. */
	step("scandir(\"/\")+free entries+free list");
	{
		struct dirent **names = NULL;
		int n = scandir("/", &names, NULL, alphasort);
		int j;

		if (n < 0) {
			printf("FAILED (scandir failed)\n");
			return 1;
		}
		printf("[%d entries] ", n);
		fflush(stdout);
		for (j = 0; j < n; j++) {
			free(names[j]);
		}
		free(names);
	}
	ok();

	/*
	 * Phases 1-13 exonerated libmalloc AND libc's caller-frees APIs. The one
	 * thing left that Xorg does right before it aborts - and that nothing
	 * above touches - is dlopen().
	 *
	 * That matters because of a confirmed defect: gLibSystemHelpers is never
	 * assigned anywhere in dyld (dyld2.cpp:322 defines it NULL and nothing
	 * writes it), and "__dyld_register_thread_helpers" appears in no dyld
	 * func-lookup table, so the registration in pd_libSystem_init.c:175 -
	 * guarded by `reg != NULL` - silently no-ops. dyld::malloc (dyldNew.c:68)
	 * only forwards to libmalloc when gLibSystemHelpers != NULL AND
	 * gProcessInfo->libSystemInitialized; with helpers unregistered it serves
	 * every allocation from its static pool instead, for the life of the
	 * process. dlopen is when dyld starts allocating in earnest.
	 *
	 * So: dlopen, then exercise the heap, then dlclose. If the abort lands
	 * here, the dyld/libmalloc seam is confirmed and the fix is to make that
	 * registration actually happen.
	 */
	printf("[malloctest] --- dlopen / dyld allocator seam ---\n");
	fflush(stdout);

	/* 14: heap churn across a dlopen boundary. */
	step("dlopen(libSystem.B.dylib)");
	{
		void *h = dlopen("/usr/lib/libSystem.B.dylib", RTLD_LAZY);

		if (h == NULL) {
			printf("dlopen failed: %s\n",
			    dlerror() ? dlerror() : "(no dlerror)");
			/* Not fatal for the purposes of this test - keep going. */
		} else {
			ok();

			step("malloc/free after dlopen");
			{
				void *p = malloc(4096);
				if (p == NULL) {
					printf("FAILED (malloc returned NULL)\n");
					return 1;
				}
				memset(p, 0x77, 4096);
				free(p);
			}
			ok();

			step("strdup/free after dlopen");
			{
				char *s = strdup("post-dlopen heap check");
				if (s == NULL) {
					printf("FAILED (strdup returned NULL)\n");
					return 1;
				}
				free(s);
			}
			ok();

			step("dlsym");
			{
				void *sym = dlsym(h, "malloc");
				printf("[malloc@%p] ", sym);
				fflush(stdout);
			}
			ok();

			step("dlclose");
			dlclose(h);
			ok();

			step("malloc/free after dlclose");
			{
				void *p = malloc(65536);
				if (p == NULL) {
					printf("FAILED (malloc returned NULL)\n");
					return 1;
				}
				memset(p, 0x11, 65536);
				free(p);
			}
			ok();
		}
	}

	/* 15: repeated dlopen/dlclose with churn - drives dyld to grow its pool. */
	step("20x dlopen/dlclose with heap churn");
	{
		for (i = 0; i < 20; i++) {
			void *h = dlopen("/usr/lib/libSystem.B.dylib", RTLD_LAZY);
			void *p = malloc(8192);
			char *s = strdup("churn");

			if (p != NULL) {
				free(p);
			}
			if (s != NULL) {
				free(s);
			}
			if (h != NULL) {
				dlclose(h);
			}
		}
	}
	ok();

	/*
	 * Phase 14 dlopen'd libSystem.B.dylib - a DYLIB with no undefined symbols -
	 * and was clean. Xorg dies dlopen'ing something quite different: an
	 * MH_BUNDLE linked -undefined dynamic_lookup, whose undefineds (xf86DrvMsg,
	 * fbScreenInit, ...) dyld must resolve against the already-loaded main
	 * executable at load time. That is the case to reproduce.
	 *
	 * Here the undefineds will NOT resolve (malloctest exports no xf86 symbols),
	 * so this dlopen is expected to FAIL - the point is that it must fail
	 * *cleanly*, returning NULL, not abort in malloc. Both tcc (input file that
	 * does not exist) and Xorg (module load) blow up on a failure path, so a
	 * clean failure here vs. an abort tells us whether the bug is in dyld's
	 * bundle-load error path.
	 */
	printf("[malloctest] --- MH_BUNDLE dlopen (the Xorg case) ---\n");
	fflush(stdout);

	step("dlopen(puredarwingop_drv.so) [expected to FAIL cleanly]");
	{
		const char *drv =
		    "/usr/lib/xorg/modules/drivers/puredarwingop_drv.so";
		void *h = dlopen(drv, RTLD_LAZY);

		if (h == NULL) {
			const char *e = dlerror();
			printf("returned NULL (good), dlerror=\"%s\"\n",
			    e != NULL ? e : "(null)");
			fflush(stdout);
		} else {
			printf("loaded (h=%p) - undefineds resolved?!\n", h);
			fflush(stdout);
			dlclose(h);
		}
	}

	step("malloc/free after failed bundle dlopen");
	{
		void *p = malloc(4096);
		if (p == NULL) {
			printf("FAILED (malloc returned NULL)\n");
			return 1;
		}
		memset(p, 0x99, 4096);
		free(p);
	}
	ok();

	/*
	 * The other half of the pattern: tcc aborts when handed an input file that
	 * does not exist. Same shape - a failed open, then a bad free on the error
	 * path. Exercise the plain libc side of that.
	 */
	printf("[malloctest] --- failed-open error paths (the tcc case) ---\n");
	fflush(stdout);

	step("fopen(nonexistent) + perror + free");
	{
		FILE *f = fopen("/definitely/not/here/test.c", "r");

		if (f != NULL) {
			printf("FAILED (fopen unexpectedly succeeded)\n");
			fclose(f);
			return 1;
		}
		/* strerror on the failure, then a heap op, mirroring an error path. */
		printf("[errno-msg=%s] ", strerror(errno));
		fflush(stdout);
		{
			char *s = strdup("post-failed-open heap check");
			if (s == NULL) {
				printf("FAILED (strdup returned NULL)\n");
				return 1;
			}
			free(s);
		}
	}
	ok();

	step("dlopen(nonexistent .so) + dlerror + free");
	{
		void *h = dlopen("/definitely/not/here/nope.so", RTLD_LAZY);
		const char *e;

		if (h != NULL) {
			printf("FAILED (dlopen unexpectedly succeeded)\n");
			dlclose(h);
			return 1;
		}
		e = dlerror();
		printf("[dlerror=%s] ", e != NULL ? e : "(null)");
		fflush(stdout);
		{
			char *s = strdup("post-failed-dlopen heap check");
			if (s == NULL) {
				printf("FAILED (strdup returned NULL)\n");
				return 1;
			}
			free(s);
		}
	}
	ok();

	printf("[malloctest] ALL PHASES PASSED - allocator is clean for these patterns\n");
	fflush(stdout);
	return 0;
}
