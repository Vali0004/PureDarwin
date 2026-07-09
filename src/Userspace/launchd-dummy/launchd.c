static long
pd_syscall3(long num, long a0, long a1, long a2)
{
	long ret;
	__asm__ volatile(
	    "movq %1, %%rax\n\t"
	    "movq %2, %%rdi\n\t"
	    "movq %3, %%rsi\n\t"
	    "movq %4, %%rdx\n\t"
	    "syscall\n\t"
	    : "=a"(ret)
	    : "r"(0x2000000L + num), "r"(a0), "r"(a1), "r"(a2)
	    : "rdi", "rsi", "rdx", "rcx", "r11", "memory");
	return ret;
}

#define SYS_mount 167
#define SYS_open    5
#define SYS_write   4

#define O_WRONLY  0x0001
#define O_NOCTTY  0x20000

void
_start(void)
{
	static const char s_devfs[] = "devfs";
	static const char s_dev[]   = "/dev";
	static const char s_cons[]  = "/dev/console";
	static const char msg[]     = "hello from /bin/launchd via dyld\n";

	/* A fresh init process starts with NO open file descriptors -- a bare
	 * write(1, ...) silently fails EBADF with nothing ever opened on fd 1
	 * (this is why the earlier version produced no output despite not
	 * crashing). Mount devfs (a RAM fs, so it works even while root is
	 * still read-only) so /dev/console appears, then open+write to it --
	 * same pattern as the known-working userland-test/hello.c probe. */
	pd_syscall3(SYS_mount, (long)s_devfs, (long)s_dev, 0);

	long fd = pd_syscall3(SYS_open, (long)s_cons, O_WRONLY | O_NOCTTY, 0);

	pd_syscall3(SYS_write, fd, (long)msg, sizeof(msg) - 1);

	for (;;) {
		__asm__ volatile("pause");
	}
}
