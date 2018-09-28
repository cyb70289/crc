/* https://github.com/axboe/fio/blob/master/crc/crc32c-arm64.c */

#define CRC32C3X8(ITR) \
	crc1 = crc32c_u64(crc1, *((const uint64_t *)data + 42*1 + (ITR)));\
	crc2 = crc32c_u64(crc2, *((const uint64_t *)data + 42*2 + (ITR)));\
	crc0 = crc32c_u64(crc0, *((const uint64_t *)data + 42*0 + (ITR)));

#define CRC32C7X3X8(ITR) do {\
	CRC32C3X8((ITR)*7+0) \
	CRC32C3X8((ITR)*7+1) \
	CRC32C3X8((ITR)*7+2) \
	CRC32C3X8((ITR)*7+3) \
	CRC32C3X8((ITR)*7+4) \
	CRC32C3X8((ITR)*7+5) \
	CRC32C3X8((ITR)*7+6) \
	} while(0)

/*
 * Function to calculate crc with PMULL Instruction
 * crc done "by 3" for fixed input block size of 1024 bytes
 */
static uint32_t fio_crc32c(unsigned char const *data,
        unsigned long length, uint32_t crc)
{
	signed long len = length;
	uint32_t crc0, crc1, crc2;

	/* Load two consts: K1 and K2 */
	const poly64_t k1 = 0xe417f38a, k2 = 0x8f158014;
	uint64_t t0, t1;

	while ((len -= 1024) >= 0) {
		/* Do first 8 bytes here for better pipelining */
		crc0 = crc32c_u64(crc, *(const uint64_t *)data);
		crc1 = 0;
		crc2 = 0;
		data += sizeof(uint64_t);

		/* Process block inline
		   Process crc0 last to avoid dependency with above */
		CRC32C7X3X8(0);
		CRC32C7X3X8(1);
		CRC32C7X3X8(2);
		CRC32C7X3X8(3);
		CRC32C7X3X8(4);
		CRC32C7X3X8(5);

		data += 42*3*sizeof(uint64_t);

		/* Merge crc0 and crc1 into crc2
		   crc1 multiply by K2
		   crc0 multiply by K1 */

		t1 = (uint64_t)vmull_p64(crc1, k2);
		t0 = (uint64_t)vmull_p64(crc0, k1);
		crc = crc32c_u64(crc2, *(const uint64_t *)data);
		crc1 = crc32c_u64(0, t1);
		crc ^= crc1;
		crc0 = crc32c_u64(0, t0);
		crc ^= crc0;

		data += sizeof(uint64_t);
	}

	if (!(len += 1024))
		return crc;

	while ((len -= sizeof(uint64_t)) >= 0) {
                crc = crc32c_u64(crc, *(const uint64_t *)data);
                data += sizeof(uint64_t);
        }

        /* The following is more efficient than the straight loop */
        if (len & sizeof(uint32_t)) {
                crc = crc32c_u32(crc, *(const uint32_t *)data);
                data += sizeof(uint32_t);
        }
        if (len & sizeof(uint16_t)) {
                crc = crc32c_u16(crc, *(const uint16_t *)data);
                data += sizeof(uint16_t);
        }
        if (len & sizeof(uint8_t)) {
                crc = crc32c_u8(crc, *(const uint8_t *)data);
        }

	return crc;
}
