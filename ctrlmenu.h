#include <sys/queue.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>
#include <X11/XKBlib.h>
#include <X11/Xft/Xft.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/sync.h>

#define CLASS "CtrlMenu"
#define NAME  "ctrlmenu"

#define INPUTSIZ     1024
#define PADDING                 7
#define SEPARATOR_THICKNESS     1
#define SEPARATOR_HEIGHT        (2 * PADDING + 2 * SEPARATOR_THICKNESS)
#define LEN(x)                  (sizeof(x) / sizeof(*(x)))
#define MAXPATHS                128          /* maximum number of paths to look for icons */
#define ICONPATH                "ICONPATH"   /* environment variable name */

struct Control;
struct Prompt;

enum {
	ITEM_ISGEN       = 0x1,
	ITEM_ICON        = 0x2,
	ITEM_FOUNDCALLER = 0x4,
	ITEM_OPENER      = 0x8,
};

enum {
	MODE_DOCKAPP    = 0x1,
	MODE_CONTEXT    = 0x2,
	MODE_RUNNER     = 0x4,
};

enum {
	GRAB_POINTER    = 0x1,
	GRAB_KEYBOARD   = 0x2,
};

enum {
	CTRLPASTE,      /* Paste from clipboard */
	CTRLCOPY,       /* Copy into clipboard */
	CTRLENTER,      /* Choose item */
	CTRLPREV,       /* Select previous item */
	CTRLNEXT,       /* Select next item */
	CTRLPGUP,       /* Select item one screen above */
	CTRLPGDOWN,     /* Select item one screen below */
	CTRLUP,         /* Select previous item in the history */
	CTRLDOWN,       /* Select next item in the history */
	CTRLBOL,        /* Move cursor to beginning of line */
	CTRLEOL,        /* Move cursor to end of line */
	CTRLLEFT,       /* Move cursor one character to the left */
	CTRLRIGHT,      /* Move cursor one character to the right */
	CTRLWLEFT,      /* Move cursor one word to the left */
	CTRLWRIGHT,     /* Move cursor one word to the right */
	CTRLDELBOL,     /* Delete from cursor to beginning of line */
	CTRLDELEOL,     /* Delete from cursor to end of line */
	CTRLDELLEFT,    /* Delete character to left of cursor */
	CTRLDELRIGHT,   /* Delete character to right of cursor */
	CTRLDELWORD,    /* Delete from cursor to beginning of word */
	CTRLSELBOL,     /* Select from cursor to beginning of line */
	CTRLSELEOL,     /* Select from cursor to end of line */
	CTRLSELLEFT,    /* Select from cursor to one character to the left */
	CTRLSELRIGHT,   /* Select from cursor to one character to the right */
	CTRLSELWLEFT,   /* Select from cursor to one word to the left */
	CTRLSELWRIGHT,  /* Select from cursor to one word to the right */
	CTRLUNDO,       /* Undo */
	CTRLREDO,       /* Redo */
	CTRLCANCEL,     /* Cancel */
	CTRLNOTHING,    /* Control does nothing */
	INSERT          /* Insert character as is */
};

enum {
	/* utf8 */
	UTF8_STRING,

	/* selection */
	CLIPBOARD,

	/* ICCCM atoms */
	WM_DELETE_WINDOW,

	/* EWMH atoms */
	_NET_WM_NAME,
	_NET_WM_WINDOW_TYPE,
	_NET_WM_WINDOW_TYPE_MENU,               /* for torn-off menus */
	_NET_WM_WINDOW_TYPE_POPUP_MENU,         /* for popup menus */
	_NET_WM_WINDOW_TYPE_PROMPT,             /* for the runner */

	ATOM_LAST
};

enum {
	ALIGN_LEFT,
	ALIGN_CENTER,
	ALIGN_RIGHT,
};

enum {
	DIR_UP,
	DIR_DOWN,
	DIR_LEFT,
	DIR_RIGHT
};

enum {
	RUNNER,
	MENU_DOCKAPP,
	MENU_TORNOFF,
	MENU_POPUP,
};

enum {
	COLOR_SEL = 0x1,        /* color for selected text */
	COLOR_ALT = 0x2,        /* color for alternate text */
};

enum {
	COLOR_MENU,
	COLOR_RUNNER,
	COLOR_LAST
};

TAILQ_HEAD(ItemQueue, Item);
struct Item {
	TAILQ_ENTRY(Item) entries;
	TAILQ_ENTRY(Item) matches;
	TAILQ_ENTRY(Item) defers;
	struct ItemQueue children;
	struct ItemQueue *genchildren;
	struct Item *caller;            /* caller item that generated */
	char *name;                     /* item name */
	char *desc;                     /* item description */
	char *cmd;                      /* command entered */
	char *acc;                      /* accelerator */
	char *file;                     /* path to icon file */
	char *genscript;                /* commands piped to sh to generate entries */
	unsigned int altpos, altlen;    /* alternative key sequence */
	Pixmap icon;
	Pixmap mask;
	KeySym altkey;
	size_t len;
	int flags;
};

TAILQ_HEAD(AcceleratorQueue, Accelerator);
struct Accelerator {
	TAILQ_ENTRY(Accelerator) entries;
	struct Item *item;
	unsigned int mods;
	KeyCode key;                    /* accelerator key */
};

