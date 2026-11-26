/*
 * ucrt.c - UCRT (api-ms-win-crt-*), VCRUNTIME140, MSVCP140, and tbbmalloc for HatinTimeGame.exe.
 */
#define _GNU_SOURCE
#include "w64.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <wctype.h>

int es3_eh_throw(CPU *c, uint64_t ret_pc, uint64_t obj, uint64_t throw_info);
void w64_shutdown(int code);

/* ---- tbbmalloc.dll ---- */
#define TBB_HDR 16
static void scalable_malloc_(CPU *c)
{
    size_t n = (size_t)A(0);
    if (!n) n = 1;
    uint8_t *p = malloc(n + TBB_HDR);
    if (!p) RET(0);
    memcpy(p, &n, sizeof n);
    memset(p + 8, 0, 8);
    RET((uint64_t)(uintptr_t)(p + TBB_HDR));
}
static void scalable_free_(CPU *c)
{
    uint8_t *p = AP(0);
    if (p) free(p - TBB_HDR);
    RETV();
}
static void scalable_realloc_(CPU *c)
{
    uint8_t *p = AP(0);
    size_t n = (size_t)A(1);
    if (!p) { c->rcx = A(1); scalable_malloc_(c); return; }
    if (!n) { free(p - TBB_HDR); RET(0); }
    uint8_t *np = realloc(p - TBB_HDR, n + TBB_HDR);
    if (!np) RET(0);
    memcpy(np, &n, sizeof n);
    RET((uint64_t)(uintptr_t)(np + TBB_HDR));
}
static void scalable_msize_(CPU *c)
{
    uint8_t *p = AP(0);
    if (!p) RET(0);
    size_t n = 0; memcpy(&n, p - TBB_HDR, sizeof n);
    RET(n);
}

/* ---- VCRUNTIME140 / MSVCP140 ---- */
static void CxxThrowException_(CPU *c)
{
    uint64_t ret = rd64(c->rsp);
    uint64_t obj = A(0), ti = A(1);
    c->rsp += 8;
    if (es3_eh_throw(c, ret, obj, ti)) return;
    c->rsp -= 8;
    w64_fatal(c, "uncaught C++ exception (obj %#llx ti %#llx from %#llx)",
              (unsigned long long)obj, (unsigned long long)ti, (unsigned long long)ret);
}
static void CxxFrameHandler3_(CPU *c) { RET(1 /* ExceptionContinueSearch */); }
static void C_specific_handler_(CPU *c) { RET(1); }
static void purecall_(CPU *c) { w64_fatal(c, "_purecall invoked"); }
static void std_terminate_(CPU *c) { w64_fatal(c, "std::terminate invoked"); }
static void set_purecall_handler_(CPU *c) { RET(0); }
static void Xbad_alloc_(CPU *c) { w64_fatal(c, "std::_Xbad_alloc"); }
static void Xlength_error_(CPU *c) { w64_fatal(c, "std::_Xlength_error(%s)", A(0) ? AS(0) : "?"); }

static void memcpy_(CPU *c) { if (A(0) && A(1) && A(2)) memcpy(AP(0), AP(1), (size_t)A(2)); RET(A(0)); }
static void memmove_(CPU *c) { if (A(0) && A(1) && A(2)) memmove(AP(0), AP(1), (size_t)A(2)); RET(A(0)); }
static void memset_(CPU *c) { if (A(0) && A(2)) memset(AP(0), AI(1) & 0xFF, (size_t)A(2)); RET(A(0)); }
static void memcmp_(CPU *c) { RET((uint32_t)(A(2) ? memcmp(AP(0), AP(1), (size_t)A(2)) : 0)); }

static void wcschr_(CPU *c)
{
    const uint16_t *s = AW(0); uint16_t ch = (uint16_t)AU(1);
    if (!s) RET(0);
    for (; ; s++) { if (*s == ch) RET((uint64_t)(uintptr_t)s); if (!*s) break; }
    RET(0);
}
static void wcsrchr_(CPU *c)
{
    const uint16_t *s = AW(0), *last = NULL; uint16_t ch = (uint16_t)AU(1);
    if (!s) RET(0);
    for (; ; s++) { if (*s == ch) last = s; if (!*s) break; }
    RET((uint64_t)(uintptr_t)last);
}
static void wcsstr_(CPU *c)
{
    const uint16_t *h = AW(0), *n = AW(1);
    if (!h || !n) RET(0);
    if (!*n) RET((uint64_t)(uintptr_t)h);
    size_t nl = w16len(n);
    for (; *h; h++) if (*h == *n && !memcmp(h, n, nl * 2)) RET((uint64_t)(uintptr_t)h);
    RET(0);
}

