/*
 * 0xFX — JACK duplex audio (Linux standalone only)
 *
 * See jack_duplex.h. The JACK API subset below is declared here instead of
 * coming from <jack/jack.h> so the build needs no JACK headers — the ABI
 * has been stable since JACK 1, and PipeWire's libjack implements it.
 */
#include "jack_duplex.h"
#include "../core/log.h"
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ── JACK API subset ──────────────────────────────────────────── */

typedef uint32_t jack_nframes_t;
typedef struct _jack_client jack_client_t;
typedef struct _jack_port   jack_port_t;
typedef struct { jack_nframes_t min, max; } jack_latency_range_t;

enum { JackNoStartServer = 0x01 };
enum { JackPortIsInput = 0x1, JackPortIsOutput = 0x2, JackPortIsPhysical = 0x4 };
enum { JackCaptureLatency = 0, JackPlaybackLatency = 1 };
#define JACK_DEFAULT_AUDIO_TYPE "32 bit float mono audio"

typedef int  (*JackProcessCallback)(jack_nframes_t nframes, void *arg);
typedef void (*JackShutdownCallback)(void *arg);

static struct {
    void *lib;
    jack_client_t *(*client_open)(const char *name, int options, int *status, ...);
    int            (*client_close)(jack_client_t *client);
    int            (*activate)(jack_client_t *client);
    int            (*deactivate)(jack_client_t *client);
    int            (*set_process_callback)(jack_client_t *client, JackProcessCallback cb, void *arg);
    void           (*on_shutdown)(jack_client_t *client, JackShutdownCallback cb, void *arg);
    jack_port_t   *(*port_register)(jack_client_t *client, const char *name, const char *type,
                                    unsigned long flags, unsigned long buffer_size);
    void          *(*port_get_buffer)(jack_port_t *port, jack_nframes_t nframes);
    const char    *(*port_name)(const jack_port_t *port);
    int            (*port_disconnect)(jack_client_t *client, jack_port_t *port);
    void           (*port_get_latency_range)(jack_port_t *port, int mode, jack_latency_range_t *range);
    const char   **(*get_ports)(jack_client_t *client, const char *name_pattern,
                                const char *type_pattern, unsigned long flags);
    void           (*free)(void *ptr);
    int            (*connect)(jack_client_t *client, const char *src, const char *dst);
    jack_nframes_t (*get_sample_rate)(jack_client_t *client);
    jack_nframes_t (*get_buffer_size)(jack_client_t *client);
    int            (*set_buffer_size)(jack_client_t *client, jack_nframes_t nframes);
} J;

/* ── State ────────────────────────────────────────────────────── */

static struct {
    jack_client_t     *client;
    jack_port_t       *in_port;
    jack_port_t       *out_port;
    fx_jack_process_fn process;
    void              *user;
    bool               active;
    volatile bool      server_gone;   /* set from JACK's shutdown callback */

    fx_jack_device_t   capture[FX_JACK_MAX_DEVICES];
    fx_jack_device_t   playback[FX_JACK_MAX_DEVICES];
    int                num_capture;
    int                num_playback;
} s;

/* ── Device lists (pure) ──────────────────────────────────────── */

/* Length of the client part of "client:port". Port short names never
 * contain ':', client names (device descriptions) might. */
static int client_len(const char *port) {
    const char *colon = strrchr(port, ':');
    return colon ? (int)(colon - port) : (int)strlen(port);
}

static bool same_client(const char *a, const char *b) {
    int la = client_len(a);
    return la == client_len(b) && strncmp(a, b, (size_t)la) == 0;
}

int fx_jack_list_capture(const char **ports, fx_jack_device_t *out, int max) {
    int n = 0;
    for (int i = 0; ports && ports[i] && n < max; i++) {
        int total = 0, pos = 0;
        for (int k = 0; ports[k]; k++) {
            if (!same_client(ports[k], ports[i])) continue;
            if (k < i) pos++;
            total++;
        }
        fx_jack_device_t *d = &out[n++];
        memset(d, 0, sizeof(*d));
        int clen = client_len(ports[i]);
        if (total > 1)
            snprintf(d->name, sizeof(d->name), "%.*s (In %d)", clen, ports[i], pos + 1);
        else
            snprintf(d->name, sizeof(d->name), "%.*s", clen, ports[i]);
        snprintf(d->ports[0], sizeof(d->ports[0]), "%s", ports[i]);
        d->num_ports = 1;
    }
    return n;
}

