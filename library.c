#include "library.h"

// gc_proto.c
// Minimal lazy, reference-based conservative GC.
// Single-threaded; bounds-checked read/write; deferred reclamation via gc_collect().

int next_collect = 0;

// I do this because if I use NULL the IDE complains, and if I don't use it the compiler complains instead
#define nullptr NULL

// GC global state
static struct {
    GCRegion *regions_head; // all regions ever allocated and not yet reclaimed
    GCRef    *refs_head;    // all active references
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

static GCRegion *gc_make_region(size_t size) {
    GCRegion *reg = calloc(1, sizeof(GCRegion));
    if (!reg) return nullptr;
    reg->ptr = calloc(1, size);
    if (!reg->ptr) { free(reg); return nullptr; }

    GC.bytes_in_use += size;
    reg->size = size;
    reg->strong_refs = 0;

    reg->next = GC.regions_head;
    if (GC.regions_head) GC.regions_head->prev = reg;

    GC.regions_head = reg;
    return reg;
}

static void gc_unlink_region(GCRegion *reg) {
    /**
     * Unlinking 101
     * We're trying to unlink from a double linked list, we reason by cases:
     * CASE 1: The node is NULL, return
     * CASE 2: The node has no previous node, so it must be the head, substitute the head
     * CASE 3: The node is not the head, so we attach our next node to our previous node
     *
     * No matter what case we're in, if the function does not return we are going to free the region
    */

    if ((reg->prev && reg->prev->next != reg) || (reg->next && reg->next->prev != reg)) {
        perror("Something went wrong while unlinking region");
        exit(EXIT_FAILURE);
    }

    if (!reg) return;
    if (!reg->prev) {
        GC.regions_head = reg->next;
        if (reg->next) reg->next->prev = nullptr;
    }
    else if (!reg->next) reg->prev->next = nullptr;
    else {
        reg->prev->next = reg->next;
        reg->next->prev = reg->prev;
    }

    GC.bytes_in_use -= reg->size;
    free(reg->ptr);
    free(reg);
}

static GCRef *gc_make_ref(GCRegion *reg) {
    // ReSharper disable once CppDFAConstantConditions
    if (!reg) return nullptr;
    GCRef *ref = calloc(1, sizeof(GCRef));
    if (!ref) return nullptr;
    ref->region = reg;

    // We are putting our new reference on top of the list
    ref->next = GC.refs_head;
    if (GC.refs_head) GC.refs_head->prev = ref;

    GC.refs_head = ref;
    GC.ref_count++;
    reg->strong_refs++;
    return ref;
}

static void gc_unlink_ref(GCRef *ref) {
    /**
     * Unlinking 101
     * We're trying to unlink from a double linked list, we reason by cases:
     * CASE 1: The node is NULL, return
     * CASE 2: The node has no previous node, so it must be the head, substitute the head
     * CASE 3: The node is not the head, so we attach our next node to our previous node
     *
     * No matter what case we're in, if the function does not return we are going to free the reference
    */

    if ((ref->prev && ref->prev->next != ref) || (ref->next && ref->next->prev != ref)) {
        perror("Something went wrong while unlinking reference");
        exit(EXIT_FAILURE);
    }

    if (!ref) return;
    if (!ref->prev) {
        GC.refs_head = ref->next;
        if (ref->next) ref->next->prev = nullptr;
    }
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
    if (!ref_ || !*ref_) return; // Check against null values

    GCRef *ref = *ref_;

    ref->region->strong_refs--;

    gc_unlink_ref(ref);
    *ref_ = nullptr; // Make the original variable zero (see docs for example)

    // Every once in a while we do a little collection and reset the counter
    if (!next_collect) {
        gc_collect();
        next_collect = rand()%10 + 1; // NOLINT(*-msc50-cpp)
    } else next_collect--;
}

size_t gc_write(GCRef *ref, size_t offset, const char *src, size_t nbytes) {
    if (!ref || !ref->region || !ref->region->ptr || !src) return 0; // Reference and region must be valid
    GCRegion *r = ref->region;

    // Bounds checking
    if (offset + nbytes > r->size) return 0;

    // Do the actual write operation
    memcpy((uint8_t*)r->ptr + offset, src, nbytes);
    return nbytes;
}

size_t gc_read(GCRef *ref, size_t offset, char *dst, size_t nbytes) {

    // Check against null pointers
    if (!ref || !ref->region || !ref->region->ptr || !dst) return 0;
    GCRegion *r = ref->region;

    // Bounds checking
    if (offset + nbytes > r->size) return 0;

    // DO THE THING!
    memcpy(dst, (uint8_t*)r->ptr + offset, nbytes);
    return nbytes;
}

void gc_collect(void) {
    /**
     * I don't think this necessitates much discussion,
     * what's happening is that I iterate through the regions
     * and when I encounter one that has no live reference, I unlink it
    */

    GCRegion *cur = GC.regions_head;
    GCRegion *next = nullptr;
    while (cur) {
        next = cur->next;
        if (cur->strong_refs == 0) {
            gc_unlink_region(cur);

            cur = GC.regions_head;
            continue;
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

    // logical free by owner of r; keep r2 alive
    gc_free(&r);
    // gc_drop_ref(r); // drop owner's handle

    gc_dump_stats(stdout); // not reclaimed yet; r2 still references it

    // gc_drop_ref(r2);       // now no references remain
    gc_collect();          // lazy free happens here

    gc_free(&r2);
    gc_dump_stats(stdout);

    gc_dump_stats(stdout);

    for (int i = 0; i < 6; i++) {
        puts("-----------------");
        GCRef *a = gc_alloc(16);
        gc_free(&a);
        gc_dump_stats(stdout);
    }
    puts("-----------------");

    GCRef *a = gc_alloc(16);
    GCRef *b = gc_new_ref(a);

    gc_free(&a);
    gc_dump_stats(stdout);

    gc_free(&b);
    gc_dump_stats(stdout);

    puts("SEEMS GOOD TILL HERE");
    for (int i = 0; i < 11; i++) {
        puts("-----------------");
        GCRef *a = gc_alloc(16);
        gc_free(&a);
        gc_dump_stats(stdout);
    }
    puts("-----------------");
    return 0;
}
#endif
