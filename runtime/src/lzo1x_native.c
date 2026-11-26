/*
 * lzo1x_native.c - Native host C replacement for UE3's lzo1x_decompress_safe (0x140001050).
 *
 * The lifted version lives in recomp_funcs_0000.c (30k lines, compiled at -O0) and accounts
 * for ~40s of startup decompression time on host. Replacing it with standard LZO1X cuts
 * package decompression by >20x on both host and Android ARM64.
 */
#include "w64.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void L_000140001050(CPU *c);
void w64_add_override(uint64_t va, w64_fn fn, const char *name);

static int lzo1x_decompress_safe_c(const uint8_t *in, size_t in_len,
                                   uint8_t *out, size_t *out_len)
{
    const uint8_t *ip = in;
    const uint8_t *const ip_end = in + in_len;
    uint8_t *op = out;
    uint8_t *const op_end = out + *out_len;
    size_t t;

    *out_len = 0;
    if (in_len < 3) return -4; /* LZO_E_INPUT_OVERRUN */

    if (*ip > 17) {
        t = (size_t)(*ip++) - 17;
        if (t < 4) goto match_next;
        if ((size_t)(op_end - op) < t) return -5;
        if ((size_t)(ip_end - ip) < t + 1) return -4;
        do *op++ = *ip++; while (--t > 0);
        goto first_literal_run;
    }

    for (;;) {
        if (ip >= ip_end) return -4;
        t = *ip++;
        if (t >= 16) goto match;
        if (t == 0) {
            while (ip < ip_end && *ip == 0) { t += 255; ip++; }
            if (ip >= ip_end) return -4;
            t += 15 + (size_t)(*ip++);
        }
        t += 3;
        if ((size_t)(op_end - op) < t) return -5;
        if ((size_t)(ip_end - ip) < t + 1) return -4;
        memcpy(op, ip, t);
        op += t;
        ip += t;

    first_literal_run:
        if (ip >= ip_end) return -4;
        t = *ip++;
        if (t >= 16) goto match;
        if (ip >= ip_end) return -4;
        {
            const uint8_t *m_pos = op - (1 + 0x0800) - (t >> 2) - ((size_t)(*ip++) << 2);
            if (m_pos < out) return -6;
            if ((size_t)(op_end - op) < 3) return -5;
            *op++ = *m_pos++;
            *op++ = *m_pos++;
            *op++ = *m_pos++;
            goto match_done;
        }

    match:
        for (;;) {
            const uint8_t *m_pos;
            size_t m_len;
            if (t >= 64) {
                if (ip >= ip_end) return -4;
                m_pos = op - 1 - ((t >> 2) & 7) - ((size_t)(*ip++) << 3);
                m_len = (t >> 5) + 1;
            } else if (t >= 32) {
                t &= 31;
                if (t == 0) {
                    while (ip < ip_end && *ip == 0) { t += 255; ip++; }
                    if (ip >= ip_end) return -4;
                    t += 31 + (size_t)(*ip++);
                }
                if ((size_t)(ip_end - ip) < 2) return -4;
                size_t next = (size_t)ip[0] | ((size_t)ip[1] << 8);
                ip += 2;
                m_pos = op - 1 - (next >> 2);
                m_len = t + 2;
                if (m_pos < out) return -6;
                if ((size_t)(op_end - op) < m_len) return -5;
                do *op++ = *m_pos++; while (--m_len > 0);
                t = next & 3;
                goto match_done_have_t;
            } else if (t >= 16) {
                m_pos = op - ((t & 8) << 11);
                t &= 7;
                if (t == 0) {
                    while (ip < ip_end && *ip == 0) { t += 255; ip++; }
                    if (ip >= ip_end) return -4;
                    t += 7 + (size_t)(*ip++);
                }
                if ((size_t)(ip_end - ip) < 2) return -4;
                size_t next = (size_t)ip[0] | ((size_t)ip[1] << 8);
                ip += 2;
                m_pos -= (next >> 2);
                if (m_pos == op) {
                    *out_len = (size_t)(op - out);
                    return (ip == ip_end) ? 0 : (ip < ip_end ? -8 : -4);
                }
                m_pos -= 0x4000;
                m_len = t + 2;
                if (m_pos < out) return -6;
                if ((size_t)(op_end - op) < m_len) return -5;
                do *op++ = *m_pos++; while (--m_len > 0);
                t = next & 3;
                goto match_done_have_t;
            } else {
                if (ip >= ip_end) return -4;
                m_pos = op - 1 - (t >> 2) - ((size_t)(*ip++) << 2);
                if (m_pos < out) return -6;
                if ((size_t)(op_end - op) < 2) return -5;
                *op++ = *m_pos++;
                *op++ = *m_pos++;
                goto match_done;
            }
            if (m_pos < out) return -6;
            if ((size_t)(op_end - op) < m_len) return -5;
            do *op++ = *m_pos++; while (--m_len > 0);
        match_done:
            t = ip[-2] & 3;
        match_done_have_t:
            if (t == 0) break;
        match_next:
            if ((size_t)(op_end - op) < t) return -5;
            if ((size_t)(ip_end - ip) < t + 1) return -4;
            *op++ = *ip++;
            if (t > 1) { *op++ = *ip++; if (t > 2) *op++ = *ip++; }
            t = *ip++;
        }
    }
}

static void hat_lzo1x_decompress_safe(CPU *c)
{
    const uint8_t *in = (const uint8_t *)(uintptr_t)A(0);
    size_t in_len = (size_t)A(1);
    uint8_t *out = (uint8_t *)(uintptr_t)A(2);
    uint64_t out_len_va = A(3);
    size_t out_len = (size_t)rd64(out_len_va);
    int rc = lzo1x_decompress_safe_c(in, in_len, out, &out_len);
    wr64(out_len_va, (uint64_t)out_len);
    static volatile int verified = 0;
    if (verified == 0 && getenv("HAT_LZO_VERIFY")) {
        /* One-shot self-check on first block against lifted implementation. */
        if (__sync_bool_compare_and_swap(&verified, 0, 1) && out_len > 0 && out_len <= (4u << 20)) {
            uint8_t *ref = malloc(out_len);
            if (ref) {
                uint64_t ref_len = out_len;
                CPU sc = *c;
                sc.rcx = (uint64_t)(uintptr_t)in;
                sc.rdx = (uint64_t)in_len;
                sc.r8  = (uint64_t)(uintptr_t)ref;
                sc.r9  = (uint64_t)(uintptr_t)&ref_len;
                sc.rsp -= 64;
                wr64(sc.rsp + 0x20, 0);
                wr64(sc.rsp, 0xdeadbeefull);
                L_000140001050(&sc);
                int ok = ((int32_t)sc.rax == rc && ref_len == out_len && memcmp(ref, out, out_len) == 0);
                w64_log("[lzo1x] self-check %s (in=%zu out=%zu rc=%d/%d)",
                        ok ? "PASS" : "MISMATCH!", in_len, out_len, rc, (int32_t)sc.rax);
                free(ref);
            }
        }
    }
    RET((uint32_t)rc);
}

__attribute__((constructor(102))) static void hat_lzo1x_init(void)
{
    w64_add_override(0x140001050ull, hat_lzo1x_decompress_safe, "lzo1x_decompress_safe");
}
