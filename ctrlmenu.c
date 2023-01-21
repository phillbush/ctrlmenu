#include <sys/wait.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "ctrlmenu.h"

#define TORNOFF_HEIGHT     (config.tornoff ? SEPARATOR_HEIGHT : 0)
#define ALARMFLAGS         (XSyncCACounter | XSyncCAValue | XSyncCAValueType | XSyncCATestType | XSyncCADelta)
#define MODS               (ShiftMask | ControlMask | Mod1Mask | Mod4Mask)
#define SCROLL_TIME        100
#define SCROLL_WAIT        200
#define ICONPATH           "ICONPATH"   /* environment variable name */
#define RUNNER             "RUNNER"

/* predicate on item (and another argument) */
typedef int (*ItemPred)(struct Item *, void *);

struct Control {
	int running;                    /* are we running? */

	/*
	 * We use the XSync extension for scrolling.
	 */
	Window scrollwin;               /* the window being scrolled */
	XSyncAlarm alarm;
	XSyncAlarmAttributes attr;

	/*
	 * The list of items specified in the config file is listed on
	 * this structure.
	 */
	struct ItemQueue *itemq;

	/*
	 * The docked menu is a dockapp menu that lives in the dock.
	 *
	 * Popup menus are override-redirect menus that pop up when
	 * selecting an entry from another menu.
	 *
	 * Tornoff menus are detached versions of the popup menus.
	 */
	struct Menu docked;             /* dockapp menu */
	struct MenuQueue popupq;        /* queue of popup menus */
	struct MenuQueue tornoffq;      /* queue of tornoff menus */

	/*
	 * Pointer to current root menu (which may be either the docked
	 * menu or a tornoff menu).
	 */
	struct Menu *curroot;

	/*
	 * The following states describe which kind of menus we have
	 * open right now.
	 */
	enum {
		STATE_NORMAL,           /* no popped up menu, alt key not released */
		STATE_ALT,              /* no popped up menu, alt key released */
		STATE_POPUP,            /* popped up menu, alt key ignored */
	} menustate;

	struct AcceleratorQueue accq;

	KeyCode altkey;
	int altpressed;

	unsigned int button;
	unsigned int buttonmod;

	KeyCode runnerkey;
	unsigned int runnermod;

	struct Prompt *prompt;
	Window promptwin;
	int promptopen;

	int passclick;
};

/* dummy items */
static struct Item tornoff = { .name = "tornoff" };
static struct Item scrollup = { .name = "scrollup" };
static struct Item scrolldown = { .name = "scrolldown" };

static void
usage(void)
{
	(void)fprintf(stderr, "usage: ctrlmenu [-dit] [-a keysym] [-r keychord] [-x buttonspec] [file]\n");
	exit(1);
}

static void
drawmenu(struct Menu *menu, struct Item *oldsel, int menutype, int alt, int drawall)
{
	struct Item *item;
	XRectangle rect;
	XftColor *colorfg, *colorbg, *coloracc;
	size_t acclen;
	int beg, textx, texty, separatorx, separatorwid, right, icony, x;
	int altx, altw;
	int issel;

	/* draw menu */
	icony = max(0, (config.itemheight - config.iconsize) / 2);
	rect.x = rect.y = beg = 0;
	rect.width = menu->rect.width;
	rect.height = menu->rect.height;
	if (drawall)
		drawrectangle(menu->pix, rect, dc.colors[COLOR_MENU].background.pixel);
	separatorwid = menu->rect.width - 2 * config.shadowThickness - 2 * PADDING;
	if (menutype == MENU_POPUP) {
		alt = 1;
		right = config.shadowThickness + 2 * PADDING + dc.triangle_width;
		beg = config.shadowThickness;
		rect.x = beg;
		rect.y = beg + TORNOFF_HEIGHT;
	} else {
		right = 2 * PADDING + dc.triangle_width;
	}
	textx = separatorx = rect.x + PADDING;
	if (menu->hasicon)
		textx += config.iconsize + PADDING;
	if (menu->overflow)
		rect.y += SEPARATOR_HEIGHT;
	for (item = menu->first; item != NULL; item = TAILQ_NEXT(item, entries)) {
		rect.height = item->name != NULL ? config.itemheight : SEPARATOR_HEIGHT;
		if (!drawall && item != oldsel && item != menu->selected)
			goto donewithitem;
		issel = item == menu->selected;
		coloracc = (issel ? &dc.colors[COLOR_MENU].altselforeground : &dc.colors[COLOR_MENU].altforeground);
		colorfg = (issel ? &dc.colors[COLOR_MENU].selforeground : &dc.colors[COLOR_MENU].foreground);
		colorbg = (issel ? &dc.colors[COLOR_MENU].selbackground : &dc.colors[COLOR_MENU].background);
		if (item->name == NULL) {
			drawseparator(menu->pix, separatorx, rect.y + PADDING, separatorwid, 0);
		} else {
			texty = rect.y + (rect.height + dc.fontascent) / 2;

			/* draw rectangle below menu item */
			drawrectangle(menu->pix, rect, colorbg->pixel);

			/* draw item icon */
			if (item->file != NULL) {
				if (!(item->flags & ITEM_ICON)) {
					geticon(menu->win, item->file, &item->icon, &item->mask);
					item->flags |= ITEM_ICON;
				}
				if (item->icon != None) {
					drawicon(menu->pix, item->icon, item->mask, PADDING, rect.y + icony);
				}
			}

			/* draw item text */
			x = textx;
			if (config.alignment == ALIGN_CENTER)
				x += (menu->maxwidth - textwidth(item->name, item->len)) / 2;
			else if (config.alignment == ALIGN_RIGHT)
				x += menu->maxwidth - textwidth(item->name, item->len);
			x = max(textx, x);
			if (alt && item->altlen > 0 && item->altpos + item->altlen <= item->len) {
				altx = x + textwidth(item->name, item->altpos);
				altw = textwidth(item->name + item->altpos, item->altlen);
				drawrectangle(
					menu->pix,
					(XRectangle){ .x = altx, .y = texty + 1, .width = altw, .height = 1 },
					colorfg->pixel
				);
			}
			drawtext(menu->pix, colorfg, x, texty, item->name, item->len);
			if (item->acc != NULL) {
				acclen = strlen(item->acc);
				drawtext(
					menu->pix,
					coloracc,
					menu->rect.width - textwidth(item->acc, acclen) - right,
					texty,
					item->acc,
					acclen
				);
			}
			if (menutype != MENU_DOCKAPP && (item->genscript != NULL || !TAILQ_EMPTY(&item->children))) {
				drawtriangle(
					menu->pix,
					colorfg->pixel,
					rect.width - rect.x - PADDING - dc.triangle_width,
					rect.y + (rect.height - dc.triangle_height) / 2,
					DIR_RIGHT
				);
			}
		}
donewithitem:
		rect.y += rect.height;
		if (menu->overflow && rect.y + rect.height > menu->rect.height - SEPARATOR_HEIGHT - beg) {
			break;
		}
	}

	rect.x = rect.y = 0;
	rect.width = menu->rect.width;
	rect.height = SEPARATOR_HEIGHT;
	if (menutype == MENU_POPUP) {
		if (config.tornoff) {
			colorfg = (menu->selected == &tornoff ? &dc.colors[COLOR_MENU].selbackground : &dc.colors[COLOR_MENU].background);
			drawrectangle(menu->pix, rect, colorfg->pixel);
			drawseparator(menu->pix, config.shadowThickness + PADDING, config.shadowThickness + PADDING, separatorwid, 1);
			rect.y += TORNOFF_HEIGHT;
		}
		rect.x = config.shadowThickness;
		rect.y = config.shadowThickness + TORNOFF_HEIGHT;
	} else {
		rect.x = rect.y = 0;
	}
	if (menu->overflow) {
		drawrectangle(
			menu->pix,
			rect,
			(menu->selected == &scrollup ? dc.colors[COLOR_MENU].selbackground.pixel : dc.colors[COLOR_MENU].background.pixel)
		);
		drawtriangle(
			menu->pix,
			(menu->selected == &scrollup ? dc.colors[COLOR_MENU].selforeground.pixel : dc.colors[COLOR_MENU].foreground.pixel),
			rect.x + (menu->rect.width - dc.triangle_height) / 2,
			rect.y + (SEPARATOR_HEIGHT - dc.triangle_width) / 2,
			DIR_UP
		);
		rect.y = menu->rect.height - SEPARATOR_HEIGHT;
		drawrectangle(
			menu->pix,
			rect,
			(menu->selected == &scrolldown ? dc.colors[COLOR_MENU].selbackground.pixel : dc.colors[COLOR_MENU].background.pixel)
		);
		drawtriangle(
			menu->pix,
			(menu->selected == &scrolldown ? dc.colors[COLOR_MENU].selforeground.pixel : dc.colors[COLOR_MENU].foreground.pixel),
			rect.x + (menu->rect.width - dc.triangle_height) / 2,
			rect.y + (SEPARATOR_HEIGHT - dc.triangle_width) / 2 - beg,
			DIR_DOWN
		);
	}

	if (menutype == MENU_POPUP) {
		drawshadows(
			menu->pix,
			(XRectangle){
				.x = 0,
				.y = 0,
				.width = menu->rect.width,
				.height = menu->rect.height,
			},
			dc.topShadow.pixel,
			dc.bottomShadow.pixel
		);
	}
	commitdrawing(menu->win, menu->pix, menu->rect);
	XFlush(dpy);
}

