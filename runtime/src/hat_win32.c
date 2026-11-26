/*
 * hat_win32.c - Additional Win32 / UE3 imports needed by HatinTimeGame.exe.
 */
#define _GNU_SOURCE
#include "w64.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static void ret0_(CPU *c) { RET(0); }
static void ret1_(CPU *c) { RET(1); }
static void retv_(CPU *c) { RETV(); }
static void ret_fail_(CPU *c) { RET(0x80004005u); }

/* ---- KERNEL32 ---- */
static void lstrlenA_(CPU *c) { RET(A(0) ? strlen(AS(0)) : 0); }
static void lstrlenW_(CPU *c) { RET(A(0) ? w16len(AW(0)) : 0); }
static void SetCriticalSectionSpinCount_(CPU *c) { RET(0); }
static void SetThreadIdealProcessor_(CPU *c) { RET(0); }
static void PulseEvent_(CPU *c) { RET(1); }
static void OpenEventW_(CPU *c) { w64_set_last_error(2); RET(0); }
static void OpenProcess_(CPU *c) { RET(0); }
static void SetCurrentDirectoryW_(CPU *c)
{
    char *u = w16_to_utf8(AW(0), -1), p[1024];
    w64_host_path(u, p, sizeof p); free(u);
    int ok = chdir(p) == 0;
    if (!ok) w64_log("[file] SetCurrentDirectoryW(%s) failed", p);
    RET(ok);
}
static void MoveFileW_(CPU *c)
{
    char *a = w16_to_utf8(AW(0), -1), *b = w16_to_utf8(AW(1), -1);
    char pa[1024], pb[1024];
    w64_host_path(a, pa, sizeof pa); w64_host_path(b, pb, sizeof pb);
    free(a); free(b);
    RET(rename(pa, pb) == 0);
}
/* PE resource lookup from g_mods[0] */
static void FindResourceW_(CPU *c) { RET(0); }
static void LoadResource_(CPU *c) { RET(0); }
static void LockResource_(CPU *c) { RET(0); }
static void SizeofResource_(CPU *c) { RET(0); }
static void GetConsoleScreenBufferInfo_(CPU *c) { if (A(1)) memset(AP(1), 0, 22); RET(0); }

/* ---- USER32 ---- */
static void AdjustWindowRect_(CPU *c) { RET(1); }
static void GetClipCursor_(CPU *c) { if (A(0)) { int32_t r[4] = {0, 0, 1280, 720}; memcpy(AP(0), r, 16); } RET(1); }
static void MapWindowPoints_(CPU *c) { RET(0); }
static void SetRectEmpty_(CPU *c) { if (A(0)) memset(AP(0), 0, 16); RET(1); }
static void BeginPaint_(CPU *c) { if (A(1)) memset(AP(1), 0, 72); RET(0x12340001ull); }
static void EndPaint_(CPU *c) { RET(1); }
static void InvalidateRect_(CPU *c) { RET(1); }
static void GetCursorInfo_(CPU *c) { if (A(0)) { wr32(A(0) + 4, 1); wr64(A(0) + 8, 0x10001ull); } RET(1); }
static void LoadCursorW_(CPU *c) { RET(0x10001ull); }
static void EnumDisplayDevicesW_(CPU *c)
{
    /* DISPLAY_DEVICEW: cb 0, DeviceName[32] 4, DeviceString[128] 68, StateFlags 324 */
    if (AU(1) > 0 || !A(2)) RET(0);
    uint8_t *d = AP(2);
    uint32_t cb = rd32(A(2));
    if (cb > 4) memset(d + 4, 0, cb - 4);
    utf8_to_w16("\\\\.\\DISPLAY1", (uint16_t *)(d + 4), 32);
    utf8_to_w16("Vulkan Display", (uint16_t *)(d + 68), 128);
    uint32_t fl = 5; /* DISPLAY_DEVICE_ATTACHED_TO_DESKTOP | PRIMARY_DEVICE */
    memcpy(d + 324, &fl, 4);
    RET(1);
}
static void GetWindowThreadProcessId_(CPU *c)
{
    if (A(1)) wr32(A(1), 0x100);
    RET((uint32_t)w64_thread_id());
}
static void MessageBoxW_(CPU *c)
{
    char *msg = A(1) ? w16_to_utf8(AW(1), -1) : strdup("");
    char *cap = A(2) ? w16_to_utf8(AW(2), -1) : strdup("");
    w64_log("[user32] MessageBoxW(%s): %s", cap, msg);
    free(msg); free(cap);
    RET(1 /* IDOK */);
}
static void GetWindowLongW_(CPU *c) { RET(0x10CF0000u); }
static void SetWindowLongW_(CPU *c) { RET(0); }

