#include <stdbool.h>
#include <malloc.h>
#include <limits.h>

#include "libnsfb.h"
#include "libnsfb_event.h"
#include "component_util.h"
#include "framebuffer/gui.h"
#include "framebuffer/fbtk.h"
#include "framebuffer/fbtk/widget.h"
#include "remarkable_import.h"

static const char *TITLE_TEXT = "Import to reMarkable (Xochitl)";
static const char *IMPORT_EXPLANATION_TEXT =
	"Import the file %s to your reMarkable";
static const char *IMPORT_COMPLETED_TEXT =
	"File %s has been imported, and will become visible after Xochitl restart";
static const char *CLOSE_BUTTON_TEXT = "Close";
static const char *IMPORT_BUTTON_TEXT = "Import";
static const char *RESTART_BUTTON_TEXT = "Restart device now";


struct gui_remarkable_import_window {
	fbtk_widget_t *window_widget;

	fbtk_widget_t *title_widget;
	fbtk_widget_t *explanation_widget;

	fbtk_widget_t *close_button_widget;
	fbtk_widget_t *import_button_widget;
	fbtk_widget_t *restart_button_widget;

	const char *file_path;
};

static void
import_window_destroy(struct gui_remarkable_import_window *window)
{

    free(window);
}

static int
close_button_click(struct fbtk_widget_s *widget, fbtk_callback_info *info)
{
	if (info->event->type == NSFB_EVENT_KEY_UP) {
		struct gui_remarkable_import_window *window = info->context;
		import_window_destroy(window);
	}
	return 0;
}

void import_window_open(const char *file_path, struct gui_window *gui)
{
	const int WINDOW_MAX_HEIGHT = 500;
	int import_window_height = gui->window->height - gui->toolbar->height -
				   gui->status->height - 300;
	if (import_window_height > WINDOW_MAX_HEIGHT) {
		import_window_height = WINDOW_MAX_HEIGHT;
	}

	struct gui_remarkable_import_window *window = malloc(sizeof *window);
	window->window_widget = fbtk_create_window(
		gui->window,
		WINDOW_SPACING,
		gui->toolbar->height + WINDOW_SPACING,
		gui->window->width - gui->vscroll->width - WINDOW_SPACING * 2,
		import_window_height,
		FB_COLOUR_WHITE);

    draw_border_outline(window->window_widget);
    
    fbtk_set_zorder(window->window_widget, INT_MIN);
    fbtk_set_mapping(window->window_widget, true);
}