int fx_jack_list_playback(const char **ports, fx_jack_device_t *out, int max) {
    int n = 0;
    for (int i = 0; ports && ports[i]; i++) {
        fx_jack_device_t *d = NULL;
        for (int k = 0; k < n; k++) {
            if (same_client(out[k].ports[0], ports[i])) { d = &out[k]; break; }
        }
        if (!d) {
            if (n >= max) continue;
            d = &out[n++];
            memset(d, 0, sizeof(*d));
            snprintf(d->name, sizeof(d->name), "%.*s", client_len(ports[i]), ports[i]);
        }
        /* Mono engine output goes to the device's first pair (main L/R) */
        if (d->num_ports < FX_JACK_DEVICE_PORTS) {
            snprintf(d->ports[d->num_ports], sizeof(d->ports[0]), "%s", ports[i]);
            d->num_ports++;
        }
    }
    return n;
}

/* Characters of the client names two ports share, cut back to a word
 * boundary: "iRig HD 2 Mono" vs "iRig HD 2 Analog Stereo" -> "iRig HD 2" */
static int shared_words(const char *a, const char *b) {
    const int la = client_len(a), lb = client_len(b);
    int i = 0;
    while (i < la && i < lb && a[i] == b[i]) i++;
    if ((i == la || a[i] == ' ') && (i == lb || b[i] == ' ')) return i;
    while (i > 0 && a[i - 1] != ' ') i--;
    return i;
}

int fx_jack_match_playback(const char *capture_port, const fx_jack_device_t *playback, int n) {
    int best = n > 0 ? 0 : -1, best_len = 0;
    for (int k = 0; k < n && capture_port; k++) {
        int len = shared_words(capture_port, playback[k].ports[0]);
        if (len > best_len) { best_len = len; best = k; }
    }
    return best;
}

/* ── Callbacks ────────────────────────────────────────────────── */

static int on_process(jack_nframes_t nframes, void *arg) {
    (void)arg;
    const float *in  = (const float *)J.port_get_buffer(s.in_port, nframes);
    float       *out = (float *)J.port_get_buffer(s.out_port, nframes);
    s.process(in, out, (int)nframes, s.user);
    return 0;
}

static void on_shutdown(void *arg) {
    (void)arg;
    s.server_gone = true;
}

/* ── Library loading ──────────────────────────────────────────── */

static void unload_lib(void) {
    if (J.lib) dlclose(J.lib);
    memset(&J, 0, sizeof(J));
}

static bool load_lib(void) {
    J.lib = dlopen("libjack.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!J.lib) {
        FX_INFO("JACK: libjack.so.0 not available (%s)", dlerror());
        return false;
    }
#define LOAD(field, sym) \
    if (!(*(void **)&J.field = dlsym(J.lib, sym))) { \
        FX_WARN("JACK: libjack is missing %s", sym); \
        unload_lib(); \
        return false; \
    }
    LOAD(client_open,            "jack_client_open")
    LOAD(client_close,           "jack_client_close")
    LOAD(activate,               "jack_activate")
    LOAD(deactivate,             "jack_deactivate")
    LOAD(set_process_callback,   "jack_set_process_callback")
    LOAD(on_shutdown,            "jack_on_shutdown")
    LOAD(port_register,          "jack_port_register")
    LOAD(port_get_buffer,        "jack_port_get_buffer")
    LOAD(port_name,              "jack_port_name")
    LOAD(port_disconnect,        "jack_port_disconnect")
    LOAD(port_get_latency_range, "jack_port_get_latency_range")
    LOAD(get_ports,              "jack_get_ports")
    LOAD(free,                   "jack_free")
    LOAD(connect,                "jack_connect")
    LOAD(get_sample_rate,        "jack_get_sample_rate")
    LOAD(get_buffer_size,        "jack_get_buffer_size")
    LOAD(set_buffer_size,        "jack_set_buffer_size")
#undef LOAD
    return true;
}

/* ── Public API ───────────────────────────────────────────────── */

static void refresh_devices(void) {
    const char **cap = J.get_ports(s.client, NULL, JACK_DEFAULT_AUDIO_TYPE,
                                   JackPortIsPhysical | JackPortIsOutput);
    s.num_capture = fx_jack_list_capture(cap, s.capture, FX_JACK_MAX_DEVICES);
    if (cap) J.free(cap);

    const char **play = J.get_ports(s.client, NULL, JACK_DEFAULT_AUDIO_TYPE,
                                    JackPortIsPhysical | JackPortIsInput);
    s.num_playback = fx_jack_list_playback(play, s.playback, FX_JACK_MAX_DEVICES);
    if (play) J.free(play);
}

