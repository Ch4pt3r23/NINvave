/* SPDX-License-Identifier: GPL-3.0-only */
/* NINvave: a groovebox for the M-VAVE FM-1 made of Nine Inch Nails multitracks (Pretty Hate Machine: SOURCES.md),
 * on Dinghy's platform. Seven tracks of one bar (16 steps): KICK SNARE HAT PERC LOOP BASS SYNTH. Every kit and
 * every preset pattern is cut from the songs' own stems by tools/nin_extract.py (firmware/gen/nin_data.h).
 *
 *   PRESETS    the pattern: the NIN presets (HLAH A..), then the user slots U1..U8
 *   SELECT     the track (or the black keys in STEP mode)
 *   ALGORITHM  the tempo (a MIDI clock in takes over)
 *   KNOB1..4   the track's SOUND (any of the kits), PITCH, DECAY, LEVEL
 *   SEQ        the keys: STEP (white keys = the 16 steps of the track; black keys 1..7 = the tracks, 8 mute, 9 clear)
 *              or PLAY (all keys play the track chromatically; with REC on and running, what you play is recorded)
 *   PLAY REC   start / stop, record
 *   FX         the grit on the mix: off, CRUSH, DRIVE
 *   OCT- OCT+  the octave of the keys in PLAY mode
 *   SAVE       the pattern to its user slot (a NIN preset: to the first free one)
 *   MIDI in    notes play the track as the keys do; a clock (with START / STOP) drives the sequencer
 *
 * Rules of the road (no FPU, no libgcc): integers only, no float, no 64-bit division. docs/RULES.md. */
#define APP_NAME "NINVAVE"
#define STORE_MAGIC 0x564E494Eu                 /* "NINV": never another Dinghy fork's data */
static void app_idle(void);
#define LCD_IDLE app_idle
#include "platform.c"
#include "nin_data.h"

#define NT NIN_NTRACK
enum { T_KICK, T_SNARE, T_HAT, T_PERC, T_LOOP, T_BASS, T_SYNTH };
static const char *const TRACK_NAME[NT] = {"KICK", "SNARE", "HAT", "PERC", "LOOP", "BASS", "SYNTH"};
static const uint8_t DEF_LEVEL[NT] = {14, 13, 10, 11, 10, 13, 11};
static int melodic(uint32_t t) { return t >= T_BASS; }

/* ---- the pattern: written by the main loop, read by the audio ISR (bytes and halfwords: a torn read is one step) */
typedef struct {
    uint16_t mask[NT];                          /* bit s: step s plays */
    uint8_t note[NT][16];                       /* BASS, SYNTH: the MIDI note of each step */
    uint8_t sound[NT];                          /* NIN_SOUND index */
    int8_t pitch[NT];                           /* semitones */
    uint8_t decay[NT], level[NT];               /* 0..15 */
    uint8_t mute, song;                         /* mute: bit t */
    uint16_t bpm10;                             /* tempo x 10 */
    char name[8];
} pat_t;
static pat_t pat;

static const uint32_t SEMI_Q16[12] = {65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715};
static const int32_t DECAY_Q15[16] = {30142, 31166, 31616, 31957, 32187, 32315, 32428, 32495,   /* per 16 frames: */
                                      32563, 32604, 32642, 32666, 32686, 32713, 32735, 32768};  /* 30 ms .. 2.5 s, 15 = none */
static const int32_t LEVEL_Q15[16] = {0, 263, 796, 1523, 2413, 3449, 4617, 5908, 7315, 8832, 10454, 12176, 13995, 15907, 17910, 20000};

static uint32_t ratio_q16(int32_t semi)         /* 2^(semi/12), Q16, semi -48..48 */
{
    int32_t o;
    if (semi < -48) semi = -48;
    if (semi > 48) semi = 48;
    o = (semi + 48) / 12 - 4;
    semi -= o * 12;
    return o < 0 ? SEMI_Q16[semi] >> -o : SEMI_Q16[semi] << o;
}
static uint32_t song_bpm10(uint32_t s)
{
    uint32_t i;
    for (i = 0; i < NIN_NPRESET; i++)
        if (NIN_PRESET[i].song == s) return NIN_PRESET[i].bpm10;
    return 1200;
}

