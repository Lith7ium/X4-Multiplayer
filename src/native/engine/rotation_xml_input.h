/* Original, read-only saved-Euler adapter helpers. No hooks or native writes.
 * The caller supplies a guarded byte-copy reader, never a dereference macro.
 * Layouts are pinned separately; this API is not a version/build detector. */
#ifndef X4MP_ROTATION_XML_INPUT_H
#define X4MP_ROTATION_XML_INPUT_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define X4RX_TEXT_MAX 256u
#define X4RX_VECTOR_MAX 32u
#define X4RX_CAPACITY_MAX 1024u
#define X4RX_RECORD_MAX 192u
#define X4RX_BYTES_MAX 8192u
#define X4RX_BANK_SIZE 0x1a0u

enum x4rx_result {
    X4RX_OK = 0, X4RX_KEEP_NATIVE = 1,
    X4RX_ARGUMENT = -1, X4RX_READ = -2, X4RX_BOUNDS = -3,
    X4RX_SHAPE = -4, X4RX_DUPLICATE = -5, X4RX_TEXT = -6,
    X4RX_CHANGED = -7, X4RX_STOP = -8, X4RX_GUARD = -9,
    X4RX_EVALUATOR = -10
};
enum x4rx_platform { X4RX_WINDOWS = 1, X4RX_LINUX = 2 };
enum x4rx_source { X4RX_OFFSET_NODE = 1, X4RX_ROTATION_NODE = 2 };
enum x4rx_duplicate_policy { X4RX_DUPLICATES_REFUSE = 0, X4RX_FIRST_ATTRIBUTE = 1 };
enum x4rx_axis { X4RX_PITCH = 0, X4RX_YAW = 1, X4RX_ROLL = 2 };

struct x4rx_layout {
    unsigned platform;
    uint16_t children, child_capacity, child_count;
    uint16_t attributes, attr_capacity, attr_count;
};
static const struct x4rx_layout x4rx_windows = { X4RX_WINDOWS, 0xc8, 0xe8, 0xf0, 0x18, 0xb8, 0xc0 };
static const struct x4rx_layout x4rx_linux = { X4RX_LINUX, 0xd8, 0xf8, 0x100, 0x20, 0xc0, 0xc8 };

struct x4rx_memory {
    void *context;
    int (*read)(void *, uint64_t, void *, size_t);
    int (*stopped)(void *);
};
struct x4rx_attribute { unsigned present; size_t length; char text[X4RX_TEXT_MAX + 1]; };
struct x4rx_input {
    unsigned orientation; /* 0 = absent element, 1 = present Euler element. */
    unsigned duplicate_attributes;
    struct x4rx_attribute angles[3]; /* pitch, yaw, roll; owned bytes. */
};
struct x4rx_record { uint64_t address; uint16_t length, offset; };
struct x4rx_snapshot {
    struct x4rx_input input;
    unsigned records, bytes;
    struct x4rx_record record[X4RX_RECORD_MAX];
    unsigned char data[X4RX_BYTES_MAX];
};