static void
setdockedmenu(struct Menu *root, struct ItemQueue *itemq)
{
	struct Item *item;
	int textw;

	*root = (struct Menu){
		.queue = itemq,
		.first = TAILQ_FIRST(itemq),
		.caller = NULL,
		.overflow = 0,
		.maxwidth = 0,
		.hasicon = 0,
		.isgen = 0,
		.selected = NULL,
		.pix = None,
		.rect = (XRectangle){
			.x = 0,
			.y = 0,
			.width = 0,
			.height = 0,
		},
	};
	TAILQ_FOREACH(item, root->queue, entries) {
		if (item->name != NULL) {
			textw = textwidth(item->name, strlen(item->name));
			root->rect.height += config.itemheight;
		} else {
			textw = 0;
			root->rect.height += SEPARATOR_HEIGHT;
		}
		root->maxwidth = max(root->maxwidth, textw);
	}
	root->rect.width = root->maxwidth;
	root->rect.width += PADDING * 2;
	root->win = createwindow(&root->rect, MENU_DOCKAPP, CLASS);
	root->pix = createpixmap(root->rect, root->win);
	drawmenu(root, NULL, MENU_DOCKAPP, 0, 1);
	mapwin(root->win);
}

static void
insertmenu(struct MenuQueue *menuq, Window parentwin, XRectangle parentrect, struct ItemQueue *itemq, struct Item *caller, int type, int y)
{
	XRectangle mon;
	struct Menu *menu;
	struct Item *item;
	int menuh, textw, accelw, xplusw, gap;
	int nitems;

	getmonitors();
	translatecoordinates(parentwin, &parentrect.x, &parentrect.y);
	mon = getselmon(&parentrect);
	menu = emalloc(sizeof(*menu));
	gap = (caller != NULL ? config.gap : 0);
	*menu = (struct Menu){
		.queue = itemq,
		.first = TAILQ_FIRST(itemq),
		.caller = caller,
		.overflow = 0,
		.maxwidth = 0,
		.isgen = (caller != NULL && caller->genscript != NULL),
		.hasicon = 0,
		.selected = NULL,
		.pix = None,
		.rect = (XRectangle){
			.x = parentrect.x,
			.y = parentrect.y,
			.width = config.shadowThickness * 2 + SEPARATOR_HEIGHT,
			.height = config.shadowThickness * 2 + SEPARATOR_HEIGHT,
		},
	};
	if (type == MENU_TORNOFF) {
		/* remove shadow from tornoff menus */
		menu->rect.x += config.shadowThickness;
		menu->rect.y += config.shadowThickness;
		menu->rect.width = max(0, menu->rect.width - 2 * config.shadowThickness);
		menu->rect.height = max(0, menu->rect.height - 2 * config.shadowThickness);
	}
	TAILQ_INSERT_HEAD(menuq, menu, entries);
	if (TAILQ_EMPTY(menu->queue))
		goto done;
	menuh = 0;
	if (type == MENU_POPUP)
		menuh = config.shadowThickness * 2 + TORNOFF_HEIGHT;
	accelw = 0;
	nitems = 0;
	TAILQ_FOREACH(item, menu->queue, entries) {
		if (item->name != NULL) {
			textw = textwidth(item->name, strlen(item->name));
			if (item->acc != NULL)
				accelw = max(accelw, textwidth(item->acc, strlen(item->acc)) + 2 * PADDING);
			menuh += config.itemheight;
			if (item->file != NULL)
				menu->hasicon = 1;
			nitems++;
		} else {
			textw = 0;
			menuh += SEPARATOR_HEIGHT;
		}
		menu->maxwidth = max(menu->maxwidth, textw);
		if (config.max_items > 0 && nitems > config.max_items)
			menu->overflow = 1;
		if (!menu->overflow) {
			if (menuh + config.itemheight + SEPARATOR_HEIGHT * 2 < mon.height) {
				menu->rect.height = menuh;
			} else {
				menu->overflow = 1;
				menu->rect.height = mon.height;
			}
		}
	}
	menu->rect.width = menu->maxwidth + accelw;
	menu->rect.width += dc.triangle_width + PADDING * 3;     /* PAD + name + PAD + triangle + PAD */
	if (menu->hasicon)
		menu->rect.width += config.iconsize + PADDING;
	if (type == MENU_POPUP)
		menu->rect.width += config.shadowThickness * 2;

done:
	if (type != MENU_TORNOFF) {
		/*
		 * Resize menu to fit the monitor.  We may also move it
		 * according to the parent menu.  The argument y will be
		 * the y position of the menu relative to its parent.
		 */
		menu->rect.x = mon.x;
		menu->rect.y = mon.y;
		xplusw = parentrect.x + parentrect.width + gap;
		if (menu->rect.width > mon.width / 2)
			menu->rect.width = mon.width / 2;
		if (mon.x + mon.width - xplusw >= menu->rect.width) {
			menu->rect.x = xplusw;
		} else if (parentrect.x > menu->rect.width + gap) {
			menu->rect.x = parentrect.x - menu->rect.width - gap;
		}
		y -= config.shadowThickness + TORNOFF_HEIGHT;
		y = max(0, y);
		if (mon.y + mon.height - (parentrect.y + y) >= menu->rect.height) {
			menu->rect.y = parentrect.y + y;
		} else if (mon.y + mon.height > menu->rect.height) {
			menu->rect.y = mon.y + mon.height - menu->rect.height;
		}
	}

	menu->win = createwindow(&menu->rect, type, caller != NULL ? caller->name : CLASS);
	menu->pix = createpixmap(menu->rect, menu->win);
	drawmenu(menu, NULL, type, 0, 1);
	mapwin(menu->win);
}

