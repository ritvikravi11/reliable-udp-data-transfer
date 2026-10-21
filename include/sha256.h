#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_DIGEST_SIZE 32
#define SHA256_HEX_SIZE 65

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t buffer[64];
    size_t buffer_length;
} Sha256Context;

void sha256_init(Sha256Context *context);
void sha256_update(Sha256Context *context, const uint8_t *data, size_t length);
void sha256_final(Sha256Context *context, uint8_t digest[SHA256_DIGEST_SIZE]);
void sha256_hex(const uint8_t digest[SHA256_DIGEST_SIZE],
                char output[SHA256_HEX_SIZE]);

#endif
