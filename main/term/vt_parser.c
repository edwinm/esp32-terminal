// vt_parser.c — Paul Williams' DEC ANSI parser state machine.
//
// Written as a switch rather than a transition table: at this size it is just
// as fast and far easier to follow when you are trying to work out why `less`
// looks wrong.
//
// Two deliberate deviations from the canonical DAG, both required:
//
//   1. UTF-8 decoding happens in front of GROUND, not inside it. Bytes
//      0x80..0x9F from a UTF-8 host are continuation bytes; we do not implement
//      8-bit C1 at all.
//   2. ':' (0x3A) is a sub-parameter separator, not an error. The canonical DAG
//      routes it to CSI_IGNORE, which silently discards `CSI 38:2::R:G:B m` —
//      i.e. all truecolour under tmux, kitty and libvte.
#include <string.h>

#include "term.h"
#include "utf8.h"
#include "vt_parser.h"

enum {
    S_GROUND = 0,
    S_ESCAPE,
    S_ESC_INT,        // ESC with intermediate bytes collected
    S_CSI_ENTRY,
    S_CSI_PARAM,
    S_CSI_INT,
    S_CSI_IGNORE,
    S_OSC,            // OSC payload, terminated by BEL or ST
    S_STRING,         // DCS/SOS/PM/APC payload: consumed and discarded
    S_STRING_ESC,     // saw ESC inside a string; '\' terminates it
};

typedef struct {
    uint8_t  state;
    int16_t  params[VT_MAX_PARAMS];
    uint8_t  nparams;
    bool     param_pending;      // digits have been accumulated into params[n-1]
    bool     sep_pending;        // a separator was seen and no digits followed
    uint32_t submask;            // bit i: params[i] followed a ':'
    bool     string_is_osc;      // distinguishes OSC from DCS/SOS/PM/APC
    uint8_t  intermediate;       // last intermediate byte, 0 if none
    uint8_t  priv;               // '?' '>' '<' '=' or 0
    char     osc[VT_OSC_MAX];
    int      osc_len;
    utf8_state_t utf8;
} vt_state_t;

static vt_state_t s;

void vt_parser_reset(void)
{
    memset(&s, 0, sizeof(s));
    s.state = S_GROUND;
    utf8_reset(&s.utf8);
}

// --- Parameter handling -----------------------------------------------------

static void params_clear(void)
{
    s.nparams = 0;
    s.param_pending = false;
    s.sep_pending = false;
    s.submask = 0;
    s.intermediate = 0;
    s.priv = 0;
    memset(s.params, 0, sizeof(s.params));
}

static void param_digit(uint8_t c)
{
    if (!s.param_pending) {
        // Overflowing the parameter array must not abort the sequence — real
        // emitters occasionally exceed 32 and losing the whole CSI is worse
        // than losing the tail.
        if (s.nparams >= VT_MAX_PARAMS) return;
        s.params[s.nparams] = 0;
        s.nparams++;
        s.param_pending = true;
        s.sep_pending = false;
    }
    if (s.nparams == 0 || s.nparams > VT_MAX_PARAMS) return;
    int16_t *p = &s.params[s.nparams - 1];
    if (*p < 0) *p = 0;
    int32_t v = (int32_t)*p * 10 + (c - '0');
    *p = (int16_t)(v > 16383 ? 16383 : v);      // clamp, don't overflow
}

static void param_separator(bool colon)
{
    if (!s.param_pending && s.nparams < VT_MAX_PARAMS) {
        // No digits since the last separator: an empty (default) parameter,
        // as in `CSI ;5m` or the `38:2::R:G:B` colour-space-id slot.
        s.params[s.nparams] = -1;
        s.nparams++;
    }
    s.param_pending = false;
    s.sep_pending = true;
    if (colon && s.nparams < VT_MAX_PARAMS) s.submask |= 1u << s.nparams;
}

// A trailing separator (`CSI 5;m`) means one more empty parameter.
static void params_finish(void)
{
    if (s.sep_pending && s.nparams < VT_MAX_PARAMS) {
        s.params[s.nparams] = -1;
        s.nparams++;
    }
    s.sep_pending = false;
    s.param_pending = false;
}