static inline uint32_t x4rx_u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline uint64_t x4rx_u64(const unsigned char *p) {
    return (uint64_t)x4rx_u32(p) | (uint64_t)x4rx_u32(p + 4) << 32;
}
static inline int x4rx_address(uint64_t base, uint64_t offset, size_t length, uint64_t *out) {
    if (!base || offset > UINT64_MAX - base) return X4RX_BOUNDS;
    *out = base + offset;
    if (!length || (uint64_t)(length - 1) > UINT64_MAX - *out) return X4RX_BOUNDS;
    return X4RX_OK;
}
static inline int x4rx_copy(const struct x4rx_memory *m, uint64_t a, void *out, size_t n) {
    uint64_t checked;
    if (!m || !m->read || !out) return X4RX_ARGUMENT;
    if (m->stopped && m->stopped(m->context)) return X4RX_STOP;
    if (x4rx_address(a, 0, n, &checked)) return X4RX_BOUNDS;
    if (!m->read(m->context, checked, out, n)) return X4RX_READ;
    return m->stopped && m->stopped(m->context) ? X4RX_STOP : X4RX_OK;
}
static inline int x4rx_record_read(const struct x4rx_memory *m, struct x4rx_snapshot *s,
                                 uint64_t base, uint64_t offset, void *out, size_t n) {
    uint64_t a;
    int r = x4rx_address(base, offset, n, &a);
    if (r) return r;
    if (s->records >= X4RX_RECORD_MAX || n > X4RX_BYTES_MAX - s->bytes || n > UINT16_MAX)
        return X4RX_BOUNDS;
    r = x4rx_copy(m, a, out, n);
    if (r) return r;
    s->record[s->records].address = a;
    s->record[s->records].length = (uint16_t)n;
    s->record[s->records].offset = (uint16_t)s->bytes;
    memcpy(s->data + s->bytes, out, n);
    s->bytes += (unsigned)n;
    s->records++;
    return X4RX_OK;
}
static inline int x4rx_layout_valid(const struct x4rx_layout *l) {
    const struct x4rx_layout *known;
    if (!l) return 0;
    known = l->platform == X4RX_WINDOWS ? &x4rx_windows : l->platform == X4RX_LINUX ? &x4rx_linux : NULL;
    return known && l->children == known->children && l->child_capacity == known->child_capacity &&
        l->child_count == known->child_count && l->attributes == known->attributes &&
        l->attr_capacity == known->attr_capacity && l->attr_count == known->attr_count;
}
static inline int x4rx_vector(const struct x4rx_memory *m, struct x4rx_snapshot *s, uint64_t node,
                             unsigned storage, unsigned capacity, unsigned count, unsigned stride,
                             uint64_t *start, unsigned *size) {
    unsigned char b[8]; uint64_t cap, n, end; int r;
    r = x4rx_record_read(m, s, node, capacity, b, 8); if (r) return r;
    cap = x4rx_u64(b);
    r = x4rx_record_read(m, s, node, count, b, 8); if (r) return r;
    n = x4rx_u64(b);
    if (cap < 4 || cap > X4RX_CAPACITY_MAX || n > cap || n > X4RX_VECTOR_MAX) return X4RX_BOUNDS;
    if (cap == 4) {
        r = x4rx_address(node, storage, 1, start); if (r) return r;
    } else {
        r = x4rx_record_read(m, s, node, storage, b, 8); if (r) return r;
        *start = x4rx_u64(b);
        if (!*start) return X4RX_BOUNDS;
    }
    /* Validate the occupied span even when entries will be read individually. */
    if (n && x4rx_address(*start, 0, (size_t)n * stride, &end)) return X4RX_BOUNDS;
    *size = (unsigned)n;
    return X4RX_OK;
}
static inline int x4rx_plain(const char *p, size_t n) {
    size_t i = 0; unsigned digits = 0, dot = 0;
    if (!n) return 1;
    if (p[0] == '-') i++;
    for (; i < n; ++i) {
        if (p[i] >= '0' && p[i] <= '9') digits++;
        else if (p[i] == '.' && !dot) dot = 1;
        else return 0;
    }
    return digits != 0;
}
static inline int x4rx_string(const struct x4rx_memory *m, struct x4rx_snapshot *s,
                             const struct x4rx_layout *l, uint64_t entry,
                             const unsigned char raw[40], struct x4rx_attribute *out) {
    uint64_t p, n, cap, inline_at; int r;
    if (l->platform == X4RX_WINDOWS) {
        n = x4rx_u64(raw + 24); cap = x4rx_u64(raw + 32);
        if (cap < 16) {
            r = x4rx_address(entry, 8, 1, &p); if (r) return r;
        } else p = x4rx_u64(raw + 8);
    } else {
        p = x4rx_u64(raw + 8); n = x4rx_u64(raw + 16);
        r = x4rx_address(entry, 24, 1, &inline_at); if (r) return r;
        cap = p == inline_at ? 15 : x4rx_u64(raw + 24);
    }
    if (!p || n > X4RX_TEXT_MAX || n > cap || cap > X4RX_BYTES_MAX) return X4RX_BOUNDS;
    r = x4rx_record_read(m, s, p, 0, out->text, (size_t)n + 1); if (r) return r;
    if (out->text[n] != '\0' || !x4rx_plain(out->text, (size_t)n)) return X4RX_TEXT;
    out->length = (size_t)n; out->present = 1;
    return X4RX_OK;
}
static inline int x4rx_rotation(const struct x4rx_memory *m, struct x4rx_snapshot *s,
                               const struct x4rx_layout *l, uint64_t node, unsigned duplicates) {
    unsigned char raw[40]; uint64_t attrs, children; unsigned n, child_n, i; int r;
    r = x4rx_vector(m, s, node, l->children, l->child_capacity, l->child_count, 8, &children, &child_n);
    if (r) return r;
    if (child_n) return X4RX_SHAPE;
    r = x4rx_vector(m, s, node, l->attributes, l->attr_capacity, l->attr_count, 40, &attrs, &n);
    if (r) return r;
    s->input.orientation = 1;
    for (i = 0; i < n; ++i) {
        uint32_t tag; uint64_t a; unsigned axis; struct x4rx_attribute text;
        r = x4rx_address(attrs, (uint64_t)i * 40, 40, &a); if (r) return r;
        r = x4rx_record_read(m, s, a, 0, raw, 40); if (r) return r;
        tag = x4rx_u32(raw);
        if (tag == 0x32e) axis = X4RX_PITCH;
        else if (tag == 0x532) axis = X4RX_YAW;
        else if (tag == 0x3e2) axis = X4RX_ROLL;
        else return X4RX_SHAPE;
        memset(&text, 0, sizeof(text));
        r = x4rx_string(m, s, l, a, raw, &text); if (r) return r;
        if (s->input.angles[axis].present) {
            s->input.duplicate_attributes++;
            if (duplicates == X4RX_DUPLICATES_REFUSE) return X4RX_DUPLICATE;
        } else s->input.angles[axis] = text; /* Native first-match selection. */
    }
    return X4RX_OK;
}