static void
insertpopupmenu(struct MenuQueue *menuq, Window parentwin, XRectangle parentrect, struct Item *caller, struct ItemQueue *itemq, int y)
{
	if (caller != NULL && caller->genscript != NULL) {
		itemq = emalloc(sizeof(*itemq));
		genmenu(itemq, caller);
	}
	insertmenu(menuq, parentwin, parentrect, itemq, caller, MENU_POPUP, y);
}

static int
getmenutype(struct Control *ctrl, struct Menu *menu)
{
	if (menu == &ctrl->docked)
		return MENU_DOCKAPP;
	if (TAILQ_EMPTY(&ctrl->popupq) || (menu == ctrl->curroot && menu != TAILQ_LAST(&ctrl->popupq, MenuQueue)))
		return MENU_TORNOFF;
	return MENU_POPUP;
}

static struct Menu *
getmenu(struct MenuQueue *menuq, Window win)
{
	struct Menu *menu;

	TAILQ_FOREACH(menu, menuq, entries)
		if (menu->win == win)
			return menu;
	return NULL;
}

static struct Menu *
getopenmenu(struct Control *ctrl, Window win)
{
	if (ctrl->menustate == STATE_POPUP) {
		return (ctrl->curroot != NULL && win == ctrl->curroot->win)
		? ctrl->curroot
		: getmenu(&ctrl->popupq, win);
	} else {
		return (win == ctrl->docked.win)
		? &ctrl->docked
		: getmenu(&ctrl->tornoffq, win);
	}
}

static struct Menu *
gettopmenu(struct Control *ctrl)
{
	struct Menu *menu;

	if ((menu = TAILQ_FIRST(&ctrl->popupq)) == NULL)
		menu = ctrl->curroot;
	return menu;
}

static struct Item *
getitem(struct Menu *menu, int menutype, int y, int *ytop)
{
	struct Item *item;
	int h;

	if (menu == NULL)
		return NULL;
	if (menutype == MENU_POPUP)
		h = config.shadowThickness + TORNOFF_HEIGHT;
	else
		h = 0;
	if (menutype == MENU_POPUP && config.tornoff && y < SEPARATOR_HEIGHT)
		return &tornoff;
	if (menu->overflow) {
		if (y < h + SEPARATOR_HEIGHT)
			return &scrollup;
		if (y >= menu->rect.height - SEPARATOR_HEIGHT)
			return &scrolldown;
		h += SEPARATOR_HEIGHT;
	}
	for (item = menu->first; item != NULL; item = TAILQ_NEXT(item, entries)) {
		if (item->name != NULL && y >= h && y < h + config.itemheight)
			break;
		h += (item->name != NULL) ? config.itemheight : SEPARATOR_HEIGHT;
	}
	if (ytop != NULL)
		*ytop = h;
	return item;
}

static void
delmenu(struct Menu *menu, int delgen)
{
	if (delgen && menu->isgen) {
		cleanitems(menu->queue);
		free(menu->queue);
	}
	if (menu->pix != None) {
		XFreePixmap(dpy, menu->pix);
	}
	XDestroyWindow(dpy, menu->win);
	free(menu);
}

static void
delmenus(struct MenuQueue *menuq, struct Menu *menu)
{
	struct Menu *tmp;

	while (menu != NULL) {
		tmp = menu;
		menu = TAILQ_PREV(menu, MenuQueue, entries);
		TAILQ_REMOVE(menuq, tmp, entries);
		delmenu(tmp, 1);
	}
}

static int
menugenerated(struct Control *ctrl, struct Item *item)
{
	struct Menu *menu;

	TAILQ_FOREACH(menu, &ctrl->popupq, entries)
		if (menu->caller == item)
			return 1;
	return 0;
}

static int
invalidbutton(unsigned int button)
{
	return (button != Button1 && button != Button3);
}

