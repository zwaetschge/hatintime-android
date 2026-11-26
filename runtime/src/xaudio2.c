/*
 * xaudio2.c - Native XAudio2.7 COM + software stereo mixer -> SDL3 audio sink,
 * plus X3DAudio1_7 and XAPOFX1_5 for A Hat in Time (Unreal Engine 3).
 */
#define _GNU_SOURCE
#include "w64.h"
#include <math.h>
#include <pthread.h>
#include <unistd.h>

/* Platform audio sink (android_main.c; weak no-op on headless host) */
__attribute__((weak)) int  w64_audio_sink_open(int rate, int channels) { (void)rate; (void)channels; return -1; }
__attribute__((weak)) void w64_audio_sink_write(const float *frames, int n) { (void)frames; (void)n; }
__attribute__((weak)) int  w64_audio_sink_queued(void) { return 0; }

#define XA_RATE 48000
#define XA_MAX_VOICES 128
#define XA_MAX_BUFS 16

typedef struct {
    const uint8_t *data;
    uint32_t bytes;
    uint32_t flags;
    uint32_t play_begin, play_len;
    uint32_t loop_begin, loop_len, loop_count;
    uint64_t context;
} xabuf_t;

typedef struct {
    uint64_t vptr;
    int used, kind;           /* 1 = source, 2 = submix, 3 = master */
    int playing;
    int channels, sample_rate, bits_per_sample, is_float;
    float volume;
    float freq_ratio;
    float gain_l, gain_r;
    uint64_t callback;
    uint64_t samples_played;
    xabuf_t q[XA_MAX_BUFS];
    int qh, qn;
    double cur_pos;           /* frame position in q[qh] */
    uint32_t loops_left;
} xavoice_t;

typedef struct {
    uint64_t vptr;
    int ref;
    int running;
} xa2_t;

static pthread_mutex_t g_xamu = PTHREAD_MUTEX_INITIALIZER;
static xavoice_t g_voices[XA_MAX_VOICES];
static xa2_t g_xa2;
static int g_mixer_started;
static uint64_t g_vt_xa2[20];
static uint64_t g_vt_master[24];
static uint64_t g_vt_submix[24];
static uint64_t g_vt_source[32];
static uint64_t g_vt_apo[16];
static uint64_t g_apo_obj;

static float read_sample(const xavoice_t *v, const xabuf_t *b, uint32_t frame, int ch)
{
    int nc = v->channels > 0 ? v->channels : 1;
    if (ch >= nc) ch = nc - 1;
    if (v->is_float && v->bits_per_sample == 32) {
        size_t off = ((size_t)frame * (size_t)nc + (size_t)ch) * 4;
        if (off + 4 > b->bytes) return 0.0f;
        float s; memcpy(&s, b->data + off, 4); return s;
    }
    if (v->bits_per_sample == 16) {
        size_t off = ((size_t)frame * (size_t)nc + (size_t)ch) * 2;
        if (off + 2 > b->bytes) return 0.0f;
        int16_t s; memcpy(&s, b->data + off, 2); return (float)s * (1.0f / 32768.0f);
    }
    if (v->bits_per_sample == 8) {
        size_t off = (size_t)frame * (size_t)nc + (size_t)ch;
        if (off >= b->bytes) return 0.0f;
        return ((float)b->data[off] - 128.0f) * (1.0f / 128.0f);
    }
    return 0.0f;
}