// Fetch parameter `i`, substituting `dflt` for absent or empty ones.
static int par(int i, int dflt)
{
    if (i >= s.nparams) return dflt;
    return s.params[i] < 0 ? dflt : s.params[i];
}

static int par_min1(int i)
{
    int v = par(i, 1);
    return v < 1 ? 1 : v;
}

// --- C0 controls ------------------------------------------------------------

static void execute_c0(term_t *t, uint8_t c)
{
    switch (c) {
    case 0x07: break;                              // BEL — see status.c blip
    case 0x08: term_backspace(t); break;
    case 0x09: term_tab_forward(t, 1); break;
    case 0x0A: case 0x0B: case 0x0C:               // LF, VT, FF
        term_index(t);
        if (t->newline_mode) term_carriage_return(t);
        break;
    case 0x0D: term_carriage_return(t); break;
    case 0x0E: t->gl = 1; break;                   // SO
    case 0x0F: t->gl = 0; break;                   // SI
    default: break;
    }
}

// --- CSI dispatch -----------------------------------------------------------

static void csi_dispatch(term_t *t, uint8_t final)
{
    if (s.priv == '?') {
        switch (final) {
        case 'h': case 'l':
            for (int i = 0; i < s.nparams; i++) {
                vt_set_mode(t, par(i, 0), true, final == 'h');
            }
            return;
        case 'J':                                   // DECSED — treat as ED
            term_erase_display(t, par(0, 0));
            return;
        case 'K':                                   // DECSEL — treat as EL
            term_erase_line(t, par(0, 0));
            return;
        case 'n':                                   // DEC DSR
            vt_reply_dsr(t, par(0, 0));
            return;
        case '$':                                   // DECRQM, handled at 'p'
        default:
            return;                                 // accept and ignore
        }
    }
    if (s.priv == '>') {
        if (final == 'c') vt_reply_da2(t);
        return;                                     // XTMODKEYS etc: ignore
    }
    if (s.priv == '<' || s.priv == '=') return;

    switch (final) {
    case '@': term_insert_chars(t, par_min1(0)); break;                 // ICH
    case 'A': term_move_rel(t, -par_min1(0), 0); break;                 // CUU
    case 'B': case 'e': term_move_rel(t, par_min1(0), 0); break;        // CUD/VPR
    case 'C': case 'a': term_move_rel(t, 0, par_min1(0)); break;        // CUF/HPR
    case 'D': term_move_rel(t, 0, -par_min1(0)); break;                 // CUB
    case 'E': term_move_to(t, t->cy + par_min1(0), 0); break;           // CNL
    case 'F': term_move_to(t, t->cy - par_min1(0), 0); break;           // CPL
    case 'G': case '`': term_move_to(t, t->cy, par_min1(0) - 1); break; // CHA/HPA
    case 'H': case 'f':                                                 // CUP/HVP
        // `resize` sends CSI 999;999H, so the clamping in term_move_to is
        // load-bearing, not defensive.
        term_move_to(t, par_min1(0) - 1, par_min1(1) - 1);
        break;
    case 'I': term_tab_forward(t, par_min1(0)); break;                  // CHT
    case 'Z': term_tab_backward(t, par_min1(0)); break;                 // CBT
    case 'J': term_erase_display(t, par(0, 0)); break;                  // ED
    case 'K': term_erase_line(t, par(0, 0)); break;                     // EL
    case 'L': term_insert_lines(t, par_min1(0)); break;                 // IL
    case 'M': term_delete_lines(t, par_min1(0)); break;                 // DL
    case 'P': term_delete_chars(t, par_min1(0)); break;                 // DCH
    case 'S': term_scroll_up(t, par_min1(0)); break;                    // SU
    case 'T': term_scroll_down(t, par_min1(0)); break;                  // SD
    case 'X': term_erase_chars(t, par_min1(0)); break;                  // ECH
    case 'b':                                                           // REP
        // xterm-256color's terminfo defines `rep` and ncurses uses it for runs
        // of identical characters — which is exactly how htop draws its meter
        // bars. Without this they render with holes.
        term_repeat_last(t, par_min1(0));
        break;
    case 'c': vt_reply_da(t); break;                                    // DA
    case 'd': term_move_to(t, par_min1(0) - 1, t->cx); break;           // VPA
    case 'g':                                                           // TBC
        if (par(0, 0) == 3) {
            for (int i = 0; i < TERM_COLS; i++) t->tabs[i] = false;
        } else if (t->cx < TERM_COLS) {
            t->tabs[t->cx] = false;
        }
        break;
    case 'h': case 'l':                                                 // SM/RM
        for (int i = 0; i < s.nparams; i++) {
            vt_set_mode(t, par(i, 0), false, final == 'h');
        }
        break;
    case 'm': vt_sgr(t, s.params, s.nparams, s.submask); break;         // SGR
    case 'n': vt_reply_dsr(t, par(0, 0)); break;                        // DSR
    case 'r':                                                           // DECSTBM
        term_set_scroll_region(t, par_min1(0) - 1, par(1, TERM_ROWS) - 1);
        break;
    case 's':                                                           // SCOSC
        t->sco_cx = t->cx; t->sco_cy = t->cy;
        break;
    case 'u':                                                           // SCORC
        term_move_to(t, t->sco_cy, t->sco_cx);
        break;
    case 't': vt_reply_winop(t, par(0, 0)); break;                      // window ops
    case 'p':
        if (s.intermediate == '!') term_soft_reset(t);                  // DECSTR
        break;
    default:
        break;
    }
}

