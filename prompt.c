#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <string.h>

#include "ctrlmenu.h"

#define TITLE        "Runner"
#define DEFWIDTH     600        /* default width */
#define DEFHEIGHT    20         /* default height for each text line */
#define GROUPWIDTH   150        /* width of space for group name */

#define ISMOTION(x) ((x) == CTRLBOL || (x) == CTRLEOL || (x) == CTRLLEFT \
                    || (x) == CTRLRIGHT || (x) == CTRLWLEFT || (x) == CTRLWRIGHT)
#define ISSELECTION(x) ((x) == CTRLSELBOL || (x) == CTRLSELEOL || (x) == CTRLSELLEFT \
                       || (x) == CTRLSELRIGHT || (x) == CTRLSELWLEFT || (x) == CTRLSELWRIGHT)
#define ISEDITING(x) ((x) == CTRLDELBOL || (x) == CTRLDELEOL || (x) == CTRLDELLEFT \
                     || (x) == CTRLDELRIGHT || (x) == CTRLDELWORD || (x) == INSERT)
#define ISUNDO(x) ((x) == CTRLUNDO || (x) == CTRLREDO)

TAILQ_HEAD(UndoQueue, Undo);
struct Undo {
	TAILQ_ENTRY(Undo) entries;
	char *text;
};

struct Prompt {
	int *inited;                     /* whether prompt is in use */

	/* input field */
	char text[INPUTSIZ];            /* input field text */
	size_t textsize;                /* maximum size of the text in the input field */
	size_t cursor;                  /* position of the cursor in the input field */
	size_t select;                  /* position of the selection in the input field*/
	size_t file;                    /* position of the beginning of the file name */

	// TODO: implement history
	FILE *histfp;                   /* pointer to history file */
	char **history;                 /* array of history entries */
	size_t histindex;               /* index to the selected entry in the array */
	size_t histsize;                /* how many entries there are in the array */

	/* undo history */
	struct UndoQueue undoq;         /* undo list */
	struct Undo *undocurr;          /* current undo entry */
	int prevoperation;

	/* items */
	struct ItemQueue *itemq;        /* list of items */
	struct ItemQueue matchq;        /* list of matching items */
	struct ItemQueue deferq;        /* list of matching items */
	struct ItemQueue genq;          /* list of matching items */
	struct Item *firstmatch;        /* first item that matches input */
	struct Item *listfirst;         /* first item that matches input to be listed */
	struct Item *selitem;           /* selected item */
	struct Item **itemarray;        /* array containing nitems matching text */
	int nitems;                     /* number of items in itemarray */
	int maxitems;                   /* maximum number of items in itemarray */

	/* prompt geometry */
	XRectangle rect;                /* width and height of xprompt */
	int separator;                  /* separator width */

	/* drawables */
	Pixmap pix;                     /* where to draw shapes on */
	Window win;                     /* xprompt window */

	/* input context */
	XIC xic;
	char *ictext;
	int caret;
	int composing;                  /* whether user is composing text */
};

void
redrawprompt(struct Prompt *prompt)
{
	commitdrawing(prompt->win, prompt->pix, prompt->rect);
}

/* draw the text on input field, return position of the cursor */
static void
drawinput(struct Prompt *prompt, int copy)
{
	XRectangle rect;
	size_t len;
	int minpos, maxpos;
	int xtext, ytext;
	int widthpre, widthsel;

	minpos = min(prompt->cursor, prompt->select);
	maxpos = max(prompt->cursor, prompt->select);

	/* clear input field */
	rect.x = 0;
	rect.y = 0;
	rect.width = prompt->rect.width;
	rect.height = config.itemheight;
	drawrectangle(prompt->pix, rect, dc.colors[COLOR_RUNNER].background.pixel);

	/* draw text before selection */
	xtext = PADDING;
	ytext = (config.itemheight + dc.face->height) / 2;
	if (minpos > 0) {
		widthpre = textwidth(prompt->text, minpos);
		drawtext(
			prompt->pix,
			&dc.colors[COLOR_RUNNER].foreground,
			xtext,
			ytext,
			prompt->text,
			minpos
		);
	} else {
		widthpre = 0;
	}

	/* draw selected text or pre-edited text */
	xtext += widthpre;
	rect.x = xtext;
	if (prompt->composing) {
		len = strlen(prompt->ictext);
		widthsel = textwidth(prompt->ictext, len);
		rect.width = widthsel;
		drawrectangle(prompt->pix, rect, dc.colors[COLOR_RUNNER].foreground.pixel);
		drawtext(prompt->pix, &dc.colors[COLOR_RUNNER].background, xtext, ytext, prompt->ictext, len);
	} else if (maxpos - minpos > 0) {
		len = maxpos - minpos;
		widthsel = textwidth(prompt->text + minpos, len);
		rect.width = widthsel;
		drawrectangle(prompt->pix, rect, dc.colors[COLOR_RUNNER].foreground.pixel);
		drawtext(prompt->pix, &dc.colors[COLOR_RUNNER].background, xtext, ytext, prompt->text + minpos, len);
	} else {
		widthsel = 0;
	}

	/* draw text after selection */
	xtext += widthsel;
	len = strlen(prompt->text + maxpos);
	drawtext(prompt->pix, &dc.colors[COLOR_RUNNER].foreground, xtext, ytext, prompt->text + maxpos, len);

	/* draw cursor rectangle */
	rect.x = PADDING + widthpre + ((prompt->composing && prompt->caret) ? textwidth(prompt->ictext, prompt->caret) : 0);
	rect.y = (config.itemheight - dc.face->height) / 2;
	rect.width = 1;
	rect.height = config.itemheight - PADDING;
	drawrectangle(prompt->pix, rect, dc.colors[COLOR_RUNNER].foreground.pixel);

	if (copy) {
		redrawprompt(prompt);
	}
}

