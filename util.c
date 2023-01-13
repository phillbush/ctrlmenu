#include <err.h>
#include <ctype.h>
#include <locale.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xproto.h>
#include <X11/xpm.h>

#include "ctrlmenu.h"

#define DEFOPENER    "xdg_open" /* default opener */
#define DEFCALC      "bc"       /* default calculator */
#define SHELL "sh"

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
XSyncCounter servertime;
int sync_event;
char *opener, *calculator;

static int
xerror(Display *dpy, XErrorEvent *e)
{
	if (e->request_code == X_GrabKey && (e->error_code == BadAccess || e->error_code == BadValue))
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

void
drawtext(Drawable pix, XftColor *color, int x, int y, const char *text, int len)
{
	XftDraw *draw;

	draw = XftDrawCreate(dpy, pix, visual, colormap);
	XftDrawStringUtf8(draw, color, dc.face, x, y, text, len);
	XftDrawDestroy(draw);
}

int
textwidth(const char *text, int len)
{
	XGlyphInfo box;

	XftTextExtentsUtf8(dpy, dc.face, text, len, &box);
	return box.width;
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
	int i;

	for (i = 0; i < COLOR_LAST; i++) {
		XftColorFree(dpy, visual, colormap, &dc.colors[i].background);
		XftColorFree(dpy, visual, colormap, &dc.colors[i].foreground);
		XftColorFree(dpy, visual, colormap, &dc.colors[i].selbackground);
		XftColorFree(dpy, visual, colormap, &dc.colors[i].selforeground);
		XftColorFree(dpy, visual, colormap, &dc.colors[i].altforeground);
		XftColorFree(dpy, visual, colormap, &dc.colors[i].altselforeground);
	}
	XftColorFree(dpy, visual, colormap, &dc.topShadow);
	XftColorFree(dpy, visual, colormap, &dc.bottomShadow);
	XftFontClose(dpy, dc.face);
	XFreeGC(dpy, dc.gc);
}

int
max(int x, int y)
{
	return x > y ? x : y;
}

int
min(int x, int y)
{
	return x < y ? x : y;
}

void *
emalloc(size_t size)
{
	void *p;

	if ((p = malloc(size)) == NULL)
		err(1, "malloc");
	return p;
}

void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;

	if ((p = calloc(nmemb, size)) == NULL)
		err(1, "calloc");
	return p;
}

void *
erealloc(void *ptr, size_t size)
{
	void *p;

	if ((p = realloc(ptr, size)) == NULL)
		err(1, "realloc");
	return p;
}

char *
estrdup(const char *s)
{
	char *t;

	if ((t = strdup(s)) == NULL)
		err(1, "strdup");
	return t;
}

char *
estrndup(const char *s, size_t maxlen)
{
	char *t;

	if ((t = strndup(s, maxlen)) == NULL)
		err(1, "strndup");
	return t;
}

void
epipe(int fd[])
{
	if (pipe(fd) == -1) {
		err(1, "pipe");
	}
}

void
eexecshell(const char *cmd, const char *arg)
{
	if (execlp(SHELL, SHELL, "-c", cmd, SHELL, arg, NULL) == -1) {
		err(1, "%s", "execlp");
	}
}

void
eexeccmd(const char *cmd, const char *arg)
{
	if (execlp(cmd, cmd, arg, NULL) == -1) {
		err(1, "%s", "execlp");
	}
}

void
edup2(int fd1, int fd2)
{
	if (dup2(fd1, fd2) == -1)
		err(1, "dup2");
	close(fd1);
}

pid_t
efork(void)
{
	pid_t pid;

	if ((pid = fork()) == -1)
		err(1, "fork");
	return pid;
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
	val.foreground = dc.bottomShadow.pixel;
	XChangeGC(dpy, dc.gc, GCForeground | GCLineStyle, &val);
	XDrawLine(dpy, pix, dc.gc, x, y, x + w, y);

	/* draw dark shadow */
	x++;
	y++;
	val.foreground = dc.topShadow.pixel;
	XChangeGC(dpy, dc.gc, GCForeground | GCLineStyle, &val);
	XDrawLine(dpy, pix, dc.gc, x, y, x + w, y);
}

