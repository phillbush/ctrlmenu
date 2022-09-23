#include <sys/wait.h>

#include <err.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ctrlmenu.h"

#define GETROOTMENU(c, w)  (((w) == (c)->docked.win) ? &(c)->docked : getmenu(&(c)->tornoffq, (w)))
#define GETOPENMENU(c, w)  (((c)->curroot != NULL && (w) == (c)->curroot->win) ? (c)->curroot : getmenu(&(c)->popupq, (w)))
#define TORNOFF_HEIGHT     (config.tornoff ? SEPARATOR_HEIGHT : 0)
#define MODS               (ShiftMask | ControlMask | Mod1Mask | Mod4Mask)
#define SCROLL_TIME        100
#define SCROLL_WAIT        200

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

	KeyCode runnerkey;
	unsigned int runnermod;

	struct Prompt *prompt;
	Window promptwin;
};

/* dummy items */
struct Item tornoff = { .name = "tornoff" };
struct Item scrollup = { .name = "scrollup" };
struct Item scrolldown = { .name = "scrolldown" };

static void
usage(void)
{
	(void)fprintf(stderr, "usage: ctrlmenu [-cdit] [-a keysym] [-r keychord] [file]\n");
	exit(1);
}

static void
drawmenu(struct Menu *menu, int menutype, int alt)
{
	struct Item *item;
	XRectangle rect;
	XftColor *color;
	size_t acclen;
	int beg, textx, texty, separatorwid, right;
	int altx, altw;

	/* draw menu */
	rect.x = rect.y = beg = 0;
	rect.width = menu->rect.width;
	rect.height = menu->rect.height;
	drawrectangle(menu->pix, rect, dc.menubackground.pixel);
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
	if (menu->overflow)
		rect.y += SEPARATOR_HEIGHT;
	for (item = menu->first; item != NULL; item = TAILQ_NEXT(item, entries)) {
		color = (item == menu->selected ? &dc.menuselforeground : &dc.menuforeground);
		if (item->name == NULL) {
			rect.height = SEPARATOR_HEIGHT;
			drawseparator(menu->pix, rect.x + PADDING, rect.y + PADDING, separatorwid, 0);
		} else {
			rect.height = config.fontheight;
			texty = rect.y + (rect.height + dc.face->ascent) / 2;

			/* draw rectangle below menu item */
			drawrectangle(menu->pix, rect, (item == menu->selected ? dc.menuselbackground.pixel : dc.menubackground.pixel));

			/* draw menu item text */
			textx = rect.x + PADDING;
			if (config.alignment == ALIGN_CENTER)
				textx += (menu->maxwidth - textwidth(item->name, item->len)) / 2;
			else if (config.alignment == ALIGN_RIGHT)
				textx += menu->maxwidth - textwidth(item->name, item->len);
			textx = max(rect.x + PADDING, textx);
			if (alt && item->altlen > 0 && item->altpos + item->altlen <= item->len) {
				altx = textx + textwidth(item->name, item->altpos);
				altw = textwidth(item->name + item->altpos, item->altlen);
				drawrectangle(
					menu->pix,
					(XRectangle){ .x = altx, .y = texty + 1, .width = altw, .height = 1 },
					color->pixel
				);
			}
			drawtext(menu->pix, color, textx, texty, item->name, item->len);
			if (item->acc != NULL) {
				acclen = strlen(item->acc);
				drawtext(
					menu->pix,
					color,
					menu->rect.width - textwidth(item->acc, acclen) - right,
					texty,
					item->acc,
					acclen
				);
			}
			if (menutype != MENU_DOCKAPP && (item->genscript != NULL || !TAILQ_EMPTY(&item->children))) {
				drawtriangle(
					menu->pix,
					color->pixel,
					rect.width - rect.x - PADDING - config.triangle_width,
					rect.y + (rect.height - config.triangle_height) / 2,
					DIR_RIGHT
				);
			}
		}

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
			color = (menu->selected == &tornoff ? &dc.menuselbackground : &dc.menubackground);
			drawrectangle(menu->pix, rect, color->pixel);
			drawseparator(menu->pix, config.shadowThickness + PADDING, config.shadowThickness + PADDING, separatorwid, 1);
			rect.y += SEPARATOR_HEIGHT;
		}
		rect.x = config.shadowThickness;
		rect.y = config.shadowThickness + SEPARATOR_HEIGHT;
	} else {
		rect.x = rect.y = 0;
	}
	if (menu->overflow) {
		drawrectangle(
			menu->pix,
			rect,
			(menu->selected == &scrollup ? dc.menuselbackground.pixel : dc.menubackground.pixel)
		);
		drawtriangle(
			menu->pix,
			(menu->selected == &scrollup ? dc.menuselforeground.pixel : dc.menuforeground.pixel),
			rect.x + (menu->rect.width - config.triangle_height) / 2,
			rect.y + (SEPARATOR_HEIGHT - config.triangle_width) / 2,
			DIR_UP
		);
		rect.y = menu->rect.height - SEPARATOR_HEIGHT;
		drawrectangle(
			menu->pix,
			rect,
			(menu->selected == &scrolldown ? dc.menuselbackground.pixel : dc.menubackground.pixel)
		);
		drawtriangle(
			menu->pix,
			(menu->selected == &scrolldown ? dc.menuselforeground.pixel : dc.menuforeground.pixel),
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
			dc.menutopShadow.pixel,
			dc.menubottomShadow.pixel
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
			root->rect.height += config.fontheight;
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
	drawmenu(root, MENU_DOCKAPP, 0);
	mapwin(root->win);
}

static void
insertmenu(struct MenuQueue *menuq, Window parentwin, XRectangle parentrect, struct ItemQueue *itemq, struct Item *caller, int type, int y)
{
	XRectangle mon;
	struct Menu *menu;
	struct Item *item;
	int menuh, textw, accelw, xplusw;

	getmonitors();
	translatecoordinates(parentwin, &parentrect.x, &parentrect.y);
	mon = getselmon(&parentrect);
	menu = emalloc(sizeof(*menu));
	*menu = (struct Menu){
		.queue = itemq,
		.first = TAILQ_FIRST(itemq),
		.caller = caller,
		.overflow = 0,
		.maxwidth = 0,
		.isgen = (caller != NULL && caller->genscript != NULL),
		.selected = NULL,
		.pix = None,
		.rect = (XRectangle){
			.x = parentrect.x,
			.y = parentrect.y,
			.width = config.shadowThickness * 2 + SEPARATOR_HEIGHT,
			.height = config.shadowThickness * 2 + SEPARATOR_HEIGHT,
		},
	};
	TAILQ_INSERT_HEAD(menuq, menu, entries);
	if (TAILQ_EMPTY(menu->queue))
		goto done;
	menuh = 0;
	if (type == MENU_POPUP)
		menuh = config.shadowThickness * 2 + TORNOFF_HEIGHT;
	accelw = 0;
	TAILQ_FOREACH(item, menu->queue, entries) {
		if (item->name != NULL) {
			textw = textwidth(item->name, strlen(item->name));
			if (item->acc != NULL) {
				accelw = max(accelw, textwidth(item->acc, strlen(item->acc)) + 2 * PADDING);
			}
			menuh += config.fontheight;
		} else {
			textw = 0;
			menuh += SEPARATOR_HEIGHT;
		}
		menu->maxwidth = max(menu->maxwidth, textw);
		if (!menu->overflow) {
			if (menuh + config.fontheight + SEPARATOR_HEIGHT * 2 < mon.height) {
				menu->rect.height = menuh;
			} else {
				menu->overflow = 1;
				menu->rect.height = mon.height;
			}
		}
	}
	menu->rect.width = menu->maxwidth + accelw;
	menu->rect.width += config.triangle_width + PADDING * 3;     /* PAD + name + PAD + triangle + PAD */
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
		xplusw = parentrect.x + parentrect.width;
		if (menu->rect.width > mon.width / 2)
			menu->rect.width = mon.width / 2;
		if (mon.x + mon.width - xplusw >= menu->rect.width) {
			menu->rect.x = xplusw;
		} else if (parentrect.x > menu->rect.width) {
			menu->rect.x = parentrect.x - menu->rect.width;
		}
		y -= config.shadowThickness + TORNOFF_HEIGHT;
		y = max(0, y);
		if (!menu->overflow) {
			if (mon.y + mon.height - (parentrect.y + y) >= menu->rect.height) {
				menu->rect.y = parentrect.y + y;
			} else if (mon.y + mon.height > menu->rect.height) {
				menu->rect.y = mon.y + mon.height - menu->rect.height;
			}
		}
	}

	menu->win = createwindow(&menu->rect, type, caller != NULL ? caller->name : CLASS);
	menu->pix = createpixmap(menu->rect, menu->win);
	drawmenu(menu, type, 0);
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
	if (TAILQ_EMPTY(&ctrl->popupq) || menu == ctrl->curroot)
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
		if (item->name != NULL && y >= h && y < h + config.fontheight)
			break;
		h += (item->name != NULL) ? config.fontheight : SEPARATOR_HEIGHT;
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

static void
bindaccs(struct AcceleratorQueue *accq, KeyCode key, unsigned int mods)
{
	struct Accelerator *acc;

	grabkey(key, mods);
	TAILQ_FOREACH(acc, accq, entries) {
		grabkey(acc->key, acc->mods);
	}
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
			h += (item->name != NULL) ? config.fontheight : SEPARATOR_HEIGHT;
			if (h >= end) {
				break;
			}
		}
		if (item != NULL && TAILQ_NEXT(menu->first, entries) != NULL) {
			h = (item->name != NULL) ? config.fontheight : SEPARATOR_HEIGHT;
			menu->first = TAILQ_NEXT(menu->first, entries);
			drawmenu(menu, menutype, 0);
			XFlush(dpy);
		} else {
			return 0;
		}
	} else if (TAILQ_PREV(menu->first, ItemQueue, entries) != NULL) {
		menu->first = TAILQ_PREV(menu->first, ItemQueue, entries);
		drawmenu(menu, menutype, 0);
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

static void
initgrabs(struct Control *ctrl)
{
	const char *s;

	ctrl->altkey = getkeycode(config.altkey);
	ctrl->runnermod = 0;
	s = config.runner;
	while (s[0] != '\0' && s[1] == '-') {
		switch (s[0]) {
		case 'S': ctrl->runnermod |= ShiftMask;   break;
		case 'C': ctrl->runnermod |= ControlMask; break;
		case 'A': ctrl->runnermod |= Mod1Mask;    break;
		case 'W': ctrl->runnermod |= Mod4Mask;    break;
		default: break;
		}
		s += 2;
	}
	ctrl->runnerkey = getkeycode(s);
	bindaccs(&ctrl->accq, ctrl->runnerkey, ctrl->runnermod);
	grabkeysync(ctrl->altkey);
	if (config.mode & MODE_CONTEXT) {
		grabbuttonsync(Button3);
	}
}

static void
removepopped(struct Control *ctrl)
{
	struct Menu *firstpopped;

	ungrab();
	firstpopped = TAILQ_LAST(&ctrl->popupq, MenuQueue);
	delmenus(&ctrl->popupq, firstpopped);
	if (ctrl->curroot != NULL) {
		ctrl->curroot->selected = NULL;
		drawmenu(ctrl->curroot, getmenutype(ctrl, ctrl->curroot), 0);
		ctrl->curroot = NULL;
	}
	return;
}

static void
initpopped(struct Control *ctrl, struct Menu *rootmenu)
{
	ctrl->curroot = rootmenu;
	// insertpopupmenu(&ctrl->popupq, ctrl->curroot, ctrl->curroot->selected, y);
	if (grab(GRAB_POINTER | GRAB_KEYBOARD) == -1)
		removepopped(ctrl);
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

static struct Item *
getaltitem(struct Menu *menu, KeySym ksym, int menutype, int *ytop)
{
	struct Item *item;

	if (ksym == NoSymbol || menu == NULL)
		return NULL;
	if (menutype == MENU_POPUP)
		*ytop = config.shadowThickness + TORNOFF_HEIGHT;
	else
		*ytop = 0;
	if (menu->overflow)
		*ytop += SEPARATOR_HEIGHT;
	TAILQ_FOREACH(item, menu->queue, entries) {
		if (ksym == item->altkeysym)
			return item;
		*ytop += (item->name != NULL) ? config.fontheight : SEPARATOR_HEIGHT;
	}
	return NULL;
}

static void
run(struct Control *ctrl, struct ItemQueue *itemq)
{
	struct Menu *menu, *rootmenu;
	struct Item *item;
	struct pollfd pfd;
	KeySym ksym;
	Window win, scrollwin;
	XEvent ev;
	int promptopen;         /* either 1 (prompt open) or 0 (prompt closer) */
	int menustate;          /* STATE_NORMAL, STATE_ALTPRESSED or STATE_POPUP */
	int gen, type, pos, ytop, len;
	int ret, timeout, operation;
	int ignorerelease;      /* whether to ignore button release after pressing on root */
	char buf[INPUTSIZ];

	TAILQ_INIT(&ctrl->tornoffq);
	TAILQ_INIT(&ctrl->popupq);
	ctrl->curroot = NULL;
	ctrl->prompt = setprompt(itemq, &ctrl->promptwin, &promptopen);
	if (config.mode & MODE_DOCKAPP)
		setdockedmenu(&ctrl->docked, itemq);
	initgrabs(ctrl);
	pfd.fd = XConnectionNumber(dpy);
	pfd.events = POLLIN;
	pfd.revents = POLLIN;
	timeout = -1;
	ret = 1;
	ignorerelease = 0;
	promptopen = 0;
	menustate = STATE_NORMAL;
	scrollwin = None;
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
			if (menu->selected != NULL) {
				menu->selected = NULL;
				drawmenu(menu, type, 0);
			}
			break;
		case ButtonPress:
			if (ev.xbutton.window != root || invalidbutton(&ev))
				break;
			if (menustate == STATE_NORMAL && (config.mode & MODE_CONTEXT) &&
			    ev.xbutton.subwindow == None && ev.xbutton.button == Button3) {
				initpopped(ctrl, NULL);
				insertpopupmenu(
					&ctrl->popupq,
					root,
					(XRectangle){ .x = ev.xbutton.x_root, .y = ev.xbutton.y_root, .width = 0, .height = 0 },
					NULL,
					itemq,
					0
				);
				menustate = STATE_POPUP;
				ignorerelease = 1;
			} else if (menustate == STATE_POPUP) {
				removepopped(ctrl);
				menustate = STATE_NORMAL;
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
					removepopped(ctrl);
					menustate = STATE_NORMAL;
				}
				menu = rootmenu;
			}
			if (menu == NULL)
				break;
			item = getitem(menu, getmenutype(ctrl, menu), pos, &ytop);
			type = getmenutype(ctrl, menu);
			timeout = -1;
			if (item == NULL)
				break;
			if (item == &scrollup || item == &scrolldown) {
				scrollwin = win;
				menu->selected = item;
				timeout = SCROLL_WAIT;
				drawmenu(menu, type, 0);
				break;
			}
			if (item == &tornoff) {
				menu->selected = item;
				drawmenu(menu, type, 0);
				if (menustate == STATE_POPUP && ev.type == ButtonRelease) {
					TAILQ_REMOVE(&ctrl->popupq, menu, entries);
					insertmenu(&ctrl->tornoffq, menu->win, menu->rect, menu->queue, menu->caller, MENU_TORNOFF, 0);
					delmenu(menu, 0);
					removepopped(ctrl);
					menustate = STATE_NORMAL;
				}
				break;
			}
			if (item != menu->selected) {
				menu->selected = item;
				drawmenu(menu, type, 0);
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
selectitem:
			if (!gen && (item->genscript != NULL || !TAILQ_EMPTY(&item->children))) {
				ignorerelease = 0;
				if (menustate != STATE_POPUP && ev.type == MotionNotify)
					break;
				if (menustate != STATE_POPUP) {
					initpopped(ctrl, menu);
					menustate = STATE_POPUP;
				}
				insertpopupmenu(&ctrl->popupq, menu->win, menu->rect, item, &item->children, ytop);
			} else if (ev.type == ButtonRelease && item->genscript == NULL && TAILQ_EMPTY(&item->children)) {
				enteritem(item);
				if (menustate == STATE_POPUP) {
					removepopped(ctrl);
					menustate = STATE_NORMAL;
				}
			}
			break;
		case KeyRelease:
			if (ev.xkey.keycode == ctrl->altkey) {
				if (menustate != STATE_ALTPRESSED)
					break;
				drawmenu(&ctrl->docked, MENU_DOCKAPP, 0);
				ungrab();
			}
			break;
		case KeyPress:
			if (promptopen && ev.xkey.window == ctrl->promptwin) {
				/* pass key to prompt */
				operation = getoperation(ctrl->prompt, &ev.xkey, buf, INPUTSIZ, &ksym, &len);
				promptkey(ctrl->prompt, buf, len, operation);
			} else if (menustate != STATE_POPUP && ev.xkey.keycode == ctrl->runnerkey && (ev.xkey.state & MODS) == ctrl->runnermod) {
				/* open prompt */
				mapprompt(ctrl->prompt);
				redrawprompt(ctrl->prompt);
				XFlush(dpy);
			} else if (!promptopen && (item = matchacc(&ctrl->accq, ev.xkey.keycode, ev.xkey.state)) != NULL) {
				/* enter item via accelerator keychord */
				enteritem(item);
				if (menustate == STATE_POPUP) {
					removepopped(ctrl);
					menustate = STATE_NORMAL;
				}
			} else if (ev.xkey.keycode == ctrl->altkey) {
				/* underline altchar on docked menu */
				if (menustate == STATE_POPUP)
					break;
				menustate = STATE_ALTPRESSED;
				grab(GRAB_KEYBOARD);
				drawmenu(&ctrl->docked, MENU_DOCKAPP, 1);
			} else if (ev.xkey.window == root) {
				operation = getoperation(ctrl->prompt, &ev.xkey, buf, INPUTSIZ, &ksym, &len);
				if (operation == CTRLCANCEL) {
					removepopped(ctrl);
					menustate = STATE_NORMAL;
					break;
				}
				if (operation != INSERT)
					break;
				if (menustate == STATE_ALTPRESSED || menustate == STATE_POPUP) {
					if ((menu = TAILQ_FIRST(&ctrl->popupq)) == NULL)
						menu = &ctrl->docked;
					type = getmenutype(ctrl, menu);
					if ((item = getaltitem(menu, ksym, type, &ytop)) == NULL)
						break;
					menu->selected = item;
					ev.type = ButtonRelease;
					gen = 0;
					if (menustate == STATE_ALTPRESSED) {
						menustate = STATE_NORMAL;
						ungrab();
					}
					drawmenu(menu, type, 1);
					goto selectitem;
				} else {
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
			drawmenu(menu, type, menu == &ctrl->docked && menustate == STATE_ALTPRESSED);
			break;
		case ClientMessage:
			if ((Atom)ev.xclient.data.l[0] != atoms[WM_DELETE_WINDOW])
				break;
			/* user may have closed tornoff window */
			if (ev.xclient.window == ctrl->promptwin) {
				unmapprompt(ctrl->prompt);
				break;
			}
			if ((menu = getmenu(&ctrl->tornoffq, ev.xclient.window)) == NULL)
				break;
			if (menustate == STATE_POPUP) {
				removepopped(ctrl);
				menustate = STATE_NORMAL;
			}
			TAILQ_REMOVE(&ctrl->tornoffq, menu, entries);
			delmenu(menu, 1);
			break;
		}
		XAllowEvents(dpy, AsyncKeyboard, CurrentTime);
		XAllowEvents(dpy, ReplayPointer, CurrentTime);
		if (XPending(dpy)) {
			goto nextevent;
		}
	} while ((ret = poll(&pfd, 1, timeout)) != -1);
	XAllowEvents(dpy, AsyncKeyboard, CurrentTime);
	XAllowEvents(dpy, ReplayPointer, CurrentTime);
}

void
enteritem(struct Item *item)
{
	char *cmd, *arg;

	if (item == NULL)
		return;
	if (item->isgen) {
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
	int c;

	xinit(argc, argv);
	while ((c = getopt(argc, argv, "a:cditr:")) != -1) {
		switch (c) {
		case 'a':
			config.altkey = optarg;
			break;
		case 'c':
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
			break;
		default:
			usage();
			break;
		}
	}
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
	run(&ctrl, &itemq);
	xclose();
	return 0;
}
