/**
 * @namespace   biewlib
 * @file        biewlib/sysdep/generic/linux/vio.c
 * @brief       general implementation of video i/o functions for linux.
 * @version     -
 * @remark      this source file is part of Binary vIEW project (BIEW).
 *              The Binary vIEW (BIEW) is copyright (C) 1995 Nickols_K.
 *              All rights reserved. This software is redistributable under the
 *              licence given in the file "Licence.en" ("Licence.ru" in russian
 *              translation) distributed in the BIEW archive.
 * @note        Requires POSIX compatible development system
 *
 * @author      Konstantin Boldyshev
 * @since       1999
 * @note        Development, fixes and improvements
 * @author      Alexander Krisak and Andrew Golovnia
 * @date        24.07.2003
 * @note        Russian locales support: KOI-8, CP866, CP1251, ISO8859-5.
 *              Tested at ASPLinux 7.3 and ASPLinux 9
 **/

/*
    Copyright (C) 1999-2002 Konstantin Boldyshev <konst@linuxassembly.org>

    $Id: vio.c,v 1.18 2009/09/03 16:57:40 nickols_k Exp $
*/

#ifndef lint
static const char rcs_id[] = "$Id: vio.c,v 1.18 2009/09/03 16:57:40 nickols_k Exp $";
#endif

#define _XOPEN_SOURCE 500

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#define __USE_BSD
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#if defined(__linux__)
#include <sys/vt.h>
#define HAVE_SYS_VT_H 1
#endif
#include "biewlib/biewlib.h"
#include "console.h"

#if (__GLIBC__ >= 2) && (__GLIBC_MINOR__ >= 1)
#define	PWRITE pwrite
#else
#define PWRITE(handle, buf, len, offset) lseek(handle, offset, SEEK_SET); write(handle, buf, len);
#endif

#define VIO_FILE "/dev/vcsa0"

#define VMAX_X __TVIO_MAXSCREENWIDTH
#define VMAX_Y 0x400

#define VTMP_LEN 0x80

tAbsCoord tvioWidth, tvioHeight;
unsigned tvioNumColors = 16;

int console_flags = 0, on_console = 0, output_7 = 0, transparent = 0;

static unsigned char __ansi_color[8] = {0,4,2,6,1,5,3,7};
static tAbsCoord saveX, saveY, firstX, firstY;
static int cursor_type = __TVIO_CUR_NORM;
static int viohandle, out_fd, initialized;

static unsigned violen;
static unsigned char *viomem;

static unsigned char frames_dumb[0x30] =
": %|{+++++|+++++`++}-++++++++-+++++++++++++#%[]~";

static char *screen_cp;
static unsigned is_unicode=0;

static void *nls_handle;

#ifdef HAVE_NCURSES
#include <curses.h>
#include <locale.h>

/* ncurses owns both rasterization (here) and input decoding
   (keyboard.c) on non-console terminals.  Init failure is fatal:
   this build has no direct-ANSI fallback. */
SCREEN *biew_scr = NULL;

/* CP437/DOS pseudographics 0xb0-0xdf (_PSMIN.._PSMAX) -> UTF-8 */
static const char *nc_ps437[0x30] = {
    "\xe2\x96\x91","\xe2\x96\x92","\xe2\x96\x93","\xe2\x94\x82",
    "\xe2\x94\xa4","\xe2\x95\xa1","\xe2\x95\xa2","\xe2\x95\x96",
    "\xe2\x95\x95","\xe2\x95\xa3","\xe2\x95\x91","\xe2\x95\x97",
    "\xe2\x95\x9d","\xe2\x95\x9c","\xe2\x95\x9b","\xe2\x94\x90",
    "\xe2\x94\x94","\xe2\x94\xb4","\xe2\x94\xac","\xe2\x94\x9c",
    "\xe2\x94\x80","\xe2\x94\xbc","\xe2\x95\x9e","\xe2\x95\x9f",
    "\xe2\x95\x9a","\xe2\x95\x94","\xe2\x95\xa9","\xe2\x95\xa6",
    "\xe2\x95\xa0","\xe2\x95\x90","\xe2\x95\xac","\xe2\x95\xa7",
    "\xe2\x95\xa8","\xe2\x95\xa4","\xe2\x95\xa5","\xe2\x95\x99",
    "\xe2\x95\x98","\xe2\x95\x92","\xe2\x95\x93","\xe2\x95\xab",
    "\xe2\x95\xaa","\xe2\x94\x98","\xe2\x94\x8c","\xe2\x96\x88",
    "\xe2\x96\x84","\xe2\x96\x8c","\xe2\x96\x90","\xe2\x96\x80"
};

