#include <stdio.h>
#include <inttypes.h>

#include "crctbl.c"

#if defined(__x86_64__)
#include <x86intrin.h>
#include "pmul.c"
#define crc32c_u8(crc, in)  _mm_crc32_u8(crc, in)
#define crc32c_u16(crc, in) _mm_crc32_u16(crc, in)
#define crc32c_u32(crc, in) _mm_crc32_u32(crc, in)
#define crc32c_u64(crc, in) _mm_crc32_u64(crc, in)
#elif defined(__aarch64__)
#include <arm_acle.h>
#include <arm_neon.h>
#define crc32c_u8(crc, in)  __crc32cb(crc, in)
#define crc32c_u16(crc, in) __crc32ch(crc, in)
#define crc32c_u32(crc, in) __crc32cw(crc, in)
#define crc32c_u64(crc, in) __crc32cd(crc, in)
#endif

#include "fio.c"

static uint32_t crc32_hw(const uint8_t* in, size_t size, uint32_t crc)
{
    if (((uintptr_t)(in) & 1) && size >= 1) {
        crc = crc32c_u8(crc, *in);
        ++in;
        --size;
    }
    if (((uintptr_t)(in) & 3) && size >= 2) {
        crc = crc32c_u16(crc, *(const uint16_t*)(in));
        in += 2;
        size -= 2;
    }
    if (((uintptr_t)(in) & 7) && size >= 4) {
        crc = crc32c_u32(crc, *(const uint32_t*)(in));
        in += 4;
        size -= 4;
    }

#if 1
    const uint64_t *in64 = (const uint64_t *)in;
    while (size >= 1024) {
        uint64_t t0, t1;
		uint32_t crc0 = crc, crc1 = 0, crc2 = 0;

        /*
         * crc0: in64[ 0,  1, ...,  41]
         * crc1: in64[42, 43, ...,  83]
         * crc2: in64[84, 85, ..., 125]
         */
        for (int i = 0; i < 42; i += 3, in64 += 3) {
            crc0 = crc32c_u64(crc0, *(in64));
            crc1 = crc32c_u64(crc1, *(in64+42));
            crc2 = crc32c_u64(crc2, *(in64+42*2));

            crc0 = crc32c_u64(crc0, *(in64+1));
            crc1 = crc32c_u64(crc1, *(in64+1+42));
            crc2 = crc32c_u64(crc2, *(in64+1+42*2));

            crc0 = crc32c_u64(crc0, *(in64+2));
            crc1 = crc32c_u64(crc1, *(in64+2+42));
            crc2 = crc32c_u64(crc2, *(in64+2+42*2));
        }
        in64 += 42*2;

        /* x^(42*64*2) mod P = 0xe417f38a, x^(42*64) mod P = 0x8f158014 */

        /* CRC32(crc0 * x^(42*64*2+32)) */
        t0 = (uint64_t)vmull_p64(crc0, 0xe417f38a);
        crc0 = crc32c_u64(0, t0);
        /* CRC32(crc1 * x^(42*64+32)) */
        t1 = (uint64_t)vmull_p64(crc1, 0x8f158014);
        crc1 = crc32c_u64(0, t1);
        /* (crc2 * x^32 + in64[-2]) mod P */
        crc2 = crc32c_u64(crc2, *in64++);

        crc = crc0 ^ crc1 ^ crc2;

        /* last u64 */
        crc = crc32c_u64(crc, *in64++);

        size -= 1024;
    }
    in = (const uint8_t *)in64;
#endif

    while (size >= 8) {
        crc = crc32c_u64(crc, *(const uint64_t*)(in));
        in += 8;
        size -= 8;
    }
    if (size >= 4) {
        crc = crc32c_u32(crc, *(const uint32_t*)(in));
        in += 4;
        size -= 4;
    }
    if (size >= 2) {
        crc = crc32c_u16(crc, *(const uint16_t*)(in));
        in += 2;
        size -= 2;
    }
    if (size >= 1) {
        crc = crc32c_u8(crc, *in);
    }

    return crc;
}

static uint32_t crc32_naive_u8(uint8_t in)
{
    uint32_t crc = in;
    const uint32_t p = 0x82F63B78;

    for (int i = 0; i < 8; i++) {
#if 0
        int bit0 = -(crc & 1);
        crc >>= 1;
        crc ^= (bit0 & p);
#else
        int bit0 = crc & 1;
        crc >>= 1;
        if (bit0)
            crc ^= p;
#endif
    }

    return crc;
}

static uint32_t crc32_naive(const uint8_t *in, size_t size, uint32_t crc)
{
    for (int i = 0; i < size; i++) {
        uint32_t tmp = crc32_naive_u8(crc ^ in[i]);
        crc >>= 8;
        crc ^= tmp;
    }

    return crc;
}

static uint32_t crc32_lut(const uint8_t *in, size_t size, uint32_t crc)
{
    for (int i = 0; i < size; i++) {
        uint32_t tmp = crc32_tbl[0][(crc ^ in[i]) & 0xFF];
        crc >>= 8;
        crc ^= tmp;
    }

    return crc;
}

static uint32_t crc32_lut4(const uint8_t *in, size_t size, uint32_t crc)
{
    const int unaligned = (uintptr_t)in & 3;
    if (unaligned) {
        int align = 4 - unaligned;
        if (align > size)
            align = size;
        crc = crc32_lut(in, align, crc);
        in += align;
        size -= align;
    }

    const uint32_t *in32 = (const uint32_t *)in;
    while (size >= 4) {
        crc ^= *in32++;
        crc = crc32_tbl[3][crc & 0xFF] ^
              crc32_tbl[2][(crc >> 8) & 0xFF] ^
              crc32_tbl[1][(crc >> 16) & 0xFF] ^
              crc32_tbl[0][crc >> 24];
        size -= 4;
    }

    return crc32_lut((const uint8_t *)in32, size, crc);
}

int main(int argc, const char *argv[])
{
    uint8_t in[1024*1024+3];
    const int loops = 501;
    const size_t size = sizeof(in) / sizeof(in[0]);
    uint32_t c1 = 0, c2 = 0;
    int bench = 0;

    if (argc > 1)
        bench = 1;

    for (int i = 0; i < size; ++i)
        in[i] = i+1;

    if (!bench) {
        for (int i = 0; i < loops; ++i)
            c1 = crc32_lut4(in, size, c1);
        printf("%x\n", c1);
    }

    for (int i = 0; i < loops; ++i)
        c2 = crc32_hw(in, size, c2);
//        c2 = fio_crc32c(in, size, c2);

    if (!bench) {
        if (c2 == c1)
            printf("OK\n");
        else
            printf("BAD: %x\n", c2);
    }

    return 0;
}
