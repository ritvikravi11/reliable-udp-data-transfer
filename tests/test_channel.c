#include <stdio.h>
#include <string.h>
#include "channel.h"

static int run_test(double loss, double duplicate, double corrupt,
                    unsigned int min_delay, unsigned int max_delay,
                    int expected_result, const char *name)
{
    ChannelConfig config;
    ChannelStats stats = {0};
    const uint8_t input[] = "Hello UDP";
    uint8_t output[128];
    unsigned int delay_ms = 0;

    channel_default_config(&config);
    config.loss_rate = loss;
    config.duplicate_rate = duplicate;
    config.corrupt_rate = corrupt;
    config.min_delay_ms = min_delay;
    config.max_delay_ms = max_delay;

    channel_init(config.seed);

    int result = channel_process(
        &config, &stats, input, sizeof(input),
        output, sizeof(output), &delay_ms
    );

    if (result != expected_result) {
        printf("FAIL: %s (result %d)\n", name, result);
        return 1;
    }

    if (result == 1 && corrupt == 0.0 &&
        memcmp(input, output, sizeof(input)) != 0) {
        printf("FAIL: %s (packet changed unexpectedly)\n", name);
        return 1;
    }

    if (loss == 1.0 && stats.packets_dropped != 1) {
        printf("FAIL: %s (drop counter incorrect)\n", name);
        return 1;
    }

    if (duplicate == 1.0 && stats.packets_duplicated != 1) {
        printf("FAIL: %s (duplicate counter incorrect)\n", name);
        return 1;
    }

    if (corrupt == 1.0) {
        if (stats.packets_corrupted != 1 ||
            memcmp(input, output, sizeof(input)) == 0) {
            printf("FAIL: %s (corruption not detected)\n", name);
            return 1;
        }
    }

    if (min_delay == max_delay && delay_ms != min_delay) {
        printf("FAIL: %s (delay incorrect)\n", name);
        return 1;
    }

    printf("PASS: %s\n", name);
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += run_test(0, 0, 0, 0, 0, 1, "Normal packet");
    failures += run_test(1, 0, 0, 0, 0, 0, "Packet loss");
    failures += run_test(0, 1, 0, 0, 0, 2, "Packet duplication");
    failures += run_test(0, 0, 1, 0, 0, 1, "Packet corruption");
    failures += run_test(0, 0, 0, 20, 20, 1, "Packet delay");

    if (failures != 0) {
        printf("%d test(s) failed\n", failures);
        return 1;
    }

    printf("All channel tests passed!\n");
    return 0;
}