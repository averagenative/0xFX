/*
 * recorder.c — Audio recording to WAV, MP3, or FLAC.
 *
 * Ring buffer capture from the audio callback (RT-safe).
 * Encoding and disk write happens in fx_recorder_stop().
 *
 * WAV: dr_wav (16-bit or 24-bit PCM)
 * MP3: shine fixed-point encoder (192 or 320 kbps)
 * FLAC: minimal verbatim-frame encoder (no compression, valid FLAC container)
 */

#include "recorder.h"
#include "../core/log.h"

#include "dr_wav.h"
#include "layer3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* ── Ring buffer ─────────────────────────────────────────────── */

/* Max recording: 10 minutes at 48 kHz mono */
#define RING_MAX_FRAMES (48000 * 600)

static struct {
    float             *ring;           /* heap-allocated ring buffer */
    int                write_pos;      /* next write index */
    int                capacity;       /* allocated capacity (frames) */
    float              sample_rate;
    fx_record_format_t format;
    char               path[1024];
    bool               active;
} s_rec = {0};

/* ── Public API ──────────────────────────────────────────────── */

bool fx_recorder_start(const char *path, fx_record_format_t format,
                       float sample_rate)
{
    if (s_rec.active) return false;
    if (!path || format < 0 || format >= FX_RECORD_FORMAT_COUNT) return false;

    /* Allocate ring buffer on heap (up to ~115 MB for 10 min @ 48 kHz) */
    int capacity = (int)(sample_rate * 600); /* 10 minutes */
    if (capacity > RING_MAX_FRAMES) capacity = RING_MAX_FRAMES;

    s_rec.ring = (float *)malloc((size_t)capacity * sizeof(float));
    if (!s_rec.ring) {
        FX_ERROR("recorder: failed to allocate ring buffer (%d frames)", capacity);
        return false;
    }

    s_rec.capacity = capacity;
    s_rec.write_pos = 0;
    s_rec.sample_rate = sample_rate;
    s_rec.format = format;
    strncpy(s_rec.path, path, sizeof(s_rec.path) - 1);
    s_rec.path[sizeof(s_rec.path) - 1] = '\0';
    s_rec.active = true;

    FX_INFO("recorder: started — %s (%s, %.0f Hz)",
            path, fx_recorder_format_name(format), sample_rate);
    return true;
}

void fx_recorder_feed(const float *output, int frames)
{
    if (!s_rec.active || !output) return;

    for (int i = 0; i < frames; i++) {
        if (s_rec.write_pos >= s_rec.capacity) return; /* buffer full */
        s_rec.ring[s_rec.write_pos++] = output[i];
    }
}

bool fx_recorder_active(void)
{
    return s_rec.active;
}

float fx_recorder_duration(void)
{
    if (!s_rec.active || s_rec.sample_rate <= 0.0f) return 0.0f;
    return (float)s_rec.write_pos / s_rec.sample_rate;
}

const char *fx_recorder_format_name(fx_record_format_t fmt)
{
    switch (fmt) {
    case FX_RECORD_WAV_16:   return "WAV 16-bit";
    case FX_RECORD_WAV_24:   return "WAV 24-bit";
    case FX_RECORD_MP3_192:  return "MP3 192 kbps";
    case FX_RECORD_MP3_320:  return "MP3 320 kbps";
    case FX_RECORD_FLAC_16:  return "FLAC 16-bit";
    case FX_RECORD_FLAC_24:  return "FLAC 24-bit";
    default:                 return "Unknown";
    }
}

/* ── WAV writer (via dr_wav) ─────────────────────────────────── */

