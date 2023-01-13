#include <sys/wait.h>

#include <err.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ctrlmenu.h"

#define BUFSIZE 1024
#define KSYMBUF 32
#define UTFMAX  4

/* token types */
enum {
	TOK_NONE,
	TOK_NEWLINE,
	TOK_CMD,
	TOK_NAME,
	TOK_OPENCURLY,
	TOK_CLOSECURLY,
	TOK_OPENSQUARE,
	TOK_CLOSESQUARE,
	TOK_ASTERISK,
	TOK_CARET,
	TOK_COMMA,
	TOK_ACCEL,
	TOK_WHITESPACE,
	TOK_EOF,
};

/* parsedata for reentrant parsing */
struct ParseData {
	/* housekeeping data */
	FILE *fp;
	char *filename;
	size_t lineno;
	int error;
	int eof;
	int ispipe;

	/* the token buffer */
	char *bufdata;
	int bufsize;
	int bufindex;

	/* the token itself */
	int toktype;
	char *toktext;
	char *alt;
	size_t toksize;
};

static void parselistrec(struct ParseData *parse, struct ItemQueue *itemq, struct Item *parent, struct AcceleratorQueue *accq);

static int
efgets(struct ParseData *parse)
{
	if (fgets(parse->bufdata, parse->bufsize, parse->fp) == NULL) {
		if (ferror(parse->fp))
			err(1, "fgets");
		parse->eof = 1;
		return EOF;
	}
	parse->bufindex = 0;
	parse->lineno++;
	return '\0';
}

static void
readtok(struct ParseData *parse, int isname)
{
	size_t nread, i;
	int eot;                        /* whether we reach end of token */
	int inc;                        /* whether to increment bufindex */

	eot = 0;
	i = 0;
	nread = 1;
	parse->alt = NULL;
	while (!eot) {
		while (isblank((unsigned char)parse->bufdata[parse->bufindex]))
			parse->bufindex++;
		nread += strlen(&parse->bufdata[parse->bufindex]);
		if (parse->toksize < nread) {
			if (parse->toksize == 0)
				parse->toksize = parse->bufsize;
			else
				parse->toksize <<= 1;
			parse->toktext = erealloc(parse->toktext, parse->toksize);
		}
		while (parse->bufdata[parse->bufindex] != '\0') {
			inc = 1;
			if (!parse->ispipe && strchr("{}", parse->bufdata[parse->bufindex]) != NULL) {
				parse->toktext[i] = '\0';
				eot = 1;
				break;
			} else if (parse->bufdata[parse->bufindex] == '\n') {
				parse->toktext[i] = '\0';
				eot = 1;
				break;
			} else if (isname && parse->bufdata[parse->bufindex] == '-' && parse->bufdata[parse->bufindex+1] == '-') {
				parse->toktext[i] = '\0';
				eot = 1;
				break;
			} else if (isname && parse->bufdata[parse->bufindex] == '_' &&
			           parse->bufdata[parse->bufindex + 1] != '\n' &&
			           parse->bufdata[parse->bufindex + 1] != '\0') {
				parse->alt = parse->toktext + i;
				parse->toktext[i++] = parse->bufdata[++parse->bufindex];
			} else if (isname && strchr("[]", parse->bufdata[parse->bufindex]) != NULL) {
				parse->toktext[i] = '\0';
				eot = 1;
				break;
			} else if (parse->bufdata[parse->bufindex] == '\\' &&
			           parse->bufdata[parse->bufindex + 1] != '\n' &&
			           parse->bufdata[parse->bufindex + 1] != '\0') {
				switch (parse->bufdata[++parse->bufindex]) {
				case 'a':
					parse->toktext[i++] = '\a';
					break;
				case 'b':
					parse->toktext[i++] = '\b';
					break;
				case 'f':
					parse->toktext[i++] = '\f';
					break;
				case 'n':
					parse->toktext[i++] = '\n';
					break;
				case 'r':
					parse->toktext[i++] = '\r';
					break;
				case 't':
					parse->toktext[i++] = '\t';
					break;
				case '\n':
					if (efgets(parse) == EOF) {
						warnx("%s:%zu: parse error: unexpected end of file", parse->filename, parse->lineno);
						parse->error = 1;
						eot = 1;
						break;
					}
					inc = 0;
					break;
				default:
					parse->toktext[i++] = parse->bufdata[parse->bufindex];
					break;
				}
			} else {
				parse->toktext[i++] = parse->bufdata[parse->bufindex];
			}
			if (inc) {
				parse->bufindex++;
			}
		}
	}
	if (i > 0) {
		while (isblank((unsigned char)parse->toktext[i-1]))
			i--;
	}
	parse->toktext[i] = '\0';
}