/* ---- the sound (runs in the audio ISR: keep it short, no flash writes, no screen) */
static int16_t ulaw[256];                       /* NIN_ULAW in RAM */
static struct {
    volatile uint32_t req, note;                /* a live hit: the main loop sets note, then req++ */
    uint32_t seen;
    const uint8_t *p;
    uint32_t len, pos, step;                    /* pos, step: Q16 samples */
    int32_t env, gain, dec;
    uint32_t on;
} vc[NT];
static volatile uint32_t run, seq_reset, ext_mode, ext_req, cur_step = 15, steps_played, fx;
static uint32_t ext_seen, seq_pos, nxt_q8;

static void voice_start(uint32_t t, uint32_t note)   /* note 0: the sound as it is (a drum step) */
{
    const nin_sound_t *s = &NIN_SOUND[pat.sound[t] % NIN_NSOUND];
    int32_t semi = pat.pitch[t] + (note ? (int32_t)note - (melodic(t) ? (int32_t)s->root : 60) : 0);
    uint32_t st = (uint32_t)(((uint64_t)s->step * ratio_q16(semi)) >> 16);
    if (s->track == T_LOOP) {                   /* a loop keeps to the tempo: faster tempo, faster loop */
        uint32_t sb = song_bpm10(s->song);
        st = st * pat.bpm10 / (sb ? sb : 1200u);
    }
    vc[t].p = NIN_PCM + s->off;
    vc[t].len = s->len;
    vc[t].pos = 0;
    vc[t].step = st;
    vc[t].env = 32767;
    vc[t].gain = LEVEL_Q15[pat.level[t] & 15u];
    vc[t].dec = DECAY_Q15[pat.decay[t] & 15u];
    vc[t].on = 1;
}

static void voice_run(uint32_t t, int32_t *out, uint32_t a, uint32_t b)
{
    uint32_t i, pos = vc[t].pos, step = vc[t].step, len = vc[t].len;
    const uint8_t *p = vc[t].p;
    int32_t env = vc[t].env, g = (vc[t].gain * env) >> 15;
    if (!vc[t].on) return;
    for (i = a; i < b; i++) {
        uint32_t k = pos >> 16;
        int32_t s0, s1, s;
        if ((i & 15u) == 0) {                   /* the envelope: once every 16 frames */
            env = (env * vc[t].dec) >> 15;
            g = (vc[t].gain * env) >> 15;
        }
        if (k + 1u >= len || env < 8) { vc[t].on = 0; break; }
        s0 = ulaw[p[k]];
        s1 = ulaw[p[k + 1u]];
        s = s0 + (((s1 - s0) * (int32_t)((pos & 0xFFFFu) >> 2)) >> 14);
        s = (s * g) >> 15;
        out[2u * i] += s;
        out[2u * i + 1u] += s;
        pos += step;
    }
    vc[t].pos = pos;
    vc[t].env = env;
}

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t t, i, fire = n, s = 0;
    for (t = 0; t < NT; t++)                    /* live hits: at the block's start */
        if (vc[t].req != vc[t].seen) { vc[t].seen = vc[t].req; voice_start(t, vc[t].note); }
    if (seq_reset) { seq_reset = 0; seq_pos = 0; nxt_q8 = 0; }
    if (run) {
        if (ext_mode) {                         /* a MIDI clock: a step every 6 clocks, at the block's start */
            if (ext_req != ext_seen) { ext_seen = ext_req; fire = 0; }
        } else {                                /* our own: sample exact; a 16th is 44100 * 60 / (4 * bpm) frames */
            if ((nxt_q8 >> 8) < n) { fire = nxt_q8 >> 8; nxt_q8 += 677376000u / (pat.bpm10 < 300u ? 300u : pat.bpm10); }
            nxt_q8 -= n << 8;
        }
    }
    if (fire < n) {
        s = seq_pos;
        seq_pos = (seq_pos + 1u) & 15u;
        cur_step = s;
        steps_played++;
    }
    for (t = 0; t < NT; t++) {
        if (fire < n && (pat.mask[t] >> s & 1u) && !(pat.mute >> t & 1u)) {
            voice_run(t, out, 0, fire);
            voice_start(t, melodic(t) ? pat.note[t][s] : 0u);
            voice_run(t, out, fire, n);
        } else
            voice_run(t, out, 0, n);
    }
    if (fx == 1u) {                             /* CRUSH: half the rate, 6 bits */
        for (i = 0; i < n; i += 2u) {
            int32_t l = out[2u * i] >> 10 << 10, r = out[2u * i + 1u] >> 10 << 10;
            out[2u * i] = out[2u * i + 2u] = l;
            out[2u * i + 1u] = out[2u * i + 3u] = r;
        }
    } else if (fx == 2u) {                      /* DRIVE: twice the gain into a soft clip */
        for (i = 0; i < 2u * n; i++) {
            int32_t x = out[i] * 2;
            x = x > 32767 ? 32767 : x < -32767 ? -32767 : x;
            x -= ((((x * x) >> 15) * x) >> 15) / 3;   /* x - x^3/3: +-21845 */
            out[i] = x + (x >> 1);
        }
    }
}

