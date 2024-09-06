#define _XOPEN_SOURCE 500
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <search.h>
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include "recorder.h"


typedef struct stream_map {
    char* filename;
    FILE* stream;       // key
    UT_hash_handle hh;
} stream_map_t;

typedef struct fd_map {
    char* filename;
    int fd;             // key
    UT_hash_handle hh;
} fd_map_t;

static stream_map_t* stream2name_map;
static fd_map_t*     fd2name_map;

static inline char* fd2name(int fd) {
    fd_map_t *entry = NULL;
    HASH_FIND_INT(fd2name_map, &fd, entry);
    if(entry)
        return strdup(entry->filename);     // duplicate the filename as it will be release in free_record()
    return NULL;
}

static inline char* stream2name(FILE* stream) {
    stream_map_t *entry = NULL;
    HASH_FIND_PTR(stream2name_map, &stream, entry);
    if(entry)
        return strdup(entry->filename);
    return NULL;
}


/**
 * Given a (char* path), (int fd) or (FILE* stream)
 * as the argument of (void* f_arg), get the absolute
 * file path and check if we should intercept this call.
 *
 * If not, we directly call the real call and return
 * If so, the absolute name is stored in _fname
 */
#define ARG_TYPE_FD         0
#define ARG_TYPE_STREAM     1
#define ARG_TYPE_PATH       2

#define GET_CHECK_FILENAME(func, func_args, f_arg, f_arg_type)      \
    char* _fname = NULL;                                            \
    if(logger_initialized()) {                                      \
        if(f_arg_type == ARG_TYPE_PATH)                             \
            _fname = realrealpath((char*) f_arg);                   \
        if(f_arg_type == ARG_TYPE_STREAM)                           \
            _fname = stream2name((FILE*) f_arg);                    \
        if(f_arg_type == ARG_TYPE_FD)                               \
            _fname = fd2name(*(int*) f_arg);                        \
    }                                                               \
    if(_fname== NULL || !accept_filename(_fname)) {                 \
        if(_fname) free(_fname);                                    \
        GOTCHA_SET_REAL_CALL(func, RECORDER_POSIX);         \
        return GOTCHA_REAL_CALL(func) func_args;                    \
    }                                                               \
    assert(accept_filename(_fname) == 1);


/**
 * Caller need to guarantee that the filename
 * is accepted.
 */
static inline void add_to_map(char* filename, void* arg, int arg_type) {
    if(arg_type == ARG_TYPE_STREAM) {        // FILE* stream
        stream_map_t *entry = malloc(sizeof(stream_map_t));
        entry->stream = (FILE*) arg;
        entry->filename = realrealpath(filename);
        HASH_ADD_PTR(stream2name_map, stream, entry);
    }
    if(arg_type == ARG_TYPE_FD) {
        fd_map_t *entry = malloc(sizeof(fd_map_t));
        entry->fd = *((int*) arg);
        entry->filename = realrealpath(filename);
        HASH_ADD_INT(fd2name_map, fd, entry);
    }
}

static inline void remove_from_map(void* arg, int arg_type) {
    if(arg_type == ARG_TYPE_FD) {
        int fd = (*(int*) arg);
        fd_map_t *entry = NULL;
        HASH_FIND_INT(fd2name_map, &fd, entry);
        if(entry) {
            HASH_DEL(fd2name_map, entry);
            free(entry->filename);
            free(entry);
        }
    }
    if(arg_type == ARG_TYPE_STREAM) {
        FILE* stream = (FILE*) arg;
        stream_map_t *entry = NULL;
        HASH_FIND_PTR(stream2name_map, &stream, entry);
        if(entry) {
            HASH_DEL(stream2name_map, entry);
            free(entry->filename);
            free(entry);
        }
    }
}


/*********************************************************************************
 *
 * Below are the wrappers of the POSIX functions
 *
 *********************************************************************************/