static void
advance(struct ParseData *parse)
{
	if (parse->bufdata[parse->bufindex] == '\0') {
		/* if there's no data in the buffer, fill it with more data */
		if (efgets(parse) == EOF) {
			parse->toktype = TOK_EOF;
			parse->eof = 1;
			return;
		}
	}
	while (isblank((unsigned char)parse->bufdata[parse->bufindex]))
		parse->bufindex++;
	if (parse->bufdata[parse->bufindex] == '{') {
		parse->toktype = TOK_OPENCURLY;
		parse->bufindex++;
	} else if (parse->bufdata[parse->bufindex] == '}') {
		parse->toktype = TOK_CLOSECURLY;
		parse->bufindex++;
	} else if (parse->bufdata[parse->bufindex] == '[') {
		parse->toktype = TOK_OPENSQUARE;
		parse->bufindex++;
	} else if (parse->bufdata[parse->bufindex] == ']') {
		parse->toktype = TOK_CLOSESQUARE;
		parse->bufindex++;
	} else if (parse->bufdata[parse->bufindex] == '\n') {
		parse->toktype = TOK_NEWLINE;
		parse->bufindex++;
	} else if (parse->bufdata[parse->bufindex] == '-' && parse->bufdata[parse->bufindex + 1] == '-') {
		/* we got two dashes, read command into toktext */
		parse->bufindex += 2;
		parse->toktype = TOK_CMD;
		readtok(parse, 0);
	} else {
		parse->toktype = TOK_NAME;
		readtok(parse, 1);
	}
}

static int
gettok(struct ParseData *parse)
{
	int rettype;

	if (parse->eof)
		return TOK_EOF;
	else if (parse->toktype == TOK_NONE)
		advance(parse);
	rettype = parse->toktype;
	parse->toktype = TOK_NONE;
	return rettype;
}

static void
ungettok(struct ParseData *parse, int toktype)
{
	parse->toktype = toktype;
}

static int
check(struct ParseData *parse, int expected)
{
	int gottype;

	gottype = gettok(parse);
	ungettok(parse, gottype);
	return gottype == expected;
}

static void
consume(struct ParseData *parse, int expected)
{
	static char *toktab[] = {
		[TOK_NAME]        = "entry name",
		[TOK_OPENCURLY]   = "\"{\"",
		[TOK_CLOSECURLY]  = "\"}\"",
		[TOK_OPENSQUARE]  = "\"[\"",
		[TOK_CLOSESQUARE] = "\"]\"",
		[TOK_ASTERISK]    = "\"*\"",
		[TOK_CARET]       = "\"^\"",
		[TOK_COMMA]       = "\",\"",
		[TOK_ACCEL]       = "key chord",
		[TOK_CMD]         = "command",
		[TOK_WHITESPACE]  = "whitespace",
		[TOK_NEWLINE]     = "newline",
		[TOK_EOF]         = "end of file",
	};
	int gottype;

	if ((gottype = gettok(parse)) != expected) {
		warnx(
			"%s:%zu: parse error: expected %s; got %s",
			parse->filename,
			parse->lineno,
			toktab[expected],
			toktab[gottype]
		);
		parse->error = 1;
		parse->bufindex = 0;
		parse->bufdata[0] = '\0';
	}
}

static struct Accelerator *
newaccelerator(char *str, unsigned int mods, struct Item *item)
{
	struct Accelerator *acc;

	acc = emalloc(sizeof(*acc));
	*acc = (struct Accelerator){
		.mods = mods,
		.item = item,
		.key = getkeycode(str),
	};
	return acc;
}

