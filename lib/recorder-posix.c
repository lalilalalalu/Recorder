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