CTRLMENU(1) - General Commands Manual

# NAME

**ctrlmenu** - unified menu system for X11

# SYNOPSIS

**ctrlmenu**
\[**-cdit**]
\[**-a**&nbsp;*keysym*]
\[**-r**&nbsp;*keychord*]
\[*file*]

# DESCRIPTION

**ctrlmenu**
reads a text file describing entries in a hierarchical menu,
and creates input methods for selecting and entering those entries.
When an entry is selected, its respective command is run in a subshell.

The options are as follows

**-a** *keysym*

> The symbol for the alt key (none by default).
> Despite its name, this key can be any key.

**-c**

> Show a context menu when right-clicking the desktop.

**-d**

> Show a docked menu on the window manager's dock.

**-i**

> Ignore case on the runner.

**-t**

> Enables tornoff menus.

**-r** *keychord*

> The keychord to invoke the runner (A-space, by default).
> The keychord must be a keysym, prefixed with
> **C-**
> (for Control),
> **S-**
> (for Shift),
> **A-**
> (for Alt),
> **W-**
> (for Super/Win).

Each line read from the specified file or stdin can be a separator,
a terminal entry, a common meta-entry, or a pipe meta-entry.
Any blank around an entry is ignored

	<ENTRY>     := <SEPARATOR> | <TERMINAL> | <META> | <PIPE>
	<SEPARATOR> := "--\n"
	<TERMINAL>  := <NAME> ["[" <KEYCHORD> "]"] ["--" <COMMAND>] "\n"
	<META>      := <NAME> "{\n" <ENTRIES>... "}\n"
	<PIPE>      := <NAME> "--" <COMMAND> "{\n" <SCRIPT> "}\n"

Separator

> A separator is an anonymous, non-selectable entry in the menu.
> Separators are composed by a line with only two dashes in it.

Terminal Entry

> A terminal entry is a line composed by a name, an optional keychord
> between square braces, and an optional command after two dashes.

Common Meta-Entry

> A common meta-entry is a line composed by a name followed by an open curly bracket,
> then a sequence of entries, followed by a close curly bracket.

Pipe Meta-Entry

> A pipe meta-entry is a line composed by a name followed two dashes and a command.
> Then a shell script between curly braces.
> The given shell script is run and its output is read for generated menu entries.

The name can have a underscore before the entry's alt character.

A keychord has the following form.
An
"S-"
prefix indicates that the
**Shift**
key modifier is active.
An
"C-"
prefix indicates that the
**Control**
key modifier is active.
An
"A-"
prefix indicates that the
**Alt**
key modifier is active.
An
"W-"
prefix indicates that the
**Super**
(Also known as
"windows key")
key modifier is active.
The keysym that forms the ending of the keychord is one of the keysyms listed at
the
*/usr/X11R6/include/X11/keysymdef.h*
file
(*/usr/include/X11/keysymdef.h* in some systems.)

	<KEYCHORD>  := ("S-"|"C-"|"A-"|"W-")... <KEYSYM>

# USAGE

The following input methods are provided by
**ctrlmenu**

Docked Menu

> The docked menu is an always-visible dockapp window that displays a list of menus
> on the window manager's dock.
> The docked menu is only displayed when using the
> **-d**
> command-line option.

Context Menu

> The context menu is a menu that pops up when the user right-clicks the desktop.
> The context menu is only displayed when using the
> **-c**
> command-line option.

Tornoff Menu

> A tornoff menu is a menu that appears as a regular window.
> A tornoff menu is created after selecting the dashed line at the top of a popped up menu.
> This dashed line only appears when using the
> **-t**
> command-line option.

Alt Key Sequences

> Each menu entry can be bound to a character.
> After pressing the alt key (set with the
> **-a**
> command-line option),
> it is possible to navigate the menus by pressing the letters of entries in sequence.

Accelerator key chords

> Each menu entry can be bound to a key chord.
> Pressing a key chord enters the entry bound to it.

Runner

> The runner is a window that is visible after pressing the runner key chord
> (**Alt + space**, by default).
> The runner displays a text input field where the user can type in a text to filter entries,
> and select an entry using
> **Tab**
> and
> **Shift Tab**.
> When the user presses
> **Return**,
> the selected entry is entered.

Any menu (either the docked menu, the context menu, a tornoff menu or a popped up menu)
can be browsed with the mouse, and each entry can then be clicked to be selected.
After a submenu is opened, they can be navigated using the arrow keys and the
**Tab**,
**Esc**,
and
**Enter**
keys.

# RESOURCES

**ctrlmenu**
understands the following X resources.

**ctrlmenu.faceName**

> The typeface to write text with.

**ctrlmenu.runner.ignoreCase**

> If set to
> "True",
> ignores case on the runner.

**ctrlmenu.runner.keychord**

> The keychord to spawn the runner.

**ctrlmenu.runner.background**

> The background color of non-selected entries in the runner.

**ctrlmenu.runner.foreground**

> The color of the label text of non-selected entries in the runner.

**ctrlmenu.runner.selbackground**

