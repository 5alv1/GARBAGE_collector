#include "library.h"

// gc_proto.c
// Minimal lazy, reference-based "conservative" GC wrapper prototype.
// Single-threaded; bounds-checked read/write; deferred reclamation via gc_collect().

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct GCRegion GCRegion;
typedef struct GCRef    GCRef;

struct GCRegion {
    void    *ptr;           // payload
    size_t   size;          // payload size
    size_t   strong_refs;   // number of live GCRef pointing here
    bool     logically_freed; // user requested free
    GCRegion *next;         // intrusive list link
};

struct GCRef {
    GCRegion *region;       // target region
    GCRef    *next;         // intrusive list link (for debugging/diagnostics)
};

// GC global state (simple prototype)
static struct {
    GCRegion *regions_head; // all regions ever allocated and not yet reclaimed
    GCRef    *refs_head;    // all active references (optional; helps debug)
    size_t    region_count;
    size_t    ref_count;
    size_t    bytes_in_use; // sum of region->size that are not yet reclaimed
} GC = {0};

// ---------- Helpers ----------

static GCRegion *gc_make_region(size_t size) {
    GCRegion *r = (GCRegion*)calloc(1, sizeof(GCRegion));
    if (!r) return NULL;
    r->ptr = malloc(size);
    if (!r->ptr) { free(r); return NULL; }
    r->size = size;
    r->strong_refs = 0;
    r->logically_freed = false;
    // push front
    r->next = GC.regions_head;
    GC.regions_head = r;
    GC.region_count++;
    GC.bytes_in_use += size;
    return r;
}

static void gc_unlink_region(GCRegion *r) {
    GCRegion **cur = &GC.regions_head;
    while (*cur) {
        if (*cur == r) {
            *cur = r->next;
            r->next = NULL;
            GC.region_count--;
            GC.bytes_in_use -= r->size;
            free(r->ptr);
            free(r);
            return;
        }
        cur = &(*cur)->next;
    }
}

static GCRef *gc_make_ref(GCRegion *r) {
    if (!r) return NULL;
    GCRef *ref = (GCRef*)calloc(1, sizeof(GCRef));
    if (!ref) return NULL;
    ref->region = r;
    // reference list (optional)
    ref->next = GC.refs_head;
    GC.refs_head = ref;
    GC.ref_count++;
    r->strong_refs++;
    return ref;
}

static void gc_unlink_ref(GCRef *ref) {
    // remove from ref list
    GCRef **cur = &GC.refs_head;
    while (*cur) {
        if (*cur == ref) {
            *cur = ref->next;
            ref->next = NULL;
            GC.ref_count--;
            break;
        }
        cur = &(*cur)->next;
    }
    if (ref->region) {
        if (ref->region->strong_refs > 0) ref->region->strong_refs--;
        ref->region = NULL;
    }
    free(ref);
}

// ---------- Public API ----------

// Allocate a new memory region and return its first reference
GCRef* gc_alloc(size_t size) {
    GCRegion *r = gc_make_region(size);
    if (!r) return NULL;
    return gc_make_ref(r);
}

// Create a new (additional) reference to the same region
GCRef* gc_new_ref(GCRef *ref) {
    if (!ref || !ref->region) return NULL;
    return gc_make_ref(ref->region);
}

// Drop/destroy a reference you previously created
void gc_drop_ref(GCRef *ref) {
    if (!ref) return;
    gc_unlink_ref(ref);
}

// Logical free: mark the region as no longer needed by the owner of `ref`.
// Actual memory is reclaimed lazily by gc_collect() when there are zero references.
void gc_free(GCRef *ref) {
    if (!ref || !ref->region) return;
    ref->region->logically_freed = true;
    // Note: we do NOT drop the caller's ref automatically—callers should
    // typically `gc_drop_ref(ref)` after calling gc_free(ref) if they
    // no longer want to hold the reference. Keeping it is allowed too.
}

// Bounds-checked write: returns bytes written (0 on error)
size_t gc_write(GCRef *ref, size_t offset, const void *src, size_t nbytes) {
    if (!ref || !ref->region || !ref->region->ptr || !src) return 0;
    GCRegion *r = ref->region;
    if (offset >= r->size) return 0;
    size_t room = r->size - offset;
    size_t to_copy = nbytes <= room ? nbytes : room;
    memcpy((uint8_t*)r->ptr + offset, src, to_copy);
    return to_copy;
}

// Bounds-checked read: returns bytes read (0 on error)
size_t gc_read(GCRef *ref, size_t offset, void *dst, size_t nbytes) {
    if (!ref || !ref->region || !ref->region->ptr || !dst) return 0;
    GCRegion *r = ref->region;
    if (offset >= r->size) return 0;
    size_t room = r->size - offset;
    size_t to_copy = nbytes <= room ? nbytes : room;
    memcpy(dst, (uint8_t*)r->ptr + offset, to_copy);
    return to_copy;
}

// Convenience accessors
void*  gc_ptr(GCRef *ref)  { return (ref && ref->region) ? ref->region->ptr  : NULL; }
size_t gc_size(GCRef *ref) { return (ref && ref->region) ? ref->region->size : 0;   }

// Trigger lazy collection: any region with (strong_refs == 0) AND logically_freed is reclaimed.
// If you want stricter behavior, set logically_freed by default and require explicit roots;
// this prototype follows a ref-based approach to keep the API small.
void gc_collect(void) {
    GCRegion *cur = GC.regions_head;
    GCRegion *next = NULL;
    while (cur) {
        next = cur->next;
        if (cur->logically_freed && cur->strong_refs == 0) {
            gc_unlink_region(cur);
        }
        cur = next;
    }
}

// Optional diagnostics
void gc_dump_stats(FILE *out) {
    if (!out) out = stderr;
    size_t regions = 0, live_refs = 0, pending = 0;
    for (GCRegion *r = GC.regions_head; r; r = r->next) {
        regions++;
        if (r->logically_freed && r->strong_refs == 0) pending++;
    }
    for (GCRef *p = GC.refs_head; p; p = p->next) live_refs++;
    fprintf(out, "[GC] regions=%zu, refs=%zu, bytes_in_use=%zu, reclaimable=%zu\n",
            regions, live_refs, GC.bytes_in_use, pending);
}

// Cleanup everything (force-free all regions regardless of flags)
// Call near program shutdown if desired.
void gc_shutdown(void) {
    // free all refs
    while (GC.refs_head) {
        gc_drop_ref(GC.refs_head);
    }
    // free all regions
    while (GC.regions_head) {
        gc_unlink_region(GC.regions_head);
    }
}

// ---------- Tiny usage example (compile with -DGC_PROTO_MAIN to run) ----------
#ifdef GC_PROTO_MAIN
int main(void) {
    GCRef *r = gc_alloc(16);
    if (!r) return 1;

    const char *msg = "hello";
    size_t wrote = gc_write(r, 0, msg, strlen(msg)+1);
    printf("wrote %zu bytes\n", wrote);

    char buf[16] = {0};
    size_t read = gc_read(r, 0, buf, sizeof(buf));
    printf("read %zu bytes: '%s'\n", read, buf);

    // create another reference
    GCRef *r2 = gc_new_ref(r);

    // logical free by owner of r; keep r2 alive
    gc_free(r);
    gc_drop_ref(r); // drop owner's handle

    gc_dump_stats(stdout); // not reclaimed yet; r2 still references it

    gc_drop_ref(r2);       // now no references remain
    gc_collect();          // lazy free happens here

    gc_dump_stats(stdout);
    gc_shutdown();         // safe to call even when clean
    return 0;
}
#endif