static void
drawitems(struct Prompt *prompt)
{
	XftColor *color, *altcolor;
	XRectangle rect;
	struct Item *item, *prev;
	unsigned long pixel;
	size_t len;
	int i, ytext;

	rect.x = 0;
	rect.height = config.itemheight;
	rect.width = prompt->rect.width;
	prev = NULL;
	for (i = 0; i < prompt->nitems; i++) {
		item = prompt->itemarray[i];
		if (item == prompt->selitem) {
			pixel = dc.colors[COLOR_RUNNER].selbackground.pixel;
			color = &dc.colors[COLOR_RUNNER].selforeground;
			altcolor = &dc.colors[COLOR_RUNNER].altselforeground;
		} else {
			pixel = dc.colors[COLOR_RUNNER].background.pixel;
			color = &dc.colors[COLOR_RUNNER].foreground;
			altcolor = &dc.colors[COLOR_RUNNER].altforeground;
		}
		rect.y = (i + 1) * config.itemheight + SEPARATOR_HEIGHT;
		drawrectangle(prompt->pix, rect, pixel);
		ytext = rect.y + (config.itemheight + dc.face->ascent) / 2;
		if (prev == NULL || (item->caller != NULL && item->caller != prev->caller)) {
			drawtext(
				prompt->pix,
				altcolor,
				PADDING,
				ytext,
				item->caller->name,
				strlen(item->caller->name)
			);
		}
		len = strlen(item->name);
		drawtext(
			prompt->pix,
			color,
			GROUPWIDTH,
			ytext,
			item->name,
			len
		);
		if (item->desc != NULL) {
			drawtext(
				prompt->pix,
				altcolor,
				GROUPWIDTH + textwidth(item->name, len) + PADDING,
				ytext,
				item->desc,
				strlen(item->desc)
			);
		}
		prev = item;
	}
}

void
drawprompt(struct Prompt *prompt)
{
	drawrectangle(prompt->pix, prompt->rect, dc.colors[COLOR_RUNNER].background.pixel);
	drawseparator(prompt->pix, PADDING, config.itemheight + PADDING, prompt->rect.width - 2 * PADDING, 0);
	drawinput(prompt, 0);
	drawitems(prompt);
	redrawprompt(prompt);
}

/* return location of next utf8 rune in the given direction (+1 or -1) */
static size_t
nextrune(const char *text, size_t position, int inc)
{
	ssize_t n;

	for (n = position + inc; n + inc >= 0 && (text[n] & 0xc0) == 0x80; n += inc)
		;
	return n;
}

/* return bytes from beginning of text to nth utf8 rune to the right */
static size_t
runebytes(const char *text, size_t n)
{
	size_t ret;

	ret = 0;
	while (n-- > 0)
		ret += nextrune(text + ret, 0, 1);
	return ret;
}

/* return number of characters from beginning of text to nth byte to the right */
static size_t
runechars(const char *text, size_t n)
{
	size_t ret, i;

	ret = i = 0;
	while (i < n) {
		i += nextrune(text + i, 0, 1);
		ret++;
	}
	return ret;
}

