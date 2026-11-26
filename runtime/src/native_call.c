/*
 * native_call.c - guest calls into native DXVK D3D9 COM objects, plus d3d9 / d3dx9_43 entry points.
 */
#define _GNU_SOURCE
#include "w64.h"
#include <dlfcn.h>
#include <link.h>
#include <math.h>
#include <pthread.h>

int hp_native_classify(void *self, uint64_t fn);
void *hat_d3d9_create(uint32_t sdk);
int32_t hat_d3d9_create_device(void *self, uint64_t fn, uint32_t adapter, uint32_t devtype,
                               void *hwnd, uint32_t flags, void *pp, void **out);
int32_t hat_d3d9_reset(void *self, uint64_t fn, void *pp);
int32_t hat_d3d9_present(int kind, void *self, uint64_t fn,
                         const void *src, const void *dst, void *hwnd, const void *rgn, uint32_t flags);

enum {
    NK_UNKNOWN = 0,
    NK_GENERIC,
    NK_D3D9_CLEAR,
    NK_D3D9_SET_NPATCH,
    NK_D3D9_GET_NPATCH,
    NK_D3D9_PRESENT,
    NK_D3D9_SC_PRESENT,
    NK_D3D9_CREATE_DEVICE,
    NK_D3D9_RESET,
};

/* ---- native image range ---- */
static uint64_t g_code_lo, g_code_hi, g_img_lo, g_img_hi;
static int phdr_cb(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    uint64_t me = (uint64_t)(uintptr_t)data;
    uint64_t lo = ~0ull, hi = 0, clo = ~0ull, chi = 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *p = &info->dlpi_phdr[i];
        if (p->p_type != PT_LOAD) continue;
        uint64_t a = info->dlpi_addr + p->p_vaddr, b = a + p->p_memsz;
        if (a < lo) lo = a;
        if (b > hi) hi = b;
        if (p->p_flags & PF_X) { if (a < clo) clo = a; if (b > chi) chi = b; }
    }
    if (me >= clo && me < chi) { g_img_lo = lo; g_img_hi = hi; g_code_lo = clo; g_code_hi = chi; return 1; }
    return 0;
}
static void range_init(void)
{
    dl_iterate_phdr(phdr_cb, (void *)(uintptr_t)&range_init);
    if (!g_code_hi) w64_fatal(NULL, "native_call: own image not found");
}
int w64_native_code(uint64_t va)
{
    if (!g_code_hi) range_init();
    return va >= g_code_lo && va < g_code_hi;
}
int w64_native_image(uint64_t va) { if (!g_code_hi) range_init(); return va >= g_img_lo && va < g_img_hi; }

/* ---- method address -> kind cache ---- */
#define KBITS 14
static struct { uint64_t key; int kind; } g_kinds[1 << KBITS];
static pthread_mutex_t g_kmu = PTHREAD_MUTEX_INITIALIZER;
static int kind_of(CPU *c, uint64_t fn)
{
    uint32_t h = (uint32_t)((fn * 0x9E3779B97F4A7C15ull) >> (64 - KBITS));
    for (uint32_t k = 0; k < (1u << KBITS); k++, h = (h + 1) & ((1u << KBITS) - 1)) {
        uint64_t key = __atomic_load_n(&g_kinds[h].key, __ATOMIC_ACQUIRE);
        if (key == fn) return g_kinds[h].kind;
        if (!key) break;
    }
    int kind = NK_GENERIC;
    void *self = (void *)(uintptr_t)c->rcx;
    if (self && c->rcx > 0x10000 && w64_native_image(rd64(c->rcx))) kind = hp_native_classify(self, fn);
    if (kind <= 0) kind = NK_GENERIC;
    pthread_mutex_lock(&g_kmu);
    h = (uint32_t)((fn * 0x9E3779B97F4A7C15ull) >> (64 - KBITS));
    for (uint32_t k = 0; k < (1u << KBITS); k++, h = (h + 1) & ((1u << KBITS) - 1)) {
        if (g_kinds[h].key == fn) break;
        if (!g_kinds[h].key) {
            g_kinds[h].kind = kind;
            __atomic_store_n(&g_kinds[h].key, fn, __ATOMIC_RELEASE);
            break;
        }
    }
    pthread_mutex_unlock(&g_kmu);
    if (kind != NK_GENERIC && w64_trace) w64_log("[native] %#llx kind %d", (unsigned long long)fn, kind);
    return kind;
}