bool fx_jack_init(fx_jack_process_fn process, void *user) {
    if (s.client) return true;
    if (!process || !load_lib()) return false;

    int status = 0;
    s.client = J.client_open("0xFX", JackNoStartServer, &status);
    if (!s.client) {
        FX_INFO("JACK: no server reachable (status 0x%x)", status);
        unload_lib();
        return false;
    }

    s.process  = process;
    s.user     = user;
    s.in_port  = J.port_register(s.client, "in",  JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput,  0);
    s.out_port = J.port_register(s.client, "out", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (!s.in_port || !s.out_port ||
        J.set_process_callback(s.client, on_process, NULL) != 0) {
        FX_ERROR("JACK: failed to register ports / process callback");
        fx_jack_shutdown();
        return false;
    }
    J.on_shutdown(s.client, on_shutdown, NULL);

    refresh_devices();
    FX_INFO("JACK: connected (%u Hz, %u frames), %d input(s), %d output(s)",
            J.get_sample_rate(s.client), J.get_buffer_size(s.client),
            s.num_capture, s.num_playback);
    return true;
}

void fx_jack_shutdown(void) {
    if (s.client) {
        if (s.active && !s.server_gone) J.deactivate(s.client);
        J.client_close(s.client);  /* still required after a server shutdown */
    }
    unload_lib();
    memset(&s, 0, sizeof(s));
}

unsigned int fx_jack_sample_rate(void) {
    return s.client ? J.get_sample_rate(s.client) : 0;
}

int fx_jack_buffer_frames(void) {
    return (s.client && !s.server_gone) ? (int)J.get_buffer_size(s.client) : 0;
}

int fx_jack_capture_count(void)  { return s.num_capture; }
int fx_jack_playback_count(void) { return s.num_playback; }

const fx_jack_device_t *fx_jack_capture(int index) {
    return (index >= 0 && index < s.num_capture) ? &s.capture[index] : NULL;
}

const fx_jack_device_t *fx_jack_playback(int index) {
    return (index >= 0 && index < s.num_playback) ? &s.playback[index] : NULL;
}

bool fx_jack_set_buffer_frames(int frames) {
    if (!s.client || s.server_gone || frames <= 0) return false;
    /* PipeWire applies this as node.force-quantum for our node while it
     * runs; a real JACK server changes its period for every client. Always
     * ask, even when the current size already matches — it may only match
     * because another node forced it, and a fresh client must register its
     * own request or the graph falls back to its default quantum. */
    if (J.set_buffer_size(s.client, (jack_nframes_t)frames) != 0) {
        FX_WARN("JACK: server refused buffer size %d", frames);
        return false;
    }
    return true;
}

bool fx_jack_start(int capture_idx, int playback_idx, int buffer_frames) {
    if (!s.client || s.server_gone) return false;
    if (capture_idx < 0 || capture_idx >= s.num_capture) return false;

    /* No explicit output: the input's own device if it has outputs
     * (an iRig or Scarlett monitors through itself), else the first. */
    if (playback_idx < 0 || playback_idx >= s.num_playback)
        playback_idx = fx_jack_match_playback(s.capture[capture_idx].ports[0],
                                              s.playback, s.num_playback);

    if (!s.active) {
        if (J.activate(s.client) != 0) {
            FX_ERROR("JACK: failed to activate client");
            return false;
        }
        s.active = true;
    }
    fx_jack_set_buffer_frames(buffer_frames);

    /* Rewire from scratch — also how a device switch is applied live */
    J.port_disconnect(s.client, s.in_port);
    J.port_disconnect(s.client, s.out_port);

    const fx_jack_device_t *in = &s.capture[capture_idx];
    if (J.connect(s.client, in->ports[0], J.port_name(s.in_port)) != 0) {
        FX_ERROR("JACK: failed to connect input %s", in->ports[0]);
        return false;
    }
    const fx_jack_device_t *out = playback_idx >= 0 ? &s.playback[playback_idx] : NULL;
    for (int p = 0; out && p < out->num_ports; p++) {
        if (J.connect(s.client, J.port_name(s.out_port), out->ports[p]) != 0)
            FX_WARN("JACK: failed to connect output %s", out->ports[p]);
    }

    FX_INFO("JACK started: in=%s out=%s (%u Hz, %u frames requested %d)",
            in->name, out ? out->name : "(none)",
            J.get_sample_rate(s.client), J.get_buffer_size(s.client), buffer_frames);
    return true;
}

void fx_jack_stop(void) {
    if (!s.client || !s.active) return;
    if (!s.server_gone) J.deactivate(s.client);
    s.active = false;
}

bool fx_jack_running(void) {
    return s.active && !s.server_gone;
}

float fx_jack_latency_ms(void) {
    if (!fx_jack_running()) return 0.0f;
    jack_latency_range_t cap = { 0, 0 }, play = { 0, 0 };
    J.port_get_latency_range(s.in_port,  JackCaptureLatency,  &cap);
    J.port_get_latency_range(s.out_port, JackPlaybackLatency, &play);
    jack_nframes_t rate = J.get_sample_rate(s.client);
    if (rate == 0) return 0.0f;
    return 1000.0f * (float)(cap.max + play.max) / (float)rate;
}
