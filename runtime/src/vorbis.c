/*
 * vorbis.c - libvorbisfile_64.dll implemented natively on top of stb_vorbis v1.22.
 */
#define _GNU_SOURCE
#include "w64.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "../../third_party/stb/stb_vorbis.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* Win64 vorbis_info layout (48 bytes):
 *   0: int version
 *   4: int channels
 *   8: long rate (32-bit on Win64!)
 *  12: long bitrate_upper
 *  16: long bitrate_nominal
 *  20: long bitrate_lower
 *  24: long bitrate_window
 *  32: void *codec_setup */
typedef struct {
    int32_t version;
    int32_t channels;
    int32_t rate;
    int32_t bitrate_upper;
    int32_t bitrate_nominal;
    int32_t bitrate_lower;
    int32_t bitrate_window;
    int32_t pad;
    uint64_t codec_setup;
} w64_vorbis_info_t;

#include <pthread.h>

#define OV_MAGIC 0x564F524249533634ull
typedef struct {
    uint64_t vf;
    stb_vorbis *v;
    uint8_t *buf;
    size_t len;
    uint64_t datasource;
    uint64_t close_fn;
    w64_vorbis_info_t info;
    uint64_t total_samples;
    double total_time;
} ov_wrap_t;

#define OV_MAX_HANDLES 256
static pthread_mutex_t g_ov_mu = PTHREAD_MUTEX_INITIALIZER;
static ov_wrap_t *g_ov_tab[OV_MAX_HANDLES];

static ov_wrap_t *ov_get(uint64_t vf)
{
    if (!vf) return NULL;
    pthread_mutex_lock(&g_ov_mu);
    ov_wrap_t *found = NULL;
    for (int i = 0; i < OV_MAX_HANDLES; i++) {
        if (g_ov_tab[i] && g_ov_tab[i]->vf == vf) {
            found = g_ov_tab[i];
            break;
        }
    }
    pthread_mutex_unlock(&g_ov_mu);
    return found;
}

static void ov_put(uint64_t vf, ov_wrap_t *w)
{
    pthread_mutex_lock(&g_ov_mu);
    int free_idx = -1;
    for (int i = 0; i < OV_MAX_HANDLES; i++) {
        if (g_ov_tab[i] && g_ov_tab[i]->vf == vf) {
            ov_wrap_t *old = g_ov_tab[i];
            g_ov_tab[i] = w;
            pthread_mutex_unlock(&g_ov_mu);
            if (old && old != w) {
                if (old->v) stb_vorbis_close(old->v);
                free(old->buf);
                free(old);
            }
            return;
        }
        if (!g_ov_tab[i] && free_idx < 0) free_idx = i;
    }
    if (free_idx >= 0) g_ov_tab[free_idx] = w;
    else w64_log("[vorbis] table full (%d)", OV_MAX_HANDLES);
    pthread_mutex_unlock(&g_ov_mu);
}

