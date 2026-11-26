#include "a7r3_shutter_sfx.h"
#include "a7r3_sys.h"

/* Verified A7R3 v3.10 libObj.so offsets (ObjStillRecorder & MWF::ObjMsg) */
#define LIBOBJ_RECCNT_PARSE_SLOT 0x0120615cUL
#define LIBOBJ_RECCNT_PARSE_REAL 0x008640fdUL /* Thumb */
#define LIBOBJ_OBJMSG_GETID_GOT  0x0121c040UL
#define LIBOBJ_OBJMSG_GETPRM_GOT 0x0121b470UL
#define LIBOBJ_BACKUP_READ_GOT   0x0121c0c0UL

/* Verified A7R3 v3.10 Silent Shutter Backup ID */
#define BKID_SILENT_SHUTTER      0x01070b2cUL

/* Verified A7R3 v3.10 libBizFw.so offsets (AdfCh, DataflowInfraCh, DmmCh, AvbbcPhycMem) */
#define BIZ_DFCH_SND_ASYNC       0x00033a29UL /* Thumb */
#define BIZ_ADFCH_CTOR           0x0003a9d1UL
#define BIZ_ADFCH_DTOR           0x0003a399UL
#define BIZ_ADFCH_SYS_SETTABLE   0x0003b29dUL
#define BIZ_ADFCH_SYS_SETPATH    0x0003b3f5UL
#define BIZ_ADFCH_SYS_REQUEST    0x0003b509UL
#define BIZ_ADFCH_SYS_SETVOL     0x0003b62dUL
#define ADF_DEST_ID              0x0058003fu

#define BIZ_DMMCH_CTOR           0x00035ba9UL
#define BIZ_DMMCH_DTOR           0x00035b55UL
#define BIZ_DMMCH_ALLOC_NEW      0x00035c19UL
#define BIZ_DMMCH_OPEN           0x00035f31UL
#define BIZ_DMMCH_CLOSE          0x00036039UL
#define BIZ_DMMCH_FREE           0x00036131UL

#define BIZ_PHYCMEM_CTOR         0x0003179dUL
#define BIZ_PHYCMEM_GETLOG       0x000315f5UL
#define BIZ_PHYCMEM_DTOR         0x00031715UL

/* Verified A7R3 v3.10 libInfraFsys.so offsets (PrFILE2/exFAT + LFN) */
#define FSYS_GET_DRV_FROM_PATH   0x00016af1UL
#define FSYS_GET_DEV_MUTEX_ID    0x00016a11UL
#define FSYS_UNLOCK_DEVICE       0x00024f09UL
#define FSYS_SET_CONTEXT         0x0002501dUL
#define FSYS_GET_MOUNT_DRIVES    0x00017d71UL
#define FSYS_PRFILE_FSFIRST      0x0002033dUL
#define FSYS_PRFILE_FSNEXT       0x00020531UL
#define FSYS_PF2_FOPEN           0x0002cb85UL
#define FSYS_PF2_FREAD           0x0002cc31UL
#define FSYS_PF2_FCLOSE          0x0002c9f9UL
#define FSYS_TNTFS_FOPEN_GOT     0x0006b698UL
#define FSYS_TNTFS_FREAD_GOT     0x0006b8b8UL
#define FSYS_TNTFS_FCLOSE_GOT    0x0006b7e4UL
#define FSYS_TNTFS_OPENDIR_GOT   0x0006bf0cUL
#define FSYS_TNTFS_READDIR_GOT   0x0006b914UL
#define FSYS_TNTFS_CLOSEDIR_GOT  0x0006b9a0UL

#define ADF_UNIT_ID              0x2bu
#define APP_UNIT_TYPE            0x00070817u
#define CUSTOM_KIND_ID           9u
#define TARGET_RATE_HZ           24000u
#define WAV_RAW_CAP              262144u
#define ADF_ENC_CAP              32768u

extern int pthread_create(unsigned long *tid, const void *attr,
                          void *(*fn)(void *), void *arg)
    __attribute__((weak));

typedef int (*reccnt_parse_fn)(void *self, void *msg);
typedef unsigned (*objmsg_get_id_fn)(const void *msg);
typedef const void *(*objmsg_get_prm_fn)(const void *msg, unsigned id, unsigned type);
typedef int (*backup_read_fn)(unsigned long id, void *buf);

typedef int (*df_snd_async_fn)(void *df_ch, const void *msg, uint16_t sz,
                               uint32_t osal_id, uint32_t dest_id);

struct adf_setpath_msg {
    uint16_t magic;  /* 0x2004 */
    uint16_t cmd_id; /* 0x0202 */
    uint16_t size;   /* 12 */
    uint16_t ver;    /* 1 */
    uint8_t  path;   /* 3 */
    uint8_t  pad[3];
};

struct adf_sysreq_msg {
    uint16_t magic;   /* 0x2004 */
    uint16_t cmd_id;  /* 0x0203 */
    uint16_t size;    /* 16 */
    uint16_t ver;     /* 1 */
    uint8_t  req;     /* 1 = START */
    uint8_t  reserve; /* 0 */
    uint8_t  kind_id;
    uint8_t  pad;
    uint32_t loop;    /* 0 */
};

typedef void (*adf_ctor_fn)(void *self, uint16_t unit);
typedef void (*adf_dtor_fn)(void *self);
typedef int (*adf_set_table_fn)(void *self, uint32_t unit_type, uint16_t unit_handle,
                                uint32_t offset, uint32_t size,
                                uint8_t kind_id, uint8_t prio, uint8_t regist);
typedef int (*adf_set_path_fn)(void *self, uint8_t path);
typedef int (*adf_request_fn)(void *self, uint8_t reserve, uint8_t kind_id,
                              uint8_t loop, uint32_t repeat_cnt);
typedef int (*adf_set_vol_fn)(void *self, uint8_t vol, uint8_t kind_id);

typedef void (*dmm_ctor_fn)(void *self, uint16_t unit);
typedef void (*dmm_dtor_fn)(void *self);
typedef int (*dmm_alloc_new_fn)(void *self, void *out_alc, uint32_t unit_type,
                                uint16_t num, uint32_t flags);
typedef int (*dmm_open_fn)(void *self, void *out_open, uint16_t unit_handle,
                           uint16_t opt, uint32_t flags);
typedef int (*dmm_close_fn)(void *self, void *out_close, uint16_t unit_handle);
typedef int (*dmm_free_fn)(void *self, void *out_free, uint16_t unit_handle, uint16_t opt);

typedef void (*phyc_ctor_fn)(void *self, uint32_t phy_addr, uint32_t size,
                             int cache_mode, uint32_t tag);
typedef void *(*phyc_getlog_fn)(void *self);
typedef void (*phyc_dtor_fn)(void *self);

typedef int (*fsys_get_drv_fn)(const char *path, void **out_drv);
typedef int (*fsys_get_mutex_fn)(void *mutex_obj);
typedef void (*fsys_unlock_dev_fn)(int mutex_id);
typedef void (*fsys_set_ctx_fn)(void *ctx);
typedef unsigned (*fsys_get_mount_drives_fn)(void);
typedef int (*prfile_fsfirst_fn)(void *drv, const char *pattern, uint8_t attr, void *find_info);
typedef int (*prfile_fsnext_fn)(void *drv, void *find_info);
typedef void *(*pf2_fopen_fn)(const char *path, const char *mode);
typedef uint32_t (*pf2_fread_fn)(void *buf, uint32_t sz, uint32_t cnt, void *fp);
typedef int (*pf2_fclose_fn)(void *fp);
typedef void *(*tntfs_fopen_fn)(const char *path, const char *mode);
typedef int (*tntfs_fread_fn)(void *fp, void *buf, int len);
typedef int (*tntfs_fclose_fn)(void *fp);
typedef void *(*tntfs_opendir_fn)(const char *path);
typedef const void *(*tntfs_readdir_fn)(void *dir);
typedef int (*tntfs_closedir_fn)(void *dir);