static int write_wav(const float *data, int num_frames, float sample_rate,
                     const char *filepath, int bit_depth)
{
    drwav wav;
    drwav_data_format fmt;
    fmt.container = drwav_container_riff;
    fmt.channels = 1; /* mono */
    fmt.sampleRate = (drwav_uint32)sample_rate;
    fmt.format = DR_WAVE_FORMAT_PCM;
    fmt.bitsPerSample = (drwav_uint32)bit_depth;

    if (!drwav_init_file_write(&wav, filepath, &fmt, NULL)) {
        FX_ERROR("recorder: cannot open %s for WAV writing", filepath);
        return -1;
    }

    if (bit_depth == 16) {
        int16_t *pcm = (int16_t *)malloc((size_t)num_frames * sizeof(int16_t));
        if (!pcm) { drwav_uninit(&wav); return -1; }
        for (int i = 0; i < num_frames; i++) {
            float v = data[i];
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            pcm[i] = (int16_t)(v * 32767.0f);
        }
        drwav_write_pcm_frames(&wav, (drwav_uint64)num_frames, pcm);
        free(pcm);
    } else {
        /* 24-bit: dr_wav expects int32 for 24-bit PCM */
        int32_t *pcm = (int32_t *)malloc((size_t)num_frames * sizeof(int32_t));
        if (!pcm) { drwav_uninit(&wav); return -1; }
        for (int i = 0; i < num_frames; i++) {
            float v = data[i];
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            pcm[i] = (int32_t)(v * 8388607.0f);
        }
        drwav_write_pcm_frames(&wav, (drwav_uint64)num_frames, pcm);
        free(pcm);
    }

    drwav_uninit(&wav);
    FX_INFO("recorder: wrote WAV %d-bit — %s (%d frames)",
            bit_depth, filepath, num_frames);
    return 0;
}

/* ── MP3 writer (via shine) ──────────────────────────────────── */

static int write_mp3(const float *data, int num_frames, float sample_rate,
                     const char *filepath, int bitrate)
{
    if (shine_check_config((int)sample_rate, bitrate) < 0) {
        FX_ERROR("recorder: unsupported MP3 config %d Hz / %d kbps",
                 (int)sample_rate, bitrate);
        return -1;
    }

    shine_config_t config;
    shine_set_config_mpeg_defaults(&config.mpeg);
    config.mpeg.bitr = bitrate;
    config.mpeg.mode = MONO;
    config.wave.channels = PCM_MONO;
    config.wave.samplerate = (int)sample_rate;

    shine_t encoder = shine_initialise(&config);
    if (!encoder) {
        FX_ERROR("recorder: failed to init MP3 encoder");
        return -1;
    }

    int samples_per_pass = shine_samples_per_pass(encoder);

    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        FX_ERROR("recorder: cannot open %s for MP3 writing", filepath);
        shine_close(encoder);
        return -1;
    }

    /* Convert float to int16 */
    int16_t *pcm = (int16_t *)malloc((size_t)num_frames * sizeof(int16_t));
    if (!pcm) {
        fclose(fp);
        shine_close(encoder);
        return -1;
    }
    for (int i = 0; i < num_frames; i++) {
        float v = data[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        pcm[i] = (int16_t)(v * 32767.0f);
    }

    /* Encode in chunks */
    int pos = 0;
    uint32_t total_written = 0;
    while (pos < num_frames) {
        int remaining = num_frames - pos;
        int16_t *chunk = pcm + pos;

        int16_t *buf = chunk;
        int16_t *padded = NULL;
        if (remaining < samples_per_pass) {
            padded = (int16_t *)calloc((size_t)samples_per_pass, sizeof(int16_t));
            if (padded) {
                memcpy(padded, chunk, (size_t)remaining * sizeof(int16_t));
                buf = padded;
            }
        }

        int written = 0;
        unsigned char *mp3_data = shine_encode_buffer_interleaved(
            encoder, buf, &written);
        if (written > 0 && mp3_data) {
            fwrite(mp3_data, 1, (size_t)written, fp);
            total_written += (uint32_t)written;
        }

        free(padded);
        pos += samples_per_pass;
    }

    /* Flush */
    int flushed = 0;
    unsigned char *flush_data = shine_flush(encoder, &flushed);
    if (flushed > 0 && flush_data) {
        fwrite(flush_data, 1, (size_t)flushed, fp);
        total_written += (uint32_t)flushed;
    }

    fclose(fp);
    shine_close(encoder);
    free(pcm);

    FX_INFO("recorder: wrote MP3 %d kbps — %s (%u bytes)",
            bitrate, filepath, total_written);
    return 0;
}

/* ── FLAC writer (minimal verbatim encoder) ──────────────────── */