/* VGA palette (DOS attrs -> real colors).  Blue is darker than stock
   VGA — terminals render 0xaa too brightly. */
static const struct { unsigned char r, g, b; } nc_vga[16] = {
    {0x00,0x00,0x00},{0xaa,0x00,0x00},{0x00,0xaa,0x00},{0xaa,0x55,0x00},
    {0x00,0x00,0x87},{0xaa,0x00,0xaa},{0x00,0xaa,0xaa},{0xaa,0xaa,0xaa},
    {0x55,0x55,0x55},{0x55,0x55,0xff},{0x55,0xff,0x55},{0x55,0xff,0xff},
    {0xff,0x55,0x55},{0xff,0x55,0xff},{0xff,0xff,0x55},{0xff,0xff,0xff}
};

/* resolved curses color index for each nc_vga palette entry */
static int _ccolor[16];
/* when COLOR_PAIRS can't hold 16x16 combos, backgrounds quantize to 8 */
static int full_pairs;

static int __NEAR__ __FASTCALL__ vio_pair(int fg, int bg)
{
    /* pair numbers: bg 0..15 -> 1 + fg + 16*(bg+1); bg -1 (transparent)
       -> 1 + fg + 16*17 = 273 + fg... laid out as 17 blocks of 16:
       index = 1 + fg + 16 * (bg + 1), bg in -1..15 -> pair 1..272 */
    if (!full_pairs && bg > 7) bg &= 7;
    return 1 + fg + ((bg + 1) << 4);
}

/* ACS pseudographics for non-UTF-8 terminals */
static chtype __FASTCALL__ _2ps(unsigned char c)
{
#define	ret(x) return(x)
    switch (c) {
	case BAR0:	ret(ACS_CKBOARD);
	case BAR1:	ret(ACS_CKBOARD);
	case BAR2:	ret(ACS_CKBOARD);
	case VSLINE:	ret(ACS_VLINE);
	case RSSTEE:	ret(ACS_RTEE);
	case RSDTEE:	ret(ACS_RTEE);
	case RDSTEE:	ret(ACS_RTEE);
        case TRSDCR:	ret(ACS_URCORNER);
        case TRDSCR:	ret(ACS_URCORNER);
        case RDDTEE:	ret(ACS_RTEE);
        case VDLINE:	ret(ACS_VLINE);
        case TRDDCR:	ret(ACS_URCORNER);
        case LRDDCR:	ret(ACS_LRCORNER);
        case LRSDCR:	ret(ACS_LRCORNER);
        case LRDSCR:	ret(ACS_LRCORNER);
        case TRSSCR:	ret(ACS_URCORNER);
        case LLSSCR:	ret(ACS_LLCORNER);
        case BSSTEE:	ret(ACS_BTEE);
        case TSSTEE:	ret(ACS_TTEE);
        case LSSTEE:	ret(ACS_LTEE);
        case HSLINE:	ret(ACS_HLINE);
        case SSPLUS:	ret(ACS_PLUS);
        case LSDTEE:	ret(ACS_LTEE);
        case LDSTEE:	ret(ACS_LTEE);
        case LLDDCR:	ret(ACS_LLCORNER);
        case TLDDCR:	ret(ACS_ULCORNER);
        case BDDTEE:	ret(ACS_BTEE);
        case TDDTEE:	ret(ACS_TTEE);
        case LDDTEE:	ret(ACS_LTEE);
        case HDLINE:	ret(ACS_HLINE);
        case DDPLUS:	ret(ACS_PLUS);
        case BDSTEE:	ret(ACS_BTEE);
        case BSDTEE:	ret(ACS_BTEE);
        case TDSTEE:	ret(ACS_TTEE);
        case TSDTEE:	ret(ACS_TTEE);
        case BLDSCR:	ret(ACS_LLCORNER);
        case BLSDCR:	ret(ACS_LLCORNER);
        case TLSDCR:	ret(ACS_ULCORNER);
        case TLDSCR:	ret(ACS_ULCORNER);
        case SDPLUS:	ret(ACS_PLUS);
        case DSPLUS:	ret(ACS_PLUS);
        case BRSSCR:	ret(ACS_LRCORNER);
        case TLSSCR:	ret(ACS_ULCORNER);
        case DBLK:	ret(ACS_BLOCK);
        case BBLK:	ret(ACS_BLOCK);
        case LBLK:	ret(ACS_BLOCK);
        case RBLK:	ret(ACS_BLOCK);
        case TBLK:	ret(ACS_BLOCK);
    }
    ret(0);
#undef ret
}

