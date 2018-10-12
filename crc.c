#include <stdio.h>
#include <inttypes.h>
#include <sys/time.h>
#include <assert.h>

#include "crctbl.c"

#if defined(__x86_64__)
#include <x86intrin.h>
#define crc32c_u8(crc, in)  _mm_crc32_u8(crc, in)
#define crc32c_u16(crc, in) _mm_crc32_u16(crc, in)
#define crc32c_u32(crc, in) _mm_crc32_u32(crc, in)
#define crc32c_u64(crc, in) _mm_crc32_u64(crc, in)
static uint64_t vmull_p32(uint32_t p1, uint32_t p2)
{
    __m128i a = _mm_cvtsi64_si128(p1);
    __m128i b = _mm_cvtsi64_si128(p2);

    __m128i p = _mm_clmulepi64_si128(a, b, 0);

    return *(uint64_t*)&p;
}
#elif defined(__aarch64__)
#include <arm_acle.h>
#include <arm_neon.h>
#define crc32c_u8(crc, in)  __crc32cb(crc, in)
#define crc32c_u16(crc, in) __crc32ch(crc, in)
#define crc32c_u32(crc, in) __crc32cw(crc, in)
#define crc32c_u64(crc, in) __crc32cd(crc, in)
static uint64_t vmull_p32(uint32_t p1, uint32_t p2)
{
    return vmull_p64(p1, p2);
}
#endif

#ifdef CRC32_ZLIB
#include <zlib.h>
#endif

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

#ifdef CRC32_OPT
    const uint64_t *in64 = (const uint64_t *)in;
    while (size >= 1024) {
        uint64_t t0, t1;
		uint32_t crc0 = crc, crc1 = 0, crc2 = 0;

        /*
         * crc0: in64[ 0,  1, ...,  41]
         * crc1: in64[42, 43, ...,  83]
         * crc2: in64[84, 85, ..., 125]
         */
        for (int i = 0; i < 42; i++, in64++) {
            crc0 = crc32c_u64(crc0, *(in64));
            crc1 = crc32c_u64(crc1, *(in64+42));
            crc2 = crc32c_u64(crc2, *(in64+42*2));
        }
        in64 += 42*2;

        /* CRC32(x^(42*64*2-1)) = 0xe417f38a, CRC32(x^(42*64-1)) = 0x8f158014 */

        /* CRC32(crc0 * CRC32(x^(42*64*2))) */
        t0 = vmull_p32(crc0, 0xe417f38a);
        crc0 = crc32c_u64(0, t0);
        /* CRC32(crc1 * CRC32(x^(42*64))) */
        t1 = vmull_p32(crc1, 0x8f158014);
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
        int bit0 = crc & 1;
        crc >>= 1;
        if (bit0)
            crc ^= p;
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

static uint32_t crc32_fold(const uint8_t *in, size_t size, uint32_t crc)
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

    const int blocks = size / 16;
    const int residue = size % 16;

    if (blocks > 1) {
        const uint32_t k0 = 0xf20c0dfe;     /* x^(64+128-32-1) mod P */
        const uint32_t k1 = 0x493c7d27;     /* x^(128-32-1) mod P */
        __m128i vk = _mm_set_epi64x(k1, k0);

        uint64_t tmp = (*(uint64_t *)in) ^ crc;
        __m128i next = _mm_set_epi64x(*(uint64_t *)(in+8), tmp);

        __m128i h = _mm_clmulepi64_si128(vk, next, 0x00);   /* k0 * H64 */
        __m128i l = _mm_clmulepi64_si128(vk, next, 0x11);   /* k1 * L64 */

        next = _mm_loadu_si128((__m128i *)(in+16));
        next = _mm_xor_si128(next, h);
        next = _mm_xor_si128(next, l);

        in += 16;

        for (int i = 0; i < blocks-2; ++i) {
            h = _mm_clmulepi64_si128(vk, next, 0x00);
            l = _mm_clmulepi64_si128(vk, next, 0x11);

            next = _mm_load_si128((__m128i *)(in+16));

            next = _mm_xor_si128(next, h);
            next = _mm_xor_si128(next, l);

            in += 16;
        }

        uint64_t data[2];
        _mm_store_si128((__m128i *)data, next);
        crc = crc32c_u64(0, data[0]);
        crc = crc32c_u64(crc, data[1]);
        in += 16;
    }

    crc = crc32_hw(in, residue, crc);

    return crc;
}