struct dmm_alc_unit {
    uint16_t flag;
    int16_t ret_code;
    uint16_t unit_handle;
    uint16_t rsv0;
    uint32_t phy_addr_hi;
    uint32_t rsv1;
    uint32_t phy_addr;
    uint32_t size;
};

struct sfx_sd_entry {
    char path[128];
    char name[28];
    uint32_t file_size;
};

static const uint8_t g_builtin_kinds[A7R3_SFX_BUILTIN_CNT] = { 3u };
static uint8_t g_vol_bytes[3] = { 2u, 5u, 7u };

static uint8_t g_wav_raw[WAV_RAW_CAP] __attribute__((aligned(16)));
static uint8_t g_adf_enc[ADF_ENC_CAP] __attribute__((aligned(64)));
static uint8_t g_find_info[2112] __attribute__((aligned(8)));
static struct sfx_sd_entry g_sd_files[A7R3_SFX_MAX_SD_FILES];
static volatile int g_sd_count;

static unsigned long g_obj_base;
static unsigned long g_biz_base;
static unsigned long g_fsys_base;

static volatile unsigned long *g_reccnt_slot;
static unsigned long g_orig_reccnt;
static reccnt_parse_fn g_real_reccnt;
static objmsg_get_id_fn g_msg_get_id;
static objmsg_get_prm_fn g_msg_get_prm;
static backup_read_fn g_backup_read;
static df_snd_async_fn g_df_snd_async;

static adf_ctor_fn g_adf_ctor;
static adf_dtor_fn g_adf_dtor;
static adf_set_table_fn g_adf_set_table;
static adf_set_path_fn g_adf_set_path;
static adf_request_fn g_adf_request;
static adf_set_vol_fn g_adf_set_vol;

static dmm_ctor_fn g_dmm_ctor;
static dmm_dtor_fn g_dmm_dtor;
static dmm_alloc_new_fn g_dmm_alloc_new;
static dmm_open_fn g_dmm_open;
static dmm_close_fn g_dmm_close;
static dmm_free_fn g_dmm_free;

static phyc_ctor_fn g_phyc_ctor;
static phyc_getlog_fn g_phyc_getlog;
static phyc_dtor_fn g_phyc_dtor;

static fsys_get_drv_fn g_fsys_get_drv;
static fsys_get_mutex_fn g_fsys_get_mutex;
static fsys_unlock_dev_fn g_fsys_unlock_dev;
static fsys_set_ctx_fn g_fsys_set_ctx;
static fsys_get_mount_drives_fn g_fsys_mount_drives;
static prfile_fsfirst_fn g_prfile_fsfirst;
static prfile_fsnext_fn g_prfile_fsnext;
static pf2_fopen_fn g_pf2_fopen;
static pf2_fread_fn g_pf2_fread;
static pf2_fclose_fn g_pf2_fclose;
static tntfs_fopen_fn g_tntfs_fopen;
static tntfs_fread_fn g_tntfs_fread;
static tntfs_fclose_fn g_tntfs_fclose;
static tntfs_opendir_fn g_tntfs_opendir;
static tntfs_readdir_fn g_tntfs_readdir;
static tntfs_closedir_fn g_tntfs_closedir;

static volatile int g_sfx_installed;
static volatile int g_sfx_mode;
static volatile int g_silent_shutter_on;
static int g_logged_eshut = -1;
static int g_logged_eshut_val = -1;
static volatile int g_selected_idx;
static volatile int g_vol_level = 1;
static volatile int g_loaded_sd_idx = -1;

static volatile unsigned g_trigger_seq;
static volatile unsigned g_skipped_mech_seq;
static volatile unsigned g_preview_seq;
static volatile unsigned g_load_seq;
static volatile unsigned g_rescan_seq;
static volatile unsigned g_rescan_done_seq;
static volatile unsigned g_prewarm_seq;
static volatile unsigned g_wake_seq;
static volatile int g_sfx_stop;
static volatile int g_sfx_thread_running;

static int g_dmm_active;
static uint16_t g_dmm_handle;
static uint32_t g_dmm_phy_addr;
static uint32_t g_dmm_size;
static int g_adf_custom_registered;

static uint8_t g_adf_ch[0x40];
static int g_adf_ready;
static uint8_t g_applied_kind = 0xffu;
static uint8_t g_applied_vol = 0xffu;

static void wake_sfx_worker(void)
{
    g_wake_seq++;
    a7r3_dsb();
    a7r3_futex_wake(&g_wake_seq);
}

static void mem_zero(void *dst, unsigned n)
{
    uint8_t *p = (uint8_t *)dst;
    unsigned i;
    for (i = 0; i < n; i++)
        p[i] = 0u;
}

static void mem_copy(void *dst, const void *src, unsigned n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    unsigned i;
    for (i = 0; i < n; i++)
        d[i] = s[i];
}

static unsigned str_len(const char *s)
{
    unsigned n = 0;
    while (s[n])
        n++;
    return n;
}

static void str_copy_cap(char *dst, const char *src, unsigned cap)
{
    unsigned i = 0;
    if (cap == 0u)
        return;
    while (i + 1u < cap && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static char to_lower_ch(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c + ('a' - 'A'));
    return c;
}

static int ends_with_ci4(const char *s, const char *ext4)
{
    unsigned len = str_len(s);
    unsigned i;
    if (len < 4u)
        return 0;
    for (i = 0; i < 4u; i++) {
        if (to_lower_ch(s[len - 4u + i]) != to_lower_ch(ext4[i]))
            return 0;
    }
    return 1;
}

static uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8));
}

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Explicit bit-by-bit division so Clang never emits __aeabi_uldivmod or __aeabi_uidiv. */
static uint32_t u64_div_u32(uint64_t num, uint32_t den)
{
    uint64_t rem = 0;
    uint32_t q = 0;
    int bit;
    if (den == 0u)
        return 0u;
    for (bit = 39; bit >= 0; bit--) {
        rem = (rem << 1) | ((num >> (unsigned)bit) & 1ULL);
        if (rem >= (uint64_t)den) {
            rem -= (uint64_t)den;
            if (bit < 32)
                q |= (1u << (unsigned)bit);
        }
    }
    return q;
}

static uint32_t read_got(unsigned long base, unsigned long got_off)
{
    const volatile uint32_t *slot = (const volatile uint32_t *)(base + got_off);
    if (!a7r3_pm_readable((const void *)slot, 4u))
        return 0u;
    return *slot;
}

static int sfx_should_trigger(int mode, int is_eshut)
{
    if (mode == A7R3_SFX_MODE_MECH)
        return is_eshut ? 0 : 1;
    if (mode == A7R3_SFX_MODE_ESHUT)
        return is_eshut ? 1 : 0;
    if (mode == A7R3_SFX_MODE_BOTH)
        return 1;
    return 0;
}

