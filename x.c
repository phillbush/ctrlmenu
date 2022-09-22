#include <err.h>
#include <locale.h>
#include <limits.h>
#include <stdlib.h>
#include <strings.h>

#include <X11/Xproto.h>

#include "ctrlmenu.h"

static XRectangle *mons = NULL;                 /* monitors */
static XrmDatabase xdb = NULL;
static Visual *visual;
static Colormap colormap;
static unsigned int depth;
static int nmons;
static int screen;
static int savedargc;
static char **savedargv;
static int (*xerrorxlib)(Display *, XErrorEvent *);

Atom atoms[ATOM_LAST];
struct DC dc;
Display *dpy;
XIM xim;
Window root;

static int
xerror(Display *dpy, XErrorEvent *e)
{
	if (e->request_code == X_GrabKey && e->error_code == BadAccess)
		return 0;
	return xerrorxlib(dpy, e);
	exit(1);        /* unreached */
}

static void
ealloccolor(const char *s, XftColor *color)
{
	if(!XftColorAllocName(dpy, visual, colormap, s, color))
		errx(1, "could not allocate color: %s", s);
}

static void
getresources(XrmDatabase xdb)
{
	XrmValue xval;
	long n;
	char *type;

	if (XrmGetResource(xdb, "ctrlmenu.faceName", "*", &type, &xval) == True)
		config.faceName = xval.addr;

	if (XrmGetResource(xdb, "ctrlmenu.menu.tornoff", "*", &type, &xval) == True) {
		if (strcasecmp(xval.addr, "true") == 0 ||
		    strcasecmp(xval.addr, "on") == 0 ||
		    strcasecmp(xval.addr, "1") == 0) {
			config.fstrncmp = strncasecmp;
			config.fstrstr  = strcasestr;
		} else {
			config.fstrncmp = strncmp;
			config.fstrstr  = strstr;
		}
	}
	if (XrmGetResource(xdb, "ctrlmenu.runner.keychord", "*", &type, &xval) == True)
		config.runner = xval.addr;
	if (XrmGetResource(xdb, "ctrlmenu.runner.background", "*", &type, &xval) == True)
		config.runnerbackground = xval.addr;
	if (XrmGetResource(xdb, "ctrlmenu.runner.foreground", "*", &type, &xval) == True)
		config.runnerforeground = xval.addr;
	if (XrmGetResource(xdb, "ctrlmenu.runner.selbackground", "*", &type, &xval) == True)
		config.runnerselbackground = xval.addr;
	if (XrmGetResource(xdb, "ctrlmenu.runner.selforeground", "*", &type, &xval) == True)
		config.runnerselforeground = xval.addr;
	if (XrmGetResource(xdb, "ctrlmenu.runner.altforeground", "*", &type, &xval) == True)
		config.runneraltforeground = xval.addr;
	if (XrmGetResource(xdb, "ctrlmenu.runner.altselforeground", "*", &type, &xval) == True)
		config.runneraltselforeground = xval.addr;

	if (XrmGetResource(xdb, "ctrlmenu.menu.keysym", "*", &type, &xval) == True)
		config.altkey = xval.addr;
	if (XrmGetResource(xdb, "ctrlmenu.menu.tornoff", "*", &type, &xval) == True)
		config.tornoff = strcasecmp(xval.addr, "true") == 0 ||
		                 strcasecmp(xval.addr, "on") == 0 ||
		                 strcasecmp(xval.addr, "1") == 0;
	if (XrmGetResource(xdb, "ctrlmenu.menu.background", "*", &type, &xval) == True)
		config.menubackground = xval.addr;
	if (XrmGetResource(xdb, "ctrlmenu.menu.foreground", "*", &type, &xval) == True)
		config.menuforeground = xval.addr;
	if (XrmGetResource(xdb, "ctrlmenu.menu.selbackground", "*", &type, &xval) == True)
		config.menuselbackground = xval.addr;
	if (XrmGetResource(xdb, "ctrlmenu.menu.selforeground", "*", &type, &xval) == True)
		config.menuselforeground = xval.addr;
	if (XrmGetResource(xdb, "ctrlmenu.menu.topShadow", "*", &type, &xval) == True)
		config.menutopShadow = xval.addr;
	if (XrmGetResource(xdb, "ctrlmenu.menu.bottomShadow", "*", &type, &xval) == True)
		config.menubottomShadow = xval.addr;
	if (XrmGetResource(xdb, "ctrlmenu.menu.shadowThickness", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0 && n < 100)
			config.shadowThickness = n;
	if (XrmGetResource(xdb, "ctrlmenu.menu.alignment", "*", &type, &xval) == True) {
		if (strcasecmp(xval.addr, "center") == 0)
			config.alignment = ALIGN_CENTER;
		else if (strcasecmp(xval.addr, "left") == 0)
			config.alignment = ALIGN_LEFT;
		else if (strcasecmp(xval.addr, "right") == 0)
			config.alignment = ALIGN_RIGHT;
	}
}

static void
initdc(void)
{
	/* get color pixels */
	ealloccolor(config.runnerbackground,       &dc.runnerbackground);
	ealloccolor(config.runnerforeground,       &dc.runnerforeground);
	ealloccolor(config.runnerselbackground,    &dc.runnerselbackground);
	ealloccolor(config.runnerselforeground,    &dc.runnerselforeground);
	ealloccolor(config.runneraltforeground,    &dc.runneraltforeground);
	ealloccolor(config.runneraltselforeground, &dc.runneraltselforeground);
	ealloccolor(config.menubackground,         &dc.menubackground);
	ealloccolor(config.menuforeground,         &dc.menuforeground);
	ealloccolor(config.menuselbackground,      &dc.menuselbackground);
	ealloccolor(config.menuselforeground,      &dc.menuselforeground);
	ealloccolor(config.menutopShadow,          &dc.menutopShadow);
	ealloccolor(config.menubottomShadow,       &dc.menubottomShadow);

	/* parse fonts */
	dc.face = XftFontOpenXlfd(dpy, screen, config.faceName);
	if (dc.face == NULL) {
		dc.face = XftFontOpenName(dpy, screen, config.faceName);
		if (dc.face == NULL) {
			errx(1, "could not open font: %s", config.faceName);
		}
	}
	config.fontheight = dc.face->height * 1.5;

	dc.gc = XCreateGC(dpy, root, 0, NULL);
}

static void
initatoms(void)
{
	char *atomnames[ATOM_LAST] = {
		[UTF8_STRING]                          = "UTF8_STRING",
		[CLIPBOARD]                            = "CLIPBOARD",
		[WM_DELETE_WINDOW]                     = "WM_DELETE_WINDOW",
		[_NET_WM_NAME]                         = "_NET_WM_NAME",
		[_NET_WM_WINDOW_TYPE]                  = "_NET_WM_WINDOW_TYPE",
		[_NET_WM_WINDOW_TYPE_MENU]             = "_NET_WM_WINDOW_TYPE_MENU",
		[_NET_WM_WINDOW_TYPE_POPUP_MENU]       = "_NET_WM_WINDOW_TYPE_MENU",
		[_NET_WM_WINDOW_TYPE_PROMPT]           = "_NET_WM_WINDOW_TYPE_PROMPT",
	};

	XInternAtoms(dpy, atomnames, ATOM_LAST, False, atoms);
}

static void
cleandc(void)
{
	XftColorFree(dpy, visual, colormap, &dc.runnerbackground);
	XftColorFree(dpy, visual, colormap, &dc.runnerforeground);
	XftColorFree(dpy, visual, colormap, &dc.runnerselbackground);
	XftColorFree(dpy, visual, colormap, &dc.runnerselforeground);
	XftColorFree(dpy, visual, colormap, &dc.runneraltforeground);
	XftColorFree(dpy, visual, colormap, &dc.runneraltselforeground);
	XftColorFree(dpy, visual, colormap, &dc.menubackground);
	XftColorFree(dpy, visual, colormap, &dc.menuforeground);
	XftColorFree(dpy, visual, colormap, &dc.menuselbackground);
	XftColorFree(dpy, visual, colormap, &dc.menuselforeground);
	XftColorFree(dpy, visual, colormap, &dc.menutopShadow);
	XftColorFree(dpy, visual, colormap, &dc.menubottomShadow);
	XftFontClose(dpy, dc.face);
	XFreeGC(dpy, dc.gc);
}

int
textwidth(const char *text, int len)
{
	XGlyphInfo box;

	XftTextExtentsUtf8(dpy, dc.face, text, len, &box);
	return box.width;
}

void
drawtext(Drawable pix, XftColor *color, int x, int y, const char *text, int len)
{
	XftDraw *draw;

	draw = XftDrawCreate(dpy, pix, visual, colormap);
	XftDrawStringUtf8(draw, color, dc.face, x, y, text, len);
	XftDrawDestroy(draw);
}

void
drawtriangle(Drawable pix, unsigned int color, int x, int y, int direction)
{
	XPoint *triangle;
#define NPOINTS 4

	if (direction == DIR_UP) {
		triangle = (XPoint []){
			{x, y + config.triangle_width},
			{x + config.triangle_height / 2, y},
			{x + config.triangle_height, y + config.triangle_width},
			{x, y + config.triangle_width}
		};
	} else if (direction == DIR_DOWN) {
		triangle = (XPoint []){
			{x, y},
			{x + config.triangle_height / 2, y + config.triangle_width},
			{x + config.triangle_height, y},
			{x, y}
		};
	} else if (direction == DIR_LEFT) {
		triangle = (XPoint []){
			{x + config.triangle_width, y},
			{x - config.triangle_width, y + config.triangle_height / 2},
			{x, y + config.triangle_height},
			{x, y}
		};
	} else {
		triangle = (XPoint []){
			{x, y},
			{x + config.triangle_width, y + config.triangle_height / 2},
			{x, y + config.triangle_height},
			{x, y}
		};
	}
	XSetForeground(dpy, dc.gc, color);
	XFillPolygon(
		dpy,
		pix,
		dc.gc,
		triangle,
		NPOINTS,
		Convex, CoordModeOrigin
	);
}

void
getmonitors(void)
{
	XineramaScreenInfo *info = NULL;
	int i;

	free(mons);
	if ((info = XineramaQueryScreens(dpy, &nmons)) != NULL) {
		mons = ecalloc(nmons, sizeof(*mons));
		for (i = 0; i < nmons; i++) {
			mons[i].x = info[i].x_org;
			mons[i].y = info[i].y_org;
			mons[i].width = info[i].width;
			mons[i].height = info[i].height;
		}
		XFree(info);
	} else {
		mons = emalloc(sizeof(*mons));
		mons->x = mons->y = 0;
		mons->width = DisplayWidth(dpy, screen);
		mons->height = DisplayHeight(dpy, screen);
	}
}

void
drawrectangle(Pixmap pix, XRectangle rect, unsigned int color)
{
	if (rect.width <= 0 || rect.height <= 0)
		return;
	XSetForeground(dpy, dc.gc, color);
	XFillRectangle(dpy, pix, dc.gc, rect.x, rect.y, rect.width, rect.height);
}

void
drawshadows(Pixmap pix, XRectangle rect, unsigned long top, unsigned long bot)
{
	XGCValues val;
	XRectangle *recs;
	int i;

	if (rect.width <= 0 || rect.height <= 0)
		return;

	recs = ecalloc(config.shadowThickness * 2, sizeof(*recs));

	/* draw light shadow */
	for(i = 0; i < config.shadowThickness; i++) {
		recs[i * 2]     = (XRectangle){.x = rect.x + i, .y = rect.y + i, .width = 1, .height = rect.height - (i * 2 + 1)};
		recs[i * 2 + 1] = (XRectangle){.x = rect.x + i, .y = rect.y + i, .width = rect.width - (i * 2 + 1), .height = 1};
	}
	val.foreground = top;
	XChangeGC(dpy, dc.gc, GCForeground, &val);
	XFillRectangles(dpy, pix, dc.gc, recs, config.shadowThickness * 2);

	/* draw dark shadow */
	for(i = 0; i < config.shadowThickness; i++) {
		recs[i * 2]     = (XRectangle){.x = rect.x + rect.width - 1 - i, .y = rect.y + i,         .width = 1,     .height = rect.height - i * 2};
		recs[i * 2 + 1] = (XRectangle){.x = rect.x + i,         .y = rect.y + rect.height - 1 - i, .width = rect.width - i * 2, .height = 1};
	}
	val.foreground = bot;
	XChangeGC(dpy, dc.gc, GCForeground, &val);
	XFillRectangles(dpy, pix, dc.gc, recs, config.shadowThickness * 2);

	free(recs);
}

void
drawseparator(Drawable pix, int x, int y, int w, int dash)
{
	XGCValues val;

	val.line_style = dash ? LineOnOffDash : LineSolid;

	/* draw light shadow */
	w--;
	val.foreground = dc.menubottomShadow.pixel;
	XChangeGC(dpy, dc.gc, GCForeground | GCLineStyle, &val);
	XDrawLine(dpy, pix, dc.gc, x, y, x + w, y);

	/* draw dark shadow */
	x++;
	y++;
	val.foreground = dc.menutopShadow.pixel;
	XChangeGC(dpy, dc.gc, GCForeground | GCLineStyle, &val);
	XDrawLine(dpy, pix, dc.gc, x, y, x + w, y);
}

void
commitdrawing(Window win, Pixmap pix, XRectangle rect)
{
	XCopyArea(dpy, pix, win, dc.gc, 0, 0, rect.width, rect.height, 0, 0);
}

XRectangle
getselmon(XRectangle *rect)
{
	int selmon, i;

	selmon = 0;
	for (i = 0; i < nmons; i++) {
		if (rect->x >= mons[i].x && rect->x < mons[i].x + mons[i].width &&
		    rect->y >= mons[i].y && rect->y < mons[i].y + mons[i].height) {
			selmon = i;
			break;
		}
	}
	return mons[selmon];
}

Window
createwindow(XRectangle *rect, int type, const char *title)
{
	XClassHint classh;
	XSetWindowAttributes swa;
	XSizeHints sizeh;
	XWMHints wmhints;
	Atom typeatom;
	Window win;

	swa.override_redirect = (type == MENU_POPUP) ? True : False;
	swa.save_under = (type == MENU_POPUP) ? True : False;
	swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask
	               | ButtonPressMask | ButtonReleaseMask | PointerMotionMask
		       | LeaveWindowMask | StructureNotifyMask;
	win = XCreateWindow(
		dpy,
		root,
		rect->x, rect->y, rect->width, rect->height, 0,
		depth,
		CopyFromParent,
		visual,
		CWOverrideRedirect | CWEventMask | CWSaveUnder,
		&swa
	);
	if (type == MENU_DOCKAPP) {
		wmhints.flags = IconWindowHint | StateHint;
		wmhints.initial_state = WithdrawnState;
		wmhints.icon_window = win;
	} else {
		wmhints.flags = 0;
	}
	if (type == MENU_DOCKAPP) {
		sizeh.flags = 0;
	} else {
		sizeh.flags = USPosition | PMaxSize | PMinSize;
		sizeh.min_width = sizeh.max_width = rect->width;
		sizeh.min_height = sizeh.max_height = rect->height;
	}
	classh.res_class = CLASS,
	classh.res_name = savedargv[0],
	XmbSetWMProperties(dpy, win, title, title, savedargv, savedargc, &sizeh, &wmhints, &classh);
	if (type == RUNNER)
		typeatom = atoms[_NET_WM_WINDOW_TYPE_PROMPT];
	else if (type == MENU_TORNOFF)
		typeatom = atoms[_NET_WM_WINDOW_TYPE_MENU];
	else
		typeatom = atoms[_NET_WM_WINDOW_TYPE_POPUP_MENU];
	XChangeProperty(dpy, win, atoms[_NET_WM_WINDOW_TYPE], XA_ATOM, 32, PropModeReplace, (unsigned char *)&typeatom, 1);
	XSetWMProtocols(dpy, win, &atoms[WM_DELETE_WINDOW], 1);
	return win;
}

void
mapwin(Window win)
{
	XMapRaised(dpy, win);
}

void
unmapwin(Window win)
{
	XMapRaised(dpy, win);
}

void
freepixmap(Pixmap pix)
{
	XFreePixmap(dpy, pix);
}

Pixmap
createpixmap(XRectangle rect, Window win)
{
	Pixmap pix;

	if ((pix = XCreatePixmap(dpy, win, rect.width, rect.height, depth)) == None)
		errx(1, "could not create pixmap");
	return pix;
}

KeySym
getkeysym(const char *str)
{
	KeySym ksym, lower, upper;

	if (str == NULL || *str == '\0')
		return NoSymbol;
	ksym = XStringToKeysym(str);
	XConvertCase(ksym, &lower, &upper);
	return lower;
}

KeyCode
getkeycode(const char *str)
{
	KeySym ksym;
	KeyCode key;

	ksym = getkeysym(str);
	key = XKeysymToKeycode(dpy, ksym);
	return key;
}

void
grabkey(KeyCode key, unsigned int mods)
{
	XGrabKey(
		dpy,
		key,
		mods,
		root,
		False,
		GrabModeAsync,
		GrabModeAsync
	);
}

void
grabsync(KeyCode key)
{
	XGrabKey(
		dpy,
		key,
		AnyModifier,
		root,
		False,
		GrabModeAsync,
		GrabModeSync
	);
}

int
grab(int grabwhat)
{
	if ((grabwhat & GRAB_POINTER) && XGrabPointer(dpy, root, True, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime) != GrabSuccess)
		return -1;
	if ((grabwhat & GRAB_KEYBOARD) && XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime) != GrabSuccess)
		return -1;
	return 0;
}