/* Guest setjmp/longjmp: store Win64 non-volatile CPU regs in jmp_buf (256 bytes). */
static void intrinsic_setjmp_(CPU *c)
{
    uint64_t jb = A(0);
    if (jb) {
        wr64(jb + 0x00, c->rbp); wr64(jb + 0x08, c->rbx); wr64(jb + 0x10, c->rsp + 8);
        wr64(jb + 0x18, c->rbp); wr64(jb + 0x20, c->rsi); wr64(jb + 0x28, c->rdi);
        wr64(jb + 0x30, c->r12); wr64(jb + 0x38, c->r13); wr64(jb + 0x40, c->r14);
        wr64(jb + 0x48, c->r15); wr64(jb + 0x50, rd64(c->rsp));
        for (int i = 0; i < 10; i++) memcpy((uint8_t *)(uintptr_t)jb + 0x60 + i * 16, &c->xmm[6 + i], 16);
    }
    RET(0);
}
static void longjmp_(CPU *c)
{
    uint64_t jb = A(0);
    int32_t val = AI(1);
    if (!val) val = 1;
    w64_fatal(c, "longjmp(%#llx, %d) not supported across C stack frames", (unsigned long long)jb, val);
}

/* ---- UCRT heap ---- */
static void ucrt_malloc_(CPU *c) { size_t n = (size_t)A(0); RET((uint64_t)(uintptr_t)malloc(n ? n : 1)); }
static void ucrt_free_(CPU *c) { free(AP(0)); RETV(); }
static void set_new_mode_(CPU *c) { RET(0); }

/* ---- UCRT string ---- */
static void ucrt_strdup_(CPU *c) { RET(A(0) ? (uint64_t)(uintptr_t)strdup(AS(0)) : 0); }
static void ucrt_strcmp_(CPU *c) { RET((uint32_t)strcmp(AS(0), AS(1))); }
static void ucrt_stricmp_(CPU *c) { RET((uint32_t)strcasecmp(AS(0), AS(1))); }
static void ucrt_tolower_(CPU *c) { RET((uint32_t)tolower(AI(0) & 0xFF)); }
static void ucrt_iswpunct_(CPU *c) { RET((uint32_t)iswpunct((wint_t)AU(0))); }
static uint16_t wlow(uint16_t x) { return (x >= 'A' && x <= 'Z') ? (uint16_t)(x + 32) : x; }
static void ucrt_wcsicmp_(CPU *c)
{
    const uint16_t *a = AW(0), *b = AW(1);
    while (*a && *b) { int d = (int)wlow(*a) - (int)wlow(*b); if (d) RET((uint32_t)d); a++; b++; }
    RET((uint32_t)((int)wlow(*a) - (int)wlow(*b)));
}
static void ucrt_wcsnicmp_(CPU *c)
{
    const uint16_t *a = AW(0), *b = AW(1); size_t n = (size_t)A(2);
    for (size_t i = 0; i < n; i++) {
        int d = (int)wlow(a[i]) - (int)wlow(b[i]);
        if (d || !a[i] || !b[i]) RET((uint32_t)d);
    }
    RET(0);
}
static void ucrt_wcsncmp_(CPU *c)
{
    const uint16_t *a = AW(0), *b = AW(1); size_t n = (size_t)A(2);
    for (size_t i = 0; i < n; i++) {
        int d = (int)a[i] - (int)b[i];
        if (d || !a[i] || !b[i]) RET((uint32_t)d);
    }
    RET(0);
}
static void ucrt_strcpy_s_(CPU *c)
{
    char *dst = AP(0); size_t cap = (size_t)A(1); const char *src = AS(2);
    if (!dst || !cap || !src) RET(22);
    size_t n = strlen(src);
    if (n >= cap) { dst[0] = 0; RET(34); }
    memcpy(dst, src, n + 1); RET(0);
}
static void ucrt_strcat_s_(CPU *c)
{
    char *dst = AP(0); size_t cap = (size_t)A(1); const char *src = AS(2);
    if (!dst || !cap || !src) RET(22);
    size_t dl = strlen(dst), sl = strlen(src);
    if (dl + sl >= cap) { dst[0] = 0; RET(34); }
    memcpy(dst + dl, src, sl + 1); RET(0);
}
static void ucrt_strncpy_s_(CPU *c)
{
    char *dst = AP(0); size_t cap = (size_t)A(1); const char *src = AS(2); size_t cnt = (size_t)A(3);
    if (!dst || !cap || !src) RET(22);
    size_t n = (cnt == (size_t)-1) ? strlen(src) : strnlen(src, cnt);
    if (n >= cap) { n = cap - 1; }
    memcpy(dst, src, n); dst[n] = 0; RET(0);
}
static void ucrt_wcscpy_s_(CPU *c)
{
    uint16_t *dst = AP(0); size_t cap = (size_t)A(1); const uint16_t *src = AW(2);
    if (!dst || !cap || !src) RET(22);
    size_t n = w16len(src);
    if (n >= cap) { dst[0] = 0; RET(34); }
    memcpy(dst, src, (n + 1) * 2); RET(0);
}
static void ucrt_wcscat_s_(CPU *c)
{
    uint16_t *dst = AP(0); size_t cap = (size_t)A(1); const uint16_t *src = AW(2);
    if (!dst || !cap || !src) RET(22);
    size_t dl = w16len(dst), sl = w16len(src);
    if (dl + sl >= cap) { dst[0] = 0; RET(34); }
    memcpy(dst + dl, src, (sl + 1) * 2); RET(0);
}
static void ucrt_wcsncpy_s_(CPU *c)
{
    uint16_t *dst = AP(0); size_t cap = (size_t)A(1); const uint16_t *src = AW(2); size_t cnt = (size_t)A(3);
    if (!dst || !cap || !src) RET(22);
    size_t n = 0;
    while (src[n] && (cnt == (size_t)-1 || n < cnt)) n++;
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n * 2); dst[n] = 0; RET(0);
}

