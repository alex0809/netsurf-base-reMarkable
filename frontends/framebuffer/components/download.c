#include <bits/types/clock_t.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <malloc.h>
#include <stdbool.h>
#include <limits.h>
#include <linux/limits.h>
#include <time.h>
#include <sys/stat.h>

#include "netsurf/browser_window.h"
#include "netsurf/types.h"
#include "libnsfb.h"
#include "libnsfb_event.h"
#include "desktop/download.h"
#include "netsurf/download.h"
#include "utils/errors.h"
#include "utils/file.h"
#include "utils/log.h"
#include "utils/nsoption.h"
#include "utils/nsurl.h"
#include "component_util.h"
#include "remarkable_import.h"
#include "utils/filename_utils.h"

#include "framebuffer/gui.h"
#include "framebuffer/fbtk.h"
#include "framebuffer/fbtk/widget.h"

static const int TEXTS_MAX_LENGTH = 100;

struct gui_download_window {
	download_context *ctx;
	bool download_active;

	struct gui_window *gui;

	fbtk_widget_t *download_window_widget;
	fbtk_widget_t *title_widget;
	fbtk_widget_t *destination_widget;
	fbtk_widget_t *progress_widget;

	fbtk_widget_t *cancel_button;
	fbtk_widget_t *close_button;

	fbtk_widget_t *delete_file_button;
	fbtk_widget_t *show_directory_button;
#ifdef REMARKABLE
	fbtk_widget_t *import_remarkable_button;
#endif

	char *full_path_name;
	FILE *output_file;

	const char *mime_type;
	char *filename;

	unsigned long long progress;
	unsigned long long total_length;

	time_t progress_last_update;

	char *title_text;
	char *destination_text;
	char *progress_text;
};

static void download_window_destroy(struct gui_download_window *dw)
{
	fbtk_widget_t *root;
	root = fbtk_get_root_widget(dw->download_window_widget);
	fbtk_destroy_widget(dw->download_window_widget);
	fbtk_request_redraw(root);

	free(dw->title_text);
	free(dw->destination_text);
	free(dw->progress_text);
	free(dw->full_path_name);

	free(dw);
}

static void download_window_recalculate_progress(struct gui_download_window *dw)
{
	time_t current_time;
	time(&current_time);
	time_t diff = current_time - dw->progress_last_update;

	if (diff > 1 || dw->progress == 0 || !dw->download_active) {
		if (dw->total_length == 0) {
			snprintf(
				dw->progress_text,
				100,
				"Progress: ? %% (%llu bytes of unknown total size)",
				dw->progress);
		} else {
			int percentage = (float)dw->progress /
					 dw->total_length * 100.0;
			snprintf(dw->progress_text,
				 100,
				 "Progress: %3d %% (%llu of %llu bytes)",
				 percentage,
				 dw->progress,
				 dw->total_length);
		}
		fbtk_set_text(dw->progress_widget, dw->progress_text);
		dw->progress_last_update = current_time;
		fbtk_request_redraw(dw->download_window_widget);
	}
}

static void change_to_close_button(struct gui_download_window *dw)
{
	fbtk_set_mapping(dw->cancel_button, false);
	fbtk_set_mapping(dw->close_button, true);
}

/**
 * This will set the download window to finished error state, and close + delete
 * the download file.
 */
static void handle_and_display_error(struct gui_download_window *dw,
				     const char *error_msg,
				     const char *title_msg)
{
	NSLOG(netsurf, WARN, "Error on download: %s", error_msg);

	snprintf(dw->progress_text, TEXTS_MAX_LENGTH, "%s", error_msg);
	fbtk_set_text(dw->progress_widget, dw->progress_text);

	snprintf(dw->title_text, TEXTS_MAX_LENGTH, "%s", title_msg);
	fbtk_set_text(dw->title_widget, dw->title_text);

	if (dw->output_file != NULL) {
		NSLOG(netsurf, INFO, "Closing output file");
		fclose(dw->output_file);
		remove(dw->full_path_name);
	}

	dw->download_active = false;
	change_to_close_button(dw);
}


#ifdef REMARKABLE
static int
import_file_button_click(struct fbtk_widget_s *widget, fbtk_callback_info *info)
{
	if (info->event->type == NSFB_EVENT_KEY_UP) {
		struct gui_download_window *dw = info->context;
		import_window_open(dw->full_path_name, dw->filename, dw->gui);
		download_window_destroy(dw);
	}
	return 0;
}
#endif