__attribute__((noinline))
static int parse_reccnt_hook(void *self, void *msg)
{
    int mode = g_sfx_mode;
    if (mode != A7R3_SFX_MODE_OFF && msg && g_msg_get_id && g_msg_get_prm) {
        if (g_msg_get_id(msg) == 0x1003u) {
            const void *sub = g_msg_get_prm(msg, 0x1003u, 0x29u);
            if (sub && a7r3_pm_readable(sub, 4u) && g_msg_get_id(sub) == 0x1003u) {
                if (sfx_should_trigger(mode, g_silent_shutter_on)) {
                    g_trigger_seq++;
                    wake_sfx_worker();
                } else {
                    g_skipped_mech_seq++;
                    a7r3_dsb();
                }
            }
        }
    }
    return g_real_reccnt(self, msg);
}

static void refresh_eshutter_state(void)
{
    uint32_t raw = 0;
    int val;
    int es;

    if (!g_backup_read)
        return;

    (void)g_backup_read(BKID_SILENT_SHUTTER, &raw);
    val = (int)(raw & 0xffu);
    if (val != 1 && val != 2) {
        if (val != g_logged_eshut_val) {
            g_logged_eshut_val = val;
            a7r3_log_hex("SDK_SFX_ESHUT_VAL=", (unsigned long)val);
        }
        return;
    }

    es = (val == 2) ? 1 : 0;
    g_silent_shutter_on = es;
    if (val != g_logged_eshut_val || es != g_logged_eshut) {
        g_logged_eshut_val = val;
        g_logged_eshut = es;
        a7r3_log_hex("SDK_SFX_ESHUT_VAL=", (unsigned long)val);
        a7r3_log_hex("SDK_SFX_ESHUT=", (unsigned long)es);
    }
}

static void release_custom_sound_dma(void)
{
    uint8_t adf[0x40];
    uint8_t dmm[0x40];
    uint8_t resp[0x20];

    if (g_adf_custom_registered && g_adf_ctor && g_adf_set_table && g_adf_dtor) {
        mem_zero(adf, sizeof(adf));
        g_adf_ctor(adf, (uint16_t)ADF_UNIT_ID);
        g_adf_set_table(adf, APP_UNIT_TYPE, g_dmm_handle, 0u, 0u, CUSTOM_KIND_ID, 6u, 0u);
        g_adf_dtor(adf);
        g_adf_custom_registered = 0;
    }

    if (g_dmm_active && g_dmm_ctor && g_dmm_close && g_dmm_free && g_dmm_dtor) {
        mem_zero(dmm, sizeof(dmm));
        mem_zero(resp, sizeof(resp));
        g_dmm_ctor(dmm, (uint16_t)ADF_UNIT_ID);
        g_dmm_close(dmm, resp, g_dmm_handle);
        mem_zero(resp, sizeof(resp));
        g_dmm_free(dmm, resp, g_dmm_handle, 0u);
        g_dmm_dtor(dmm);
        g_dmm_active = 0;
        g_dmm_handle = 0u;
        g_dmm_phy_addr = 0u;
        g_dmm_size = 0u;
    }
    g_loaded_sd_idx = -1;
    g_applied_kind = 0xffu;
    g_applied_vol = 0xffu;
}

static int encode_wav_to_adf(const uint8_t *raw, unsigned raw_len,
                             uint8_t *out_adf, unsigned out_cap,
                             unsigned *out_bytes)
{
    unsigned pos = 12u;
    int fmt_found = 0;
    uint16_t audio_fmt = 0;
    uint16_t num_ch = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    const uint8_t *pcm_data = NULL;
    unsigned pcm_bytes = 0;
    unsigned src_frames;
    unsigned out_frames;
    unsigned max_frames;
    unsigned blk_cnt;
    uint32_t step_q16;
    uint64_t acc_q16 = 0;
    unsigned blk;

    if (raw_len < 44u)
        return -1;
    if (raw[0] != 'R' || raw[1] != 'I' || raw[2] != 'F' || raw[3] != 'F')
        return -1;
    if (raw[8] != 'W' || raw[9] != 'A' || raw[10] != 'V' || raw[11] != 'E')
        return -1;

    while (pos + 8u <= raw_len) {
        uint32_t csz = rd_le32(raw + pos + 4u);
        unsigned body = pos + 8u;
        unsigned next;
        if (raw[pos] == 'f' && raw[pos + 1] == 'm' &&
            raw[pos + 2] == 't' && raw[pos + 3] == ' ' &&
            csz <= raw_len - body && csz >= 16u) {
            audio_fmt = rd_le16(raw + body);
            if (audio_fmt == 0xfffeu && csz >= 26u)
                audio_fmt = rd_le16(raw + body + 24u);
            num_ch = rd_le16(raw + body + 2u);
            sample_rate = rd_le32(raw + body + 4u);
            bits_per_sample = rd_le16(raw + body + 14u);
            fmt_found = 1;
        } else if (raw[pos] == 'd' && raw[pos + 1] == 'a' &&
                   raw[pos + 2] == 't' && raw[pos + 3] == 'a') {
            pcm_data = raw + body;
            pcm_bytes = (csz > raw_len - body) ? (raw_len - body) : csz;
            break;
        }
        if (csz > raw_len - body)
            break;
        next = body + csz + (csz & 1u);
        if (next <= pos)
            break;
        pos = next;
    }

    if (!fmt_found || !pcm_data || pcm_bytes == 0u ||
        audio_fmt != 1u || (num_ch != 1u && num_ch != 2u) ||
        (bits_per_sample != 8u && bits_per_sample != 16u && bits_per_sample != 24u) ||
        sample_rate < 4000u || sample_rate > 96000u) {
        a7r3_log_hex("SDK_SFX_WAV_HDR=",
                     ((unsigned long)audio_fmt << 24) |
                     ((unsigned long)num_ch << 16) |
                     ((unsigned long)bits_per_sample << 8) |
                     ((unsigned long)(sample_rate >> 8) & 0xffu));
        return -1;
    }

    {
        unsigned bps = (bits_per_sample == 24u) ? 3u :
                       ((bits_per_sample == 16u) ? 2u : 1u);
        unsigned stride = (unsigned)num_ch * bps;
        src_frames = (bps == 3u) ? u64_div_u32((uint64_t)pcm_bytes, stride)
                                 : (pcm_bytes >> ((bps == 2u ? 1u : 0u) + (num_ch == 2u ? 1u : 0u)));
    }
    if (src_frames < 8u)
        return -1;

    out_frames = u64_div_u32((uint64_t)src_frames * (uint64_t)TARGET_RATE_HZ, sample_rate);
    max_frames = (out_cap >> 6) * 62u; /* 62 samples per 64 bytes */
    if (out_frames > max_frames)
        out_frames = max_frames;
    if (out_frames < 31u)
        out_frames = 31u;

    /* Round up to multiple of 62 samples (64 bytes) if within out_cap */
    blk_cnt = u64_div_u32((uint64_t)out_frames + 30ULL, 31u);
    if (blk_cnt & 1u)
        blk_cnt++;
    while ((blk_cnt << 5) > out_cap && blk_cnt >= 2u)
        blk_cnt -= 2u;
    if (blk_cnt == 0u)
        return -1;

    step_q16 = u64_div_u32(((uint64_t)sample_rate << 16), TARGET_RATE_HZ);
    mem_zero(out_adf, blk_cnt << 5);

    for (blk = 0; blk < blk_cnt; blk++) {
        int32_t s[31];
        int32_t peak = 0;
        uint8_t lo = 0u;
        uint8_t *dst = out_adf + (blk << 5);
        unsigned i;

        for (i = 0; i < 31u; i++) {
            uint32_t idx = (uint32_t)(acc_q16 >> 16);
            int32_t v = 0;
            if (idx < src_frames) {
                uint32_t frac = (uint32_t)(acc_q16 & 0xffffu);
                uint32_t idx1 = (idx + 1u < src_frames) ? (idx + 1u) : idx;
                int32_t s0 = 0, s1 = 0;
                unsigned bps = (bits_per_sample == 24u) ? 3u :
                               ((bits_per_sample == 16u) ? 2u : 1u);
                unsigned stride = (unsigned)num_ch * bps;
                unsigned off0 = idx * stride;
                unsigned off1 = idx1 * stride;

                if (bits_per_sample == 24u) {
                    s0 = (int16_t)rd_le16(pcm_data + off0 + 1u);
                    s1 = (int16_t)rd_le16(pcm_data + off1 + 1u);
                    if (num_ch == 2u) {
                        s0 = (s0 + (int16_t)rd_le16(pcm_data + off0 + 4u)) >> 1;
                        s1 = (s1 + (int16_t)rd_le16(pcm_data + off1 + 4u)) >> 1;
                    }
                } else if (bits_per_sample == 16u) {
                    s0 = (int16_t)rd_le16(pcm_data + off0);
                    s1 = (int16_t)rd_le16(pcm_data + off1);
                    if (num_ch == 2u) {
                        s0 = (s0 + (int16_t)rd_le16(pcm_data + off0 + 2u)) >> 1;
                        s1 = (s1 + (int16_t)rd_le16(pcm_data + off1 + 2u)) >> 1;
                    }
                } else {
                    s0 = ((int32_t)pcm_data[off0] - 128) << 8;
                    s1 = ((int32_t)pcm_data[off1] - 128) << 8;
                    if (num_ch == 2u) {
                        int32_t r0 = ((int32_t)pcm_data[off0 + 1u] - 128) << 8;
                        int32_t r1 = ((int32_t)pcm_data[off1 + 1u] - 128) << 8;
                        s0 = (s0 + r0) >> 1;
                        s1 = (s1 + r1) >> 1;
                    }
                }
                v = s0 + (((s1 - s0) * (int32_t)frac) >> 16);
            }
            s[i] = v;
            if (v < 0)
                v = -v;
            if (v > peak)
                peak = v;
            acc_q16 += (uint64_t)step_q16;
        }

        while (lo < 8u && peak > (127 << lo))
            lo++;
        dst[0] = lo; /* hi = 0 -> direct Sony BFP PCM block */
        for (i = 0; i < 31u; i++) {
            int32_t r = s[i];
            int32_t q;
            if (lo > 0u) {
                int32_t half = 1 << (lo - 1u);
                q = (r >= 0) ? ((r + half) >> lo) : ((r - half) >> lo);
            } else {
                q = r;
            }
            if (q > 127)
                q = 127;
            if (q < -127)
                q = -127;
            dst[1u + i] = (uint8_t)(int8_t)q;
        }
    }

    *out_bytes = blk_cnt << 5;
    return 0;
}

