#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <grp.h>
#include <sys/mount.h>
#include <sys/statvfs.h>

extern int __pd_sys_pause(void) __asm("___pause");
extern pid_t __pd_sys_waitpid(pid_t pid, int *status, int options) __asm("___waitpid");
extern int _dyld_func_lookup(const char *name, void **address);
extern FILE *__pd_fdopen_extsn(int fd, const char *mode) __asm("_fdopen$DARWIN_EXTSN");
extern FILE *__pd_fopen_extsn(const char *path, const char *mode) __asm("_fopen$DARWIN_EXTSN");

typedef unsigned long long pd_dispatch_time_t;

#define PD_DISPATCH_TIME_FOREVER (~0ULL)
#define PD_NSEC_PER_SEC 1000000000ULL

struct pd_dispatch_semaphore {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    long value;
};

extern int __pd_chmod_unix2003(const char *path, mode_t mode) __asm("_chmod$UNIX2003");
extern int __pd_fchmod_unix2003(int fd, mode_t mode) __asm("_fchmod$UNIX2003");
extern int __pd_fcntl_syscall(int fd, int cmd, long arg) __asm("___fcntl");
extern int __pd_getrlimit_unix2003(int resource, struct rlimit *rlp) __asm("_getrlimit$UNIX2003");
extern int __pd_kill_unix2003(pid_t pid, int sig) __asm("_kill$UNIX2003");
extern void *__pd_mmap_unix2003(void *addr, size_t len, int prot, int flags, int fd, off_t offset) __asm("_mmap$UNIX2003");
extern int __pd_mprotect_default(void *addr, size_t len, int prot) __asm("_mprotect");
extern int __pd_munmap_unix2003(void *addr, size_t len) __asm("_munmap$UNIX2003");
extern int __pd_open_unix2003(const char *path, int flags, mode_t mode) __asm("_open$UNIX2003");
extern int __pd_setrlimit_unix2003(int resource, const struct rlimit *rlp) __asm("_setrlimit$UNIX2003");
extern int __pd_sigsuspend_syscall(const sigset_t *set) __asm("___sigsuspend");
extern int __pd_nanosleep(const struct timespec *requested, struct timespec *remaining) __asm("_nanosleep");
extern ssize_t __pd_write_default_syscall(int fd, const void *buf, size_t nbyte) __asm("_write");