/* move cursor to start (dir = -1) or end (dir = +1) of the word */
static size_t
movewordedge(const char *text, size_t pos, int dir)
{
	if (dir < 0) {
		while (pos > 0 && isspace((unsigned char)text[nextrune(text, pos, -1)]))
			pos = nextrune(text, pos, -1);
		while (pos > 0 && !isspace((unsigned char)text[nextrune(text, pos, -1)]))
			pos = nextrune(text, pos, -1);
	} else {
		while (text[pos] && isspace((unsigned char)text[pos]))
			pos = nextrune(text, pos, +1);
		while (text[pos] && !isspace((unsigned char)text[pos]))
			pos = nextrune(text, pos, +1);
	}
	return pos;
}

/* when this is called, the input method was closed */
static void
icdestroy(XIC xic, XPointer clientdata, XPointer calldata)
{
	struct Prompt *prompt;

	(void)xic;
	(void)calldata;
	prompt = (struct Prompt *)clientdata;
	prompt->xic = NULL;
}

/* start input method pre-editing */
static int
preeditstart(XIC xic, XPointer clientdata, XPointer calldata)
{
	struct Prompt *prompt;

	(void)xic;
	(void)calldata;
	prompt = (struct Prompt *)clientdata;
	prompt->composing = 1;
	prompt->ictext = emalloc(INPUTSIZ);
	prompt->ictext[0] = '\0';
	return INPUTSIZ;
}

/* end input method pre-editing */
static void
preeditdone(XIC xic, XPointer clientdata, XPointer calldata)
{
	struct Prompt *prompt;

	(void)xic;
	(void)calldata;
	prompt = (struct Prompt *)clientdata;
	prompt->composing = 0;
	free(prompt->ictext);
	prompt->ictext = NULL;
}

/* draw input method pre-edit text */
static void
preeditdraw(XIC xic, XPointer clientdata, XPointer calldata)
{
	XIMPreeditDrawCallbackStruct *pdraw;
	struct Prompt *prompt;
	size_t beg, dellen, inslen, endlen;

	(void)xic;
	prompt = (struct Prompt *)clientdata;
	pdraw = (XIMPreeditDrawCallbackStruct *)calldata;
	if (!pdraw)
		return;

	/* we do not support wide characters */
	if (pdraw->text && pdraw->text->encoding_is_wchar == True) {
		warnx("warning: xprompt does not support wchar; use utf8!");
		return;
	}

	beg = runebytes(prompt->ictext, pdraw->chg_first);
	dellen = runebytes(prompt->ictext + beg, pdraw->chg_length);
	inslen = pdraw->text ? runebytes(pdraw->text->string.multi_byte, pdraw->text->length) : 0;
	endlen = 0;
	if (beg + dellen < strlen(prompt->ictext))
		endlen = strlen(prompt->ictext + beg + dellen);

	/* we cannot change text past the end of our pre-edit string */
	if (beg + dellen >= prompt->textsize || beg + inslen >= prompt->textsize)
		return;

	/* get space for text to be copied, and copy it */
	memmove(prompt->ictext + beg + inslen, prompt->ictext + beg + dellen, endlen + 1);
	if (pdraw->text && pdraw->text->length)
		memcpy(prompt->ictext + beg, pdraw->text->string.multi_byte, inslen);
	(prompt->ictext + beg + inslen + endlen)[0] = '\0';

	/* get caret position */
	prompt->caret = runebytes(prompt->ictext, pdraw->caret);

	drawinput(prompt, 1);
}

/* move caret on pre-edit text */
static void
preeditcaret(XIC xic, XPointer clientdata, XPointer calldata)
{
	XIMPreeditCaretCallbackStruct *pcaret;
	struct Prompt *prompt;

	(void)xic;
	prompt = (struct Prompt *)clientdata;
	pcaret = (XIMPreeditCaretCallbackStruct *)calldata;
	if (!pcaret)
		return;
	switch (pcaret->direction) {
	case XIMForwardChar:
		prompt->caret = nextrune(prompt->ictext, prompt->caret, +1);
		break;
	case XIMBackwardChar:
		prompt->caret = nextrune(prompt->ictext, prompt->caret, -1);
		break;
	case XIMForwardWord:
		prompt->caret = movewordedge(prompt->ictext, prompt->caret, +1);
		break;
	case XIMBackwardWord:
		prompt->caret = movewordedge(prompt->ictext, prompt->caret, -1);
		break;
	case XIMLineStart:
		prompt->caret = 0;
		break;
	case XIMLineEnd:
		if (prompt->ictext[prompt->caret] != '\0')
			prompt->caret = strlen(prompt->ictext);
		break;
	case XIMAbsolutePosition:
		prompt->caret = runebytes(prompt->ictext, pcaret->position);
		break;
	case XIMDontChange:
		/* do nothing */
		break;
	case XIMCaretUp:
	case XIMCaretDown:
	case XIMNextLine:
	case XIMPreviousLine:
		/* not implemented */
		break;
	}
	pcaret->position = runechars(prompt->ictext, prompt->caret);
	drawinput(prompt, 1);
}