/* On failure the caller's snapshot is untouched. FIRST_ATTRIBUTE is diagnostic
 * only; the proposed exact-snapshot native mode must use DUPLICATES_REFUSE. */
static inline int x4rx_capture(const struct x4rx_memory *m, const struct x4rx_layout *l,
                              uint64_t node, unsigned source, unsigned duplicates,
                              struct x4rx_snapshot *out) {
    struct x4rx_snapshot s; unsigned char b[8]; uint64_t children, attrs, rotation = 0;
    unsigned n, attr_n, i, positions = 0; int r;
    if (!out || !x4rx_layout_valid(l) || (source != X4RX_OFFSET_NODE && source != X4RX_ROTATION_NODE) ||
        duplicates > X4RX_FIRST_ATTRIBUTE) return X4RX_ARGUMENT;
    memset(&s, 0, sizeof(s));
    r = x4rx_record_read(m, &s, node, 12, b, 4); if (r) return r;
    if (x4rx_u32(b) != (source == X4RX_OFFSET_NODE ? 0x2dcu : 0x3e7u)) return X4RX_SHAPE;
    if (source == X4RX_ROTATION_NODE) rotation = node;
    else {
        r = x4rx_vector(m, &s, node, l->attributes, l->attr_capacity, l->attr_count, 40, &attrs, &attr_n);
        if (r) return r;
        if (attr_n) return X4RX_SHAPE;
        r = x4rx_vector(m, &s, node, l->children, l->child_capacity, l->child_count, 8, &children, &n);
        if (r) return r;
        for (i = 0; i < n; ++i) {
            uint64_t child; uint32_t tag;
            r = x4rx_record_read(m, &s, children, (uint64_t)i * 8, b, 8); if (r) return r;
            child = x4rx_u64(b);
            r = x4rx_record_read(m, &s, child, 12, b, 4); if (r) return r;
            tag = x4rx_u32(b);
            if (tag == 0x340) { if (++positions > 1) return X4RX_DUPLICATE; }
            else if (tag == 0x3e7) { if (rotation) return X4RX_DUPLICATE; rotation = child; }
            else return X4RX_SHAPE; /* Includes quaternion: not this capability. */
        }
    }
    if (rotation) { r = x4rx_rotation(m, &s, l, rotation, duplicates); if (r) return r; }
    *out = s;
    return rotation ? X4RX_OK : X4RX_KEEP_NATIVE;
}

/* Replays exactly the bounded input bytes AND their structural addresses.
 * A disappeared page, reused node or altered string invalidates the capture.
 * This is not a synchronization primitive; a future wrapper must also establish
 * native owner/exclusion before reading and before applying any plan. */