static void
setaltkey(struct ParseData *parse, struct Item *item)
{
	static const unsigned char utfbyte[] = {0x80, 0x00, 0xC0, 0xE0, 0xF0};
	static const unsigned char utfmask[] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
	char buf[KSYMBUF];
	size_t i, n;

	if (parse->alt != NULL) {
		/* read alt sequence */
		for (i = n = 0; i < UTFMAX; i++) {
			if (((unsigned char)parse->alt[n] & utfmask[i]) == utfbyte[i]) {
				n++;
				break;
			}
		}
		if (n < KSYMBUF - 1 && n > 0 && i < UTFMAX) {
			memcpy(buf, parse->alt, n);
			buf[n] = '\0';
			item->altpos = parse->alt - parse->toktext;
			item->altlen = n;
			item->altkey = getkeycode(buf);
		}
	}
}

static void
parsename(struct ParseData *parse, struct Item *item)
{
	consume(parse, TOK_NAME);
	setaltkey(parse, item);
	item->name = estrdup(parse->toktext);
	item->len = strlen(item->name);
}

static void
parsebraces(struct ParseData *parse, struct Item *item, struct AcceleratorQueue *accq)
{
	struct Accelerator *acc;
	size_t len, i;
	unsigned int mods;
	char *file, *accstr, *desc, *p;

	file = NULL;
	desc = NULL;
	accstr = NULL;
	consume(parse, TOK_NAME);
	for (p = strtok(parse->toktext, ":");
	     p != NULL;
	     p = strtok(NULL, ":")) {
		while (isblank((unsigned char)p[0]))
			p++;
		if (*p == '#') {
			file = p + 1;
		} else if (*p == '!') {
			accstr = p + 1;
		} else {
			desc = p;
			break;
		}
	}
	if (accq != NULL && accstr != NULL) {
		mods = 0;
		item->acc = estrdup(accstr);
		while (accstr[0] != '\0' && accstr[1] == '-') {
			switch (accstr[0]) {
			case 'S': mods |= ShiftMask;    break;
			case 'C': mods |= ControlMask;  break;
			case 'A': mods |= Mod1Mask;     break;
			case 'W': mods |= Mod4Mask;     break;
			default:
				warnx("%s:%zu: parse error: unknown modifier \"%c-\"", parse->filename, parse->lineno, accstr[0]);
				parse->error = 1;
				return;
			}
			accstr += 2;
		}
		for (i = 0; !isblank(accstr[i]); i++)
			;
		accstr[i] = '\0';
		if (accq != NULL) {
			acc = newaccelerator(accstr, mods, item);
			TAILQ_INSERT_HEAD(accq, acc, entries);
		}
	}
	if (file != NULL) {
		len = strlen(file);
		while (len > 0 && isblank((unsigned char)file[len-1]))
			len--;
		item->file = estrndup(file, len);
	}
	if (desc != NULL) {
		len = strlen(desc);
		while (len > 0 && isblank((unsigned char)desc[len-1]))
			len--;
		item->desc = estrndup(desc, len);
	}
}

static char *
parsescript(struct ParseData *parse)
{
	size_t nread, spn, len, i;

	nread = 1;
	i = 0;
	while (efgets(parse) != EOF) {
		while (isblank((unsigned char)parse->bufdata[parse->bufindex]))
			parse->bufindex++;
		len = strlen(&parse->bufdata[parse->bufindex]);
		nread += len;
		if (parse->toksize < nread) {
			if (parse->toksize == 0)
				parse->toksize = parse->bufsize;
			else
				parse->toksize <<= 1;
			parse->toktext = erealloc(parse->toktext, parse->toksize);
		}
		if (parse->bufdata[parse->bufindex] == '}') {
			break;
		}
		spn = strcspn(&parse->bufdata[parse->bufindex], "\n");
		if (spn > 1 && parse->bufdata[parse->bufindex + spn - 1] == '\\') {
			memmove(&parse->toktext[i], &parse->bufdata[parse->bufindex], spn - 1);
			i += spn - 1;
		} else {
			memmove(&parse->toktext[i], &parse->bufdata[parse->bufindex], spn + 1);
			i += spn + 1;
		}
	}
	parse->toktext[i] = '\0';
	return estrdup(parse->toktext);
}