static struct Item *
matchacc(struct AcceleratorQueue *accq, KeyCode key, unsigned int mods)
{
	struct Accelerator *acc;

	TAILQ_FOREACH(acc, accq, entries)
		if (acc->key != NoSymbol && acc->key == key && (mods & MODS) == acc->mods)
			return acc->item;
	return NULL;
}

static int
scroll(struct Control *ctrl, Window win)
{
	struct Menu *menu;
	struct Item *item;
	int menutype;
	int h, end;

	if ((menu = getopenmenu(ctrl, win)) == NULL)
		return 0;
	end = h = SEPARATOR_HEIGHT;
	if ((menutype = getmenutype(ctrl, menu)) == MENU_POPUP) {
		h += config.shadowThickness + TORNOFF_HEIGHT;
		end += config.shadowThickness;
	}
	end = menu->rect.height - end;
	if (menu->selected == &scrolldown) {
		for (item = menu->first; item != NULL; item = TAILQ_NEXT(item, entries)) {
			h += (item->name != NULL) ? config.itemheight : SEPARATOR_HEIGHT;
			if (h >= end) {
				break;
			}
		}
		if (item != NULL && TAILQ_NEXT(menu->first, entries) != NULL) {
			h = (item->name != NULL) ? config.itemheight : SEPARATOR_HEIGHT;
			menu->first = TAILQ_NEXT(menu->first, entries);
			drawmenu(menu, NULL, menutype, 0, 1);
			XFlush(dpy);
		} else {
			return 0;
		}
	} else if (TAILQ_PREV(menu->first, ItemQueue, entries) != NULL) {
		menu->first = TAILQ_PREV(menu->first, ItemQueue, entries);
		drawmenu(menu, NULL, menutype, 0, 1);
		XFlush(dpy);
	} else {
		return 0;
	}
	return 1;
}

static void
configuremenu(struct Menu *menu, int x, int y, int w, int h)
{
	menu->rect.x = x;
	menu->rect.y = y;
	menu->rect.width = w;
	menu->rect.height = h;
	if (menu->pix != None)
		freepixmap(menu->pix);
	menu->pix = createpixmap(menu->rect, menu->win);
}

static unsigned int
getmod(char **str)
{
	unsigned int mod;
	int any;
	char *s, *p;

	mod = 0;
	any = 0;
	if (*str == NULL)
		return 0;
	s = NULL;
	for (p = strtok((*str), "-"); p; s = p, p = strtok(NULL, "-")) {
		if (s == NULL)
			continue;
		switch (*s) {
		case 'S': mod |= ShiftMask;   break;
		case 'C': mod |= ControlMask; break;
		case 'A':
		case '1': mod |= Mod1Mask;    break;
		case '2': mod |= Mod2Mask;    break;
		case '3': mod |= Mod3Mask;    break;
		case 'W':
		case '4': mod |= Mod4Mask;    break;
		case '5': mod |= Mod5Mask;    break;
		case 'N': any = 1;            break;
		default: break;
		}
	}
	*str = s;
	return any ? AnyModifier : mod;
}

static void
initgrabs(struct Control *ctrl)
{
	struct Accelerator *acc;
	size_t len;
	char *runner, *button, *s;

	ctrl->altkey = 0;
	ctrl->runnerkey = 0;
	ctrl->runnermod = 0;
	ctrl->buttonmod = 0;
	ctrl->button = 0;
	if (config.mode & MODE_DOCKAPP) {
		ctrl->altkey = getkeycode(config.altkey);
		grabkeysync(ctrl->altkey);
	}
	if (config.runner != NULL && config.runner[0] != '\0') {
		runner = estrdup(config.runner);
		s = runner;
		ctrl->runnermod = getmod(&s);
		ctrl->runnerkey = getkeycode(s);
		grabkey(ctrl->runnerkey, ctrl->runnermod);
		free(runner);
	}
	if (config.button != NULL && config.button[0] != '\0') {
		button = estrdup(config.button);
		s = button;
		ctrl->buttonmod = getmod(&s);
		ctrl->button = strtoul(s, NULL, 10);
		len = strlen(s);
		if (len > 0 && (s[len-1] == 'P' || s[len-1] == 'p'))
			ctrl->passclick = 1;
		grabbuttonsync(ctrl->button);
		free(button);
	}
	TAILQ_FOREACH(acc, &ctrl->accq, entries) {
		grabkey(acc->key, acc->mods);
	}
}

static void
removepopped(struct Control *ctrl)
{
	struct Menu *firstpopped;

	ungrab();
	firstpopped = TAILQ_LAST(&ctrl->popupq, MenuQueue);
	delmenus(&ctrl->popupq, firstpopped);
	if (ctrl->curroot != NULL && ctrl->curroot != firstpopped) {
		ctrl->curroot->selected = NULL;
		drawmenu(ctrl->curroot, NULL, getmenutype(ctrl, ctrl->curroot), 0, 1);
		ctrl->curroot = NULL;
	}
	ctrl->menustate = STATE_NORMAL;
	if (config.mode == 0)
		ctrl->running = 0;
	return;
}

static void
initpopped(struct Control *ctrl, struct Menu *rootmenu, Window parentwin, XRectangle parentrect, struct Item *caller, struct ItemQueue *itemq, int y)
{
	if (grab(GRAB_POINTER | GRAB_KEYBOARD) == -1)
		removepopped(ctrl);
	insertpopupmenu(&ctrl->popupq, parentwin, parentrect, caller, itemq, y);
	ctrl->curroot = rootmenu;
	ctrl->menustate = STATE_POPUP;
	return;
}

static void
draw(struct Control *ctrl, Window win)
{
	struct Menu *menu;

	if ((menu = getopenmenu(ctrl, win)) != NULL) {
		commitdrawing(win, menu->pix, menu->rect);
	} else if (win == ctrl->promptwin) {
		redrawprompt(ctrl->prompt);
	}
}