static void *xa_mixer_thread(void *arg)
{
    (void)arg;
    int sink_ok = w64_audio_sink_open(XA_RATE, 2) == 0;
    w64_log("[xaudio2] mixer thread started (%d Hz stereo, sink %s)", XA_RATE, sink_ok ? "SDL3" : "null");
    const int N = 480; /* 10 ms */
    float mix[480 * 2];
    int blocks = 0;
    float peak = 0.0f;
    for (;;) {
        if (sink_ok && w64_audio_sink_queued() > XA_RATE / 15) {
            usleep(4000);
            continue;
        }
        memset(mix, 0, sizeof mix);
        pthread_mutex_lock(&g_xamu);
        if (g_xa2.running) {
            for (int vi = 0; vi < XA_MAX_VOICES; vi++) {
                xavoice_t *v = &g_voices[vi];
                if (!v->used || v->kind != 1 || !v->playing || v->qn == 0) continue;
                int bps = (v->bits_per_sample / 8) * (v->channels > 0 ? v->channels : 1);
                if (bps <= 0) continue;
                double step = ((double)(v->sample_rate > 0 ? v->sample_rate : XA_RATE) * (double)v->freq_ratio) / (double)XA_RATE;
                if (step < 0.01) step = 0.01;
                if (step > 16.0) step = 16.0;
                float gl = v->volume * v->gain_l, gr = v->volume * v->gain_r;
                for (int i = 0; i < N && v->qn > 0; i++) {
                    xabuf_t *b = &v->q[v->qh];
                    uint32_t total = b->bytes / (uint32_t)bps;
                    uint32_t end = (b->play_len && b->play_begin + b->play_len <= total) ? (b->play_begin + b->play_len) : total;
                    uint32_t fi = (uint32_t)v->cur_pos;
                    if (fi >= end || !b->data) {
                        if (v->loops_left > 0) {
                            if (v->loops_left < 255) v->loops_left--;
                            v->cur_pos = (double)b->loop_begin;
                            continue;
                        }
                        v->qh = (v->qh + 1) % XA_MAX_BUFS;
                        v->qn--;
                        v->cur_pos = v->qn > 0 ? (double)v->q[v->qh].play_begin : 0.0;
                        v->loops_left = v->qn > 0 ? v->q[v->qh].loop_count : 0;
                        continue;
                    }
                    float sl = read_sample(v, b, fi, 0);
                    float sr = v->channels >= 2 ? read_sample(v, b, fi, 1) : sl;
                    mix[i * 2 + 0] += sl * gl;
                    mix[i * 2 + 1] += sr * gr;
                    v->cur_pos += step;
                    v->samples_played++;
                }
            }
        }
        pthread_mutex_unlock(&g_xamu);
        for (int i = 0; i < N * 2; i++) {
            float x = mix[i];
            if (x > 1.0f) x = 1.0f; else if (x < -1.0f) x = -1.0f;
            float ax = x < 0 ? -x : x;
            if (ax > peak) peak = ax;
            mix[i] = x;
        }
        if (sink_ok) w64_audio_sink_write(mix, N);
        else usleep(10000);
        if (++blocks >= 1000) {
            w64_log("[xaudio2] last 10 s: peak %.3f", peak);
            blocks = 0; peak = 0.0f;
        }
    }
    return NULL;
}

static void xa_start_mixer(void)
{
    if (g_mixer_started) return;
    g_mixer_started = 1;
    pthread_t th;
    if (pthread_create(&th, NULL, xa_mixer_thread, NULL) == 0) pthread_detach(th);
}

/* ---- IXAudio2Voice common methods (0..18) ---- */
static void xv_GetVoiceDetails_(CPU *c)
{
    xavoice_t *v = AP(0);
    if (A(1)) {
        wr32(A(1) + 0, 0);
        wr32(A(1) + 4, (uint32_t)(v && v->channels ? v->channels : 2));
        wr32(A(1) + 8, (uint32_t)(v && v->sample_rate ? v->sample_rate : XA_RATE));
    }
    RETV();
}
static void xv_ok_(CPU *c) { RET(0); }
static void xv_void_(CPU *c) { RETV(); }
static void xv_SetVolume_(CPU *c)
{
    xavoice_t *v = AP(0);
    if (v) v->volume = AF(1);
    RET(0);
}
static void xv_GetVolume_(CPU *c)
{
    xavoice_t *v = AP(0);
    if (A(1)) { float vol = v ? v->volume : 1.0f; memcpy(AP(1), &vol, 4); }
    RETV();
}
static void xv_SetChannelVolumes_(CPU *c)
{
    xavoice_t *v = AP(0);
    uint32_t n = AU(1);
    const float *vols = AP(2);
    if (v && vols && n >= 1) {
        v->gain_l = vols[0];
        v->gain_r = n >= 2 ? vols[1] : vols[0];
    }
    RET(0);
}
static void xv_SetOutputMatrix_(CPU *c)
{
    /* (this, pDestVoice, SrcCh, DstCh, const float* pLevelMatrix, OperationSet) */
    xavoice_t *v = AP(0);
    uint32_t src = AU(2), dst = AU(3);
    const float *m = AP(4);
    if (v && m && src >= 1 && dst >= 1) {
        float l = 0.0f, r = 0.0f;
        for (uint32_t s = 0; s < src; s++) {
            l += m[0 * src + s];
            r += (dst >= 2) ? m[1 * src + s] : m[0 * src + s];
        }
        if (src > 1) { l /= (float)src; r /= (float)src; }
        v->gain_l = l; v->gain_r = r;
    }
    RET(0);
}
static void xv_DestroyVoice_(CPU *c)
{
    xavoice_t *v = AP(0);
    pthread_mutex_lock(&g_xamu);
    if (v) { v->playing = 0; v->qn = 0; v->used = 0; }
    pthread_mutex_unlock(&g_xamu);
    RETV();
}