/* ---- GDI32 ---- */
static void gdi_handle_(CPU *c) { RET(0x20001ull); }
static void GetTextExtentPoint32W_(CPU *c)
{
    int n = AI(2);
    if (A(3)) { wr32(A(3), (uint32_t)(n * 8)); wr32(A(3) + 4, 16); }
    RET(1);
}

/* ---- PSAPI ---- */
static void GetProcessMemoryInfo_(CPU *c)
{
    if (A(1) && AU(2) >= 72) {
        memset(AP(1), 0, AU(2));
        wr32(A(1), AU(2));
    }
    RET(1);
}
static void GetPerformanceInfo_(CPU *c)
{
    if (A(0) && AU(1) >= 104) {
        memset(AP(0), 0, AU(1));
        wr32(A(0), AU(1));
        uint64_t pages = (uint64_t)sysconf(_SC_PHYS_PAGES);
        wr64(A(0) + 32, pages);
        wr64(A(0) + 40, pages / 2);
        wr64(A(0) + 64, 4096);
    }
    RET(1);
}

/* ---- WSOCK32 (minimal POSIX bridge / offline-safe, both name & ordinal) ---- */
static int g_wsa_err = 0;
static void ws_seterr(void) { g_wsa_err = 10035; }
static void ws_startup_(CPU *c) { if (A(1)) { memset(AP(1), 0, 408); wr16(A(1), (uint16_t)AU(0)); wr16(A(1) + 2, 0x0202); } RET(0); }
static void ws_cleanup_(CPU *c) { RET(0); }
static void ws_lasterr_(CPU *c) { RET((uint32_t)g_wsa_err); }
static void ws_socket_(CPU *c) { static uint64_t next = 0x100; RET(next++); }
static void ws_closesocket_(CPU *c) { RET(0); }
static void ws_htonl_(CPU *c) { RET(htonl(AU(0))); }
static void ws_ntohl_(CPU *c) { RET(ntohl(AU(0))); }
static void ws_htons_(CPU *c) { RET(htons((uint16_t)AU(0))); }
static void ws_ntohs_(CPU *c) { RET(ntohs((uint16_t)AU(0))); }
static void ws_gethostname_(CPU *c) { if (A(0) && AI(1) > 0) snprintf(AP(0), (size_t)AI(1), "hatintime"); RET(0); }
static void ws_gethostbyname_(CPU *c) { g_wsa_err = 11001; RET(0); }
static void ws_inet_addr_(CPU *c) { struct in_addr a; RET(A(0) && inet_aton(AS(0), &a) ? (uint32_t)a.s_addr : 0xFFFFFFFFu); }
static void ws_setsockopt_(CPU *c) { RET(0); }
static void ws_getsockopt_(CPU *c) { RET(0); }
static void ws_getsockname_(CPU *c) { if (A(1) && A(2)) memset(AP(1), 0, rd32(A(2))); RET(0); }
static void ws_ioctlsocket_(CPU *c) { RET(0); }
static void ws_bind_(CPU *c) { RET(0); }
static void ws_listen_(CPU *c) { RET(0); }
static void ws_connect_(CPU *c) { ws_seterr(); RET((uint32_t)-1); }
static void ws_accept_(CPU *c) { ws_seterr(); RET((uint64_t)-1); }
static void ws_send_(CPU *c) { RET((uint32_t)AI(2)); }
static void ws_sendto_(CPU *c) { RET((uint32_t)AI(2)); }
static void ws_recv_(CPU *c) { ws_seterr(); RET((uint32_t)-1); }
static void ws_recvfrom_(CPU *c) { ws_seterr(); RET((uint32_t)-1); }
static void ws_select_(CPU *c) { RET(0); }

