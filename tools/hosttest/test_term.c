// test_term.c — host-side regression tests for the screen model and VT parser.
//
// The parser is the part of this firmware most likely to be subtly wrong and
// the part hardest to debug on the device, so it is compiled and exercised on
// the build machine. Run with:
//
//     ./tools/hosttest/run.sh
//
// The stub headers in tools/hosttest/stubs/ supply the handful of FreeRTOS and
// IDF symbols term_screen.c touches; nothing else is faked.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "term.h"

static int  s_failures;
static char s_reply[256];
static int  s_reply_len;

// The parser's reply channel; on the device this goes out over USB CDC.
void term_reply(const char *s, int len)
{
    if (s_reply_len + len < (int)sizeof(s_reply)) {
        memcpy(s_reply + s_reply_len, s, len);
        s_reply_len += len;
        s_reply[s_reply_len] = '\0';
    }
}

// --- Harness ----------------------------------------------------------------

static void feed(const char *s) { term_feed((const uint8_t *)s, strlen(s)); }

static void reset_all(void)
{
    term_reset(term_get(), true);
    s_reply_len = 0;
    s_reply[0] = '\0';
}

static const char *s_group = "";
static void group(const char *name) { s_group = name; }

static void check(bool ok, const char *what)
{
    if (!ok) {
        printf("  FAIL  [%s] %s\n", s_group, what);
        s_failures++;
    }
}

static void check_eq_int(int got, int want, const char *what)
{
    if (got != want) {
        printf("  FAIL  [%s] %s: got %d, want %d\n", s_group, what, got, want);
        s_failures++;
    }
}

static void check_eq_str(const char *got, const char *want, const char *what)
{
    if (strcmp(got, want) != 0) {
        printf("  FAIL  [%s] %s: got \"", s_group, what);
        for (const char *p = got; *p; p++) {
            if (*p == 0x1B) printf("\\e"); else putchar(*p);
        }
        printf("\", want \"");
        for (const char *p = want; *p; p++) {
            if (*p == 0x1B) printf("\\e"); else putchar(*p);
        }
        printf("\"\n");
        s_failures++;
    }
}

// Row as printable ASCII; anything else becomes '?'. Trailing spaces trimmed.
static const char *row_text(int r)
{
    static char buf[TERM_COLS + 1];
    term_t *t = term_get();
    for (int c = 0; c < TERM_COLS; c++) {
        uint16_t ch = t->row[r][c].ch;
        buf[c] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : (ch == ' ' || ch == 0 ? ' ' : '?');
    }
    int n = TERM_COLS;
    while (n > 0 && buf[n - 1] == ' ') n--;
    buf[n] = '\0';
    return buf;
}

static void dump_screen(void)
{
    for (int r = 0; r < TERM_ROWS; r++) printf("  %2d |%s|\n", r, row_text(r));
}

static void repeat_char(char c, int n)
{
    char buf[TERM_COLS * 2 + 1];
    for (int i = 0; i < n; i++) buf[i] = c;
    buf[n] = '\0';
    feed(buf);
}

// --- Tests ------------------------------------------------------------------