TAILQ_HEAD(MenuQueue, Menu);
struct Menu {
	TAILQ_ENTRY(Menu) entries;
	struct ItemQueue *queue;        /* list of items contained by the menu */
	struct Item *caller;            /* item that generated the menu */
	struct Item *first;             /* first item displayed on the menu */
	struct Item *selected;          /* item currently selected in the menu */
	XRectangle rect;                /* menu geometry */
	Window win;                     /* menu window to map on the screen */
	Pixmap pix;                     /* pixmap to draw on */
	int overflow;                   /* whether the menu is higher than the monitor */
	int maxwidth;                   /* maximum width of a text on the menu */
	int isgen;                      /* whether menu was generated from a genscript */
	int hasicon;                    /* whether menu has an entry with icon */
};

struct Config {
	int (*fstrncmp)(const char *, const char *, size_t);
	char *(*fstrstr)(const char *, const char *);
	const char *faceName;
	struct {
		const char *background;
		const char *foreground;
		const char *selbackground;
		const char *selforeground;
		const char *altforeground;
		const char *altselforeground;
	} colors[COLOR_LAST];
	const char *topShadow;
	const char *bottomShadow;
	char *iconpath;
	char *runner;
	char *altkey;
	char *button;
	int runnergroup;
	int runnertsv;
	int max_items;
	int runner_items;
	int tornoff;
	int itemheight;
	int shadowThickness;
	int alignment;
	int iconsize;
	int mode;
	int gap;

	char *iconpaths[MAXPATHS];
	int niconpaths;
};

struct DC {
	FcPattern *pattern;
	XftFont *face;
	struct {
		XftColor background;
		XftColor foreground;
		XftColor selbackground;
		XftColor selforeground;
		XftColor altforeground;
		XftColor altselforeground;
	} colors[COLOR_LAST];
	XftColor topShadow;
	XftColor bottomShadow;
	GC gc;
	int fontascent;
	int triangle_width;
	int triangle_height;
};

extern struct DC dc;
extern struct Config config;
extern Display *dpy;
extern XIM xim;
extern Atom atoms[];
extern Window root;
extern int sync_event;
extern XSyncCounter servertime;
extern char *opener, *calculator;

/* parse.c */
void genmenu(struct ItemQueue *itemq, struct Item *caller);
void runcalc(struct ItemQueue *itemq, char *text);
void readfile(FILE *fp, char *filename, struct ItemQueue *itemq, struct AcceleratorQueue *accq);
void cleanitems(struct ItemQueue *itemq);
void cleanaccelerators(struct AcceleratorQueue *accq);

/* util.c */
int max(int x, int y);
int min(int x, int y);
void *emalloc(size_t size);
void *ecalloc(size_t nmemb, size_t size);
void *erealloc(void *ptr, size_t size);
char *estrdup(const char *s);
char *estrndup(const char *s, size_t maxlen);
void epipe(int fd[]);
void eexecshell(const char *cmd, const char *arg);
void eexeccmd(const char *cmd, const char *arg);
void edup2(int fd1, int fd2);
pid_t efork(void);
void esetsid(void);
void xinit(int argc, char *argv[]);
void initdc(void);
void xclose(void);
void getmonitors(void);
void drawrectangle(Pixmap pix, XRectangle rect, unsigned int color);
void drawtext(Drawable pix, XftColor *color, int x, int y, const char *text, int len);
void drawtriangle(Drawable pix, unsigned int color, int x, int y, int direction);
void drawshadows(Pixmap pix, XRectangle rect, unsigned long top, unsigned long bot);
void drawseparator(Drawable pix, int x, int y, int w, int dash);
void commitdrawing(Window win, Pixmap pix, XRectangle rect);
void copypixmap(Pixmap to, Pixmap from, XRectangle rect);
void grabkey(KeyCode key, unsigned int mods);
void ungrab(void);
void freepixmap(Pixmap pix);
void mapwin(Window win);
void unmapwin(Window win);
void grabkeysync(KeyCode key);
void grabbuttonsync(unsigned int button);
int grab(int grabwhat);
int textwidth(const char *text, int len);
void translatecoordinates(Window win, short *x, short *y);
void querypointer(int *x, int *y);
XRectangle getselmon(XRectangle *rect);
Window createwindow(XRectangle *rect, int type, const char *title);
Pixmap createpixmap(XRectangle rect, Window win);
KeyCode getkeycode(const char *str);
int isresourcetrue(const char *val);
char *getresource(const char *res, const char *name, const char *class);
void geticon(Window win, char *file, Pixmap *icon, Pixmap *mask);
void drawicon(Pixmap pix, Pixmap icon, Pixmap mask, int x, int y);

/* prompt.c */
int getoperation(struct Prompt *prompt, XKeyEvent *ev, char *buf, size_t bufsize, KeySym *ksym, int *len);
KeySym getkeysym(const char *str);
void *setprompt(struct ItemQueue *itemq, Window *win, int *inited);
void mapprompt(struct Prompt *prompt);
void unmapprompt(struct Prompt *prompt);
void drawprompt(struct Prompt *prompt);
void redrawprompt(struct Prompt *prompt);
void promptkey(struct Prompt *prompt, char *buf, int len, int operation);

/* ctrlmenu.c */
void enteritem(struct Item *item);
