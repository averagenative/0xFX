/*
 * 0xFX — JACK port-name grouping tests (TASK-375)
 *
 * Covers the pure device-list helpers in src/audio/jack_duplex.c. Port
 * names follow what PipeWire's libjack reports: "<node.description>:<port>".
 */
#include "audio/jack_duplex.h"
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

static fx_jack_device_t devs[FX_JACK_MAX_DEVICES];

static void test_capture_list(void) {
    printf("test_capture_list...\n");
    const char *ports[] = {
        "Built-in Audio Analog Stereo:capture_FL",
        "Built-in Audio Analog Stereo:capture_FR",
        "iRig HD 2 Mono:capture_MONO",
        "Scarlett Solo USB:capture_FL",
        "Scarlett Solo USB:capture_FR",
        NULL
    };
    int n = fx_jack_list_capture(ports, devs, FX_JACK_MAX_DEVICES);
    ASSERT(n == 5, "one capture entry per port");
    ASSERT(strcmp(devs[0].name, "Built-in Audio Analog Stereo (In 1)") == 0, "multi-port device numbers its inputs");
    ASSERT(strcmp(devs[1].name, "Built-in Audio Analog Stereo (In 2)") == 0, "second input numbered 2");
    ASSERT(strcmp(devs[2].name, "iRig HD 2 Mono") == 0, "single-port device keeps the plain name");
    ASSERT(strcmp(devs[4].name, "Scarlett Solo USB (In 2)") == 0, "instrument input on a Solo is In 2");
    ASSERT(devs[2].num_ports == 1 && strcmp(devs[2].ports[0], "iRig HD 2 Mono:capture_MONO") == 0,
           "capture entry connects its own port");
    ASSERT(strcmp(devs[4].ports[0], "Scarlett Solo USB:capture_FR") == 0, "In 2 connects capture_FR");
    printf("  OK\n");
}

static void test_playback_list(void) {
    printf("test_playback_list...\n");
    const char *ports[] = {
        "Built-in Audio Analog Stereo:playback_FL",
        "iRig HD 2:playback_FL",
        "Built-in Audio Analog Stereo:playback_FR",
        "iRig HD 2:playback_FR",
        "Scarlett 18i20 USB:playback_AUX0",
        "Scarlett 18i20 USB:playback_AUX1",
        "Scarlett 18i20 USB:playback_AUX2",
        "Scarlett 18i20 USB:playback_AUX3",
        NULL
    };
    int n = fx_jack_list_playback(ports, devs, FX_JACK_MAX_DEVICES);
    ASSERT(n == 3, "one playback entry per device");
    ASSERT(strcmp(devs[0].name, "Built-in Audio Analog Stereo") == 0, "device named after the client");
    ASSERT(devs[0].num_ports == 2 &&
           strcmp(devs[0].ports[1], "Built-in Audio Analog Stereo:playback_FR") == 0,
           "interleaved ports still group by device");
    ASSERT(strcmp(devs[1].name, "iRig HD 2") == 0, "second device in first-seen order");
    ASSERT(devs[2].num_ports == 2 &&
           strcmp(devs[2].ports[0], "Scarlett 18i20 USB:playback_AUX0") == 0 &&
           strcmp(devs[2].ports[1], "Scarlett 18i20 USB:playback_AUX1") == 0,
           "multichannel output uses its first pair only");
    printf("  OK\n");
}

static void test_edge_cases(void) {
    printf("test_edge_cases...\n");
    ASSERT(fx_jack_list_capture(NULL, devs, FX_JACK_MAX_DEVICES) == 0, "NULL capture list is empty");
    ASSERT(fx_jack_list_playback(NULL, devs, FX_JACK_MAX_DEVICES) == 0, "NULL playback list is empty");

    /* Client names can contain ':' — the port name is after the last one */
    const char *colon[] = { "USB Audio: Line 6:capture_1", "USB Audio: Line 6:capture_2", NULL };
    int n = fx_jack_list_capture(colon, devs, FX_JACK_MAX_DEVICES);
    ASSERT(n == 2 && strcmp(devs[0].name, "USB Audio: Line 6 (In 1)") == 0,
           "client name with a colon groups correctly");

    const char *many[] = { "A:capture_1", "B:capture_1", "C:capture_1", NULL };
    ASSERT(fx_jack_list_capture(many, devs, 2) == 2, "capture list respects max");
    const char *many_out[] = { "A:playback_1", "B:playback_1", "C:playback_1", "A:playback_2", NULL };
    n = fx_jack_list_playback(many_out, devs, 2);
    ASSERT(n == 2 && devs[0].num_ports == 2, "playback list respects max and still fills known devices");
    printf("  OK\n");
}

static void test_default_output(void) {
    printf("test_default_output...\n");
    const char *ports[] = {
        "Built-in Audio Analog Stereo:playback_FL",
        "Built-in Audio Analog Stereo:playback_FR",
        "iRig HD 2 Analog Stereo:playback_FL",
        "iRig HD 2 Analog Stereo:playback_FR",
        "iRig Pro I/O Analog Stereo:playback_FL",
        NULL
    };
    int n = fx_jack_list_playback(ports, devs, FX_JACK_MAX_DEVICES);
    ASSERT(n == 3, "three outputs");
    ASSERT(fx_jack_match_playback("iRig HD 2 Mono:capture_MONO", devs, n) == 1,
           "iRig input defaults to the same card's output (different profile name)");
    ASSERT(fx_jack_match_playback("Built-in Audio Analog Stereo:capture_FL", devs, n) == 0,
           "built-in input defaults to built-in output");
    ASSERT(fx_jack_match_playback("iRig Pro I/O Mono:capture_MONO", devs, n) == 2,
           "longest shared word prefix wins over a shorter one");
    ASSERT(fx_jack_match_playback("Scarlett Solo USB:capture_FL", devs, n) == 0,
           "no shared words falls back to the first output");
    ASSERT(fx_jack_match_playback("iRig HD 2 Mono:capture_MONO", devs, 0) == -1,
           "no outputs gives -1");
    printf("  OK\n");
}

int main(void) {
    test_capture_list();
    test_playback_list();
    test_edge_cases();
    test_default_output();
    printf("\n═══ Results: %d passed, %d failed ═══\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