/* ---- presets and user slots */
static uint32_t sound_of(uint32_t song, uint32_t t)
{
    uint32_t i;
    for (i = 0; i < NIN_NSOUND; i++)
        if (NIN_SOUND[i].song == song && NIN_SOUND[i].track == t) return i;
    return 0;
}
static void name_set(char *d, const char *s)
{
    uint32_t i;
    for (i = 0; i < 7u && s[i]; i++) d[i] = s[i];
    for (; i < 8u; i++) d[i] = 0;
}
static void load_nin(uint32_t i)
{
    const nin_preset_t *p = &NIN_PRESET[i];
    pat_t q;
    uint32_t t, s;
    memset(&q, 0, sizeof q);
    for (t = 0; t < 5u; t++) q.mask[t] = p->mask[t];
    for (s = 0; s < 16u; s++) {
        if (p->bass[s]) { q.mask[T_BASS] |= (uint16_t)(1u << s); q.note[T_BASS][s] = p->bass[s]; }
        if (p->synth[s]) { q.mask[T_SYNTH] |= (uint16_t)(1u << s); q.note[T_SYNTH][s] = p->synth[s]; }
    }
    for (t = 0; t < NT; t++) {
        q.sound[t] = (uint8_t)sound_of(p->song, t);
        q.decay[t] = 15;
        q.level[t] = DEF_LEVEL[t];
    }
    q.song = p->song;
    q.bpm10 = p->bpm10;
    name_set(q.name, p->name);
    memcpy(&pat, &q, sizeof q);
}
#define NUSER 8u
static uint32_t user_ok;                        /* bit u: user slot u holds a pattern */
static int load_user(uint32_t u)
{
    pat_t q;
    if (blob_load(u, &q, sizeof q) || q.bpm10 < 300u || q.bpm10 > 3000u) return -1;
    memcpy(&pat, &q, sizeof q);
    return 0;
}

/* ---- the screen */
#define N_RED RGB(224, 32, 32)
#define N_GREY RGB(200, 200, 200)
#define N_DIM RGB(90, 90, 90)
#define N_CELL RGB(38, 38, 38)
#define N_BEAT RGB(62, 62, 62)

