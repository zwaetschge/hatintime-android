// hat_d3d9.cpp - Direct3D 9 bridge over DXVK native 1.9.2b for A Hat in Time.
#define _GNU_SOURCE
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <time.h>

#include <d3d9.h>

extern "C" {
void w64_log(const char* fmt, ...);
void* hp_wsi_host_open(uint32_t w, uint32_t h);
void* hp_wsi_window_handle(void);
void hp_wsi_get_screen(uint32_t* w, uint32_t* h);
void hp_wsi_present_begin(void);
void hp_wsi_present_end(void);
}

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

namespace {

std::atomic<unsigned long> g_frame{0};
bool g_logged_slots = false;

template <typename Iface, typename Mem>
int vidx(Mem m) {
    struct { uintptr_t a, b; } u;
    static_assert(sizeof m == sizeof u, "Itanium ABI pointer-to-member");
    std::memcpy(&u, &m, sizeof u);
#if defined(__aarch64__) || defined(__arm__)
    return (u.b & 1) ? int(u.a / sizeof(void*)) : -1;
#else
    return (u.a & 1) ? int((u.a - 1) / sizeof(void*)) : -1;
#endif
}

struct Slot { int idx, kind; };

template <typename Iface>
int probe(void* self, uint64_t fn, const Slot* s, size_t n) {
    Iface* q = nullptr;
    if (FAILED(static_cast<IUnknown*>(self)->QueryInterface(__uuidof(Iface), reinterpret_cast<void**>(&q))) || !q)
        return 0;
    void** vt = *reinterpret_cast<void***>(q);
    int r = 0;
    for (size_t i = 0; i < n; i++)
        if (s[i].idx >= 0 && reinterpret_cast<uintptr_t>(vt[s[i].idx]) == fn) { r = s[i].kind; break; }
    q->Release();
    return r;
}

}  // namespace

extern "C" int hp_native_classify(void* self, uint64_t fn) {
    static const Slot dev[] = {
        { vidx<IDirect3DDevice9>(&IDirect3DDevice9::Reset),         NK_D3D9_RESET },
        { vidx<IDirect3DDevice9>(&IDirect3DDevice9::Present),       NK_D3D9_PRESENT },
        { vidx<IDirect3DDevice9>(&IDirect3DDevice9::Clear),         NK_D3D9_CLEAR },
        { vidx<IDirect3DDevice9>(&IDirect3DDevice9::SetNPatchMode), NK_D3D9_SET_NPATCH },
        { vidx<IDirect3DDevice9>(&IDirect3DDevice9::GetNPatchMode), NK_D3D9_GET_NPATCH },
    };
    static const Slot d3d[] = {
        { vidx<IDirect3D9>(&IDirect3D9::CreateDevice), NK_D3D9_CREATE_DEVICE },
    };
    static const Slot sc[] = {
        { vidx<IDirect3DSwapChain9>(&IDirect3DSwapChain9::Present), NK_D3D9_SC_PRESENT },
    };
    if (!g_logged_slots) {
        g_logged_slots = true;
        w64_log("[d3d9] vtable slots: Reset %d Present %d Clear %d NPatch %d/%d, CreateDevice %d, SCPresent %d",
                dev[0].idx, dev[1].idx, dev[2].idx, dev[3].idx, dev[4].idx, d3d[0].idx, sc[0].idx);
    }
    int k;
    if ((k = probe<IDirect3DDevice9>(self, fn, dev, 5))) return k;
    if ((k = probe<IDirect3D9>(self, fn, d3d, 1))) return k;
    if ((k = probe<IDirect3DSwapChain9>(self, fn, sc, 1))) return k;
    return NK_GENERIC;
}

extern "C" void* hat_d3d9_create(uint32_t sdk) {
#ifndef __ANDROID__
    if (!hp_wsi_window_handle()) {
        uint32_t w = 1280, h = 720;
        if (const char* s = getenv("W64_SCREEN")) std::sscanf(s, "%ux%u", &w, &h);
        hp_wsi_host_open(w, h);
    }
#endif
    IDirect3D9* d3d = Direct3DCreate9(sdk);
    w64_log("[d3d9] Direct3DCreate9(%u) -> %p", sdk, (void*)d3d);
    if (d3d) {
        D3DADAPTER_IDENTIFIER9 id{};
        if (SUCCEEDED(d3d->GetAdapterIdentifier(0, 0, &id)))
            w64_log("[d3d9] adapter 0: %s (vendor %#x device %#x)", id.Description, id.VendorId, id.DeviceId);
    }
    return d3d;
}