/* delete selected text */
static void
delselection(struct Prompt *prompt)
{
	int minpos, maxpos;
	size_t len;

	if (prompt->select == prompt->cursor)
		return;
	minpos = min(prompt->cursor, prompt->select);
	maxpos = max(prompt->cursor, prompt->select);
	len = strlen(prompt->text + maxpos);
	memmove(prompt->text + minpos, prompt->text + maxpos, len + 1);
	prompt->cursor = prompt->select = minpos;
}

/* insert string on prompt->text and update prompt->cursor */
static void
insert(struct Prompt *prompt, const char *str, ssize_t n)
{
	if (strlen(prompt->text) + n > prompt->textsize - 1)
		return;

	/* move existing text out of the way, insert new text, and update cursor */
	memmove(&prompt->text[prompt->cursor + n], &prompt->text[prompt->cursor],
	        prompt->textsize - prompt->cursor - max(n, 0));
	if (n > 0)
		memcpy(&prompt->text[prompt->cursor], str, n);
	prompt->cursor += n;
	prompt->select = prompt->cursor;
}

/* delete word from the input field */
static void
delword(struct Prompt *prompt)
{
	while (prompt->cursor > 0 && isspace((unsigned char)prompt->text[nextrune(prompt->text, prompt->cursor, -1)]))
		insert(prompt, NULL, nextrune(prompt->text, prompt->cursor, -1) - prompt->cursor);
	while (prompt->cursor > 0 && !isspace((unsigned char)prompt->text[nextrune(prompt->text, prompt->cursor, -1)]))
		insert(prompt, NULL, nextrune(prompt->text, prompt->cursor, -1) - prompt->cursor);
}

/* copy entry from undo list into text */
static void
undo(struct Prompt *prompt)
{
	if (prompt->undocurr != NULL) {
		if (prompt->undocurr->text == NULL) {
			return;
		}
		if (strcmp(prompt->undocurr->text, prompt->text) == 0) {
			prompt->undocurr = TAILQ_NEXT(prompt->undocurr, entries);
		}
	}
	if (prompt->undocurr != NULL) {
		insert(prompt, NULL, 0 - prompt->cursor);
		insert(prompt, prompt->undocurr->text, strlen(prompt->undocurr->text));
		prompt->undocurr = TAILQ_NEXT(prompt->undocurr, entries);
	}
}

/* copy entry from undo list into text */
static void
redo(struct Prompt *prompt)
{
	if (prompt->undocurr && TAILQ_PREV(prompt->undocurr, UndoQueue, entries))
		prompt->undocurr = TAILQ_PREV(prompt->undocurr, UndoQueue, entries);
	if (prompt->undocurr && TAILQ_PREV(prompt->undocurr, UndoQueue, entries) && strcmp(prompt->undocurr->text, prompt->text) == 0)
		prompt->undocurr = TAILQ_PREV(prompt->undocurr, UndoQueue, entries);
	if (prompt->undocurr) {
		insert(prompt, NULL, 0 - prompt->cursor);
		insert(prompt, prompt->undocurr->text, strlen(prompt->undocurr->text));
	}
}

/* add entry to undo list */
static void
addundo(struct Prompt *prompt, int editing)
{
	struct Undo *undo, *tmp;

	/* when adding a new entry to the undo list, delete the entries after current one */
	undo = TAILQ_PREV(prompt->undocurr, UndoQueue, entries);
	while (undo != NULL) {
		tmp = undo;
		undo = TAILQ_PREV(undo, UndoQueue, entries);
		TAILQ_REMOVE(&prompt->undoq, tmp, entries);
		free(tmp->text);
		free(tmp);
	}

	/* add a new entry only if it differs from the one at the top of the list */
	undo = TAILQ_FIRST(&prompt->undoq);
	if (undo->text == NULL || strcmp(undo->text, prompt->text) != 0) {
		undo = emalloc(sizeof(*undo));
		undo->text = estrdup(prompt->text);
		TAILQ_INSERT_HEAD(&prompt->undoq, undo, entries);

		/* if we are editing text, the current entry is the top one*/
		if (editing) {
			prompt->undocurr = undo;
		}
	}
}