/* ---- UCRT convert ---- */
static void ucrt_wtoi_(CPU *c) { char *u = w16_to_utf8(AW(0), -1); int r = atoi(u); free(u); RET((uint32_t)r); }
static void ucrt_wtoi64_(CPU *c) { char *u = w16_to_utf8(AW(0), -1); long long r = atoll(u); free(u); RET((uint64_t)r); }
static void ucrt_wtof_(CPU *c) { char *u = w16_to_utf8(AW(0), -1); double r = atof(u); free(u); RETD(r); }
static void ucrt_wcstod_(CPU *c)
{
    const uint16_t *ws = AW(0);
    char *u = w16_to_utf8(ws, -1), *end = NULL;
    double r = strtod(u, &end);
    if (A(1)) wr64(A(1), (uint64_t)(uintptr_t)(ws + (end - u)));
    free(u); RETD(r);
}
static void ucrt_wcstoul_(CPU *c)
{
    const uint16_t *ws = AW(0);
    char *u = w16_to_utf8(ws, -1), *end = NULL;
    uint32_t r = (uint32_t)strtoul(u, &end, AI(2));
    if (A(1)) wr64(A(1), (uint64_t)(uintptr_t)(ws + (end - u)));
    free(u); RET(r);
}
static void ucrt_wcstoui64_(CPU *c)
{
    const uint16_t *ws = AW(0);
    char *u = w16_to_utf8(ws, -1), *end = NULL;
    uint64_t r = (uint64_t)strtoull(u, &end, AI(2));
    if (A(1)) wr64(A(1), (uint64_t)(uintptr_t)(ws + (end - u)));
    free(u); RET(r);
}

/* ---- UCRT utility (qsort with Win64 guest comparator!) ---- */
static uint32_t g_rand_seed = 1;
static void ucrt_srand_(CPU *c) { g_rand_seed = AU(0); RETV(); }
static void ucrt_rand_(CPU *c) { g_rand_seed = g_rand_seed * 214013u + 2531011u; RET((g_rand_seed >> 16) & 0x7FFFu); }
static __thread struct { CPU *c; uint64_t cmp; } t_qsort;
static int qsort_cb(const void *a, const void *b)
{
    return (int)(int32_t)w64_call(t_qsort.c, t_qsort.cmp, 2, (uint64_t)(uintptr_t)a, (uint64_t)(uintptr_t)b);
}
static void ucrt_qsort_(CPU *c)
{
    if (A(0) && A(1) > 1 && A(2) && A(3)) {
        t_qsort.c = c; t_qsort.cmp = A(3);
        qsort(AP(0), (size_t)A(1), (size_t)A(2), qsort_cb);
    }
    RETV();
}

/* ---- UCRT math ---- */
static void m_sin_(CPU *c) { RETD(sin(AD(0))); }
static void m_cos_(CPU *c) { RETD(cos(AD(0))); }
static void m_tan_(CPU *c) { RETD(tan(AD(0))); }
static void m_atan2_(CPU *c) { RETD(atan2(AD(0), AD(1))); }
static void m_pow_(CPU *c) { RETD(pow(AD(0), AD(1))); }
static void m_floor_(CPU *c) { RETD(floor(AD(0))); }
static void m_fmod_(CPU *c) { RETD(fmod(AD(0), AD(1))); }
static void m_log10_(CPU *c) { RETD(log10(AD(0))); }
static void m_ldexp_(CPU *c) { RETD(ldexp(AD(0), AI(1))); }
static void m_frexp_(CPU *c) { int e = 0; double r = frexp(AD(0), &e); if (A(1)) wr32(A(1), (uint32_t)e); RETD(r); }
static void m_sinf_(CPU *c) { RETF(sinf(AF(0))); }
static void m_cosf_(CPU *c) { RETF(cosf(AF(0))); }
static void m_tanf_(CPU *c) { RETF(tanf(AF(0))); }
static void m_asinf_(CPU *c) { RETF(asinf(AF(0))); }
static void m_acosf_(CPU *c) { RETF(acosf(AF(0))); }
static void m_atanf_(CPU *c) { RETF(atanf(AF(0))); }
static void m_atan2f_(CPU *c) { RETF(atan2f(AF(0), AF(1))); }
static void m_expf_(CPU *c) { RETF(expf(AF(0))); }
static void m_logf_(CPU *c) { RETF(logf(AF(0))); }
static void m_log10f_(CPU *c) { RETF(log10f(AF(0))); }
static void m_powf_(CPU *c) { RETF(powf(AF(0), AF(1))); }
static void m_floorf_(CPU *c) { RETF(floorf(AF(0))); }
static void m_fmodf_(CPU *c) { RETF(fmodf(AF(0), AF(1))); }
static void m_finite_(CPU *c) { RET(isfinite(AD(0)) ? 1 : 0); }
static void m_isnan_(CPU *c) { RET(isnan(AD(0)) ? 1 : 0); }
static void m_setusermatherr_(CPU *c) { RETV(); }