/* nearest index in the xterm 256 palette (basic 16 + 6x6x6 cube + gray
   ramp) for terminals that can't redefine colors */
static int __NEAR__ __FASTCALL__ vio_nearest256(int r, int g, int b)
{
    static const unsigned char base[16][3] = {
	{0,0,0},{170,0,0},{0,170,0},{170,85,0},
	{0,0,170},{170,0,170},{0,170,170},{170,170,170},
	{85,85,85},{85,85,255},{85,255,85},{85,255,255},
	{255,85,85},{255,85,255},{255,255,85},{255,255,255}
    };
    static const unsigned char cube[6] = {0,95,135,175,215,255};
    int best = -1; long bd = 0x7fffffff;
    int i, j, k;
    for (i = 0; i < 16; i++) {
	long d = (long)(base[i][0]-r)*(base[i][0]-r)
	       + (long)(base[i][1]-g)*(base[i][1]-g)
	       + (long)(base[i][2]-b)*(base[i][2]-b);
	if (d < bd) { bd = d; best = i; }
    }
    for (i = 0; i < 6; i++) for (j = 0; j < 6; j++) for (k = 0; k < 6; k++) {
	long d = (long)(cube[i]-r)*(cube[i]-r)
	       + (long)(cube[j]-g)*(cube[j]-g)
	       + (long)(cube[k]-b)*(cube[k]-b);
	if (d < bd) { bd = d; best = 16 + i*36 + j*6 + k; }
    }
    for (i = 0; i < 24; i++) {
	int gv = 8 + i*10;
	long d = (long)(gv-r)*(gv-r) + (long)(gv-g)*(gv-g) + (long)(gv-b)*(gv-b);
	if (d < bd) { bd = d; best = 232 + i; }
    }
    return best;
}
#endif


#define _addr(x, y)	(viomem + (x) + (y) * tvioWidth)
#define _bg(x)		((x) >> 4)
#define _fg(x)		((x) & 0x0f)
#define _2color(x)	__Xlat__(__ansi_color, (x) & 7)
/* #define _2color(x) (char)__ansi_color[((x) & 7)] */

static struct {
    unsigned char last;
    unsigned char color[0x10];
} tp = { 0, {0} };

#ifdef HAVE_NCURSES
static int __NEAR__ __FASTCALL__ is_transp(int bg)
{
    int i;
    for (i = 0; i < tp.last; i++) if (bg == tp.color[i]) return 1;
    return 0;
}
#endif

inline static int __FASTCALL__ printable(unsigned char c)
{
    int result;
    if(is_unicode) result = !(c < 0x20 || c == 0x7f);
    else result = !(c < 0x20 || c == 0x7f || c == 0x9b); /* 0x80< c < 0xA0 */

    if (result && terminal->type == TERM_XTERM && !is_unicode)
	result = !(
		c == 0x84 || c == 0x85 || c == 0x88 ||
		(c >= 0x8D && c <= 0x90) ||
		(c >= 0x96 && c <= 0x98) || 
		(c >= 0x9A && c <= 0x9F) );

    return result;
}

int __FASTCALL__ __vioGetCursorType( void )
{
    return cursor_type;
}

