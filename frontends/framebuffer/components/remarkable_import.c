#include "utils/errors.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <limits.h>

#include "libnsfb.h"
#include "libnsfb_event.h"
#include "component_util.h"
#include "framebuffer/gui.h"
#include "framebuffer/fbtk.h"
#include "framebuffer/fbtk/widget.h"
#include "remarkable_import.h"
#include "remarkable_xochitl_import.h"
#include "utils/filename_utils.h"

static const char *TITLE_TEXT = "Import to reMarkable (Xochitl)";
static const char *TITLE_TEXT_COMPLETED = "Import done";
static const char *TITLE_TEXT_FAILED = "Import failed";

static const char *IMPORT_EXPLANATION_TEXT =
	"Import the file \"%s\" as a reMarkable document?";
static const char *IMPORT_COMPLETED_TEXT =
	"File \"%s\" has been imported, and will become visible after Xochitl restart";
static const char *IMPORT_FAILED_TEXT =
	"Import for file \"%s\" failed (error code: %d)";
static const char *INVALID_FORMAT_TEXT =
	"Can't import: only .epub and .pdf files are supported";
static const char *CLOSE_BUTTON_TEXT = "Close";
static const char *IMPORT_BUTTON_TEXT = "Import";
static const char *IMPORT_BUTTON_TEXT_FAILED = "Retry import";
static const char *RESTART_BUTTON_TEXT = "Restart";

struct gui_remarkable_import_window {
	fbtk_widget_t *window_widget;

	fbtk_widget_t *title_widget;
	fbtk_widget_t *explanation_widget;

	fbtk_widget_t *close_button_widget;
	fbtk_widget_t *import_button_widget;
	fbtk_widget_t *restart_button_widget;

	char *file_path;
	char *display_name;
};