// The bug that makes every full-width line emit a spurious blank one, and that
// looks like broken scrolling rather than broken wrapping.
static void test_deferred_wrap(void)
{
    group("deferred wrap");
    reset_all();
    term_t *t = term_get();

    repeat_char('x', TERM_COLS);
    check_eq_int(t->cy, 0, "cursor stays on row 0 after filling it");
    check_eq_int(t->cx, TERM_COLS - 1, "cursor parks in the last column");
    check(t->pending_wrap, "pending_wrap is armed");

    feed("\r\n");
    check_eq_int(t->cy, 1, "CRLF after a full row moves down exactly one");

    repeat_char('y', TERM_COLS);
    feed("\r\n");
    check_eq_int(t->cy, 2, "second full row also advances by one");

    check_eq_str(row_text(0),
                 "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
                 "xxxxxxxxxxxxxxxxxxxx", "row 0 holds 80 x");
    check_eq_str(row_text(1),
                 "yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy"
                 "yyyyyyyyyyyyyyyyyyyy", "row 1 holds 80 y");
    check_eq_str(row_text(2), "", "no blank row was inserted between them");

    // Wrapping without an explicit newline.
    reset_all();
    repeat_char('a', TERM_COLS);
    feed("Z");
    check_eq_int(t->cy, 1, "the next printable triggers the wrap");
    check_eq_int(t->cx, 1, "and lands in column 1");
    check_eq_str(row_text(1), "Z", "the wrapped character is on the new row");

    // A cursor-positioning sequence must clear the pending wrap.
    reset_all();
    repeat_char('b', TERM_COLS);
    feed("\033[1;1H");
    check(!t->pending_wrap, "CUP clears pending_wrap");

    // Autowrap off: the cursor sticks and characters overwrite.
    reset_all();
    feed("\033[?7l");
    repeat_char('c', TERM_COLS + 5);
    check_eq_int(t->cy, 0, "DECAWM off keeps us on row 0");
    check_eq_int(t->cx, TERM_COLS - 1, "and pinned to the last column");
}

static void test_scroll_region(void)
{
    group("scroll region");
    reset_all();
    term_t *t = term_get();

    for (int i = 0; i < 8; i++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "L%d\r\n", i);
        feed(buf);
    }
    feed("\033[3;5r");                      // rows 2..4 (0-based)
    check_eq_int(t->top, 2, "DECSTBM sets the top margin");
    check_eq_int(t->bot, 4, "DECSTBM sets the bottom margin");
    check_eq_int(t->cy, 0, "DECSTBM homes the cursor");

    feed("\033[5;1H");                      // last row of the region
    feed("\n");                             // should scroll only rows 2..4
    check_eq_str(row_text(0), "L0", "row above the region is untouched");
    check_eq_str(row_text(1), "L1", "row above the region is untouched");
    check_eq_str(row_text(2), "L3", "region scrolled up");
    check_eq_str(row_text(3), "L4", "region scrolled up");
    check_eq_str(row_text(4), "", "vacated row is blank");
    check_eq_str(row_text(5), "L5", "row below the region is untouched");

    // Reverse index at the top margin scrolls the region down.
    feed("\033[3;1H\033M");
    check_eq_str(row_text(2), "", "RI at the top margin opened a blank row");
    check_eq_str(row_text(3), "L3", "and pushed the region down");

    // Full reset of the region.
    feed("\033[r");
    check_eq_int(t->top, 0, "CSI r with no args restores the full region");
    check_eq_int(t->bot, TERM_ROWS - 1, "CSI r with no args restores the full region");
}

static void test_editing(void)
{
    group("editing");
    reset_all();

    feed("abcdefghij\033[1;4H");            // cursor on 'd'
    feed("\033[3P");                        // DCH 3 -> delete def
    check_eq_str(row_text(0), "abcghij", "DCH shifts the tail left");

    feed("\033[1;4H\033[2@");               // ICH 2
    check_eq_str(row_text(0), "abc  ghij", "ICH opens a gap");

    feed("\033[1;1H\033[4X");               // ECH 4
    check_eq_str(row_text(0), "     ghij", "ECH blanks without shifting");

    reset_all();
    feed("one\r\ntwo\r\nthree\r\n");
    feed("\033[2;1H\033[M");                // DL 1 on row 1
    check_eq_str(row_text(1), "three", "DL pulls the following rows up");

    feed("\033[2;1H\033[L");                // IL 1
    check_eq_str(row_text(1), "", "IL opens a blank row");
    check_eq_str(row_text(2), "three", "and pushes the rest down");

    reset_all();
    feed("hello world\033[1;6H\033[K");     // EL 0
    check_eq_str(row_text(0), "hello", "EL 0 erases to end of line");

    reset_all();
    feed("hello\r\nworld\033[1;1H\033[J");  // ED 0 from home
    check_eq_str(row_text(0), "", "ED 0 from home clears everything");
    check_eq_str(row_text(1), "", "ED 0 from home clears everything");
}