static void patch_pp(const char* tag, D3DPRESENT_PARAMETERS* pp) {
    if (!pp) return;
    uint32_t sw = 1280, sh = 720;
    hp_wsi_get_screen(&sw, &sh);
    w64_log("[d3d9] %s req: %ux%u fmt=%u cnt=%u msaa=%u swap=%u windowed=%u depth=%u/%u interval=%#x hwnd=%p",
            tag, pp->BackBufferWidth, pp->BackBufferHeight, unsigned(pp->BackBufferFormat),
            pp->BackBufferCount, unsigned(pp->MultiSampleType), unsigned(pp->SwapEffect),
            unsigned(pp->Windowed), unsigned(pp->EnableAutoDepthStencil), unsigned(pp->AutoDepthStencilFormat),
            pp->PresentationInterval, (void*)pp->hDeviceWindow);
    // Borderless windowed matches Android's ANativeWindow and Xvfb without mode switches.
    pp->Windowed = TRUE;
    pp->FullScreen_RefreshRateInHz = 0;
    if (!pp->hDeviceWindow) pp->hDeviceWindow = static_cast<HWND>(hp_wsi_window_handle());
#ifdef __ANDROID__
    if (sw && sh && (pp->BackBufferWidth == 0 || pp->BackBufferWidth > sw || pp->BackBufferHeight > sh)) {
        pp->BackBufferWidth = sw;
        pp->BackBufferHeight = sh;
    }
#endif
}

