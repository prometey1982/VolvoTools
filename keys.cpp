uint32_t VolvoGenerateKey(uint8_t pin_array[5], uint8_t seed_array[3], uint8_t key_array[3])
{
	unsigned int high_part;
	unsigned int low_part;
	unsigned int hash = 0xC541A9;
	uint8_t old;
	uint8_t is_bit_set;
	uint32_t result;

	high_part = pin_array[4] << 24 | pin_array[3] << 16 | pin_array[2] << 8 | pin_array[1];
	low_part = pin_array[0] << 24 | seed_array[2] << 16 | seed_array[1] << 8 | seed_array[0];
	for (size_t i = 0; i < 32; ++i)
	{
		old = low_part;
		low_part >>= 1;
		is_bit_set = hash ^ old;
		hash >>= 1;
		if ((is_bit_set & 1) != 0)
			hash = (hash | 0x800000) ^ 0x109028;
	}
	for (size_t i = 0; i < 32; ++i)
	{
		old = high_part;
		high_part >>= 1;
		is_bit_set = hash ^ old;
		hash >>= 1;
		if ((is_bit_set & 1) != 0)
			hash = (hash | 0x800000) ^ 0x109028;
	}
	result = ((hash & 0xF00000) >> 12) | hash & 0xF000 | (uint8_t)(16 * hash) | ((hash & 0xFF0) << 12) | ((hash & 0xF0000) >> 16);
	key_array[2] = result & 0xFF;
	key_array[0] = (result >> 16) & 0xFF;
	key_array[1] = (result >> 8) & 0xFF;
	return result;
}

uint32_t p3_hash(uint8_t pin[5], uint8_t seed[3])
{
	uint32_t n = 0xc541a9, m = 0x1212050;
	uint64_t k;
	uint8_t* in = (unsigned char*)&k;

	in[0] = seed[0];
	in[1] = seed[1];
	in[2] = seed[2];
	in[3] = pin[0];
	in[4] = pin[1];
	in[5] = pin[2];
	in[6] = pin[3];
	in[7] = pin[4];

	for (size_t i = 0; i < 64; i++, n >>= 1, k >>= 1) {
		if ((n ^ k) & 0x1)
			n ^= m;
	}
	return ((n & 0xF00000) >> 12) | n & 0xF000 | (uint8_t)(16 * n) | ((n & 0xFF0) << 12) | ((n & 0xF0000) >> 16);
}