// xterm-256color's terminfo defines `rep`, and htop's meter bars use it.
static void test_rep(void)
{
    group("REP");
    reset_all();
    feed("x\033[5b");
    check_eq_str(row_text(0), "xxxxxx", "CSI b repeats the last graphic char");

    reset_all();
    feed("\033[31mo\033[9b");
    check_eq_str(row_text(0), "oooooooooo", "REP after SGR still repeats");
    check_eq_int(term_get()->row[0][5].fg, term_pal256[1], "repeats keep the pen colour");
}

static void test_sgr(void)
{
    group("SGR");
    reset_all();
    term_t *t = term_get();

    feed("\033[31mR");
    check_eq_int(t->row[0][0].fg, term_pal256[1], "SGR 31 selects red");

    feed("\033[0m\033[1;31mB");
    check_eq_int(t->row[0][1].fg, term_pal256[9], "bold brightens indexed 31 -> 91");

    feed("\033[0m\033[31;1mB");
    check_eq_int(t->row[0][2].fg, term_pal256[9], "and the reverse parameter order too");

    feed("\033[0m\033[38;5;99mX");
    check_eq_int(t->row[0][3].fg, term_pal256[99], "38;5;n selects from the 256 palette");

    feed("\033[0m\033[48;5;17mX");
    check_eq_int(t->row[0][4].bg, term_pal256[17], "48;5;n sets the background");

    // Truecolour, semicolon form.
    feed("\033[0m\033[38;2;255;0;0mX");
    check_eq_int(t->row[0][5].fg, term_pal256[9], "38;2;255;0;0 is pure red in RGB565");

    // Truecolour, colon form with the empty colour-space slot — what tmux,
    // kitty and libvte actually emit.
    feed("\033[0m\033[38:2::255:0:0mX");
    check_eq_int(t->row[0][6].fg, term_pal256[9], "38:2::R:G:B parses identically");

    feed("\033[0m\033[38:5:99mX");
    check_eq_int(t->row[0][7].fg, term_pal256[99], "38:5:n parses identically");

    feed("\033[0m\033[7mX");
    check(t->row[0][8].attr & ATTR_REVERSE, "SGR 7 sets reverse");
    feed("\033[27mX");
    check(!(t->row[0][9].attr & ATTR_REVERSE), "SGR 27 clears reverse");

    feed("\033[4mX\033[24mX");
    check(t->row[0][10].attr & ATTR_UNDERLINE, "SGR 4 sets underline");
    check(!(t->row[0][11].attr & ATTR_UNDERLINE), "SGR 24 clears underline");

    // An empty CSI m means CSI 0 m.
    reset_all();
    feed("D");                                  // a cell written with the default pen
    uint16_t default_fg = t->row[0][0].fg;
    check(default_fg != term_pal256[1], "the default foreground is not palette red");
    feed("\033[31m\033[mX");
    check_eq_int(t->row[0][1].fg, default_fg, "empty SGR is a reset");
    check_eq_int((int)t->pen.fg.kind, COLOR_DEFAULT, "empty SGR restores the default pen");

    // A trailing separator is one more (empty) parameter, not a dropped one.
    reset_all();
    feed("\033[31;mX");
    check_eq_int((int)t->pen.fg.kind, COLOR_DEFAULT, "CSI 31;m ends with an implicit 0");
}