void __FASTCALL__ __vioSetCursorType(int type)
{
    cursor_type = type;
#ifdef HAVE_NCURSES
    if (biew_scr) {
	/* ncurses: 0 hidden, 1 normal, 2 very-visible — close enough
	   to OFF/NORM/SOLID */
	curs_set(type == __TVIO_CUR_OFF ? 0 :
		 type == __TVIO_CUR_SOLID ? 2 : 1);
    }
    /* on_console has no cursor-shape control via vcsa: the kernel
       cursor stays at its default */
#endif
}

void __FASTCALL__ __vioGetCursorPos(tAbsCoord *x, tAbsCoord *y)
{
    *x = saveX;
    *y = saveY;
}

void __FASTCALL__ __vioSetCursorPos(tAbsCoord x, tAbsCoord y)
{
    saveX = x;
    saveY = y;
#ifdef HAVE_NCURSES
    if (biew_scr) {
	if (cursor_type != __TVIO_CUR_OFF) { move(y, x); refresh(); }
    } else
#endif
    if (on_console) {
	/* vcsa[0..3] = rows,cols,cursor x,cursor y: move the kernel
	   cursor through the console API, not an escape sequence */
	unsigned char pos[2] = { (unsigned char)x, (unsigned char)y };
	PWRITE(viohandle, pos, 2, 2);
    }
}

void __FASTCALL__ __vioReadBuff(tAbsCoord x, tAbsCoord y, tvioBuff *buff, unsigned len)
{
    unsigned char *addr = _addr(x, y);

    memcpy(buff->attrs, addr, len);
    memcpy(buff->chars, addr + violen, len);
    memcpy(buff->oem_pg, addr + (violen << 1), len);
}

void __FASTCALL__ __vioWriteBuff(tAbsCoord x, tAbsCoord y, const tvioBuff *buff, unsigned len)
{
#define	LEN(x) (x << 4)
    unsigned char *addr;
#ifdef HAVE_NCURSES
    unsigned i;
#endif

/*    if (!len) return; */

    addr = _addr(x, y);

    memcpy(addr, buff->attrs, len);
    memcpy(addr + violen, buff->chars, len);
    memcpy(addr + (violen << 1), buff->oem_pg, len);

#ifdef HAVE_NCURSES
    if (biew_scr) {
	for (i = 0; i < len; i++) {
	    unsigned linear = x + i;
	    unsigned cy = y + linear / tvioWidth;
	    unsigned cx = linear % tvioWidth;
	    unsigned char c  = buff->chars[i];
	    unsigned char ca = buff->attrs[i];
	    unsigned char cp = buff->oem_pg[i];
	    int fg = _2color(ca & 7) | ((ca & 8) ? 8 : 0);
	    int bg = _2color(_bg(ca) & 7) | ((ca & 0x80) ? 8 : 0);
	    short pr = (short)vio_pair(fg, is_transp(_bg(ca)) ? -1 : bg);

	    wmove(stdscr, (int)cy, (int)cx);
	    wattr_set(stdscr, A_NORMAL, pr, NULL);
	    if (COLORS < 16) {
		if (fg > 7) wattron(stdscr, A_BOLD);
		if (bg > 7) wattron(stdscr, A_BLINK);
	    }
	    if (c && printable(c)) {
		if (cp >= _PSMIN && cp <= _PSMAX) {
		    if (output_7) waddch(stdscr, frames_dumb[cp - _PSMIN]);
		    else if (is_unicode) waddstr(stdscr, nc_ps437[cp - _PSMIN]);
		    else waddch(stdscr, _2ps(cp));
		} else if (is_unicode) {
		    unsigned l = 1;
		    char *destb = nls_recode2screen_cp(nls_handle, &c, &l);
		    if (destb) { waddstr(stdscr, destb); free(destb); }
		    else waddch(stdscr, c);
		} else waddch(stdscr, c);
	    } else if (c) waddch(stdscr, '.');
	    else waddch(stdscr, ' ');
	}
	move(saveY, saveX);
	refresh();
    } else
#endif
    if (on_console) {
	unsigned char cache_pb[LEN(VMAX_X)];
	unsigned char *pb = len > VMAX_X ? malloc(LEN(len)) : cache_pb;

	if (pb == NULL) {
	    printm("Memory allocation failed: %s\nExiting..", strerror(errno));
	    exit(errno);
	}
        __INTERLEAVE_BUFFERS(len, pb, buff->chars, buff->attrs);

	PWRITE(viohandle, pb, len << 1, 4 + ((x + y * tvioWidth) << 1));
	if (pb != cache_pb) free(pb);
    }
#undef	LEN
}

