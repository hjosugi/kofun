/*
 * kbfix — pinned C fixture library for the `kofun bindgen-c` stage-1 gate
 * (issue #574).
 *
 * The header is deliberately self-contained (no #include) so the clang AST
 * holds exactly these declarations, and it exercises both halves of the
 * stage-1 contract:
 *
 *   bound        opaque handle create/destroy, status-code returns, a
 *                buffer+length function, a callback typedef with a documented
 *                lifetime, a library-owned/client-owned cleanup pair, and a
 *                fixed-layout record passed by value;
 *
 *   audit-only   a macro constant, a function-like macro, a variadic
 *                function, a union, a bitfield, a flexible array member, and
 *                an inline function, and a non-default calling convention.
 *                These must appear in the audit report with reasons and must
 *                not appear in the generated module.
 */
#ifndef KBFIX_H
#define KBFIX_H

/* audit-only: object-like macro constant — macros carry no ABI. */
#define KBFIX_MAX_LABEL 32

/* audit-only: function-like macro. */
#define KBFIX_CLAMP(value, low, high) \
    ((value) < (low) ? (low) : ((value) > (high) ? (high) : (value)))

/* Opaque counter handle. CLIENT-OWNED: every handle returned by
 * kbfix_counter_new must be released with kbfix_counter_free exactly once. */
typedef struct kbfix_counter kbfix_counter_t;

/* Status codes for every fallible entry point. */
enum kbfix_status {
    KBFIX_OK = 0,
    KBFIX_ERR_NULL = -1,
    KBFIX_ERR_RANGE = -2,
    KBFIX_ERR_CAPACITY = 3
};

/* Fixed-layout record, passed and returned by value. */
typedef struct kbfix_stats {
    long total;
    int events;
    unsigned char flags;
} kbfix_stats_t;

/* Callback typedef. LIFETIME: the callback is invoked synchronously inside
 * kbfix_counter_add on the calling thread, never after
 * kbfix_counter_on_change or kbfix_counter_free returns; the library never
 * retains context beyond the registered window. */
typedef void (*kbfix_on_change)(void *context, long value);

/* CLIENT-OWNED handle pair: new allocates, free releases. free(NULL) is a
 * no-op; the counter value saturates at KBFIX_COUNTER_LIMIT. */
kbfix_counter_t *kbfix_counter_new(long initial);
void kbfix_counter_free(kbfix_counter_t *counter);

/* Status-code returns; a refused add leaves the counter unchanged. */
enum kbfix_status kbfix_counter_add(kbfix_counter_t *counter, long amount);
long kbfix_counter_value(const kbfix_counter_t *counter);

/* Record round trip by value. */
kbfix_stats_t kbfix_stats_scale(kbfix_stats_t stats, long factor);

/* Buffer + explicit length: writes at most capacity bytes of the counter's
 * label into buffer, stores the full length in out_length, and returns
 * KBFIX_ERR_CAPACITY when capacity was too small. Never NUL-terminates
 * beyond capacity. */
enum kbfix_status kbfix_label_copy(const kbfix_counter_t *counter,
                                  char *buffer,
                                  unsigned long capacity,
                                  unsigned long *out_length);

/* Registers the change callback; see the kbfix_on_change lifetime note. */
enum kbfix_status kbfix_counter_on_change(kbfix_counter_t *counter,
                                          kbfix_on_change callback,
                                          void *context);

/* LIBRARY-OWNED: the returned string is static, valid for the process
 * lifetime, and must NOT be freed by the caller. */
const char *kbfix_library_name(void);

/* Length of a NUL-terminated string; lets a caller that cannot dereference
 * pointers observe the library-owned string above. */
long kbfix_name_length(const char *name);

/* Scalar-only round trip through the target-default calling convention. */
long kbfix_scalar_roundtrip(long value);

/* audit-only: a real non-default convention on the pinned x86_64 target. */
#if defined(__x86_64__)
long __attribute__((ms_abi)) kbfix_ms_abi_probe(long value);
#endif

/* audit-only: variadic function. */
int kbfix_log(const char *format, ...);

/* audit-only: union. */
union kbfix_word {
    long as_long;
    unsigned char bytes[8];
};

/* audit-only: bitfields. */
struct kbfix_flags {
    unsigned int low : 4;
    unsigned int high : 4;
};

/* audit-only: flexible array member. */
struct kbfix_message {
    long length;
    char data[];
};

/* audit-only: inline function (no external symbol). */
static inline long kbfix_double(long value) { return value * 2; }

#ifdef KBFIX_EXTRA
/* Present only under -DKBFIX_EXTRA: proves preprocessor context changes the
 * generated artifact, not merely its recorded context block. */
long kbfix_extra_probe(long value);
#endif

#endif /* KBFIX_H */