/* ---- IXAudio2SourceVoice methods (19..28) ---- */
static void xsv_Start_(CPU *c) { xavoice_t *v = AP(0); pthread_mutex_lock(&g_xamu); if (v) v->playing = 1; pthread_mutex_unlock(&g_xamu); RET(0); }
static void xsv_Stop_(CPU *c) { xavoice_t *v = AP(0); pthread_mutex_lock(&g_xamu); if (v) v->playing = 0; pthread_mutex_unlock(&g_xamu); RET(0); }
static void xsv_SubmitSourceBuffer_(CPU *c)
{
    xavoice_t *v = AP(0);
    uint64_t bp = A(1);
    if (!v || !bp) RET(0x80070057u);
    /* XAUDIO2_BUFFER (Win64): Flags 0, AudioBytes 4, pAudioData 8, PlayBegin 16, PlayLength 20, LoopBegin 24, LoopLength 28, LoopCount 32, pContext 40 */
    pthread_mutex_lock(&g_xamu);
    if (v->qn < XA_MAX_BUFS) {
        int idx = (v->qh + v->qn) % XA_MAX_BUFS;
        xabuf_t *b = &v->q[idx];
        b->flags = rd32(bp + 0);
        b->bytes = rd32(bp + 4);
        b->data = (const uint8_t *)(uintptr_t)rd64(bp + 8);
        b->play_begin = rd32(bp + 16);
        b->play_len = rd32(bp + 20);
        b->loop_begin = rd32(bp + 24);
        b->loop_len = rd32(bp + 28);
        b->loop_count = rd32(bp + 32);
        b->context = rd64(bp + 40);
        if (v->qn == 0) { v->cur_pos = (double)b->play_begin; v->loops_left = b->loop_count; }
        v->qn++;
    }
    pthread_mutex_unlock(&g_xamu);
    RET(0);
}
static void xsv_FlushSourceBuffers_(CPU *c)
{
    xavoice_t *v = AP(0);
    pthread_mutex_lock(&g_xamu);
    if (v) { v->qn = 0; v->qh = 0; v->cur_pos = 0; v->loops_left = 0; }
    pthread_mutex_unlock(&g_xamu);
    RET(0);
}
static void xsv_ExitLoop_(CPU *c)
{
    xavoice_t *v = AP(0);
    pthread_mutex_lock(&g_xamu);
    if (v) v->loops_left = 0;
    pthread_mutex_unlock(&g_xamu);
    RET(0);
}
static void xsv_GetState_(CPU *c)
{
    xavoice_t *v = AP(0);
    uint64_t out = A(1);
    /* XAUDIO2_VOICE_STATE (Win64): pCurrentBufferContext 0, BuffersQueued 8, SamplesPlayed 16 */
    if (out) {
        pthread_mutex_lock(&g_xamu);
        wr64(out + 0, (v && v->qn > 0) ? v->q[v->qh].context : 0);
        wr32(out + 8, (uint32_t)(v ? v->qn : 0));
        wr64(out + 16, v ? v->samples_played : 0);
        pthread_mutex_unlock(&g_xamu);
    }
    RETV();
}
static void xsv_SetFrequencyRatio_(CPU *c) { xavoice_t *v = AP(0); if (v) v->freq_ratio = AF(1); RET(0); }
static void xsv_GetFrequencyRatio_(CPU *c) { xavoice_t *v = AP(0); if (A(1)) { float r = v ? v->freq_ratio : 1.0f; memcpy(AP(1), &r, 4); } RETV(); }
static void xsv_SetSourceSampleRate_(CPU *c) { xavoice_t *v = AP(0); if (v) v->sample_rate = (int)AU(1); RET(0); }