/* ---- OLEAUT32 ordinals (#2 = SysAllocString, #6 = SysFreeString) ---- */
static void oa_SysAllocString_(CPU *c)
{
    const uint16_t *s = AW(0);
    if (!s) RET(0);
    size_t n = w16len(s);
    uint8_t *b = malloc(4 + (n + 1) * 2);
    if (!b) RET(0);
    uint32_t blen = (uint32_t)(n * 2);
    memcpy(b, &blen, 4);
    memcpy(b + 4, s, (n + 1) * 2);
    RET((uint64_t)(uintptr_t)(b + 4));
}
static void oa_SysFreeString_(CPU *c)
{
    uint8_t *p = AP(0);
    if (p) free(p - 4);
    RETV();
}

/* ---- DINPUT8 dummy IDirectInput8W COM object (0 devices, lets UE3 proceed to XInput + Win32) ---- */
static uint64_t g_vt_di8[16];
static uint64_t g_di8_obj;
static void di8_qi_(CPU *c) { if (A(2)) wr64(A(2), A(0)); RET(0); }
static void di8_create_dev_(CPU *c) { if (A(2)) wr64(A(2), 0); RET(0x80040154u); }
static void DirectInput8Create_(CPU *c)
{
    if (!g_di8_obj) {
        for (int i = 0; i < 16; i++) g_vt_di8[i] = w64_hostfn("IDirectInput8::ok", ret0_);
        g_vt_di8[0] = w64_hostfn("IDirectInput8::QI", di8_qi_);
        g_vt_di8[1] = w64_hostfn("IDirectInput8::AddRef", ret1_);
        g_vt_di8[2] = w64_hostfn("IDirectInput8::Release", ret1_);
        g_vt_di8[3] = w64_hostfn("IDirectInput8::CreateDevice", di8_create_dev_);
        g_di8_obj = (uint64_t)(uintptr_t)g_vt_di8;
    }
    if (A(3)) wr64(A(3), (uint64_t)(uintptr_t)&g_di8_obj);
    RET(0);
}
static void GetAdaptersInfo_(CPU *c) { RET(232 /* ERROR_NO_DATA */); }
static void nvtt_errstr_(CPU *c) { RET((uint64_t)(uintptr_t)"unsupported"); }

