#include "component_util.h"

const int ELEMENTS_SPACING = 20;
const int SMALL_TEXT_HEIGHT = 40;
const int LARGE_TEXT_HEIGHT = 60;
const int BUTTON_HEIGHT = 60;
const int WINDOW_BORDER_THICKNESS = 5;
const int WINDOW_SPACING = 50;

void draw_border_outline(fbtk_widget_t *widget)
{

	fbtk_create_fill(widget,
			 0,
			 0,
			 widget->width,
			 WINDOW_BORDER_THICKNESS,
			 FB_COLOUR_LIGHTGREY);
	fbtk_create_fill(widget,
			 0,
			 widget->height - WINDOW_BORDER_THICKNESS,
			 widget->width,
			 WINDOW_BORDER_THICKNESS,
			 FB_COLOUR_LIGHTGREY);
	fbtk_create_fill(widget,
			 0,
			 0,
			 WINDOW_BORDER_THICKNESS,
			 widget->height,
			 FB_COLOUR_LIGHTGREY);
	fbtk_create_fill(widget,
			 widget->width - WINDOW_BORDER_THICKNESS,
			 0,
			 widget->width,
			 widget->height,
			 FB_COLOUR_LIGHTGREY);
}