static inline int x4rx_unchanged(const struct x4rx_memory *m, const struct x4rx_snapshot *s) {
    unsigned i; unsigned char b[X4RX_TEXT_MAX + 1];
    if (!s || s->records > X4RX_RECORD_MAX || s->bytes > X4RX_BYTES_MAX) return X4RX_ARGUMENT;
    for (i = 0; i < s->records; ++i) {
        const struct x4rx_record *r = &s->record[i]; int result;
        if (!r->length || r->length > sizeof(b) || r->offset > s->bytes || r->length > s->bytes - r->offset)
            return X4RX_ARGUMENT;
        result = x4rx_copy(m, r->address, b, r->length); if (result) return result;
        if (memcmp(b, s->data + r->offset, r->length)) return X4RX_CHANGED;
    }
    return X4RX_OK;
}

/* These facts must come from an independently verified native Load latch and
 * manager ownership adapter. A loading flag alone cannot populate this proof. */
struct x4rx_guard {
    uint64_t load_serial, owner_thread, current_thread, module, manager;
    unsigned active, stopped, failed;
};
static inline int x4rx_guard_valid(const struct x4rx_guard *g) {
    return g && g->load_serial && g->owner_thread && g->owner_thread == g->current_thread &&
        g->module && g->manager && g->active == 1 && !g->stopped && !g->failed;
}
static inline int x4rx_same_guard(const struct x4rx_guard *a, const struct x4rx_guard *b) {
    return x4rx_guard_valid(a) && x4rx_guard_valid(b) && a->load_serial == b->load_serial &&
        a->owner_thread == b->owner_thread && a->module == b->module && a->manager == b->manager;
}
struct x4rx_word { uint16_t offset; uint32_t before, candidate; };
struct x4rx_plan { struct x4rx_word words[9]; };
typedef int (*x4rx_evaluate_fn)(void *, const struct x4rx_input *, uint32_t[9]);
typedef void (*x4rx_original_fn)(void *);
typedef int (*x4rx_guard_fn)(void *, struct x4rx_guard *);

/* Pure orchestration for an owned-memory original in offline tests. It does NOT
 * apply the plan and is NOT a ready-to-hook native ABI wrapper. Original is
 * called exactly once even on capture/guard/evaluator/STOP refusal. A nonreturning
 * original cannot publish a plan. No lock is acquired across any callback. */
static inline int x4rx_after_original_plan(const struct x4rx_memory *m, const struct x4rx_layout *l,
        uint64_t node, unsigned source, uint64_t bank, x4rx_guard_fn guard, void *guard_context,
        x4rx_evaluate_fn evaluate, void *evaluate_context, x4rx_original_fn original,
        void *original_context, struct x4rx_plan *out) {
    struct x4rx_guard before, after; struct x4rx_snapshot snapshot;
    struct x4rx_plan plan; uint32_t candidate[9]; int result, r; unsigned i;
    if (!original || !out || !guard || !evaluate) return X4RX_ARGUMENT;
    memset(&before, 0, sizeof(before)); memset(&after, 0, sizeof(after));
    result = guard(guard_context, &before) && x4rx_guard_valid(&before) ? X4RX_OK : X4RX_GUARD;
    if (!result) result = x4rx_capture(m, l, node, source, X4RX_DUPLICATES_REFUSE, &snapshot);
    if (!result && evaluate(evaluate_context, &snapshot.input, candidate) != 0) result = X4RX_EVALUATOR;
    original(original_context);
    if (result < 0) return result;
    if (!guard(guard_context, &after) || !x4rx_same_guard(&before, &after)) return X4RX_GUARD;
    r = x4rx_unchanged(m, &snapshot); if (r) return r;
    if (result == X4RX_KEEP_NATIVE) return result;
    for (i = 0; i < 9; ++i) {
        unsigned char b[4]; uint64_t a;
        /* All scalar candidate words must be finite IEEE binary32. */
        if ((candidate[i] & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000)) return X4RX_EVALUATOR;
        plan.words[i].offset = (uint16_t)(0x10 + (i / 3) * 16 + (i % 3) * 4);
        r = x4rx_address(bank, plan.words[i].offset, 4, &a); if (r) return r;
        r = x4rx_copy(m, a, b, 4); if (r) return r;
        plan.words[i].before = x4rx_u32(b); plan.words[i].candidate = candidate[i];
    }
    r = x4rx_unchanged(m, &snapshot); if (r) return r;
    /* Guard again after the output reads: STOP during a reader is not a plan. */
    if (!guard(guard_context, &after) || !x4rx_same_guard(&before, &after)) return X4RX_GUARD;
    *out = plan;
    return X4RX_OK;
}
#endif