/* ---- UCRT filesystem ---- */
static void ucrt_wmkdir_(CPU *c)
{
    char *u = w16_to_utf8(AW(0), -1), p[1024];
    w64_host_path(u, p, sizeof p); free(u);
    int r = mkdir(p, 0777);
    RET((uint32_t)(r == 0 || errno == EEXIST ? 0 : -1));
}
static void ucrt_wstat64i32_(CPU *c)
{
    char *u = w16_to_utf8(AW(0), -1), p[1024];
    w64_host_path(u, p, sizeof p); free(u);
    struct stat st;
    if (stat(p, &st) != 0) RET((uint32_t)-1);
    uint8_t *b = AP(1);
    if (b) {
        memset(b, 0, 48);
        uint16_t mode = S_ISDIR(st.st_mode) ? 0x41FF : 0x81FF;
        memcpy(b + 6, &mode, 2);
        int32_t sz = (int32_t)st.st_size; memcpy(b + 20, &sz, 4);
        int64_t at = st.st_atime, mt = st.st_mtime, ct = st.st_ctime;
        memcpy(b + 24, &at, 8); memcpy(b + 32, &mt, 8); memcpy(b + 40, &ct, 8);
    }
    RET(0);
}

/* ---- UCRT time ---- */
static void ucrt_time64_(CPU *c) { int64_t t = (int64_t)time(NULL); if (A(0)) wr64(A(0), (uint64_t)t); RET((uint64_t)t); }
static void ucrt_gmtime64_s_(CPU *c)
{
    if (!A(0) || !A(1)) RET(22);
    time_t t = (time_t)(int64_t)rd64(A(1));
    struct tm tm; gmtime_r(&t, &tm);
    int32_t *o = AP(0);
    o[0]=tm.tm_sec; o[1]=tm.tm_min; o[2]=tm.tm_hour; o[3]=tm.tm_mday;
    o[4]=tm.tm_mon; o[5]=tm.tm_year; o[6]=tm.tm_wday; o[7]=tm.tm_yday; o[8]=tm.tm_isdst;
    RET(0);
}
static void ucrt_wstrdate_s_(CPU *c)
{
    time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
    char b[32]; snprintf(b, sizeof b, "%02d/%02d/%02d", tm.tm_mon + 1, tm.tm_mday, tm.tm_year % 100);
    utf8_to_w16(b, (uint16_t *)AP(0), (int)A(1));
    RET(0);
}
static void ucrt_wstrtime_s_(CPU *c)
{
    time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm);
    char b[32]; snprintf(b, sizeof b, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    utf8_to_w16(b, (uint16_t *)AP(0), (int)A(1));
    RET(0);
}
static void ucrt_wutime64_(CPU *c)
{
    char *u = w16_to_utf8(AW(0), -1), p[1024];
    w64_host_path(u, p, sizeof p); free(u);
    if (!A(1)) { RET((uint32_t)utime(p, NULL)); }
    struct utimbuf ub;
    ub.actime = (time_t)(int64_t)rd64(A(1));
    ub.modtime = (time_t)(int64_t)rd64(A(1) + 8);
    RET((uint32_t)utime(p, &ub));
}

/* ---- UCRT runtime / startup ---- */
static char g_cmdline_narrow[2048] = "HatinTimeGame.exe";
static void ucrt_initterm_(CPU *c)
{
    uint64_t a = A(0), b = A(1);
    for (uint64_t p = a; p < b; p += 8) {
        uint64_t fn = rd64(p);
        if (fn) w64_call(c, fn, 0);
    }
    RETV();
}
static void ucrt_initterm_e_(CPU *c)
{
    uint64_t a = A(0), b = A(1);
    for (uint64_t p = a; p < b; p += 8) {
        uint64_t fn = rd64(p);
        if (fn) { int r = (int)(int32_t)w64_call(c, fn, 0); if (r) RET((uint32_t)r); }
    }
    RET(0);
}
static void ucrt_initialize_onexit_table_(CPU *c)
{
    if (A(0)) memset(AP(0), 0, 24);
    RET(0);
}
static void ucrt_register_onexit_(CPU *c) { RET(0); }
static void ucrt_crt_atexit_(CPU *c) { RET(0); }
static void ucrt_noop0_(CPU *c) { RET(0); }
static void ucrt_noopv_(CPU *c) { RETV(); }
static void ucrt_get_narrow_winmain_cmdline_(CPU *c)
{
    const char *e = getenv("W64_CMDLINE");
    if (e && *e) snprintf(g_cmdline_narrow, sizeof g_cmdline_narrow, "%s", e);
    RET((uint64_t)(uintptr_t)g_cmdline_narrow);
}
static void ucrt_exit_(CPU *c) { w64_shutdown(AI(0)); }
static void ucrt_abort_(CPU *c) { w64_fatal(c, "abort() called by guest"); }
static void ucrt_invalid_param_(CPU *c) { w64_fatal(c, "_invalid_parameter_noinfo_noreturn"); }
static void ucrt_configthreadlocale_(CPU *c) { RET(0); }
static void ucrt_seh_filter_exe_(CPU *c) { RET(0); }

