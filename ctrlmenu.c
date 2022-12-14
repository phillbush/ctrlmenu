#include <sys/wait.h>

#include <err.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "ctrlmenu.h"

#define GETROOTMENU(c, w)  (((w) == (c)->docked.win) ? &(c)->docked : getmenu(&(c)->tornoffq, (w)))
#define GETOPENMENU(c, w)  (((c)->curroot != NULL && (w) == (c)->curroot->win) ? (c)->curroot : getmenu(&(c)->popupq, (w)))
#define GETFIRSTMENU(c)    (TAILQ_FIRST(&(c)->popupq) != NULL ? TAILQ_FIRST(&(c)->popupq) : (c)->curroot)
#define TORNOFF_HEIGHT     (config.tornoff ? SEPARATOR_HEIGHT : 0)
#define MODS               (ShiftMask | ControlMask | Mod1Mask | Mod4Mask)
#define SCROLL_TIME        100
#define SCROLL_WAIT        200
#define ICONPATH           "ICONPATH"   /* environment variable name */
#define RUNNER             "RUNNER"

enum {
	STATE_NORMAL,
	STATE_ALTPRESSED,
	STATE_POPUP,
};

struct Control {
	/*
	 * This structure is used mostly when getting an Expose event at
	 * an event loop.  The draw() routine at menu.c traverses the
	 * menus to get the one being exposed and redraws its window.
	 */

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

	struct AcceleratorQueue accq;

	KeyCode altkey;

	unsigned int button;
	unsigned int buttonmod;

	KeyCode runnerkey;
	unsigned int runnermod;

	struct Prompt *prompt;
	Window promptwin;

	int passclick;
};

/* dummy items */
static struct Item tornoff = { .name = "tornoff" };
static struct Item scrollup = { .name = "scrollup" };
static struct Item scrolldown = { .name = "scrolldown" };

