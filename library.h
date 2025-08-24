#ifndef GARBAGE_COLLECTOR_LIBRARY_H
#define GARBAGE_COLLECTOR_LIBRARY_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct GCRegion {
    void    *ptr;           // payload
    size_t   size;          // payload size
    size_t   strong_refs;   // number of live GCRef pointing here
    struct GCRegion *next;         // intrusive list link
    struct GCRegion *prev;
} GCRegion;

typedef struct GCRef {
    GCRegion *region;       // target region
    struct GCRef    *next;         // intrusive list link (for debugging/diagnostics)
    struct GCRef    *prev;
} GCRef;

void gc_collect(void);
void gc_free(GCRef **ref_);
GCRef* gc_alloc(size_t size);
GCRef* gc_new_ref(GCRef *ref);

size_t gc_write(GCRef *ref, size_t offset, const char *src, size_t nbytes);
size_t gc_read(GCRef *ref, size_t offset, char *dst, size_t nbytes);

void gc_dump_stats(FILE *out);

void *to_raw(GCRef *ref);

#endif //GARBAGE_COLLECTOR_LIBRARY_H