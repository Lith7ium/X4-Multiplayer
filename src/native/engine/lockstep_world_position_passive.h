/* One immutable, default-null passive sidecar. It never owns an original call.
 * Retained per invocation, including denied scopes and exceptional cleanup. */
struct wp_scope;
typedef struct wp_passive_callbacks {
    void (*begin)(struct wp_scope*,void*,void*,void*,void*,uintptr_t,uintptr_t);
    void (*normal)(struct wp_scope*,uintptr_t);
    void (*finish)(struct wp_scope*,unsigned);
    void (*radius_begin)(struct wp_scope*,void*,void*,void*,uintptr_t,uintptr_t);
    void (*radius_normal)(struct wp_scope*,float);
    void (*radius_finish)(struct wp_scope*,unsigned);
    void (*sample_begin)(struct wp_scope*,void*,float,unsigned,unsigned,uintptr_t,uintptr_t);
    void (*sample_normal)(struct wp_scope*,float);
    void (*sample_finish)(struct wp_scope*,unsigned);
} wp_passive_callbacks;
static const wp_passive_callbacks *wp_passive;