/* check whether item matches text */
static int
itemmatch(struct Item *item, const char *text, size_t textlen, int middle)
{
	if (textlen == 0)
		return !middle;
	return ((*config.fstrncmp)(item->name, text, textlen) == 0)
	    != (middle && (*config.fstrstr)(item->name, text) != NULL);
}

/* search for matching items and fill matchq */
static void
searchitems(struct Prompt *prompt, struct ItemQueue *itemq, const char *text, size_t len, int middle)
{
	struct Item *item = NULL;

	TAILQ_FOREACH(item, itemq, entries) {
		if (item->name == NULL) {
			continue;
		} else if (!TAILQ_EMPTY(&item->children)) {
			searchitems(prompt, &item->children, text, len, middle);
		} else if (item->genscript != NULL && !middle) {
			//
		} else if (itemmatch(item, text, len, middle)) {
			TAILQ_INSERT_TAIL(&prompt->matchq, item, matches);
		}
	}
}

static void
getgenerators(struct Prompt *prompt, struct ItemQueue *itemq)
{
	struct Item *item = NULL;

	TAILQ_FOREACH(item, itemq, entries) {
		if (item->name == NULL) {
			continue;
		} else if (!TAILQ_EMPTY(&item->children)) {
			getgenerators(prompt, &item->children);
		} else if (item->genscript != NULL) {
			TAILQ_INSERT_TAIL(&prompt->deferq, item, defers);
		}
	}
}

/* navigate through the list of matching items; and fill item array */
static void
navmatchlist(struct Prompt *prompt, int direction)
{
	struct Item *item;
	int i, selnum;

	if (direction != 0 && prompt->selitem == NULL) {
		prompt->selitem = prompt->listfirst;
		goto done;
	}
	if (!prompt->selitem)
		goto done;
	if (direction > 0 && TAILQ_NEXT(prompt->selitem, matches)) {
		prompt->selitem = TAILQ_NEXT(prompt->selitem, matches);
		for (selnum = 0, item = prompt->listfirst; 
		     selnum < prompt->maxitems && item != NULL && item != TAILQ_PREV(prompt->selitem, ItemQueue, matches);
		     selnum++, item = TAILQ_NEXT(item, matches))
			;
		if (selnum + 1 >= prompt->maxitems) {
			for (i = 0, item = prompt->listfirst;
			     i < prompt->maxitems && item != NULL;
			     i++, item = TAILQ_NEXT(item, matches))
				;
			prompt->listfirst = (item != NULL) ? item : prompt->selitem;
		}
	} else if (direction < 0 && TAILQ_PREV(prompt->selitem, ItemQueue, matches)) {
		prompt->selitem = TAILQ_PREV(prompt->selitem, ItemQueue, matches);
		if (prompt->selitem == TAILQ_PREV(prompt->listfirst, ItemQueue, matches)) {
			for (i = 0, item = prompt->listfirst;
			     i < prompt->maxitems && item;
			     i++, item = TAILQ_PREV(item, ItemQueue, matches))
				;
			prompt->listfirst = (item != NULL) ? item : prompt->firstmatch;
		}
	}

done:
	/* fill .itemarray */
	for (i = 0, item = prompt->listfirst;
	     i < prompt->maxitems && item;
	     i++, item = TAILQ_NEXT(item, matches))
		prompt->itemarray[i] = item;
	prompt->nitems = i;
}

static void
getmatchlist(struct Prompt *prompt)
{
	struct Item *item;
	size_t len;
	const char *text;

	len = strlen(prompt->text);
	text = prompt->text;
	TAILQ_INIT(&prompt->matchq);
	searchitems(prompt, prompt->itemq, text, len, 0);
	TAILQ_FOREACH(item, &prompt->deferq, defers)
		searchitems(prompt, item->genchildren, text, len, 0);
	searchitems(prompt, prompt->itemq, text, len, 1);
	TAILQ_FOREACH(item, &prompt->deferq, defers)
		searchitems(prompt, item->genchildren, text, len, 1);
	item = TAILQ_FIRST(&prompt->matchq);
	prompt->firstmatch = item;
	prompt->listfirst = item;
	prompt->selitem = NULL;
	navmatchlist(prompt, 0);
}

static void
initundo(struct Prompt *prompt)
{
	struct Undo *undo;

	undo = emalloc(sizeof(*undo));
	undo->text = NULL;
	TAILQ_INIT(&prompt->undoq);
	TAILQ_INSERT_HEAD(&prompt->undoq, undo, entries);
	prompt->undocurr = TAILQ_FIRST(&prompt->undoq);
}

