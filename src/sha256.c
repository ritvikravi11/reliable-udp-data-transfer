#include "../include/sha256.h"

#include <string.h>

static const uint32_t constants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static uint32_t rotate_right(uint32_t value, unsigned int amount)
{
    return (value >> amount) | (value << (32U - amount));
}

static void transform(Sha256Context *context, const uint8_t block[64])
{
    uint32_t words[64];
    uint32_t a, b, c, d, e, f, g, h;
    int i;

    for (i = 0; i < 16; ++i) {
        words[i] = ((uint32_t)block[i * 4] << 24) |
            ((uint32_t)block[i * 4 + 1] << 16) |
            ((uint32_t)block[i * 4 + 2] << 8) |
            (uint32_t)block[i * 4 + 3];
    }
    for (i = 16; i < 64; ++i) {
        uint32_t s0 = rotate_right(words[i - 15], 7) ^
            rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3);
        uint32_t s1 = rotate_right(words[i - 2], 17) ^
            rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    a = context->state[0]; b = context->state[1];
    c = context->state[2]; d = context->state[3];
    e = context->state[4]; f = context->state[5];
    g = context->state[6]; h = context->state[7];

    for (i = 0; i < 64; ++i) {
        uint32_t s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^
            rotate_right(e, 25);
        uint32_t choice = (e & f) ^ ((~e) & g);
        uint32_t temporary1 = h + s1 + choice + constants[i] + words[i];
        uint32_t s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^
            rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temporary2 = s0 + majority;

        h = g; g = f; f = e; e = d + temporary1;
        d = c; c = b; b = a; a = temporary1 + temporary2;
    }

    context->state[0] += a; context->state[1] += b;
    context->state[2] += c; context->state[3] += d;
    context->state[4] += e; context->state[5] += f;
    context->state[6] += g; context->state[7] += h;
}

void sha256_init(Sha256Context *context)
{
    static const uint32_t initial_state[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };

    if (context == NULL) {
        return;
    }
    memcpy(context->state, initial_state, sizeof(initial_state));
    context->bit_count = 0;
    context->buffer_length = 0;
}

void sha256_update(Sha256Context *context, const uint8_t *data, size_t length)
{
    size_t copied;

    if (context == NULL || data == NULL) {
        return;
    }
    context->bit_count += (uint64_t)length * 8U;

    while (length > 0) {
        copied = 64U - context->buffer_length;
        if (copied > length) {
            copied = length;
        }
        memcpy(context->buffer + context->buffer_length, data, copied);
        context->buffer_length += copied;
        data += copied;
        length -= copied;
        if (context->buffer_length == 64U) {
            transform(context, context->buffer);
            context->buffer_length = 0;
        }
    }
}

void sha256_final(Sha256Context *context, uint8_t digest[SHA256_DIGEST_SIZE])
{
    size_t i;
    uint64_t bit_count;

    if (context == NULL || digest == NULL) {
        return;
    }
    bit_count = context->bit_count;
    context->buffer[context->buffer_length++] = 0x80U;
    while (context->buffer_length != 56U) {
        if (context->buffer_length == 64U) {
            transform(context, context->buffer);
            context->buffer_length = 0;
        }
        context->buffer[context->buffer_length++] = 0;
    }
    for (i = 0; i < 8; ++i) {
        context->buffer[63U - i] = (uint8_t)(bit_count >> (i * 8U));
    }
    transform(context, context->buffer);

    for (i = 0; i < 8; ++i) {
        digest[i * 4] = (uint8_t)(context->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(context->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(context->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)context->state[i];
    }
}

void sha256_hex(const uint8_t digest[SHA256_DIGEST_SIZE],
                char output[SHA256_HEX_SIZE])
{
    static const char hex[] = "0123456789abcdef";
    size_t i;

    if (digest == NULL || output == NULL) {
        return;
    }
    for (i = 0; i < SHA256_DIGEST_SIZE; ++i) {
        output[i * 2] = hex[digest[i] >> 4];
        output[i * 2 + 1] = hex[digest[i] & 0x0fU];
    }
    output[SHA256_HEX_SIZE - 1] = '\0';
}