static int
delete_button_click(struct fbtk_widget_s *widget, fbtk_callback_info *info)
{
	if (info->event->type == NSFB_EVENT_KEY_UP) {
		struct gui_download_window *dw = info->context;
		if (remove(dw->full_path_name) != 0) {
			NSLOG(netsurf,
			      ERROR,
			      "Could not remove file %s (errno %d)",
			      dw->full_path_name,
			      errno);
		}
		download_window_destroy(dw);
	}
	return 0;
}

static int show_directory_button_click(struct fbtk_widget_s *widget,
				       fbtk_callback_info *info)
{
	if (info->event->type == NSFB_EVENT_KEY_UP) {
		struct gui_download_window *dw = info->context;
		struct nsurl *url;
		char *path = malloc((PATH_MAX + 10) * sizeof(char));
		get_path_url_without_filename(dw->full_path_name, path);
		if (nsurl_create(path, &url) == NSERROR_OK) {
			browser_window_navigate(dw->gui->bw,
						url,
						NULL,
						BW_NAVIGATE_HISTORY,
						NULL,
						NULL,
						NULL);
		} else {
			NSLOG(netsurf,
			      ERROR,
			      "Could not create URL for navigating to %s",
			      path);
		}

		download_window_destroy(dw);
		free(path);
	}
	return 0;
}

static int
cancel_button_click(struct fbtk_widget_s *widget, fbtk_callback_info *info)
{
	if (info->event->type == NSFB_EVENT_KEY_UP) {
		struct gui_download_window *dw = info->context;
		download_context_abort(dw->ctx);
		download_context_destroy(dw->ctx);
		handle_and_display_error(dw,
					 "Download was cancelled",
					 "Download cancelled");
	}
	return 0;
}

static int
close_button_click(struct fbtk_widget_s *widget, fbtk_callback_info *info)
{
	if (info->event->type == NSFB_EVENT_KEY_UP) {
		struct gui_download_window *dw = info->context;
		download_window_destroy(dw);
	}
	return 0;
}

/**
 * Build the full filename (including appending a number if the file already
 * exists) and open the file desciptor for the output file
 *
 * On success, return 0. On failure, return -1.
 */
static int prepare_output_file(struct gui_download_window *dw)
{
	if ((nsoption_charp(fb_download_directory) == NULL) ||
	    (nsoption_charp(fb_download_directory)[0] == '\0')) {
		handle_and_display_error(
			dw,
			"Download directory is not set. Please set via option \"fb_download_directory\".",
			"Download error");
		return -1;
	}

	snprintf(dw->full_path_name,
		 PATH_MAX,
		 "%s/%s",
		 nsoption_charp(fb_download_directory),
		 dw->filename);

	struct stat check_file;
	int stat_result = stat(dw->full_path_name, &check_file);
	int next_number = 1;
	while (stat_result == 0) {
		char *filename_without_extension = malloc(sizeof(char) *
							  strlen(dw->filename));
		get_filename_without_extension(dw->filename,
					       filename_without_extension);

		snprintf(dw->full_path_name,
			 PATH_MAX,
			 "%s/%s (%d).%s",
			 nsoption_charp(fb_download_directory),
			 filename_without_extension,
			 next_number,
			 get_extension(dw->filename));
		stat_result = stat(dw->full_path_name, &check_file);
		next_number++;

		free(filename_without_extension);
	}

	netsurf_mkdir_all(dw->full_path_name);
	dw->output_file = fopen(dw->full_path_name, "w");
	char error_text[TEXTS_MAX_LENGTH];
	snprintf(error_text,
		 TEXTS_MAX_LENGTH,
		 "Error attempting to open output file. Errno: %d",
		 errno);

	if (dw->output_file == NULL) {
		handle_and_display_error(dw, error_text, "Download error");
		return -1;
	}

	snprintf(dw->destination_text,
		 TEXTS_MAX_LENGTH,
		 "Output file: %s",
		 dw->full_path_name);
	fbtk_set_text(dw->destination_widget, dw->destination_text);

	return 0;
}

