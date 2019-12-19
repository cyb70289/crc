#include <stdio.h>
#include <stdint.h>
#include <assert.h>

const uint32_t P = 0x82F63B78;

/* x^n mod P */
static uint32_t poly(int n)
{
    uint32_t crc = 1;

    printf("x^%d mod P = ", n);
    assert(n > 31);
    n -= 31;

    while (n--) {
        int bit0 = crc & 1;
        crc >>= 1;
        if (bit0)
            crc ^= P;
    }

    printf("%x\n", crc);
}

/* CRC32(x^n) */
static uint32_t crc32(int n)
{
    poly(n+32);
}

int main(void)
{
    poly(42*64-32-1);
    poly(42*64*2-32-1);
    poly(96+128-32-1);
    poly(64+128-32-1);
    poly(128-32-1);

    return 0;
}
