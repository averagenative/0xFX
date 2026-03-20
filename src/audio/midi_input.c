/*
 * 0xFX — MIDI input manager
 *
 * Handles MIDI device enumeration, CC reception, and MIDI learn.
 * Platform backends:
 *   Windows: Win32 MIDI API (midiIn* from mmsystem.h)
 *   Linux:   ALSA rawmidi (optional — degrades to no-MIDI if unavailable)
 *
 * CC messages only — no notes, no sysex.
 * Thread-safe: MIDI callbacks arrive from OS threads.
 */
#include "midi_input.h"
#include <stdio.h>
#include <string.h>

/* ── Platform detection ──────────────────────────────────────── */

#if defined(_WIN32)
#define FX_MIDI_WIN32 1
#include <windows.h>
#include <mmsystem.h>
#elif defined(__linux__)
/* Try ALSA — compile-time optional */
#if __has_include(<alsa/asoundlib.h>)
#define FX_MIDI_ALSA 1
#include <alsa/asoundlib.h>
#include <pthread.h>
#else
#define FX_MIDI_NONE 1
#endif
#else
#define FX_MIDI_NONE 1
#endif

/* ── Constants ───────────────────────────────────────────────── */

#define MAX_MIDI_DEVICES 32
#define MIDI_CC_COUNT    128
#define MIDI_STATUS_CC   0xB0

/* Unmapped sentinel */
#define MIDI_UNMAPPED    (-1)

/* ── State ───────────────────────────────────────────────────── */

typedef struct {
    /* Device info */
    char device_names[MAX_MIDI_DEVICES][256];
    int  num_devices;

    /* Active device */
    bool is_open;

    /* CC mapping table: cc_map[cc_number] = param_target_id or MIDI_UNMAPPED */
    int  cc_map[MIDI_CC_COUNT];

    /* MIDI learn state */
    bool learn_active;
    int  learn_target;

    /* User callback */
    fx_midi_cc_callback_t callback;
    void                 *callback_userdata;

#if defined(FX_MIDI_WIN32)
    HMIDIIN midi_handle;
#elif defined(FX_MIDI_ALSA)
    snd_rawmidi_t *rawmidi_handle;
    pthread_t      read_thread;
    volatile bool  thread_running;
#endif
} midi_manager_t;

static midi_manager_t g_midi = {0};

/* ── Internal: process a CC message ──────────────────────────── */

static void handle_cc(int channel, int cc, int value) {
    /* MIDI learn: map this CC to the learn target */
    if (g_midi.learn_active) {
        g_midi.cc_map[cc] = g_midi.learn_target;
        g_midi.learn_active = false;
        printf("[0xFX] MIDI learn: CC %d -> param %d\n", cc, g_midi.learn_target);
    }

    /* Fire user callback */
    if (g_midi.callback) {
        g_midi.callback(channel, cc, value, g_midi.callback_userdata);
    }
}

/* ── Platform: Windows ───────────────────────────────────────── */

#if defined(FX_MIDI_WIN32)

static void CALLBACK midi_in_callback(HMIDIIN hMidiIn, UINT wMsg,
                                       DWORD_PTR dwInstance,
                                       DWORD_PTR dwParam1,
                                       DWORD_PTR dwParam2) {
    (void)hMidiIn;
    (void)dwInstance;
    (void)dwParam2;

    if (wMsg == MIM_DATA) {
        unsigned char status  = (unsigned char)(dwParam1 & 0xFF);
        unsigned char data1   = (unsigned char)((dwParam1 >> 8) & 0xFF);
        unsigned char data2   = (unsigned char)((dwParam1 >> 16) & 0xFF);

        /* CC message: status 0xBn where n = channel */
        if ((status & 0xF0) == MIDI_STATUS_CC) {
            int channel = status & 0x0F;
            int cc      = data1 & 0x7F;
            int value   = data2 & 0x7F;
            handle_cc(channel, cc, value);
        }
    }
}

