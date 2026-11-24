#include "a7r3_entry_cert.h"
#include "a7r3_sys.h"

/* Verified A7R3 v3.10 viewUnified2.so offsets */
#define VU2_PROC_MGR_GUARD 0x00a48b0cUL
#define VU2_PROC_MGR_INST  0x00a48b10UL
#define VU2_CERT_GETTER    0x003ec7b9UL /* Thumb */
#define VU2_CERT_GUARD     0x00a6f380UL
#define VU2_CERT_INST      0x00a6f384UL
#define VU2_CERT_VPTR      0x0088d850UL

#define PROC_ID_CERT_LOGO  0x10f

#define CERT_VT_SLOT_GRAY  (0x38u / 4u)
#define CERT_VT_SLOT_DIS   (0x48u / 4u)
#define CERT_VT_SLOT_EXEC  (0x68u / 4u)
#define CERT_VT_COPY_WORDS 30u

#define LIBOBJ_TEXT_SETSTR_ID_GOT    0x0121aa9cUL
#define LIBOBJ_TEXT_SETSTR_ID_REAL   0x005dbe09UL /* Thumb */
#define LIBOBJ_TEXT_SETSTR_CSTR_REAL 0x005dbfa1UL /* Thumb */
#define STR_ID_CERT_LOGO             0x0204ecbau
#define STR_ID_CERT_LOGO_GUIDE       0x0204ecbbu
#define STR_ID_CERT_LOGO_CONF        0x0204ecbcu

typedef int (*pmgr_is_disable_fn)(void *mgr, int proc_id);
typedef void *(*cert_getter_fn)(void);
typedef void (*text_setstr_id_fn)(void *sprite, unsigned res_id);
typedef void (*text_setstr_cstr_fn)(void *sprite, const char *utf8);

static unsigned long g_cert_vt_raw[32];
static volatile unsigned long *g_cert_vptr_slot;
static unsigned long g_orig_cert_vptr;
static int g_cert_installed;
static volatile unsigned g_exec_count;
static volatile int g_exec_pending;

static volatile unsigned long *g_text_setstr_got;
static unsigned long g_orig_text_setstr_got;
static text_setstr_id_fn g_real_text_setstr_id;
static text_setstr_cstr_fn g_real_text_setstr_cstr;
static volatile unsigned g_label_hits;
static unsigned g_last_label_hits;

__attribute__((noinline))
static void cert_text_setstr_hook(void *sprite, unsigned res_id)
{
    if (res_id == STR_ID_CERT_LOGO && g_real_text_setstr_cstr) {
        g_label_hits++;
        a7r3_dsb();
        g_real_text_setstr_cstr(sprite, "\xe6\x8b\x93\xe5\xb1\x95\xe8\x8f\x9c\xe5\x8d\x95");
        return;
    }
    if ((res_id == STR_ID_CERT_LOGO_GUIDE || res_id == STR_ID_CERT_LOGO_CONF) &&
        g_real_text_setstr_cstr) {
        g_real_text_setstr_cstr(sprite, "\xe6\x89\x93\xe5\xbc\x80\xe6\x8b\x93\xe5\xb1\x95\xe5\x8a\x9f\xe8\x83\xbd\xe8\x8f\x9c\xe5\x8d\x95\xe3\x80\x82");
        return;
    }
    g_real_text_setstr_id(sprite, res_id);
}

static int cert_ret0(void *self)
{
    (void)self;
    return 0;
}

static int cert_exec_hook(void *self)
{
    unsigned cnt;
    (void)self;
    cnt = g_exec_count + 1u;
    g_exec_count = cnt;
    g_exec_pending = 1;
    a7r3_dsb();
    a7r3_log_hex("SDK_CERT_EXEC=", (unsigned long)cnt);
    return 1;
}

