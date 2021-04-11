/*
 * Copyright 2010 Vincent Sanders <vince@simtec.co.uk>
 *
 * Framebuffer windowing toolkit on screen keyboard.
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <limits.h>

#include <libnsfb.h>
#include <libnsfb_plot.h>
#include <libnsfb_event.h>
#include <libnsfb_cursor.h>

#include "utils/log.h"
#include "utils/nsoption.h"
#include "netsurf/browser_window.h"

#include "framebuffer/gui.h"
#include "framebuffer/fbtk.h"
#include "framebuffer/image_data.h"

#include "widget.h"

struct kbd_button_s {
	int x;
	int y;
	int w;
	int h;
	const char *t;
	enum nsfb_key_code_e keycode;
	/// if the enum is not NOT_TOGGLE_KEY, it will cause the key to be
	/// toggled on press, and only released after the next key is pressed.
	/// On second press, it will be locked.
	enum key_state { NOT_TOGGLE_KEY, NOT_PRESSED, PRESSED, LOCKED } state;
    fbtk_widget_t *widget;
};

#define KEYCOUNT 58

static struct kbd_button_s kbdbase[KEYCOUNT] = {
	{0, 0, 20, 15, "`", NSFB_KEY_BACKQUOTE, NOT_TOGGLE_KEY, NULL},
	{20, 0, 20, 15, "1", NSFB_KEY_1, NOT_TOGGLE_KEY, NULL},
	{40, 0, 20, 15, "2", NSFB_KEY_2, NOT_TOGGLE_KEY, NULL},
	{60, 0, 20, 15, "3", NSFB_KEY_3, NOT_TOGGLE_KEY, NULL},
	{80, 0, 20, 15, "4", NSFB_KEY_4, NOT_TOGGLE_KEY, NULL},
	{100, 0, 20, 15, "5", NSFB_KEY_5, NOT_TOGGLE_KEY, NULL},
	{120, 0, 20, 15, "6", NSFB_KEY_6, NOT_TOGGLE_KEY, NULL},
	{140, 0, 20, 15, "7", NSFB_KEY_7, NOT_TOGGLE_KEY, NULL},
	{160, 0, 20, 15, "8", NSFB_KEY_8, NOT_TOGGLE_KEY, NULL},
	{180, 0, 20, 15, "9", NSFB_KEY_9, NOT_TOGGLE_KEY, NULL},
	{200, 0, 20, 15, "0", NSFB_KEY_0, NOT_TOGGLE_KEY, NULL},
	{220, 0, 20, 15, "-", NSFB_KEY_MINUS, NOT_TOGGLE_KEY, NULL},
	{240, 0, 20, 15, "=", NSFB_KEY_EQUALS, NOT_TOGGLE_KEY, NULL},
	{260, 0, 40, 15, "\xe2\x8c\xab", NSFB_KEY_BACKSPACE, NOT_TOGGLE_KEY, NULL},
	{0, 15, 30, 15, "\xe2\x86\xb9", NSFB_KEY_TAB, NOT_TOGGLE_KEY, NULL},
	{30, 15, 20, 15, "q", NSFB_KEY_q, NOT_TOGGLE_KEY, NULL},
	{50, 15, 20, 15, "w", NSFB_KEY_w, NOT_TOGGLE_KEY, NULL},
	{70, 15, 20, 15, "e", NSFB_KEY_e, NOT_TOGGLE_KEY, NULL},
	{90, 15, 20, 15, "r", NSFB_KEY_r, NOT_TOGGLE_KEY, NULL},
	{110, 15, 20, 15, "t", NSFB_KEY_t, NOT_TOGGLE_KEY, NULL},
	{130, 15, 20, 15, "y", NSFB_KEY_y, NOT_TOGGLE_KEY, NULL},
	{150, 15, 20, 15, "u", NSFB_KEY_u, NOT_TOGGLE_KEY, NULL},
	{170, 15, 20, 15, "i", NSFB_KEY_i, NOT_TOGGLE_KEY, NULL},
	{190, 15, 20, 15, "o", NSFB_KEY_o, NOT_TOGGLE_KEY, NULL},
	{210, 15, 20, 15, "p", NSFB_KEY_p, NOT_TOGGLE_KEY, NULL},
	{230, 15, 20, 15, "[", NSFB_KEY_LEFTBRACKET, NOT_TOGGLE_KEY, NULL},
	{250, 15, 20, 15, "]", NSFB_KEY_RIGHTBRACKET, NOT_TOGGLE_KEY, NULL},
	{275, 15, 25, 30, "\xe2\x8f\x8e", NSFB_KEY_RETURN, NOT_TOGGLE_KEY, NULL},
	{35, 30, 20, 15, "a", NSFB_KEY_a, NOT_TOGGLE_KEY, NULL},
	{55, 30, 20, 15, "s", NSFB_KEY_s, NOT_TOGGLE_KEY, NULL},
	{75, 30, 20, 15, "d", NSFB_KEY_d, NOT_TOGGLE_KEY, NULL},
	{95, 30, 20, 15, "f", NSFB_KEY_f, NOT_TOGGLE_KEY, NULL},
	{115, 30, 20, 15, "g", NSFB_KEY_g, NOT_TOGGLE_KEY, NULL},
	{135, 30, 20, 15, "h", NSFB_KEY_h, NOT_TOGGLE_KEY, NULL},
	{155, 30, 20, 15, "j", NSFB_KEY_j, NOT_TOGGLE_KEY, NULL},
	{175, 30, 20, 15, "k", NSFB_KEY_k, NOT_TOGGLE_KEY, NULL},
	{195, 30, 20, 15, "l", NSFB_KEY_l, NOT_TOGGLE_KEY, NULL},
	{215, 30, 20, 15, ";", NSFB_KEY_SEMICOLON, NOT_TOGGLE_KEY, NULL},
	{235, 30, 20, 15, "'", NSFB_KEY_QUOTE, NOT_TOGGLE_KEY, NULL},
	{255, 30, 20, 15, "#", NSFB_KEY_HASH, NOT_TOGGLE_KEY, NULL},
	{0, 45, 25, 15, "\xe2\x87\xa7", NSFB_KEY_LSHIFT, NOT_PRESSED, NULL},
	{25, 45, 20, 15, "\\", NSFB_KEY_BACKSLASH, NOT_TOGGLE_KEY, NULL},
	{45, 45, 20, 15, "z", NSFB_KEY_z, NOT_TOGGLE_KEY, NULL},
	{65, 45, 20, 15, "x", NSFB_KEY_x, NOT_TOGGLE_KEY, NULL},
	{85, 45, 20, 15, "c", NSFB_KEY_c, NOT_TOGGLE_KEY, NULL},
	{105, 45, 20, 15, "v", NSFB_KEY_v, NOT_TOGGLE_KEY, NULL},
	{125, 45, 20, 15, "b", NSFB_KEY_b, NOT_TOGGLE_KEY, NULL},
	{145, 45, 20, 15, "n", NSFB_KEY_n, NOT_TOGGLE_KEY, NULL},
	{165, 45, 20, 15, "m", NSFB_KEY_m, NOT_TOGGLE_KEY, NULL},
	{185, 45, 20, 15, ",", NSFB_KEY_COMMA, NOT_TOGGLE_KEY, NULL},
	{205, 45, 20, 15, ".", NSFB_KEY_PERIOD, NOT_TOGGLE_KEY, NULL},
	{225, 45, 20, 15, "/", NSFB_KEY_SLASH, NOT_TOGGLE_KEY, NULL},
	{245, 45, 55, 15, "\xe2\x87\xa7", NSFB_KEY_RSHIFT, NOT_PRESSED, NULL},
	{40, 67, 185, 15, "", NSFB_KEY_SPACE, NOT_TOGGLE_KEY, NULL},
	{250, 60, 20, 15, "\xe2\x96\xb2", NSFB_KEY_UP, NOT_TOGGLE_KEY, NULL},
	{230, 67, 20, 15, "\xe2\x97\x80", NSFB_KEY_LEFT, NOT_TOGGLE_KEY, NULL},
	{270, 67, 20, 15, "\xe2\x96\xb6", NSFB_KEY_RIGHT, NOT_TOGGLE_KEY, NULL},
	{250, 75, 20, 15, "\xe2\x96\xbc", NSFB_KEY_DOWN, NOT_TOGGLE_KEY, NULL},
};

static fbtk_widget_t *osk;

static int osk_click(fbtk_widget_t *widget, fbtk_callback_info *cbi);

static int osk_close(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	if (cbi->event->type != NSFB_EVENT_KEY_UP)
		return 0;

	fbtk_set_mapping(osk, false);

	return 0;
}

static void
osk_redraw_button(struct kbd_button_s *kbd_button)
{
	fbtk_widget_t *updated_widget = fbtk_create_text_button(
		osk,
		kbd_button->widget->x,
		kbd_button->widget->y,
		kbd_button->widget->width,
		kbd_button->widget->height,
		kbd_button->state == NOT_PRESSED ? FB_FRAME_COLOUR
		: kbd_button->state == PRESSED	 ? FB_COLOUR_DARKGREY
						 : FB_COLOUR_BLACK,
		kbd_button->state == LOCKED ? FB_COLOUR_WHITE : FB_COLOUR_BLACK,
		osk_click,
		kbd_button);
	fbtk_set_text(updated_widget, kbd_button->t);
	fbtk_destroy_widget(kbd_button->widget);
    kbd_button->widget = updated_widget;
}

static int osk_click(fbtk_widget_t *widget, fbtk_callback_info *cbi)
{
	nsfb_event_t event;
	struct kbd_button_s *kbd_button = cbi->context;

	// special handling for toggle keys
	if (kbd_button->state != NOT_TOGGLE_KEY) {
		if (cbi->event->type == NSFB_EVENT_KEY_DOWN) {
			switch (kbd_button->state) {
			case NOT_PRESSED:
				// if toggle key wasn't pressed before, mark it
				// as pressed and then send key down event
				kbd_button->state = PRESSED;
				event.type = NSFB_EVENT_KEY_DOWN;
				event.value.keycode = kbd_button->keycode;
				fbtk_input(widget, &event);
            	osk_redraw_button(kbd_button);
				break;
			case PRESSED:
				// if toggle key was pressed before, do not
				// forward the new press, and mark the key as
				// locked
				kbd_button->state = LOCKED;
            	osk_redraw_button(kbd_button);
				break;
			case LOCKED:
				// if toggle key was locked before, unlock it,
				// i.e. send a key up event
				kbd_button->state = NOT_PRESSED;
				event.type = NSFB_EVENT_KEY_UP;
				event.value.keycode = kbd_button->keycode;
				fbtk_input(widget, &event);
            	osk_redraw_button(kbd_button);
				break;
			default:
				break;
			}
		}
	} else {
		event.type = cbi->event->type;
		event.value.keycode = kbd_button->keycode;
		fbtk_input(widget, &event);
		// after a non-toggle key is released, unset all pressed
		// toggle-keys, and release them
		if (cbi->event->type == NSFB_EVENT_KEY_UP) {
			size_t i;
			for (i = 0;
			     i < sizeof(kbdbase) / sizeof(struct kbd_button_s);
			     i++) {
				if (kbdbase[i].state == PRESSED) {
					event.type = NSFB_EVENT_KEY_UP;
					event.value.keycode =
						kbdbase[i].keycode;
					fbtk_input(widget, &event);
					kbdbase[i].state = NOT_PRESSED;
                	osk_redraw_button(&kbdbase[i]);
				}
			}
		}
	}


	return 0;
}

/* exported function documented in fbtk.h */
void fbtk_enable_oskb(fbtk_widget_t *fbtk)
{
	fbtk_widget_t *widget;
	int kloop;
	int maxx = 0;
	int maxy = 0;
	int ww;
	int wh;
	fbtk_widget_t *root = fbtk_get_root_widget(fbtk);
	int furniture_width = nsoption_int(fb_furniture_size);

	for (kloop = 0; kloop < KEYCOUNT; kloop++) {
		if ((kbdbase[kloop].x + kbdbase[kloop].w) > maxx)
			maxx = kbdbase[kloop].x + kbdbase[kloop].w;
		if ((kbdbase[kloop].y + kbdbase[kloop].h) > maxy)
			maxy = kbdbase[kloop].y + kbdbase[kloop].h;
	}

	ww = fbtk_get_width(root);

	/* scale window height apropriately */
	wh = (maxy * ww) / maxx;

	osk = fbtk_create_window(
		root, 0, fbtk_get_height(root) - wh, 0, wh, 0xff202020);

	for (kloop = 0; kloop < KEYCOUNT; kloop++) {
		widget = fbtk_create_text_button(osk,
						 (kbdbase[kloop].x * ww) / maxx,
						 (kbdbase[kloop].y * ww) / maxx,
						 (kbdbase[kloop].w * ww) / maxx,
						 (kbdbase[kloop].h * ww) / maxx,
						 FB_FRAME_COLOUR,
						 FB_COLOUR_BLACK,
						 osk_click,
						 &kbdbase[kloop]);
		fbtk_set_text(widget, kbdbase[kloop].t);
        kbdbase[kloop].widget = widget;
	}

	widget = fbtk_create_button(osk,
				    fbtk_get_width(osk) - furniture_width,
				    fbtk_get_height(osk) - furniture_width,
				    furniture_width,
				    furniture_width,
				    FB_FRAME_COLOUR,
				    &osk_image,
				    osk_close,
				    NULL);
}

/* exported function documented in fbtk.h */
void map_osk(void)
{
	fbtk_set_zorder(osk, INT_MIN);
	fbtk_set_mapping(osk, true);
}

/* exported function documented in fbtk.h */
bool unmap_osk(void) {
    if (osk->mapped) {
        fbtk_set_mapping(osk, false);
        return true;
    }
    return false;
}

/*
 * Local Variables:
 * c-basic-offset:8
 * End:
 */
