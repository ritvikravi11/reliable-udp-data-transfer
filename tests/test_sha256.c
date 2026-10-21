#include <stdio.h>
#include <string.h>

#include "../include/sha256.h"

int main(void)
{
    static const char expected[] =
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad";
    const char message[] = "abc";
    Sha256Context context;
    uint8_t digest[SHA256_DIGEST_SIZE];
    char hexadecimal[SHA256_HEX_SIZE];

    sha256_init(&context);
    sha256_update(&context, (const uint8_t *)message, strlen(message));
    sha256_final(&context, digest);
    sha256_hex(digest, hexadecimal);

    if (strcmp(hexadecimal, expected) != 0) {
        fprintf(stderr, "SHA-256 mismatch: %s\n", hexadecimal);
        return 1;
    }
    printf("SHA-256 test passed\n");
    return 0;
}