static void test_alt_screen(void)
{
    group("alt screen");
    reset_all();
    term_t *t = term_get();

    feed("primary content\r\n");
    feed("\033[?1049h");
    check(t->alt_screen, "?1049h switches to the alternate screen");
    check_eq_str(row_text(0), "", "the alternate screen starts cleared");

    // ?1049h saves the cursor and clears, but does not home it — applications
    // always follow with an explicit CUP, so the cursor is still on row 1.
    check_eq_int(t->cy, 1, "?1049h leaves the cursor where it was");
    feed("alt content");
    check_eq_str(row_text(1), "alt content", "writes land on the alternate screen");
    check_eq_str(row_text(0), "", "the alternate screen really is a separate buffer");

    feed("\033[?1049l");
    check(!t->alt_screen, "?1049l switches back");
    check_eq_str(row_text(0), "primary content", "the primary screen was preserved");
    check_eq_str(row_text(1), "", "and the alternate screen's content did not leak in");
    check_eq_int(t->cy, 1, "and the cursor was restored");
}

// `resize` learns the terminal size only because CUP clamps and DSR 6 answers.
static void test_reports(void)
{
    group("reports");
    reset_all();

    feed("\033[999;999H\033[6n");
    check_eq_str(s_reply, "\033[24;80R", "CPR reports the clamped cursor position");

    s_reply_len = 0; s_reply[0] = '\0';
    feed("\033[18t");
    check_eq_str(s_reply, "\033[8;24;80t", "CSI 18 t reports the text area");

    s_reply_len = 0; s_reply[0] = '\0';
    feed("\033[5n");
    check_eq_str(s_reply, "\033[0n", "DSR 5 answers OK");

    s_reply_len = 0; s_reply[0] = '\0';
    feed("\033[c");
    check_eq_str(s_reply, "\033[?62;1;6;9;15;22c", "primary DA identifies as a VT220-ish xterm");
}

static void test_utf8_and_charsets(void)
{
    group("utf8 / charsets");
    reset_all();
    term_t *t = term_get();

    feed("\xc3\xa9");                        // U+00E9 e-acute
    check_eq_int(t->row[0][0].ch, 0x00E9, "2-byte UTF-8 decodes");
    check_eq_int(t->cx, 1, "and advances one column");

    feed("\xe2\x94\x80");                    // U+2500 box drawing horizontal
    check_eq_int(t->row[0][1].ch, 0x2500, "3-byte UTF-8 decodes");

    feed("\xff");                            // invalid lead
    check_eq_int(t->row[0][2].ch, 0xFFFD, "invalid bytes become U+FFFD");

    feed("\xc0\xaf");                        // overlong '/'
    check_eq_int(t->row[0][3].ch, 0xFFFD, "overlong sequences are rejected");

    // A wide character takes two cells, so the column accounting stays right.
    reset_all();
    feed("\xe6\xbc\xa2");                    // U+6F22
    check_eq_int(t->cx, 2, "a wide character advances two columns");
    check(t->row[0][0].attr & ATTR_WIDE_LEAD, "first cell is the lead");
    check(t->row[0][1].attr & ATTR_WIDE_TAIL, "second cell is the tail");

    // DEC Special Graphics.
    reset_all();
    feed("\033(0qx\033(Bq");
    check_eq_int(t->row[0][0].ch, 0x2500, "ESC ( 0 maps q to the horizontal line");
    check_eq_int(t->row[0][1].ch, 0x2502, "and x to the vertical line");
    check_eq_int(t->row[0][2].ch, 'q', "ESC ( B restores ASCII");

    // SO/SI shift between G0 and G1.
    reset_all();
    feed("\033)0\016q\017q");
    check_eq_int(t->row[0][0].ch, 0x2500, "SO shifts G1 (DEC graphics) in");
    check_eq_int(t->row[0][1].ch, 'q', "SI shifts G0 (ASCII) back");
}

