// vt_parser.h — the escape-sequence state machine.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "term.h"

#define VT_MAX_PARAMS 32
#define VT_OSC_MAX    256

void vt_parser_reset(void);

// Consume host bytes and mutate the model. Caller holds term_lock().
void vt_parser_feed(term_t *t, const uint8_t *data, size_t len);

// --- Implemented in vt_modes.c ---------------------------------------------

// SGR. `submask` bit i is set when params[i] was introduced by ':' rather than
// ';' — modern emitters write CSI 38:2::R:G:B m and the colon form must not be
// treated as a parse error.
void vt_sgr(term_t *t, const int16_t *params, int n, uint32_t submask);

// SM/RM and their DEC private (`?`) variants.
void vt_set_mode(term_t *t, int mode, bool priv, bool set);

// DA / DA2 / DSR / window-ops replies. These write back to the host.
void vt_reply_da(term_t *t);
void vt_reply_da2(term_t *t);
void vt_reply_dsr(term_t *t, int what);
void vt_reply_winop(term_t *t, int what);

// DEC Special Graphics: map 0x5F..0x7E to a Unicode codepoint. Returns 0 when
// the byte is outside the mapped range.
uint32_t vt_dec_graphics(uint8_t c);