extern "C" int32_t hat_d3d9_create_device(void* self, uint64_t fn, uint32_t adapter, uint32_t devtype,
                                          void* hwnd, uint32_t flags, void* pp_raw, void** out) {
    auto* pp = static_cast<D3DPRESENT_PARAMETERS*>(pp_raw);
    patch_pp("CreateDevice", pp);
    if (!hwnd) hwnd = hp_wsi_window_handle();
    using F = HRESULT (*)(void*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
    HRESULT hr = reinterpret_cast<F>(fn)(self, adapter, D3DDEVTYPE(devtype), static_cast<HWND>(hwnd), flags, pp,
                                         reinterpret_cast<IDirect3DDevice9**>(out));
    w64_log("[d3d9] CreateDevice -> %#x dev=%p (%ux%u)", unsigned(hr), out ? *out : nullptr,
            pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0);
    return hr;
}

extern "C" int32_t hat_d3d9_reset(void* self, uint64_t fn, void* pp_raw) {
    auto* pp = static_cast<D3DPRESENT_PARAMETERS*>(pp_raw);
    patch_pp("Reset", pp);
    using F = HRESULT (*)(void*, D3DPRESENT_PARAMETERS*);
    HRESULT hr = reinterpret_cast<F>(fn)(self, pp);
    w64_log("[d3d9] Reset -> %#x (%ux%u)", unsigned(hr), pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0);
    return hr;
}

extern "C" unsigned long hp_present_count(void) { return g_frame.load(std::memory_order_relaxed); }

static std::atomic<uint32_t> g_st_gp_n{0}, g_st_cp_n{0}, g_st_sp_n{0};
static std::atomic<uint64_t> g_st_gp_us{0}, g_st_cp_us{0}, g_st_sp_us{0};
static int stutter_on() {
    static int on = -1;
    if (on < 0) on = getenv("HP_STUTTER") && atoi(getenv("HP_STUTTER")) != 0 ? 1 : 0;
    return on;
}
extern "C" void hp_stutter_add_pipe(int is_compute, uint64_t us) {
    if (!stutter_on()) return;
    if (is_compute) { g_st_cp_n.fetch_add(1, std::memory_order_relaxed); g_st_cp_us.fetch_add(us, std::memory_order_relaxed); }
    else            { g_st_gp_n.fetch_add(1, std::memory_order_relaxed); g_st_gp_us.fetch_add(us, std::memory_order_relaxed); }
}
extern "C" void hp_stutter_add_susp(uint64_t us) {
    if (!stutter_on()) return;
    g_st_sp_n.fetch_add(1, std::memory_order_relaxed);
    g_st_sp_us.fetch_add(us, std::memory_order_relaxed);
}
static uint64_t mono_us() {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000ull + (uint64_t)t.tv_nsec / 1000ull;
}

extern "C" int32_t hat_d3d9_present(int kind, void* self, uint64_t fn,
                                    const void* src, const void* dst, void* hwnd, const void* rgn, uint32_t flags) {
    int st = stutter_on();
    uint64_t pt0 = st ? mono_us() : 0;
    hp_wsi_present_begin();
    HRESULT hr;
    if (kind == NK_D3D9_SC_PRESENT) {
        using F = HRESULT (*)(void*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
        hr = reinterpret_cast<F>(fn)(self, static_cast<const RECT*>(src), static_cast<const RECT*>(dst),
                                     static_cast<HWND>(hwnd), static_cast<const RGNDATA*>(rgn), flags);
    } else {
        using F = HRESULT (*)(void*, const RECT*, const RECT*, HWND, const RGNDATA*);
        hr = reinterpret_cast<F>(fn)(self, static_cast<const RECT*>(src), static_cast<const RECT*>(dst),
                                     static_cast<HWND>(hwnd), static_cast<const RGNDATA*>(rgn));
    }
    hp_wsi_present_end();
    uint64_t pt1 = st ? mono_us() : 0;

    static unsigned long n = 0, bad = 0;
    n++;
    g_frame.store(n, std::memory_order_relaxed);
    if (hr != D3D_OK && bad++ < 20)
        w64_log("[d3d9] Present #%lu -> %#x", n, unsigned(hr));
    if (n == 1 || n == 100 || n == 1000)
        w64_log("[d3d9] %lu presents", n);

    if (st) {
        static uint64_t prev_t = 0;
        static float dts[150];
        static int dti = 0;
        static uint32_t bkt[6], w_gp_n, w_cp_n, w_sp_n;
        static uint64_t w_gp_us, w_cp_us, w_sp_us;
        uint32_t gp_n = g_st_gp_n.exchange(0, std::memory_order_relaxed);
        uint64_t gp_us = g_st_gp_us.exchange(0, std::memory_order_relaxed);
        uint32_t cp_n = g_st_cp_n.exchange(0, std::memory_order_relaxed);
        uint64_t cp_us = g_st_cp_us.exchange(0, std::memory_order_relaxed);
        uint32_t sp_n = g_st_sp_n.exchange(0, std::memory_order_relaxed);
        uint64_t sp_us = g_st_sp_us.exchange(0, std::memory_order_relaxed);
        w_gp_n += gp_n; w_gp_us += gp_us; w_cp_n += cp_n; w_cp_us += cp_us; w_sp_n += sp_n; w_sp_us += sp_us;
        if (prev_t) {
            float dt = (float)(pt1 - prev_t) / 1000.0f;
            float pr = (float)(pt1 - pt0) / 1000.0f;
            int bi = dt < 20 ? 0 : dt < 30 ? 1 : dt < 45 ? 2 : dt < 80 ? 3 : dt < 150 ? 4 : 5;
            bkt[bi]++;
            if (dt >= 45.0f) {
                static int spk = 0;
                if (spk++ < 120)
                    w64_log("[spike] f%lu dt=%.1fms pres=%.1fms | gp=%u(%.1fms) cp=%u(%.1fms) susp=%u(%.1fms)",
                            n, dt, pr, gp_n, gp_us / 1000.0, cp_n, cp_us / 1000.0, sp_n, sp_us / 1000.0);
            }
            dts[dti++] = dt;
            if (dti == 150) {
                std::sort(dts, dts + 150);
                w64_log("[stutter] 150f p50=%.1f p95=%.1f p99=%.1f max=%.1f ms | <20:%u 20-30:%u 30-45:%u 45-80:%u 80-150:%u >150:%u | gp=%u(%.0fms) cp=%u(%.0fms) susp=%u(%.0fms)",
                        dts[75], dts[142], dts[148], dts[149], bkt[0], bkt[1], bkt[2], bkt[3], bkt[4], bkt[5],
                        w_gp_n, w_gp_us / 1000.0, w_cp_n, w_cp_us / 1000.0, w_sp_n, w_sp_us / 1000.0);
                dti = 0; std::memset(bkt, 0, sizeof bkt);
                w_gp_n = w_cp_n = w_sp_n = 0; w_gp_us = w_cp_us = w_sp_us = 0;
            }
        }
        prev_t = pt1;
    }

    static struct timespec t0;
    if (n % 300 == 1) {
        struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
        if (n > 1) w64_log("[d3d9] %.1f fps", 300.0 / ((t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) / 1e9));
        t0 = t;
    }
    return hr;
}