/* CRC-8 table (polynomial 0x07) for FLAC frame headers */
static const uint8_t flac_crc8_table[256] = {
    0x00,0x07,0x0E,0x09,0x1C,0x1B,0x12,0x15,0x38,0x3F,0x36,0x31,0x24,0x23,0x2A,0x2D,
    0x70,0x77,0x7E,0x79,0x6C,0x6B,0x62,0x65,0x48,0x4F,0x46,0x41,0x54,0x53,0x5A,0x5D,
    0xE0,0xE7,0xEE,0xE9,0xFC,0xFB,0xF2,0xF5,0xD8,0xDF,0xD6,0xD1,0xC4,0xC3,0xCA,0xCD,
    0x90,0x97,0x9E,0x99,0x8C,0x8B,0x82,0x85,0xA8,0xAF,0xA6,0xA1,0xB4,0xB3,0xBA,0xBD,
    0xC7,0xC0,0xC9,0xCE,0xDB,0xDC,0xD5,0xD2,0xFF,0xF8,0xF1,0xF6,0xE3,0xE4,0xED,0xEA,
    0xB7,0xB0,0xB9,0xBE,0xAB,0xAC,0xA5,0xA2,0x8F,0x88,0x81,0x86,0x93,0x94,0x9D,0x9A,
    0x27,0x20,0x29,0x2E,0x3B,0x3C,0x35,0x32,0x1F,0x18,0x11,0x16,0x03,0x04,0x0D,0x0A,
    0x57,0x50,0x59,0x5E,0x4B,0x4C,0x45,0x42,0x6F,0x68,0x61,0x66,0x73,0x74,0x7D,0x7A,
    0x89,0x8E,0x87,0x80,0x95,0x92,0x9B,0x9C,0xB1,0xB6,0xBF,0xB8,0xAD,0xAA,0xA3,0xA4,
    0xF9,0xFE,0xF7,0xF0,0xE5,0xE2,0xEB,0xEC,0xC1,0xC6,0xCF,0xC8,0xDD,0xDA,0xD3,0xD4,
    0x69,0x6E,0x67,0x60,0x75,0x72,0x7B,0x7C,0x51,0x56,0x5F,0x58,0x4D,0x4A,0x43,0x44,
    0x19,0x1E,0x17,0x10,0x05,0x02,0x0B,0x0C,0x21,0x26,0x2F,0x28,0x3D,0x3A,0x33,0x34,
    0x4E,0x49,0x40,0x47,0x52,0x55,0x5C,0x5B,0x76,0x71,0x78,0x7F,0x6A,0x6D,0x64,0x63,
    0x3E,0x39,0x30,0x37,0x22,0x25,0x2C,0x2B,0x06,0x01,0x08,0x0F,0x1A,0x1D,0x14,0x13,
    0xAE,0xA9,0xA0,0xA7,0xB2,0xB5,0xBC,0xBB,0x96,0x91,0x98,0x9F,0x8A,0x8D,0x84,0x83,
    0xDE,0xD9,0xD0,0xD7,0xC2,0xC5,0xCC,0xCB,0xE6,0xE1,0xE8,0xEF,0xFA,0xFD,0xF4,0xF3,
};