static char *put(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static char *put_num(char *d, int32_t v, int dec)   /* v with dec (0 or 1) decimals */
{
    char t[12];
    uint32_t u = (uint32_t)(v < 0 ? -v : v), k = 0;
    if (v < 0) *d++ = '-';
    do { t[k++] = (char)('0' + u % 10u); u /= 10u; if (dec && k == 1u) t[k++] = '.'; } while (u || (dec && k < 3u));
    while (k) *d++ = t[--k];
    *d = 0;
    return d;
}

static void draw_header(uint32_t slot, uint32_t bpm10, uint32_t state)
{
    char s[40], *p = s;
    if (slot < NIN_NPRESET) put(s, pat.name);
    else { p = put(s, "USER "); *p++ = (char)('1' + slot - NIN_NPRESET); *p = 0;
           if (!(user_ok >> (slot - NIN_NPRESET) & 1u)) put(p, " NEW"); }
    draw_text_box(0, 24, 240, &FONT_L, s, C_WHITE, 1);
    p = put(s, slot < NIN_NPRESET ? NIN_SONG[pat.song] : "");
    if (p != s) p = put(p, "  ");
    p = put_num(p, (int32_t)bpm10, 1);
    p = put(p, state & 2u ? " BPM EXT" : " BPM");
    draw_text_box(0, 60, 240, &FONT_S, s, N_DIM, 1);
    draw_text_box(0, 4, 240, &FONT_S, state & 1u ? (state & 4u ? "NINVAVE  REC" : "NINVAVE  PLAY") : "NINVAVE",
                  state & 4u ? N_RED : N_GREY, 1);
}

static void draw_grid(uint32_t sel, uint32_t step, uint32_t running)
{
    uint32_t t, s;
    cv_begin(240, 64, C_BLACK);
    for (t = 0; t < NT; t++)
        for (s = 0; s < 16u; s++) {
            int on = pat.mask[t] >> s & 1u, here = running && s == step;
            uint16_t c = on ? (pat.mute >> t & 1u ? N_DIM : t == sel ? N_RED : N_GREY) : s % 4u == 0 ? N_BEAT : N_CELL;
            if (here) c = on ? C_WHITE : N_DIM;
            cv_rect(8 + (int32_t)s * 14, (int32_t)t * 9 + 1, 12, 7, c);
        }
    cv_rect(0, (int32_t)sel * 9 + 1, 4, 7, N_RED);      /* the selected track */
    cv_blit(0, 84);
}

static void draw_track(uint32_t sel)
{
    char s[40], *p;
    const nin_sound_t *snd = &NIN_SOUND[pat.sound[sel] % NIN_NSOUND];
    p = put(s, TRACK_NAME[sel]);
    if (pat.mute >> sel & 1u) put(p, " MUTE");
    draw_text_box(0, 154, 240, &FONT_L, s, pat.mute >> sel & 1u ? N_DIM : N_RED, 1);
    p = put(s, NIN_SONG_TAG[snd->song]);
    p = put(p, " ");
    p = put(p, TRACK_NAME[snd->track]);
    p = put(p, "  P");
    if (pat.pitch[sel] >= 0) *p++ = '+';
    p = put_num(p, pat.pitch[sel], 0);
    p = put(p, " D");
    p = put_num(p, pat.decay[sel], 0);
    p = put(p, " L");
    put_num(p, pat.level[sel], 0);
    draw_text_box(0, 190, 240, &FONT_S, s, N_GREY, 1);
}

static void draw_status(uint32_t play_mode, int32_t oct, const char *msg)
{
    static const char *const FX[3] = {"", "  CRUSH", "  DRIVE"};
    char s[40], *p;
    if (msg) { draw_text_box(0, 216, 240, &FONT_S, msg, N_RED, 1); return; }
    p = put(s, play_mode ? "PLAY KEYS  OCT" : "STEP KEYS");
    if (play_mode) { *p++ = oct < 0 ? '-' : '+'; p = put_num(p, oct < 0 ? -oct : oct, 0); }
    put(p, FX[fx % 3u]);
    draw_text_box(0, 216, 240, &FONT_S, s, N_DIM, 1);
}

/* ---- MIDI in: from the main loop, and from app_idle while the screen draws (idle_ok) */
static struct { uint8_t slot, fx, oct, pad; } set = {0, 0, 0, 0};   /* what survives a power-off */
static uint32_t idle_ok, sel = T_KICK, rec, clk_n, clk_t0, clk_bpm10, clk_last;
static uint32_t step_ms_at, last_step_seen = ~0u, pat_ver;

static void live(uint32_t note)                 /* play the selected track; with REC on and running: record it */
{
    vc[sel].note = note;
    vc[sel].req++;
    if (rec && run) {
        uint32_t s = cur_step, len_ms = 150000u / (pat.bpm10 ? pat.bpm10 : 1200u);
        if (fm1_ms - step_ms_at > len_ms / 2u) s = (s + 1u) & 15u;   /* nearer the next step */
        pat.mask[sel] |= (uint16_t)(1u << s);
        if (melodic(sel)) pat.note[sel][s] = (uint8_t)note;
        pat_ver++;
    }
}

static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2) != 0) {
        uint32_t ty = st & 0xF0u;
        if (st == 0xF8u) {                      /* clock: 24 a quarter, a step every 6 */
            if (clk_n % 24u == 0u) {
                uint32_t ms = fm1_ms - clk_t0;
                if (clk_n && ms) clk_bpm10 = (600000u + ms / 2u) / ms;
                clk_t0 = fm1_ms;
            }
            if (ext_mode && run && clk_n % 6u == 0u) ext_req++;
            clk_n++;
            clk_last = fm1_ms;
            continue;
        }
        if (st == 0xFAu) { ext_mode = 1; clk_n = 0; seq_reset = 1; run = 1; continue; }   /* START */
        if (st == 0xFBu) { ext_mode = 1; run = 1; continue; }                             /* CONTINUE */
        if (st == 0xFCu) { run = 0; continue; }                                           /* STOP */
        if (ty == 0x90u && d2) live(d1);
    }
}
static void app_idle(void)
{
    if (idle_ok) midi_in();
}

