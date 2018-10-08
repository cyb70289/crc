#include <stdio.h>
#include <stdint.h>

/* CRC32(x^(42*64)) */
static uint32_t poly1(void)
{
    const uint32_t p = 0x82F63B78;
    uint32_t crc = 1;
    int zeros = 42*64;

    while (zeros--) {
        int bit0 = crc & 1;
        crc >>= 1;
        if (bit0)
            crc ^= p;
    }

    printf("%x\n", crc);
}

/* CRC32(x^(42*64*2)) */
static uint32_t poly2(void)
{
    const uint32_t p = 0x82F63B78;
    uint32_t crc = 1;
    int zeros = 42*64*2;

    while (zeros--) {
        int bit0 = crc & 1;
        crc >>= 1;
        if (bit0)
            crc ^= p;
    }

    printf("%x\n", crc);
}

int main(void)
{
    poly1();
    poly2();

    return 0;
}