/* CRC-16 table (polynomial 0x8005) for FLAC frame footers */
static const uint16_t flac_crc16_table[256] = {
    0x0000,0x8005,0x800F,0x000A,0x801B,0x001E,0x0014,0x8011,
    0x8033,0x0036,0x003C,0x8039,0x0028,0x802D,0x8027,0x0022,
    0x8063,0x0066,0x006C,0x8069,0x0078,0x807D,0x8077,0x0072,
    0x0050,0x8055,0x805F,0x005A,0x804B,0x004E,0x0044,0x8041,
    0x80C3,0x00C6,0x00CC,0x80C9,0x00D8,0x80DD,0x80D7,0x00D2,
    0x00F0,0x80F5,0x80FF,0x00FA,0x80EB,0x00EE,0x00E4,0x80E1,
    0x00A0,0x80A5,0x80AF,0x00AA,0x80BB,0x00BE,0x00B4,0x80B1,
    0x8093,0x0096,0x009C,0x8099,0x0088,0x808D,0x8087,0x0082,
    0x8183,0x0186,0x018C,0x8189,0x0198,0x819D,0x8197,0x0192,
    0x01B0,0x81B5,0x81BF,0x01BA,0x81AB,0x01AE,0x01A4,0x81A1,
    0x01E0,0x81E5,0x81EF,0x01EA,0x81FB,0x01FE,0x01F4,0x81F1,
    0x81D3,0x01D6,0x01DC,0x81D9,0x01C8,0x81CD,0x81C7,0x01C2,
    0x0140,0x8145,0x814F,0x014A,0x815B,0x015E,0x0154,0x8151,
    0x8173,0x0176,0x017C,0x8179,0x0168,0x816D,0x8167,0x0162,
    0x8123,0x0126,0x012C,0x8129,0x0138,0x813D,0x8137,0x0132,
    0x0110,0x8115,0x811F,0x011A,0x810B,0x010E,0x0104,0x8101,
    0x8303,0x0306,0x030C,0x8309,0x0318,0x831D,0x8317,0x0312,
    0x0330,0x8335,0x833F,0x033A,0x832B,0x032E,0x0324,0x8321,
    0x0360,0x8365,0x836F,0x036A,0x837B,0x037E,0x0374,0x8371,
    0x8353,0x0356,0x035C,0x8359,0x0348,0x834D,0x8347,0x0342,
    0x03C0,0x83C5,0x83CF,0x03CA,0x83DB,0x03DE,0x03D4,0x83D1,
    0x83F3,0x03F6,0x03FC,0x83F9,0x03E8,0x83ED,0x83E7,0x03E2,
    0x83A3,0x03A6,0x03AC,0x83A9,0x03B8,0x83BD,0x83B7,0x03B2,
    0x0390,0x8395,0x839F,0x039A,0x838B,0x038E,0x0384,0x8381,
    0x0280,0x8285,0x828F,0x028A,0x829B,0x029E,0x0294,0x8291,
    0x82B3,0x02B6,0x02BC,0x82B9,0x02A8,0x82AD,0x82A7,0x02A2,
    0x82E3,0x02E6,0x02EC,0x82E9,0x02F8,0x82FD,0x82F7,0x02F2,
    0x02D0,0x82D5,0x82DF,0x02DA,0x82CB,0x02CE,0x02C4,0x82C1,
    0x8243,0x0246,0x024C,0x8249,0x0258,0x825D,0x8257,0x0252,
    0x0270,0x8275,0x827F,0x027A,0x826B,0x026E,0x0264,0x8261,
    0x0220,0x8225,0x822F,0x022A,0x823B,0x023E,0x0234,0x8231,
    0x8213,0x0216,0x021C,0x8219,0x0208,0x820D,0x8207,0x0202,
};

static uint8_t flac_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++)
        crc = flac_crc8_table[crc ^ data[i]];
    return crc;
}

static uint16_t flac_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++)
        crc = (uint16_t)((crc << 8) ^ flac_crc16_table[(crc >> 8) ^ data[i]]);
    return crc;
}

static void flac_write_be16(FILE *fp, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)(v & 0xFF) };
    fwrite(b, 1, 2, fp);
}

static void flac_write_be24(FILE *fp, uint32_t v)
{
    uint8_t b[3] = { (uint8_t)((v >> 16) & 0xFF), (uint8_t)((v >> 8) & 0xFF),
                      (uint8_t)(v & 0xFF) };
    fwrite(b, 1, 3, fp);
}

static size_t flac_encode_utf8(uint32_t val, uint8_t *buf)
{
    if (val < 0x80) {
        buf[0] = (uint8_t)val;
        return 1;
    } else if (val < 0x800) {
        buf[0] = (uint8_t)(0xC0 | (val >> 6));
        buf[1] = (uint8_t)(0x80 | (val & 0x3F));
        return 2;
    } else if (val < 0x10000) {
        buf[0] = (uint8_t)(0xE0 | (val >> 12));
        buf[1] = (uint8_t)(0x80 | ((val >> 6) & 0x3F));
        buf[2] = (uint8_t)(0x80 | (val & 0x3F));
        return 3;
    } else if (val < 0x200000) {
        buf[0] = (uint8_t)(0xF0 | (val >> 18));
        buf[1] = (uint8_t)(0x80 | ((val >> 12) & 0x3F));
        buf[2] = (uint8_t)(0x80 | ((val >> 6) & 0x3F));
        buf[3] = (uint8_t)(0x80 | (val & 0x3F));
        return 4;
    } else if (val < 0x4000000) {
        buf[0] = (uint8_t)(0xF8 | (val >> 24));
        buf[1] = (uint8_t)(0x80 | ((val >> 18) & 0x3F));
        buf[2] = (uint8_t)(0x80 | ((val >> 12) & 0x3F));
        buf[3] = (uint8_t)(0x80 | ((val >> 6) & 0x3F));
        buf[4] = (uint8_t)(0x80 | (val & 0x3F));
        return 5;
    } else {
        buf[0] = (uint8_t)(0xFC | (val >> 30));
        buf[1] = (uint8_t)(0x80 | ((val >> 24) & 0x3F));
        buf[2] = (uint8_t)(0x80 | ((val >> 18) & 0x3F));
        buf[3] = (uint8_t)(0x80 | ((val >> 12) & 0x3F));
        buf[4] = (uint8_t)(0x80 | ((val >> 6) & 0x3F));
        buf[5] = (uint8_t)(0x80 | (val & 0x3F));
        return 6;
    }
}

