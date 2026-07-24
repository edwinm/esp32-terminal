// ui_tab_console.c — serial terminal on the 2nd Mabee connector (GPIO43/44).
// Shows incoming UART data and sends lines typed on an on-screen keyboard.
#include "ui.h"
#include "console_uart.h"
#include "esp_lvgl_port.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define RX_TEXT_CAP 3000   // trim the received view past this many chars

static lv_obj_t *s_rx;      // received-data textarea (read-only-ish)
static lv_obj_t *s_input;   // line being composed
static lv_obj_t *s_kb;      // on-screen keyboard

// Append text to the received view (called from the UART RX task).
static void rx_cb(const char *data, int len, void *ctx)
{
    (void)ctx;
    // Filter to printable + common whitespace so control bytes don't corrupt it.
    char *clean = malloc(len + 1);
    if (!clean) return;
    for (int i = 0; i < len; i++) {
        char c = data[i];
        clean[i] = (c == '\n' || c == '\r' || c == '\t' ||
                    (c >= 0x20 && c < 0x7F)) ? c : '.';
    }
    clean[len] = 0;

    if (lvgl_port_lock(0)) {
        if (lv_strlen(lv_textarea_get_text(s_rx)) > RX_TEXT_CAP)
            lv_textarea_set_text(s_rx, "");
        lv_textarea_add_text(s_rx, clean);
        lv_obj_scroll_to_y(s_rx, LV_COORD_MAX, LV_ANIM_OFF);
        lvgl_port_unlock();
    }
    free(clean);
}

static void send_current_line(void)
{
    const char *txt = lv_textarea_get_text(s_input);
    if (txt && txt[0]) {
        console_uart_send(txt, strlen(txt));
        console_uart_send("\n", 1);
        // Local echo so the user sees what was sent.
        lv_textarea_add_text(s_rx, "> ");
        lv_textarea_add_text(s_rx, txt);
        lv_textarea_add_text(s_rx, "\n");
        lv_obj_scroll_to_y(s_rx, LV_COORD_MAX, LV_ANIM_OFF);
    }
    lv_textarea_set_text(s_input, "");
}

static void input_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED) {
        lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_kb, s_input);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {          // checkmark = send
        send_current_line();
    } else if (code == LV_EVENT_CANCEL) {  // keyboard close
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void baud_event_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    char sel[12];
    lv_dropdown_get_selected_str(dd, sel, sizeof(sel));
    console_uart_set_baud(atoi(sel));
}

static void clear_cb(lv_event_t *e) { (void)e; lv_textarea_set_text(s_rx, ""); }

void ui_tab_console_create(lv_obj_t *parent)
{
    lv_obj_set_style_pad_all(parent, 4, 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 4, 0);

    // Controls row: baud selector + Clear.
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_set_style_border_width(row, 0, 0);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, "GPIO43/44  baud:");
    lv_obj_t *baud = lv_dropdown_create(row);
    lv_dropdown_set_options(baud, "9600\n19200\n38400\n57600\n115200");
    lv_dropdown_set_selected(baud, 4);   // 115200
    lv_obj_add_event_cb(baud, baud_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *clr = lv_button_create(row);
    lv_obj_t *cl = lv_label_create(clr);
    lv_label_set_text(cl, "Clear");
    lv_obj_add_event_cb(clr, clear_cb, LV_EVENT_CLICKED, NULL);

    // Received data view (grows to fill).
    s_rx = lv_textarea_create(parent);
    lv_obj_set_width(s_rx, LV_PCT(100));
    lv_obj_set_flex_grow(s_rx, 1);
    lv_textarea_set_placeholder_text(s_rx, "incoming serial data...");
    lv_obj_set_style_text_font(s_rx, &lv_font_montserrat_14, 0);

    // Compose line.
    s_input = lv_textarea_create(parent);
    lv_obj_set_width(s_input, LV_PCT(100));
    lv_textarea_set_one_line(s_input, true);
    lv_textarea_set_placeholder_text(s_input, "tap to type, checkmark to send");
    lv_obj_add_event_cb(s_input, input_event_cb, LV_EVENT_ALL, NULL);

    // On-screen keyboard (hidden until the input is focused).
    s_kb = lv_keyboard_create(parent);
    lv_obj_set_size(s_kb, LV_PCT(100), 150);
    lv_keyboard_set_textarea(s_kb, s_input);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_kb, kb_event_cb, LV_EVENT_ALL, NULL);

    console_uart_set_rx_cb(rx_cb, NULL);
}
