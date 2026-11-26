#ifndef A7R3_FB_HEADER_H
#define A7R3_FB_HEADER_H

#include <stdint.h>

#define A7R3_FB_W          640u
#define A7R3_FB_H          480u
#define A7R3_FB_PIXELS     (A7R3_FB_W * A7R3_FB_H)
#define A7R3_FB_BYTES      (A7R3_FB_PIXELS * 2u)

/* Sony ILCE-7RM3 (v3.10) real-hardware display framebuffers */
#define A7R3_PA_RENDER     0x202E0200UL /* 640x480 RGBA4444, 8x8 Morton tiled, bottom-up */
#define A7R3_PA_FLIP_A     0x23A80000UL /* 640x480 RGBA4444, linear row-major, top-down */
#define A7R3_PA_FLIP_B     0x23B16000UL /* 640x480 RGBA4444, linear row-major, top-down */

/* libObj.so writable .got slot for ioctl (GPU present wait/submit cmds 0xc0048211/2) */
#define A7R3_LIBOBJ_IOCTL_GOT 0x0121cad0UL

/* Convert logical top-down (x, y) into 8x8 Morton tiled bottom-up pixel index
 * used by A7R3's GPU render target (0x202E0200). */
static inline unsigned a7r3_morton_index(unsigned x, unsigned y)
{
    unsigned yy = (A7R3_FB_H - 1u) - y;
    unsigned rowbase = (yy >> 3) * (A7R3_FB_W >> 3); /* 80 tiles per tile-row */
    unsigned z = (x & 1u) | ((yy & 1u) << 1) | ((x & 2u) << 1) |
                 ((yy & 2u) << 2) | ((x & 4u) << 2) | ((yy & 4u) << 3);
    return (rowbase + (x >> 3)) * 64u + z;
}

/* Map /dev/mem with O_RDWR | O_SYNC (0x101002) for uncached hardware writes. */
int  a7r3_fb_open(void);
void a7r3_fb_close(void);

/* Convert a 640x480 linear RGBA4444 canvas into internal Morton + linear staging buffers. */
void a7r3_fb_pack_frame(const uint16_t *lin_rgba4444);

/* Write the packed full-screen frame to flipA, flipB, and render. */
void a7r3_fb_present_full(void);

/* Write only a sub-rectangle [x0..x0+w, y0..y0+h) directly onto flipA, flipB, and render.
 * Useful for drawing a corner entry button or floating popup over the native UI. */
void a7r3_fb_blit_rect(int x0, int y0, int w, int h,
                       const uint16_t *src_rgba4444, int src_stride);

/* Capture / restore the stock linear frame (flipA) for clean overlay open/close. */
int  a7r3_fb_grab_native(uint16_t *dst_lin);
void a7r3_fb_restore_native(const uint16_t *src_lin);

/* Pre-packed fast native save/restore (< 1 ms restore without re-packing). */
int  a7r3_fb_save_native(uint16_t *dst_lin_opt);
void a7r3_fb_restore_saved(void);

/* Repair any partial native GPU overwrites using 60-band coprime sampling + rotating exact sweep. */
unsigned a7r3_fb_repair_tick(void);

/* Atomic libObj.so ioctl .got hook: auto-repairs bands around GPU submit ioctls while active. */
int  a7r3_fb_install_ioctl_hook(void);
void a7r3_fb_remove_ioctl_hook(void);
void a7r3_fb_set_guard_active(int active);

/* Passive GPU surface PA sniffing (no writes) after a display switch. */
void a7r3_fb_arm_pa_probe(unsigned budget);
int  a7r3_fb_pop_probed_pa(unsigned long *out_pa);

/* Lightweight pixel-exact transparent AF reticle overlay for playback mode. */
void a7r3_fb_pb_box_show(int cx, int cy, int half_w, int half_h);
void a7r3_fb_pb_box_hide(void);

#endif /* A7R3_FB_HEADER_H */