/* ---- IXAudio2 (2.7) methods ---- */
static void xa2_QI_(CPU *c) { wr64(A(2), A(0)); RET(0); }
static void xa2_AddRef_(CPU *c) { RET(++g_xa2.ref); }
static void xa2_Release_(CPU *c) { RET(g_xa2.ref > 1 ? --g_xa2.ref : 1); }
static void xa2_GetDeviceCount_(CPU *c) { if (A(1)) wr32(A(1), 1); RET(0); }
static void xa2_GetDeviceDetails_(CPU *c)
{
    /* XAUDIO2_DEVICE_DETAILS (Win64, 1080 bytes):
     *   0: WCHAR DeviceID[256] (512 bytes)
     * 512: WCHAR DisplayName[256] (512 bytes)
     * 1024: XAUDIO2_DEVICE_ROLE Role (4 bytes = 0xF GlobalDefault)
     * 1028: WAVEFORMATEXTENSIBLE OutputFormat (40 bytes) */
    uint8_t *d = AP(2);
    if (!d) RET(0x80070057u);
    memset(d, 0, 1080);
    utf8_to_w16("default", (uint16_t *)(d + 0), 256);
    utf8_to_w16("Android Audio", (uint16_t *)(d + 512), 256);
    uint32_t role = 0xFu; memcpy(d + 1024, &role, 4);
    uint16_t fmt = 0xFFFEu, ch = 2, align = 4, bits = 16, cb = 22, vbits = 16;
    uint32_t rate = XA_RATE, avg = XA_RATE * 4, cmask = 3; /* KSAUDIO_SPEAKER_STEREO */
    memcpy(d + 1028 + 0, &fmt, 2);
    memcpy(d + 1028 + 2, &ch, 2);
    memcpy(d + 1028 + 4, &rate, 4);
    memcpy(d + 1028 + 8, &avg, 4);
    memcpy(d + 1028 + 12, &align, 2);
    memcpy(d + 1028 + 14, &bits, 2);
    memcpy(d + 1028 + 16, &cb, 2);
    memcpy(d + 1028 + 18, &vbits, 2);
    memcpy(d + 1028 + 20, &cmask, 4);
    static const uint8_t pcm_guid[16] = { 1,0,0,0, 0,0, 0x10,0, 0x80,0, 0,0xAA,0,0x38,0x9B,0x71 };
    memcpy(d + 1028 + 24, pcm_guid, 16);
    RET(0);
}
static void xa2_Initialize_(CPU *c)
{
    g_xa2.running = 1;
    xa_start_mixer();
    w64_log("[xaudio2] IXAudio2::Initialize");
    RET(0);
}
static xavoice_t *alloc_voice(int kind, uint64_t *vt)
{
    pthread_mutex_lock(&g_xamu);
    for (int i = 0; i < XA_MAX_VOICES; i++) {
        if (!g_voices[i].used) {
            xavoice_t *v = &g_voices[i];
            memset(v, 0, sizeof *v);
            v->vptr = (uint64_t)(uintptr_t)vt;
            v->used = 1; v->kind = kind;
            v->channels = 2; v->sample_rate = XA_RATE; v->bits_per_sample = 16;
            v->volume = 1.0f; v->freq_ratio = 1.0f; v->gain_l = 1.0f; v->gain_r = 1.0f;
            pthread_mutex_unlock(&g_xamu);
            return v;
        }
    }
    pthread_mutex_unlock(&g_xamu);
    return NULL;
}
static void xa2_CreateSourceVoice_(CPU *c)
{
    uint64_t pp = A(1), wfx = A(2);
    xavoice_t *v = alloc_voice(1, g_vt_source);
    if (!v) RET(0x8007000Eu);
    if (wfx) {
        uint16_t tag = rd16(wfx + 0);
        v->channels = rd16(wfx + 2);
        v->sample_rate = (int)rd32(wfx + 4);
        v->bits_per_sample = rd16(wfx + 14);
        v->is_float = (tag == 3);
    }
    v->callback = A(6);
    wr64(pp, (uint64_t)(uintptr_t)v);
    RET(0);
}
static void xa2_CreateSubmixVoice_(CPU *c)
{
    xavoice_t *v = alloc_voice(2, g_vt_submix);
    if (!v) RET(0x8007000Eu);
    v->channels = (int)AU(2);
    v->sample_rate = (int)AU(3);
    wr64(A(1), (uint64_t)(uintptr_t)v);
    RET(0);
}
static void xa2_CreateMasteringVoice_(CPU *c)
{
    xavoice_t *v = alloc_voice(3, g_vt_master);
    if (!v) RET(0x8007000Eu);
    v->channels = AU(2) ? (int)AU(2) : 2;
    v->sample_rate = AU(3) ? (int)AU(3) : XA_RATE;
    wr64(A(1), (uint64_t)(uintptr_t)v);
    w64_log("[xaudio2] CreateMasteringVoice (%d ch, %d Hz)", v->channels, v->sample_rate);
    RET(0);
}
static void xa2_StartEngine_(CPU *c) { g_xa2.running = 1; xa_start_mixer(); RET(0); }
static void xa2_StopEngine_(CPU *c) { g_xa2.running = 0; RETV(); }
static void xa2_GetPerformanceData_(CPU *c) { if (A(1)) memset(AP(1), 0, 64); RETV(); }