/* ---- UCRT stdio ---- */
static uint64_t g_iob[3];
static int g_commode = 0;
static FILE *ucrt_fp(uint64_t a)
{
    if (a == (uint64_t)(uintptr_t)&g_iob[0]) return stdin;
    if (a == (uint64_t)(uintptr_t)&g_iob[1]) return stdout;
    if (a == (uint64_t)(uintptr_t)&g_iob[2]) return stderr;
    return (FILE *)(uintptr_t)a;
}
static void ucrt_acrt_iob_func_(CPU *c)
{
    uint32_t i = AU(0);
    if (i > 2) i = 1;
    RET((uint64_t)(uintptr_t)&g_iob[i]);
}
static void ucrt_p_commode_(CPU *c) { RET((uint64_t)(uintptr_t)&g_commode); }
static void ucrt_set_fmode_(CPU *c) { RET(0); }
static void ucrt_fopen_s_(CPU *c)
{
    uint64_t out = A(0);
    const char *fn = AS(1), *mode = AS(2);
    if (!out || !fn || !mode) RET(22);
    char p[1024]; w64_host_path(fn, p, sizeof p);
    char hm[16]; int j = 0;
    for (int i = 0; mode[i] && j < 14; i++) if (mode[i] != 't' && mode[i] != 'S' && mode[i] != 'R') hm[j++] = mode[i];
    hm[j] = 0;
    FILE *f = fopen(p, hm[0] ? hm : "rb");
    wr64(out, (uint64_t)(uintptr_t)f);
    RET(f ? 0 : (uint32_t)errno);
}
static void ucrt_fclose_(CPU *c) { FILE *f = ucrt_fp(A(0)); RET((uint32_t)(f && f != stdin && f != stdout && f != stderr ? fclose(f) : 0)); }
static void ucrt_fflush_(CPU *c) { FILE *f = A(0) ? ucrt_fp(A(0)) : NULL; RET((uint32_t)fflush(f)); }
static void ucrt_fread_(CPU *c) { FILE *f = ucrt_fp(A(3)); RET(f ? fread(AP(0), (size_t)A(1), (size_t)A(2), f) : 0); }
static void ucrt_fwrite_(CPU *c) { FILE *f = ucrt_fp(A(3)); RET(f ? fwrite(AP(0), (size_t)A(1), (size_t)A(2), f) : 0); }
static void ucrt_fseeki64_(CPU *c) { FILE *f = ucrt_fp(A(0)); RET((uint32_t)(f ? fseeko(f, (off_t)(int64_t)A(1), AI(2)) : -1)); }
static void ucrt_ftell_(CPU *c) { FILE *f = ucrt_fp(A(0)); RET((uint64_t)(f ? ftello(f) : -1)); }
static void ucrt_fgets_(CPU *c) { FILE *f = ucrt_fp(A(2)); RET((uint64_t)(uintptr_t)(f ? fgets(AP(0), AI(1), f) : NULL)); }
static void ucrt_fputs_(CPU *c) { FILE *f = ucrt_fp(A(1)); RET((uint32_t)(f ? fputs(AS(0), f) : -1)); }
static void ucrt_fputc_(CPU *c) { FILE *f = ucrt_fp(A(1)); RET((uint32_t)(f ? fputc(AI(0), f) : -1)); }