static int acquire_drive(char drive, void **out_drv, int *out_mutex, uint8_t *out_fstype)
{
    char drv_root[4];
    void *drv = NULL;
    int rc;
    int mutex_id;
    const uint32_t *state_ptr;

    if (!g_fsys_get_drv || !g_fsys_get_mutex || !g_fsys_unlock_dev)
        return -1;

    drv_root[0] = drive;
    drv_root[1] = ':';
    drv_root[2] = '\\';
    drv_root[3] = '\0';

    rc = g_fsys_get_drv(drv_root, &drv);
    if (rc != 0 || !drv)
        return -1;

    mutex_id = g_fsys_get_mutex((uint8_t *)drv + 0x954);
    state_ptr = *(const uint32_t *const *)((const uint8_t *)drv + 0x135c);
    if (!state_ptr || !a7r3_pm_readable(state_ptr, 4u) || ((*state_ptr) & 0x140u) != 0u) {
        g_fsys_unlock_dev(mutex_id);
        return -1;
    }

    *out_drv = drv;
    *out_mutex = mutex_id;
    *out_fstype = *((const uint8_t *)drv + 0x920);
    return 0;
}

static int load_sd_sound_into_adf(int sd_idx)
{
    void *drv = NULL;
    int mutex_id = 0;
    uint8_t fs_type = 0;
    const char *path;
    unsigned total = 0;
    int rc;
    unsigned enc_len = 0;
    uint8_t dmm[0x40];
    uint8_t adf[0x40];
    uint8_t pmem[0x30];
    struct dmm_alc_unit alc;
    uint8_t open_resp[0x20];
    void *va;

    if (sd_idx < 0 || sd_idx >= g_sd_count)
        return -1;
    if (!g_fsys_set_ctx || !g_pf2_fopen || !g_pf2_fread || !g_pf2_fclose ||
        !g_dmm_ctor || !g_dmm_alloc_new || !g_dmm_open || !g_dmm_dtor ||
        !g_phyc_ctor || !g_phyc_getlog || !g_phyc_dtor ||
        !g_adf_ctor || !g_adf_set_table || !g_adf_dtor)
        return -1;

    release_custom_sound_dma();

    path = g_sd_files[sd_idx].path;
    if (acquire_drive(path[0], &drv, &mutex_id, &fs_type) != 0) {
        a7r3_log_hex("SDK_SFX_OPEN_ERR=", 0xdead0002UL);
        return -1;
    }

    if (fs_type != 0x10u) {
        void *ctx = *(void *const *)((const uint8_t *)drv + 0x250);
        void *fp;
        g_fsys_set_ctx(ctx);
        fp = g_pf2_fopen(path, "r");
        if (!fp) {
            g_fsys_unlock_dev(mutex_id);
            a7r3_log_hex("SDK_SFX_OPEN_ERR=", 0xdead0001UL);
            return -1;
        }
        while (total < WAV_RAW_CAP) {
            uint32_t got = g_pf2_fread(g_wav_raw + total, 1u, WAV_RAW_CAP - total, fp);
            if (got == 0u)
                break;
            total += got;
        }
        g_pf2_fclose(fp);
    } else if (g_tntfs_fopen && g_tntfs_fread && g_tntfs_fclose) {
        void *fp = g_tntfs_fopen(path, "rb");
        if (!fp) {
            g_fsys_unlock_dev(mutex_id);
            a7r3_log_hex("SDK_SFX_OPEN_ERR=", 0xdead0003UL);
            return -1;
        }
        while (total < WAV_RAW_CAP) {
            int got = g_tntfs_fread(fp, g_wav_raw + total, (int)(WAV_RAW_CAP - total));
            if (got <= 0)
                break;
            total += (unsigned)got;
        }
        g_tntfs_fclose(fp);
    } else {
        g_fsys_unlock_dev(mutex_id);
        a7r3_log_hex("SDK_SFX_OPEN_ERR=", 0xdead0004UL);
        return -1;
    }
    g_fsys_unlock_dev(mutex_id);

    if (total < 64u) {
        a7r3_log_hex("SDK_SFX_SHORT=", (unsigned long)total);
        return -1;
    }

    if (ends_with_ci4(g_sd_files[sd_idx].name, ".bin")) {
        enc_len = (total > ADF_ENC_CAP) ? ADF_ENC_CAP : total;
        enc_len &= ~63u;
        if (enc_len < 64u)
            return -1;
        mem_copy(g_adf_enc, g_wav_raw, enc_len);
    } else {
        if (encode_wav_to_adf(g_wav_raw, total, g_adf_enc, ADF_ENC_CAP, &enc_len) != 0) {
            a7r3_log_str("SDK_SFX_WAV_ERR\n");
            return -1;
        }
    }

    mem_zero(dmm, sizeof(dmm));
    mem_zero(&alc, sizeof(alc));
    g_dmm_ctor(dmm, (uint16_t)ADF_UNIT_ID);
    rc = g_dmm_alloc_new(dmm, &alc, APP_UNIT_TYPE, 1u, 0u);
    if (rc != 0 || alc.ret_code != 0 || alc.phy_addr == 0u || alc.size < 64u) {
        a7r3_log_hex("SDK_SFX_DMM_ALC_ERR=", (unsigned long)rc);
        a7r3_log_hex("SDK_SFX_DMM_ALC_RET=", (unsigned long)(uint16_t)alc.ret_code);
        g_dmm_dtor(dmm);
        return -1;
    }

    mem_zero(open_resp, sizeof(open_resp));
    rc = g_dmm_open(dmm, open_resp, alc.unit_handle, 0u, 0u);
    g_dmm_dtor(dmm);
    if (rc != 0) {
        a7r3_log_hex("SDK_SFX_DMM_OPEN_ERR=", (unsigned long)rc);
        return -1;
    }

    g_dmm_active = 1;
    g_dmm_handle = alc.unit_handle;
    g_dmm_phy_addr = alc.phy_addr;
    g_dmm_size = alc.size;

    if (enc_len > g_dmm_size) {
        enc_len = g_dmm_size & ~63u;
        if (enc_len < 64u) {
            release_custom_sound_dma();
            return -1;
        }
    }

    mem_zero(pmem, sizeof(pmem));
    g_phyc_ctor(pmem, g_dmm_phy_addr, g_dmm_size, 1, (uint32_t)ADF_UNIT_ID);
    va = g_phyc_getlog(pmem);
    if (!va) {
        a7r3_log_str("SDK_SFX_PHY_MAP_ERR\n");
        g_phyc_dtor(pmem);
        release_custom_sound_dma();
        return -1;
    }
    mem_copy(va, g_adf_enc, enc_len);
    a7r3_dsb();
    g_phyc_dtor(pmem);

    mem_zero(adf, sizeof(adf));
    g_adf_ctor(adf, (uint16_t)ADF_UNIT_ID);
    rc = g_adf_set_table(adf, APP_UNIT_TYPE, g_dmm_handle, 0u, enc_len, CUSTOM_KIND_ID, 6u, 1u);
    g_adf_dtor(adf);
    if (rc != 0) {
        a7r3_log_hex("SDK_SFX_SETTABLE_ERR=", (unsigned long)rc);
        release_custom_sound_dma();
        return -1;
    }

    g_adf_custom_registered = 1;
    g_loaded_sd_idx = sd_idx;
    g_applied_kind = 0xffu;
    g_applied_vol = 0xffu;
    a7r3_log_hex("SDK_SFX_SD_LOADED_SZ=", (unsigned long)enc_len);
    return 0;
}