static void xa2_init_vtables(void)
{
    static int done;
    if (done) return;
    done = 1;
    g_vt_xa2[0] = w64_hostfn("IXAudio2::QI", xa2_QI_);
    g_vt_xa2[1] = w64_hostfn("IXAudio2::AddRef", xa2_AddRef_);
    g_vt_xa2[2] = w64_hostfn("IXAudio2::Release", xa2_Release_);
    g_vt_xa2[3] = w64_hostfn("IXAudio2::GetDeviceCount", xa2_GetDeviceCount_);
    g_vt_xa2[4] = w64_hostfn("IXAudio2::GetDeviceDetails", xa2_GetDeviceDetails_);
    g_vt_xa2[5] = w64_hostfn("IXAudio2::Initialize", xa2_Initialize_);
    g_vt_xa2[6] = w64_hostfn("IXAudio2::RegisterForCallbacks", xv_ok_);
    g_vt_xa2[7] = w64_hostfn("IXAudio2::UnregisterForCallbacks", xv_void_);
    g_vt_xa2[8] = w64_hostfn("IXAudio2::CreateSourceVoice", xa2_CreateSourceVoice_);
    g_vt_xa2[9] = w64_hostfn("IXAudio2::CreateSubmixVoice", xa2_CreateSubmixVoice_);
    g_vt_xa2[10] = w64_hostfn("IXAudio2::CreateMasteringVoice", xa2_CreateMasteringVoice_);
    g_vt_xa2[11] = w64_hostfn("IXAudio2::StartEngine", xa2_StartEngine_);
    g_vt_xa2[12] = w64_hostfn("IXAudio2::StopEngine", xa2_StopEngine_);
    g_vt_xa2[13] = w64_hostfn("IXAudio2::CommitChanges", xv_ok_);
    g_vt_xa2[14] = w64_hostfn("IXAudio2::GetPerformanceData", xa2_GetPerformanceData_);
    g_vt_xa2[15] = w64_hostfn("IXAudio2::SetDebugConfiguration", xv_void_);

    uint64_t base[19] = {
        w64_hostfn("IXAVoice::GetVoiceDetails", xv_GetVoiceDetails_),
        w64_hostfn("IXAVoice::SetOutputVoices", xv_ok_),
        w64_hostfn("IXAVoice::SetEffectChain", xv_ok_),
        w64_hostfn("IXAVoice::EnableEffect", xv_ok_),
        w64_hostfn("IXAVoice::DisableEffect", xv_ok_),
        w64_hostfn("IXAVoice::GetEffectState", xv_void_),
        w64_hostfn("IXAVoice::SetEffectParameters", xv_ok_),
        w64_hostfn("IXAVoice::GetEffectParameters", xv_ok_),
        w64_hostfn("IXAVoice::SetFilterParameters", xv_ok_),
        w64_hostfn("IXAVoice::GetFilterParameters", xv_void_),
        w64_hostfn("IXAVoice::SetOutputFilterParameters", xv_ok_),
        w64_hostfn("IXAVoice::GetOutputFilterParameters", xv_void_),
        w64_hostfn("IXAVoice::SetVolume", xv_SetVolume_),
        w64_hostfn("IXAVoice::GetVolume", xv_GetVolume_),
        w64_hostfn("IXAVoice::SetChannelVolumes", xv_SetChannelVolumes_),
        w64_hostfn("IXAVoice::GetChannelVolumes", xv_void_),
        w64_hostfn("IXAVoice::SetOutputMatrix", xv_SetOutputMatrix_),
        w64_hostfn("IXAVoice::GetOutputMatrix", xv_void_),
        w64_hostfn("IXAVoice::DestroyVoice", xv_DestroyVoice_),
    };
    memcpy(g_vt_master, base, sizeof base);
    memcpy(g_vt_submix, base, sizeof base);
    memcpy(g_vt_source, base, sizeof base);
    g_vt_source[19] = w64_hostfn("IXASource::Start", xsv_Start_);
    g_vt_source[20] = w64_hostfn("IXASource::Stop", xsv_Stop_);
    g_vt_source[21] = w64_hostfn("IXASource::SubmitSourceBuffer", xsv_SubmitSourceBuffer_);
    g_vt_source[22] = w64_hostfn("IXASource::FlushSourceBuffers", xsv_FlushSourceBuffers_);
    g_vt_source[23] = w64_hostfn("IXASource::Discontinuity", xv_ok_);
    g_vt_source[24] = w64_hostfn("IXASource::ExitLoop", xsv_ExitLoop_);
    g_vt_source[25] = w64_hostfn("IXASource::GetState", xsv_GetState_);
    g_vt_source[26] = w64_hostfn("IXASource::SetFrequencyRatio", xsv_SetFrequencyRatio_);
    g_vt_source[27] = w64_hostfn("IXASource::GetFrequencyRatio", xsv_GetFrequencyRatio_);
    g_vt_source[28] = w64_hostfn("IXASource::SetSourceSampleRate", xsv_SetSourceSampleRate_);

    for (int i = 0; i < 16; i++) g_vt_apo[i] = w64_hostfn("IXAPO::stub", xv_ok_);
    g_vt_apo[0] = w64_hostfn("IXAPO::QI", xa2_QI_);
    g_vt_apo[1] = w64_hostfn("IXAPO::AddRef", xa2_AddRef_);
    g_vt_apo[2] = w64_hostfn("IXAPO::Release", xa2_Release_);
    g_apo_obj = (uint64_t)(uintptr_t)g_vt_apo;
    g_xa2.vptr = (uint64_t)(uintptr_t)g_vt_xa2;
    g_xa2.ref = 1;
}

