#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/time.h>
#include <assert.h>

#if defined(__x86_64__)
#include <x86intrin.h>
#define crc32c_u8(crc, in)  _mm_crc32_u8(crc, in)
#define crc32c_u16(crc, in) _mm_crc32_u16(crc, in)
#define crc32c_u32(crc, in) _mm_crc32_u32(crc, in)
#define crc32c_u64(crc, in) _mm_crc32_u64(crc, in)
static uint64_t vmull_p32(uint32_t p1, uint32_t p2)
{
    __m128i p = _mm_set_epi64x(p1, p2);
    p = _mm_clmulepi64_si128(p, p, 0x01);
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

/*
 * Define USE_PMULL = 0 to drop pmull and use only crc hw instructions.
 * Make sure performance improvement actually comes from pmull + crc
 * parallism, not loop unrool.
 */
#define USE_PMULL   1

/* Test logs (more block size, more block count --> better performance)
 *
 * baseline
 * - blk_cnt = 8, blk_sz = 4096
 * positive:
 * - blk_cnt = 10, blk_sz = 10*512
 * negative:
 * - blk_cnt = 10, blk_sz = 10*256
 * - blk_cnt = 6, blk_sz = 6*512
 * - move pmull to middle buf4_ptr: no changes
 */

static const int blk_sz = 4096;
static const int blk_cnt = 8;

static uint32_t pmull_crc_poc(const uint8_t *in, size_t size, uint32_t crc)
{
    static const int blk_loops = blk_sz / blk_cnt / 16;

#ifdef __aarch64__
    uint64x2_t vk =  { 0x87654321, 0x12345678 };
#else
    __m128i vk = _mm_set_epi64x(0x12345678, 0x87654321);
#endif

    while (size >= blk_sz) {
        uint32_t crc1 = crc, crc2 = 0, crc3 = 0, crc4 = 0;
        uint32_t crc5 = 0,   crc6 = 0, crc7 = 0, crc8 = 0;
#ifdef __aarch64__
        uint64x2_t h, l, next = { 0, 0 };
#else
        __m128i h, l, next = _mm_setzero_si128();
#endif

        const int ptr64_gap = blk_sz / blk_cnt / 8;

        const uint64_t *buf1_ptr = (const uint64_t *)in;
        const uint64_t *buf2_ptr = buf1_ptr + ptr64_gap;
        const uint64_t *buf3_ptr = buf2_ptr + ptr64_gap;
        const uint64_t *buf4_ptr = buf3_ptr + ptr64_gap;
        const uint64_t *buf5_ptr = buf4_ptr + ptr64_gap;
        const uint64_t *buf6_ptr = buf5_ptr + ptr64_gap;
        const uint64_t *buf7_ptr = buf6_ptr + ptr64_gap;
        const uint64_t *buf8_ptr = buf7_ptr + ptr64_gap;

        for (int i = 0; i < blk_loops; ++i) {
            crc1 = crc32c_u64(crc1, *buf1_ptr++);
            crc1 = crc32c_u64(crc1, *buf1_ptr++);

            crc2 = crc32c_u64(crc2, *buf2_ptr++);
            crc2 = crc32c_u64(crc2, *buf2_ptr++);

            crc3 = crc32c_u64(crc3, *buf3_ptr++);
            crc3 = crc32c_u64(crc3, *buf3_ptr++);

            crc4 = crc32c_u64(crc4, *buf4_ptr++);
            crc4 = crc32c_u64(crc4, *buf4_ptr++);

            crc5 = crc32c_u64(crc5, *buf5_ptr++);
            crc5 = crc32c_u64(crc5, *buf5_ptr++);

            crc6 = crc32c_u64(crc6, *buf6_ptr++);
            crc6 = crc32c_u64(crc6, *buf6_ptr++);

            crc7 = crc32c_u64(crc7, *buf7_ptr++);
            crc7 = crc32c_u64(crc7, *buf7_ptr++);

#if USE_PMULL
#ifdef __aarch64__
	        h = (uint64x2_t)vmull_p64(
                    (poly64_t)vgetq_lane_u64(next, 1),
                    (poly64_t)vgetq_lane_u64(vk, 1));
	        l = (uint64x2_t)vmull_p64(
                    (poly64_t)vgetq_lane_u64(next, 0),
                    (poly64_t)vgetq_lane_u64(vk, 0));
            next = vld1q_u64(buf8_ptr);
	        next = veorq_u64(next, h);
            next = veorq_u64(next, l);
            buf8_ptr += 2;
#else
            h = _mm_clmulepi64_si128(vk, next, 0x00);
            l = _mm_clmulepi64_si128(vk, next, 0x11);
            next = _mm_load_si128((__m128i *)buf8_ptr);
            next = _mm_xor_si128(next, h);
            next = _mm_xor_si128(next, l);
            buf8_ptr += 2;
#endif
#else   /* USE_PMULL = 0 */
            crc8 = crc32c_u64(crc8, *buf8_ptr++);
            crc8 = crc32c_u64(crc8, *buf8_ptr++);
#endif
        }

#if USE_PMULL
        uint64_t data[2];
#ifdef __aarch64__
        vst1q_u64(data, next);
#else
        _mm_store_si128((__m128i *)data, next);
#endif
        crc8 = crc32c_u64(crc8, data[0]);
        crc8 = crc32c_u64(crc8, data[1]);
#endif

        crc1 = crc32c_u64(0, vmull_p32(crc1, 0x11111111));
        crc2 = crc32c_u64(0, vmull_p32(crc2, 0x22222222));
        crc3 = crc32c_u64(0, vmull_p32(crc3, 0x33333333));
        crc4 = crc32c_u64(0, vmull_p32(crc4, 0x44444444));
        crc5 = crc32c_u64(0, vmull_p32(crc5, 0x55555555));
        crc6 = crc32c_u64(0, vmull_p32(crc6, 0x66666666));
        crc7 = crc32c_u64(0, vmull_p32(crc7, 0x77777777));

        crc = crc1 ^ crc2 ^ crc3 ^ crc4 ^ crc5 ^ crc6 ^ crc7 ^ crc8;

        in += blk_sz;
        size -= blk_sz;
    }

    return crc;
}

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

    const uint64_t *in64 = (const uint64_t *)in;
    while (size >= 1024) {
		uint32_t crc0 = crc, crc1 = 0, crc2 = 0;

        for (int i = 0; i < 42; i++, in64++) {
            crc0 = crc32c_u64(crc0, *(in64));
            crc1 = crc32c_u64(crc1, *(in64+42));
            crc2 = crc32c_u64(crc2, *(in64+42*2));
        }
        in64 += 42*2;

        crc0 = crc32c_u64(0, vmull_p32(crc0, 0xcec3662e));
        crc1 = crc32c_u64(0, vmull_p32(crc1, 0xa60ce07b));

        crc = crc0 ^ crc1 ^ crc2;

        /* last two u64 */
        crc = crc32c_u64(crc, *in64++);
        crc = crc32c_u64(crc, *in64++);

        size -= 1024;
    }
    in = (const uint8_t *)in64;

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

int main(void)
{
    const size_t size = 64*1024*1024;

    const double data_mb = 10000;    /* Test 10G data */
    const int loops = data_mb*1024*1024 / size;
    uint8_t *in;
    uint32_t c1 = 0, c2 = 0;
    struct timeval tv1, tv2;
    double time;

    assert(blk_sz % blk_cnt == 0);
    assert((blk_sz / blk_cnt) % 16 == 0);

    if (posix_memalign((void **)&in, 1024, size)) {
        printf("alloc failed\n");
        return 1;
    }

    for (int i = 0; i < size; ++i) {
        in[i] = i+1;
    }

    /***************************************************************/
    printf("3 parallel crc hw...\n");

    /* warm up */
    for (int i = 0; i < loops; ++i) {
        crc32_hw(in, size, 0);
    }

    gettimeofday(&tv1, 0);
    for (int i = 0; i < loops; ++i) {
        c1 = crc32_hw(in, size, c1);
    }
    gettimeofday(&tv2, 0);

    time = tv2.tv_usec - tv1.tv_usec;
    time = time / 1000000 + tv2.tv_sec - tv1.tv_sec;
    printf("BW: %.2f MB/s\n\n", data_mb / time);

    /***************************************************************/
    printf("pmull + crc hw...\n");

    /* warm up */
    for (int i = 0; i < loops; ++i) {
        pmull_crc_poc(in, size, 0);
    }

    gettimeofday(&tv1, 0);
    for (int i = 0; i < loops; ++i) {
        c2 = pmull_crc_poc(in, size, c2);
    }
    gettimeofday(&tv2, 0);

    time = tv2.tv_usec - tv1.tv_usec;
    time = time / 1000000 + tv2.tv_sec - tv1.tv_sec;
    printf("BW: %.2f MB/s\n\n", data_mb / time);

    if (c2 == c1)
        printf("OK\n");
    else
        printf("!!! WRONG RESULT !!!: %x, should be %x\n", c2, c1);

    return 0;
}