static void resolve_active_sound(uint8_t *out_kind, uint8_t *out_vol)
{
    int sel = g_selected_idx;
    int vol_i = g_vol_level;
    uint8_t kind_id;

    if (vol_i < 0)
        vol_i = 0;
    else if (vol_i > 2)
        vol_i = 2;
    *out_vol = g_vol_bytes[vol_i];

    if (sel < A7R3_SFX_BUILTIN_CNT) {
        if (sel < 0)
            sel = 0;
        kind_id = g_builtin_kinds[sel];
    } else {
        int sd_idx = sel - A7R3_SFX_BUILTIN_CNT;
        if (g_loaded_sd_idx != sd_idx || !g_adf_custom_registered) {
            if (load_sd_sound_into_adf(sd_idx) != 0)
                kind_id = g_builtin_kinds[0];
            else
                kind_id = CUSTOM_KIND_ID;
        } else {
            kind_id = CUSTOM_KIND_ID;
        }
    }
    *out_kind = kind_id;
}

static void prewarm_adf(void)
{
    uint8_t kind_id;
    uint8_t vol;

    if (!g_adf_ready || !g_adf_set_vol || !g_adf_set_path)
        return;
    resolve_active_sound(&kind_id, &vol);
    if (vol != g_applied_vol || kind_id != g_applied_kind) {
        g_adf_set_vol(g_adf_ch, vol, kind_id);
        g_applied_vol = vol;
        g_applied_kind = kind_id;
    }
    if (g_adf_set_path(g_adf_ch, 3u) != 0) {
        if (g_adf_set_path(g_adf_ch, 2u) != 0)
            g_adf_set_path(g_adf_ch, 0u);
    }
}

static void play_current_sound(int is_preview, unsigned trig_no)
{
    uint8_t kind_id;
    uint8_t vol;
    unsigned t0, t1, t2;
    int rc_p, rc_r;

    if (!g_adf_ready || !g_adf_set_vol || !g_adf_set_path || !g_adf_request)
        return;

    resolve_active_sound(&kind_id, &vol);
    if (vol != g_applied_vol || kind_id != g_applied_kind) {
        g_adf_set_vol(g_adf_ch, vol, kind_id);
        g_applied_vol = vol;
        g_applied_kind = kind_id;
    }

    t0 = a7r3_now_ms();
    if (g_df_snd_async) {
        struct adf_setpath_msg pmsg;
        mem_zero(&pmsg, sizeof(pmsg));
        pmsg.magic = 0x2004u;
        pmsg.cmd_id = 0x0202u;
        pmsg.size = 12u;
        pmsg.ver = 1u;
        pmsg.path = 3u;
        rc_p = g_df_snd_async(g_adf_ch + 4, &pmsg, (uint16_t)sizeof(pmsg), 0u, ADF_DEST_ID);
    } else {
        rc_p = -1;
    }
    if (rc_p != 0) {
        rc_p = g_adf_set_path(g_adf_ch, 3u);
        if (rc_p != 0)
            rc_p = g_adf_set_path(g_adf_ch, 2u);
        if (rc_p != 0)
            rc_p = g_adf_set_path(g_adf_ch, 0u);
    }
    t1 = a7r3_now_ms();
    if (g_df_snd_async) {
        struct adf_sysreq_msg rmsg;
        mem_zero(&rmsg, sizeof(rmsg));
        rmsg.magic = 0x2004u;
        rmsg.cmd_id = 0x0203u;
        rmsg.size = 16u;
        rmsg.ver = 1u;
        rmsg.req = 1u;
        rmsg.reserve = 0u;
        rmsg.kind_id = kind_id;
        rmsg.loop = 0u;
        rc_r = g_df_snd_async(g_adf_ch + 4, &rmsg, (uint16_t)sizeof(rmsg), 0u, ADF_DEST_ID);
    } else {
        rc_r = -1;
    }
    if (rc_r != 0)
        rc_r = g_adf_request(g_adf_ch, 1u, 0u, kind_id, 0u);
    t2 = a7r3_now_ms();

    if (is_preview || trig_no <= 5u) {
        a7r3_log_hex("SDK_SFX_PLAY=",
                     ((unsigned long)kind_id << 24) |
                     ((unsigned long)vol << 16) |
                     (((unsigned long)rc_p & 0xffu) << 8) |
                     ((unsigned long)rc_r & 0xffu));
        a7r3_log_hex("SDK_SFX_TIMING=",
                     (((unsigned long)(t1 - t0) & 0xffffu) << 16) |
                     ((unsigned long)(t2 - t1) & 0xffffu));
    }
}

