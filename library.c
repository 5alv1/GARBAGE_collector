#include "library.h"

// gc_proto.c
// Minimal lazy, reference-based conservative GC.
// Single-threaded; bounds-checked read/write; deferred reclamation via gc_collect().

int next_collect = 0;

#define nullptr NULL

// GC global state
static struct {
    GCRegion *regions_head; // all regions ever allocated and not yet reclaimed
    GCRef    *refs_head;    // all active references (optional; helps debug)
    size_t    region_count;
    size_t    ref_count;
    size_t    bytes_in_use; // sum of region->size that are not yet reclaimed
} GC = {
    nullptr,
    nullptr,
    0,
    0,
    0
};

// ---------- Helpers ----------

static GCRegion *gc_make_region(size_t size) {
    GCRegion *r = (GCRegion*)calloc(1, sizeof(GCRegion));
    if (!r) return nullptr;
    r->ptr = malloc(size);
    if (!r->ptr) { free(r); return nullptr; }
    r->size = size;
    r->strong_refs = 0;

    r->next = GC.regions_head;
    if (GC.regions_head) GC.regions_head->prev = r;

    GC.regions_head = r;
    return r;
}

static void gc_unlink_region(GCRegion *r) {
    free(r->ptr);
    GCRegion *prev = r->prev;
    GCRegion *next = r->next;

    if (prev && next) {
        prev->next = next;
        next->prev = prev;
    } else if (!prev) GC.regions_head = r->next;
    else prev->next = nullptr;

    free(r);
}

static GCRef *gc_make_ref(GCRegion *r) {
    if (!r) return nullptr;
    GCRef *ref = (GCRef*)calloc(1, sizeof(GCRef));
    if (!ref) return nullptr;
    ref->region = r;
    // reference list (optional)
    ref->next = GC.refs_head;
    if (GC.refs_head) GC.refs_head->prev = ref;

    GC.refs_head = ref;
    GC.ref_count++;
    r->strong_refs++;
    return ref;
}

static void gc_unlink_ref(GCRef *ref) {
    if (!ref) return;
    if (!ref->prev) GC.refs_head = ref->next;
    else if (!ref->next) ref->prev->next = nullptr;
    else {
        ref->prev->next = ref->next;
        ref->next->prev = ref->prev;
    }

    free(ref);
}

GCRef* gc_alloc(size_t size) {
    GCRegion *r = gc_make_region(size);
    if (!r) return nullptr;
    return gc_make_ref(r);
}

GCRef* gc_new_ref(GCRef *ref) {
    if (!ref || !ref->region) return nullptr;
    return gc_make_ref(ref->region);
}

void gc_free(GCRef **ref_) {
    if (!ref_ || !*ref_) return;
    GCRef *ref = *ref_;

    ref->region->strong_refs--;

    gc_unlink_ref(ref);
    *ref_ = nullptr;

    if (!next_collect) {
        gc_collect();
        next_collect = rand()%100;
    } else next_collect--;
}

size_t gc_write(GCRef *ref, size_t offset, const char *src, size_t nbytes) {
    if (!ref || !ref->region || !ref->region->ptr || !src) return 0;
    GCRegion *r = ref->region;
    if (offset + nbytes > r->size) return 0;
    memcpy((uint8_t*)r->ptr + offset, src, nbytes);
    return nbytes;
}

size_t gc_read(GCRef *ref, size_t offset, char *dst, size_t nbytes) {
    if (!ref || !ref->region || !ref->region->ptr || !dst) return 0;
    GCRegion *r = ref->region;
    if (offset + nbytes > r->size) return 0;
    memcpy(dst, (uint8_t*)r->ptr + offset, nbytes);
    return nbytes;
}

void gc_collect(void) {
    GCRegion *cur = GC.regions_head;
    GCRegion *next = nullptr;
    while (cur) {
        next = cur->next;
        if (cur->strong_refs == 0) {
            gc_unlink_region(cur);
        }
        cur = next;
    }
}

void gc_dump_stats(FILE *out) {
    if (!out) out = stderr;
    size_t regions = 0, live_refs = 0, pending = 0;
    for (GCRegion *r = GC.regions_head; r; r = r->next) {
        regions++;
        if (r->strong_refs == 0) pending++;
    }
    for (GCRef *p = GC.refs_head; p; p = p->next) live_refs++;
    fprintf(out, "[GC] regions=%zu, refs=%zu, bytes_in_use=%zu, reclaimable=%zu\n",
            regions, live_refs, GC.bytes_in_use, pending);
    fprintf(out, "[GC] Until next collect=%d\n", next_collect);
    fflush(out);
}

// UNSAFE
void *to_raw(GCRef *ref) {
    if (!ref || !ref->region || !ref->region->ptr) return nullptr;
    return ref->region->ptr;
}

// ---------- Tiny usage example (compile with -DGC_PROTO_MAIN to run) ----------
#ifdef GC_PROTO_MAIN
int main(void) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    GCRef *r = gc_alloc(16);
    if (!r) return 1;

    const char *msg = "hello";
    size_t wrote = gc_write(r, 0, msg, strlen(msg)+1);
    printf("wrote %zu bytes\n", wrote);

    char buf[16] = {0};
    size_t read = gc_read(r, 0, buf, 16);
    printf("read %zu bytes: '%s'\n", read, buf);

    // create another reference
    GCRef *r2 = gc_new_ref(r);
    GCRef *r3 = gc_new_ref(r2);

    // logical free by owner of r; keep r2 alive
    gc_free(&r);
    // gc_drop_ref(r); // drop owner's handle

    gc_dump_stats(stdout); // not reclaimed yet; r2 still references it

    // gc_drop_ref(r2);       // now no references remain
    gc_collect();          // lazy free happens here

    gc_free(&r2);
    gc_dump_stats(stdout);

    gc_dump_stats(stdout);
    // gc_shutdown();         // safe to call even when clean
    return 0;
}
#endif