#define FLAC_BLOCK_SIZE 4096

static int flac_sample_rate_code(uint32_t sr)
{
    switch (sr) {
    case  8000: return 4;
    case 16000: return 5;
    case 22050: return 6;
    case 24000: return 7;
    case 32000: return 8;
    case 44100: return 9;
    case 48000: return 10;
    case 96000: return 11;
    default:    return 0;
    }
}

static int write_flac(const float *data, int num_frames, float sample_rate,
                      const char *filepath, int bit_depth)
{
    if (bit_depth != 16 && bit_depth != 24) {
        FX_WARN("recorder: FLAC unsupported bit depth %d, using 16", bit_depth);
        bit_depth = 16;
    }

    const uint32_t channels = 1; /* mono */
    const uint32_t sr = (uint32_t)sample_rate;
    const uint32_t total_frames = (uint32_t)num_frames;
    const int bytes_per_sample = bit_depth / 8;

    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        FX_ERROR("recorder: cannot open %s for FLAC writing", filepath);
        return -1;
    }

    /* fLaC marker */
    fwrite("fLaC", 1, 4, fp);

    /* STREAMINFO metadata block (last block, type=0) */
    uint8_t meta_header[4];
    meta_header[0] = 0x80 | 0x00; /* last block=1, type=STREAMINFO(0) */
    meta_header[1] = 0x00;
    meta_header[2] = 0x00;
    meta_header[3] = 34;
    fwrite(meta_header, 1, 4, fp);

    /* STREAMINFO: 34 bytes */
    flac_write_be16(fp, FLAC_BLOCK_SIZE);  /* min block size */
    flac_write_be16(fp, FLAC_BLOCK_SIZE);  /* max block size */
    flac_write_be24(fp, 0);                /* min frame size (unknown) */
    flac_write_be24(fp, 0);                /* max frame size (unknown) */

    /* 20 bits sample rate + 3 bits (channels-1) + 5 bits (bps-1) + 36 bits total samples */
    uint64_t si_packed = 0;
    si_packed |= ((uint64_t)sr & 0xFFFFF) << 44;
    si_packed |= ((uint64_t)(channels - 1) & 0x7) << 41;
    si_packed |= ((uint64_t)(bit_depth - 1) & 0x1F) << 36;
    si_packed |= (uint64_t)total_frames & 0xFFFFFFFFFULL;

    uint8_t si_bytes[8];
    for (int i = 7; i >= 0; i--) {
        si_bytes[i] = (uint8_t)(si_packed & 0xFF);
        si_packed >>= 8;
    }
    fwrite(si_bytes, 1, 8, fp);

    /* MD5 signature: zeros (skip computation) */
    uint8_t md5[16] = {0};
    fwrite(md5, 1, 16, fp);

    /* Audio frames */
    uint32_t frames_written = 0;
    uint32_t frame_number = 0;

    size_t max_frame_bytes = 16 +
                             channels * (1 + (size_t)FLAC_BLOCK_SIZE * (size_t)bytes_per_sample) +
                             2;
    uint8_t *frame_buf = (uint8_t *)malloc(max_frame_bytes);
    if (!frame_buf) {
        FX_ERROR("recorder: failed to allocate FLAC frame buffer");
        fclose(fp);
        return -1;
    }

    while (frames_written < total_frames) {
        uint32_t block_size = FLAC_BLOCK_SIZE;
        if (frames_written + block_size > total_frames)
            block_size = total_frames - frames_written;

        size_t pos = 0;

        /* Frame header sync: 0xFFF8 */
        frame_buf[pos++] = 0xFF;
        frame_buf[pos++] = 0xF8;

        /* Block size code + sample rate code */
        int bs_code;
        int need_bs16 = 0;
        if (block_size == FLAC_BLOCK_SIZE) {
            bs_code = 0x0C; /* 4096 */
        } else {
            if (block_size > 256) {
                bs_code = 0x07; /* 16-bit blocksize-1 at end */
                need_bs16 = 1;
            } else {
                bs_code = 0x06; /* 8-bit blocksize-1 at end */
            }
        }

        int sr_code = flac_sample_rate_code(sr);
        frame_buf[pos++] = (uint8_t)((bs_code << 4) | sr_code);

        /* Channel assignment (4 bits) + sample size (3 bits) + reserved (1 bit) */
        /* Channel: 0x00 = 1 channel (mono, independent) */
        int bps_code;
        switch (bit_depth) {
        case 16: bps_code = 4; break;
        case 24: bps_code = 6; break;
        default: bps_code = 4; break;
        }
        frame_buf[pos++] = (uint8_t)(0x00 | (bps_code << 1));

        /* UTF-8 coded frame number */
        pos += flac_encode_utf8(frame_number, frame_buf + pos);

        /* Block size value at end of header (if needed) */
        if (bs_code == 0x06) {
            frame_buf[pos++] = (uint8_t)(block_size - 1);
        } else if (need_bs16) {
            frame_buf[pos++] = (uint8_t)((block_size - 1) >> 8);
            frame_buf[pos++] = (uint8_t)((block_size - 1) & 0xFF);
        }

        /* CRC-8 of header */
        frame_buf[pos] = flac_crc8(frame_buf, pos);
        pos++;

        /* Subframe: verbatim, 1 channel */
        const float *src = data + frames_written;

        /* Subframe header: verbatim, no wasted bits */
        frame_buf[pos++] = 0x02;

        for (uint32_t s = 0; s < block_size; s++) {
            float v = src[s];
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;

            if (bit_depth == 16) {
                int16_t sample = (int16_t)(v * 32767.0f);
                frame_buf[pos++] = (uint8_t)((uint16_t)sample >> 8);
                frame_buf[pos++] = (uint8_t)((uint16_t)sample & 0xFF);
            } else {
                int32_t sample = (int32_t)(v * 8388607.0f);
                frame_buf[pos++] = (uint8_t)((uint32_t)sample >> 16);
                frame_buf[pos++] = (uint8_t)(((uint32_t)sample >> 8) & 0xFF);
                frame_buf[pos++] = (uint8_t)((uint32_t)sample & 0xFF);
            }
        }

        /* CRC-16 footer */
        uint16_t crc16 = flac_crc16(frame_buf, pos);
        frame_buf[pos++] = (uint8_t)(crc16 >> 8);
        frame_buf[pos++] = (uint8_t)(crc16 & 0xFF);

        fwrite(frame_buf, 1, pos, fp);

        frames_written += block_size;
        frame_number++;
    }

    free(frame_buf);
    fclose(fp);

    FX_INFO("recorder: wrote FLAC %d-bit — %s (%u frames)",
            bit_depth, filepath, total_frames);
    return 0;
}