/* UCRT __stdio_common_vsprintf(opts, buf, cap, fmt, locale, va) */
static void ucrt_vsprintf_(CPU *c)
{
    char *buf = AP(1); size_t cap = (size_t)A(2); const char *fmt = AS(3);
    w64_va v = { NULL, 0, A(5) };
    int n = w64_vformat(buf, buf ? cap : 0, fmt, &v);
    RET((uint32_t)n);
}
/* UCRT __stdio_common_vsnprintf_s(opts, buf, cap, maxcount, fmt, locale, va) */
static void ucrt_vsnprintf_s_(CPU *c)
{
    char *buf = AP(1); size_t cap = (size_t)A(2), mc = (size_t)A(3); const char *fmt = AS(4);
    size_t lim = cap;
    if (mc != (size_t)-1 && mc + 1 < lim) lim = mc + 1;
    w64_va v = { NULL, 0, A(6) };
    int n = w64_vformat(buf, buf ? lim : 0, fmt, &v);
    RET((uint32_t)n);
}
/* Wide formatting: convert fmt UTF-16 -> UTF-8 with %s <-> %ls swap (in wprintf, %s is wide and %S/%hs is narrow!). */
static void fmt_w16_to_utf8(const uint16_t *wfmt, char *out, size_t cap)
{
    char *u = w16_to_utf8(wfmt, -1);
    size_t o = 0;
    for (const char *p = u; *p && o + 4 < cap; ) {
        if (*p != '%') { out[o++] = *p++; continue; }
        out[o++] = *p++;
        if (*p == '%') { out[o++] = *p++; continue; }
        while (*p && strchr("-+ #0'0123456789.*", *p) && o + 4 < cap) out[o++] = *p++;
        int is_h = 0, is_l = 0;
        while (*p && o + 4 < cap) {
            if (*p == 'h') { is_h = 1; out[o++] = *p++; }
            else if (*p == 'l') { is_l = 1; out[o++] = *p++; }
            else if (*p == 'I' || *p == 'z' || *p == 'j' || *p == 't' || *p == 'w') { out[o++] = *p++; }
            else break;
        }
        if (*p == 's') {
            if (is_h) out[o++] = 's';
            else if (is_l) out[o++] = 's';
            else { out[o++] = 'l'; out[o++] = 's'; }
            p++;
        } else if (*p == 'S') {
            out[o++] = 's'; p++;
        } else if (*p) {
            out[o++] = *p++;
        }
    }
    out[o] = 0;
    free(u);
}
static void ucrt_vswprintf_(CPU *c)
{
    uint16_t *buf = AP(1); size_t cap = (size_t)A(2); const uint16_t *wfmt = AW(3);
    char fmt[2048]; fmt_w16_to_utf8(wfmt, fmt, sizeof fmt);
    char tmp[8192];
    w64_va v = { NULL, 0, A(5) };
    int n = w64_vformat(tmp, sizeof tmp, fmt, &v);
    if (!buf || !cap) {
        int wn = 0;
        for (int i = 0; tmp[i]; i++) if ((tmp[i] & 0xC0) != 0x80) wn++;
        RET((uint32_t)wn);
    }
    int wrote = utf8_to_w16(tmp, buf, (int)cap);
    RET((uint32_t)(wrote > 0 ? wrote - 1 : 0));
}
static void ucrt_vsnwprintf_s_(CPU *c)
{
    uint16_t *buf = AP(1); size_t cap = (size_t)A(2), mc = (size_t)A(3); const uint16_t *wfmt = AW(4);
    size_t lim = cap;
    if (mc != (size_t)-1 && mc + 1 < lim) lim = mc + 1;
    char fmt[2048]; fmt_w16_to_utf8(wfmt, fmt, sizeof fmt);
    char tmp[8192];
    w64_va v = { NULL, 0, A(6) };
    w64_vformat(tmp, sizeof tmp, fmt, &v);
    if (!buf || !lim) RET(0);
    int wrote = utf8_to_w16(tmp, buf, (int)lim);
    RET((uint32_t)(wrote > 0 ? wrote - 1 : 0));
}
static void ucrt_vfprintf_(CPU *c)
{
    FILE *f = ucrt_fp(A(1)); const char *fmt = AS(2);
    char tmp[8192];
    w64_va v = { NULL, 0, A(4) };
    int n = w64_vformat(tmp, sizeof tmp, fmt, &v);
    if (f) fwrite(tmp, 1, strlen(tmp), f);
    RET((uint32_t)n);
}
static void ucrt_vfwprintf_(CPU *c)
{
    FILE *f = ucrt_fp(A(1)); const uint16_t *wfmt = AW(2);
    char fmt[2048]; fmt_w16_to_utf8(wfmt, fmt, sizeof fmt);
    char tmp[8192];
    w64_va v = { NULL, 0, A(4) };
    int n = w64_vformat(tmp, sizeof tmp, fmt, &v);
    if (f) fwrite(tmp, 1, strlen(tmp), f);
    RET((uint32_t)n);
}
static void ucrt_vswscanf_(CPU *c)
{
    /* Minimal vswscanf: handle common %d, %u, %x, %f, %lf, %s patterns */
    char *src = w16_to_utf8(AW(1), -1);
    char *fmt = w16_to_utf8(AW(3), -1);
    uint64_t va = A(5);
    const char *s = src, *f = fmt;
    int matched = 0;
    while (*f) {
        if (isspace((unsigned char)*f)) { while (isspace((unsigned char)*s)) s++; f++; continue; }
        if (*f != '%') { if (*s != *f) break; s++; f++; continue; }
        f++;
        int is_l = 0, is_ll = 0;
        if (*f == 'l') { is_l = 1; f++; if (*f == 'l') { is_ll = 1; f++; } }
        uint64_t dst = rd64(va); va += 8;
        char *end = NULL;
        while (isspace((unsigned char)*s)) s++;
        if (*f == 'd' || *f == 'i') {
            long long v = strtoll(s, &end, *f == 'i' ? 0 : 10);
            if (end == s) break;
            if (is_ll) wr64(dst, (uint64_t)v); else wr32(dst, (uint32_t)(int32_t)v);
            s = end; matched++;
        } else if (*f == 'u' || *f == 'x' || *f == 'X') {
            unsigned long long v = strtoull(s, &end, (*f == 'u') ? 10 : 16);
            if (end == s) break;
            if (is_ll) wr64(dst, v); else wr32(dst, (uint32_t)v);
            s = end; matched++;
        } else if (*f == 'f' || *f == 'g' || *f == 'e') {
            double v = strtod(s, &end);
            if (end == s) break;
            if (is_l) { memcpy((void *)(uintptr_t)dst, &v, 8); }
            else { float fv = (float)v; memcpy((void *)(uintptr_t)dst, &fv, 4); }
            s = end; matched++;
        } else if (*f == 's') {
            const char *st = s; while (*s && !isspace((unsigned char)*s)) s++;
            if (s == st) break;
            char tmp[512]; size_t l = (size_t)(s - st); if (l >= sizeof tmp) l = sizeof tmp - 1;
            memcpy(tmp, st, l); tmp[l] = 0;
            utf8_to_w16(tmp, (uint16_t *)(uintptr_t)dst, 512);
            matched++;
        } else break;
        if (*f) f++;
    }
    free(src); free(fmt);
    RET((uint32_t)matched);
}