static void
cleanundo(struct Prompt *prompt)
{
	struct Undo *undo;

	while ((undo = TAILQ_FIRST(&prompt->undoq)) != NULL) {
		TAILQ_REMOVE(&prompt->undoq, undo, entries);
		free(undo->text);
		free(undo);
	}
}

void *
setprompt(struct ItemQueue *itemq, Window *win, int *inited)
{
	XICCallback start, done, draw, caret, destroy;
	XVaNestedList preedit = NULL;
	XIMStyles *imstyles;
	XIMStyle preeditstyle;
	XIMStyle statusstyle;
	struct Prompt *prompt;
	long eventmask;
	int i;

	prompt = emalloc(sizeof(*prompt));
	*prompt = (struct Prompt){
		.inited = inited,
		.ictext = NULL,
		.textsize = INPUTSIZ,
		.cursor = 0,
		.select = 0,
		.file = 0,
		.undocurr = NULL,
		.itemq = itemq,
		.firstmatch = NULL,
		.selitem = NULL,
		.listfirst = NULL,
		.maxitems = config.runner_items,
		.nitems = 0,
	};
	prompt->itemarray = ecalloc(prompt->maxitems, sizeof(*prompt->itemarray)),
	prompt->rect.x = prompt->rect.y = 0;
	prompt->rect.width = DEFWIDTH;
	prompt->rect.height = SEPARATOR_HEIGHT + config.itemheight * (prompt->maxitems + 1);
;
	prompt->win = createwindow(&prompt->rect, RUNNER, TITLE);

	/* create callbacks for the input method */
	destroy.client_data = NULL;
	destroy.callback = (XICProc)icdestroy;

	/* set destroy callback for the input method */
	if (XSetIMValues(xim, XNDestroyCallback, &destroy, NULL) != NULL)
		warnx("XSetIMValues: could not set input method values");

	/* get styles supported by input method */
	if (XGetIMValues(xim, XNQueryInputStyle, &imstyles, NULL) != NULL)
		errx(1, "XGetIMValues: could not obtain input method values");

	/* check whether input method support on-the-spot pre-editing */
	preeditstyle = XIMPreeditNothing;
	statusstyle = XIMStatusNothing;
	for (i = 0; i < imstyles->count_styles; i++) {
		if (imstyles->supported_styles[i] & XIMPreeditCallbacks) {
			preeditstyle = XIMPreeditCallbacks;
			break;
		}
	}

	/* create callbacks for the input context */
	start.client_data = (XPointer)prompt;
	done.client_data = (XPointer)prompt;
	draw.client_data = (XPointer)prompt;
	caret.client_data = (XPointer)prompt;
	start.callback = (XICProc)preeditstart;
	done.callback = (XICProc)preeditdone;
	draw.callback = (XICProc)preeditdraw;
	caret.callback = (XICProc)preeditcaret;

	/* create list of values for input context */
	preedit = XVaCreateNestedList(0,
                                      XNPreeditStartCallback, &start,
                                      XNPreeditDoneCallback, &done,
                                      XNPreeditDrawCallback, &draw,
                                      XNPreeditCaretCallback, &caret,
                                      NULL);
	if (preedit == NULL)
		errx(1, "XVaCreateNestedList: could not create nested list");

	/* create input context */
	prompt->xic = XCreateIC(xim,
	                   XNInputStyle, preeditstyle | statusstyle,
	                   XNPreeditAttributes, preedit,
	                   XNClientWindow, prompt->win,
	                   XNDestroyCallback, &destroy,
	                   NULL);
	if (prompt->xic == NULL)
		errx(1, "XCreateIC: could not obtain input method");

	/* get events the input method is interested in */
	if (XGetICValues(prompt->xic, XNFilterEvents, &eventmask, NULL))
		errx(1, "XGetICValues: could not obtain input context values");

	XSelectInput(dpy, prompt->win, StructureNotifyMask |
	             ExposureMask | KeyPressMask | VisibilityChangeMask |
	             ButtonPressMask | PointerMotionMask | eventmask);
	
	XFree(preedit);

	*win = prompt->win;
	prompt->pix = createpixmap(prompt->rect, prompt->win);
	drawprompt(prompt);

	return prompt;
}