void __FASTCALL__ __init_vio(const char *user_cp,unsigned long flags)
{
#ifdef HAVE_SYS_VT_H
    struct vt_mode vtmode;
#endif
    char *tty = ttyname(STDOUT_FILENO);

    console_flags = flags;

    out_fd = open(tty, O_WRONLY);
    if (out_fd < 0) out_fd = STDOUT_FILENO;

#ifdef HAVE_SYS_VT_H
    if (TESTFLAG(console_flags, __TVIO_FLG_DIRECT_CONSOLE_ACCESS))
	on_console = ioctl(out_fd, VT_GETMODE, &vtmode) >= 0;
#endif

    if (on_console) {
	char vcsa_name[0x10];
	int len = strlen(tty);
	int i = 0;

	for (i = 0; i < len; i ++) if (isdigit((int)*(tty + i))) break;

	if (i < len) sprintf(vcsa_name, "/dev/vcsa%d", atoi(tty + i));

	if ((viohandle = open(vcsa_name, O_RDWR)) < 0) {
	    sprintf(vcsa_name, "/dev/vcsa");
	    if ((viohandle = open(vcsa_name, O_RDWR)) < 0) {
		printm("Can't open %s: %s\nDirect console access disabled..\n", vcsa_name, strerror(errno));
		on_console = 0;
	    }
	}
    }

    if (on_console) {
	unsigned char b[4];
	read(viohandle, &b, 4);
	tvioHeight = b[0]; tvioWidth = b[1];
	firstX = b[2]; firstY = b[3];
    } else {
	struct winsize w;
	ioctl(out_fd, TIOCGWINSZ, &w);
	tvioWidth = w.ws_col;
	tvioHeight = w.ws_row;
#ifdef HAVE_NCURSES
	{
	    /* Konsole advertises TERM=xterm-256color but sends its own
	       SS3-modified F-key encoding (ESC O <mod> P/Q/R/S for F1-F4),
	       which the xterm terminfo does not contain, so modified F1-F4
	       arrive undecoded.  The konsole-256color terminfo maps them;
	       use it when running under Konsole. */
	    if ((getenv("KONSOLE_VERSION") || getenv("KONSOLE_DBUS_SERVICE")) &&
		getenv("TERM") && strcmp(getenv("TERM"), "xterm-256color") == 0)
		setenv("TERM", "konsole-256color", 1);
	    setlocale(LC_ALL, "");
	    biew_scr = newterm(NULL, stdout, stdin);
	    if (!biew_scr) {
		printm("Terminal initialization failed: this build requires "
		       "ncurses (TERM='%s').\nExiting..\n",
		       getenv("TERM") ? getenv("TERM") : "");
		exit(EXIT_FAILURE);
	    }
	    set_term(biew_scr);
	    if (has_colors()) {
		int i, j;
		start_color();
		use_default_colors();
		/* exact VGA palette where the terminal can redefine
		   colors; else nearest 256-color index */
		if (can_change_color() && COLORS >= 32) {
		    for (i = 0; i < 16; i++) {
			init_extended_color(16 + i,
			    nc_vga[i].r * 1000 / 255,
			    nc_vga[i].g * 1000 / 255,
			    nc_vga[i].b * 1000 / 255);
			_ccolor[i] = 16 + i;
		    }
		} else if (COLORS >= 256) {
		    for (i = 0; i < 16; i++)
			_ccolor[i] = vio_nearest256(nc_vga[i].r, nc_vga[i].g,
						    nc_vga[i].b);
		} else {
		    for (i = 0; i < 16; i++)
			_ccolor[i] = i & (COLORS - 1) & 7;
		}
		full_pairs = (COLOR_PAIRS >= 273);
		for (j = -1; j < 16; j++) {
		    int cb = (j < 0) ? -1 : _ccolor[j & 15];
		    if (!full_pairs && j > 7) continue;
		    for (i = 0; i < 16; i++)
			init_extended_pair(1 + i + ((j + 1) << 4),
					   _ccolor[i], cb);
		}
	    } else tvioNumColors = 2;
	    tvioWidth = COLS;
	    tvioHeight = LINES;
	}
#endif
    }
#ifdef HAVE_ICONV
    screen_cp=nls_get_screen_cp();
    if(strncasecmp(screen_cp,"UTF",3)==0 && !on_console) {
	is_unicode=1;
    }
#endif
    nls_handle=nls_init(screen_cp,user_cp);
    if(nls_handle==NULL) is_unicode=0;
    if (!output_7) output_7 = TESTFLAG(console_flags, __TVIO_FLG_USE_7BIT);
    if (tvioWidth <= 0) tvioWidth = 80;
    if (tvioHeight <= 0) tvioHeight = 25;
    if (tvioWidth > VMAX_X) tvioWidth = VMAX_X;
    if (tvioHeight > VMAX_Y) tvioHeight = VMAX_Y;
    saveX = firstX;
    saveY = firstY;
    violen = tvioWidth * tvioHeight;

    viomem = malloc((violen << 1) + violen);
    if (viomem == NULL) {
	printm("Can't allocate memory for output: %s\nExiting..\n", strerror(errno));
	exit(errno);
    }
    memset(viomem, 0, (violen << 1) + violen);

    do_nls = 0;		/* disable nls convertion */

    if (on_console) {
	unsigned char *buf = malloc(violen << 1);

	if (buf != NULL) {
	    unsigned i;

	    read(viohandle, buf, violen << 1);
	    for (i = 0; i < (violen << 1); i += 2) {
    		viomem[violen + (i >> 1)] = buf[i];
		viomem[i >> 1] = buf[i + 1];
	    }
	    free(buf);
	}
    }
#ifdef HAVE_NCURSES
    else if (biew_scr) {
	do_nls = 1;
	{ char* lang;
	    if (((lang = getenv("LANG")) != NULL) &&
	        (strncmp(lang, "ru_", 3) == 0) &&
	        (strcasecmp(lang, "ru_RU.utf8") != 0) &&
	        (strcasecmp(lang, "ru_UA.utf8") != 0))
	      do_nls = 1;
	}
    }
#endif
    initialized = 1;
}