typedef uint64_t (*ncall_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

void w64_native_call(CPU *c, uint64_t fn)
{
    int kind = kind_of(c, fn);
    switch (kind) {
    case NK_D3D9_CLEAR: {
        /* HRESULT Clear(this, DWORD count, const D3DRECT* rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) */
        uint32_t zb = (uint32_t)A(5);
        float z; memcpy(&z, &zb, 4);
        int32_t hr = ((int32_t (*)(void *, uint32_t, const void *, uint32_t, uint32_t, float, uint32_t))(uintptr_t)fn)(
            AP(0), AU(1), AP(2), AU(3), AU(4), z, AU(6));
        c->rax = (uint64_t)(uint32_t)hr;
        break;
    }
    case NK_D3D9_SET_NPATCH: {
        int32_t hr = ((int32_t (*)(void *, float))(uintptr_t)fn)(AP(0), c->xmm[1].f32[0]);
        c->rax = (uint64_t)(uint32_t)hr;
        break;
    }
    case NK_D3D9_GET_NPATCH: {
        float r = ((float (*)(void *))(uintptr_t)fn)(AP(0));
        c->xmm[0].u64[0] = 0; c->xmm[0].u64[1] = 0; c->xmm[0].f32[0] = r;
        break;
    }
    case NK_D3D9_PRESENT:
    case NK_D3D9_SC_PRESENT:
        c->rax = (uint64_t)(uint32_t)hat_d3d9_present(kind, AP(0), fn, AP(1), AP(2), AP(3), AP(4),
                                                      kind == NK_D3D9_SC_PRESENT ? AU(5) : 0);
        break;
    case NK_D3D9_CREATE_DEVICE:
        c->rax = (uint64_t)(uint32_t)hat_d3d9_create_device(AP(0), fn, AU(1), AU(2), AP(3), AU(4), AP(5), (void **)AP(6));
        break;
    case NK_D3D9_RESET:
        c->rax = (uint64_t)(uint32_t)hat_d3d9_reset(AP(0), fn, AP(1));
        break;
    default:
        c->rax = ((ncall_t)(uintptr_t)fn)(A(0), A(1), A(2), A(3), A(4), A(5), A(6), A(7), A(8), A(9), A(10), A(11), A(12), A(13));
        break;
    }
    c->rsp += 8;
}

/* ---- d3d9.dll & d3dx9_43.dll ---- */
static void Direct3DCreate9_(CPU *c) { RET((uint64_t)(uintptr_t)hat_d3d9_create(AU(0))); }
static void D3DPerf_(CPU *c) { RET(0); }
static void D3DXCheckVersion_(CPU *c) { RET(1); }
static void D3DXDebugMute_(CPU *c) { RET(0); }

/* D3DXMatrixInverse(D3DXMATRIX* pOut, FLOAT* pDeterminant, const D3DXMATRIX* pM) */
static void D3DXMatrixInverse_(CPU *c)
{
    float *out = (float *)AP(0), *pdet = (float *)AP(1);
    const float *m = (const float *)AP(2);
    if (!out || !m) RET(0);
    double inv[16];
    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
    inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
    inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
    inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];
    double det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (pdet) *pdet = (float)det;
    if (det == 0.0) RET(0);
    double r = 1.0 / det;
    for (int i = 0; i < 16; i++) out[i] = (float)(inv[i] * r);
    RET(A(0));
}
static void D3DXShaderStub_(CPU *c) { w64_log("[d3dx9] shader compiler stub called -> E_FAIL"); RET(0x80004005u); }

/* DXVK hooks */
uint64_t w64_sem_create(int32_t initial, int32_t max);
void w64_sem_release(uint64_t h);
void *hp_dxvk_sem_create(uint32_t initial, uint32_t max) { return (void *)(uintptr_t)w64_sem_create((int32_t)initial, (int32_t)max); }
void hp_dxvk_sem_release(void *h) { w64_sem_release((uint64_t)(uintptr_t)h); }
void hp_dxvk_event_set(void *h) { w64_sem_release((uint64_t)(uintptr_t)h); }
void hp_dxvk_handle_close(void *h) { w64_handle_close((uint64_t)(uintptr_t)h); }
void hp_dxvk_log(int level, const char *line)
{
#ifdef __ANDROID__
    static int min = -1;
    if (min < 0) { const char *e = getenv("HP_DXVK_LOG"); min = e && *e ? atoi(e) : 3; }
    if (level >= min) w64_log("[dxvk] %s", line);
#else
    if (level >= 3 || getenv("HP_DXVK_LOG")) w64_log("[dxvk] %s", line);
#endif
}
int hp_rp_mode(void) { return 0; }
const char *hp_userdir(void) { return w64_userdir; }

static const w64_export_t d3d_exports[] = {
    { "d3d9.dll", "Direct3DCreate9", Direct3DCreate9_ },
    { "d3d9.dll", "D3DPERF_SetOptions", D3DPerf_ },
    { "d3d9.dll", "D3DPERF_BeginEvent", D3DPerf_ },
    { "d3d9.dll", "D3DPERF_EndEvent", D3DPerf_ },
    { "d3d9.dll", "D3DPERF_SetMarker", D3DPerf_ },
    { "d3d9.dll", "D3DPERF_SetRegion", D3DPerf_ },
    { "d3d9.dll", "D3DPERF_GetStatus", D3DPerf_ },
    { "d3dx9_43.dll", "D3DXCheckVersion", D3DXCheckVersion_ },
    { "d3dx9_43.dll", "D3DXDebugMute", D3DXDebugMute_ },
    { "d3dx9_43.dll", "D3DXMatrixInverse", D3DXMatrixInverse_ },
    { "d3dx9_43.dll", "D3DXPreprocessShader", D3DXShaderStub_ },
    { "d3dx9_43.dll", "D3DXCompileShader", D3DXShaderStub_ },
    { "d3dx9_43.dll", "D3DXDisassembleShader", D3DXShaderStub_ },
    { 0 }
};
__attribute__((constructor(102))) static void native_call_init(void) { w64_register(d3d_exports); }