static void
parsecmd(struct ParseData *parse, struct Item *item)
{
	consume(parse, TOK_CMD);
	item->cmd = estrdup(parse->toktext);
}

static struct Item *
parseitemrec(struct ParseData *parse, struct Item *parent, struct AcceleratorQueue *accq)
{
	struct Item *item;

	item = emalloc(sizeof(*item));
	*item = (struct Item){
		.caller = parent,
		.name = NULL,
		.desc = NULL,
		.cmd = NULL,
		.altpos = 0,
		.altlen = 0,
		.altkey = 0,
		.acc = NULL,
		.genscript = NULL,
		.flags = 0,
		.file = NULL,
		.icon = None,
		.mask = None,
	};
	TAILQ_INIT(&item->children);
	if (check(parse, TOK_CMD)) {
		/*
		 * A line with only "--" is a separator.
		 * Just consume the "--" back and ignore everything after it.
		 */
		consume(parse, TOK_CMD);
	} else {
		parsename(parse, item);
		if (check(parse, TOK_OPENSQUARE)) {
			consume(parse, TOK_OPENSQUARE);
			parsebraces(parse, item, accq);
			consume(parse, TOK_CLOSESQUARE);
		}
		if (check(parse, TOK_CMD)) {
			parsecmd(parse, item);
			if (check(parse, TOK_OPENCURLY)) {
				consume(parse, TOK_OPENCURLY);
				item->genscript = parsescript(parse);
				consume(parse, TOK_CLOSECURLY);
			}
		} else if (check(parse, TOK_OPENCURLY)) {
			consume(parse, TOK_OPENCURLY);
			parselistrec(parse, &item->children, item, accq);
			consume(parse, TOK_CLOSECURLY);
		}
	}
	consume(parse, TOK_NEWLINE);
	return item;
}

static void
parselistrec(struct ParseData *parse, struct ItemQueue *itemq, struct Item *parent, struct AcceleratorQueue *accq)
{
	struct Item *item;
	int toktype;

	TAILQ_INIT(itemq);
	while((toktype = gettok(parse)) != TOK_EOF && toktype != TOK_CLOSECURLY) {
		if (toktype == TOK_NEWLINE)     /* consume blank lines */
			continue;
		ungettok(parse, toktype);
		if ((item = parseitemrec(parse, parent, accq)) == NULL)
			break;
		TAILQ_INSERT_TAIL(itemq, item, entries);
	}
	ungettok(parse, toktype);
}

static struct Item *
parsepipeditem(char *buf, struct Item *caller)
{
	struct Item *item;
	char *s;

	item = emalloc(sizeof(*item));
	*item = (struct Item){
		.caller = caller,
		.name = NULL,
		.desc = NULL,
		.cmd = NULL,
		.altpos = 0,
		.altlen = 0,
		.altkey = 0,
		.acc = NULL,
		.genscript = NULL,
		.flags = ITEM_ISGEN,
		.file = NULL,
		.icon= None,
		.mask= None,
	};
	TAILQ_INIT(&item->children);
	if ((s = strchr(buf, '\t')) != NULL) {
		*s = '\0';
		item->name = estrdup(buf);
		item->len = strlen(item->name);
		buf = s + 1;
		if ((s = strchr(buf, '\t')) != NULL) {
			*s = '\0';
			item->desc = estrdup(buf);
			buf = s + 1;
		}
		item->cmd = estrdup(buf);
	} else {
		item->name = estrdup(buf);
		item->len = strlen(item->name);
	}
	return item;
}

static void
parsepipe(struct ParseData *parse, struct ItemQueue *itemq, struct Item *caller)
{
	struct Item *item;
	ssize_t linelen;
	size_t linesize = 0;
	char *line = NULL;
	char *s;

	TAILQ_INIT(itemq);
	while ((linelen = getline(&line, &linesize, parse->fp)) != -1) {
		s = line;
		while (isblank(*(unsigned char *)s))
			s++;
		if (*s == '\n' || *s == '\0')
			continue;
		s[strcspn(s, "\n")] = '\0';
		if ((item = parsepipeditem(s, caller)) == NULL)
			break;
		TAILQ_INSERT_TAIL(itemq, item, entries);
	}
}