static int
itemcycle(struct Menu *menu, int type, int forward)
{
	struct Item *item, *prev, *oldfirst;
	int h, end, y;

	end = h = SEPARATOR_HEIGHT;
	if (type == MENU_POPUP) {
		h += config.shadowThickness + TORNOFF_HEIGHT;
		end += config.shadowThickness;
	}
	end = menu->rect.height - end;
	oldfirst = menu->first;
	if (forward) {
		prev = NULL;
		y = h;
		/* look for next selectable item; scroll as needed */
		for (item = menu->first; item != NULL; item = TAILQ_NEXT(item, entries)) {
			y += (item->name != NULL) ? config.itemheight : SEPARATOR_HEIGHT;
			if (menu->overflow && y >= end)
				menu->first = TAILQ_NEXT(menu->first, entries);
			if (item->name != NULL) {
				if (prev == menu->selected || (prev != NULL && menu->selected == NULL)) {
					menu->selected = item;
					goto done;
				}
				prev = item;
			}
		}
		y = h;
		/* select first selectable item; scroll as needed */
		if (menu->overflow)
			menu->first = TAILQ_FIRST(menu->queue);
		TAILQ_FOREACH(item, menu->queue, entries) {
			if (item->name != NULL) {
				menu->selected = item;
				goto done;
			}
		}
	} else {
		prev = NULL;
		y = h;
		/* if first item selected, select previous selectable item and scroll */
		if (menu->overflow && menu->first != TAILQ_FIRST(menu->queue) && menu->selected == menu->first) {
			for (item = TAILQ_PREV(menu->first, ItemQueue, entries);
			     item != NULL;
			     item = TAILQ_PREV(item, ItemQueue, entries)) {
				if (item->name != NULL) {
					menu->selected = menu->first = item;
					goto done;
				}
			}
		}
		/* select previous selectable item; scroll as needed */
		for (item = menu->first; item != NULL; item = TAILQ_NEXT(item, entries)) {
			y += (item->name != NULL) ? config.itemheight : SEPARATOR_HEIGHT;
			if (menu->overflow && y >= end)
				menu->first = TAILQ_NEXT(menu->first, entries);
			if (item->name != NULL) {
				if (item == menu->selected) {
					menu->selected = prev;
					if (prev != NULL) {
						goto done;
					}
				}
				prev = item;
			}
		}
		y = h;
		prev = NULL;
		/* select last selectable item; scroll as needed */
		TAILQ_FOREACH_REVERSE(item, menu->queue, ItemQueue, entries) {
			y += (item->name != NULL) ? config.itemheight : SEPARATOR_HEIGHT;
			if (item->name != NULL && menu->selected == NULL) {
				menu->selected = item;
				if (!menu->overflow) {
					goto done;
				}
			}
			if (menu->overflow && y >= end && prev != NULL) {
				menu->first = prev;
				goto done;
			}
			prev = item;
		}
	}
done:
	return oldfirst != menu->first;
}

static int
checkaltkey(struct Item *item, void *p)
{
	KeyCode key;

	key = *(KeyCode *)p;
	return (key != 0 && key == item->altkey);
}

static int
checkselected(struct Item *item, void *p)
{
	struct Menu *menu;

	menu = (struct Menu *)p;
	return (item == menu->selected);
}

static struct Item *
getitempred(struct Menu *menu, int type, ItemPred pred, void *p, int *ytop)
{
	struct Item *item;

	/* get item (and its y position) for which the given predicate is valid */
	if (menu == NULL)
		return NULL;
	if (type == MENU_POPUP)
		*ytop = config.shadowThickness + TORNOFF_HEIGHT;
	else
		*ytop = 0;
	if (menu->overflow)
		*ytop += SEPARATOR_HEIGHT;
	TAILQ_FOREACH(item, menu->queue, entries) {
		if ((*pred)(item, p))
			return item;
		*ytop += (item->name != NULL) ? config.itemheight : SEPARATOR_HEIGHT;
	}
	return NULL;
}

static void
openitem(struct Control *ctrl, struct Menu *menu, struct Item *item, int delete, int ismotion, int y)
{
	struct Menu *firstpop;
	int gen, type;

	type = getmenutype(ctrl, menu);
	gen = menugenerated(ctrl, item);  /* whether a menu was generated for this item */
	if (delete) {
		firstpop = TAILQ_FIRST(&ctrl->popupq);
		if (ctrl->menustate == STATE_POPUP && !gen &&
		    menu != firstpop && firstpop != NULL &&
		    (TAILQ_FIRST(&item->children) != TAILQ_FIRST(firstpop->queue))) {
			if (menu == ctrl->curroot) {
				delmenus(&ctrl->popupq, TAILQ_LAST(&ctrl->popupq, MenuQueue));
			} else if (type == MENU_POPUP) {
				delmenus(&ctrl->popupq, TAILQ_PREV(menu, MenuQueue, entries));
			}
		}
	}
	if (!gen && (item->genscript != NULL || !TAILQ_EMPTY(&item->children))) {
		if (ctrl->menustate != STATE_POPUP) {
			if (ismotion)
				return;
			initpopped(ctrl, menu, menu->win, menu->rect, item, &item->children, y);
		} else {
			insertpopupmenu(&ctrl->popupq, menu->win, menu->rect, item, &item->children, y);
		}
	} else if (!ismotion && item->genscript == NULL && TAILQ_EMPTY(&item->children)) {
		enteritem(item);
		if (ctrl->menustate == STATE_POPUP) {
			removepopped(ctrl);
		}
	}
}

static void
enteralt(struct Control *ctrl)
{
	if (!(config.mode & MODE_DOCKAPP))
		return;
	if (ctrl->menustate != STATE_NORMAL)
		return;
	ctrl->menustate = STATE_ALT;
	ctrl->docked.selected = NULL;
	(void)itemcycle(&ctrl->docked, MENU_DOCKAPP, 1);
	drawmenu(&ctrl->docked, NULL, MENU_DOCKAPP, 1, 1);
}

static void
exitalt(struct Control *ctrl)
{
	ungrab();
	if (!(config.mode & MODE_DOCKAPP))
		return;
	ctrl->menustate = STATE_NORMAL;
	ctrl->docked.selected = NULL;
	drawmenu(&ctrl->docked, NULL, MENU_DOCKAPP, 0, 1);
}

static void
xevexpose(XEvent *e, struct Control *ctrl)
{
	XExposeEvent *xev;

	xev = &e->xexpose;
	if (xev->count == 0) {
		draw(ctrl, xev->window);
	}
}