static void enumerate_midi_devices(void) {
    UINT count = midiInGetNumDevs();
    g_midi.num_devices = (int)(count < MAX_MIDI_DEVICES ? count : MAX_MIDI_DEVICES);

    for (int i = 0; i < g_midi.num_devices; i++) {
        MIDIINCAPS caps;
        if (midiInGetDevCaps((UINT)i, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            snprintf(g_midi.device_names[i], 256, "%s", caps.szPname);
        } else {
            snprintf(g_midi.device_names[i], 256, "MIDI Device %d", i);
        }
    }
}

static bool open_midi_device(int index) {
    MMRESULT result = midiInOpen(&g_midi.midi_handle, (UINT)index,
                                  (DWORD_PTR)midi_in_callback,
                                  0, CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR) {
        fprintf(stderr, "[0xFX] Failed to open MIDI device %d\n", index);
        return false;
    }

    result = midiInStart(g_midi.midi_handle);
    if (result != MMSYSERR_NOERROR) {
        fprintf(stderr, "[0xFX] Failed to start MIDI input\n");
        midiInClose(g_midi.midi_handle);
        return false;
    }

    return true;
}

static void close_midi_device(void) {
    midiInStop(g_midi.midi_handle);
    midiInClose(g_midi.midi_handle);
    g_midi.midi_handle = NULL;
}

/* ── Platform: Linux ALSA ────────────────────────────────────── */

#elif defined(FX_MIDI_ALSA)

static void *alsa_read_thread(void *arg) {
    (void)arg;
    unsigned char buf[3];

    while (g_midi.thread_running) {
        int n = (int)snd_rawmidi_read(g_midi.rawmidi_handle, buf, 3);
        if (n < 3) continue;

        unsigned char status = buf[0];
        unsigned char data1  = buf[1];
        unsigned char data2  = buf[2];

        /* CC message: status 0xBn */
        if ((status & 0xF0) == MIDI_STATUS_CC) {
            int channel = status & 0x0F;
            int cc      = data1 & 0x7F;
            int value   = data2 & 0x7F;
            handle_cc(channel, cc, value);
        }
    }
    return NULL;
}

static void enumerate_midi_devices(void) {
    int card = -1;
    g_midi.num_devices = 0;

    while (snd_card_next(&card) >= 0 && card >= 0) {
        snd_ctl_t *ctl;
        char name[64];
        snprintf(name, sizeof(name), "hw:%d", card);

        if (snd_ctl_open(&ctl, name, 0) < 0) continue;

        int device = -1;
        while (snd_ctl_rawmidi_next_device(ctl, &device) >= 0 && device >= 0) {
            if (g_midi.num_devices >= MAX_MIDI_DEVICES) break;

            snd_rawmidi_info_t *info;
            snd_rawmidi_info_alloca(&info);
            snd_rawmidi_info_set_device(info, (unsigned int)device);
            snd_rawmidi_info_set_subdevice(info, 0);
            snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);

            if (snd_ctl_rawmidi_info(ctl, info) >= 0) {
                const char *dev_name = snd_rawmidi_info_get_name(info);
                int idx = g_midi.num_devices;
                snprintf(g_midi.device_names[idx], 256, "hw:%d,%d - %s",
                         card, device, dev_name ? dev_name : "Unknown");
                g_midi.num_devices++;
            }
        }
        snd_ctl_close(ctl);
    }
}

static bool open_midi_device(int index) {
    if (index < 0 || index >= g_midi.num_devices) return false;

    /* Extract hw:X,Y from the stored name */
    char hw_name[64];
    /* device_names format: "hw:X,Y - Name" */
    if (sscanf(g_midi.device_names[index], "%63[^ ]", hw_name) != 1) {
        return false;
    }

    int err = snd_rawmidi_open(&g_midi.rawmidi_handle, NULL, hw_name,
                                SND_RAWMIDI_NONBLOCK);
    if (err < 0) {
        fprintf(stderr, "[0xFX] Failed to open MIDI device %s: %s\n",
                hw_name, snd_strerror(err));
        return false;
    }

    /* Switch to blocking mode for the read thread */
    snd_rawmidi_nonblock(g_midi.rawmidi_handle, 0);

    /* Start read thread */
    g_midi.thread_running = true;
    if (pthread_create(&g_midi.read_thread, NULL, alsa_read_thread, NULL) != 0) {
        fprintf(stderr, "[0xFX] Failed to create MIDI read thread\n");
        snd_rawmidi_close(g_midi.rawmidi_handle);
        g_midi.rawmidi_handle = NULL;
        return false;
    }

    return true;
}

