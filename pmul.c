#include <stdio.h>
#include <inttypes.h>

#if defined(__x86_64__)
#include <x86intrin.h> 

static uint64_t pmul(uint64_t p1, uint64_t p2)
{
    __m128i a = _mm_cvtsi64_si128(p1);
    __m128i b = _mm_cvtsi64_si128(p2);

    __m128i p = _mm_clmulepi64_si128(a, b, 0);

    return *(uint64_t*)&p;
}

#elif defined(__aarch64__)
#include <arm_acle.h> 


#endif

static uint64_t pmul_naive(uint64_t p1, uint64_t p2)
{
    uint64_t p = 0;
}

int main(void)
{
    printf("0x%" PRIx64 "\n", pmul(11, 22));

    return 0;
}