void
copypixmap(Pixmap to, Pixmap from, XRectangle rect)
{
	XCopyArea(dpy, from, to, dc.gc, 0, 0, rect.width, rect.height, rect.x, rect.y);
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
	classh.res_name = NAME,
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

static int
isabsolute(const char *s)
{
	return s[0] == '/' || (s[0] == '.' && (s[1] == '/' || (s[1] == '.' && s[2] == '/')));
}

void
geticon(Window win, char *file, Pixmap *icon, Pixmap *mask)
{
	XpmAttributes attr;
	int status, i;
	char path[PATH_MAX];

	*icon = None;
	*mask = None;
	memset(&attr, 0, sizeof(attr));
	status = XpmFileInvalid;
	if (config.niconpaths == 0 || isabsolute(file)) {
		status = XpmReadFileToPixmap(dpy, win, file, icon, mask, &attr);
	} else {
		for (i = 0; status != XpmSuccess && i < config.niconpaths; i++) {
			snprintf(path, sizeof(path), "%s/%s", config.iconpaths[i], file);
			status = XpmReadFileToPixmap(dpy, win, path, icon, mask, &attr);
		}
	}
	if (status != XpmSuccess)
		goto error;
	return;
error:
	if (*icon != None)
		XFreePixmap(dpy, *icon);
	if (*mask != None)
		XFreePixmap(dpy, *mask);
	warnx("could not open pixmap: %s", file);
}

void
drawicon(Pixmap pix, Pixmap icon, Pixmap mask, int x, int y)
{
	XGCValues val;

	val.clip_x_origin = x;
	val.clip_y_origin = y;
	val.clip_mask = mask;
	XChangeGC(dpy, dc.gc, GCClipXOrigin | GCClipYOrigin | GCClipMask, &val);
	XCopyArea(dpy, icon, pix, dc.gc, 0, 0, config.iconsize, config.iconsize, x, y);
	val.clip_mask = None;
	XChangeGC(dpy, dc.gc, GCClipMask, &val);
}

char *
getresource(const char *res, const char *name, const char *class)
{
	XrmValue xval;
	char *type;
	char namebuf[128], classbuf[128];

	if (xdb == NULL)
		return NULL;
	if (name != NULL)
		snprintf(namebuf, sizeof(namebuf), "%s.%s.%s", NAME, name, res);
	else
		snprintf(namebuf, sizeof(namebuf), "%s.%s", NAME, res);
	if (class != NULL)
		snprintf(classbuf, sizeof(classbuf), "%s.%s.%s", CLASS, class, res);
	else
		snprintf(classbuf, sizeof(classbuf), "%s.%s", CLASS, res);
	if (XrmGetResource(xdb, namebuf, classbuf, &type, &xval) == True)
		return xval.addr;
	return NULL;
}

int
isresourcetrue(const char *val)
{
	return val != NULL &&
		(strcasecmp(val, "true") == 0 ||
		 strcasecmp(val, "on") == 0 ||
		 strcasecmp(val, "1") == 0);
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
	if (key == 0)
		return;
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
grabkeysync(KeyCode key)
{
	if (key == 0)
		return;
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

void
grabbuttonsync(unsigned int button)
{
	if (button == 0)
		return;
	XGrabButton(
		dpy,
		button,
		AnyModifier,
		root,
		False,
		ButtonPressMask,
		GrabModeSync,
		GrabModeAsync,
		None,
		None
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
translatecoordinates(Window win, short *xret, short *yret)
{
	Window dw;
	int x, y;

	if (win == root)
		return;
	XTranslateCoordinates(dpy, win, root, 0, 0, &x, &y, &dw);
	*xret = x;
	*yret = y;
}

void
querypointer(int *x, int *y)
{
	Window dw;          /* dummy variable */
	unsigned du;        /* dummy variable */
	int di, dj;         /* dummy variable */

	*x = *y = 0;
	XQueryPointer(dpy, root, &dw, &dw, x, y, &di, &dj, &du);
}

void
xinit(int argc, char *argv[])
{
	int ncounter, i, tmp;
	char *xrm;
	XSyncSystemCounter *counters;

	if ((opener = getenv("OPENER")) == NULL || *opener == '\0')
		opener = DEFOPENER;
	if ((calculator = getenv("CALC")) == NULL || *calculator == '\0')
		calculator = DEFCALC;
	XInitThreads();
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		warnx("warning: no locale support");
	if ((dpy = XOpenDisplay(NULL)) == NULL)
		errx(1, "could not open display");
	if (!XSyncQueryExtension(dpy, &sync_event, &tmp))
		errx(1, "XSync extension not available");
	if (!XSyncInitialize(dpy, &tmp, &tmp))
		errx(1, "failed to initialize XSync extension");
	if ((xim = XOpenIM(dpy, NULL, NULL, NULL)) == NULL)
		errx(1, "XOpenIM: could not open input device");
	if ((xrm = XResourceManagerString(dpy)) != NULL)
		xdb = XrmGetStringDatabase(xrm);
	root = DefaultRootWindow(dpy);
	screen = DefaultScreen(dpy);
	depth = DefaultDepth(dpy, screen);
	visual = DefaultVisual(dpy, screen);
	colormap = DefaultColormap(dpy, screen);
	xerrorxlib = XSetErrorHandler(xerror);
	XSelectInput(dpy, root, KeyPressMask | KeyReleaseMask);
	initatoms();
	savedargc = argc;
	savedargv = argv;
	servertime = None;
	if ((counters = XSyncListSystemCounters(dpy, &ncounter)) != NULL) {
		for (i = 0; i < ncounter; i++) {
			if (strcmp(counters[i].name, "SERVERTIME") == 0) {
				servertime = counters[i].counter;
				break;
			}
		}
		XSyncFreeSystemCounterList(counters);
	}
	if (servertime == None) {
		errx(1, "SERVERTIME counter not found");
	}
}

void
initdc(void)
{
	int i;

	/* get color pixels */
	for (i = 0; i < COLOR_LAST; i++) {
		ealloccolor(config.colors[i].background,       &dc.colors[i].background);
		ealloccolor(config.colors[i].foreground,       &dc.colors[i].foreground);
		ealloccolor(config.colors[i].selbackground,    &dc.colors[i].selbackground);
		ealloccolor(config.colors[i].selforeground,    &dc.colors[i].selforeground);
		ealloccolor(config.colors[i].altforeground,    &dc.colors[i].altforeground);
		ealloccolor(config.colors[i].altselforeground, &dc.colors[i].altselforeground);
	}
	ealloccolor(config.topShadow,     &dc.topShadow);
	ealloccolor(config.bottomShadow,  &dc.bottomShadow);

	/* parse fonts */
	dc.face = XftFontOpenXlfd(dpy, screen, config.faceName);
	if (dc.face == NULL) {
		dc.face = XftFontOpenName(dpy, screen, config.faceName);
		if (dc.face == NULL) {
			errx(1, "could not open font: %s", config.faceName);
		}
	}
	config.fontascent = dc.face->ascent;

	dc.gc = XCreateGC(dpy, root, 0, NULL);
}

void
xclose(void)
{
	if (xdb != NULL)
		XrmDestroyDatabase(xdb);
	cleandc();
	XCloseDisplay(dpy);
}
