#include <stdio.h>
#include <stdint.h>

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

int main(void)
{
    uint32_t crc[4][256];

    for (int i = 0; i < 256; ++i)
        crc[0][i] = crc32_naive_u8(i);

    for (int i = 0; i < 256; ++i) {
        crc[1][i] = (crc[0][i] >> 8) ^ crc[0][crc[0][i] & 0xFF];
        crc[2][i] = (crc[1][i] >> 8) ^ crc[0][crc[1][i] & 0xFF];
        crc[3][i] = (crc[2][i] >> 8) ^ crc[0][crc[2][i] & 0xFF];
    }

    printf("static uint32_t crc32_tbl[][256] = {\n");
    printf("   ");

    for (int i = 0; i < 4; ++i) {
        printf(" {\n");
        for (int j = 0; j < 256 / 4; ++j) {
            printf("        ");
            for (int k = 0; k < 4; ++k)
                printf("%s0x%08X,%s", k?" ":"", crc[i][j*4+k], k==3?"\n":"");
        }
        printf("    },");
    }

    printf("\n};\n");

    return 0;
}