void
readpipe(FILE *fp, struct ItemQueue *itemq, struct Item *caller)
{
	struct ParseData parse;
	char buf[BUFSIZE];

	buf[0] = '\0';
	parse = (struct ParseData){
		.fp = fp,
		.filename = "(generated)",
		.lineno = 0,
		.error = 0,
		.eof = 0,
		.ispipe = 1,

		.bufdata = buf,
		.bufsize = BUFSIZE,

		.toktype = TOK_NONE,
		.toktext = NULL,
		.toksize = 0,
		.alt = NULL,
	};
	parsepipe(&parse, itemq, caller);
	fclose(fp);
	free(parse.toktext);
	wait(NULL);
	if (parse.error) {
		cleanitems(itemq);
	}
}

void
genmenu(struct ItemQueue *itemq, struct Item *caller)
{
	FILE *fp;
	int fd[2];

	epipe(fd);
	if (efork() == 0) {      /* child */
		close(fd[0]);
		if (fd[1] != STDOUT_FILENO)
			edup2(fd[1], STDOUT_FILENO);
		eexecshell(caller->genscript, NULL);
		exit(1);
	}
	if ((fp = fdopen(fd[0], "r")) == NULL) {
		warnx("could open file pointer");
		return;
	}
	close(fd[1]);
	readpipe(fp, itemq, caller);
}

void
runcalc(struct ItemQueue *itemq, char *text)
{
	FILE *fp[2];
	int fd[2][2];

	epipe(fd[0]);
	epipe(fd[1]);
	if (efork() == 0) {      /* child */
		close(fd[0][1]);
		close(fd[1][0]);
		if (fd[0][0] != STDIN_FILENO)
			edup2(fd[0][0], STDIN_FILENO);
		if (fd[1][1] != STDOUT_FILENO)
			edup2(fd[1][1], STDOUT_FILENO);
		eexeccmd(calculator, NULL);
		exit(1);
	}
	close(fd[0][0]);
	close(fd[1][1]);
	if ((fp[0] = fdopen(fd[0][1], "w")) == NULL) {
		warnx("could open file pointer");
		return;
	}
	if ((fp[1] = fdopen(fd[1][0], "r")) == NULL) {
		warnx("could open file pointer");
		return;
	}
	fprintf(fp[0], "%s\n", text);
	fclose(fp[0]);
	readpipe(fp[1], itemq, NULL);
}

void
cleanitems(struct ItemQueue *itemq)
{
	struct Item *item;

	while ((item = TAILQ_FIRST(itemq)) != NULL) {
		if (item->name != NULL)
			free(item->name);
		if (item->desc != NULL)
			free(item->desc);
		if (item->cmd != NULL)
			free(item->cmd);
		if (item->acc != NULL)
			free(item->acc);
		if (item->genscript != NULL)
			free(item->genscript);
		if (item->file != NULL)
			free(item->file);
		cleanitems(&item->children);
		TAILQ_REMOVE(itemq, item, entries);
		if (item->icon != None)
			XFreePixmap(dpy, item->icon);
		if (item->mask != None)
			XFreePixmap(dpy, item->mask);
		free(item);
	}
}

void
cleanaccelerators(struct AcceleratorQueue *accq)
{
	struct Accelerator *acc;

	while ((acc = TAILQ_FIRST(accq)) != NULL) {
		TAILQ_REMOVE(accq, acc, entries);
		free(acc);
	}
}

void
readfile(FILE *fp, char *filename, struct ItemQueue *itemq, struct AcceleratorQueue *accq)
{
	struct ParseData parse;
	char buf[BUFSIZE];

	buf[0] = '\0';
	parse = (struct ParseData){
		.fp = fp,
		.filename = filename,
		.lineno = 0,
		.error = 0,
		.eof = 0,
		.ispipe = 0,

		.bufdata = buf,
		.bufsize = BUFSIZE,

		.toktype = TOK_NONE,
		.toktext = NULL,
		.alt = NULL,
		.toksize = 0,
	};
	TAILQ_INIT(accq);
	parselistrec(&parse, itemq, NULL, accq);
	free(parse.toktext);
	if (parse.error) {
		cleanitems(itemq);
		cleanaccelerators(accq);
		exit(1);
	}
}