static struct gui_download_window *
gui_download_create(download_context *ctx, struct gui_window *gui)
{
	NSLOG(netsurf,
	      INFO,
	      "Creating download window for file with name %s, total size %llu",
	      download_context_get_filename(ctx),
	      download_context_get_total_length(ctx));

	struct gui_download_window *dw;
	dw = malloc(sizeof *dw);

	dw->download_active = true;
	dw->progress = 0;
	dw->progress_last_update = 0;
	dw->ctx = ctx;
	dw->total_length = download_context_get_total_length(ctx);
	dw->mime_type = download_context_get_mime_type(ctx);
	dw->output_file = NULL;
    dw->filename = malloc(strlen(download_context_get_filename(ctx) + 1));
	strcpy(dw->filename, download_context_get_filename(ctx));
	dw->gui = gui;

	dw->title_text = (char *)malloc(TEXTS_MAX_LENGTH * sizeof(char));
	dw->progress_text = (char *)malloc(TEXTS_MAX_LENGTH * sizeof(char));
	dw->destination_text = (char *)malloc(TEXTS_MAX_LENGTH * sizeof(char));
	dw->full_path_name = malloc(PATH_MAX * sizeof(char));

	const int DOWNLOAD_WIDGET_MAX_HEIGHT = 500;

	const int DOWNLOAD_CANCEL_BUTTON_WIDTH = 400;
	const int DOWNLOAD_CLOSE_BUTTON_WIDTH = 300;

	int download_window_widget_height = gui->window->height -
					    gui->toolbar->height -
					    gui->status->height - 300;
	if (download_window_widget_height > DOWNLOAD_WIDGET_MAX_HEIGHT)
		download_window_widget_height = DOWNLOAD_WIDGET_MAX_HEIGHT;
	dw->download_window_widget = fbtk_create_window(
		gui->window,
		WINDOW_SPACING,
		gui->toolbar->height + WINDOW_SPACING,
		gui->window->width - gui->vscroll->width - WINDOW_SPACING * 2,
		download_window_widget_height,
		FB_COLOUR_WHITE);
	dw->title_widget = fbtk_create_text(dw->download_window_widget,
					    ELEMENTS_SPACING,
					    ELEMENTS_SPACING,
					    dw->download_window_widget->width -
						    ELEMENTS_SPACING * 2,
					    LARGE_TEXT_HEIGHT,
					    FB_COLOUR_WHITE,
					    FB_COLOUR_BLACK,
					    true);
	snprintf(dw->title_text, TEXTS_MAX_LENGTH, "Downloading...");
	fbtk_set_text(dw->title_widget, dw->title_text);

	draw_border_outline(dw->download_window_widget);

	dw->destination_widget = fbtk_create_text(
		dw->download_window_widget,
		ELEMENTS_SPACING,
		dw->title_widget->y + dw->title_widget->height +
			ELEMENTS_SPACING,
		dw->download_window_widget->width - ELEMENTS_SPACING * 2,
		SMALL_TEXT_HEIGHT,
		FB_COLOUR_WHITE,
		FB_COLOUR_BLACK,
		false);

	dw->progress_widget = fbtk_create_text(
		dw->download_window_widget,
		ELEMENTS_SPACING,
		dw->destination_widget->y + dw->destination_widget->height +
			ELEMENTS_SPACING,
		dw->download_window_widget->width - ELEMENTS_SPACING * 2,
		SMALL_TEXT_HEIGHT,
		FB_COLOUR_WHITE,
		FB_COLOUR_BLACK,
		false);
	download_window_recalculate_progress(dw);

	dw->cancel_button = fbtk_create_text_button(
		dw->download_window_widget,
		dw->download_window_widget->width / 2 -
			DOWNLOAD_CANCEL_BUTTON_WIDTH / 2,
		dw->download_window_widget->height - BUTTON_HEIGHT -
			ELEMENTS_SPACING,
		DOWNLOAD_CANCEL_BUTTON_WIDTH,
		BUTTON_HEIGHT,
		FB_COLOUR_LIGHTGREY,
		FB_COLOUR_BLACK,
		cancel_button_click,
		dw);
	fbtk_set_text(dw->cancel_button, "Cancel download");

	dw->close_button = fbtk_create_text_button(
		dw->download_window_widget,
		dw->download_window_widget->width / 2 -
			DOWNLOAD_CLOSE_BUTTON_WIDTH / 2,
		dw->download_window_widget->height - BUTTON_HEIGHT -
			ELEMENTS_SPACING,
		DOWNLOAD_CLOSE_BUTTON_WIDTH,
		BUTTON_HEIGHT,
		FB_COLOUR_LIGHTGREY,
		FB_COLOUR_BLACK,
		close_button_click,
		dw);
	fbtk_set_text(dw->close_button, "Close window");
	fbtk_set_mapping(dw->close_button, false);

	int success_buttons_width = (dw->download_window_widget->width -
				     ELEMENTS_SPACING * 4) /
				    3;
#ifdef REMARKABLE
	dw->import_remarkable_button = fbtk_create_text_button(
		dw->download_window_widget,
		ELEMENTS_SPACING,
		dw->download_window_widget->height - BUTTON_HEIGHT * 2 -
			ELEMENTS_SPACING * 2,
		success_buttons_width,
		BUTTON_HEIGHT,
		FB_COLOUR_LIGHTGREY,
		FB_COLOUR_BLACK,
		import_file_button_click,
		dw);
	fbtk_set_text(dw->import_remarkable_button, "Import to reMarkable");
	fbtk_set_mapping(dw->import_remarkable_button, false);
#endif

	dw->show_directory_button = fbtk_create_text_button(
		dw->download_window_widget,
		ELEMENTS_SPACING * 2 + success_buttons_width,
		dw->download_window_widget->height - BUTTON_HEIGHT * 2 -
			ELEMENTS_SPACING * 2,
		success_buttons_width,
		BUTTON_HEIGHT,
		FB_COLOUR_LIGHTGREY,
		FB_COLOUR_BLACK,
		show_directory_button_click,
		dw);
	fbtk_set_text(dw->show_directory_button, "Open directory");
	fbtk_set_mapping(dw->show_directory_button, false);

	dw->delete_file_button = fbtk_create_text_button(
		dw->download_window_widget,
		ELEMENTS_SPACING * 3 + success_buttons_width * 2,
		dw->download_window_widget->height - BUTTON_HEIGHT * 2 -
			ELEMENTS_SPACING * 2,
		success_buttons_width,
		BUTTON_HEIGHT,
		FB_COLOUR_LIGHTGREY,
		FB_COLOUR_BLACK,
		delete_button_click,
		dw);
	fbtk_set_text(dw->delete_file_button, "Delete file");
	fbtk_set_mapping(dw->delete_file_button, false);

	fbtk_set_mapping(dw->download_window_widget, true);
	fbtk_set_zorder(dw->download_window_widget, INT_MIN + 1);

	if (prepare_output_file(dw) != 0) {
		NSLOG(netsurf, WARN, "Could not prepare output file");
		return NULL;
	}

	return dw;
}