void
mapprompt(struct Prompt *prompt)
{
	struct Item *item;

	if (*prompt->inited)
		return;
	prompt->text[0] = '\0';
	prompt->cursor = 0;
	prompt->select = 0;
	*prompt->inited = 1;
	initundo(prompt);
	TAILQ_INIT(&prompt->deferq);
	getgenerators(prompt, prompt->itemq);
	TAILQ_FOREACH(item, &prompt->deferq, defers) {
		item->genchildren = emalloc(sizeof(*item->genchildren));
		readpipe(item->genchildren, item);
	}
	getmatchlist(prompt);
	drawprompt(prompt);
	XMapWindow(dpy, prompt->win);
	XFlush(dpy);
}

void
unmapprompt(struct Prompt *prompt)
{
	struct Item *item;

	if (!(*prompt->inited))
		return;
	*prompt->inited = 0;
	XUnmapWindow(dpy, prompt->win);
	cleanundo(prompt);
	free(prompt->ictext);
	prompt->ictext = NULL;
	TAILQ_FOREACH(item, &prompt->deferq, defers) {
		cleanitems(item->genchildren);
		free(item->genchildren);
		item->genchildren = NULL;
	}
	XFlush(dpy);
}

int
getoperation(struct Prompt *prompt, XKeyEvent *ev, char *buf, size_t bufsize, KeySym *ksym, int *len)
{
	Status status;

	*len = XmbLookupString(prompt->xic, ev, buf, bufsize, ksym, &status);
	switch (status) {
	default:        /* XLookupNone, XBufferOverflow */
		return CTRLNOTHING;
	case XLookupChars:
		if (iscntrl(*buf) || *buf == '\0')
			return CTRLNOTHING;
		return INSERT;
	case XLookupKeySym:
	case XLookupBoth:
		break;
	}

	switch (*ksym) {
	case XK_Escape:         return CTRLCANCEL;
	case XK_Return:         return CTRLENTER;
	case XK_KP_Enter:       return CTRLENTER;
	case XK_ISO_Left_Tab:   return CTRLPREV;
	case XK_Tab:            return CTRLNEXT;
	case XK_Prior:          return CTRLPGUP;
	case XK_Next:           return CTRLPGDOWN;
	case XK_BackSpace:      return CTRLDELLEFT;
	case XK_Delete:         return CTRLDELRIGHT;
	case XK_Up:             return CTRLUP;
	case XK_Down:           return CTRLDOWN;
	case XK_Home:
		if (ev->state & ShiftMask)
			return CTRLSELBOL;
		return CTRLBOL;
	case XK_End:
		if (ev->state & ShiftMask)
			return CTRLSELEOL;
		return CTRLEOL;
	case XK_Left:
		if (ev->state & ShiftMask && ev->state & ControlMask)
			return CTRLSELWLEFT;
		if (ev->state & ShiftMask)
			return CTRLSELLEFT;
		if (ev->state & ControlMask)
			return CTRLWLEFT;
		return CTRLLEFT;
	case XK_Right:
		if (ev->state & ShiftMask && ev->state & ControlMask)
			return CTRLSELWRIGHT;
		if (ev->state & ShiftMask)
			return CTRLSELRIGHT;
		if (ev->state & ControlMask)
			return CTRLWRIGHT;
		return CTRLRIGHT;
	}

	/* handle Ctrl + Letter combinations */
	if (ev->state & ControlMask && ((*ksym >= XK_a && *ksym <= XK_z) || (*ksym >= XK_A && *ksym <= XK_Z))) {
		if (ev->state & ShiftMask) {
			switch (*ksym) {
			case XK_A: case XK_a:   return CTRLSELBOL;
			case XK_E: case XK_e:   return CTRLSELEOL;
			case XK_B: case XK_b:   return CTRLSELLEFT;
			case XK_F: case XK_f:   return CTRLSELRIGHT;
			case XK_Z: case XK_z:   return CTRLREDO;
			}
		} else {
			switch (*ksym) {
			case XK_a:              return CTRLBOL;
			case XK_b:              return CTRLLEFT;
			case XK_c:              return CTRLCOPY;
			case XK_d:              return CTRLDELRIGHT;
			case XK_e:              return CTRLEOL;
			case XK_f:              return CTRLRIGHT;
			case XK_h:              return CTRLDELLEFT;
			case XK_k:              return CTRLDELEOL;
			case XK_m:              return CTRLENTER;
			case XK_n:              return CTRLNEXT;
			case XK_p:              return CTRLPREV;
			case XK_u:              return CTRLDELBOL;
			case XK_v:              return CTRLPASTE;
			case XK_w:              return CTRLDELWORD;
			case XK_z:              return CTRLUNDO;
			}
		}
		return CTRLNOTHING;
	}

	if (iscntrl(*buf) || *buf == '\0')
		return CTRLNOTHING;

	return INSERT;
}