static void test_osc_and_strings(void)
{
    group("OSC / strings");
    reset_all();

    // Title set, BEL-terminated, then normal text.
    feed("\033]0;my title\007hello");
    check_eq_str(row_text(0), "hello", "OSC terminated by BEL is consumed");

    reset_all();
    feed("\033]2;other\033\\world");
    check_eq_str(row_text(0), "world", "OSC terminated by ST is consumed");

    // A payload longer than the OSC buffer must still be consumed to the end;
    // bailing out early desynchronises the machine and prints base64 on screen.
    reset_all();
    feed("\033]52;c;");
    for (int i = 0; i < 40; i++) feed("0123456789ABCDEF");   // 640 bytes
    feed("\007tail");
    check_eq_str(row_text(0), "tail", "an over-long OSC payload is fully swallowed");

    reset_all();
    feed("\033P1;2q~~~~\033\\after");
    check_eq_str(row_text(0), "after", "DCS payloads are swallowed to ST");
}

static void test_tabs_and_misc(void)
{
    group("tabs / misc");
    reset_all();
    term_t *t = term_get();

    feed("a\tb\tc");
    check_eq_str(row_text(0), "a       b       c", "default tab stops are every 8");

    feed("\033[1;1H\033[3g");                // clear all tab stops
    feed("\033[1;5H\033H");                  // set one at column 4
    feed("\033[1;1H\t");
    check_eq_int(t->cx, 4, "HTS sets a tab stop, TBC 3 cleared the rest");

    // Backspace does not wrap backwards past column 0.
    reset_all();
    feed("\010\010x");
    check_eq_int(t->cx, 1, "BS at column 0 is a no-op");

    // DECALN, which is the first thing vttest does.
    reset_all();
    feed("\033#8");
    check_eq_str(row_text(0),
                 "EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE"
                 "EEEEEEEEEEEEEEEEEEEE", "DECALN fills the screen with E");

    // Save/restore cursor round-trips the pen too.
    reset_all();
    feed("\033[10;20H\033[31m\0337");
    feed("\033[1;1H\033[0m");
    feed("\0338X");
    check_eq_int(t->cy, 9, "DECRC restores the row");
    check_eq_int(t->cx, 20, "DECRC restores the column");
    check_eq_int(t->row[9][19].fg, term_pal256[1], "DECRC restores the pen colour");

    // Application cursor mode is what the on-screen keyboard reads.
    reset_all();
    feed("\033[?1h");
    check(term_app_cursor_mode(), "?1h enables application cursor keys");
    feed("\033[?1l");
    check(!term_app_cursor_mode(), "?1l disables them");
}

static void test_scroll_and_flood(void)
{
    group("scrolling");
    reset_all();

    for (int i = 0; i < TERM_ROWS + 5; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "line%d\r\n", i);
        feed(buf);
    }
    // 29 lines written, each ending in LF. The LF after line23 is the first to
    // scroll, so lines 23..28 each cost one row: the top shows line6, and the
    // cursor sits on the blank row below line28.
    check_eq_str(row_text(0), "line6", "output scrolled off the top");
    check_eq_str(row_text(TERM_ROWS - 2), "line28", "the newest line is at the bottom");
    check_eq_str(row_text(TERM_ROWS - 1), "", "the cursor row is blank");

    // The row-pointer rotation must not leave two rows aliased.
    term_t *t = term_get();
    for (int a = 0; a < TERM_ROWS; a++) {
        for (int b = a + 1; b < TERM_ROWS; b++) {
            if (t->row[a] == t->row[b]) {
                check(false, "scrolling aliased two rows to the same buffer");
                return;
            }
        }
    }
}

int main(int argc, char **argv)
{
    term_init();

    test_deferred_wrap();
    test_scroll_region();
    test_editing();
    test_rep();
    test_sgr();
    test_alt_screen();
    test_reports();
    test_utf8_and_charsets();
    test_osc_and_strings();
    test_tabs_and_misc();
    test_scroll_and_flood();

    if (argc > 1 && strcmp(argv[1], "-v") == 0) dump_screen();

    if (s_failures) {
        printf("\n%d check(s) failed\n", s_failures);
        return 1;
    }
    printf("all terminal checks passed\n");
    return 0;
}