// --- ESC dispatch -----------------------------------------------------------

static void esc_dispatch(term_t *t, uint8_t final)
{
    if (s.intermediate == '(' || s.intermediate == ')') {
        // SCS: designate a character set into G0 or G1.
        t->charset[s.intermediate == '(' ? 0 : 1] = final;
        return;
    }
    if (s.intermediate == '#') {
        if (final == '8') {                        // DECALN — vttest's first act
            term_cell_t fill = term_blank_cell(t);
            fill.ch = 'E';
            for (int r = 0; r < TERM_ROWS; r++)
                for (int c = 0; c < TERM_COLS; c++) t->row[r][c] = fill;
            term_move_to(t, 0, 0);
        }
        return;
    }
    if (s.intermediate) return;

    switch (final) {
    case '7': term_save_cursor(t); break;          // DECSC
    case '8': term_restore_cursor(t); break;       // DECRC
    case 'D': term_index(t); break;                // IND
    case 'E': term_index(t); term_carriage_return(t); break;   // NEL
    case 'M': term_reverse_index(t); break;        // RI
    case 'H': if (t->cx < TERM_COLS) t->tabs[t->cx] = true; break;  // HTS
    case 'c':                                      // RIS
        term_reset(t, true);
        break;
    case '=': t->app_keypad = true; break;         // DECKPAM
    case '>': t->app_keypad = false; break;        // DECKPNM
    default: break;
    }
}

// --- OSC --------------------------------------------------------------------

static void osc_dispatch(term_t *t)
{
    (void)t;
    // OSC 0/1/2 set the window title; we have no title bar, so everything is
    // consumed and discarded. The important part is the consuming: see the
    // note in the S_OSC case about not bailing out early.
    s.osc_len = 0;
}

// --- Printable --------------------------------------------------------------

static void put_printable(term_t *t, uint32_t cp)
{
    if (cp < 0x80 && t->charset[t->gl] == '0') {
        uint32_t mapped = vt_dec_graphics((uint8_t)cp);
        if (mapped) cp = mapped;
    }
    term_put_char(t, cp);
}

// --- Main loop --------------------------------------------------------------

