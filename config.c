#include <strings.h>
#include <string.h>

#include "ctrlmenu.h"

struct Config config = {
	.fstrncmp               = strncmp,
	.fstrstr                = strstr,

	.faceName               = "monospace:size=9",

	.menubackground         = "#313131",
	.menuforeground         = "#FFFFFF",
	.menuselbackground      = "#3465A4",
	.menuselforeground      = "#FFFFFF",
	.menutopShadow          = "#737373",
	.menubottomShadow       = "#101010",
	.shadowThickness        = 2,
	.alignment              = ALIGN_LEFT,

	.runnerbackground       = "#0A0A0A",
	.runnerforeground       = "#FFFFFF",
	.runnerselbackground    = "#3465A4",
	.runnerselforeground    = "#FFFFFF",
	.runneraltforeground    = "#232323",
	.runneraltselforeground = "#232323",

	.number_items           = 8,

	.tornoff                = 0,
	.triangle_width         = 4,
	.triangle_height        = 8,

	.runner                 = "A-space",
	.altkey                 = "",
};