static void
xevleave(XEvent *e, struct Control *ctrl)
{
	XLeaveWindowEvent *xev;
	struct Menu *menu;
	struct Item *oldsel;
	int type, alt;

	xev = &e->xcrossing;
	if ((menu = getopenmenu(ctrl, xev->window)) == NULL)
		return;
	type = getmenutype(ctrl, menu);
	alt = type == MENU_DOCKAPP && ctrl->menustate == STATE_ALT;
	if ((oldsel = menu->selected) != NULL) {
		menu->selected = NULL;
		drawmenu(menu, oldsel, type, alt, 0);
	}
}

static void
xevbpress(XEvent *e, struct Control *ctrl)
{
	XButtonEvent *xev;
	XRectangle rect;

	xev = &e->xbutton;

	if (xev->window != root || invalidbutton(xev->button))
		return;
	if (ctrl->menustate != STATE_POPUP &&
	    (config.mode & MODE_CONTEXT) &&
	    xev->button == ctrl->button &&
	    (ctrl->buttonmod == AnyModifier ||
	     (ctrl->buttonmod != 0 && ctrl->buttonmod == (xev->state & MODS)) ||
	     (xev->subwindow == None))) {
		/* user clicked on root window; popup root menu */
		rect = (XRectangle){
			.x = xev->x_root,
			.y = xev->y_root,
			.width = 0,
			.height = 0,
		};
		initpopped(ctrl, NULL, root, rect, NULL, ctrl->itemq, 0);
		// while (!XCheckTypedEvent(dpy, ButtonRelease, e)) {
		// 	; /* remove the next ButtonRelease event */
		// }
	} else if (ctrl->menustate == STATE_POPUP) {
		/* user clicked out of a popped up menu; remove popped up menus */
		removepopped(ctrl);
	}
}

static void
xevmotion(XEvent *e, struct Control *ctrl)
{
	struct Menu *menu;
	struct Item *item, *oldsel;
	XMotionEvent *xev;
	int type, alt, y;

	xev = &e->xmotion;

	menu = getopenmenu(ctrl, xev->window);
	if (menu == NULL)
		return;
	type = getmenutype(ctrl, menu);
	alt = type == MENU_DOCKAPP && ctrl->menustate == STATE_ALT;
	item = getitem(menu, type, xev->y, &y);
	if (item != menu->selected) {
		oldsel = menu->selected;
		menu->selected = item;
		drawmenu(menu, oldsel, type, alt, 0);
	}
	if (item == &scrollup || item == &scrolldown) {
		/* motion over scroll buttons */
		ctrl->scrollwin = xev->window;
		if (ctrl->alarm == None)
			ctrl->alarm = XSyncCreateAlarm(dpy, ALARMFLAGS, &ctrl->attr);
		return;
	}
	if (ctrl->alarm != None) {
		XSyncDestroyAlarm(dpy, ctrl->alarm);
		ctrl->alarm = None;
	}
	if (item == NULL)
		return;
	openitem(ctrl, menu, item, True, True, y);
}

static void
xevbrelease(XEvent *e, struct Control *ctrl)
{
	struct Menu *menu;
	struct Item *item, *oldsel;
	XButtonEvent *xev;
	int type, alt, y;

	xev = &e->xbutton;
	if (invalidbutton(xev->button))
		return;
	menu = getopenmenu(ctrl, xev->window);
	if (menu == NULL)
		return;
	type = getmenutype(ctrl, menu);
	alt = type == MENU_DOCKAPP && ctrl->menustate == STATE_ALT;
	item = getitem(menu, type, xev->y, &y);
	if (item != menu->selected) {
		oldsel = menu->selected;
		menu->selected = item;
		drawmenu(menu, oldsel, type, alt, 0);
	}
	if (item == NULL)
		return;
	if (item == &scrollup || item == &scrolldown)
		return;
	if (item == &tornoff && ctrl->menustate == STATE_POPUP) {
		TAILQ_REMOVE(&ctrl->popupq, menu, entries);
		insertmenu(&ctrl->tornoffq, menu->win, menu->rect, menu->queue, menu->caller, MENU_TORNOFF, 0);
		delmenu(menu, 0);
		removepopped(ctrl);
		return;
	}
	openitem(ctrl, menu, item, True, False, y);
}

static void
xevkrelease(XEvent *e, struct Control *ctrl)
{
	XKeyEvent *xev;

	xev = &e->xkey;
	if (ctrl->altpressed && xev->keycode == ctrl->altkey && ctrl->menustate != STATE_POPUP) {
		XAllowEvents(dpy, ReplayKeyboard, xev->time);
		XFlush(dpy);
		enteralt(ctrl);
	}
}