static void import_window_destroy(struct gui_remarkable_import_window *window)
{
	fbtk_widget_t *root;
	root = fbtk_get_root_widget(window->window_widget);
	fbtk_destroy_widget(window->window_widget);
	fbtk_request_redraw(root);

	free(window->file_path);
	free(window->display_name);

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

static int
import_button_click(struct fbtk_widget_s *widget, fbtk_callback_info *info)
{
	if (info->event->type == NSFB_EVENT_KEY_UP) {

		struct gui_remarkable_import_window *window = info->context;

		// TODO determine the file type by its content, and not its name
		const char *ext = get_extension(window->display_name);
		enum Filetype type;
		if (strcmp("pdf", ext) == 0) {
			type = pdf;
		} else if (strcmp("epub", ext) == 0) {
			type = epub;
		} else {
			fbtk_set_text(window->title_widget, TITLE_TEXT_FAILED);
			fbtk_set_text(window->explanation_widget,
				      INVALID_FORMAT_TEXT);
			return 0;
		}


		char *filename_without_extension = malloc(
			(strlen(window->display_name) + 1) * sizeof(char));
		get_filename_without_extension(window->display_name,
					       filename_without_extension);
		int import_result = import_file_to_xochitl(window->file_path,
							   filename_without_extension,
							   type);
		if (import_result != NSERROR_OK) {
			size_t failed_text_len = strlen(IMPORT_FAILED_TEXT) * 3;
			char *failed_text = malloc(failed_text_len *
						   sizeof(char));
			snprintf(failed_text,
				 failed_text_len,
				 IMPORT_FAILED_TEXT,
				 window->display_name,
				 import_result);
			fbtk_set_text(window->explanation_widget, failed_text);
			free(failed_text);
			fbtk_set_text(window->title_widget, TITLE_TEXT_FAILED);
			fbtk_set_text(window->import_button_widget,
				      IMPORT_BUTTON_TEXT_FAILED);
		} else {
			size_t completed_text_len =
				strlen(IMPORT_COMPLETED_TEXT) * 3;
			char *completed_text = malloc(completed_text_len *
						      sizeof(char));
			snprintf(completed_text,
				 completed_text_len,
				 IMPORT_COMPLETED_TEXT,
				 window->display_name);
			fbtk_set_text(window->explanation_widget,
				      completed_text);
			free(completed_text);
			fbtk_set_text(window->title_widget,
				      TITLE_TEXT_COMPLETED);

			fbtk_set_mapping(window->import_button_widget, false);
			fbtk_set_mapping(window->restart_button_widget, true);
		}
		free(filename_without_extension);
	}
	return 0;
}

static int
restart_button_click(struct fbtk_widget_s *widget, fbtk_callback_info *info)
{
	if (info->event->type == NSFB_EVENT_KEY_UP) {
        // TODO load from parameter
		system("systemctl restart remux");
	}
	return 0;
}

void import_window_open(const char *file_path,
			const char *display_name,
			struct gui_window *gui)
{
	const int WINDOW_MAX_HEIGHT = 500;
	int import_window_height = gui->window->height - gui->toolbar->height -
				   gui->status->height - 300;
	if (import_window_height > WINDOW_MAX_HEIGHT) {
		import_window_height = WINDOW_MAX_HEIGHT;
	}

	struct gui_remarkable_import_window *window = malloc(sizeof *window);

	window->file_path = malloc(strlen(file_path) + 1);
	window->display_name = malloc(strlen(display_name) + 1);
	strcpy(window->display_name, display_name);
	strcpy(window->file_path, file_path);

	window->window_widget = fbtk_create_window(
		gui->window,
		WINDOW_SPACING,
		gui->toolbar->height + WINDOW_SPACING,
		gui->window->width - gui->vscroll->width - WINDOW_SPACING * 2,
		import_window_height,
		FB_COLOUR_WHITE);

	const int BUTTON_WIDTH = 400;

	window->close_button_widget = fbtk_create_text_button(
		window->window_widget,
		window->window_widget->width / 2 + ELEMENTS_SPACING / 2,
		window->window_widget->height - BUTTON_HEIGHT -
			ELEMENTS_SPACING,
		BUTTON_WIDTH,
		BUTTON_HEIGHT,
		FB_COLOUR_LIGHTGREY,
		FB_COLOUR_BLACK,
		close_button_click,
		window);
	fbtk_set_text(window->close_button_widget, CLOSE_BUTTON_TEXT);

	window->import_button_widget = fbtk_create_text_button(
		window->window_widget,
		window->window_widget->width / 2 - BUTTON_WIDTH -
			ELEMENTS_SPACING / 2,
		window->window_widget->height - BUTTON_HEIGHT -
			ELEMENTS_SPACING,
		BUTTON_WIDTH,
		BUTTON_HEIGHT,
		FB_COLOUR_LIGHTGREY,
		FB_COLOUR_BLACK,
		import_button_click,
		window);
	fbtk_set_text(window->import_button_widget, IMPORT_BUTTON_TEXT);

	window->restart_button_widget = fbtk_create_text_button(
		window->window_widget,
		window->window_widget->width / 2 - BUTTON_WIDTH -
			ELEMENTS_SPACING / 2,
		window->window_widget->height - BUTTON_HEIGHT -
			ELEMENTS_SPACING,
		BUTTON_WIDTH,
		BUTTON_HEIGHT,
		FB_COLOUR_LIGHTGREY,
		FB_COLOUR_BLACK,
		restart_button_click,
		window);
	fbtk_set_text(window->restart_button_widget, RESTART_BUTTON_TEXT);

	window->explanation_widget = fbtk_create_text(
		window->window_widget,
		ELEMENTS_SPACING,
		ELEMENTS_SPACING * 2 + LARGE_TEXT_HEIGHT,
		window->window_widget->width - ELEMENTS_SPACING * 2,
		SMALL_TEXT_HEIGHT,
		FB_COLOUR_WHITE,
		FB_COLOUR_BLACK,
		false);
	size_t explanation_text_len = strlen(IMPORT_EXPLANATION_TEXT) * 3;
	char *explanation_text = malloc(explanation_text_len * sizeof(char));
	snprintf(explanation_text,
		 explanation_text_len,
		 IMPORT_EXPLANATION_TEXT,
		 window->display_name);
	fbtk_set_text(window->explanation_widget, explanation_text);
	free(explanation_text);

	window->title_widget = fbtk_create_text(window->window_widget,
						ELEMENTS_SPACING,
						ELEMENTS_SPACING,
						window->window_widget->width -
							ELEMENTS_SPACING * 2,
						LARGE_TEXT_HEIGHT,
						FB_COLOUR_WHITE,
						FB_COLOUR_BLACK,
						true);
	fbtk_set_text(window->title_widget, TITLE_TEXT);

	draw_border_outline(window->window_widget);

	fbtk_set_zorder(window->window_widget, INT_MIN);
	fbtk_set_mapping(window->window_widget, true);
	fbtk_set_mapping(window->restart_button_widget, false);
}