> The background color of selected entries in the runner.

**ctrlmenu.runner.selforeground**

> The color of the label text of selected entries in the runner.

**ctrlmenu.runner.altforeground**

> The color of the descriptive text of non-selected entries in the runner.

**ctrlmenu.runner.altselforeground**

> The color of the descriptive text of selected entries in the runner.

**ctrlmenu.menu.keysym**

> The keysym to show the alt characters on the menu.

**ctrlmenu.menu.dockapp**

> If set to
> "True",
> enable the docked menu.
> This is equivalent to the
> **-d**
> command-line option.

**ctrlmenu.menu.context**

> If set to
> "True",
> enable the context menu.
> This is equivalent to the
> **-c**
> command-line option.

**ctrlmenu.menu.tornoff**

> If set to
> "True",
> enable tornoff menus.
> This is equivalent to the
> **-t**
> command-line option.

**ctrlmenu.menu.background**

> The background color of non-selected entries in the menus.

**ctrlmenu.menu.foreground**

> The color of the label text of non-selected entries in the menus.

**ctrlmenu.menu.selbackground**

> The background color of selected entries in the menus.

**ctrlmenu.menu.selforeground**

> The color of the label text of selected entries in the menus.

**ctrlmenu.menu.topShadow**

> The color of the top shadow around the menu

**ctrlmenu.menu.bottomShadow**

> The color of the bottom shadow around the menu.

**ctrlmenu.menu.shadowThickness**

> Thickness of the 3D shadow effect.

**ctrlmenu.menu.alignment**

> If set to
> **left**,
> **center**,
> or
> **right**,
> text is aligned to the left, center, or right of the menu, respectively.
> By default, text is aligned to the left.

# ENVIRONMENT

The following environment variables affect the execution of
**ctrlmenu**.

DISPLAY

> The display to start
> **ctrlmenu**
> on.

# EXAMPLE

Consider the following input:

	_Apps {
		Open _Terminal [A-T]               -- exec xterm
		Take _Screenshot [A-S]             -- exec scrot
		File _Browser                      -- exec thunar
		--
		_Seamonkey                         -- exec seamonkey
		_Firefox                           -- exec firefox
		_Chrome                            -- exec chrome
		_HexChat                           -- exec hexchat
		--
		_Games {
			_NetHack                   -- exec nethack-3.6.6
			_Spider                    -- exec spider
			Super Tux _Kart            -- exec supertuxkart
			TES III Morro_wind         -- exec openmw
		}
	}
	_Window {
		Stic_k Container [A-S-S]           -- exec shodc state -y
		S_hade Container [A-S-N]           -- exec shodc state -s
		Minimi_ze Container [A-S-Z]        -- exec shodc state -m
		_Maximize Container [A-S-W]        -- exec shodc state -M
		Container _FullScreen [A-S-F]      -- exec shodc state -f
		Container _Below [A-S-B]           -- exec shodc state -b
		Container _Above [A-S-A]           -- exec shodc state -a
		--
		_Go To Desktop {
			Go To Desktop _1 [A-1]     -- exec shodc goto 1
			Go To Desktop _2 [A-2]     -- exec shodc goto 2
			Go To Desktop _3 [A-3]     -- exec shodc goto 3
			Go To Desktop _4 [A-4]     -- exec shodc goto 4
			Go To Desktop _5 [A-5]     -- exec shodc goto 5
		}
		Move Container _To {
			Move to Desktop _1 [A-S-1] -- exec shodc sendto 1
			Move to Desktop _2 [A-S-2] -- exec shodc sendto 2
			Move to Desktop _3 [A-S-3] -- exec shodc sendto 3
			Move to Desktop _4 [A-S-4] -- exec shodc sendto 4
			Move to Desktop _5 [A-S-5] -- exec shodc sendto 5
		}
		--
		_Close Window [A-S-Q]              -- exec shodc close
	}

This input creates a dockapp window with two buttons:
"Apps"
and
"Window".
The underline on the names of those entries in the input file indicates that
pressing the alt key (set with the
**-a**
command-line option)
underlines the letter
"A"
in
"Apps"
and the letter
"W"
in
"Window"
buttons, so pressing the key
**A**
or
**W**
opens the menus for the apps or window controlling.
Each window can also be opened by clicking on those buttons.

On the
"Apps"
menu, an entry called
"Open Terminal"
can be clicked to run
**exec xterm**
on a terminal.
This entry can also be selected by the following methods:

*	Pressing
	**Alt-T**
	(the accelerator keychord).
*	Pressing
	**T**
	(the alt key)
	when the menu is open.
*	Typing
	"Open Terminal"
	(or any substring)
	after pressing
	**Alt-space**
	(or any other keychord set with the
	**-r**
	command-line option)
	to open the runner.

# SEE ALSO

shod(1)

# BUGS

**ctrlmenu**
is created to work with the
shod(1)
window manager.
Although it probably works on
fvwm(1),
support for other window managers is not planned.

OpenBSD 7.2 - September 23, 2022
