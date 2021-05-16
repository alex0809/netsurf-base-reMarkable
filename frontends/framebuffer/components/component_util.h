#ifndef WIDGET_CONSTANTS
#define WIDGET_CONSTANTS

#include <stdbool.h>

#include "libnsfb.h"
#include "framebuffer/gui.h"
#include "framebuffer/fbtk.h"
#include "framebuffer/fbtk/widget.h"

extern const int ELEMENTS_SPACING;
extern const int SMALL_TEXT_HEIGHT;
extern const int LARGE_TEXT_HEIGHT;
extern const int BUTTON_HEIGHT;
extern const int WINDOW_BORDER_THICKNESS;
extern const int WINDOW_SPACING;

void draw_border_outline(fbtk_widget_t *widget);

#endif