static const w64_export_t hat_win32_exports[] = {
    /* KERNEL32 */
    { "KERNEL32.dll", "lstrlenA", lstrlenA_ },
    { "KERNEL32.dll", "lstrlenW", lstrlenW_ },
    { "KERNEL32.dll", "SetCriticalSectionSpinCount", SetCriticalSectionSpinCount_ },
    { "KERNEL32.dll", "SetThreadIdealProcessor", SetThreadIdealProcessor_ },
    { "KERNEL32.dll", "PulseEvent", PulseEvent_ },
    { "KERNEL32.dll", "OpenEventW", OpenEventW_ },
    { "KERNEL32.dll", "OpenProcess", OpenProcess_ },
    { "KERNEL32.dll", "SetCurrentDirectoryW", SetCurrentDirectoryW_ },
    { "KERNEL32.dll", "MoveFileW", MoveFileW_ },
    { "KERNEL32.dll", "FindResourceW", FindResourceW_ },
    { "KERNEL32.dll", "LoadResource", LoadResource_ },
    { "KERNEL32.dll", "LockResource", LockResource_ },
    { "KERNEL32.dll", "SizeofResource", SizeofResource_ },
    { "KERNEL32.dll", "GetConsoleWindow", ret0_ },
    { "KERNEL32.dll", "SetConsoleTitleW", ret1_ },
    { "KERNEL32.dll", "SetConsoleTextAttribute", ret1_ },
    { "KERNEL32.dll", "SetConsoleScreenBufferSize", ret1_ },
    { "KERNEL32.dll", "GetConsoleScreenBufferInfo", GetConsoleScreenBufferInfo_ },
    { "KERNEL32.dll", "AllocConsole", ret0_ },
    { "KERNEL32.dll", "FreeConsole", ret1_ },
    /* USER32 */
    { "USER32.dll", "EnumDisplayDevicesW", EnumDisplayDevicesW_ },
    { "USER32.dll", "PostThreadMessageW", ret1_ },
    { "USER32.dll", "GetCapture", ret0_ },
    { "USER32.dll", "SetActiveWindow", ret0_ },
    { "USER32.dll", "AdjustWindowRect", AdjustWindowRect_ },
    { "USER32.dll", "GetClipCursor", GetClipCursor_ },
    { "USER32.dll", "MapWindowPoints", MapWindowPoints_ },
    { "USER32.dll", "GetWindowLongW", GetWindowLongW_ },
    { "USER32.dll", "SetWindowLongW", SetWindowLongW_ },
    { "USER32.dll", "SetWindowsHookExW", ret0_ },
    { "USER32.dll", "UnhookWindowsHookEx", ret1_ },
    { "USER32.dll", "CallNextHookEx", ret0_ },
    { "USER32.dll", "GetCursorInfo", GetCursorInfo_ },
    { "USER32.dll", "LoadCursorW", LoadCursorW_ },
    { "USER32.dll", "BeginPaint", BeginPaint_ },
    { "USER32.dll", "EndPaint", EndPaint_ },
    { "USER32.dll", "InvalidateRect", InvalidateRect_ },
    { "USER32.dll", "DrawStateW", ret1_ },
    { "USER32.dll", "SetRectEmpty", SetRectEmpty_ },
    { "USER32.dll", "RegisterHotKey", ret1_ },
    { "USER32.dll", "UnregisterHotKey", ret1_ },
    { "USER32.dll", "SetWindowPlacement", ret1_ },
    { "USER32.dll", "GetDlgItem", ret0_ },
    { "USER32.dll", "SendInput", ret0_ },
    { "USER32.dll", "MessageBoxW", MessageBoxW_ },
    { "USER32.dll", "FindWindowW", ret0_ },
    { "USER32.dll", "GetTopWindow", ret0_ },
    { "USER32.dll", "GetWindowThreadProcessId", GetWindowThreadProcessId_ },
    /* GDI32 */
    { "GDI32.dll", "SetBkMode", ret1_ },
    { "GDI32.dll", "SetTextColor", ret0_ },
    { "GDI32.dll", "SetBkColor", ret0_ },
    { "GDI32.dll", "GetObjectW", ret0_ },
    { "GDI32.dll", "TextOutW", ret1_ },
    { "GDI32.dll", "CreateCompatibleBitmap", gdi_handle_ },
    { "GDI32.dll", "CreateCompatibleDC", gdi_handle_ },
    { "GDI32.dll", "CreateDCA", gdi_handle_ },
    { "GDI32.dll", "SelectObject", gdi_handle_ },
    { "GDI32.dll", "GetStockObject", gdi_handle_ },
    { "GDI32.dll", "DeleteObject", ret1_ },
    { "GDI32.dll", "DeleteDC", ret1_ },
    { "GDI32.dll", "CreateFontIndirectW", gdi_handle_ },
    { "GDI32.dll", "GetDIBits", ret0_ },
    { "GDI32.dll", "GetTextExtentPoint32W", GetTextExtentPoint32W_ },
    /* IMM32 / IPHLPAPI / PSAPI / ole32 / faultrep */
    { "IMM32.dll", "ImmCreateContext", ret0_ },
    { "IMM32.dll", "ImmDestroyContext", ret1_ },
    { "IPHLPAPI.DLL", "GetAdaptersInfo", GetAdaptersInfo_ },
    { "PSAPI.DLL", "GetProcessMemoryInfo", GetProcessMemoryInfo_ },
    { "PSAPI.DLL", "GetPerformanceInfo", GetPerformanceInfo_ },
    { "ole32.dll", "CoFreeUnusedLibraries", retv_ },
    { "faultrep.dll", "ReportFault", ret0_ },
    /* DINPUT8 / OPENGL32 / discord-rpc */
    { "DINPUT8.dll", "DirectInput8Create", DirectInput8Create_ },
    { "OPENGL32.dll", "wglMakeCurrent", ret0_ },
    { "OPENGL32.dll", "wglCreateContext", ret0_ },
    { "OPENGL32.dll", "wglGetProcAddress", ret0_ },
    { "discord-rpc.dll", "Discord_Initialize", retv_ },
    { "discord-rpc.dll", "Discord_Shutdown", retv_ },
    { "discord-rpc.dll", "Discord_UpdatePresence", retv_ },
    /* OLEAUT32 ordinals */
    { "OLEAUT32.dll", "#2", oa_SysAllocString_ },
    { "OLEAUT32.dll", "#6", oa_SysFreeString_ },
    /* WSOCK32 (names + ordinals) */
    { "WSOCK32.dll", "WSAStartup", ws_startup_ },       { "WSOCK32.dll", "#115", ws_startup_ },
    { "WSOCK32.dll", "WSACleanup", ws_cleanup_ },       { "WSOCK32.dll", "#116", ws_cleanup_ },
    { "WSOCK32.dll", "WSAGetLastError", ws_lasterr_ },  { "WSOCK32.dll", "#111", ws_lasterr_ },
    { "WSOCK32.dll", "accept", ws_accept_ },            { "WSOCK32.dll", "#1", ws_accept_ },
    { "WSOCK32.dll", "bind", ws_bind_ },                { "WSOCK32.dll", "#2", ws_bind_ },
    { "WSOCK32.dll", "closesocket", ws_closesocket_ },  { "WSOCK32.dll", "#3", ws_closesocket_ },
    { "WSOCK32.dll", "connect", ws_connect_ },          { "WSOCK32.dll", "#4", ws_connect_ },
    { "WSOCK32.dll", "getsockname", ws_getsockname_ },  { "WSOCK32.dll", "#6", ws_getsockname_ },
    { "WSOCK32.dll", "getsockopt", ws_getsockopt_ },    { "WSOCK32.dll", "#7", ws_getsockopt_ },
    { "WSOCK32.dll", "htonl", ws_htonl_ },              { "WSOCK32.dll", "#8", ws_htonl_ },
    { "WSOCK32.dll", "htons", ws_htons_ },              { "WSOCK32.dll", "#9", ws_htons_ },
    { "WSOCK32.dll", "inet_addr", ws_inet_addr_ },      { "WSOCK32.dll", "#10", ws_inet_addr_ },
    { "WSOCK32.dll", "ioctlsocket", ws_ioctlsocket_ },  { "WSOCK32.dll", "#12", ws_ioctlsocket_ },
    { "WSOCK32.dll", "listen", ws_listen_ },            { "WSOCK32.dll", "#13", ws_listen_ },
    { "WSOCK32.dll", "ntohl", ws_ntohl_ },              { "WSOCK32.dll", "#14", ws_ntohl_ },
    { "WSOCK32.dll", "ntohs", ws_ntohs_ },              { "WSOCK32.dll", "#15", ws_ntohs_ },
    { "WSOCK32.dll", "recv", ws_recv_ },                { "WSOCK32.dll", "#16", ws_recv_ },
    { "WSOCK32.dll", "recvfrom", ws_recvfrom_ },        { "WSOCK32.dll", "#17", ws_recvfrom_ },
    { "WSOCK32.dll", "select", ws_select_ },            { "WSOCK32.dll", "#18", ws_select_ },
    { "WSOCK32.dll", "send", ws_send_ },                { "WSOCK32.dll", "#19", ws_send_ },
    { "WSOCK32.dll", "sendto", ws_sendto_ },            { "WSOCK32.dll", "#20", ws_sendto_ },
    { "WSOCK32.dll", "setsockopt", ws_setsockopt_ },    { "WSOCK32.dll", "#21", ws_setsockopt_ },
    { "WSOCK32.dll", "socket", ws_socket_ },            { "WSOCK32.dll", "#23", ws_socket_ },
    { "WSOCK32.dll", "gethostbyname", ws_gethostbyname_ }, { "WSOCK32.dll", "#52", ws_gethostbyname_ },
    { "WSOCK32.dll", "gethostname", ws_gethostname_ },  { "WSOCK32.dll", "#57", ws_gethostname_ },
    /* dbghelp */
    { "dbghelp.dll", "MiniDumpWriteDump", ret0_ },
    { "dbghelp.dll", "SymLoadModule64", ret0_ },
    { "dbghelp.dll", "SymGetSymFromAddr64", ret0_ },
    { "dbghelp.dll", "SymSetSearchPath", ret1_ },
    { "dbghelp.dll", "StackWalk64", ret0_ },
    { "dbghelp.dll", "SymGetLineFromAddr64", ret0_ },
    { "dbghelp.dll", "SymGetModuleBase64", ret0_ },
    { "dbghelp.dll", "SymGetModuleInfo64", ret0_ },
    { "dbghelp.dll", "SymSetOptions", ret0_ },
    { "dbghelp.dll", "SymGetOptions", ret0_ },
    { "dbghelp.dll", "SymFunctionTableAccess64", ret0_ },
    /* nvtt_64 (editor-only texture compression) */
    { "nvtt_64.dll", "??0Compressor@nvtt@@QEAA@XZ", ret0_ },
    { "nvtt_64.dll", "??1Compressor@nvtt@@QEAA@XZ", retv_ },
    { "nvtt_64.dll", "?enableCudaAcceleration@Compressor@nvtt@@QEAAX_N@Z", retv_ },
    { "nvtt_64.dll", "?process@Compressor@nvtt@@QEBA_NAEBUInputOptions@2@AEBUCompressionOptions@2@AEBUOutputOptions@2@@Z", ret0_ },
    { "nvtt_64.dll", "?estimateSize@Compressor@nvtt@@QEBAHAEBUInputOptions@2@AEBUCompressionOptions@2@@Z", ret0_ },
    { "nvtt_64.dll", "??0InputOptions@nvtt@@QEAA@XZ", ret0_ },
    { "nvtt_64.dll", "??1InputOptions@nvtt@@QEAA@XZ", retv_ },
    { "nvtt_64.dll", "?setMipmapGeneration@InputOptions@nvtt@@QEAAX_NH@Z", retv_ },
    { "nvtt_64.dll", "?setWrapMode@InputOptions@nvtt@@QEAAXW4WrapMode@2@@Z", retv_ },
    { "nvtt_64.dll", "?setGamma@InputOptions@nvtt@@QEAAXMM@Z", retv_ },
    { "nvtt_64.dll", "?setNormalMap@InputOptions@nvtt@@QEAAX_N@Z", retv_ },
    { "nvtt_64.dll", "?setFormat@InputOptions@nvtt@@QEAAXW4InputFormat@2@@Z", retv_ },
    { "nvtt_64.dll", "?setMipmapData@InputOptions@nvtt@@QEAA_NPEBXHHHHH@Z", ret0_ },
    { "nvtt_64.dll", "?setTextureLayout@InputOptions@nvtt@@QEAAXW4TextureType@2@HHH@Z", retv_ },
    { "nvtt_64.dll", "??0CompressionOptions@nvtt@@QEAA@XZ", ret0_ },
    { "nvtt_64.dll", "??1CompressionOptions@nvtt@@QEAA@XZ", retv_ },
    { "nvtt_64.dll", "?setColorWeights@CompressionOptions@nvtt@@QEAAXMMMM@Z", retv_ },
    { "nvtt_64.dll", "?setQuality@CompressionOptions@nvtt@@QEAAXW4Quality@2@@Z", retv_ },
    { "nvtt_64.dll", "?setFormat@CompressionOptions@nvtt@@QEAAXW4Format@2@@Z", retv_ },
    { "nvtt_64.dll", "??0OutputOptions@nvtt@@QEAA@XZ", ret0_ },
    { "nvtt_64.dll", "??1OutputOptions@nvtt@@QEAA@XZ", retv_ },
    { "nvtt_64.dll", "?setOutputHeader@OutputOptions@nvtt@@QEAAX_N@Z", retv_ },
    { "nvtt_64.dll", "?setOutputHandler@OutputOptions@nvtt@@QEAAXPEAUOutputHandler@2@@Z", retv_ },
    { "nvtt_64.dll", "?setErrorHandler@OutputOptions@nvtt@@QEAAXPEAUErrorHandler@2@@Z", retv_ },
    { "nvtt_64.dll", "?errorString@nvtt@@YAPEBDW4Error@1@@Z", nvtt_errstr_ },
    { 0 }
};
__attribute__((constructor(102))) static void hat_win32_init(void) { w64_register(hat_win32_exports); }