static ov_wrap_t *ov_take(uint64_t vf)
{
    if (!vf) return NULL;
    pthread_mutex_lock(&g_ov_mu);
    ov_wrap_t *found = NULL;
    for (int i = 0; i < OV_MAX_HANDLES; i++) {
        if (g_ov_tab[i] && g_ov_tab[i]->vf == vf) {
            found = g_ov_tab[i];
            g_ov_tab[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&g_ov_mu);
    return found;
}

/* int ov_open_callbacks(void *datasource, OggVorbis_File *vf, const char *initial, long ibytes, ov_callbacks callbacks)
 * On Win64, ov_callbacks is 32 bytes (4 function pointers), so it is passed by pointer in arg 4! */
static void ov_open_callbacks_(CPU *c)
{
    uint64_t ds = A(0), vf = A(1), init_p = A(2), cb_p = A(4);
    int32_t ibytes = AI(3);
    if (!vf || !cb_p) RET((uint32_t)-131 /* OV_EFAULT */);
    uint64_t read_fn  = rd64(cb_p + 0);
    uint64_t seek_fn  = rd64(cb_p + 8);
    uint64_t close_fn = rd64(cb_p + 16);
    uint64_t tell_fn  = rd64(cb_p + 24);
    static int vlog = 0;

    size_t cap = 65536, len = 0;
    if (seek_fn && tell_fn) {
        int64_t cur = (int64_t)(int32_t)w64_call(c, tell_fn, 1, ds);
        /* UE3's OggSeekCallback returns the new byte offset (not 0), so always seek back to cur. */
        w64_call(c, seek_fn, 3, ds, 0, 2 /* SEEK_END */);
        int64_t end = (int64_t)(int32_t)w64_call(c, tell_fn, 1, ds);
        if (end > cur && end < (256ll << 20))
            cap = (size_t)(end - cur) + (ibytes > 0 ? (size_t)ibytes : 0) + 64;
        w64_call(c, seek_fn, 3, ds, (uint64_t)(cur >= 0 ? cur : 0), 0 /* SEEK_SET */);
    }
    uint8_t *buf = malloc(cap);
    if (!buf) RET((uint32_t)-131);
    if (init_p && ibytes > 0) {
        if ((size_t)ibytes > cap) { cap = (size_t)ibytes + 65536; buf = realloc(buf, cap); }
        memcpy(buf, (const void *)(uintptr_t)init_p, (size_t)ibytes);
        len = (size_t)ibytes;
    }
    if (read_fn) {
        for (;;) {
            if (len + 65536 > cap) {
                cap *= 2;
                if (cap > (256u << 20)) break;
                uint8_t *nb = realloc(buf, cap);
                if (!nb) break;
                buf = nb;
            }
            uint64_t got = w64_call(c, read_fn, 4, (uint64_t)(uintptr_t)(buf + len), 1, 65536, ds);
            if (!got || got > 65536) break;
            len += (size_t)got;
        }
    }
    int err = 0;
    stb_vorbis *v = stb_vorbis_open_memory(buf, (int)len, &err, NULL);
    if (!v) {
        w64_log("[vorbis] stb_vorbis_open_memory failed (%d, %zu bytes)", err, len);
        free(buf);
        RET((uint32_t)-132 /* OV_ENOTVORBIS */);
    }
    stb_vorbis_info si = stb_vorbis_get_info(v);
    ov_wrap_t *w = calloc(1, sizeof *w);
    w->vf = vf;
    w->v = v;
    w->buf = buf;
    w->len = len;
    w->datasource = ds;
    w->close_fn = close_fn;
    w->info.version = 0;
    w->info.channels = si.channels;
    w->info.rate = (int32_t)si.sample_rate;
    w->total_samples = stb_vorbis_stream_length_in_samples(v);
    w->total_time = stb_vorbis_stream_length_in_seconds(v);
    ov_put(vf, w);
    if (vlog++ < 5)
        w64_log("[vorbis] opened vf=%#llx %zu bytes -> %dch %dHz %.2fs (%llu samples)",
                (unsigned long long)vf, len, si.channels, (int)si.sample_rate,
                w->total_time, (unsigned long long)w->total_samples);
    RET(0);
}

static void ov_clear_(CPU *c)
{
    uint64_t vf = A(0);
    ov_wrap_t *w = ov_take(vf);
    if (w) {
        if (w->v) stb_vorbis_close(w->v);
        if (w->close_fn && w->datasource) w64_call(c, w->close_fn, 1, w->datasource);
        free(w->buf);
        free(w);
    }
    RET(0);
}

static void ov_info_(CPU *c)
{
    ov_wrap_t *w = ov_get(A(0));
    RET(w ? (uint64_t)(uintptr_t)&w->info : 0);
}

static void ov_pcm_total_(CPU *c)
{
    ov_wrap_t *w = ov_get(A(0));
    RET(w ? w->total_samples : (uint64_t)-1);
}

static void ov_time_total_(CPU *c)
{
    ov_wrap_t *w = ov_get(A(0));
    RETD(w ? w->total_time : -1.0);
}

static void ov_pcm_seek_page_(CPU *c)
{
    ov_wrap_t *w = ov_get(A(0));
    if (!w || !w->v) RET((uint32_t)-131);
    int64_t pos = (int64_t)A(1);
    if (pos < 0) pos = 0;
    int ok = stb_vorbis_seek(w->v, (unsigned int)pos);
    RET(ok ? 0 : (uint32_t)-128);
}

/* long ov_read(OggVorbis_File *vf, char *buffer, int length, int bigendianp, int word, int sgned, int *bitstream) */
static void ov_read_(CPU *c)
{
    ov_wrap_t *w = ov_get(A(0));
    if (!w || !w->v || !A(1)) RET((uint32_t)-131);
    uint8_t *out = (uint8_t *)AP(1);
    int length = AI(2);
    int word = AI(4);
    uint64_t bs = A(6);
    if (bs) wr32(bs, 0);
    if (length <= 0) RET(0);
    int ch = w->info.channels > 0 ? w->info.channels : 2;
    int shorts = length / 2;
    if (word == 1) shorts = length;
    if (shorts < ch) RET(0);
    if (word == 1) {
        int16_t tmp[2048];
        int want = shorts < 2048 ? shorts : 2048;
        int got_frames = stb_vorbis_get_samples_short_interleaved(w->v, ch, tmp, want);
        int got = got_frames * ch;
        for (int i = 0; i < got; i++) out[i] = (uint8_t)((tmp[i] >> 8) + (AI(5) ? 0 : 128));
        RET((uint32_t)got);
    }
    int got_frames = stb_vorbis_get_samples_short_interleaved(w->v, ch, (short *)out, shorts);
    RET((uint32_t)(got_frames * ch * 2));
}

static const w64_export_t vorbis_exports[] = {
    { "libvorbisfile_64.dll", "ov_open_callbacks", ov_open_callbacks_ },
    { "libvorbisfile_64.dll", "ov_clear", ov_clear_ },
    { "libvorbisfile_64.dll", "ov_info", ov_info_ },
    { "libvorbisfile_64.dll", "ov_pcm_total", ov_pcm_total_ },
    { "libvorbisfile_64.dll", "ov_time_total", ov_time_total_ },
    { "libvorbisfile_64.dll", "ov_pcm_seek_page", ov_pcm_seek_page_ },
    { "libvorbisfile_64.dll", "ov_read", ov_read_ },
    { 0 }
};
__attribute__((constructor(102))) static void w64_vorbis_init(void) { w64_register(vorbis_exports); }