static void scan_sd_dir_prfile(char drive, const char *subdir, int *io_cnt)
{
    void *drv = NULL;
    int mutex_id = 0;
    uint8_t fs_type = 0;
    char pat[32];
    int p = 0;
    int rc;

    if (acquire_drive(drive, &drv, &mutex_id, &fs_type) != 0)
        return;

    if (fs_type != 0x10u) {
        pat[p++] = drive;
        pat[p++] = ':';
        pat[p++] = '\\';
        {
            unsigned i = 0;
            while (subdir[i] && p + 4 < (int)sizeof(pat))
                pat[p++] = subdir[i++];
        }
        pat[p++] = '\\';
        pat[p++] = '*';
        pat[p] = '\0';

        mem_zero(g_find_info, sizeof(g_find_info));
        rc = g_prfile_fsfirst(drv, pat, 0x80u, g_find_info);
        if (subdir[0] == 'S' && subdir[1] == 'H') {
            a7r3_log_hex("SDK_SFX_SCAN_RC=",
                         ((unsigned long)(uint8_t)drive << 24) |
                         ((unsigned long)fs_type << 16) |
                         ((unsigned long)rc & 0xffffu));
        }
        while (rc == 0 && *io_cnt < A7R3_SFX_MAX_SD_FILES) {
            const char *fname = *(const char *const *)g_find_info;
            uint8_t fat_attr = g_find_info[0x26];
            uint32_t fsz = rd_le32(g_find_info + 0x08);
            if (fname && a7r3_pm_readable(fname, 1u) &&
                !(fat_attr & 0x10u) && fname[0] != '.' &&
                (ends_with_ci4(fname, ".wav") || ends_with_ci4(fname, ".bin"))) {
                struct sfx_sd_entry *e = &g_sd_files[*io_cnt];
                unsigned k = 0;
                e->path[k++] = drive;
                e->path[k++] = ':';
                e->path[k++] = '\\';
                {
                    unsigned i = 0;
                    while (subdir[i] && k + 4u < sizeof(e->path))
                        e->path[k++] = subdir[i++];
                }
                e->path[k++] = '\\';
                str_copy_cap(e->path + k, fname, sizeof(e->path) - k);
                str_copy_cap(e->name, fname, sizeof(e->name));
                e->file_size = fsz;
                (*io_cnt)++;
            }
            rc = g_prfile_fsnext(drv, g_find_info);
        }
    } else if (g_tntfs_opendir && g_tntfs_readdir && g_tntfs_closedir) {
        char dir_path[32];
        void *dir;
        int dlen = 0;

        dir_path[dlen++] = drive;
        dir_path[dlen++] = ':';
        dir_path[dlen++] = '\\';
        {
            unsigned i = 0;
            while (subdir[i] && dlen + 2 < (int)sizeof(dir_path))
                dir_path[dlen++] = subdir[i++];
        }
        dir_path[dlen] = '\0';

        dir = g_tntfs_opendir(dir_path);
        if (!dir && subdir[0] == 'S' && subdir[1] == 'H') {
            str_copy_cap(dir_path + 3, "shutter_sfx", sizeof(dir_path) - 3u);
            dir = g_tntfs_opendir(dir_path);
        }
        a7r3_log_hex("SDK_SFX_SCAN_RC=",
                     ((unsigned long)(uint8_t)drive << 24) |
                     ((unsigned long)fs_type << 16) |
                     (dir ? 0UL : 2UL));
        if (dir) {
            while (*io_cnt < A7R3_SFX_MAX_SD_FILES) {
                const uint8_t *ent = (const uint8_t *)g_tntfs_readdir(dir);
                uint8_t d_type;
                const char *fname;
                if (!ent || !a7r3_pm_readable(ent, 0x14u))
                    break;
                d_type = ent[0x12];
                fname = (const char *)(ent + 0x13);
                if (d_type != 4u && fname[0] != '\0' && fname[0] != '.' &&
                    (ends_with_ci4(fname, ".wav") || ends_with_ci4(fname, ".bin"))) {
                    struct sfx_sd_entry *e = &g_sd_files[*io_cnt];
                    unsigned k = 0;
                    str_copy_cap(e->path, dir_path, sizeof(e->path));
                    k = str_len(e->path);
                    if (k + 2u < sizeof(e->path)) {
                        e->path[k++] = '\\';
                        str_copy_cap(e->path + k, fname, sizeof(e->path) - k);
                    }
                    str_copy_cap(e->name, fname, sizeof(e->name));
                    e->file_size = 0u;
                    (*io_cnt)++;
                }
            }
            g_tntfs_closedir(dir);
        }
    } else if (subdir[0] == 'S' && subdir[1] == 'H') {
        a7r3_log_hex("SDK_SFX_SCAN_RC=",
                     ((unsigned long)(uint8_t)drive << 24) |
                     ((unsigned long)fs_type << 16) |
                     0xff10UL);
    }

    g_fsys_unlock_dev(mutex_id);
}

static void do_rescan_sd(void)
{
    unsigned drives = 0u;
    int cnt = 0;
    int i;

    if (!g_fsys_mount_drives || !g_prfile_fsfirst || !g_prfile_fsnext)
        return;

    drives = g_fsys_mount_drives();
    a7r3_log_hex("SDK_SFX_DRIVES=", (unsigned long)drives);
    if (drives == 0u)
        drives = 0x7u;
    for (i = 0; i < 26 && cnt < A7R3_SFX_MAX_SD_FILES; i++) {
        if (!(drives & (1u << (unsigned)i)))
            continue;
        scan_sd_dir_prfile((char)('A' + i), "SHUTTER_SFX", &cnt);
    }

    g_sd_count = cnt;
    g_loaded_sd_idx = -1;
    if (g_selected_idx >= A7R3_SFX_BUILTIN_CNT + cnt)
        g_selected_idx = 0;
    a7r3_dsb();
    g_rescan_done_seq = g_rescan_seq;
    a7r3_dsb();
    a7r3_log_hex("SDK_SFX_SD_CNT=", (unsigned long)cnt);
}

static void calibrate_volume_from_backup(void)
{
    uint32_t v_fl = 0;
    uint32_t v_ka = 0;
    uint32_t base;

    if (g_backup_read) {
        g_backup_read(0x002b0022UL, &v_fl);
        g_backup_read(0x002b002aUL, &v_ka);
    }
    v_fl &= 0xffu;
    v_ka &= 0xffu;
    a7r3_log_hex("SDK_SFX_BK_VOL=", ((unsigned long)v_ka << 8) | (unsigned long)v_fl);

    base = v_ka ? v_ka : (v_fl ? v_fl : 0x40u);
    if (base <= 7u) {
        g_vol_bytes[0] = (uint8_t)(base > 2u ? base - 2u : 1u);
        g_vol_bytes[1] = (uint8_t)base;
        g_vol_bytes[2] = (uint8_t)(base + 2u <= 7u ? base + 2u : 7u);
    } else {
        uint32_t low = base >> 1;
        uint32_t high = base + (base >> 1);
        g_vol_bytes[0] = (uint8_t)(low ? low : 1u);
        g_vol_bytes[1] = (uint8_t)base;
        g_vol_bytes[2] = (uint8_t)(high <= 0xffu ? high : 0xffu);
    }
}