static nserror gui_download_data(struct gui_download_window *dw,
				 const char *data,
				 unsigned int size)
{
	NSLOG(netsurf,
	      DEBUG,
	      "Received download update with data of size %d",
	      size);

	if (fwrite(data, sizeof(char), size, dw->output_file) != size) {
		char error_text[TEXTS_MAX_LENGTH];
		snprintf(error_text,
			 TEXTS_MAX_LENGTH,
			 "Error writing file to disk: error code %d",
			 errno);
		handle_and_display_error(dw, error_text, "Download error");
		return NSERROR_SAVE_FAILED;
	}

	dw->progress += size;
	download_window_recalculate_progress(dw);

	return NSERROR_OK;
}


static void
gui_download_error(struct gui_download_window *dw, const char *error_msg)
{
	handle_and_display_error(dw, error_msg, "Download error");
	download_context_destroy(dw->ctx);
}

static void gui_download_done(struct gui_download_window *dw)
{
	// Download done is also called after cancel/error
	if (dw->download_active) {
		snprintf(dw->title_text,
			 TEXTS_MAX_LENGTH,
			 "Download finished!");
		fbtk_set_text(dw->title_widget, dw->title_text);

		fclose(dw->output_file);
		dw->download_active = false;
		download_window_recalculate_progress(dw);
		download_context_destroy(dw->ctx);
		change_to_close_button(dw);
#ifdef REMARKABLE
		fbtk_set_mapping(dw->import_remarkable_button, true);
#endif
		fbtk_set_mapping(dw->show_directory_button, true);
		fbtk_set_mapping(dw->delete_file_button, true);
	}
}

static struct gui_download_table download_table = {
	.create = gui_download_create,
	.data = gui_download_data,
	.error = gui_download_error,
	.done = gui_download_done,
};

struct gui_download_table *framebuffer_download_table = &download_table;
