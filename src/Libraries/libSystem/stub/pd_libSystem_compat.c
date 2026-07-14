#include <sys/types.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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