static void *sfx_worker(void *arg)
{
    unsigned last_trig = g_trigger_seq;
    unsigned last_mech = g_skipped_mech_seq;
    unsigned last_prev = g_preview_seq;
    unsigned last_load = g_load_seq;
    unsigned last_rescan = g_rescan_seq;
    unsigned last_prewarm = g_prewarm_seq;

    (void)arg;
    g_sfx_thread_running = 1;
    calibrate_volume_from_backup();
    if (g_adf_ctor && g_adf_dtor) {
        mem_zero(g_adf_ch, sizeof(g_adf_ch));
        g_adf_ctor(g_adf_ch, (uint16_t)ADF_UNIT_ID);
        g_adf_ready = 1;
    }
    do_rescan_sd();

    while (!g_sfx_stop) {
        unsigned w;

        if (g_rescan_seq != last_rescan) {
            last_rescan = g_rescan_seq;
            do_rescan_sd();
            g_rescan_done_seq = last_rescan;
            a7r3_dsb();
        }
        if (g_load_seq != last_load) {
            last_load = g_load_seq;
            if (g_selected_idx >= A7R3_SFX_BUILTIN_CNT)
                load_sd_sound_into_adf(g_selected_idx - A7R3_SFX_BUILTIN_CNT);
        }
        if (g_trigger_seq != last_trig) {
            last_trig = g_trigger_seq;
            if (last_trig <= 5u)
                a7r3_log_hex("SDK_SFX_TRIG=", (unsigned long)last_trig);
            if (sfx_should_trigger(g_sfx_mode, g_silent_shutter_on))
                play_current_sound(0, last_trig);
        }
        if (g_skipped_mech_seq != last_mech) {
            last_mech = g_skipped_mech_seq;
            if (last_mech <= 5u)
                a7r3_log_hex("SDK_SFX_MECH_SKIP=", (unsigned long)last_mech);
        }
        if (g_preview_seq != last_prev) {
            last_prev = g_preview_seq;
            play_current_sound(1, 0u);
        }
        if (g_prewarm_seq != last_prewarm) {
            last_prewarm = g_prewarm_seq;
            if (g_sfx_mode != A7R3_SFX_MODE_OFF)
                prewarm_adf();
        }

        w = g_wake_seq;
        a7r3_dsb();
        if (!g_sfx_stop &&
            g_rescan_seq == last_rescan &&
            g_load_seq == last_load &&
            g_trigger_seq == last_trig &&
            g_preview_seq == last_prev &&
            g_prewarm_seq == last_prewarm) {
            a7r3_futex_wait(&g_wake_seq, w, 0u);
        }
    }

    if (g_adf_ready && g_adf_dtor) {
        g_adf_dtor(g_adf_ch);
        g_adf_ready = 0;
    }
    release_custom_sound_dma();
    g_sfx_thread_running = 0;
    a7r3_dsb();
    return NULL;
}

int a7r3_shutter_sfx_install(void)
{
    volatile unsigned long *slot;
    unsigned long cur;
    unsigned long tid = 0;
    int (*volatile pcreate)(unsigned long *, const void *,
                            void *(*)(void *), void *) = pthread_create;

    if (g_sfx_installed)
        return 0;

    g_obj_base = a7r3_mod_base("/libObj.so");
    g_biz_base = a7r3_mod_base("/libBizFw.so");
    g_fsys_base = a7r3_mod_base("/libInfraFsys.so");

    if (!g_obj_base || !g_biz_base || !g_fsys_base) {
        a7r3_log_str("SDK_SFX_NO_MOD\n");
        return -1;
    }

    g_msg_get_id = (objmsg_get_id_fn)(uintptr_t)read_got(g_obj_base, LIBOBJ_OBJMSG_GETID_GOT);
    g_msg_get_prm = (objmsg_get_prm_fn)(uintptr_t)read_got(g_obj_base, LIBOBJ_OBJMSG_GETPRM_GOT);
    g_backup_read = (backup_read_fn)(uintptr_t)read_got(g_obj_base, LIBOBJ_BACKUP_READ_GOT);

    g_df_snd_async = (df_snd_async_fn)(uintptr_t)(g_biz_base + BIZ_DFCH_SND_ASYNC);
    g_adf_ctor = (adf_ctor_fn)(uintptr_t)(g_biz_base + BIZ_ADFCH_CTOR);
    g_adf_dtor = (adf_dtor_fn)(uintptr_t)(g_biz_base + BIZ_ADFCH_DTOR);
    g_adf_set_table = (adf_set_table_fn)(uintptr_t)(g_biz_base + BIZ_ADFCH_SYS_SETTABLE);
    g_adf_set_path = (adf_set_path_fn)(uintptr_t)(g_biz_base + BIZ_ADFCH_SYS_SETPATH);
    g_adf_request = (adf_request_fn)(uintptr_t)(g_biz_base + BIZ_ADFCH_SYS_REQUEST);
    g_adf_set_vol = (adf_set_vol_fn)(uintptr_t)(g_biz_base + BIZ_ADFCH_SYS_SETVOL);

    g_dmm_ctor = (dmm_ctor_fn)(uintptr_t)(g_biz_base + BIZ_DMMCH_CTOR);
    g_dmm_dtor = (dmm_dtor_fn)(uintptr_t)(g_biz_base + BIZ_DMMCH_DTOR);
    g_dmm_alloc_new = (dmm_alloc_new_fn)(uintptr_t)(g_biz_base + BIZ_DMMCH_ALLOC_NEW);
    g_dmm_open = (dmm_open_fn)(uintptr_t)(g_biz_base + BIZ_DMMCH_OPEN);
    g_dmm_close = (dmm_close_fn)(uintptr_t)(g_biz_base + BIZ_DMMCH_CLOSE);
    g_dmm_free = (dmm_free_fn)(uintptr_t)(g_biz_base + BIZ_DMMCH_FREE);

    g_phyc_ctor = (phyc_ctor_fn)(uintptr_t)(g_biz_base + BIZ_PHYCMEM_CTOR);
    g_phyc_getlog = (phyc_getlog_fn)(uintptr_t)(g_biz_base + BIZ_PHYCMEM_GETLOG);
    g_phyc_dtor = (phyc_dtor_fn)(uintptr_t)(g_biz_base + BIZ_PHYCMEM_DTOR);

    g_fsys_get_drv = (fsys_get_drv_fn)(uintptr_t)(g_fsys_base + FSYS_GET_DRV_FROM_PATH);
    g_fsys_get_mutex = (fsys_get_mutex_fn)(uintptr_t)(g_fsys_base + FSYS_GET_DEV_MUTEX_ID);
    g_fsys_unlock_dev = (fsys_unlock_dev_fn)(uintptr_t)(g_fsys_base + FSYS_UNLOCK_DEVICE);
    g_fsys_set_ctx = (fsys_set_ctx_fn)(uintptr_t)(g_fsys_base + FSYS_SET_CONTEXT);
    g_fsys_mount_drives = (fsys_get_mount_drives_fn)(uintptr_t)(g_fsys_base + FSYS_GET_MOUNT_DRIVES);
    g_prfile_fsfirst = (prfile_fsfirst_fn)(uintptr_t)(g_fsys_base + FSYS_PRFILE_FSFIRST);
    g_prfile_fsnext = (prfile_fsnext_fn)(uintptr_t)(g_fsys_base + FSYS_PRFILE_FSNEXT);
    g_pf2_fopen = (pf2_fopen_fn)(uintptr_t)(g_fsys_base + FSYS_PF2_FOPEN);
    g_pf2_fread = (pf2_fread_fn)(uintptr_t)(g_fsys_base + FSYS_PF2_FREAD);
    g_pf2_fclose = (pf2_fclose_fn)(uintptr_t)(g_fsys_base + FSYS_PF2_FCLOSE);
    g_tntfs_fopen = (tntfs_fopen_fn)(uintptr_t)read_got(g_fsys_base, FSYS_TNTFS_FOPEN_GOT);
    g_tntfs_fread = (tntfs_fread_fn)(uintptr_t)read_got(g_fsys_base, FSYS_TNTFS_FREAD_GOT);
    g_tntfs_fclose = (tntfs_fclose_fn)(uintptr_t)read_got(g_fsys_base, FSYS_TNTFS_FCLOSE_GOT);
    g_tntfs_opendir = (tntfs_opendir_fn)(uintptr_t)read_got(g_fsys_base, FSYS_TNTFS_OPENDIR_GOT);
    g_tntfs_readdir = (tntfs_readdir_fn)(uintptr_t)read_got(g_fsys_base, FSYS_TNTFS_READDIR_GOT);
    g_tntfs_closedir = (tntfs_closedir_fn)(uintptr_t)read_got(g_fsys_base, FSYS_TNTFS_CLOSEDIR_GOT);

    slot = (volatile unsigned long *)(g_obj_base + LIBOBJ_RECCNT_PARSE_SLOT);
    if (!a7r3_pm_writable((const void *)slot, 4u)) {
        a7r3_log_str("SDK_SFX_SLOT_RO\n");
        return -1;
    }
    cur = *slot;
    if (cur != g_obj_base + LIBOBJ_RECCNT_PARSE_REAL) {
        a7r3_log_hex("SDK_SFX_SLOT_MISMATCH=", cur);
        return -1;
    }

    g_reccnt_slot = slot;
    g_orig_reccnt = cur;
    g_real_reccnt = (reccnt_parse_fn)(uintptr_t)cur;
    *slot = (unsigned long)(uintptr_t)parse_reccnt_hook;
    a7r3_dsb();

    g_sfx_stop = 0;
    g_sfx_installed = 1;
    if (pcreate)
        pcreate(&tid, NULL, sfx_worker, NULL);

    a7r3_log_str("SDK_SFX_INSTALL_OK\n");
    return 0;
}

