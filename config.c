#include <strings.h>
#include <string.h>

#include "ctrlmenu.h"

struct Config config = {
	.fstrncmp               = strncmp,
	.fstrstr                = strstr,

	.faceName               = "monospace:size=9",

	.colors[COLOR_MENU] = {
		.background             = "#313131",
		.foreground             = "#FFFFFF",
		.selbackground          = "#3465A4",
		.selforeground          = "#FFFFFF",
		.altforeground          = "#707880",
		.altselforeground       = "#707880",
	},

	.colors[COLOR_RUNNER] = {
		.background             = "#0A0A0A",
		.foreground             = "#FFFFFF",
		.selbackground          = "#3465A4",
		.selforeground          = "#FFFFFF",
		.altforeground          = "#707880",
		.altselforeground       = "#707880",
	},

	.topShadow              = "#737373",
	.bottomShadow           = "#101010",
	.shadowThickness        = 2,
	.gap                    = 0,
	.alignment              = ALIGN_LEFT,

	.max_items              = 0,
	.tornoff                = 0,
	.mode                   = 0,
	.runnergroup            = 0,
	.runnertsv              = 0,

	.runner_items           = 8,
	.runner                 = NULL,
	.altkey                 = NULL,
	.button                 = NULL,

	.itemheight             = 24,
	.triangle_width         = 4,
	.triangle_height        = 8,
	.iconsize               = 16,
	.iconpath               = NULL,
};