int a7r3_entry_cert_install(void)
{
    unsigned long vu2_base;
    volatile unsigned char *cert_guard;
    volatile unsigned long *cert_inst;
    volatile unsigned char *mgr_guard;
    void *mgr = NULL;
    pmgr_is_disable_fn mgr_dis = NULL;
    const unsigned long *orig_vt;
    unsigned i;

    if (g_cert_installed)
        return 0;

    vu2_base = a7r3_mod_base("/viewUnified2.so");
    if (!vu2_base) {
        a7r3_log_str("SDK_CERT_NO_VU2\n");
        return -1;
    }

    cert_guard = (volatile unsigned char *)(vu2_base + VU2_CERT_GUARD);
    cert_inst = (volatile unsigned long *)(vu2_base + VU2_CERT_INST);
    if (!a7r3_pm_readable((const void *)cert_guard, 1u) ||
        !a7r3_pm_writable((const void *)cert_inst, 4u)) {
        a7r3_log_str("SDK_CERT_BAD_ADDR\n");
        return -1;
    }

    mgr_guard = (volatile unsigned char *)(vu2_base + VU2_PROC_MGR_GUARD);
    if (a7r3_pm_readable((const void *)mgr_guard, 8u) && (*mgr_guard & 1u) != 0u) {
        void *inst = (void *)(vu2_base + VU2_PROC_MGR_INST);
        unsigned long *vt = *(unsigned long **)inst;
        if (a7r3_pm_readable(vt, 15u * 4u)) {
            mgr = inst;
            mgr_dis = (pmgr_is_disable_fn)vt[13];
        }
    }

    if (mgr && mgr_dis) {
        int before = mgr_dis(mgr, PROC_ID_CERT_LOGO);
        a7r3_log_hex("SDK_CERT_BEFORE_DIS=", (unsigned long)(unsigned int)before);
    } else {
        cert_getter_fn getter = (cert_getter_fn)(vu2_base + VU2_CERT_GETTER);
        a7r3_log_str("SDK_CERT_FALLBACK_GETTER\n");
        (void)getter();
    }

    if ((*cert_guard & 1u) == 0u || *cert_inst != (vu2_base + VU2_CERT_VPTR)) {
        a7r3_log_hex("SDK_CERT_VPTR_MISMATCH=", *cert_inst);
        return -1;
    }

    orig_vt = (const unsigned long *)(vu2_base + VU2_CERT_VPTR - 8UL);
    if (!a7r3_pm_readable(orig_vt, CERT_VT_COPY_WORDS * 4u)) {
        a7r3_log_str("SDK_CERT_VT_UNREADABLE\n");
        return -1;
    }

    for (i = 0; i < CERT_VT_COPY_WORDS; i++)
        g_cert_vt_raw[i] = orig_vt[i];

    g_cert_vt_raw[2u + CERT_VT_SLOT_GRAY] = (unsigned long)(void *)cert_ret0;
    g_cert_vt_raw[2u + CERT_VT_SLOT_DIS]  = (unsigned long)(void *)cert_ret0;
    g_cert_vt_raw[2u + CERT_VT_SLOT_EXEC] = (unsigned long)(void *)cert_exec_hook;

    g_orig_cert_vptr = *cert_inst;
    g_cert_vptr_slot = cert_inst;
    *g_cert_vptr_slot = (unsigned long)&g_cert_vt_raw[2];
    a7r3_dsb();
    g_cert_installed = 1;
    a7r3_log_hex("SDK_CERT_HOOK=", (unsigned long)&g_cert_vt_raw[2]);

    if (mgr && mgr_dis) {
        int after = mgr_dis(mgr, PROC_ID_CERT_LOGO);
        a7r3_log_hex("SDK_CERT_AFTER_DIS=", (unsigned long)(unsigned int)after);
    }

    {
        unsigned long obj_base = a7r3_mod_base("/libObj.so");
        if (obj_base) {
            volatile unsigned long *text_slot =
                (volatile unsigned long *)(obj_base + LIBOBJ_TEXT_SETSTR_ID_GOT);
            if (a7r3_pm_writable((const void *)text_slot, 4u)) {
                g_orig_text_setstr_got = *text_slot;
                g_real_text_setstr_id = (text_setstr_id_fn)(obj_base + LIBOBJ_TEXT_SETSTR_ID_REAL);
                g_real_text_setstr_cstr = (text_setstr_cstr_fn)(obj_base + LIBOBJ_TEXT_SETSTR_CSTR_REAL);
                a7r3_dsb();
                *text_slot = (unsigned long)(uintptr_t)&cert_text_setstr_hook;
                a7r3_dsb();
                g_text_setstr_got = text_slot;
                a7r3_log_hex("SDK_CERT_LABEL_HOOK=", 1UL);
            }
        }
    }

    return 0;
}

int a7r3_entry_cert_take_triggered(void)
{
    if (!g_exec_pending)
        return 0;
    g_exec_pending = 0;
    a7r3_dsb();
    return 1;
}

int a7r3_entry_cert_take_label_hits(unsigned *out_hits)
{
    unsigned h = g_label_hits;
    if (h == g_last_label_hits)
        return 0;
    g_last_label_hits = h;
    if (out_hits)
        *out_hits = h;
    return 1;
}

void a7r3_entry_cert_remove(void)
{
    if (!g_cert_installed || !g_cert_vptr_slot)
        return;
    if (g_text_setstr_got) {
        *g_text_setstr_got = g_orig_text_setstr_got;
        g_text_setstr_got = NULL;
    }
    *g_cert_vptr_slot = g_orig_cert_vptr;
    a7r3_dsb();
    a7r3_nanosleep_ms(50u);
    a7r3_log_hex("SDK_CERT_LABEL_HITS=", (unsigned long)g_label_hits);
    a7r3_log_hex("SDK_CERT_RESTORED=", g_orig_cert_vptr);
    g_cert_installed = 0;
}
