#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include "recorder.h"

/* Global hander for the local trace log */
FILE *__recorderfh;
hashmap_map *func2id_map;

typedef struct IoOperation {
    unsigned char func_id;
    unsigned char filename_id;
    double start_time;
    double end_time;
    /**
     * attr1 = offset for read/write/seek
     * attr1 = mode for open
     *
     * attr2 = counts for read/write
     * attr2 = whence for seek
     */
    size_t attr1;
    size_t attr2;
} IoOperation_t;


static inline unsigned char get_func_id(const char *func) {
    int id = -1;
    if (!func || !func2id_map)
        return id;

    /* filename already exists */
    if (hashmap_get(func2id_map, func, &id) == MAP_OK)
        return id;

    /* insert filename into the map */
    id = hashmap_length(func2id_map);
    hashmap_put(func2id_map, func, id);
    return id;
}

static inline unsigned char get_filename_id(const char *filename) {
    return 1;
}


static int(*__real_fwrite) (const void * ptr, size_t size, size_t count, FILE * stream) = NULL;
int write_to_file(IoOperation_t *op) {

    if (!__real_fwrite)
        __real_fwrite = dlsym(RTLD_NEXT, "fwrite");

    printf("size of IoOperation: %ld bytes\n", sizeof(IoOperation_t));
    if (__recorderfh) {
        __real_fwrite(op, sizeof(IoOperation_t), 1, __recorderfh);
    }
    return 0;
}

/*
 * read, write, seek
 */
void write_data_operation(const char *func, const char *filename, double start, double end, size_t offset, size_t count_or_whence) {
    printf("%s ", func);
    IoOperation_t op = {
        .func_id = get_func_id(func),
        .filename_id = get_filename_id(filename),
        .start_time = start,
        .end_time = end,
        .attr1 = offset,
        .attr2 = count_or_whence
    };
    write_to_file(&op);
}
void write_meta_operation(const char *func, const char *filename, double start, double end, unsigned char mode) {
    IoOperation_t op = {
        .func_id = get_func_id(func),
        .filename_id = get_filename_id(filename),
        .start_time = start,
        .end_time = end,
        .attr1 = (size_t) mode,
    };
    write_to_file(&op);
}