/* Called from CoCreateInstance_ in wasapi.c */
uint64_t hat_xaudio2_create(const uint8_t *clsid, const uint8_t *iid)
{
    /* CLSID_XAudio2 (2.7): 5a508685-a254-4fba-9b82-9a24b00306af
     * CLSID_XAudio2_Debug: db05ea35-0329-4d4b-a53a-6dead03d3852
     * CLSID_AudioReverb:   6a93130e-1d53-41d1-a9cf-e758800bb179
     * CLSID_AudioVolumeMeter: cac1105f-619b-4d04-831a-44e1cbf12d57 */
    static const uint8_t clsid_xa2[16]  = { 0x85,0x86,0x50,0x5a, 0x54,0xa2, 0xba,0x4f, 0x9b,0x82, 0x9a,0x24,0xb0,0x03,0x06,0xaf };
    static const uint8_t clsid_xa2d[16] = { 0x35,0xea,0x05,0xdb, 0x29,0x03, 0x4b,0x4d, 0xa5,0x3a, 0x6d,0xea,0xd0,0x3d,0x38,0x52 };
    static const uint8_t iid_xa2[16]    = { 0x58,0x1f,0xcf,0x8b, 0xe7,0x9f, 0x83,0x45, 0x8a,0xc6, 0xe2,0xad,0xc4,0x65,0xc8,0xbb };
    static const uint8_t clsid_rev[16]  = { 0x0e,0x13,0x93,0x6a, 0x53,0x1d, 0xd1,0x41, 0xa9,0xcf, 0xe7,0x58,0x80,0x0b,0xb1,0x79 };
    static const uint8_t clsid_vm[16]   = { 0x5f,0x10,0xc1,0xca, 0x9b,0x61, 0x04,0x4d, 0x83,0x1a, 0x44,0xe1,0xcb,0xf1,0x2d,0x57 };
    xa2_init_vtables();
    if (!memcmp(clsid, clsid_xa2, 16) || !memcmp(clsid, clsid_xa2d, 16) || (iid && !memcmp(iid, iid_xa2, 16))) {
        w64_log("[xaudio2] CoCreateInstance(CLSID_XAudio2) -> %p", (void *)&g_xa2);
        return (uint64_t)(uintptr_t)&g_xa2;
    }
    if (!memcmp(clsid, clsid_rev, 16) || !memcmp(clsid, clsid_vm, 16)) {
        return (uint64_t)(uintptr_t)&g_apo_obj;
    }
    return 0;
}