void vt_parser_feed(term_t *t, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];
        bool reprocess;

        do {
            reprocess = false;

            // String states swallow everything except their terminator, so
            // they are handled before the generic control dispatch.
            if (s.state == S_OSC || s.state == S_STRING || s.state == S_STRING_ESC) {
                if (s.state == S_STRING_ESC) {
                    if (c == '\\') {               // ST
                        if (s.string_is_osc) osc_dispatch(t);
                        s.osc_len = 0;
                        s.state = S_GROUND;
                        break;
                    }
                    // ESC followed by something else: the string is over and
                    // this byte starts a new escape sequence.
                    if (s.string_is_osc) osc_dispatch(t);
                    s.osc_len = 0;
                    params_clear();
                    utf8_reset(&s.utf8);
                    s.state = S_ESCAPE;
                    reprocess = true;
                    continue;
                }
                if (c == 0x1B) { s.state = S_STRING_ESC; break; }
                if (c == 0x07) {                   // BEL also terminates OSC
                    if (s.string_is_osc) osc_dispatch(t);
                    s.osc_len = 0;
                    s.state = S_GROUND;
                    break;
                }
                if (c == 0x18 || c == 0x1A) { s.osc_len = 0; s.state = S_GROUND; break; }
                if (s.state == S_OSC && s.osc_len < VT_OSC_MAX - 1) {
                    s.osc[s.osc_len++] = (char)c;
                }
                // Keep consuming past the buffer cap. An OSC 52 clipboard
                // payload runs to kilobytes; bailing out of the state machine
                // early desynchronises it and starts printing base64 on screen.
                break;
            }

            // C0 controls execute from every non-string state.
            if (c < 0x20) {
                if (c == 0x1B) {
                    params_clear();
                    utf8_reset(&s.utf8);   // an ESC abandons any partial UTF-8
                    s.state = S_ESCAPE;
                    break;
                }
                if (c == 0x18 || c == 0x1A) { s.state = S_GROUND; break; }
                // A C0 control cannot appear inside a UTF-8 sequence, so if one
                // was in progress it was truncated: drop it and resynchronise.
                utf8_reset(&s.utf8);
                execute_c0(t, c);
                break;
            }

            switch (s.state) {
            case S_GROUND: {
                uint32_t cp;
                if (utf8_decode(&s.utf8, c, &cp)) put_printable(t, cp);
                break;
            }

            case S_ESCAPE:
                if (c == '[') { params_clear(); s.state = S_CSI_ENTRY; break; }
                if (c == ']') { s.osc_len = 0; s.string_is_osc = true; s.state = S_OSC; break; }
                if (c == 'P' || c == 'X' || c == '^' || c == '_') {
                    s.osc_len = 0;
                    s.string_is_osc = false;
                    s.state = S_STRING;            // DCS / SOS / PM / APC
                    break;
                }
                if (c >= 0x20 && c <= 0x2F) {      // intermediate
                    s.intermediate = c;
                    s.state = S_ESC_INT;
                    break;
                }
                esc_dispatch(t, c);
                utf8_reset(&s.utf8);
                s.state = S_GROUND;
                break;

            case S_ESC_INT:
                if (c >= 0x20 && c <= 0x2F) { s.intermediate = c; break; }
                esc_dispatch(t, c);
                s.state = S_GROUND;
                break;

            case S_CSI_ENTRY:
                if (c >= '<' && c <= '?') { s.priv = c; s.state = S_CSI_PARAM; break; }
                /* fall through */
            case S_CSI_PARAM:
                if (c >= '0' && c <= '9') { param_digit(c); s.state = S_CSI_PARAM; break; }
                if (c == ';') { param_separator(false); s.state = S_CSI_PARAM; break; }
                if (c == ':') { param_separator(true);  s.state = S_CSI_PARAM; break; }
                if (c >= 0x20 && c <= 0x2F) {
                    s.intermediate = c;
                    s.state = S_CSI_INT;
                    break;
                }
                if (c >= 0x40 && c <= 0x7E) {
                    params_finish();
                    csi_dispatch(t, c);
                    s.state = S_GROUND;
                    break;
                }
                s.state = S_CSI_IGNORE;
                break;

            case S_CSI_INT:
                if (c >= 0x20 && c <= 0x2F) { s.intermediate = c; break; }
                if (c >= 0x40 && c <= 0x7E) {
                    params_finish();
                    csi_dispatch(t, c);
                    s.state = S_GROUND;
                    break;
                }
                s.state = S_CSI_IGNORE;
                break;

            case S_CSI_IGNORE:
                if (c >= 0x40 && c <= 0x7E) s.state = S_GROUND;
                break;

            default:
                s.state = S_GROUND;
                break;
            }
        } while (reprocess);
    }
}