static void app_main(void)
{
    uint32_t prev = 0, k, t_change = 0, dirty = 0, slot, play_mode = 0, msg_until = 0;
    uint32_t hdr_drawn = ~0u, grid_drawn = ~0u, track_drawn = ~0u, status_drawn = ~0u;
    const char *msg = 0;
    uint8_t held[27] = {0};
    for (k = 0; k < 256u; k++) ulaw[k] = NIN_ULAW[k];
    if (settings_load(&set, sizeof set) || set.slot >= NIN_NPRESET + NUSER || set.fx > 2u || (int8_t)set.oct < -2 || (int8_t)set.oct > 2)
        set.slot = 0, set.fx = 0, set.oct = 0;
    for (k = 0; k < NUSER; k++) {
        pat_t q;
        if (!blob_load(k, &q, sizeof q)) user_ok |= 1u << k;
    }
    slot = set.slot;
    fx = set.fx;
    if (slot >= NIN_NPRESET && load_user(slot - NIN_NPRESET)) slot = 0;
    if (slot < NIN_NPRESET) load_nin(slot);

    lcd_fill(0, 0, 240, 240, C_BLACK);

    for (;;) {
        uint32_t keys, down, up, rel, b, w, st;
        int32_t oct = (int8_t)set.oct, e;
        platform_poll();

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0, w = 0; k < 27u; k++) {
            static const uint32_t BLACK = 0x252A52Au;   /* keys 1 3 5 8 10 13 15 17 20 22 25 */
            uint32_t is_black = BLACK >> k & 1u, idx = is_black ? 0u : w;
            if (is_black) {                     /* the black key's number, 0..10 */
                uint32_t j, n = 0;
                for (j = 0; j < k; j++) n += BLACK >> j & 1u;
                idx = n;
            } else
                w++;
            if (down >> k & 1u) {
                if (play_mode) {
                    held[k] = (uint8_t)((int32_t)53 + (int32_t)k + 12 * oct);
                    live(held[k]);
                    midi_send(0x90u, held[k], 100);
                } else if (!is_black) {         /* a step of the track */
                    pat.mask[sel] ^= (uint16_t)(1u << idx);
                    if (melodic(sel) && !pat.note[sel][idx]) pat.note[sel][idx] = NIN_SOUND[pat.sound[sel] % NIN_NSOUND].root;
                    pat_ver++;
                } else if (idx < NT) {          /* a track: select it, hear it */
                    sel = idx;
                    vc[sel].note = 0;
                    vc[sel].req++;
                } else if (idx == 7u) { pat.mute ^= (uint8_t)(1u << sel); pat_ver++; }
                else if (idx == 8u) { pat.mask[sel] = 0; pat_ver++; }
            }
            if (up >> k & 1u && play_mode && held[k]) { midi_send(0x80u, held[k], 0); held[k] = 0; }
        }

        b = fm1_input_edges(&rel);              /* buttons pressed since the last call (bit = button id) */
        if (b & (1u << BTN_PLAY)) { if (run) run = 0; else { ext_mode = 0; seq_reset = 1; run = 1; } }
        if (b & (1u << BTN_REC)) rec = !rec;
        if (b & (1u << BTN_SEQ)) play_mode = !play_mode;
        if (b & (1u << BTN_FX)) { fx = (fx + 1u) % 3u; set.fx = (uint8_t)fx; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_DOWN) && oct > -2) { set.oct = (uint8_t)(oct - 1); dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && oct < 2) { set.oct = (uint8_t)(oct + 1); dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_SAVE)) {
            uint32_t u = slot >= NIN_NPRESET ? slot - NIN_NPRESET : NUSER;
            if (u == NUSER)
                for (u = 0; u < NUSER && user_ok >> u & 1u; u++) {}
            if (u == NUSER) u = 0;              /* all full: the first */
            name_set(pat.name, "USER");
            if (!blob_save(u, &pat, sizeof pat)) {   /* (an erase: the sound stops for ~50 ms) */
                static char m[16];
                char *p = put(m, "SAVED U");
                *p++ = (char)('1' + u);
                *p = 0;
                user_ok |= 1u << u;
                slot = NIN_NPRESET + u;
                set.slot = (uint8_t)slot;
                msg = m;
            } else
                msg = "SAVE FAILED";
            msg_until = fm1_ms + 1500u;
            dirty = 1;
            t_change = fm1_ms;
        }
        if ((e = fm1_enc_take(ENC_PRESETS)) != 0) {
            int32_t n = (int32_t)slot + e, all = (int32_t)(NIN_NPRESET + NUSER);
            slot = (uint32_t)((n % all + all) % all);
            if (slot < NIN_NPRESET) load_nin(slot);
            else load_user(slot - NIN_NPRESET);  /* an empty slot: the pattern stays, SAVE puts it there */
            set.slot = (uint8_t)slot;
            dirty = 1;
            t_change = fm1_ms;
            pat_ver++;
        }
        if ((e = fm1_enc_take(ENC_SELECT)) != 0) sel = (uint32_t)(((int32_t)sel + e) % (int32_t)NT + (int32_t)NT) % NT;
        if ((e = fm1_enc_take(ENC_ALGORITHM)) != 0) {
            int32_t v = (int32_t)pat.bpm10 + 10 * e;
            pat.bpm10 = (uint16_t)(v < 400 ? 400 : v > 2400 ? 2400 : v);
        }
        if ((e = fm1_enc_take(ENC_KNOB1)) != 0)
            pat.sound[sel] = (uint8_t)((((int32_t)pat.sound[sel] + e) % (int32_t)NIN_NSOUND + (int32_t)NIN_NSOUND) % NIN_NSOUND);
        if ((e = fm1_enc_take(ENC_KNOB2)) != 0) {
            int32_t v = pat.pitch[sel] + e;
            pat.pitch[sel] = (int8_t)(v < -24 ? -24 : v > 24 ? 24 : v);
        }
        if ((e = fm1_enc_take(ENC_KNOB3)) != 0) {
            int32_t v = pat.decay[sel] + e;
            pat.decay[sel] = (uint8_t)(v < 0 ? 0 : v > 15 ? 15 : v);
        }
        if ((e = fm1_enc_take(ENC_KNOB4)) != 0) {
            int32_t v = pat.level[sel] + e;
            pat.level[sel] = (uint8_t)(v < 0 ? 0 : v > 15 ? 15 : v);
        }

        midi_in();
        if (ext_mode && fm1_ms - clk_last > 2000u) { ext_mode = 0; clk_bpm10 = 0; }   /* the clock has stopped coming */
        if (cur_step != last_step_seen) { last_step_seen = cur_step; step_ms_at = fm1_ms; }
        if (msg && fm1_ms > msg_until) msg = 0;

        oct = (int8_t)set.oct;
        for (k = 0, w = 0; k < 27u; k++) {      /* the LEDs: the steps (dim) and the playhead in STEP mode, held keys in PLAY */
            static const uint32_t BLACK = 0x252A52Au;
            uint32_t on = 0, glow = 0;
            if (play_mode) on = keys >> k & 1u;
            else if (!(BLACK >> k & 1u)) { glow = pat.mask[sel] >> w & 1u; on = run && cur_step == w; w++; }
            else {
                uint32_t j, n = 0;
                for (j = 0; j < k; j++) n += BLACK >> j & 1u;
                on = n == sel;
            }
            fm1_led_key(KEY_LED(k), (int)on);
            fm1_led_key_glow(KEY_LED(k), (int)glow);
        }
        fm1_led_key(BTN_PLAY, (int)run);
        fm1_led_key(BTN_REC, (int)rec);
        fm1_led_key(BTN_SEQ, (int)!play_mode);
        fm1_led_key(BTN_FX, fx != 0u);
        fm1_led_key(BTN_OCT_DOWN, oct < 0);
        fm1_led_key(BTN_OCT_UP, oct > 0);

        idle_ok = 1;                            /* the drawing below may read MIDI meanwhile (LCD_IDLE) */
        {
            uint32_t bpm = ext_mode && clk_bpm10 ? clk_bpm10 : pat.bpm10;
            st = slot | bpm << 8 | (run ? 1u : 0u) << 24 | (ext_mode ? 2u : 0u) << 24 | (rec ? 4u : 0u) << 24 | (user_ok << 27);
            if (st != hdr_drawn) { hdr_drawn = st; draw_header(slot, bpm, (run ? 1u : 0u) | (ext_mode ? 2u : 0u) | (rec ? 4u : 0u)); }
        }
        st = pat_ver << 8 | sel << 5 | (run ? cur_step : 31u);
        if (st != grid_drawn) { grid_drawn = st; draw_grid(sel, cur_step, run); }
        st = sel | (uint32_t)pat.sound[sel] << 3 | (uint32_t)(uint8_t)pat.pitch[sel] << 10 | (uint32_t)pat.decay[sel] << 18
           | (uint32_t)pat.level[sel] << 22 | (pat.mute >> sel & 1u) << 26;
        if (st != track_drawn) { track_drawn = st; draw_track(sel); }
        st = msg ? 0xFFFFFFF0u ^ (uint32_t)(uintptr_t)msg : play_mode | (uint32_t)(oct + 2) << 1 | fx << 4;
        if (st != status_drawn) { status_drawn = st; draw_status(play_mode, oct, msg); }
        idle_ok = 0;

        if (dirty && !run && fm1_ms - t_change > 2000u) {   /* 2 s after the change, when stopped (an erase stalls the sound) */
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