void
promptkey(struct Prompt *prompt, char *buf, int len, int operation)
{

	if (operation == CTRLNOTHING)
		return;
	if (ISUNDO(operation) && ISEDITING(prompt->prevoperation))
		addundo(prompt, 0);
	if (ISEDITING(operation) && operation != prompt->prevoperation)
		addundo(prompt, 1);
	prompt->prevoperation = operation;
	switch (operation) {
	case CTRLPASTE:
		XConvertSelection(dpy, atoms[CLIPBOARD], atoms[UTF8_STRING], atoms[UTF8_STRING], prompt->win, CurrentTime);
		return;
	case CTRLCOPY:
		XSetSelectionOwner(dpy, atoms[CLIPBOARD], prompt->win, CurrentTime);
		return;
	case CTRLCANCEL:
		unmapprompt(prompt);
		return;
	case CTRLENTER:
		enteritem(prompt->selitem);
		unmapprompt(prompt);
		return;
	case CTRLPREV:
		/* FALLTHROUGH */
	case CTRLNEXT:
		if (!prompt->listfirst)
			getmatchlist(prompt);
		else if (operation == CTRLNEXT)
			navmatchlist(prompt, 1);
		else if (operation == CTRLPREV)
			navmatchlist(prompt, -1);
		break;
	case CTRLPGUP:
	case CTRLPGDOWN:
		// TODO
		return;
	case CTRLSELBOL:
	case CTRLBOL:
		prompt->cursor = 0;
		break;
	case CTRLSELEOL:
	case CTRLEOL:
		if (prompt->text[prompt->cursor] != '\0')
			prompt->cursor = strlen(prompt->text);
		break;
	case CTRLUP:
		/* FALLTHROUGH */
	case CTRLDOWN:
		// dir = (operation == CTRLUP) ? -1 : +1;
		// if (prompt->histsize == 0)
		// 	return;
		// s = navhist(prompt, dir);
		// if (s) {
		// 	insert(prompt, NULL, 0 - prompt->cursor);
		// 	insert(prompt, s, strlen(s));
		// }
		break;
	case CTRLSELLEFT:
	case CTRLLEFT:
		if (prompt->cursor > 0)
			prompt->cursor = nextrune(prompt->text, prompt->cursor, -1);
		else
			return;
		break;
	case CTRLSELRIGHT:
	case CTRLRIGHT:
		if (prompt->text[prompt->cursor] != '\0')
			prompt->cursor = nextrune(prompt->text, prompt->cursor, +1);
		else
			return;
		break;
	case CTRLSELWLEFT:
	case CTRLWLEFT:
		prompt->cursor = movewordedge(prompt->text, prompt->cursor, -1);
		break;
	case CTRLSELWRIGHT:
	case CTRLWRIGHT:
		prompt->cursor = movewordedge(prompt->text, prompt->cursor, +1);
		break;
	case CTRLDELBOL:
		insert(prompt, NULL, 0 - prompt->cursor);
		break;
	case CTRLDELEOL:
		prompt->text[prompt->cursor] = '\0';
		break;
	case CTRLDELRIGHT:
	case CTRLDELLEFT:
		if (prompt->cursor != prompt->select) {
			delselection(prompt);
			break;
		}
		if (operation == CTRLDELRIGHT) {
			if (prompt->text[prompt->cursor] == '\0')
				return;
			prompt->cursor = nextrune(prompt->text, prompt->cursor, +1);
		}
		if (prompt->cursor == 0)
			return;
		insert(prompt, NULL, nextrune(prompt->text, prompt->cursor, -1) - prompt->cursor);
		break;
	case CTRLDELWORD:
		delword(prompt);
		break;
	case CTRLUNDO:
		undo(prompt);
		break;
	case CTRLREDO:
		redo(prompt);
		break;
	case CTRLNOTHING:
		return;
	case INSERT:
		operation = INSERT;
		delselection(prompt);
		insert(prompt, buf, len);
		break;
	default:
		break;
	}
	if (ISMOTION(operation)) {          /* moving cursor while selecting */
		prompt->select = prompt->cursor;
		drawprompt(prompt);
		return;
	}
	if (ISSELECTION(operation)) {       /* moving cursor while selecting */
		XSetSelectionOwner(dpy, XA_PRIMARY, prompt->win, CurrentTime);
		drawinput(prompt, 1);
		return;
	}
	if (ISEDITING(operation) || ISUNDO(operation)) {
		getmatchlist(prompt);
		drawprompt(prompt);
		return;
	}
	drawprompt(prompt);
}
