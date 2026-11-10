/* music.c — deterministic native WAV music synthesis for the five moods.
 *
 * No samples are downloaded and no external audio library is required: the
 * soundtrack is generated from oscillators and envelopes in C, then written as
 * 44.1 kHz stereo 16-bit PCM.
 */
#include "as_media.h"
#include "as_core.h"

#include <math.h>
#include <string.h>

#include <glib/gstdio.h>

#define RATE 44100
#define TWO_PI (2.0 * G_PI)

typedef struct {
    gint notes[4][3];
    gint chords;
    gdouble chord_seconds;
    gdouble bpm;
    gboolean arpeggio;
    gboolean kick;
    gboolean hats;
    gboolean hit;
    gboolean clap;
    gdouble pad_gain;
    gdouble bass_gain;
} MoodSpec;

static gdouble midi_freq(gint midi)
{
    return 440.0 * pow(2.0, (midi - 69) / 12.0);
}

static gdouble frac(gdouble x)
{
    return x - floor(x);
}

static gdouble noise(gint i)
{
    return frac(sin((gdouble) i * 12.9898) * 43758.5453) * 2.0 - 1.0;
}

static MoodSpec spec_for(const gchar *mood)
{
    MoodSpec s;
    memset(&s, 0, sizeof(s));
    if (g_strcmp0(mood, "Electronic") == 0) {
        s.chords = 4;
        s.chord_seconds = 0.5;
        s.bpm = 120;
        s.arpeggio = TRUE;
        s.kick = TRUE;
        s.hats = TRUE;
        s.pad_gain = 0.10;
        s.bass_gain = 0.18;
        gint notes[4][3] = { { 57, 60, 64 }, { 53, 57, 60 },
                             { 48, 52, 55 }, { 55, 59, 62 } };
        memcpy(s.notes, notes, sizeof(notes));
    } else if (g_strcmp0(mood, "Dramatic") == 0) {
        s.chords = 4;
        s.chord_seconds = 2.0;
        s.bpm = 76;
        s.hit = TRUE;
        s.pad_gain = 0.20;
        s.bass_gain = 0.26;
        gint notes[4][3] = { { 50, 53, 57 }, { 46, 50, 53 },
                             { 48, 52, 55 }, { 45, 48, 52 } };
        memcpy(s.notes, notes, sizeof(notes));
    } else if (g_strcmp0(mood, "Ambient") == 0) {
        s.chords = 4;
        s.chord_seconds = 4.0;
        s.pad_gain = 0.22;
        s.bass_gain = 0.10;
        gint notes[4][3] = { { 57, 60, 64 }, { 55, 59, 62 },
                             { 53, 57, 60 }, { 52, 55, 59 } };
        memcpy(s.notes, notes, sizeof(notes));
    } else if (g_strcmp0(mood, "Motivational") == 0) {
        s.chords = 4;
        s.chord_seconds = 1.0;
        s.bpm = 120;
        s.arpeggio = TRUE;
        s.kick = TRUE;
        s.hats = TRUE;
        s.clap = TRUE;
        s.pad_gain = 0.12;
        s.bass_gain = 0.20;
        gint notes[4][3] = { { 60, 64, 67 }, { 55, 59, 62 },
                             { 57, 60, 64 }, { 53, 57, 60 } };
        memcpy(s.notes, notes, sizeof(notes));
    } else { /* Cinematic */
        s.chords = 4;
        s.chord_seconds = 2.5;
        s.bpm = 80;
        s.hit = TRUE;
        s.pad_gain = 0.22;
        s.bass_gain = 0.22;
        gint notes[4][3] = { { 45, 48, 52 }, { 41, 45, 48 },
                             { 48, 52, 55 }, { 43, 47, 50 } };
        memcpy(s.notes, notes, sizeof(notes));
    }
    return s;
}

const gchar *as_music_mood_for_styles(const gchar *voice_style,
                                      const gchar *visual_style)
{
    if (g_strcmp0(voice_style, "Dramatic") == 0)
        return "Dramatic";
    if (g_strcmp0(voice_style, "Energetic") == 0)
        return "Electronic";
    if (g_strcmp0(voice_style, "Motivational") == 0)
        return "Motivational";
    if (g_strcmp0(voice_style, "Documentary") == 0)
        return "Ambient";
    if (g_strcmp0(visual_style, "Sci-Fi") == 0 ||
        g_strcmp0(visual_style, "Fast-Paced Social Media") == 0)
        return "Electronic";
    if (g_strcmp0(visual_style, "Dark / Mystery") == 0)
        return "Dramatic";
    if (g_strcmp0(visual_style, "Minimal") == 0 ||
        g_strcmp0(visual_style, "Educational") == 0)
        return "Ambient";
    return "Cinematic";
}