static void
xevkpress(XEvent *e, struct Control *ctrl)
{
	struct Menu *menu;
	struct Item *item, *oldsel;
	XKeyEvent *xev;
	KeySym ksym;
	int scrolled, type, len, operation, alt, y;
	char buf[INPUTSIZ];

	xev = &e->xkey;
	ksym = XkbKeycodeToKeysym(dpy, xev->keycode, 0, 0);
	ctrl->altpressed = 0;
	if (ksym == XK_Tab && (xev->state & ShiftMask))        /* Shift-Tab = ISO_Left_Tab */
		ksym = XK_ISO_Left_Tab;
	if (ksym == XK_Escape && ctrl->menustate == STATE_ALT) {
		exitalt(ctrl);
		return;
	} else if (ksym == XK_Escape && ctrl->menustate == STATE_POPUP) {
		/* esc closes popped up menu when current menu is the root menu */
		removepopped(ctrl);
		return;
	} else if (ctrl->promptopen && xev->window == ctrl->promptwin) {
		/* pass key to prompt */
		operation = getoperation(ctrl->prompt, xev, buf, INPUTSIZ, &ksym, &len);
		promptkey(ctrl->prompt, buf, len, operation);
		return;
	} else if (ctrl->menustate != STATE_POPUP && (config.mode & MODE_RUNNER) &&
		   xev->keycode == ctrl->runnerkey &&
		   (xev->state & MODS) == ctrl->runnermod) {
		/* open prompt */
		mapprompt(ctrl->prompt);
		redrawprompt(ctrl->prompt);
		exitalt(ctrl);
		XFlush(dpy);
		return;
	} else if ((item = matchacc(&ctrl->accq, xev->keycode, xev->state)) != NULL) {
		/* enter item via accelerator keychord */
		enteritem(item);
		exitalt(ctrl);
		if (ctrl->menustate == STATE_POPUP) {
			removepopped(ctrl);
		}
		return;
	} else if (xev->keycode == ctrl->altkey) {
		ctrl->altpressed = 1;
		XAllowEvents(dpy, ReplayKeyboard, xev->time);
		XFlush(dpy);
		grab(GRAB_KEYBOARD);
		return;
	}
	if ((menu = gettopmenu(ctrl)) == NULL)
		menu = &ctrl->docked;
	if (menu != NULL) {
		type = getmenutype(ctrl, menu);
		alt = type == MENU_DOCKAPP && ctrl->menustate == STATE_ALT;
	}
	if (menu != NULL && ctrl->menustate != STATE_NORMAL && (ksym == XK_Tab || ksym == XK_Down)) {
		oldsel = menu->selected;
		scrolled = itemcycle(menu, type, 1);
		drawmenu(menu, oldsel, type, alt, scrolled);
	} else if (menu != NULL && ctrl->menustate != STATE_NORMAL && (ksym == XK_ISO_Left_Tab || ksym == XK_Up)) {
		oldsel = menu->selected;
		scrolled = itemcycle(menu, type, 0);
		drawmenu(menu, oldsel, type, alt, scrolled);
	} else if (menu != NULL && ctrl->menustate != STATE_NORMAL && (ksym == XK_Return || ksym == XK_Right) && menu->selected != NULL) {
		item = menu->selected;
		(void)getitempred(menu, type, checkselected, menu, &y);
		openitem(ctrl, menu, item, False, False, y);
		return;
	} else if (menu != NULL && ctrl->menustate != STATE_NORMAL && ksym == XK_Left && menu != TAILQ_LAST(&ctrl->popupq, MenuQueue)) {
		delmenus(&ctrl->popupq, menu);
		return;
	} else if (xev->window == root && ctrl->menustate != STATE_NORMAL) {
		/* enter item via alt key */
		if ((menu = TAILQ_FIRST(&ctrl->popupq)) == NULL)
			menu = &ctrl->docked;
		type = getmenutype(ctrl, menu);
		if ((item = getitempred(menu, type, checkaltkey, &xev->keycode, &y)) == NULL)
			return;
		oldsel = menu->selected;
		menu->selected = item;
		if (ctrl->menustate == STATE_ALT) {
			ctrl->menustate = STATE_NORMAL;
			ungrab();
		}
		drawmenu(menu, oldsel, type, 1, 0);
		openitem(ctrl, menu, item, False, False, y);
	} else if ((config.mode & MODE_RUNNER) && xev->window == root) {
		operation = getoperation(ctrl->prompt, xev, buf, INPUTSIZ, &ksym, &len);
		if (operation != INSERT || ctrl->altpressed)
			return;
		mapprompt(ctrl->prompt);
		redrawprompt(ctrl->prompt);
		promptkey(ctrl->prompt, buf, len, operation);
		XFlush(dpy);
	} else if (ctrl->menustate == STATE_NORMAL) {
		exitalt(ctrl);
	}
}

static void
xevconfigure(XEvent *e, struct Control *ctrl)
{
	struct Menu *menu;
	XConfigureEvent *xev;
	int type, alt;

	xev = &e->xconfigure;
	if ((menu = getopenmenu(ctrl, xev->window)) == NULL)
		return;
	type = getmenutype(ctrl, menu);
	alt = type == MENU_DOCKAPP && ctrl->menustate == STATE_ALT;
	configuremenu(menu, xev->x, xev->y, xev->width, xev->height);
	drawmenu(menu, NULL, type, alt, 1);
}

static void
xevclient(XEvent *e, struct Control *ctrl)
{
	struct Menu *menu;
	XClientMessageEvent *xev;

	xev = &e->xclient;
	/* user may have closed tornoff window */
	if ((Atom)xev->data.l[0] != atoms[WM_DELETE_WINDOW])
		return;
	if (xev->window == ctrl->docked.win) {
		ctrl->running = 0;
		return;
	}
	if ((menu = getmenu(&ctrl->tornoffq, xev->window)) == NULL)
		return;
	if (ctrl->menustate == STATE_POPUP)
		removepopped(ctrl);
	TAILQ_REMOVE(&ctrl->tornoffq, menu, entries);
	delmenu(menu, 1);
}

static void
xevalarm(XEvent *e, struct Control *ctrl)
{
	if (e->type != sync_event + XSyncAlarmNotify || ctrl->alarm == None)
		return;
	if (!scroll(ctrl, ctrl->scrollwin))
		ctrl->scrollwin = None;
	XSyncChangeAlarm(dpy, ctrl->alarm, ALARMFLAGS, &ctrl->attr);
}

static void
xevmapping(XEvent *e, struct Control *ctrl)
{
	(void)e;
	XUngrabButton(dpy, AnyButton, AnyModifier, root);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	initgrabs(ctrl);
}

static void (*xevents[LASTEvent])(XEvent *, struct Control *) = {
	[ButtonPress]           = xevbpress,
	[ButtonRelease]         = xevbrelease,
	[ConfigureNotify]       = xevconfigure,
	[ClientMessage]         = xevclient,
	[Expose]                = xevexpose,
	[KeyPress]              = xevkpress,
	[KeyRelease]            = xevkrelease,
	[LeaveNotify]           = xevleave,
	[MappingNotify]         = xevmapping,
	[MotionNotify]          = xevmotion,
};

static void
run(struct Control *ctrl)
{
	XRectangle rect;
	XEvent ev;
	int x, y;

	ctrl->attr = (XSyncAlarmAttributes){
		.trigger.counter        = servertime,
		.trigger.value_type     = XSyncRelative,
		.trigger.test_type      = XSyncPositiveComparison,
	};
	XSyncIntToValue(&ctrl->attr.trigger.wait_value, 128);
	XSyncIntToValue(&ctrl->attr.delta, 0);
	TAILQ_INIT(&ctrl->tornoffq);
	TAILQ_INIT(&ctrl->popupq);
	ctrl->curroot = NULL;
	ctrl->passclick = 0;
	if (config.mode & MODE_RUNNER)
		ctrl->prompt = setprompt(ctrl->itemq, &ctrl->promptwin, &ctrl->promptopen);
	else
		ctrl->prompt = NULL;
	if (config.mode & MODE_DOCKAPP)
		setdockedmenu(&ctrl->docked, ctrl->itemq);
	initgrabs(ctrl);
	ctrl->scrollwin = None;
	ctrl->promptopen = 0;
	ctrl->alarm = None;
	if (config.mode == 0) {
		querypointer(&x, &y);
		rect = (XRectangle){ .x = x, .y = y, .width = 0, .height = 0 },
		initpopped(ctrl, NULL, root, rect, NULL, ctrl->itemq, 0);
	} else {
		ctrl->menustate = STATE_NORMAL;
	}
	ctrl->running = 1;
	while (ctrl->running && !XNextEvent(dpy, &ev)) {
		if (XFilterEvent(&ev, None))
			;
		else if (ev.type < LASTEvent && xevents[ev.type])
			(*xevents[ev.type])(&ev, ctrl);
		else
			xevalarm(&ev, ctrl);
		XAllowEvents(dpy, ReplayKeyboard, CurrentTime);
		XAllowEvents(dpy, ctrl->passclick ? ReplayPointer : AsyncPointer, CurrentTime);
	}
	cleanitems(ctrl->itemq);
	XAllowEvents(dpy, ReplayKeyboard, CurrentTime);
	XAllowEvents(dpy, ReplayPointer, CurrentTime);
}

