#ifndef GARBAGE_COLLECTOR_LIBRARY_H
#define GARBAGE_COLLECTOR_LIBRARY_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/**
 * This is a type that represents a memory region, user should not interact with it directly.
 */
typedef struct GCRegion {
    void    *ptr;           // payload
    size_t   size;          // payload size
    size_t   strong_refs;   // number of live GCRef pointing here
    struct GCRegion *next;         // intrusive list link
    struct GCRegion *prev;
} GCRegion;

/**
 * This is a type that represents a strong reference to a memory region
 */
typedef struct GCRef {
    GCRegion *region;       // target region
    struct GCRef    *next;         // intrusive list link (for debugging/diagnostics)
    struct GCRef    *prev;
} GCRef;

/**
 * Collects all the memory regions with no live references
 */
void gc_collect(void);

/**
 * This frees a reference, freeing enough references to a certain memory region will make it eligible for
 * collection, example code:
 *
 * GCRef *ref = gc_alloc(16) // reference initialization
 * // do stuff with the reference
 * gc_free(&ref);
 * // the ref pointer will be zeroed
 *
 * @param ref_ a pointer to a pointer of a reference, so we can zero out the variable with a reference in it
 */
void gc_free(GCRef **ref_);

/**
 * This makes you allocate a region and gives you a reference to it
 *
 * @param size the size of the wanted region
 * @return a pointer to a reference of the allocated memory region
 */
GCRef* gc_alloc(size_t size);

/**
 * This function gives you the possibility of copying a reference to a certain region, this will increase
 * the counter of live references
 *
 * @param ref the reference you want to copy
 * @return the new copy of the reference
 */
GCRef* gc_new_ref(GCRef *ref);

/**
 * This function gives you the possibility of writing data to a memory region
 *
 * @param ref the reference of the region you want to write into
 * @param offset the offset you want to start at
 * @param src the source of the data you want to write
 * @param nbytes the number of bytes you want to write
 * @return the number of written bytes
 */
size_t gc_write(GCRef *ref, size_t offset, const char *src, size_t nbytes);

/**
 * This function makes you read from a region
 *
 * @param ref the reference of the region you want to read data from
 * @param offset the offset you want the read to start from
 * @param dst the destination of the data
 * @param nbytes the number of bytes you want to read
 * @return the number of bytes that were read
 */
size_t gc_read(GCRef *ref, size_t offset, char *dst, size_t nbytes);

/**
 * This function gives you the possibility of dumping the GC stats into a file
 *
 * @param out file where you want to dump the stats at
 */
void gc_dump_stats(FILE *out);

/**
 * This function should be used as rarely as possible, since it's possible to make the GC unsafe very easily,
 * this function gives you a raw pointer to a certain memory region
 *
 * @param ref reference to the region you need
 * @return raw pointer to the region
 */
void *to_raw(GCRef *ref);

#endif //GARBAGE_COLLECTOR_LIBRARY_H