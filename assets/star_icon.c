/* Vendors the LVGL example "img_star" asset into the project so it is always
 * compiled, independent of LV_BUILD_EXAMPLES (which is off in this build).
 * Used as the favorite-toggle icon in the Macros panel (recolored at runtime). */
#include "lvgl/lvgl.h"

#undef LV_BUILD_EXAMPLES
#define LV_BUILD_EXAMPLES 1
#include "lvgl/examples/assets/img_star.c"