void a7r3_shutter_sfx_uninstall(void)
{
    int spin;

    if (!g_sfx_installed)
        return;

    g_sfx_mode = A7R3_SFX_MODE_OFF;
    g_sfx_stop = 1;
    wake_sfx_worker();

    if (g_reccnt_slot && g_orig_reccnt) {
        *g_reccnt_slot = g_orig_reccnt;
        a7r3_dsb();
        a7r3_log_str("SDK_SFX_RESTORED=0x1\n");
    }

    for (spin = 0; spin < 200 && g_sfx_thread_running; spin++)
        a7r3_nanosleep_ms(2u);

    g_reccnt_slot = NULL;
    g_sfx_installed = 0;
}

void a7r3_shutter_sfx_set_mode(int mode)
{
    if (mode < A7R3_SFX_MODE_OFF || mode > A7R3_SFX_MODE_BOTH)
        mode = A7R3_SFX_MODE_OFF;
    g_sfx_mode = mode;
    a7r3_log_hex("SDK_SFX_MODE=", (unsigned long)(unsigned)mode);
    if (mode != A7R3_SFX_MODE_OFF) {
        g_prewarm_seq++;
        wake_sfx_worker();
    } else {
        a7r3_dsb();
    }
}

int a7r3_shutter_sfx_get_mode(void)
{
    return g_sfx_mode;
}

void a7r3_shutter_sfx_set_enabled(int on)
{
    a7r3_shutter_sfx_set_mode(on ? A7R3_SFX_MODE_ESHUT : A7R3_SFX_MODE_OFF);
}

int a7r3_shutter_sfx_get_enabled(void)
{
    return g_sfx_mode != A7R3_SFX_MODE_OFF;
}

int a7r3_shutter_sfx_is_eshutter(void)
{
    return g_silent_shutter_on;
}

void a7r3_shutter_sfx_refresh_state(void)
{
    if (g_sfx_installed)
        refresh_eshutter_state();
}

int a7r3_shutter_sfx_rescan_sd(void)
{
    g_rescan_seq++;
    wake_sfx_worker();
    return g_sd_count;
}

int a7r3_shutter_sfx_rescan_sd_sync(void)
{
    unsigned target;
    int spin;
    if (!g_sfx_thread_running)
        return g_sd_count;
    target = g_rescan_seq + 1u;
    g_rescan_seq = target;
    wake_sfx_worker();
    for (spin = 0; spin < 300; spin++) {
        a7r3_dsb();
        if ((int)(g_rescan_done_seq - target) >= 0 || !g_sfx_thread_running)
            break;
        a7r3_nanosleep_ms(5u);
    }
    return g_sd_count;
}

int a7r3_shutter_sfx_count(void)
{
    return A7R3_SFX_BUILTIN_CNT + g_sd_count;
}

int a7r3_shutter_sfx_sd_count(void)
{
    return g_sd_count;
}

int a7r3_shutter_sfx_get_selected(void)
{
    return g_selected_idx;
}

int a7r3_shutter_sfx_select(int idx)
{
    int total = A7R3_SFX_BUILTIN_CNT + g_sd_count;
    if (idx < 0)
        idx = total - 1;
    else if (idx >= total)
        idx = 0;
    g_selected_idx = idx;
    if (idx >= A7R3_SFX_BUILTIN_CNT)
        g_load_seq++;
    g_prewarm_seq++;
    wake_sfx_worker();
    return g_selected_idx;
}

const char *a7r3_shutter_sfx_name(int idx)
{
    if (idx == 0)
        return "内置测试音效";
    if (idx >= A7R3_SFX_BUILTIN_CNT && idx < A7R3_SFX_BUILTIN_CNT + g_sd_count)
        return g_sd_files[idx - A7R3_SFX_BUILTIN_CNT].name;
    if (idx == A7R3_SFX_BUILTIN_CNT && g_sd_count == 0)
        return "SD卡暂无WAV (点击刷新)";
    return "";
}

void a7r3_shutter_sfx_set_volume(int level)
{
    if (level < 0)
        level = 0;
    else if (level > 2)
        level = 2;
    g_vol_level = level;
    g_prewarm_seq++;
    wake_sfx_worker();
}

int a7r3_shutter_sfx_get_volume(void)
{
    return g_vol_level;
}

void a7r3_shutter_sfx_preview(void)
{
    g_preview_seq++;
    wake_sfx_worker();
}

void a7r3_shutter_sfx_reset(void)
{
    g_sfx_mode = A7R3_SFX_MODE_OFF;
    g_selected_idx = 0;
    g_vol_level = 1;
    a7r3_dsb();
}