/* ── Stop: encode and write to disk ──────────────────────────── */

void fx_recorder_stop(void)
{
    if (!s_rec.active) return;
    s_rec.active = false;

    int num_frames = s_rec.write_pos;
    if (num_frames == 0) {
        FX_WARN("recorder: no audio recorded, skipping file write");
        free(s_rec.ring);
        s_rec.ring = NULL;
        return;
    }

    FX_INFO("recorder: encoding %d frames (%.1f sec)...",
            num_frames, (float)num_frames / s_rec.sample_rate);

    int result = -1;
    switch (s_rec.format) {
    case FX_RECORD_WAV_16:
        result = write_wav(s_rec.ring, num_frames, s_rec.sample_rate, s_rec.path, 16);
        break;
    case FX_RECORD_WAV_24:
        result = write_wav(s_rec.ring, num_frames, s_rec.sample_rate, s_rec.path, 24);
        break;
    case FX_RECORD_MP3_192:
        result = write_mp3(s_rec.ring, num_frames, s_rec.sample_rate, s_rec.path, 192);
        break;
    case FX_RECORD_MP3_320:
        result = write_mp3(s_rec.ring, num_frames, s_rec.sample_rate, s_rec.path, 320);
        break;
    case FX_RECORD_FLAC_16:
        result = write_flac(s_rec.ring, num_frames, s_rec.sample_rate, s_rec.path, 16);
        break;
    case FX_RECORD_FLAC_24:
        result = write_flac(s_rec.ring, num_frames, s_rec.sample_rate, s_rec.path, 24);
        break;
    default:
        FX_ERROR("recorder: unknown format %d", s_rec.format);
        break;
    }

    if (result != 0) {
        FX_ERROR("recorder: failed to write %s", s_rec.path);
    }

    free(s_rec.ring);
    s_rec.ring = NULL;
}