static void
parseiconpaths(char *s)
{
	if (s == NULL) {
		config.iconpath = NULL;
		return;
	}
	config.iconpath = estrdup(s);
	config.niconpaths = 0;
	for (s = strtok(config.iconpath, ":"); s != NULL; s = strtok(NULL, ":")) {
		if (config.niconpaths < MAXPATHS) {
			config.iconpaths[config.niconpaths++] = s;
		}
	}
}

void
enteritem(struct Item *item)
{
	char *cmd, *arg;

	if (item == NULL)
		return;
	if (item->flags & ITEM_ISGEN) {
		cmd = item->caller->cmd;
		arg = (item->cmd != NULL) ? item->cmd : item->name;
	} else {
		cmd = (item->cmd != NULL) ? item->cmd : item->name;
		arg = NULL;
	}
	if (efork() == 0) {
		esetsid();
		if (efork() == 0) {
			if (item->flags & ITEM_OPENER)
				eexeccmd(opener, cmd);
			else
				eexecshell(cmd, arg);
			exit(0);
		}
		exit(0);
	}
	wait(NULL);
}

int
main(int argc, char *argv[])
{
	struct Control ctrl;
	struct ItemQueue itemq;
	FILE *fp;
	long n;
	int i, c;
	char *s, *name, *class;

	config.niconpaths = 0;
	parseiconpaths(getenv(ICONPATH));
	xinit(argc, argv);
	if ((s = getresource("faceName", NULL, NULL)) != NULL)
		config.faceName = s;
	for (i = 0; i < COLOR_LAST; i++) {
		name = (i == COLOR_RUNNER ? "text" : "menu");
		class = (i == COLOR_RUNNER ? "Text" : "Menu");
		if ((s = getresource("background", name, class)) != NULL)
			config.colors[i].background = s;
		if ((s = getresource("foreground", name, class)) != NULL)
			config.colors[i].foreground = s;
		if ((s = getresource("selbackground", name, class)) != NULL)
			config.colors[i].selbackground = s;
		if ((s = getresource("selforeground", name, class)) != NULL)
			config.colors[i].selforeground = s;
		if ((s = getresource("altforeground", name, class)) != NULL)
			config.colors[i].altforeground = s;
		if ((s = getresource("altselforeground", name, class)) != NULL)
			config.colors[i].altselforeground = s;
	}
	if ((s = getresource("topShadowColor", NULL, NULL)) != NULL)
		config.topShadow = s;
	if ((s = getresource("bottomShadowColor", NULL, NULL)) != NULL)
		config.bottomShadow = s;
	if ((s = getresource("itemHeight", NULL, NULL)) != NULL &&
	    (n = strtol(s, NULL, 10)) > 0 && n < 100)
		config.itemheight = n;
	if ((s = getresource("iconSize", NULL, NULL)) != NULL &&
	    (n = strtol(s, NULL, 10)) > 0 && n < 100)
		config.iconsize = n;
	if ((s = getresource("shadowThickness", NULL, NULL)) != NULL &&
	    (n = strtol(s, NULL, 10)) > 0 && n < 100)
		config.shadowThickness = n;
	if ((s = getresource("gap", NULL, NULL)) != NULL &&
	    (n = strtol(s, NULL, 10)) > 0 && n < 100)
		config.gap = n;
	if ((s = getresource("maxItems", NULL, NULL)) != NULL &&
	    (n = strtol(s, NULL, 10)) > 0 && n < 100)
		config.max_items = n;
	if ((s = getresource("alignment", NULL, NULL)) != NULL) {
		if (strcasecmp(s, "center") == 0) {
			config.alignment = ALIGN_CENTER;
		} else if (strcasecmp(s, "left") == 0) {
			config.alignment = ALIGN_LEFT;
		} else if (strcasecmp(s, "right") == 0) {
			config.alignment = ALIGN_RIGHT;
		}
	}
	config.tornoff = isresourcetrue(getresource("tornoff", NULL, NULL));
	while ((c = getopt(argc, argv, "a:ditr:x:")) != -1) {
		switch (c) {
		case 'a':
			config.altkey = optarg;
			break;
		case 'd':
			config.mode |= MODE_DOCKAPP;
			break;
		case 'i':
			config.fstrncmp = strncasecmp;
			config.fstrstr  = strcasestr;
			break;
		case 't':
			config.tornoff = 1;
			break;
		case 'r':
			config.runner = optarg;
			config.mode |= MODE_RUNNER;
			break;
		case 'x':
			config.button = optarg;
			config.mode |= MODE_CONTEXT;
			break;
		default:
			usage();
			break;
		}
	}
	if (!config.mode)
		config.tornoff = 0;
	argc -= optind;
	argv += optind;
	if (argc == 0)
		readfile(stdin, "-", &itemq, &ctrl.accq);
	else if (argc == 1) {
		if (argv[0][0] == '-' && argv[0][1] == '\0')
			fp = stdin;
		else if ((fp = fopen(*argv, "r")) == NULL)
			err(1, "%s", *argv);
		readfile(fp, *argv, &itemq, &ctrl.accq);
		fclose(fp);
	} else {
		usage();
	}
	initdc();
	ctrl.itemq = &itemq;
	run(&ctrl);
	free(config.iconpath);
	xclose();
	return 0;
}