static const w64_export_t ucrt_exports[] = {
    /* tbbmalloc */
    { "tbbmalloc.dll", "scalable_malloc", scalable_malloc_ },
    { "tbbmalloc.dll", "scalable_free", scalable_free_ },
    { "tbbmalloc.dll", "scalable_realloc", scalable_realloc_ },
    { "tbbmalloc.dll", "scalable_msize", scalable_msize_ },
    /* VCRUNTIME140 */
    { "VCRUNTIME140.dll", "_CxxThrowException", CxxThrowException_ },
    { "VCRUNTIME140.dll", "__CxxFrameHandler3", CxxFrameHandler3_ },
    { "VCRUNTIME140.dll", "__C_specific_handler", C_specific_handler_ },
    { "VCRUNTIME140.dll", "_purecall", purecall_ },
    { "VCRUNTIME140.dll", "__std_terminate", std_terminate_ },
    { "VCRUNTIME140.dll", "_set_purecall_handler", set_purecall_handler_ },
    { "VCRUNTIME140.dll", "memcpy", memcpy_ },
    { "VCRUNTIME140.dll", "memmove", memmove_ },
    { "VCRUNTIME140.dll", "memset", memset_ },
    { "VCRUNTIME140.dll", "memcmp", memcmp_ },
    { "VCRUNTIME140.dll", "wcschr", wcschr_ },
    { "VCRUNTIME140.dll", "wcsrchr", wcsrchr_ },
    { "VCRUNTIME140.dll", "wcsstr", wcsstr_ },
    { "VCRUNTIME140.dll", "__intrinsic_setjmp", intrinsic_setjmp_ },
    { "VCRUNTIME140.dll", "longjmp", longjmp_ },
    /* MSVCP140 */
    { "MSVCP140.dll", "?_Xbad_alloc@std@@YAXXZ", Xbad_alloc_ },
    { "MSVCP140.dll", "?_Xlength_error@std@@YAXPEBD@Z", Xlength_error_ },
    /* heap */
    { "api-ms-win-crt-heap-l1-1-0.dll", "malloc", ucrt_malloc_ },
    { "api-ms-win-crt-heap-l1-1-0.dll", "free", ucrt_free_ },
    { "api-ms-win-crt-heap-l1-1-0.dll", "_set_new_mode", set_new_mode_ },
    /* string */
    { "api-ms-win-crt-string-l1-1-0.dll", "_strdup", ucrt_strdup_ },
    { "api-ms-win-crt-string-l1-1-0.dll", "strcmp", ucrt_strcmp_ },
    { "api-ms-win-crt-string-l1-1-0.dll", "_stricmp", ucrt_stricmp_ },
    { "api-ms-win-crt-string-l1-1-0.dll", "tolower", ucrt_tolower_ },
    { "api-ms-win-crt-string-l1-1-0.dll", "iswpunct", ucrt_iswpunct_ },
    { "api-ms-win-crt-string-l1-1-0.dll", "_wcsicmp", ucrt_wcsicmp_ },
    { "api-ms-win-crt-string-l1-1-0.dll", "_wcsnicmp", ucrt_wcsnicmp_ },
    { "api-ms-win-crt-string-l1-1-0.dll", "wcsncmp", ucrt_wcsncmp_ },
    { "api-ms-win-crt-string-l1-1-0.dll", "strcpy_s", ucrt_strcpy_s_ },
    { "api-ms-win-crt-string-l1-1-0.dll", "strcat_s", ucrt_strcat_s_ },
    { "api-ms-win-crt-string-l1-1-0.dll", "strncpy_s", ucrt_strncpy_s_ },
    { "api-ms-win-crt-string-l1-1-0.dll", "wcscpy_s", ucrt_wcscpy_s_ },
    { "api-ms-win-crt-string-l1-1-0.dll", "wcscat_s", ucrt_wcscat_s_ },
    { "api-ms-win-crt-string-l1-1-0.dll", "wcsncpy_s", ucrt_wcsncpy_s_ },
    /* convert */
    { "api-ms-win-crt-convert-l1-1-0.dll", "_wtoi", ucrt_wtoi_ },
    { "api-ms-win-crt-convert-l1-1-0.dll", "_wtoi64", ucrt_wtoi64_ },
    { "api-ms-win-crt-convert-l1-1-0.dll", "_wtof", ucrt_wtof_ },
    { "api-ms-win-crt-convert-l1-1-0.dll", "wcstod", ucrt_wcstod_ },
    { "api-ms-win-crt-convert-l1-1-0.dll", "wcstoul", ucrt_wcstoul_ },
    { "api-ms-win-crt-convert-l1-1-0.dll", "_wcstoui64", ucrt_wcstoui64_ },
    /* utility */
    { "api-ms-win-crt-utility-l1-1-0.dll", "srand", ucrt_srand_ },
    { "api-ms-win-crt-utility-l1-1-0.dll", "rand", ucrt_rand_ },
    { "api-ms-win-crt-utility-l1-1-0.dll", "qsort", ucrt_qsort_ },
    /* math */
    { "api-ms-win-crt-math-l1-1-0.dll", "sin", m_sin_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "cos", m_cos_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "tan", m_tan_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "atan2", m_atan2_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "pow", m_pow_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "floor", m_floor_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "fmod", m_fmod_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "log10", m_log10_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "ldexp", m_ldexp_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "frexp", m_frexp_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "sinf", m_sinf_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "cosf", m_cosf_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "tanf", m_tanf_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "asinf", m_asinf_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "acosf", m_acosf_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "atanf", m_atanf_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "atan2f", m_atan2f_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "expf", m_expf_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "logf", m_logf_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "log10f", m_log10f_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "powf", m_powf_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "floorf", m_floorf_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "fmodf", m_fmodf_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "_finite", m_finite_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "_isnan", m_isnan_ },
    { "api-ms-win-crt-math-l1-1-0.dll", "__setusermatherr", m_setusermatherr_ },
    /* filesystem */
    { "api-ms-win-crt-filesystem-l1-1-0.dll", "_wmkdir", ucrt_wmkdir_ },
    { "api-ms-win-crt-filesystem-l1-1-0.dll", "_wstat64i32", ucrt_wstat64i32_ },
    /* time */
    { "api-ms-win-crt-time-l1-1-0.dll", "_time64", ucrt_time64_ },
    { "api-ms-win-crt-time-l1-1-0.dll", "_gmtime64_s", ucrt_gmtime64_s_ },
    { "api-ms-win-crt-time-l1-1-0.dll", "_wstrdate_s", ucrt_wstrdate_s_ },
    { "api-ms-win-crt-time-l1-1-0.dll", "_wstrtime_s", ucrt_wstrtime_s_ },
    { "api-ms-win-crt-time-l1-1-0.dll", "_wutime64", ucrt_wutime64_ },
    /* locale */
    { "api-ms-win-crt-locale-l1-1-0.dll", "_configthreadlocale", ucrt_configthreadlocale_ },
    /* runtime */
    { "api-ms-win-crt-runtime-l1-1-0.dll", "_initterm", ucrt_initterm_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "_initterm_e", ucrt_initterm_e_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "_initialize_onexit_table", ucrt_initialize_onexit_table_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "_register_onexit_function", ucrt_register_onexit_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "_crt_atexit", ucrt_crt_atexit_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "_register_thread_local_exe_atexit_callback", ucrt_noopv_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "_initialize_narrow_environment", ucrt_noop0_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "_configure_narrow_argv", ucrt_noop0_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "_set_app_type", ucrt_noopv_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "_set_abort_behavior", ucrt_noop0_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "_set_invalid_parameter_handler", ucrt_noop0_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "_get_narrow_winmain_command_line", ucrt_get_narrow_winmain_cmdline_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "_seh_filter_exe", ucrt_seh_filter_exe_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "_cexit", ucrt_noopv_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "_c_exit", ucrt_noopv_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "exit", ucrt_exit_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "_exit", ucrt_exit_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "abort", ucrt_abort_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "terminate", std_terminate_ },
    { "api-ms-win-crt-runtime-l1-1-0.dll", "_invalid_parameter_noinfo_noreturn", ucrt_invalid_param_ },
    /* stdio */
    { "api-ms-win-crt-stdio-l1-1-0.dll", "__acrt_iob_func", ucrt_acrt_iob_func_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "__p__commode", ucrt_p_commode_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "_set_fmode", ucrt_set_fmode_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "fopen_s", ucrt_fopen_s_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "fclose", ucrt_fclose_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "fflush", ucrt_fflush_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "fread", ucrt_fread_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "fwrite", ucrt_fwrite_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "_fseeki64", ucrt_fseeki64_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "ftell", ucrt_ftell_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "fgets", ucrt_fgets_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "fputs", ucrt_fputs_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "fputc", ucrt_fputc_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "__stdio_common_vsprintf", ucrt_vsprintf_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "__stdio_common_vsprintf_s", ucrt_vsprintf_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "__stdio_common_vsnprintf_s", ucrt_vsnprintf_s_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "__stdio_common_vswprintf", ucrt_vswprintf_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "__stdio_common_vsnwprintf_s", ucrt_vsnwprintf_s_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "__stdio_common_vfprintf", ucrt_vfprintf_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "__stdio_common_vfwprintf", ucrt_vfwprintf_ },
    { "api-ms-win-crt-stdio-l1-1-0.dll", "__stdio_common_vswscanf", ucrt_vswscanf_ },
    { 0 }
};
__attribute__((constructor(102))) static void ucrt_init(void) { w64_register(ucrt_exports); }