int WRAPPER_NAME(close)(int fd) {
    GET_CHECK_FILENAME(close, (fd), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(int, close, (fd));
    remove_from_map(&fd, ARG_TYPE_FD);
    char** args = assemble_args_list(1, _fname);
    RECORDER_INTERCEPTOR_EPILOGUE(1, args);
}

int WRAPPER_NAME(fclose)(FILE *stream) {
    GET_CHECK_FILENAME(fclose, (stream), stream, ARG_TYPE_STREAM);
    char** args = assemble_args_list(1, _fname);
    remove_from_map(stream, ARG_TYPE_STREAM);
    RECORDER_INTERCEPTOR_PROLOGUE(int, fclose, (stream));
    RECORDER_INTERCEPTOR_EPILOGUE(1, args);
}

int WRAPPER_NAME(fsync)(int fd) {
    GET_CHECK_FILENAME(fsync, (fd), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(int, fsync, (fd));
    char** args = assemble_args_list(1, _fname);
    RECORDER_INTERCEPTOR_EPILOGUE(1, args);
}

int WRAPPER_NAME(fdatasync)(int fd) {
    GET_CHECK_FILENAME(fdatasync, (fd), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(int, fdatasync, (fd));
    char** args = assemble_args_list(1, _fname);
    RECORDER_INTERCEPTOR_EPILOGUE(1, args);
}

/*
void* WRAPPER_NAME(mmap64)(void *addr, size_t length, int prot, int flags, int fd, off64_t offset) {
    GET_CHECK_FILENAME(mmap64, (addr, length, prot, flags, fd, offset), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(void*, mmap64, (addr, length, prot, flags, fd, offset));
    char** args = assemble_args_list(6, ptoa(addr), itoa(length), itoa(prot), itoa(flags), _fname, itoa(offset));
    RECORDER_INTERCEPTOR_EPILOGUE(6, args);
}

void* WRAPPER_NAME(mmap)(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    GET_CHECK_FILENAME(mmap, (addr, length, prot, flags, fd, offset), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(void*, mmap, (addr, length, prot, flags, fd, offset));
    char** args = assemble_args_list(6, ptoa(addr), itoa(length), itoa(prot), itoa(flags), _fname, itoa(offset));
    RECORDER_INTERCEPTOR_EPILOGUE(6, args);
}

int WRAPPER_NAME(msync)(void *addr, size_t length, int flags) {
    RECORDER_INTERCEPTOR_PROLOGUE(int, msync, (addr, length, flags));
    char** args = assemble_args_list(3, ptoa(addr), itoa(length), itoa(flags));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}
*/

int WRAPPER_NAME(creat)(const char *path, mode_t mode) {
    GET_CHECK_FILENAME(creat, (path, mode), path, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, creat, (path, mode));
    add_to_map(_fname, &res, ARG_TYPE_FD);
    char** args = assemble_args_list(2, _fname, itoa(mode));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args);
}

int WRAPPER_NAME(creat64)(const char *path, mode_t mode) {
    GET_CHECK_FILENAME(creat64, (path, mode), path, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, creat64, (path, mode));
    add_to_map(_fname, &res, ARG_TYPE_FD);
    char** args = assemble_args_list(2, _fname, itoa(mode));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args);
}

int WRAPPER_NAME(open64)(const char *path, int flags, ...) {
    if (flags & O_CREAT) {
        va_list arg;
        va_start(arg, flags);
        int mode = va_arg(arg, int);
        va_end(arg);
        GET_CHECK_FILENAME(open64, (path, flags, mode), path, ARG_TYPE_PATH);
        RECORDER_INTERCEPTOR_PROLOGUE(int, open64, (path, flags, mode));
        add_to_map(_fname, &res, ARG_TYPE_FD);
        char** args = assemble_args_list(3, _fname, itoa(flags), itoa(mode));
        RECORDER_INTERCEPTOR_EPILOGUE(3, args);

    } else {
        GET_CHECK_FILENAME(open64, (path, flags), path, ARG_TYPE_PATH);
        RECORDER_INTERCEPTOR_PROLOGUE(int, open64, (path, flags));
        add_to_map(_fname, &res, ARG_TYPE_FD);
        char** args = assemble_args_list(2, _fname, itoa(flags));
        RECORDER_INTERCEPTOR_EPILOGUE(2, args);
    }
}

int WRAPPER_NAME(open)(const char *path, int flags, ...) {
    if (flags & O_CREAT) {
        va_list arg;
        va_start(arg, flags);
        int mode = va_arg(arg, int);
        va_end(arg);
        GET_CHECK_FILENAME(open, (path, flags, mode), path, ARG_TYPE_PATH);
        RECORDER_INTERCEPTOR_PROLOGUE(int, open, (path, flags, mode));
        add_to_map(_fname, &res, ARG_TYPE_FD);
        char** args = assemble_args_list(3, _fname, itoa(flags), itoa(mode));
        RECORDER_INTERCEPTOR_EPILOGUE(3, args);
    } else {
        GET_CHECK_FILENAME(open, (path, flags), path, ARG_TYPE_PATH);
        RECORDER_INTERCEPTOR_PROLOGUE(int, open, (path, flags));
        add_to_map(_fname, &res, ARG_TYPE_FD);
        char** args = assemble_args_list(2, _fname, itoa(flags));
        RECORDER_INTERCEPTOR_EPILOGUE(2, args);
    }
}

FILE* WRAPPER_NAME(fopen64)(const char *path, const char *mode) {
    GET_CHECK_FILENAME(fopen64, (path, mode), path, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(FILE*, fopen64, (path, mode));
    add_to_map(_fname, res, ARG_TYPE_STREAM);
    char** args = assemble_args_list(2, _fname, strdup(mode));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args);
}

FILE* WRAPPER_NAME(fopen)(const char *path, const char *mode) {
    GET_CHECK_FILENAME(fopen, (path, mode), path, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(FILE*, fopen, (path, mode))
    add_to_map(_fname, res, ARG_TYPE_STREAM);
    char** args = assemble_args_list(2, realrealpath(path), strdup(mode));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args);
}


/*
 * From http://refspecs.linuxbase.org/LSB_3.0.0/LSB-PDA/LSB-PDA/baselib-xstat-1.html:
 * The functions __xstat(), __lxstat(), and __fxstat() shall implement the ISO POSIX (2003) functions stat(), lstat(), and fstat() respectively
 *
 * This means stat(), lstat(), fstat() are just wrappers in GLIC and dlsym() is not able to hook them.
 * So wee need to hook __xstat(), __lxstat(), and __fxstat()
 */
/*
 * NOTE on __xstat(2), __lxstat(2), and __fxstat(2)
 * The additional parameter vers shall be 3 or the behavior of these functions
 * is undefined. (ISO POSIX(2003))
 *
 * from /sys/stat.h, it seems that we need to test if vers being _STAT_VER,
 * instead of using the absolute value 3.
 */

/*
 * NOTE 2: As of glibc-2.33, _STAT_VER is no longer defined in bits/stat.h.
 * __xstat is also no longer declared in stat.h, but it still exists in the
 * library, so HAVE___XSTAT will be true.  (The same goes for __lxstat &
 * __fxstat.)  In such a case, we have to define _STAT_VER ourselves.
 */
#ifndef _STAT_VER
  #define _STAT_VER 3
#endif

#if __GLIBC__ == 2 && __GLIBC_MINOR__ < 33
int WRAPPER_NAME(__xstat)(int vers, const char *path, struct stat *buf) {
    GET_CHECK_FILENAME(__xstat, (vers, path, buf), path, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, __xstat, (vers, path, buf));
    char** args = assemble_args_list(3, itoa(vers), _fname, ptoa(buf));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}

int WRAPPER_NAME(__xstat64)(int vers, const char *path, struct stat64 *buf) {
    GET_CHECK_FILENAME(__xstat64, (vers, path, buf), path, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, __xstat64, (vers, path, buf));
    char** args = assemble_args_list(3, itoa(vers), _fname, ptoa(buf));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}

int WRAPPER_NAME(__lxstat)(int vers, const char *path, struct stat *buf) {
    GET_CHECK_FILENAME(__lxstat, (vers, path, buf), path, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, __lxstat, (vers, path, buf));
    char** args = assemble_args_list(3, itoa(vers), _fname, ptoa(buf));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}

int WRAPPER_NAME(__lxstat64)(int vers, const char *path, struct stat64 *buf) {
    GET_CHECK_FILENAME(__lxstat64, (vers, path, buf), path, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, __lxstat64, (vers, path, buf));
    char** args = assemble_args_list(3, itoa(vers), _fname, ptoa(buf));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}

int WRAPPER_NAME(__fxstat)(int vers, int fd, struct stat *buf) {
    GET_CHECK_FILENAME(__fxstat, (vers, fd, buf), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(int, __fxstat, (vers, fd, buf));
    char** args = assemble_args_list(3, itoa(vers), _fname, ptoa(buf));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}

int WRAPPER_NAME(__fxstat64)(int vers, int fd, struct stat64 *buf) {
    GET_CHECK_FILENAME(__fxstat64, (vers, fd, buf), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(int, __fxstat64, (vers, fd, buf));
    char** args = assemble_args_list(3, itoa(vers), _fname, ptoa(buf));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}
#endif

ssize_t WRAPPER_NAME(pread64)(int fd, void *buf, size_t count, off64_t offset) {
    GET_CHECK_FILENAME(pread64, (fd, buf, count, offset), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(ssize_t, pread64, (fd, buf, count, offset));
    off64_t stored_offset = offset;
    if (logger_intraprocess_pattern_recognition())
        stored_offset = iopr_intraprocess("pread64", (off64_t)offset);
    char** args = assemble_args_list(4, _fname, ptoa(buf), itoa(count), itoa(offset));
    RECORDER_INTERCEPTOR_EPILOGUE(4, args);
}

ssize_t WRAPPER_NAME(pread)(int fd, void *buf, size_t count, off_t offset) {
    GET_CHECK_FILENAME(pread, (fd, buf, count, offset), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(ssize_t, pread, (fd, buf, count, offset));
    off64_t stored_offset = (off64_t) offset;
    if (logger_intraprocess_pattern_recognition())
        stored_offset = iopr_intraprocess("pread", (off64_t)offset);
    char** args = assemble_args_list(4, _fname, ptoa(buf), itoa(count), itoa(offset));
    RECORDER_INTERCEPTOR_EPILOGUE(4, args);
}

ssize_t WRAPPER_NAME(pwrite64)(int fd, const void *buf, size_t count, off64_t offset) {
    GET_CHECK_FILENAME(pwrite64, (fd, buf, count, offset), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(ssize_t, pwrite64, (fd, buf, count, offset));
    off64_t stored_offset = (off64_t) offset;
    if (logger_intraprocess_pattern_recognition())
        stored_offset = iopr_intraprocess("pwrite64", (off64_t)offset);
    char** args = assemble_args_list(4, _fname, ptoa(buf), itoa(count), itoa(stored_offset));
    RECORDER_INTERCEPTOR_EPILOGUE(4, args);
}
ssize_t WRAPPER_NAME(pwrite)(int fd, const void *buf, size_t count, off_t offset) {
    GET_CHECK_FILENAME(pwrite, (fd, buf, count, offset), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(ssize_t, pwrite, (fd, buf, count, offset));
    off64_t stored_offset = (off64_t) offset;
    if (logger_intraprocess_pattern_recognition())
        stored_offset = iopr_intraprocess("pwrite", (off64_t)offset);
    char** args = assemble_args_list(4, _fname, ptoa(buf), itoa(count), itoa(stored_offset));
    RECORDER_INTERCEPTOR_EPILOGUE(4, args);
}

ssize_t WRAPPER_NAME(readv)(int fd, const struct iovec *iov, int iovcnt) {
    GET_CHECK_FILENAME(readv, (fd, iov, iovcnt), &fd, ARG_TYPE_FD);
    // Return total bytes as the second argument
    // So during post-processing we know both the offset and bytes count
    int total = 0; int i = 0;
    for (i = 0; i < iovcnt; i++)
        total += iov[i].iov_len;
    RECORDER_INTERCEPTOR_PROLOGUE(ssize_t, readv, (fd, iov, iovcnt));
    char** args = assemble_args_list(3, _fname, itoa(total), itoa(iovcnt));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}

ssize_t WRAPPER_NAME(writev)(int fd, const struct iovec *iov, int iovcnt) {
    GET_CHECK_FILENAME(writev, (fd, iov, iovcnt), &fd, ARG_TYPE_FD);
    // Return total bytes as the second argument
    // So during post-processing we know both the offset and bytes count
    int total = 0; int i = 0;
    for (i = 0; i < iovcnt; i++)
        total += iov[i].iov_len;
    RECORDER_INTERCEPTOR_PROLOGUE(ssize_t, writev, (fd, iov, iovcnt));
    char** args = assemble_args_list(3, _fname, itoa(total), itoa(iovcnt));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}

size_t WRAPPER_NAME(fread)(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    GET_CHECK_FILENAME(fread, (ptr, size, nmemb, stream), stream, ARG_TYPE_STREAM);
    RECORDER_INTERCEPTOR_PROLOGUE(size_t, fread, (ptr, size, nmemb, stream));
    char** args = assemble_args_list(4, ptoa(ptr), itoa(size), itoa(nmemb), _fname);
    RECORDER_INTERCEPTOR_EPILOGUE(4, args);
}

size_t WRAPPER_NAME(fwrite)(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    GET_CHECK_FILENAME(fwrite, (ptr, size, nmemb, stream), stream, ARG_TYPE_STREAM);
    RECORDER_INTERCEPTOR_PROLOGUE(size_t, fwrite, (ptr, size, nmemb, stream));
    char** args = assemble_args_list(4, ptoa(ptr), itoa(size), itoa(nmemb), _fname);
    RECORDER_INTERCEPTOR_EPILOGUE(4, args);
}

/*
int WRAPPER_NAME(fprintf)(FILE *stream, const char *format, ...) {
    // Get the size of the string fprintf will write
    va_list fprintf_args;
    va_start(fprintf_args, format);
    int size = vsnprintf(NULL, 0, format, fprintf_args) + 1;
    va_end(fprintf_args);

    va_start(fprintf_args, format);
    GET_CHECK_FILENAME(vfprintf, (stream, format, fprintf_args), stream, ARG_TYPE_STREAM);
    RECORDER_INTERCEPTOR_PROLOGUE(size_t, vfprintf, (stream, format, fprintf_args));
    va_end(fprintf_args);
    char** args = assemble_args_list(2, _fname, itoa(size));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args);
}
*/

ssize_t WRAPPER_NAME(read)(int fd, void *buf, size_t count) {
    GET_CHECK_FILENAME(read, (fd, buf, count), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(ssize_t, read, (fd, buf, count));
    char** args = assemble_args_list(3, _fname, ptoa(buf), itoa(count));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}

ssize_t WRAPPER_NAME(write)(int fd, const void *buf, size_t count) {
    GET_CHECK_FILENAME(write, (fd, buf, count), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(ssize_t, write, (fd, buf, count));
    char** args = assemble_args_list(3, _fname, ptoa(buf), itoa(count));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}

int WRAPPER_NAME(fseek)(FILE *stream, long offset, int whence) {
    GET_CHECK_FILENAME(fseek, (stream, offset, whence), stream, ARG_TYPE_STREAM);
    RECORDER_INTERCEPTOR_PROLOGUE(int, fseek, (stream, offset, whence));
    char** args = assemble_args_list(3, _fname, itoa(offset), itoa(whence));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}

long WRAPPER_NAME(ftell)(FILE *stream) {
    GET_CHECK_FILENAME(ftell, (stream), stream, ARG_TYPE_STREAM);
    RECORDER_INTERCEPTOR_PROLOGUE(long, ftell, (stream));
    char** args = assemble_args_list(1, _fname);
    RECORDER_INTERCEPTOR_EPILOGUE(1, args)
}


off64_t WRAPPER_NAME(lseek64)(int fd, off64_t offset, int whence) {
    GET_CHECK_FILENAME(lseek64, (fd, offset, whence), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(off64_t, lseek64, (fd, offset, whence));
    off64_t stored_offset = (off64_t) offset;
    if (logger_intraprocess_pattern_recognition())
        stored_offset = iopr_intraprocess("lseek64", (off64_t)offset);
    char** args = assemble_args_list(3, _fname, itoa(stored_offset), itoa(whence));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}

off_t WRAPPER_NAME(lseek)(int fd, off_t offset, int whence) {
    GET_CHECK_FILENAME(lseek, (fd, offset, whence), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(off_t, lseek, (fd, offset, whence));
    off64_t stored_offset = (off64_t) offset;
    if (logger_intraprocess_pattern_recognition())
        stored_offset = iopr_intraprocess("lseek", (off64_t)offset);
    char** args = assemble_args_list(3, _fname, itoa(stored_offset), itoa(whence));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}


/* Below are non File-I/O related function calls */
char* WRAPPER_NAME(getcwd)(char *buf, size_t size) {
    RECORDER_INTERCEPTOR_PROLOGUE(char*, getcwd, (buf, size));
    char** args = assemble_args_list(2, ptoa(buf), itoa(size));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args);
}
int WRAPPER_NAME(mkdir)(const char *pathname, mode_t mode) {
    GET_CHECK_FILENAME(mkdir, (pathname, mode), pathname, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, mkdir, (pathname, mode));
    char** args = assemble_args_list(2, _fname, itoa(mode));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args)
}
int WRAPPER_NAME(rmdir)(const char *pathname) {
    GET_CHECK_FILENAME(rmdir, (pathname), pathname, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, rmdir, (pathname));
    char** args = assemble_args_list(1, _fname);
    RECORDER_INTERCEPTOR_EPILOGUE(1, args);
}
int WRAPPER_NAME(chdir)(const char *path) {
    GET_CHECK_FILENAME(chdir, (path), path, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, chdir, (path));
    char** args = assemble_args_list(1, _fname);
    RECORDER_INTERCEPTOR_EPILOGUE(1, args);
}
int WRAPPER_NAME(link)(const char *oldpath, const char *newpath) {
    GET_CHECK_FILENAME(link, (oldpath, newpath), oldpath, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, link, (oldpath, newpath));
    char** args = assemble_args_list(2, realrealpath(oldpath), realrealpath(newpath));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args);
}
int WRAPPER_NAME(unlink)(const char *pathname) {
    GET_CHECK_FILENAME(unlink, (pathname), pathname, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, unlink, (pathname));
    char** args = assemble_args_list(1, _fname);
    RECORDER_INTERCEPTOR_EPILOGUE(1, args);
}
int WRAPPER_NAME(linkat)(int fd1, const char *path1, int fd2, const char *path2, int flag) {
    RECORDER_INTERCEPTOR_PROLOGUE(int, linkat, (fd1, path1, fd2, path2, flag));
    char** args = assemble_args_list(5, itoa(fd1), realrealpath(path1), itoa(fd2), realrealpath(path2), itoa(flag));
    RECORDER_INTERCEPTOR_EPILOGUE(5, args);
}
int WRAPPER_NAME(symlink)(const char *path1, const char *path2) {
    RECORDER_INTERCEPTOR_PROLOGUE(int, symlink, (path1, path2));
    char** args = assemble_args_list(2, realrealpath(path1), realrealpath(path2));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args);
}
int WRAPPER_NAME(symlinkat)(const char *path1, int fd, const char *path2) {
    GET_CHECK_FILENAME(symlinkat, (path1, fd, path2), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(int, symlinkat, (path1, fd, path2));
    char** args = assemble_args_list(3, realrealpath(path1), _fname, realrealpath(path2));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}
ssize_t WRAPPER_NAME(readlink)(const char *path, char *buf, size_t bufsize) {
    GET_CHECK_FILENAME(readlink, (path, buf, bufsize), path, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, readlink, (path, buf, bufsize));
    char** args = assemble_args_list(3, _fname, ptoa(buf), itoa(bufsize));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}

ssize_t WRAPPER_NAME(readlinkat)(int fd, const char *path, char *buf, size_t bufsize) {
    GET_CHECK_FILENAME(readlinkat, (fd, path, buf, bufsize), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(int, readlinkat, (fd, path, buf, bufsize));
    char** args = assemble_args_list(4, _fname, realrealpath(path), ptoa(buf), itoa(bufsize));
    RECORDER_INTERCEPTOR_EPILOGUE(4, args);
}

int WRAPPER_NAME(rename)(const char *oldpath, const char *newpath) {
    RECORDER_INTERCEPTOR_PROLOGUE(int, rename, (oldpath, newpath));
    char** args = assemble_args_list(2, realrealpath(oldpath), realrealpath(newpath));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args);
}
int WRAPPER_NAME(chmod)(const char *path, mode_t mode) {
    GET_CHECK_FILENAME(chmod, (path, mode), path, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, chmod, (path, mode));
    char** args = assemble_args_list(2, _fname, itoa(mode));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args);
}
int WRAPPER_NAME(chown)(const char *path, uid_t owner, gid_t group) {
    GET_CHECK_FILENAME(chown, (path, owner, group), path, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, chown, (path, owner, group));
    char** args = assemble_args_list(3, _fname, itoa(owner), itoa(group));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}
int WRAPPER_NAME(lchown)(const char *path, uid_t owner, gid_t group) {
    GET_CHECK_FILENAME(lchown, (path, owner, group), path, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, lchown, (path, owner, group));
    char** args = assemble_args_list(3, _fname, itoa(owner), itoa(group));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}
int WRAPPER_NAME(utime)(const char *filename, const struct utimbuf *buf) {
    GET_CHECK_FILENAME(utime, (filename, buf), filename, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, utime, (filename, buf));
    char** args = assemble_args_list(2, _fname, ptoa(buf));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args);
}
DIR* WRAPPER_NAME(opendir)(const char *name) {
    GET_CHECK_FILENAME(opendir, (name), name, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(DIR*, opendir, (name));
    char** args = assemble_args_list(1, _fname);
    RECORDER_INTERCEPTOR_EPILOGUE(1, args);
}
struct dirent* WRAPPER_NAME(readdir)(DIR *dir) {
    // TODO: DIR - get path
    RECORDER_INTERCEPTOR_PROLOGUE(struct dirent*, readdir, (dir));
    char** args = assemble_args_list(1, ptoa(dir));
    RECORDER_INTERCEPTOR_EPILOGUE(1, args);
}
int WRAPPER_NAME(closedir)(DIR *dir) {
    RECORDER_INTERCEPTOR_PROLOGUE(int, closedir, (dir));
    char** args = assemble_args_list(1, ptoa(dir)); // TODO dir is not availble after a success closedir() call
    RECORDER_INTERCEPTOR_EPILOGUE(1, args);
}

/*
void WRAPPER_NAME(rewinddir)(DIR *dir) {
    // TODO
}
int WRAPPER_NAME(__xmknod)(int ver, const char *path, mode_t mode, dev_t dev) {
    RECORDER_INTERCEPTOR_PROLOGUE(int, __xmknod, (ver, path, mode, dev));
    char** args = assemble_args_list(4, itoa(ver), _fnametmp, itoa(mode), itoa(dev));
    RECORDER_INTERCEPTOR_EPILOGUE(4, args);
}
int WRAPPER_NAME(__xmknodat)(int ver, int fd, const char *path, mode_t mode, dev_t dev) {
    RECORDER_INTERCEPTOR_PROLOGUE(int, __xmknodat, (ver, fd, path, mode, dev));
    char** args = assemble_args_list(5, itoa(ver), _fname, _fnametmp, itoa(mode), itoa(dev));
    RECORDER_INTERCEPTOR_EPILOGUE(5, args);
}
*/

// Advanced File Operations
// TODO: third argument
#ifdef RECORDER_ENABLE_FCNTL_TRACE
int WRAPPER_NAME(fcntl)(int fd, int cmd, ...) {
    if(cmd==F_DUPFD || cmd==F_DUPFD_CLOEXEC || cmd==F_SETFD || cmd==F_SETFL || cmd==F_SETOWN) {            // arg: int
        va_list arg;
        va_start(arg, cmd);
        int val = va_arg(arg, int);
        va_end(arg);

        GET_CHECK_FILENAME(fcntl, (fd, cmd, val), &fd, ARG_TYPE_FD);

        RECORDER_INTERCEPTOR_PROLOGUE(int, fcntl, (fd, cmd, val));
        char** args = assemble_args_list(3, _fname, itoa(cmd), itoa(val));
        RECORDER_INTERCEPTOR_EPILOGUE(3, args);
    } else if(cmd==F_GETFD || cmd==F_GETFL || cmd==F_GETOWN) {                     // arg: void

        GET_CHECK_FILENAME(fcntl, (fd, cmd), &fd, ARG_TYPE_FD);

        RECORDER_INTERCEPTOR_PROLOGUE(int, fcntl, (fd, cmd));
        char** args = assemble_args_list(2, _fname, itoa(cmd));
        RECORDER_INTERCEPTOR_EPILOGUE(2, args);
    } else if(cmd==F_SETLK || cmd==F_SETLKW || cmd==F_GETLK) {
        va_list arg;
        va_start(arg, cmd);
        struct flock *lk = va_arg(arg, struct flock*);
        va_end(arg);

        GET_CHECK_FILENAME(fcntl, (fd, cmd, lk), &fd, ARG_TYPE_FD);

        RECORDER_INTERCEPTOR_PROLOGUE(int, fcntl, (fd, cmd, lk));
        char** args = assemble_args_list(3, _fname, itoa(cmd), itoa(lk->l_type));
        RECORDER_INTERCEPTOR_EPILOGUE(3, args);
    } else {                        // assume arg: void, cmd==F_GETOWN_EX || cmd==F_SETOWN_EX ||cmd==F_GETSIG || cmd==F_SETSIG)
        GET_CHECK_FILENAME(fcntl, (fd, cmd), &fd, ARG_TYPE_FD);
        RECORDER_INTERCEPTOR_PROLOGUE(int, fcntl, (fd, cmd));
        char** args = assemble_args_list(2, _fname, itoa(cmd));
        RECORDER_INTERCEPTOR_EPILOGUE(2, args);
    }
}
#endif

int WRAPPER_NAME(dup)(int oldfd) {
    GET_CHECK_FILENAME(dup, (oldfd), &oldfd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(int, dup, (oldfd));
    add_to_map(_fname, &res, ARG_TYPE_FD);
    char** args = assemble_args_list(1, itoa(oldfd));
    RECORDER_INTERCEPTOR_EPILOGUE(1, args);
}
int WRAPPER_NAME(dup2)(int oldfd, int newfd) {
    GET_CHECK_FILENAME(dup2, (oldfd, newfd), &oldfd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(int, dup2, (oldfd, newfd));
    add_to_map(_fname, &res, ARG_TYPE_FD);
    char** args = assemble_args_list(2, itoa(oldfd), itoa(newfd));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args);
}
int WRAPPER_NAME(pipe)(int pipefd[2]) {
    // TODO: pipefd?
    RECORDER_INTERCEPTOR_PROLOGUE(int, pipe, (pipefd));
    char** args = assemble_args_list(2, itoa(pipefd[0]), itoa(pipefd[1]));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args);
}
int WRAPPER_NAME(mkfifo)(const char *pathname, mode_t mode) {
    GET_CHECK_FILENAME(mkfifo, (pathname, mode), pathname, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, mkfifo, (pathname, mode));
    char** args = assemble_args_list(2, _fname, itoa(mode));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args);
}
mode_t WRAPPER_NAME(umask)(mode_t mask) {
    RECORDER_INTERCEPTOR_PROLOGUE(mode_t, umask, (mask));
    char** args = assemble_args_list(1, itoa(mask));
    RECORDER_INTERCEPTOR_EPILOGUE(1, args);
}

FILE* WRAPPER_NAME(fdopen)(int fd, const char *mode) {
    GET_CHECK_FILENAME(fdopen, (fd, mode), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(FILE*, fdopen, (fd, mode));
    add_to_map(_fname, res, ARG_TYPE_STREAM);
    char** args = assemble_args_list(2, _fname, strdup(mode));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args);
}
int WRAPPER_NAME(fileno)(FILE *stream) {
    GET_CHECK_FILENAME(fileno, (stream), stream, ARG_TYPE_STREAM);
    RECORDER_INTERCEPTOR_PROLOGUE(int, fileno, (stream));
    char** args = assemble_args_list(1, _fname);
    RECORDER_INTERCEPTOR_EPILOGUE(1, args);
}
int WRAPPER_NAME(access)(const char *path, int amode) {
    GET_CHECK_FILENAME(access, (path, amode), path, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, access, (path, amode));
    char** args = assemble_args_list(2, _fname, itoa(amode));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args);
}
int WRAPPER_NAME(faccessat)(int fd, const char *path, int amode, int flag) {
    GET_CHECK_FILENAME(faccessat, (fd, path, amode, flag), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(int, faccessat, (fd, path, amode, flag));
    char** args = assemble_args_list(4, _fname, realrealpath(path), itoa(amode), itoa(flag));
    RECORDER_INTERCEPTOR_EPILOGUE(4, args);
}
FILE* WRAPPER_NAME(tmpfile)(void) {
    // TODO get and check filename of tmpfile
    RECORDER_INTERCEPTOR_PROLOGUE(FILE*, tmpfile, ());
    char **args = NULL;
    RECORDER_INTERCEPTOR_EPILOGUE(0, args);
}
int WRAPPER_NAME(remove)(const char *path) {
    GET_CHECK_FILENAME(remove, (path), path, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, remove, (path));
    char** args = assemble_args_list(1, _fname);
    RECORDER_INTERCEPTOR_EPILOGUE(1, args)
}
int WRAPPER_NAME(truncate)(const char *path, off_t length) {
    GET_CHECK_FILENAME(truncate, (path, length), path, ARG_TYPE_PATH);
    RECORDER_INTERCEPTOR_PROLOGUE(int, truncate, (path, length));
    char** args = assemble_args_list(2, _fname, itoa(length));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args);
}
int WRAPPER_NAME(ftruncate)(int fd, off_t length) {
    GET_CHECK_FILENAME(ftruncate, (fd, length), &fd, ARG_TYPE_FD);
    RECORDER_INTERCEPTOR_PROLOGUE(int, ftruncate, (fd, length));
    char** args = assemble_args_list(2, _fname, itoa(length));
    RECORDER_INTERCEPTOR_EPILOGUE(2, args);
}

int WRAPPER_NAME(fseeko)(FILE *stream, off_t offset, int whence) {
    GET_CHECK_FILENAME(fseeko, (stream, offset, whence), stream, ARG_TYPE_STREAM);
    RECORDER_INTERCEPTOR_PROLOGUE(int, fseeko, (stream, offset, whence));
    char** args = assemble_args_list(3, _fname, itoa(offset), itoa(whence));
    RECORDER_INTERCEPTOR_EPILOGUE(3, args);
}
off_t WRAPPER_NAME(ftello)(FILE *stream) {
    GET_CHECK_FILENAME(ftello, (stream), stream, ARG_TYPE_STREAM);
    RECORDER_INTERCEPTOR_PROLOGUE(long, ftello, (stream));
    char** args = assemble_args_list(1, _fname);
    RECORDER_INTERCEPTOR_EPILOGUE(1, args)
}

int WRAPPER_NAME(fflush)(FILE *stream) {
    GET_CHECK_FILENAME(fflush, (stream), stream, ARG_TYPE_STREAM);
    RECORDER_INTERCEPTOR_PROLOGUE(int, fflush, (stream));
    char** args = assemble_args_list(1, _fname);
    RECORDER_INTERCEPTOR_EPILOGUE(1, args)
}