/* ---- X3DAudio1_7.dll & XAPOFX1_5.dll ---- */
static void X3DAudioInitialize_(CPU *c)
{
    uint8_t *h = AP(2);
    if (h) { memset(h, 0, 20); uint32_t m = AU(0); memcpy(h, &m, 4); }
    RETV();
}
static void X3DAudioCalculate_(CPU *c)
{
    /* (Instance, pListener, pEmitter, Flags, pDSPSettings)
     * X3DAUDIO_LISTENER: OrientFront(0..11), OrientTop(12..23), Position(24..35)
     * X3DAUDIO_EMITTER:  pCone(0), OrientFront(8..19), OrientTop(20..31), Position(32..43), ... CurveDistanceScaler(88)
     * X3DAUDIO_DSP_SETTINGS (Win64): pMatrixCoefficients(0), pDelayTimes(8), SrcChannelCount(16), DstChannelCount(20), ... DopplerFactor(36), EmitterToListenerDistance(40) */
    const uint8_t *lis = AP(1), *em = AP(2);
    uint8_t *dsp = AP(4);
    if (!dsp) RETV();
    float *mat = (float *)(uintptr_t)rd64((uint64_t)(uintptr_t)dsp + 0);
    uint32_t src = rd32((uint64_t)(uintptr_t)dsp + 16);
    uint32_t dst = rd32((uint64_t)(uintptr_t)dsp + 20);
    float dist = 1.0f, pan = 0.0f, atten = 1.0f;
    if (lis && em) {
        float lf[3], lt[3], lp[3], ep[3];
        memcpy(lf, lis + 0, 12); memcpy(lt, lis + 12, 12); memcpy(lp, lis + 24, 12);
        memcpy(ep, em + 32, 12);
        float dx = ep[0] - lp[0], dy = ep[1] - lp[1], dz = ep[2] - lp[2];
        dist = sqrtf(dx*dx + dy*dy + dz*dz);
        float rx = lt[1]*lf[2] - lt[2]*lf[1], ry = lt[2]*lf[0] - lt[0]*lf[2], rz = lt[0]*lf[1] - lt[1]*lf[0];
        if (dist > 0.001f) pan = (dx*rx + dy*ry + dz*rz) / dist;
        if (pan < -1.0f) pan = -1.0f; if (pan > 1.0f) pan = 1.0f;
        float scale = 1.0f; memcpy(&scale, em + 88, 4);
        if (scale <= 0.0f) scale = 1.0f;
        atten = scale / (scale + dist);
    }
    if (mat && src > 0 && dst > 0) {
        float gl = atten * sqrtf(0.5f * (1.0f - pan));
        float gr = atten * sqrtf(0.5f * (1.0f + pan));
        for (uint32_t d = 0; d < dst; d++)
            for (uint32_t s = 0; s < src; s++)
                mat[d * src + s] = (dst == 1) ? atten : (d == 0 ? gl : (d == 1 ? gr : 0.0f));
    }
    float dop = 1.0f;
    memcpy(dsp + 36, &dop, 4);
    memcpy(dsp + 40, &dist, 4);
    RETV();
}
static void CreateFX_(CPU *c)
{
    xa2_init_vtables();
    if (A(1)) wr64(A(1), (uint64_t)(uintptr_t)&g_apo_obj);
    RET(0);
}

static const w64_export_t xaudio2_exports[] = {
    { "X3DAudio1_7.dll", "X3DAudioInitialize", X3DAudioInitialize_ },
    { "X3DAudio1_7.dll", "X3DAudioCalculate", X3DAudioCalculate_ },
    { "XAPOFX1_5.dll", "CreateFX", CreateFX_ },
    { 0 }
};
__attribute__((constructor(102))) static void xaudio2_init(void) { w64_register(xaudio2_exports); }