static void print128(const char *s, const __m128i *v128)
{
    const uint64_t *v64 = (const uint64_t *)v128;
    printf("%s%" PRIx64 ", %" PRIx64 "\n", s, v64[0], v64[1]);
}

static void test_pmul(void)
{
    union {
        __m128i m128[2];
        uint64_t m64[4];
        uint8_t m8[32];
    } u;

    for (int i = 0; i < 32; i++)
        u.m8[i] = i+1;

    const uint32_t _crc = 0x4dbfd94;
    uint32_t crc = _crc;

    for (int i = 0; i < 4; ++i)
        crc = crc32c_u64(crc, u.m64[i]);
    printf("crc = %x\n", crc);

    crc = crc32_lut4(u.m8, 32, _crc);
    printf("crc = %x\n", crc);

    const uint32_t k0 = 0xf20c0dfe;     /* x^(64+128-32-1) mod P */
    const uint32_t k1 = 0x493c7d27;     /* x^(128-32-1) mod P */

    u.m64[0] ^= _crc;

    __m128i vk = _mm_set_epi64x(k1, k0);
    print128("vk = ", &vk);

    __m128i h = _mm_clmulepi64_si128(vk, u.m128[0], 0x00);
    __m128i l = _mm_clmulepi64_si128(vk, u.m128[0], 0x11);
    print128("h = ", &h);
    print128("l = ", &l);

    print128("m128[1] = ", &u.m128[1]);
    u.m128[1] = _mm_xor_si128(u.m128[1], h);
    u.m128[1] = _mm_xor_si128(u.m128[1], l);
    print128("m128[1] = ", &u.m128[1]);
    printf("m64[2] = %" PRIx64 ", m64[3] = %" PRIx64 "\n", u.m64[2], u.m64[3]);

    crc = crc32c_u64(0, u.m64[2]);
    crc = crc32c_u64(crc, u.m64[3]);

    printf("crc = %x\n", crc);
}

int main(int argc, const char *argv[])
{
    const size_t size = 1024 * 1024 + 3;
    int loops = 20190;
    uint8_t in[size];
    uint32_t c1 = 0, c2 = 0;
    int check = 0;
    struct timeval tv1, tv2;

    if (argc > 1) {
        check = 1;
        loops = 3;
    }

    for (int i = 0; i < size; ++i)
        in[i] = i+1;

    if (check) {
        for (int i = 0; i < loops; ++i)
            c1 = crc32_lut4(in, size, c1);
        printf("%x\n", c1);
    }

    gettimeofday(&tv1, 0);
    for (int i = 0; i < loops; ++i)
#if defined(CRC32_ZLIB)
        c2 = crc32(c2, in, size);
#elif defined(CRC32_FOLD)
        c2 = crc32_fold(in, size, c2);
#else
        c2 = crc32_hw(in, size, c2);
#endif
    gettimeofday(&tv2, 0);

    double time = tv2.tv_usec - tv1.tv_usec;
    time = time / 1000000 + tv2.tv_sec - tv1.tv_sec;
    double data = ((double)size * loops) / (1024*1024);

#ifdef CRC32_ZLIB
    printf("ZLIB\n");
#endif
    printf("time: %.4f s\n", time);
    printf("data: %.0f MB\n", data);
    printf("BW: %.2f MB/s\n", data / time);

    if (check) {
        if (c2 == c1)
            printf("OK\n");
        else
            printf("BAD: %x\n", c2);
    }

    return 0;
}