gboolean as_music_synthesize(const gchar *mood, gdouble seconds,
                             const gchar *dest_path, GError **error)
{
    if (!dest_path || seconds <= 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_ARGUMENT,
                    "Invalid music synthesis arguments.");
        return FALSE;
    }
    if (seconds > 600)
        seconds = 600;
    MoodSpec spec = spec_for(mood);
    gsize frames = (gsize) (seconds * RATE);
    gsize data_bytes = frames * 2 * 2;
    guint8 *wav = g_malloc0(44 + data_bytes);

    memcpy(wav, "RIFF", 4);
    guint32 riff_size = (guint32) (36 + data_bytes);
    memcpy(wav + 4, &riff_size, 4);
    memcpy(wav + 8, "WAVEfmt ", 8);
    guint32 fmt_size = 16;
    memcpy(wav + 16, &fmt_size, 4);
    guint16 format = 1;
    memcpy(wav + 20, &format, 2);
    guint16 channels = 2;
    memcpy(wav + 22, &channels, 2);
    guint32 rate = RATE;
    memcpy(wav + 24, &rate, 4);
    guint32 byte_rate = RATE * 4;
    memcpy(wav + 28, &byte_rate, 4);
    guint16 align = 4;
    memcpy(wav + 32, &align, 2);
    guint16 bits = 16;
    memcpy(wav + 34, &bits, 2);
    memcpy(wav + 36, "data", 4);
    guint32 data_size = (guint32) data_bytes;
    memcpy(wav + 40, &data_size, 4);

    gint16 *samples = (gint16 *) (wav + 44);
    gdouble beat = 60.0 / spec.bpm;
    for (gsize i = 0; i < frames; i++) {
        gdouble t = (gdouble) i / RATE;
        gint chord = (gint) (t / spec.chord_seconds) % spec.chords;
        gdouble local = fmod(t, spec.chord_seconds);
        gdouble env = 1.0;
        if (local < 0.08)
            env = local / 0.08;
        gdouble chord_fade = 1.0;
        if (local > spec.chord_seconds - 0.12)
            chord_fade = (spec.chord_seconds - local) / 0.12;
        gdouble pad = 0;
        for (gint n = 0; n < 3; n++) {
            gdouble f = midi_freq(spec.notes[chord][n]);
            pad += sin(TWO_PI * f * t) + 0.35 * sin(TWO_PI * f * 1.005 * t) +
                   0.20 * sin(TWO_PI * f * 0.5 * t);
        }
        pad *= spec.pad_gain * env * chord_fade / 3.0;

        gdouble bass = sin(TWO_PI * midi_freq(spec.notes[chord][0] - 12) * t) *
                       spec.bass_gain * env;

        gdouble arp = 0;
        if (spec.arpeggio) {
            gdouble step = 0.125;
            gint step_index = (gint) (t / step);
            gint note = spec.notes[chord][step_index % 3] + 12;
            gdouble phase = fmod(t, step);
            gdouble aenv = exp(-7.0 * phase);
            gdouble tone = sin(TWO_PI * midi_freq(note) * t);
            tone += 0.25 * sin(TWO_PI * midi_freq(note) * 2 * t);
            arp = tone * aenv * 0.16;
        }

        gdouble drums = 0;
        if (spec.kick) {
            gdouble phase = fmod(t, beat);
            if (phase < 0.16) {
                gdouble f = 48.0 + 120.0 * exp(-35.0 * phase);
                drums += sin(TWO_PI * f * phase) * exp(-18.0 * phase) * 0.55;
            }
        }
        if (spec.hats) {
            gdouble phase = fmod(t, beat * 0.5);
            if (phase < 0.05)
                drums += noise((gint) i) * exp(-70.0 * phase) * 0.10;
        }
        if (spec.hit) {
            gdouble phase = fmod(t, 2.0);
            if (phase < 0.5) {
                gdouble f = 58.0 + 50.0 * exp(-8.0 * phase);
                drums += sin(TWO_PI * f * phase) * exp(-6.0 * phase) * 0.38;
            }
        }
        if (spec.clap) {
            gdouble phase = fmod(t, beat * 4.0);
            if ((phase < 0.05 && fmod(t, beat * 2.0) < beat) ||
                (phase > beat * 2.0 && phase < beat * 2.0 + 0.05))
                drums += noise((gint) i + 17) * exp(-55.0 * phase) * 0.22;
        }

        gdouble fade = 1.0;
        if (t < 0.6)
            fade = t / 0.6;
        if (seconds - t < 1.0)
            fade = MIN(fade, (seconds - t) / 1.0);
        gdouble left = (pad + bass + arp + drums) * fade;
        gdouble right = (pad * 0.96 + bass + arp * 1.05 + drums) * fade;
        left = tanh(left * 1.2) * 0.85;
        right = tanh(right * 1.2) * 0.85;
        samples[i * 2] = (gint16) CLAMP(left, -1.0, 1.0) * 32767;
        samples[i * 2 + 1] = (gint16) CLAMP(right, -1.0, 1.0) * 32767;
    }

    gboolean ok = g_file_set_contents(dest_path, (const gchar *) wav,
                                      (gssize) (44 + data_bytes), error);
    g_free(wav);
    if (ok)
        g_chmod(dest_path, 0644);
    return ok;
}