void
ungrab(void)
{
	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
}

void
translatecoordinates(Window win, XRectangle *rect)
{
	Window dw;
	int x, y;

	XTranslateCoordinates(dpy, win, root, 0, 0, &x, &y, &dw);
	rect->x = x;
	rect->y = y;
}

void
xinit(int argc, char *argv[])
{
	char *xrm;

	XInitThreads();
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		warnx("warning: no locale support");
	if ((dpy = XOpenDisplay(NULL)) == NULL)
		errx(1, "could not open display");
	if ((xim = XOpenIM(dpy, NULL, NULL, NULL)) == NULL)
		errx(1, "XOpenIM: could not open input device");
	if ((xrm = XResourceManagerString(dpy)) != NULL)
		if ((xdb = XrmGetStringDatabase(xrm)) != NULL)
			getresources(xdb);
	root = DefaultRootWindow(dpy);
	screen = DefaultScreen(dpy);
	depth = DefaultDepth(dpy, screen);
	visual = DefaultVisual(dpy, screen);
	colormap = DefaultColormap(dpy, screen);
	xerrorxlib = XSetErrorHandler(xerror);
	XSelectInput(dpy, root, KeyPressMask | KeyReleaseMask);
	initdc();
	initatoms();
	savedargc = argc;
	savedargv = argv;
}

void
xclose(void)
{
	if (xdb != NULL)
		XrmDestroyDatabase(xdb);
	cleandc();
	XCloseIM(xim);
	XCloseDisplay(dpy);
}