static void
usage(void)
{
	(void)fprintf(stderr, "usage: ctrlmenu [-cdit] [-a keysym] [-r keychord] [file]\n");
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
	int issel, flag;

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
		right = config.shadowThickness + 2 * PADDING + config.triangle_width;
		beg = config.shadowThickness;
		rect.x = beg;
		rect.y = beg + TORNOFF_HEIGHT;
	} else {
		right = 2 * PADDING + config.triangle_width;
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
		flag = (issel ? ITEM_SELICON : ITEM_ICON);
		coloracc = (issel ? &dc.colors[COLOR_MENU].altselforeground : &dc.colors[COLOR_MENU].altforeground);
		colorfg = (issel ? &dc.colors[COLOR_MENU].selforeground : &dc.colors[COLOR_MENU].foreground);
		colorbg = (issel ? &dc.colors[COLOR_MENU].selbackground : &dc.colors[COLOR_MENU].background);
		if (item->name == NULL) {
			drawseparator(menu->pix, separatorx, rect.y + PADDING, separatorwid, 0);
		} else {
			texty = rect.y + (rect.height + config.fontascent) / 2;

			/* draw rectangle below menu item */
			drawrectangle(menu->pix, rect, colorbg->pixel);

			/* draw item icon */
			if (item->file != NULL) {
				if (!(item->flags & flag)) {
					item->icon[issel] = geticon(menu->win, item->file, issel, COLOR_MENU);
					item->flags |= flag;
				}
				if (item->icon[issel] != None) {
					copypixmap(
						menu->pix,
						item->icon[issel],
						(XRectangle){
							.x = PADDING ,
							.y = rect.y + icony,
							.width = config.iconsize,
							.height = config.iconsize,
						}
					);
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
					rect.width - rect.x - PADDING - config.triangle_width,
					rect.y + (rect.height - config.triangle_height) / 2,
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
			rect.x + (menu->rect.width - config.triangle_height) / 2,
			rect.y + (SEPARATOR_HEIGHT - config.triangle_width) / 2,
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
			rect.x + (menu->rect.width - config.triangle_height) / 2,
			rect.y + (SEPARATOR_HEIGHT - config.triangle_width) / 2 - beg,
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
	menu->rect.width += config.triangle_width + PADDING * 3;     /* PAD + name + PAD + triangle + PAD */
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
		readpipe(itemq, caller);
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
invalidbutton(XEvent *ev)
{
	return ((ev->type == ButtonPress || ev->type == ButtonRelease) &&
                (ev->xbutton.button != Button1 && ev->xbutton.button != Button3));
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

	if ((menu = GETOPENMENU(ctrl, win)) == NULL && (menu = GETROOTMENU(ctrl, win)) == NULL)
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

	if (config.mode & MODE_DOCKAPP) {
		ctrl->altkey = getkeycode(config.altkey);
		grabkeysync(ctrl->altkey);
	}
	if (config.runner != NULL && config.runner[0] != '\0') {
		ctrl->runnermod = getmod(&config.runner);
		ctrl->runnerkey = getkeycode(config.runner);
		grabkey(ctrl->runnerkey, ctrl->runnermod);
	}
	if (config.button != NULL && config.button[0] != '\0') {
		ctrl->buttonmod = getmod(&config.button);
		ctrl->button = strtoul(config.button, NULL, 10);
		len = strlen(config.button);
		if (len > 0 && (config.button[len-1] == 'P' || config.button[len-1] == 'p'))
			ctrl->passclick = 1;
		grabbuttonsync(ctrl->button);
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
	return;
}

static void
initpopped(struct Control *ctrl, struct Menu *rootmenu, Window parentwin, XRectangle parentrect, struct Item *caller, struct ItemQueue *itemq, int y)
{
	if (grab(GRAB_POINTER | GRAB_KEYBOARD) == -1)
		removepopped(ctrl);
	insertpopupmenu(&ctrl->popupq, parentwin, parentrect, caller, itemq, y);
	ctrl->curroot = rootmenu;
	return;
}

static void
draw(struct Control *ctrl, Window win)
{
	struct Menu *menu;

	if ((menu = GETROOTMENU(ctrl, win)) != NULL ||
	    (menu = getmenu(&ctrl->popupq, win)) != NULL) {
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

#define LOOPYTOP(menu, type, item, ytop, cond, retfound, retdef)                                 \
	do {                                                                                     \
		if ((type) == MENU_POPUP)                                                        \
			(ytop) = config.shadowThickness + TORNOFF_HEIGHT;                        \
		else                                                                             \
			(ytop) = 0;                                                              \
		if ((menu)->overflow)                                                            \
			(ytop) += SEPARATOR_HEIGHT;                                              \
		TAILQ_FOREACH((item), (menu)->queue, entries) {                                  \
			if ((cond))                                                              \
				return (retfound);                                               \
			(ytop) += ((item)->name != NULL) ? config.itemheight : SEPARATOR_HEIGHT; \
		}                                                                                \
		return (retdef);                                                                 \
	} while(0);

static struct Item *
getaltitem(struct Menu *menu, KeyCode key, int type, int *ytop)
{
	struct Item *item;

	if (key == 0 || menu == NULL)
		return NULL;
	LOOPYTOP(menu, type, item, *ytop, (key == item->altkey), item, NULL);
	return NULL;
}

static int
getypos(struct Menu *menu, int type)
{
	struct Item *item;
	int ytop;

	if (menu == NULL)
		return 0;
	LOOPYTOP(menu, type, item, ytop, (item == menu->selected), ytop, 0);
	return 0;
}

#define REMOVEPOPPED(ctrl)                \
	do {                              \
		removepopped(ctrl);       \
		menustate = STATE_NORMAL; \
		if (config.mode == 0) {   \
			goto done;        \
		}                         \
	} while (0)

#define OPENITEM(ctrl, menu, item, type, gen, motion, y)                                             \
	do {                                                                                           \
		if (!(gen) && ((item)->genscript != NULL || !TAILQ_EMPTY(&(item)->children))) {        \
			ignorerelease = 0;                                                             \
			if (menustate != STATE_POPUP && (motion))                                      \
				break;                                                                 \
			if (menustate != STATE_POPUP) {                                                \
				initpopped(                                                            \
					(ctrl),                                                        \
					(menu),                                                        \
					(menu)->win,                                                   \
					(menu)->rect,                                                  \
					(item),                                                        \
					&(item)->children,                                             \
					(y)                                                            \
				);                                                                     \
				menustate = STATE_POPUP;                                               \
			} else {                                                                       \
				insertpopupmenu(                                                       \
					&(ctrl)->popupq,                                               \
					(menu)->win,                                                   \
					(menu)->rect,                                                  \
					(item),                                                        \
					&(item)->children,                                             \
					(y)                                                            \
				);                                                                     \
			}                                                                              \
		} else if (!(motion) && (item)->genscript == NULL && TAILQ_EMPTY(&(item)->children)) { \
			enteritem((item));                                                             \
			if (menustate == STATE_POPUP) {                                                \
				REMOVEPOPPED((ctrl));                                                  \
			}                                                                              \
		}                                                                                      \
	} while (0)

static void
run(struct Control *ctrl, struct ItemQueue *itemq)
{
	struct Menu *menu, *rootmenu;
	struct Item *item, *oldsel;
	struct pollfd pfd;
	Window win, scrollwin;
	XEvent ev;
	KeySym ksym;
	int menustate;          /* STATE_NORMAL, STATE_ALTPRESSED or STATE_POPUP */
	int promptopen;         /* whether prompt window is open */
	int gen, type, pos, x, y, scrolled;
	int ret, timeout, len, operation;
	int ignorerelease;      /* whether to ignore button release after pressing on root */
	char buf[INPUTSIZ];

	TAILQ_INIT(&ctrl->tornoffq);
	TAILQ_INIT(&ctrl->popupq);
	ctrl->curroot = NULL;
	ctrl->passclick = 0;
	if (config.mode & MODE_RUNNER)
		ctrl->prompt = setprompt(itemq, &ctrl->promptwin, &promptopen);
	else
		ctrl->prompt = NULL;
	if (config.mode & MODE_DOCKAPP)
		setdockedmenu(&ctrl->docked, itemq);
	initgrabs(ctrl);
	pfd.fd = XConnectionNumber(dpy);
	pfd.events = POLLIN;
	pfd.revents = POLLIN;
	timeout = -1;
	ret = 1;
	ignorerelease = 0;
	scrollwin = None;
	promptopen = 0;
	if (config.mode == 0) {
		menustate = STATE_POPUP;
		querypointer(&x, &y);
		initpopped(
			ctrl,
			NULL,
			root,
			(XRectangle){ .x = x, .y = y, .width = 0, .height = 0 },
			NULL,
			itemq,
			0
		);
	} else {
		menustate = STATE_NORMAL;
	}
	do {
		if (ret == 0) {
			if (scroll(ctrl, scrollwin)) {
				timeout = SCROLL_TIME;
			} else {
				timeout = -1;
				scrollwin = None;
			}
			continue;
		}
		ret = 1;
		if (!(pfd.revents & POLLIN))
			continue;
nextevent:
		XNextEvent(dpy, &ev);
		if (XFilterEvent(&ev, None))
			continue;
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0)
				draw(ctrl, ev.xbutton.window);
			break;
		case LeaveNotify:
			/* clear selection when leaving a root menu */
			if (menustate == STATE_POPUP || (menu = GETROOTMENU(ctrl, ev.xbutton.window)) == NULL)
				break;
			type = getmenutype(ctrl, menu);
			if ((oldsel = menu->selected) != NULL) {
				menu->selected = NULL;
				drawmenu(menu, oldsel, type, 0, 0);
			}
			break;
		case ButtonPress:
			if (ev.xbutton.window != root || invalidbutton(&ev))
				break;
			if (menustate == STATE_NORMAL && (config.mode & MODE_CONTEXT) &&
			    ev.xbutton.button == ctrl->button &&
			    (ctrl->buttonmod == AnyModifier ||
			     (ctrl->buttonmod != 0 && (ev.xbutton.state & MODS) == ctrl->buttonmod) ||
			     (ev.xbutton.subwindow == None))) {
				initpopped(
					ctrl,
					NULL,
					root,
					(XRectangle){ .x = ev.xbutton.x_root, .y = ev.xbutton.y_root, .width = 0, .height = 0 },
					NULL,
					itemq,
					0
				);
				menustate = STATE_POPUP;
				ignorerelease = 1;
			} else if (menustate == STATE_POPUP) {
				REMOVEPOPPED(ctrl);
			}
			break;
		case MotionNotify:
		case ButtonRelease:
			if (ev.type == ButtonRelease && ignorerelease) {
				ignorerelease = 0;
				break;
			}
			if (invalidbutton(&ev))
				break;
			win = (ev.type == MotionNotify) ? ev.xmotion.window : ev.xbutton.window;
			pos = (ev.type == MotionNotify) ? ev.xmotion.y : ev.xbutton.y;
			rootmenu = GETROOTMENU(ctrl, win);
			menu = GETOPENMENU(ctrl, win);
			if (rootmenu != NULL && menu == NULL) {
				if (menustate == STATE_POPUP) {
					REMOVEPOPPED(ctrl);
				}
				menu = rootmenu;
			}
			if (menu == NULL)
				break;
			item = getitem(menu, getmenutype(ctrl, menu), pos, &y);
			type = getmenutype(ctrl, menu);
			timeout = -1;
			if (item != menu->selected) {
				oldsel = menu->selected;
				menu->selected = item;
				drawmenu(menu, oldsel, type, 0, 0);
			}
			if (item == NULL)
				break;
			if (item == &scrollup || item == &scrolldown) {
				scrollwin = win;
				timeout = SCROLL_WAIT;
				break;
			}
			if (item == &tornoff) {
				if (menustate == STATE_POPUP && ev.type == ButtonRelease) {
					TAILQ_REMOVE(&ctrl->popupq, menu, entries);
					insertmenu(&ctrl->tornoffq, menu->win, menu->rect, menu->queue, menu->caller, MENU_TORNOFF, 0);
					delmenu(menu, 0);
					REMOVEPOPPED(ctrl);
				}
				break;
			}
			gen = menugenerated(ctrl, item);  /* whether a menu was generated for this item */
			if (menustate == STATE_POPUP && !gen && menu != TAILQ_FIRST(&ctrl->popupq) && TAILQ_FIRST(&ctrl->popupq) != NULL &&
			    (TAILQ_FIRST(&item->children) != TAILQ_FIRST(TAILQ_FIRST(&ctrl->popupq)->queue))) {
				if (menu == ctrl->curroot) {
					delmenus(&ctrl->popupq, TAILQ_LAST(&ctrl->popupq, MenuQueue));
				} else if (type == MENU_POPUP) {
					delmenus(&ctrl->popupq, TAILQ_PREV(menu, MenuQueue, entries));
				}
			}
			OPENITEM(ctrl, menu, item, type, gen, ev.type == MotionNotify, y);
			break;
		case KeyRelease:
			if (ev.xkey.keycode == ctrl->altkey) {
				if (menustate != STATE_ALTPRESSED)
					break;
				drawmenu(&ctrl->docked, NULL, MENU_DOCKAPP, 0, 1);
				ungrab();
			}
			break;
		case KeyPress:
			ksym = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);
			if (ksym == XK_Escape && menustate == STATE_POPUP) {
				/* esc closes popped up menu when current menu is the root menu */
				REMOVEPOPPED(ctrl);
				break;
			}
			if (ksym == XK_Tab && (ev.xkey.state & ShiftMask)) {
				/* Shift-Tab = ISO_Left_Tab */
				ksym = XK_ISO_Left_Tab;
			}
			if (promptopen && ev.xkey.window == ctrl->promptwin) {
				/* pass key to prompt */
				operation = getoperation(ctrl->prompt, &ev.xkey, buf, INPUTSIZ, &ksym, &len);
				promptkey(ctrl->prompt, buf, len, operation);
			} else if (menustate != STATE_POPUP && (config.mode & MODE_RUNNER) &&
			           ev.xkey.keycode == ctrl->runnerkey &&
			           (ev.xkey.state & MODS) == ctrl->runnermod) {
				/* open prompt */
				mapprompt(ctrl->prompt);
				redrawprompt(ctrl->prompt);
				XFlush(dpy);
			} else if ((item = matchacc(&ctrl->accq, ev.xkey.keycode, ev.xkey.state)) != NULL) {
				/* enter item via accelerator keychord */
				enteritem(item);
				if (menustate == STATE_POPUP) {
					REMOVEPOPPED(ctrl);
				}
			} else if ((config.mode & MODE_DOCKAPP) && ev.xkey.keycode == ctrl->altkey) {
				/* underline altchar on docked menu */
				if (menustate == STATE_POPUP)
					break;
				menustate = STATE_ALTPRESSED;
				grab(GRAB_KEYBOARD);
				drawmenu(&ctrl->docked, NULL, MENU_DOCKAPP, 1, 1);
			} else {
				menu = GETFIRSTMENU(ctrl);
				if (menu != NULL)
					type = getmenutype(ctrl, menu);
				if (menu != NULL && menustate == STATE_POPUP && (ksym == XK_Tab || ksym == XK_Down)) {
					oldsel = menu->selected;
					scrolled = itemcycle(menu, type, 1);
					drawmenu(menu, oldsel, type, menu == &ctrl->docked && menustate == STATE_ALTPRESSED, scrolled);
				} else if (menu != NULL && menustate == STATE_POPUP && (ksym == XK_ISO_Left_Tab || ksym == XK_Up)) {
					oldsel = menu->selected;
					scrolled = itemcycle(menu, type, 0);
					drawmenu(menu, oldsel, type, menu == &ctrl->docked && menustate == STATE_ALTPRESSED, scrolled);
				} else if (menu != NULL && menustate == STATE_POPUP && (ksym == XK_Return || ksym == XK_Right) && menu->selected != NULL) {
					item = menu->selected;
					y = getypos(menu, type);
					ev.type = ButtonRelease;
					OPENITEM(ctrl, menu, item, type, 0, 0, y);
					break;
				} else if (menu != NULL && menustate == STATE_POPUP && ksym == XK_Left && menu != TAILQ_LAST(&ctrl->popupq, MenuQueue)) {
					delmenus(&ctrl->popupq, menu);
					break;
				} else if (ev.xkey.window == root && (menustate == STATE_ALTPRESSED || menustate == STATE_POPUP)) {
					/* enter item via alt key */
					if ((menu = TAILQ_FIRST(&ctrl->popupq)) == NULL)
						menu = &ctrl->docked;
					type = getmenutype(ctrl, menu);
					if ((item = getaltitem(menu, ev.xkey.keycode, type, &y)) == NULL)
						break;
					oldsel = menu->selected;
					menu->selected = item;
					if (menustate == STATE_ALTPRESSED) {
						menustate = STATE_NORMAL;
						ungrab();
					}
					drawmenu(menu, oldsel, type, 1, 0);
					OPENITEM(ctrl, menu, item, type, 0, 0, y);
				} else if ((config.mode & MODE_RUNNER) && ev.xkey.window == root) {
					operation = getoperation(ctrl->prompt, &ev.xkey, buf, INPUTSIZ, &ksym, &len);
					if (operation != INSERT)
						break;
					mapprompt(ctrl->prompt);
					redrawprompt(ctrl->prompt);
					promptkey(ctrl->prompt, buf, len, operation);
					XFlush(dpy);
				}
			}
			break;
		case ConfigureNotify:
			if ((menu = GETROOTMENU(ctrl, ev.xconfigure.window)) == NULL &&
			    (menu = getmenu(&ctrl->popupq, ev.xconfigure.window)) == NULL)
				break;
			type = getmenutype(ctrl, menu);
			configuremenu(menu, ev.xconfigure.x, ev.xconfigure.y, ev.xconfigure.width, ev.xconfigure.height);
			drawmenu(menu, NULL, type, menu == &ctrl->docked && menustate == STATE_ALTPRESSED, 1);
			break;
		case ClientMessage:
			/* user may have closed tornoff window */
			if ((Atom)ev.xclient.data.l[0] != atoms[WM_DELETE_WINDOW])
				break;
			if ((menu = getmenu(&ctrl->tornoffq, ev.xclient.window)) == NULL)
				break;
			if (menustate == STATE_POPUP) {
				REMOVEPOPPED(ctrl);
			}
			TAILQ_REMOVE(&ctrl->tornoffq, menu, entries);
			delmenu(menu, 1);
			break;
		}
		XAllowEvents(dpy, AsyncKeyboard, CurrentTime);
		XAllowEvents(dpy, ctrl->passclick ? ReplayPointer : AsyncPointer, CurrentTime);
		if (XPending(dpy)) {
			goto nextevent;
		}
	} while ((ret = poll(&pfd, 1, timeout)) != -1);
done:
	cleanitems(itemq);
	XAllowEvents(dpy, AsyncKeyboard, CurrentTime);
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
		if (efork() == 0) {
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
		name = (i == COLOR_RUNNER ? "text" : NULL);
		class = (i == COLOR_RUNNER ? "Text" : NULL);
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
	while ((c = getopt(argc, argv, "a:c:ditr:")) != -1) {
		switch (c) {
		case 'a':
			config.altkey = optarg;
			break;
		case 'c':
			config.button = optarg;
			config.mode |= MODE_CONTEXT;
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
	run(&ctrl, &itemq);
	free(config.iconpath);
	xclose();
	return 0;
}
