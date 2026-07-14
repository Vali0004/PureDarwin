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

extern int __pd_sys_pause(void) __asm("___pause");
extern pid_t __pd_sys_waitpid(pid_t pid, int *status, int options) __asm("___waitpid");
extern int _dyld_func_lookup(const char *name, void **address);
extern FILE *__pd_fdopen_extsn(int fd, const char *mode) __asm("_fdopen$DARWIN_EXTSN");
extern FILE *__pd_fopen_extsn(const char *path, const char *mode) __asm("_fopen$DARWIN_EXTSN");

typedef unsigned long long pd_dispatch_time_t;

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

    while (semaphore->value < 0) {
        int err = pthread_cond_wait(&semaphore->cond, &semaphore->lock);
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