extern int __pd_pthread_cancel_unix2003(pthread_t thread) __asm("_pthread_cancel$UNIX2003");
extern int __pd_pthread_cond_init_unix2003(pthread_cond_t *cond, const pthread_condattr_t *attr) __asm("_pthread_cond_init$UNIX2003");
extern int __pd_pthread_cond_timedwait_unix2003(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime) __asm("_pthread_cond_timedwait$UNIX2003");
extern int __pd_pthread_cond_wait_unix2003(pthread_cond_t *cond, pthread_mutex_t *mutex) __asm("_pthread_cond_wait$UNIX2003");
extern int __pd_pthread_join_unix2003(pthread_t thread, void **value_ptr) __asm("_pthread_join$UNIX2003");
extern int __pd_pthread_mutexattr_destroy_unix2003(pthread_mutexattr_t *attr) __asm("_pthread_mutexattr_destroy$UNIX2003");
extern int __pd_pthread_rwlock_destroy_unix2003(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_destroy$UNIX2003");
extern int __pd_pthread_rwlock_init_unix2003(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr) __asm("_pthread_rwlock_init$UNIX2003");
extern int __pd_pthread_rwlock_rdlock_unix2003(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_rdlock$UNIX2003");
extern int __pd_pthread_rwlock_tryrdlock_unix2003(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_tryrdlock$UNIX2003");
extern int __pd_pthread_rwlock_trywrlock_unix2003(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_trywrlock$UNIX2003");
extern int __pd_pthread_rwlock_unlock_unix2003(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_unlock$UNIX2003");
extern int __pd_pthread_rwlock_wrlock_unix2003(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_wrlock$UNIX2003");
extern int __pd_pthread_setcancelstate_unix2003(int state, int *oldstate) __asm("_pthread_setcancelstate$UNIX2003");
extern int __pd_pthread_setcanceltype_unix2003(int type, int *oldtype) __asm("_pthread_setcanceltype$UNIX2003");
extern int __pd_pthread_sigmask_unix2003(int how, const sigset_t *set, sigset_t *oset) __asm("_pthread_sigmask$UNIX2003");
extern void __pd_pthread_testcancel_unix2003(void) __asm("_pthread_testcancel$UNIX2003");

int
pause(void)
{
    return __pd_sys_pause();
}

pid_t
waitpid(pid_t pid, int *status, int options)
{
    return __pd_sys_waitpid(pid, status, options);
}

int __pd_chmod_default(const char *path, mode_t mode) __asm("_chmod");
int
__pd_chmod_default(const char *path, mode_t mode)
{
    return __pd_chmod_unix2003(path, mode);
}

int __pd_fchmod_default(int fd, mode_t mode) __asm("_fchmod");
int
__pd_fchmod_default(int fd, mode_t mode)
{
    return __pd_fchmod_unix2003(fd, mode);
}

int __pd_fcntl_default(int fd, int cmd, ...) __asm("_fcntl");
int
__pd_fcntl_default(int fd, int cmd, ...)
{
    va_list ap;
    long arg;

    va_start(ap, cmd);
    arg = va_arg(ap, long);
    va_end(ap);
    return __pd_fcntl_syscall(fd, cmd, arg);
}

int __pd_getrlimit_default(int resource, struct rlimit *rlp) __asm("_getrlimit");
int
__pd_getrlimit_default(int resource, struct rlimit *rlp)
{
    return __pd_getrlimit_unix2003(resource, rlp);
}

int __pd_kill_default(pid_t pid, int sig) __asm("_kill");
int
__pd_kill_default(pid_t pid, int sig)
{
    return __pd_kill_unix2003(pid, sig);
}

void *__pd_mmap_default(void *addr, size_t len, int prot, int flags, int fd, off_t offset) __asm("_mmap");
void *
__pd_mmap_default(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
    return __pd_mmap_unix2003(addr, len, prot, flags, fd, offset);
}

int __pd_mprotect_unix2003(void *addr, size_t len, int prot) __asm("_mprotect$UNIX2003");
int
__pd_mprotect_unix2003(void *addr, size_t len, int prot)
{
    return __pd_mprotect_default(addr, len, prot);
}

int __pd_munmap_default(void *addr, size_t len) __asm("_munmap");
int
__pd_munmap_default(void *addr, size_t len)
{
    return __pd_munmap_unix2003(addr, len);
}

int __pd_open_default(const char *path, int flags, ...) __asm("_open");
int
__pd_open_default(const char *path, int flags, ...)
{
    va_list ap;
    mode_t mode = 0;

    if (flags & O_CREAT) {
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    return __pd_open_unix2003(path, flags, mode);
}

int __pd_setrlimit_default(int resource, const struct rlimit *rlp) __asm("_setrlimit");
int
__pd_setrlimit_default(int resource, const struct rlimit *rlp)
{
    return __pd_setrlimit_unix2003(resource, rlp);
}

int __pd_sigsuspend_default(const sigset_t *set) __asm("_sigsuspend");
int
__pd_sigsuspend_default(const sigset_t *set)
{
    return __pd_sigsuspend_syscall(set);
}

unsigned int __pd_sleep_default(unsigned int seconds) __asm("_sleep");
unsigned int
__pd_sleep_default(unsigned int seconds)
{
    struct timespec requested = { (time_t)seconds, 0 };
    struct timespec remaining = { 0, 0 };

    while (__pd_nanosleep(&requested, &remaining) == -1) {
        if (errno != EINTR) {
            return requested.tv_sec;
        }
        requested = remaining;
    }
    return 0;
}

unsigned int __pd_sleep_unix2003(unsigned int seconds) __asm("_sleep$UNIX2003");
unsigned int
__pd_sleep_unix2003(unsigned int seconds)
{
    return __pd_sleep_default(seconds);
}

ssize_t __pd_write_unix2003(int fd, const void *buf, size_t nbyte) __asm("_write$UNIX2003");
ssize_t
__pd_write_unix2003(int fd, const void *buf, size_t nbyte)
{
    return __pd_write_default_syscall(fd, buf, nbyte);
}

int __pd_pthread_cancel_default(pthread_t thread) __asm("_pthread_cancel");
int
__pd_pthread_cancel_default(pthread_t thread)
{
    return __pd_pthread_cancel_unix2003(thread);
}

int __pd_pthread_cond_init_default(pthread_cond_t *cond, const pthread_condattr_t *attr) __asm("_pthread_cond_init");
int
__pd_pthread_cond_init_default(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    return __pd_pthread_cond_init_unix2003(cond, attr);
}

int __pd_pthread_cond_timedwait_default(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime) __asm("_pthread_cond_timedwait");
int
__pd_pthread_cond_timedwait_default(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime)
{
    return __pd_pthread_cond_timedwait_unix2003(cond, mutex, abstime);
}

int __pd_pthread_cond_wait_default(pthread_cond_t *cond, pthread_mutex_t *mutex) __asm("_pthread_cond_wait");
int
__pd_pthread_cond_wait_default(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    return __pd_pthread_cond_wait_unix2003(cond, mutex);
}

int __pd_pthread_join_default(pthread_t thread, void **value_ptr) __asm("_pthread_join");
int
__pd_pthread_join_default(pthread_t thread, void **value_ptr)
{
    return __pd_pthread_join_unix2003(thread, value_ptr);
}

int __pd_pthread_mutexattr_destroy_default(pthread_mutexattr_t *attr) __asm("_pthread_mutexattr_destroy");
int
__pd_pthread_mutexattr_destroy_default(pthread_mutexattr_t *attr)
{
    return __pd_pthread_mutexattr_destroy_unix2003(attr);
}

int __pd_pthread_rwlock_destroy_default(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_destroy");
int
__pd_pthread_rwlock_destroy_default(pthread_rwlock_t *rwlock)
{
    return __pd_pthread_rwlock_destroy_unix2003(rwlock);
}

int __pd_pthread_rwlock_init_default(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr) __asm("_pthread_rwlock_init");
int
__pd_pthread_rwlock_init_default(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr)
{
    return __pd_pthread_rwlock_init_unix2003(rwlock, attr);
}

int __pd_pthread_rwlock_rdlock_default(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_rdlock");
int
__pd_pthread_rwlock_rdlock_default(pthread_rwlock_t *rwlock)
{
    return __pd_pthread_rwlock_rdlock_unix2003(rwlock);
}

int __pd_pthread_rwlock_tryrdlock_default(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_tryrdlock");
int
__pd_pthread_rwlock_tryrdlock_default(pthread_rwlock_t *rwlock)
{
    return __pd_pthread_rwlock_tryrdlock_unix2003(rwlock);
}

int __pd_pthread_rwlock_trywrlock_default(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_trywrlock");
int
__pd_pthread_rwlock_trywrlock_default(pthread_rwlock_t *rwlock)
{
    return __pd_pthread_rwlock_trywrlock_unix2003(rwlock);
}

int __pd_pthread_rwlock_unlock_default(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_unlock");
int
__pd_pthread_rwlock_unlock_default(pthread_rwlock_t *rwlock)
{
    return __pd_pthread_rwlock_unlock_unix2003(rwlock);
}

int __pd_pthread_rwlock_wrlock_default(pthread_rwlock_t *rwlock) __asm("_pthread_rwlock_wrlock");
int
__pd_pthread_rwlock_wrlock_default(pthread_rwlock_t *rwlock)
{
    return __pd_pthread_rwlock_wrlock_unix2003(rwlock);
}

int __pd_pthread_setcancelstate_default(int state, int *oldstate) __asm("_pthread_setcancelstate");
int
__pd_pthread_setcancelstate_default(int state, int *oldstate)
{
    return __pd_pthread_setcancelstate_unix2003(state, oldstate);
}

int __pd_pthread_setcanceltype_default(int type, int *oldtype) __asm("_pthread_setcanceltype");
int
__pd_pthread_setcanceltype_default(int type, int *oldtype)
{
    return __pd_pthread_setcanceltype_unix2003(type, oldtype);
}

int __pd_pthread_sigmask_default(int how, const sigset_t *set, sigset_t *oset) __asm("_pthread_sigmask");
int
__pd_pthread_sigmask_default(int how, const sigset_t *set, sigset_t *oset)
{
    return __pd_pthread_sigmask_unix2003(how, set, oset);
}

void __pd_pthread_testcancel_default(void) __asm("_pthread_testcancel");
void
__pd_pthread_testcancel_default(void)
{
    __pd_pthread_testcancel_unix2003();
}

uint32_t
notify_check(int token, int *check)
{
    (void)token;
    if (check != NULL) {
        *check = 0;
    }
    return 0;
}

uint32_t
notify_register_check(const char *name, int *out_token)
{
    (void)name;
    if (out_token != NULL) {
        *out_token = 0;
    }
    return 0;
}

uint32_t
notify_monitor_file(int token, const char *path, int flags)
{
    (void)token;
    (void)path;
    (void)flags;
    return 0;
}

uint32_t
notify_cancel(int token)
{
    (void)token;
    return 0;
}

FILE *
__pd_fdopen_extsn(int fd, const char *mode)
{
    return fdopen(fd, mode);
}

FILE *
__pd_fopen_extsn(const char *path, const char *mode)
{
    return fopen(path, mode);
}

size_t
fwrite(const void *ptr, size_t size, size_t count, FILE *stream)
{
    const unsigned char *bytes = ptr;
    size_t total = size * count;
    size_t written;

    if (size != 0 && count > ((size_t)-1 / size)) {
        errno = EINVAL;
        return 0;
    }

    for (written = 0; written < total; written++) {
        if (fputc(bytes[written], stream) == EOF) {
            break;
        }
    }

    return size == 0 ? count : written / size;
}

int
remove(const char *path)
{
    if (unlink(path) == 0) {
        return 0;
    }

    if (errno == EISDIR || errno == EPERM) {
        return rmdir(path);
    }

    return -1;
}

double
ldexp(double value, int exponent)
{
    return __builtin_ldexp(value, exponent);
}

int
dlclose(void *handle)
{
    typedef int (*dlclose_fn)(void *);
    static dlclose_fn fn;

    if (fn == NULL && !_dyld_func_lookup("__dyld_dlclose", (void **)&fn)) {
        return -1;
    }

    return fn(handle);
}

void *
dlopen(const char *path, int mode)
{
    typedef void *(*dlopen_fn)(const char *, int, void *);
    static dlopen_fn fn;

    if (fn == NULL && !_dyld_func_lookup("__dyld_dlopen_internal", (void **)&fn)) {
        return NULL;
    }

    return fn(path, mode, __builtin_return_address(0));
}

void *
dlsym(void *handle, const char *symbol)
{
    typedef void *(*dlsym_fn)(void *, const char *, void *);
    static dlsym_fn fn;

    if (fn == NULL && !_dyld_func_lookup("__dyld_dlsym_internal", (void **)&fn)) {
        return NULL;
    }

    return fn(handle, symbol, __builtin_return_address(0));
}

char *
dlerror(void)
{
    typedef char *(*dlerror_fn)(void);
    static dlerror_fn fn;

    if (fn == NULL && !_dyld_func_lookup("__dyld_dlerror", (void **)&fn)) {
        return NULL;
    }

    return fn();
}

void *
dispatch_semaphore_create(long value)
{
    struct pd_dispatch_semaphore *semaphore;

    if (value < 0) {
        return NULL;
    }

    semaphore = calloc(1, sizeof(*semaphore));
    if (semaphore == NULL) {
        return NULL;
    }

    if (pthread_mutex_init(&semaphore->lock, NULL) != 0) {
        free(semaphore);
        return NULL;
    }

    if (pthread_cond_init(&semaphore->cond, NULL) != 0) {
        pthread_mutex_destroy(&semaphore->lock);
        free(semaphore);
        return NULL;
    }

    semaphore->value = value;
    return semaphore;
}

long
dispatch_semaphore_signal(void *object)
{
    struct pd_dispatch_semaphore *semaphore = object;

    if (semaphore == NULL) {
        return -1;
    }

    pthread_mutex_lock(&semaphore->lock);
    semaphore->value++;
    if (semaphore->value <= 0) {
        pthread_cond_signal(&semaphore->cond);
        pthread_mutex_unlock(&semaphore->lock);
        return 1;
    }
    pthread_mutex_unlock(&semaphore->lock);
    return 0;
}

long
dispatch_semaphore_wait(void *object, pd_dispatch_time_t timeout)
{
    struct pd_dispatch_semaphore *semaphore = object;
    long result = 0;
    struct timespec abstime;

    if (semaphore == NULL) {
        return -1;
    }

    pthread_mutex_lock(&semaphore->lock);
    semaphore->value--;
    if (semaphore->value >= 0) {
        pthread_mutex_unlock(&semaphore->lock);
        return 0;
    }

    if (timeout == 0) {
        semaphore->value++;
        pthread_mutex_unlock(&semaphore->lock);
        return 1;
    }

    if (timeout != PD_DISPATCH_TIME_FOREVER) {
        struct timeval now;
        uint64_t deadline_sec;
        uint64_t deadline_nsec;

        gettimeofday(&now, NULL);
        deadline_sec = timeout / PD_NSEC_PER_SEC;
        deadline_nsec = timeout % PD_NSEC_PER_SEC;

        /*
         * libdispatch encodes dispatch_time(DISPATCH_TIME_NOW, delta) as an
         * absolute nanosecond deadline. For early clients that pass a small
         * relative value directly, treat it as a delta from now.
         */
        if (deadline_sec < (uint64_t)now.tv_sec) {
            deadline_sec += (uint64_t)now.tv_sec;
            deadline_nsec += (uint64_t)now.tv_usec * 1000ULL;
            if (deadline_nsec >= PD_NSEC_PER_SEC) {
                deadline_sec++;
                deadline_nsec -= PD_NSEC_PER_SEC;
            }
        }

        abstime.tv_sec = (time_t)deadline_sec;
        abstime.tv_nsec = (long)deadline_nsec;
    }

    while (semaphore->value < 0) {
        int err;

        if (timeout == PD_DISPATCH_TIME_FOREVER) {
            err = pthread_cond_wait(&semaphore->cond, &semaphore->lock);
        } else {
            err = pthread_cond_timedwait(&semaphore->cond, &semaphore->lock, &abstime);
        }
        if (err != 0) {
            semaphore->value++;
            result = err == ETIMEDOUT ? 1 : -1;
            break;
        }
    }

    pthread_mutex_unlock(&semaphore->lock);
    return result;
}

long
atol(const char *str)
{
    return strtol(str, NULL, 10);
}

char *index(const char *s, int c)
{
    return __builtin_strchr(s, c);
}

/*
 * Minimal terminfo shims. On real Darwin these live in libncurses; PureDarwin
 * has neither an ncurses library nor a compiled terminfo database. xterm links
 * ncurses' setupterm()/tigetstr()/del_curterm()/cur_term for its *optional*
 * termcap-query and function-key feature (xtermcap.c, OPT_TCAP_*), and dyld
 * aborts at load if these flat-namespace symbols resolve nowhere. Provide just
 * enough to satisfy the linker and behave as "no terminal capabilities found":
 * setupterm succeeds so xterm's TcapInit passes, but every tigetstr() lookup
 * reports the capability absent, so no bogus escape sequences are invented.
 */
void *cur_term = NULL;

int
setupterm(const char *term, int filedes, int *errret)
{
    (void)term;
    (void)filedes;
    if (errret != NULL) {
        *errret = 1;            /* 1 == success, per the terminfo convention */
    }
    return 0;                   /* OK */
}

char *
tigetstr(const char *capname)
{
    (void)capname;
    return (char *)-1;          /* (char *)-1 == capability absent/cancelled */
}

int
del_curterm(void *oterm)
{
    (void)oterm;
    return 0;                   /* OK */
}

/* ncurses use_extended_names(): xterm calls it to toggle extended terminfo
 * capability names. With no terminfo backend there is nothing to toggle. */
int
use_extended_names(int enable)
{
    (void)enable;
    return 0;
}

/*
 * The rest of this file back-fills libc/libutil/libinfo functions that xterm
 * references but PureDarwin's static libc archives don't yet provide, so the
 * flat-namespace symbols it defers at link time actually resolve at load
 * instead of aborting dyld. Where a real implementation is cheap and correct
 * (alarm, the *_r passwd wrappers, openpty) we provide it; where the feature is
 * non-essential to xterm coming up (usershell iteration, reverse DNS) we
 * provide a minimal well-behaved stub.
 */

#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <netdb.h>
#include <pwd.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

unsigned int
alarm(unsigned int seconds)
{
    struct itimerval new_it;
    struct itimerval old_it;

    new_it.it_value.tv_sec = (time_t)seconds;
    new_it.it_value.tv_usec = 0;
    new_it.it_interval.tv_sec = 0;
    new_it.it_interval.tv_usec = 0;

    if (setitimer(ITIMER_REAL, &new_it, &old_it) < 0) {
        return 0;
    }

    /* Round any residual fraction up, as POSIX alarm() reports whole seconds. */
    return (unsigned int)old_it.it_value.tv_sec
        + (old_it.it_value.tv_usec != 0 ? 1u : 0u);
}

void
arc4random_buf(void *buf, size_t nbytes)
{
    /*
     * Not a cryptographic source - PureDarwin has no arc4random engine yet.
     * A splitmix64-seeded xorshift keeps xterm's callers (temp-name salting,
     * not security-critical here) fed with non-degenerate bytes.
     */
    static uint64_t state;
    unsigned char *out = buf;
    size_t i;

    if (state == 0) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        state = ((uint64_t)tv.tv_sec << 20) ^ (uint64_t)tv.tv_usec
            ^ ((uint64_t)(uintptr_t)&state);
        if (state == 0) {
            state = 0x9e3779b97f4a7c15ULL;
        }
    }

    for (i = 0; i < nbytes; i++) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        out[i] = (unsigned char)(state >> 24);
    }
}

int
wcwidth(wchar_t wc)
{
    if (wc == 0) {
        return 0;
    }
    if (wc < 0x20 || (wc >= 0x7f && wc < 0xa0)) {
        return -1;              /* C0/C1 control characters */
    }
    /* No East-Asian-width tables here; treat everything printable as 1 cell.
     * xterm's own OPT_WIDE_CHARS logic refines this when enabled. */
    return 1;
}

/* Group- and shell-database iteration xterm touches on its setuid path (which
 * PureDarwin builds with --disable-setuid). Minimal, well-behaved stubs. */
void
endgrent(void)
{
}

char *
getusershell(void)
{
    return NULL;                /* empty shell list -> caller uses its default */
}

void
endusershell(void)
{
}

int
initgroups(const char *name, int basegid)
{
    (void)name;
    (void)basegid;
    return 0;
}

struct hostent *
gethostbyaddr(const void *addr, socklen_t len, int type)
{
    (void)addr;
    (void)len;
    (void)type;
    return NULL;                /* no reverse resolver; caller falls back */
}

char *
nl_langinfo(int item)
{
    /* CODESET is nl_langinfo item 0 on Darwin; report UTF-8 so xterm selects
     * its wide-character path. Everything else: empty string. */
    if (item == 0) {
        return (char *)"UTF-8";
    }
    return (char *)"";
}

static void
pd_pwcopy(struct passwd *dst, const struct passwd *src,
          char *buf, size_t buflen, struct passwd **result)
{
    size_t need_name = src->pw_name ? strlen(src->pw_name) + 1 : 0;
    size_t need_pass = src->pw_passwd ? strlen(src->pw_passwd) + 1 : 0;
    size_t need_gecos = src->pw_gecos ? strlen(src->pw_gecos) + 1 : 0;
    size_t need_dir = src->pw_dir ? strlen(src->pw_dir) + 1 : 0;
    size_t need_shell = src->pw_shell ? strlen(src->pw_shell) + 1 : 0;
    size_t off = 0;

    if (need_name + need_pass + need_gecos + need_dir + need_shell > buflen) {
        *result = NULL;         /* caller sees ERANGE via return value */
        return;
    }

    *dst = *src;

#define PD_PW_DUP(field, len)                                   \
    do {                                                        \
        if ((len) != 0) {                                       \
            memcpy(buf + off, src->field, (len));               \
            dst->field = buf + off;                             \
            off += (len);                                       \
        } else {                                                \
            dst->field = NULL;                                  \
        }                                                       \
    } while (0)

    PD_PW_DUP(pw_name, need_name);
    PD_PW_DUP(pw_passwd, need_pass);
    PD_PW_DUP(pw_gecos, need_gecos);
    PD_PW_DUP(pw_dir, need_dir);
    PD_PW_DUP(pw_shell, need_shell);
#undef PD_PW_DUP

    *result = dst;
}

int
getpwuid_r(uid_t uid, struct passwd *pwd, char *buffer, size_t bufsize,
           struct passwd **result)
{
    struct passwd *found = getpwuid(uid);

    if (found == NULL) {
        *result = NULL;
        return 0;               /* not found is not an error */
    }
    pd_pwcopy(pwd, found, buffer, bufsize, result);
    return (*result == NULL) ? ERANGE : 0;
}

int
getpwnam_r(const char *login, struct passwd *pwd, char *buffer, size_t bufsize,
           struct passwd **result)
{
    struct passwd *found = getpwnam(login);

    if (found == NULL) {
        *result = NULL;
        return 0;
    }
    pd_pwcopy(pwd, found, buffer, bufsize, result);
    return (*result == NULL) ? ERANGE : 0;
}

/*
 * PureDarwin's libc archive ships none of the Unix98 pty helpers, so provide
 * them here directly over /dev/ptmx using Darwin's pty ioctls (sys/ttycom.h).
 * These are the same operations Apple's libc performs; whether they succeed at
 * runtime depends on the kernel's ptmx/ptsd driver being present.
 */
#ifndef TIOCPTYGRANT
#define TIOCPTYGRANT _IO('t', 84)
#endif
#ifndef TIOCPTYGNAME
#define TIOCPTYGNAME _IOC(IOC_OUT, 't', 83, 128)
#endif
#ifndef TIOCPTYUNLK
#define TIOCPTYUNLK _IO('t', 82)
#endif

int
posix_openpt(int oflag)
{
    return open("/dev/ptmx", oflag);
}

int
grantpt(int fildes)
{
    return ioctl(fildes, TIOCPTYGRANT);
}

int
unlockpt(int fildes)
{
    return ioctl(fildes, TIOCPTYUNLK);
}

char *
ptsname(int fildes)
{
    static char buffer[128];

    if (ioctl(fildes, TIOCPTYGNAME, buffer) < 0) {
        return NULL;
    }
    return buffer;
}

int
openpty(int *amaster, int *aslave, char *name,
        struct termios *termp, struct winsize *winp)
{
    int master;
    int slave;
    char *slave_name;

    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) {
        return -1;
    }
    if (grantpt(master) < 0 || unlockpt(master) < 0) {
        close(master);
        return -1;
    }
    slave_name = ptsname(master);
    if (slave_name == NULL) {
        close(master);
        return -1;
    }
    slave = open(slave_name, O_RDWR | O_NOCTTY);
    if (slave < 0) {
        close(master);
        return -1;
    }

    if (termp != NULL) {
        (void)tcsetattr(slave, TCSAFLUSH, termp);
    }
    if (winp != NULL) {
        (void)ioctl(slave, TIOCSWINSZ, winp);
    }
    if (name != NULL) {
        strcpy(name, slave_name);
    }

    *amaster = master;
    *aslave = slave;
    return 0;
}

/*
 * popen$DARWIN_EXTSN: xterm references the versioned symbol (the SDK's
 * <stdio.h> asm-renames popen). PureDarwin's libc archive exports neither
 * variant, so implement it here via fork/exec/pipe. Book-kept fds so the
 * companion pclose can reap; if pclose isn't linked the child is simply
 * reaped by the kernel at exit.
 */
extern FILE *__pd_popen_extsn(const char *command, const char *type)
    __asm("_popen$DARWIN_EXTSN");

FILE *
__pd_popen_extsn(const char *command, const char *type)
{
    int fds[2];
    int read_side;
    int want_read;
    pid_t pid;

    if (command == NULL || type == NULL
        || (type[0] != 'r' && type[0] != 'w')) {
        errno = EINVAL;
        return NULL;
    }
    want_read = (type[0] == 'r');

    if (pipe(fds) < 0) {
        return NULL;
    }

    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return NULL;
    }

    if (pid == 0) {
        if (want_read) {
            dup2(fds[1], STDOUT_FILENO);
        } else {
            dup2(fds[0], STDIN_FILENO);
        }
        close(fds[0]);
        close(fds[1]);
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }

    if (want_read) {
        close(fds[1]);
        read_side = fds[0];
    } else {
        close(fds[0]);
        read_side = fds[1];
    }
    return fdopen(read_side, want_read ? "r" : "w");
}

/*
 * select$1050: xterm's SDK <sys/select.h> asm-renames select to the 10.5
 * ("UNIX2003") variant. libSystem already provides select$DARWIN_EXTSN from
 * the libc archive, so forward the 1050 alias to it.
 */
extern int __pd_select_extsn(int nfds, fd_set *readfds, fd_set *writefds,
                             fd_set *errorfds, struct timeval *timeout)
    __asm("_select$DARWIN_EXTSN");
extern int __pd_select_1050(int nfds, fd_set *readfds, fd_set *writefds,
                            fd_set *errorfds, struct timeval *timeout)
    __asm("_select$1050");

int
__pd_select_1050(int nfds, fd_set *readfds, fd_set *writefds,
                 fd_set *errorfds, struct timeval *timeout)
{
    return __pd_select_extsn(nfds, readfds, writefds, errorfds, timeout);
}

/*
 * nano (via gnulib) needs these; none of them are in libc_static/
 * libsystem_kernel, so implement them directly.
 */

#include <dirent.h>
#include <search.h>
#include <wchar.h>

double
frexp(double value, int *exp)
{
    union { double d; uint64_t u; } v = { .d = value };
    uint64_t mant = v.u & 0x000fffffffffffffULL;
    int biased_exp = (int)((v.u >> 52) & 0x7ff);
    int sign = (v.u >> 63) & 1;

    if (biased_exp == 0x7ff || value == 0.0) {
        /* NaN, +-Inf, or +-0: return value unchanged, exponent 0. */
        *exp = 0;
        return value;
    }
    if (biased_exp == 0) {
        /* Subnormal: normalize by hand before extracting the exponent. */
        int shift = 0;
        while ((mant & 0x0010000000000000ULL) == 0) {
            mant <<= 1;
            shift++;
        }
        mant &= 0x000fffffffffffffULL;
        biased_exp = 1 - shift;
    }
    *exp = biased_exp - 1022;   /* bias 1023, plus one since mantissa gets a leading 0.5 */

    v.u = ((uint64_t)sign << 63) | (1022ULL << 52) | mant;   /* result in [0.5, 1.0) */
    return v.d;
}

/* The SDK's <signal.h> defines these as inlining macros; undef them so we
 * can provide real, linkable function symbols instead. */
#undef sigemptyset
#undef sigfillset
#undef sigaddset

int
sigemptyset(sigset_t *set)
{
    *set = 0;
    return 0;
}

int
sigfillset(sigset_t *set)
{
    *set = ~(sigset_t)0;
    return 0;
}

int
sigaddset(sigset_t *set, int signo)
{
    if (signo <= 0 || signo >= (int)(sizeof(sigset_t) * 8)) {
        errno = EINVAL;
        return -1;
    }
    *set |= ((sigset_t)1 << (signo - 1));
    return 0;
}

wchar_t *
wmemcpy(wchar_t *dst, const wchar_t *src, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
    return dst;
}

wchar_t *
wmemchr(const wchar_t *s, wchar_t c, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (s[i] == c)
            return (wchar_t *)&s[i];
    return NULL;
}

wchar_t *
wcscat(wchar_t *dst, const wchar_t *src)
{
    wchar_t *d = dst;
    while (*d)
        d++;
    while ((*d++ = *src++))
        ;
    return dst;
}

/* This system's locale is fixed to UTF-8 (see nl_langinfo above), so the
 * wide<->multibyte conversions below implement UTF-8 directly rather than
 * consulting a locale table. mbsinit/btowc treat every byte state as
 * stateless (UTF-8 has no shift state), which is always safe to report. */

int
mbsinit(const mbstate_t *ps)
{
    (void)ps;
    return 1;
}

wint_t
btowc(int c)
{
    if (c == EOF)
        return WEOF;
    if ((unsigned char)c < 0x80)
        return (wint_t)(unsigned char)c;
    return WEOF;   /* multi-byte lead byte: not representable as a single wchar_t here */
}

static size_t
pd_wctoutf8(wchar_t wc, char *out)
{
    uint32_t c = (uint32_t)wc;

    if (c < 0x80) {
        if (out) out[0] = (char)c;
        return 1;
    } else if (c < 0x800) {
        if (out) {
            out[0] = (char)(0xC0 | (c >> 6));
            out[1] = (char)(0x80 | (c & 0x3F));
        }
        return 2;
    } else if (c < 0x10000) {
        if (out) {
            out[0] = (char)(0xE0 | (c >> 12));
            out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[2] = (char)(0x80 | (c & 0x3F));
        }
        return 3;
    } else {
        if (out) {
            out[0] = (char)(0xF0 | (c >> 18));
            out[1] = (char)(0x80 | ((c >> 12) & 0x3F));
            out[2] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[3] = (char)(0x80 | (c & 0x3F));
        }
        return 4;
    }
}

size_t
wcrtomb(char *s, wchar_t wc, mbstate_t *ps)
{
    (void)ps;
    if (s == NULL)
        return 1;   /* querying shift-state reset length: UTF-8 has none */
    return pd_wctoutf8(wc, s);
}

size_t
wcstombs(char *dst, const wchar_t *src, size_t n)
{
    size_t used = 0;

    while (*src) {
        char buf[4];
        size_t len = pd_wctoutf8(*src, buf);

        if (dst) {
            if (used + len > n)
                break;
            memcpy(dst + used, buf, len);
        } else if (used + len > n) {
            /* no destination: caller (wcsrtombs below) wants a length only */
        }
        used += len;
        src++;
    }
    if (dst && used < n)
        dst[used] = '\0';
    return used;
}

size_t
wcsrtombs(char *dst, const wchar_t **src, size_t n, mbstate_t *ps)
{
    (void)ps;
    size_t total = wcstombs(dst, *src, n);
    if (dst)
        *src = NULL;   /* whole string consumed (no embedded NUL support needed here) */
    return total;
}

wint_t
wctob(wint_t c)
{
    if (c == WEOF)
        return EOF;
    if ((uint32_t)c < 0x80)
        return (wint_t)(unsigned char)c;
    return WEOF;
}

/*
 * getpwent/endpwent: nano only calls these while iterating the passwd
 * database for username tab-completion. PureDarwin exposes no such
 * enumerable database (getpwnam_r/getpwuid_r above answer single lookups
 * from a fixed table), so report "no more entries" immediately - nano
 * treats that as an empty completion list, not an error.
 */
struct passwd *
getpwent(void)
{
    return NULL;
}

void
endpwent(void)
{
}

/*
 * rewinddir$INODE64: opendir$INODE64/readdir$INODE64 (from libc_static) read
 * directory entries directly off the fd with no additional userspace
 * buffering layer here, so resetting the fd's file offset is equivalent to
 * reopening the stream at the start.
 */
void
__pd_rewinddir_inode64(DIR *dirp) __asm("_rewinddir$INODE64");

void
__pd_rewinddir_inode64(DIR *dirp)
{
    lseek(dirfd(dirp), 0, SEEK_SET);
}

/*
 * tsearch/tfind/tdelete/twalk: minimal unbalanced BST, sufficient for nano's
 * small in-memory lookup tables (it does not need tree-height guarantees).
 */
/* `key` must be the first member: callers dereference tsearch()'s return
 * value as `*(void **)node` to read the key back (the POSIX/glibc tsearch
 * calling convention), so the node pointer and the key-pointer slot must
 * coincide. */
struct pd_tnode {
    void            *key;
    struct pd_tnode *left;
    struct pd_tnode *right;
};

void *
tsearch(const void *key, void **rootp, int (*compar)(const void *, const void *))
{
    struct pd_tnode **cur = (struct pd_tnode **)rootp;

    if (rootp == NULL)
        return NULL;

    while (*cur != NULL) {
        int cmp = compar(key, (*cur)->key);
        if (cmp == 0)
            return *cur;
        cur = (cmp < 0) ? &(*cur)->left : &(*cur)->right;
    }

    {
        struct pd_tnode *n = (struct pd_tnode *)malloc(sizeof(*n));
        if (n == NULL)
            return NULL;
        n->left = n->right = NULL;
        n->key = (void *)key;
        *cur = n;
        return n;
    }
}

void *
tfind(const void *key, void *const *rootp, int (*compar)(const void *, const void *))
{
    struct pd_tnode *cur;

    if (rootp == NULL)
        return NULL;
    cur = *(struct pd_tnode *const *)rootp;
    while (cur != NULL) {
        int cmp = compar(key, cur->key);
        if (cmp == 0)
            return cur;
        cur = (cmp < 0) ? cur->left : cur->right;
    }
    return NULL;
}

/* Find the in-order successor's parent link to splice out `node`. */
static struct pd_tnode **
pd_tnode_min_link(struct pd_tnode **linkp)
{
    while ((*linkp)->left != NULL)
        linkp = &(*linkp)->left;
    return linkp;
}

void *
tdelete(const void *key, void **rootp, int (*compar)(const void *, const void *))
{
    struct pd_tnode **cur = (struct pd_tnode **)rootp;
    struct pd_tnode *parent_ret;

    if (rootp == NULL)
        return NULL;

    while (*cur != NULL) {
        int cmp = compar(key, (*cur)->key);
        if (cmp == 0)
            break;
        cur = (cmp < 0) ? &(*cur)->left : &(*cur)->right;
    }
    if (*cur == NULL)
        return NULL;

    parent_ret = (*cur == *(struct pd_tnode **)rootp) ? *cur : NULL;
    {
        struct pd_tnode *node = *cur;

        if (node->left == NULL) {
            *cur = node->right;
        } else if (node->right == NULL) {
            *cur = node->left;
        } else {
            struct pd_tnode **succ_link = pd_tnode_min_link(&node->right);
            struct pd_tnode *succ = *succ_link;

            *succ_link = succ->right;
            succ->left = node->left;
            succ->right = node->right;
            *cur = succ;
        }
        free(node);
    }
    return parent_ret ? *rootp : rootp;   /* glibc: non-NULL that isn't `node` itself */
}

static void
pd_twalk_r(const struct pd_tnode *node, void (*action)(const void *, VISIT, int), int depth)
{
    if (node == NULL)
        return;
    if (node->left == NULL && node->right == NULL) {
        action(node, leaf, depth);
        return;
    }
    action(node, preorder, depth);
    pd_twalk_r(node->left, action, depth + 1);
    action(node, postorder, depth);
    pd_twalk_r(node->right, action, depth + 1);
    action(node, endorder, depth);
}

void
twalk(const void *root, void (*action)(const void *, VISIT, int))
{
    pd_twalk_r((const struct pd_tnode *)root, action, 0);
}

long long
atoll(const char *nptr)
{
    return strtoll(nptr, NULL, 10);
}

double
sqrt(double x)
{
    return __builtin_sqrt(x);
}

double
trunc(double x)
{
    return __builtin_trunc(x);
}

double
fmod(double x, double y)
{
    return __builtin_fmod(x, y);
}

int
dprintf(int fd, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0)
        return n;
    if ((size_t)n >= sizeof(buf))
        n = sizeof(buf) - 1;   /* truncate: matches a fixed-size fallback, not glibc's realloc growth */
    return (int)write(fd, buf, (size_t)n);
}

int
system(const char *command)
{
    pid_t pid;
    int status;

    if (command == NULL)
        return 1;   /* a shell is always "available" here */

    pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return status;
}

static uint64_t pd_random_state = 0x2545F4914F6CDD1DULL;

void
srandom(unsigned int seed)
{
    pd_random_state = seed ? seed : 1;
}

long
random(void)
{
    pd_random_state ^= pd_random_state << 13;
    pd_random_state ^= pd_random_state >> 7;
    pd_random_state ^= pd_random_state << 17;
    return (long)(pd_random_state & 0x7fffffff);
}

ssize_t
getdelim(char **lineptr, size_t *n, int delim, FILE *stream)
{
    size_t cap, len = 0;
    char *buf;
    int c;

    if (lineptr == NULL || n == NULL || stream == NULL) {
        errno = EINVAL;
        return -1;
    }
    cap = (*lineptr != NULL && *n > 0) ? *n : 128;
    buf = (*lineptr != NULL) ? *lineptr : (char *)malloc(cap);
    if (buf == NULL)
        return -1;

    while ((c = fgetc(stream)) != EOF) {
        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            char *nbuf = (char *)realloc(buf, ncap);
            if (nbuf == NULL) {
                *lineptr = buf;
                *n = cap;
                return -1;
            }
            buf = nbuf;
            cap = ncap;
        }
        buf[len++] = (char)c;
        if (c == delim)
            break;
    }
    if (len == 0 && c == EOF) {
        *lineptr = buf;
        *n = cap;
        return -1;
    }
    buf[len] = '\0';
    *lineptr = buf;
    *n = cap;
    return (ssize_t)len;
}

ssize_t
getline(char **lineptr, size_t *n, FILE *stream)
{
    return getdelim(lineptr, n, '\n', stream);
}

static void
pd_grcopy(struct group *dst, const struct group *src,
          char *buf, size_t buflen, struct group **result)
{
    size_t need_name = src->gr_name ? strlen(src->gr_name) + 1 : 0;
    size_t need_passwd = src->gr_passwd ? strlen(src->gr_passwd) + 1 : 0;
    size_t nmembers = 0, need_members, need_member_strings = 0, off = 0;
    size_t i;

    if (src->gr_mem != NULL) {
        while (src->gr_mem[nmembers] != NULL) {
            need_member_strings += strlen(src->gr_mem[nmembers]) + 1;
            nmembers++;
        }
    }
    need_members = (nmembers + 1) * sizeof(char *);

    if (need_name + need_passwd + need_members + need_member_strings > buflen) {
        *result = NULL;
        return;
    }

    *dst = *src;

    if (need_name != 0) {
        memcpy(buf + off, src->gr_name, need_name);
        dst->gr_name = buf + off;
        off += need_name;
    } else {
        dst->gr_name = NULL;
    }
    if (need_passwd != 0) {
        memcpy(buf + off, src->gr_passwd, need_passwd);
        dst->gr_passwd = buf + off;
        off += need_passwd;
    } else {
        dst->gr_passwd = NULL;
    }
    dst->gr_mem = (char **)(buf + off);
    off += need_members;
    for (i = 0; i < nmembers; i++) {
        size_t len = strlen(src->gr_mem[i]) + 1;
        memcpy(buf + off, src->gr_mem[i], len);
        dst->gr_mem[i] = buf + off;
        off += len;
    }
    dst->gr_mem[nmembers] = NULL;

    *result = dst;
}

int
getgrnam_r(const char *name, struct group *grp, char *buffer, size_t bufsize,
           struct group **result)
{
    struct group *found = getgrnam(name);

    if (found == NULL) {
        *result = NULL;
        return 0;
    }
    pd_grcopy(grp, found, buffer, bufsize, result);
    return (*result == NULL) ? ERANGE : 0;
}

int
getgrgid_r(gid_t gid, struct group *grp, char *buffer, size_t bufsize,
           struct group **result)
{
    struct group *found = getgrgid(gid);

    if (found == NULL) {
        *result = NULL;
        return 0;
    }
    pd_grcopy(grp, found, buffer, bufsize, result);
    return (*result == NULL) ? ERANGE : 0;
}

extern int __pd_getgroups(int gidsetsize, gid_t grouplist[]) __asm("_getgroups");
int __pd_getgroups_extsn(int gidsetsize, gid_t grouplist[]) __asm("_getgroups$DARWIN_EXTSN");

int
__pd_getgroups_extsn(int gidsetsize, gid_t grouplist[])
{
    return __pd_getgroups(gidsetsize, grouplist);
}

int
statvfs(const char *path, struct statvfs *buf)
{
    struct statfs sf;

    if (statfs(path, &sf) != 0)
        return -1;
    memset(buf, 0, sizeof(*buf));
    buf->f_bsize   = sf.f_bsize;
    buf->f_frsize  = sf.f_bsize;
    buf->f_blocks  = sf.f_blocks;
    buf->f_bfree   = sf.f_bfree;
    buf->f_bavail  = sf.f_bavail;
    buf->f_files   = sf.f_files;
    buf->f_ffree   = sf.f_ffree;
    buf->f_favail  = sf.f_ffree;
    buf->f_fsid    = (unsigned long)sf.f_fsid.val[0];
    buf->f_flag    = 0;
    buf->f_namemax = 255;
    return 0;
}

int __pd_getmntinfo_inode64(struct statfs **mntbufp, int flags) __asm("_getmntinfo$INODE64");

int
__pd_getmntinfo_inode64(struct statfs **mntbufp, int flags)
{
    (void)flags;
    *mntbufp = NULL;
    return 0;
}

static const char *
pd_strptime_num(const char *s, int mindigits, int maxdigits, int *out)
{
    int n = 0, count = 0;

    while (count < maxdigits && *s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
        count++;
    }
    if (count < mindigits)
        return NULL;
    *out = n;
    return s;
}

char *
strptime(const char *s, const char *format, struct tm *tm)
{
    int val;

    while (*format) {
        if (*format == '%' && format[1]) {
            format++;
            switch (*format) {
            case 'Y':
                s = pd_strptime_num(s, 1, 4, &val);
                if (!s) return NULL;
                tm->tm_year = val - 1900;
                break;
            case 'y':
                s = pd_strptime_num(s, 1, 2, &val);
                if (!s) return NULL;
                tm->tm_year = (val < 69) ? val + 100 : val;
                break;
            case 'm':
                s = pd_strptime_num(s, 1, 2, &val);
                if (!s) return NULL;
                tm->tm_mon = val - 1;
                break;
            case 'd':
                s = pd_strptime_num(s, 1, 2, &val);
                if (!s) return NULL;
                tm->tm_mday = val;
                break;
            case 'H':
                s = pd_strptime_num(s, 1, 2, &val);
                if (!s) return NULL;
                tm->tm_hour = val;
                break;
            case 'M':
                s = pd_strptime_num(s, 1, 2, &val);
                if (!s) return NULL;
                tm->tm_min = val;
                break;
            case 'S':
                s = pd_strptime_num(s, 1, 2, &val);
                if (!s) return NULL;
                tm->tm_sec = val;
                break;
            case '%':
                if (*s != '%') return NULL;
                s++;
                break;
            case 'n':
            case 't':
                while (*s == ' ' || *s == '\t' || *s == '\n')
                    s++;
                break;
            default:
                return NULL;   /* unsupported specifier */
            }
            format++;
        } else if (*format == ' ') {
            while (*s == ' ' || *s == '\t')
                s++;
            format++;
        } else {
            if (*s != *format)
                return NULL;
            s++;
            format++;
        }
    }
    return (char *)s;
}