void __FASTCALL__ __vioResize(unsigned w, unsigned h)
{
    unsigned char *nm;

    if (!initialized) return;
    if (w > VMAX_X) w = VMAX_X;
    if (h > VMAX_Y) h = VMAX_Y;
    if (!w) w = tvioWidth;
    if (!h) h = tvioHeight;
    if (w == (unsigned)tvioWidth && h == (unsigned)tvioHeight) return;

    nm = realloc(viomem, (w * h) * 3);
    if (!nm) return;
    viomem = nm;
    tvioWidth = w; tvioHeight = h;
    violen = w * h;
    memset(viomem, 0, violen * 3);
#ifdef HAVE_NCURSES
    if (biew_scr) resizeterm((int)h, (int)w);
#endif
}

void __FASTCALL__ __term_vio(void)
{
    if (!initialized) return;

    __vioSetCursorType(__TVIO_CUR_NORM);
#ifdef HAVE_NCURSES
    if (biew_scr) { endwin(); delscreen(biew_scr); biew_scr = NULL; }
    else
#endif
	__vioSetCursorPos(firstX, firstY);

    nls_term(nls_handle);
    if (on_console) close(viohandle);

    free(viomem);

    close(out_fd);
    initialized = 0;
}

void __FASTCALL__ __vioSetTransparentColor(unsigned char value)
{
    if (value == 0xff) {
	int i = 0;
	for (i = 0; i < 0x10; i++) tp.color[i] = 0xff;
	tp.last = 0;
    } else if (tp.last < 0x10) {
	tp.color[tp.last] = value;
	tp.last++;
    }
}