static void close_midi_device(void) {
    g_midi.thread_running = false;

    /* Unblock the read thread by closing the rawmidi handle */
    if (g_midi.rawmidi_handle) {
        snd_rawmidi_close(g_midi.rawmidi_handle);
        g_midi.rawmidi_handle = NULL;
    }

    pthread_join(g_midi.read_thread, NULL);
}

/* ── Platform: No MIDI ───────────────────────────────────────── */

#elif defined(FX_MIDI_NONE)

static void enumerate_midi_devices(void) {
    g_midi.num_devices = 0;
}

static bool open_midi_device(int index) {
    (void)index;
    return false;
}

static void close_midi_device(void) {
    /* no-op */
}

#endif

/* ── Public API ──────────────────────────────────────────────── */

void fx_midi_init(void) {
    memset(&g_midi, 0, sizeof(g_midi));

    /* Initialize CC map to unmapped */
    for (int i = 0; i < MIDI_CC_COUNT; i++) {
        g_midi.cc_map[i] = MIDI_UNMAPPED;
    }
    g_midi.learn_target = MIDI_UNMAPPED;

    enumerate_midi_devices();

    printf("[0xFX] MIDI initialized. Found %d device(s):\n", g_midi.num_devices);
    for (int i = 0; i < g_midi.num_devices; i++) {
        printf("    [%d] %s\n", i, g_midi.device_names[i]);
    }
}

void fx_midi_shutdown(void) {
    if (g_midi.is_open) {
        close_midi_device();
        g_midi.is_open = false;
    }
    memset(&g_midi, 0, sizeof(g_midi));
}

int fx_midi_get_device_count(void) {
    return g_midi.num_devices;
}

const char *fx_midi_get_device_name(int index) {
    if (index < 0 || index >= g_midi.num_devices) return NULL;
    return g_midi.device_names[index];
}

bool fx_midi_open(int device_index) {
    if (device_index < 0 || device_index >= g_midi.num_devices) return false;

    /* Close existing device if open */
    if (g_midi.is_open) {
        close_midi_device();
        g_midi.is_open = false;
    }

    if (open_midi_device(device_index)) {
        g_midi.is_open = true;
        printf("[0xFX] MIDI opened: %s\n", g_midi.device_names[device_index]);
        return true;
    }

    return false;
}

void fx_midi_close(void) {
    if (g_midi.is_open) {
        close_midi_device();
        g_midi.is_open = false;
        printf("[0xFX] MIDI closed\n");
    }
}

bool fx_midi_is_open(void) {
    return g_midi.is_open;
}

void fx_midi_set_callback(fx_midi_cc_callback_t cb, void *userdata) {
    g_midi.callback = cb;
    g_midi.callback_userdata = userdata;
}

void fx_midi_learn_start(int param_target_id) {
    g_midi.learn_target = param_target_id;
    g_midi.learn_active = true;
    printf("[0xFX] MIDI learn started for param %d\n", param_target_id);
}

void fx_midi_learn_cancel(void) {
    g_midi.learn_active = false;
    g_midi.learn_target = MIDI_UNMAPPED;
}

bool fx_midi_learn_active(void) {
    return g_midi.learn_active;
}

int fx_midi_learn_target(void) {
    return g_midi.learn_target;
}

void fx_midi_map_cc(int cc_number, int param_target_id) {
    if (cc_number >= 0 && cc_number < MIDI_CC_COUNT) {
        g_midi.cc_map[cc_number] = param_target_id;
    }
}

void fx_midi_unmap_cc(int cc_number) {
    if (cc_number >= 0 && cc_number < MIDI_CC_COUNT) {
        g_midi.cc_map[cc_number] = MIDI_UNMAPPED;
    }
}

int fx_midi_get_mapped_param(int cc_number) {
    if (cc_number < 0 || cc_number >= MIDI_CC_COUNT) return MIDI_UNMAPPED;
    return g_midi.cc_map[cc_number];
}
