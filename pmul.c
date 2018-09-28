#if defined(__x86_64__)
typedef uint64_t poly64_t;

static uint64_t vmull_p64(uint64_t p1, uint64_t p2)
{
    __m128i a = _mm_cvtsi64_si128(p1);
    __m128i b = _mm_cvtsi64_si128(p2);

    __m128i p = _mm_clmulepi64_si128(a, b, 0);

    return *(uint64_t*)&p;
}
#endif
