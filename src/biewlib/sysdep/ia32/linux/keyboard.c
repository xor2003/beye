/**
 * @namespace   biewlib
 * @file        biewlib/sysdep/ia32/linux/keyboard.c
 * @brief       Linux direct console / vt100 keyboard library
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
**/

/*
    Copyright (C) 1999-2001 Konstantin Boldyshev <konst@linuxassembly.org>

    $Id: keyboard.c,v 1.14 2009/09/24 09:12:13 nickols_k Exp $
*/

#ifndef lint
static const char rcs_id[] = "$Id: keyboard.c,v 1.14 2009/09/24 09:12:13 nickols_k Exp $";
#endif

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/kd.h>
#include <sys/vt.h>

#include "biewlib/kbd_code.h"
#include "biewlib/biewlib.h"
#include "console.h"

#ifdef HAVE_MOUSE
#include <gpm.h>
static int gpmhandle;
#endif

#ifdef HAVE_NCURSES
#include <curses.h>
extern SCREEN *biew_scr;	/* created by __init_vio (non-console) */
extern void __FASTCALL__ BiewScreenResize(void) __attribute__((weak));	/* biew.c: re-layout top-level windows; weak so -lbiew-only tools still link */
#endif

/* true when a terminal library (ncurses) owns input decoding on this
   terminal; in that case SIGIO does not pump and waiters like
   __inputRawInfo must drive ReadNextEvent themselves */
int __NEAR__ termlib_input(void)
{
#ifdef HAVE_NCURSES
    if (biew_scr) return 1;
#endif
    return 0;
}

#define KBUFSIZE	64	/**< size of keyboard buffer */
#define KSCANSIZE	84	/**< size of scancode table */
#define KEYNUM		128	/* */

#define KSCAN_SHIFTL	0x2a
#define KSCAN_SHIFTR	0x36
#define KSCAN_CTRL	0x1d
#define KSCAN_ALT	0x38
#define KSCAN_CAPS	0x3a
#define KSCAN_NUM	0x45
#define KSCAN_SCROLL	0x46
#define KE_NONE		0xffffffff

static volatile int shift_status = 0,	/**< status of shift keys */
		    console_restart = 0,
		    keypressed[KEYNUM];	/**< indicates whether key is down */
static tBool mouse_status = True;
static struct termios sattr, tattr;	/**< terminal attributes */
int in_fd;

static void *nls_handle;
static int is_unicode=0;

/**
    keyboard FIFO
*/

static struct {
    int pool[KBUFSIZE];
    int current;
} keybuf;

char rawkb_buf[KBUFSIZE];
unsigned rawkb_len; /* size of rawkb_buf*/
unsigned rawkb_mode=0;
int rawkb_escape;
/**
    scancode tables
*/

static unsigned int scancode_table[KSCANSIZE] =
{
    KE_ESCAPE,	'1',	'2',	'3',	'4',	'5',	'6',	'7',
    '8',	'9',	'0',	'-',	'=',	KE_BKSPACE,KE_TAB,'q',
    'w',	'e',	'r',	't',	'y',	'u',	'i',	'o',
    'p',	'[',	']',	KE_ENTER,KE_NONE,'a',	's',	'd',
    'f',	'g',	'h',	'j',	'k',	'l',	';',	0x27,
    '`',	KE_NONE,0x5c,	'z',	'x',	'c',	'v',	'b',
    'n',	'm',	',',	'.',	'/',	KE_NONE,'*',	KE_NONE,
    ' ',	KE_NONE,KE_F(1),KE_F(2),KE_F(3),KE_F(4),KE_F(5),KE_F(6),
    KE_F(7),	KE_F(8),KE_F(9),KE_F(10),KE_NONE,KE_NONE,KE_HOME,KE_UPARROW,
    KE_PGUP,	'-',	KE_LEFTARROW,0,KE_RIGHTARROW,'+',KE_END,KE_DOWNARROW,
    KE_PGDN,	KE_INS,	KE_DEL,KE_NONE
};

unsigned int scancode_caps_table[KSCANSIZE] =
{
    KE_ESCAPE,	'!',	'@',	'#',	'$',	'%',	'^',	'&',
    '*',	'(',	')',	'_',	'+',	KE_BKSPACE,KE_TAB,'Q',
    'W',	'E',	'R',	'T',	'Y',	'U',	'I',	'O',
    'P',	'{',	'}',	KE_ENTER,0,	'A',	'S',	'D',
    'F',	'G',	'H',	'J',	'K',	'L',	':',	0x22,
    '~',	KE_NONE,'|',	'Z',	'X',	'C',	'V',	'B',
    'N',	'M',	'<',	'>',	'?',	KE_NONE,'*',	KE_NONE,
    ' ',	0,	KE_F(1),KE_F(2),KE_F(3),KE_F(4),KE_F(5),KE_F(6),
    KE_F(7),	KE_F(8),	KE_F(9),	KE_F(10),	KE_NONE,	KE_NONE,	KE_HOME,KE_UPARROW,
    KE_PGUP,	'-',	KE_LEFTARROW,0,KE_RIGHTARROW,'+',KE_END,KE_DOWNARROW,
    KE_PGDN,	KE_INS,	KE_DEL,	KE_NONE
};


static mevent mouse = {0, 0, 0, 0};
/* a decoded mouse event means getch() may already have pulled follow-up
   bytes into ncurses' fifo — keep pumping even while the kernel fd is idle */
static int mouse_dirty;

/*

*/

static void __FASTCALL__ pushEvent(unsigned _event)
{
    unsigned event=_event;
#if defined (_VT100_) && defined(HAVE_ICONV)
    if(is_unicode) {
	static unsigned char utf_buff[8];
	static unsigned utf_ptr=0;
	char *destb;
	int err;
	unsigned char *eptr;
	unsigned i,len;
	if((event&(~0xFF))==0) {
	    utf_buff[utf_ptr]=event;
	    len=utf_ptr+1;
	    if((err=nls_test(nls_handle,utf_buff,&len))!=0) {
		utf_ptr++;
		/* on incompelete input we need to raise
		   new loop of console reading */
		raise(SIGIO);
		return;
	    }
	    len=utf_ptr+1;
	    destb=nls_recode2screen_cp(nls_handle,utf_buff,&len);
	    event=0;
	    eptr=(unsigned char *)&event;
	    for(i=0;i<min(sizeof(unsigned),len);i++) {
		event<<=8;
		eptr[0]=destb[i];
	    }
	    utf_ptr=0;
	    free(destb);
	}
    }
#endif
    if (event && keybuf.current < KBUFSIZE) {
	if (keybuf.current)
	    memmove(&keybuf.pool[1], keybuf.pool,
		    keybuf.current * sizeof(keybuf.pool[0]));
	keybuf.pool[0] = event;
	keybuf.current++;
    }
}

static void __FASTCALL__ console_leave(void)
{
    ioctl(in_fd, TCSETSW, &sattr);
    ioctl(in_fd, KDSKBMODE, K_XLATE);
    ioctl(in_fd, VT_RELDISP, VT_ACKACQ);
    signal(SIGUSR1, (void *) console_leave);
}

static void __FASTCALL__ console_enter(void)
{
    ioctl(in_fd, KDSKBMODE, K_RAW);
    ioctl(in_fd, TCSETSW, &tattr);
    memset((void *)&keypressed, 0, KEYNUM * sizeof(int));
    console_restart = 1;
    signal(SIGUSR2, (void *) console_enter);
}

/**
    ReadNextEvent is non-blocking
*/

/* terminfo maps modified F-keys to F13+ (ncurses convention):
   +12 shift, +24 ctrl, +36 ctrl+shift, +48 alt.
   n is the raw F-number (1..60); returns KE_F(base)|ADD_* or 0. */
unsigned __FASTCALL__ __kbdModFKey(unsigned n)
{
    unsigned f, grp, k;
    if (!n || n > 60) return 0;
    n--;
    f = (n % 12) + 1;
    grp = n / 12;
    k = KE_F(f);
    if (grp & 1) k |= ADD_SHIFT;
    if (grp & 2) k |= ADD_CONTROL;
    if (grp & 4) k |= ADD_ALT;
    return k;
}

/* set by the kitty keyboard probe (HAVE_NCURSES only); referenced by the
   generic shift-tracking code, so it must exist in every build */
static int kitty_mode = 0;

#ifdef HAVE_NCURSES
static int __NEAR__ map_curses_key(int ch)
{
    if (ch == '\r' || ch == '\n') return KE_ENTER;
    if (ch >= 0 && ch <= 255) return ch;	/* raw byte (incl. utf-8 parts) */
    if (ch >= KEY_F(1) && ch <= KEY_F(63))
    {
	int n = ch - KEY_F(0);
	if (n <= 12) return KE_F(n);
	if (n <= 24) return KE_SHIFT_F(n - 12);
	if (n <= 36) return KE_CTL_F(n - 24);
	if (n <= 48) return KE_SHIFT_F(n - 36) | ADD_CONTROL;
	if (n <= 60) return KE_ALT_F(n - 48);
	return KE_ALT_F(n - 60) | ADD_SHIFT;
    }
    switch (ch)
    {
	case KEY_ENTER:		return KE_ENTER;
	case KEY_BACKSPACE:	return KE_BKSPACE;
	case KEY_LEFT:		return KE_LEFTARROW;
	case KEY_RIGHT:		return KE_RIGHTARROW;
	case KEY_UP:		return KE_UPARROW;
	case KEY_DOWN:		return KE_DOWNARROW;
	case KEY_HOME:		return KE_HOME;
	case KEY_END:		return KE_END;
	case KEY_IC:		return KE_INS;
	case KEY_DC:		return KE_DEL;
	case KEY_NPAGE:		return KE_PGDN;
	case KEY_PPAGE:		return KE_PGUP;
	case KEY_BTAB:		return KE_SHIFT_TAB;
	case KEY_SLEFT:		return KE_LEFTARROW | ADD_SHIFT;
	case KEY_SRIGHT:	return KE_RIGHTARROW | ADD_SHIFT;
	case KEY_SHOME:		return KE_HOME | ADD_SHIFT;
	case KEY_SEND:		return KE_END | ADD_SHIFT;
	case KEY_SDC:		return KE_DEL | ADD_SHIFT;
	case KEY_SIC:		return KE_INS | ADD_SHIFT;
	case KEY_SNEXT:		return KE_PGDN | ADD_SHIFT;
	case KEY_SPREVIOUS:	return KE_PGUP | ADD_SHIFT;
    }
    return 0;
}

/*
    Kitty keyboard protocol (CSI u).  Ordinary terminals have no way to
    report a bare Shift/Ctrl/Alt press, so the modifier legend can only
    switch on the Linux console.  Terminals implementing the kitty
    protocol do report modifier-only key events: we probe for support
    with "CSI ? u", push flags 15 (disambiguate | event types |
    alternate keys | report all keys) and then decode CSI u events
    ourselves — including press/release so a lone modifier press can
    emit KE_SHIFTKEYS and repaint the prompt bar, exactly like the
    console path does.
*/

static volatile int kitty_resized = 0;
static unsigned char kib[512];		/* pending raw input bytes */
static int kin = 0;

static void kitty_winch(int sig)
{
    (void)sig;
    kitty_resized = 1;
}

/* return sub-parameter si of field fi from a CSI parameter string, -1 absent */
static int kget(const unsigned char *s, int n, int fi, int si)
{
    int i = 0, f = 0;
    while (i < n)
    {
	int j = i, k;
	while (i < n && s[i] != ';') i++;
	if (f == fi)
	{
	    for (k = 0; k <= si; k++)
	    {
		int v = 0, seen = 0;
		if (k > 0)
		{
		    if (j >= i || s[j] != ':') return -1;
		    j++;
		}
		while (j < i && s[j] != ':')
		{
		    if (s[j] >= '0' && s[j] <= '9')
		    { v = v * 10 + s[j] - '0'; seen = 1; }
		    j++;
		}
		if (k == si) return seen ? v : -1;
	    }
	    return -1;
	}
	i++;
	f++;
    }
    return -1;
}

/* feed a literal unicode codepoint through pushEvent as UTF-8 bytes so
   the iconv layer recomposes the console codepage char */
static void kitty_char(unsigned cp)
{
    unsigned char u[4];
    int n, i;
    if (cp < 0x80)	{ u[0] = cp; n = 1; }
    else if (cp < 0x800)
    { u[0] = 0xC0 | (cp >> 6); u[1] = 0x80 | (cp & 0x3F); n = 2; }
    else if (cp < 0x10000)
    { u[0] = 0xE0 | (cp >> 12); u[1] = 0x80 | ((cp >> 6) & 0x3F);
      u[2] = 0x80 | (cp & 0x3F); n = 3; }
    else
    { u[0] = 0xF0 | (cp >> 18); u[1] = 0x80 | ((cp >> 12) & 0x3F);
      u[2] = 0x80 | ((cp >> 6) & 0x3F); u[3] = 0x80 | (cp & 0x3F); n = 4; }
    for (i = 0; i < n; i++) pushEvent(u[i]);
}

#define KP_MORE  (-1)	/* incomplete sequence — wait for more input */
#define KP_NONE  (-2)	/* consumed input, no event to report */
#define KP_PUSHED (-3)	/* event(s) already pushed via pushEvent */

/* sync shift_status from a kitty modifier mask (mask = mods-1) */
static void kitty_sync_mods(int mask)
{
    int ns = shift_status & ~(KS_SHIFT|KS_CTRL|KS_ALT|KS_CAPSLOCK|KS_NUMLOCK);
    if (mask & 1)   ns |= KS_SHIFT;
    if (mask & 2)   ns |= KS_ALT;
    if (mask & 4)   ns |= KS_CTRL;
    if (mask & 64)  ns |= KS_CAPSLOCK;
    if (mask & 128) ns |= KS_NUMLOCK;
    shift_status = ns;
}

static int kitty_csi(const unsigned char *s, int n, int fin)
{
    int code, shifted, mods, ev, mask, k;
    if (fin == 'M' || fin == 'm')	/* SGR mouse */
    {
	int b, x, y;
	if (n < 1 || s[0] != '<') return KP_NONE;
	b = kget(s, n, 0, 0); x = kget(s, n, 1, 0); y = kget(s, n, 2, 0);
	if (b < 0 || x < 1 || y < 1) return KP_NONE;
	mouse.x = x - 1;
	mouse.y = y - 1;
	mouse_dirty = 1;
	if (fin == 'm')
	{
	    mouse.buttons = mouse.pressed = 0;
	    return KP_NONE;
	}
	if (b & 64)	/* wheel */
	    return (b & 1) ? KE_DOWNARROW : KE_UPARROW;
	mouse.pressed = 1;
	switch (b & 3)
	{
	    case 0: mouse.buttons |= MS_LEFTPRESS; break;
	    case 1: mouse.buttons |= MS_MIDDLEPRESS; break;
	    case 2: mouse.buttons |= MS_RIGHTPRESS; break;
	    default: return KP_NONE;
	}
	return KE_MOUSE;
    }
    mods = kget(s, n, 1, 0); if (mods < 0) mods = 1;
    mask = mods - 1;
    if (fin == '~')
    {
	/* legacy CSI <num>[;mods]~ function keys (mixed-input safety) */
	static const int tk[] = {
	    KE_HOME, KE_INS, KE_DEL, KE_END, KE_PGUP, KE_PGDN };
	int num = kget(s, n, 0, 0), t;
	if (num >= 1 && num <= 6) t = tk[num - 1];
	else if (num == 7 || num == 8) t = num == 7 ? KE_HOME : KE_END;
	else if (num >= 11 && num <= 14) t = KE_F(num - 10);
	else if (num >= 15 && num <= 24)
	{
	    static const char fn[] = { 5,0,6,7,8,9,10,0,11,12 };
	    t = fn[num - 15] ? KE_F(fn[num - 15]) : 0;
	}
	else t = 0;
	if (!t) return KP_NONE;
	kitty_sync_mods(mask);
	if (mask & 1) t |= ADD_SHIFT;
	if (mask & 2) t |= ADD_ALT;
	if (mask & 4) t |= ADD_CONTROL;
	return t;
    }
    if (fin != 'u')
    {
	/* legacy CSI [1;mods]<letter> — arrows, home/end, F1-F4, btab */
	static const char fins[] = "ABCDHFPQRSZ";
	static const int kv[] = {
	    KE_UPARROW, KE_DOWNARROW, KE_RIGHTARROW, KE_LEFTARROW,
	    KE_HOME, KE_END, KE_F(1), KE_F(2), KE_F(3), KE_F(4),
	    KE_SHIFT_TAB };
	const char *p = strchr(fins, fin);
	int t;
	if (!p) return KP_NONE;
	t = kv[p - fins];
	kitty_sync_mods(mask);
	if (fin != 'Z')
	{
	    if (mask & 1) t |= ADD_SHIFT;
	    if (mask & 2) t |= ADD_ALT;
	    if (mask & 4) t |= ADD_CONTROL;
	}
	return t;
    }
    code    = kget(s, n, 0, 0);
    shifted = kget(s, n, 0, 1);
    ev      = kget(s, n, 1, 1); if (ev  < 0) ev   = 1;
    if (code < 0) return KP_NONE;

    if (code >= 57441 && code <= 57454)		/* modifier keys */
    {
	int bit = 0, ns;
	switch (code)
	{
	    case 57441: case 57447: bit = KS_SHIFT; break;
	    case 57442: case 57448: bit = KS_CTRL;  break;
	    case 57443: case 57449: bit = KS_ALT;   break;
	}
	if (!bit) return KP_NONE;		/* super/hyper/meta/iso */
	ns = ev == 3 ? shift_status & ~bit : shift_status | bit;
	if (ns != shift_status) { shift_status = ns; return KE_SHIFTKEYS; }
	return KP_NONE;
    }
    if (code >= 57358 && code <= 57360)		/* caps/scroll/num lock */
    {
	if (ev == 1)
	{
	    static const int lk[3] = { KS_CAPSLOCK, KS_SCRLOCK, KS_NUMLOCK };
	    shift_status ^= lk[code - 57358];
	    return KE_SHIFTKEYS;
	}
	return KP_NONE;
    }
    if (ev == 3) return KP_NONE;		/* key release */

    kitty_sync_mods(mask);
    k = 0;
    switch (code)
    {
	case 27:	case 57344: k = KE_ESCAPE;	break;
	case 9:		case 57346: k = KE_TAB;		break;
	case 13:	case 57345: case 57414: k = KE_ENTER; break;
	case 127:	case 57347: k = KE_BKSPACE;	break;
	case 57348: case 57425: k = KE_INS;	break;
	case 57349: case 57426: k = KE_DEL;	break;
	case 57350: case 57417: k = KE_LEFTARROW;	break;
	case 57351: case 57418: k = KE_RIGHTARROW;break;
	case 57352: case 57419: k = KE_UPARROW;	break;
	case 57353: case 57420: k = KE_DOWNARROW;	break;
	case 57354: case 57421: k = KE_PGUP;	break;
	case 57355: case 57422: k = KE_PGDN;	break;
	case 57356: case 57423: k = KE_HOME;	break;
	case 57357: case 57424: k = KE_END;	break;
	case 57427: k = (mask & 128) ? '5' : KE_HOME; break;
	default:
	    if (code >= 57364 && code <= 57375) k = KE_F(code - 57363);
	    else if (code >= 57399 && code <= 57408) k = '0' + (code - 57399);
	    else if (code == 57409) k = '.';
	    else if (code == 57410) k = '/';
	    else if (code == 57411) k = '*';
	    else if (code == 57412) k = '-';
	    else if (code == 57413) k = '+';
	    else if (code == 57415) k = '=';
	    else if (code == 57416) k = ',';
	    else if (code > 0 && code < 0xE000)
	    {
		/* literal text codepoint: unicode goes through the utf8
		   feeder (iconv recomposes the console char), ASCII
		   returns directly with any modifiers attached */
		unsigned cp = (mask & 1 && shifted > 0) ? shifted : code;
		if (cp >= 0x80)
		{
		    kitty_char(cp);
		    return KP_PUSHED;
		}
		k = cp;
		if (mask & 1) k |= ADD_SHIFT;
		if (mask & 2) k |= ADD_ALT;
		if (mask & 4) k |= ADD_CONTROL;
		return k;
	    }
	    break;
    }
    if (!k) return KP_NONE;
    if (mask & 1) k |= ADD_SHIFT;
    if (mask & 2) k |= ADD_ALT;
    if (mask & 4) k |= ADD_CONTROL;
    return k;
}

/* parse one complete event from kib; fills kib from in_fd as needed */
static int kitty_pull(void)
{
    for (;;)
    {
	int i, fin;
	if (kin == 0)
	{
	    int r = read(in_fd, kib, (int)sizeof(kib));
	    if (r <= 0) return KP_MORE;
	    kin = r;
	}
	if (kib[0] != 0x1b)
	{
	    int b = kib[0];
	    memmove(kib, kib + 1, --kin);
	    return b;
	}
	if (kin < 2) goto more;
	if (kib[1] == 'O')	/* SS3 — not expected in kitty mode */
	{
	    int k;
	    if (kin < 3) goto more;
	    k = kitty_csi(NULL, 0, kib[2]);
	    memmove(kib, kib + 3, kin - 3); kin -= 3;
	    if (k >= 0) return k;
	    continue;
	}
	if (kib[1] != '[')	/* ESC + byte → Alt fallback */
	{
	    int k;
	    if (kib[1] < 0x80) { k = kib[1] | ADD_ALT; memmove(kib, kib + 2, kin - 2); kin -= 2; return k; }
	    memmove(kib, kib + 1, --kin);
	    return KE_ESCAPE;
	}
	for (i = 2; i < kin; i++)
	    if (kib[i] >= 0x40) break;	/* first final-range byte */
	if (i == kin) goto more;
	fin = kib[i];
	{
	    int k = kitty_csi(kib + 2, i - 2, fin);
	    memmove(kib, kib + i + 1, kin - i - 1); kin -= i + 1;
	    if (k >= 0) return k;
	    if (k == KP_PUSHED) return KP_PUSHED;
	}
	continue;
more:
	{
	    int r;
	    if (kin >= (int)sizeof(kib)) { kin = 0; return KP_MORE; }
	    r = read(in_fd, kib + kin, (int)sizeof(kib) - kin);
	    if (r <= 0) return KP_MORE;
	    kin += r;
	}
    }
}

/* ask for kitty flags: supporting terminals reply CSI ? <flags> u;
   leftovers are pushed back through ungetch on failure */
static void kitty_probe(void)
{
    fd_set fds;
    struct timeval tv;
    int i, tries;
    putp("\033[?u");
    fflush(stdout);
    for (tries = 0; tries < 8; tries++)
    {
	tv.tv_sec = 0; tv.tv_usec = 40000;
	FD_ZERO(&fds); FD_SET(in_fd, &fds);
	if (select(in_fd + 1, &fds, NULL, NULL, &tv) <= 0) break;
	i = (int)read(in_fd, kib + kin, (int)sizeof(kib) - kin);
	if (i <= 0) break;
	kin += i;
	for (i = 0; i + 3 < kin; i++)
	{
	    if (kib[i] == 0x1b && kib[i+1] == '[' && kib[i+2] == '?')
	    {
		int j = i + 3;
		while (j < kin && kib[j] >= '0' && kib[j] <= '9') j++;
		if (j < kin && kib[j] == 'u')
		{
		    memmove(kib, kib + j + 1, kin - j - 1);
		    kin -= j + 1;
		    /* disambiguate|events|alternates|all-keys */
		    putp("\033[>15u");
		    fflush(stdout);
		    signal(SIGWINCH, kitty_winch);
		    kitty_mode = 1;
		    return;
		}
	    }
	}
    }
    while (kin) ungetch(kib[--kin]);	/* return probed bytes to curses */
}
#endif


void __FASTCALL__ ReadNextEvent(void)
{
#define get(x)	read(in_fd,&(x),1)
#define ret(x)	pushEvent((x)); return;

	unsigned key = 0;
	int i;

#ifdef HAVE_MOUSE
    if (gpmhandle) {
	fd_set gpmfds;
	struct timeval t = { 0, 0 };

	FD_ZERO(&gpmfds);
	FD_SET(gpmhandle, &gpmfds);

	if (select(gpmhandle + 1, &gpmfds, NULL, NULL, &t)) {
	    Gpm_Event ge;
	    Gpm_GetEvent(&ge);
	    if (ge.type & GPM_DOWN) {
		if (ge.buttons & GPM_B_LEFT)	
		    mouse.buttons |= MS_LEFTPRESS;
		if (ge.buttons & GPM_B_MIDDLE)
		    mouse.buttons |= MS_MIDDLEPRESS;
		if (ge.buttons & GPM_B_RIGHT)
		    mouse.buttons |= MS_RIGHTPRESS;
		mouse.x = ge.x - 1; 
		mouse.y = ge.y - 1;
		mouse.pressed = 1;
		if (mouse.buttons) ret(KE_MOUSE);
	    } else if (ge.type & GPM_UP) mouse.pressed = 0;
	}
    }
#endif

	/* If key is ready, place it into keyboard buffer */

        if (on_console) {
	    unsigned char c = 0;
	    int ss = shift_status;

	    do { get(c); } while (c == 0xe0);
	    if (c == 0xe1) get(c);
	    if (c == 0xC6) { break_status = True; if(rawkb_mode) { rawkb_buf[0]=c; rawkb_len=1; rawkb_mode=0; } return; } /* CTRL+BREAK check */
	    if (c) {
		if(rawkb_mode) { rawkb_buf[0]=c; rawkb_len=1; }
		keypressed[c & (KEYNUM - 1)] = c & KEYNUM ? 0 : 1;
		for (i = 1; i < KSCANSIZE; i++)
		    if (keypressed[i] &&
		    (i != KSCAN_SHIFTL) &&
		    (i != KSCAN_SHIFTR) &&
		    (i != KSCAN_CTRL) &&
		    (i != KSCAN_ALT) &&
		    (i != KSCAN_CAPS) &&
		    (i != KSCAN_NUM) &&
		    (i != KSCAN_SCROLL))
		    key = scancode_table[i - 1];
	    }
	    if (__kbdGetShiftsKey() != ss) key = KE_SHIFTKEYS;
	    if ((shift_status & KS_SHIFT) == KS_SHIFT) {
		for (i = 1; i < KSCANSIZE; i++)
		    if (keypressed[i] &&
		    (i != KSCAN_SHIFTL) &&
		    (i != KSCAN_SHIFTR) &&
		    (i != KSCAN_CTRL) &&
		    (i != KSCAN_ALT) &&
		    (i != KSCAN_CAPS) &&
		    (i != KSCAN_NUM) &&
		    (i != KSCAN_SCROLL))
		    key = scancode_caps_table[i - 1];
	    }
	    if (((shift_status & KS_ALT) == KS_ALT) &&
		((shift_status & KS_CTRL) == KS_CTRL) &&
		(key >= KE_F(1) && key <= KE_F(10))) {

		struct vt_stat vt;
		int newvt;

		if (ioctl(in_fd, VT_GETSTATE, &vt) != 0) goto c_end;
		switch (key) {
		    case KE_F(1): newvt = 1; break;
		    case KE_F(2): newvt = 2; break;
		    case KE_F(3): newvt = 3; break;
		    case KE_F(4): newvt = 4; break;
		    case KE_F(5): newvt = 5; break;
		    case KE_F(6): newvt = 6; break;
		    case KE_F(7): newvt = 7; break;
		    default: goto c_end;
		}

		if (vt.v_active != newvt) {
		    if (ioctl(in_fd, VT_ACTIVATE, newvt) != 0) goto c_end;
		    console_restart = 0;
		    while (!console_restart) __OsYield();
		}
	    }
        } else {
/*
    VT100 emulation — kitty CSI u or ncurses decodes the input stream.
*/
#ifdef HAVE_NCURSES
	    if (kitty_mode)
	    {
		int k;
		if (kitty_resized)
		{
		    struct winsize wsz;
		    kitty_resized = 0;
		    if (ioctl(in_fd, TIOCGWINSZ, &wsz) == 0 &&
			wsz.ws_col && wsz.ws_row &&
			((unsigned)wsz.ws_col != (unsigned)tvioWidth ||
			 (unsigned)wsz.ws_row != (unsigned)tvioHeight))
		    {
			__vioResize(wsz.ws_col, wsz.ws_row);
			if (BiewScreenResize) BiewScreenResize();
		    }
		}
		for (;;)
		{
		    k = kitty_pull();
		    if (k == KP_PUSHED || k == KP_MORE) return;
		    if (k >= 0) { key = k; goto place_key; }
		}
	    }
	    if (biew_scr)
	    {
		int ch;
		while ((ch = getch()) != ERR)
		{
		    if (ch == KEY_RESIZE)
		    {
			struct winsize wsz;
			if (ioctl(in_fd, TIOCGWINSZ, &wsz) == 0 &&
			    wsz.ws_col && wsz.ws_row &&
			    ((unsigned)wsz.ws_col != (unsigned)tvioWidth ||
			     (unsigned)wsz.ws_row != (unsigned)tvioHeight))
			{
			    __vioResize(wsz.ws_col, wsz.ws_row);
			    if (BiewScreenResize) BiewScreenResize();
			}
			continue;
		    }
		    if (ch == KEY_MOUSE)
		    {
			MEVENT me;
			if (getmouse(&me) == OK)
			{
			    int m = 0;
			    mouse_dirty = 1;
			    if (me.bstate & BUTTON4_PRESSED) { ret(KE_UPARROW); }
			    if (me.bstate & BUTTON5_PRESSED) { ret(KE_DOWNARROW); }
			    if (me.bstate & (BUTTON1_PRESSED|BUTTON1_CLICKED))
				{ m = 1; mouse.buttons |= MS_LEFTPRESS; }
			    if (me.bstate & (BUTTON2_PRESSED|BUTTON2_CLICKED))
				{ m = 1; mouse.buttons |= MS_MIDDLEPRESS; }
			    if (me.bstate & (BUTTON3_PRESSED|BUTTON3_CLICKED))
				{ m = 1; mouse.buttons |= MS_RIGHTPRESS; }
			    if (me.bstate & (BUTTON1_RELEASED|BUTTON2_RELEASED|BUTTON3_RELEASED))
				mouse.buttons = mouse.pressed = 0;
			    else mouse.pressed = 1;
			    mouse.x = me.x;
			    mouse.y = me.y;
			    if (m) { ret(KE_MOUSE); }
			}
			continue;
		    }
		    if (ch == '\x1b')
		    {
			/* ESC-prefix Alt (Konsole "Meta" mode): the following
			   sequence encodes the real key, so merge it.  A lone
			   ESC stays a lone ESC. */
			int nxt = getch();
			if (nxt == ERR) key = KE_ESCAPE;
			else
			{
			    key = map_curses_key(nxt);
			    if (key)
			    {
				key |= ADD_ALT;
				if(rawkb_mode) { rawkb_buf[0]=(unsigned char)nxt; rawkb_len=1; }
			    }
			    else { ungetch(nxt); key = KE_ESCAPE; }
			}
			goto place_key;
		    }
		    key = map_curses_key(ch);
		    if (!key) continue;
		    if(rawkb_mode) { rawkb_buf[0]=(unsigned char)ch; rawkb_len=1; }
		    goto place_key;
		}
		return;
	    }
#endif


	}
c_end:
place_key:
	if(!rawkb_mode) { ret(key); }
	else { rawkb_escape=(key==KE_ESCAPE&&rawkb_len==1); rawkb_mode=0; }
#undef ret
}

int __FASTCALL__ __kbdGetShiftsKey(void)
{
	/* shift_status = 6;ioctl(in_fd,TIOCLINUX,&shift_status); */
    if (on_console) {
	 shift_status = 0;
	 if (keypressed[KSCAN_SHIFTL] || keypressed[KSCAN_SHIFTR]) shift_status |= KS_SHIFT;
	 if (keypressed[KSCAN_CTRL])	shift_status |= KS_CTRL;
	 if (keypressed[KSCAN_ALT])	shift_status |= KS_ALT;
	 if (keypressed[KSCAN_CAPS])	shift_status |= KS_CAPSLOCK;
	 if (keypressed[KSCAN_NUM])	shift_status |= KS_NUMLOCK;
	 if (keypressed[KSCAN_SCROLL])	shift_status |= KS_SCRLOCK;
    }
    return shift_status;
}

int __FASTCALL__ __kbdTestKey(unsigned long flg)
{
    if(__MsGetBtns() && flg == KBD_NONSTOP_ON_MOUSE_PRESS) return KE_MOUSE;
    return keybuf.current;
}

int __FASTCALL__ __kbdGetKey (unsigned long flg)
{
    int key = 0, s = 0;

    if (__MsGetBtns() && flg == KBD_NONSTOP_ON_MOUSE_PRESS) return KE_MOUSE;

    while (!keybuf.current) {
	__OsYield();
	if (!on_console) {
	    /* non-blocking read — needed when SIGIO can't reach us
	       (e.g. Cosmopolitan builds, where O_ASYNC stays off) */
	    ReadNextEvent();
	}
#ifdef	HAVE_MOUSE
	if (gpmhandle) ReadNextEvent();
#endif
    }
    key = keybuf.pool[--keybuf.current];

    if (!(key == KE_MOUSE || key == KE_SHIFTKEYS)) {
	if ((shift_status & KS_ALT) == KS_ALT)		s |= ADD_ALT;
	if ((shift_status & KS_SHIFT) == KS_SHIFT)	s |= ADD_SHIFT;
	if ((shift_status & KS_CTRL) == KS_CTRL) {
	    s |= ADD_CONTROL;
	    if (key == 'o' || key == 'O') key = KE_CTL_(O);    /* CTRL+O */
	}
	if (!on_console && !kitty_mode) shift_status = 0;
    }

    return key | s;
}

tBool __FASTCALL__ __MsGetState(void)
{
    return mouse_status;
}

void __FASTCALL__ __MsSetState(tBool ms_visible)
{
    mouse_status = ms_visible;
}

void __FASTCALL__ __MsGetPos(tAbsCoord *x, tAbsCoord *y)
{
    *x = mouse.x;
    *y = mouse.y;
}

int __FASTCALL__ __MsGetBtns(void)
{
#ifdef	HAVE_MOUSE
    if (gpmhandle) ReadNextEvent();
#endif
    /* ncurses/kitty: the mouse state only advances when the tty input is
       drained — the __GetEvent release-wait loops poll us, so pump here.
       Gate the pump: bare getch() does cursor bookkeeping on every call,
       and the wait loops would spin it into an output flood.  Kernel-
       readable covers new input; mouse_dirty covers events already pulled
       into ncurses' fifo by an earlier pass. */
    if (!on_console && in_fd >= 0)
    {
	fd_set fds;
	struct timeval z = { 0, 0 };
	FD_ZERO(&fds); FD_SET(in_fd, &fds);
	if (mouse_dirty || select(in_fd + 1, &fds, NULL, NULL, &z) > 0)
	{
	    mouse_dirty = 0;
	    ReadNextEvent();
	}
    }
    return mouse.pressed ? mouse.buttons : 0;
}

/*

*/

static void
dummy_handler (int sig)
{
    /*fprintf(stderr, "warning: received early %d\n", sig);*/
}

void __FASTCALL__ __init_keyboard(const char *user_cp)
{
    int mode = O_NONBLOCK;

    signal(SIGIO, dummy_handler);
    in_fd = open(ttyname(STDIN_FILENO), O_RDONLY);
    if (in_fd < 0) in_fd = STDIN_FILENO;

    /* O_ASYNC without F_SETOWN signals the tty's foreground process
       group, killing any process that shares it (e.g. a parent that
       spawned us); claim the fd first so SIGIO reaches only us. */
    if (fcntl(in_fd, F_SETOWN, getpid()) == 0)
	mode |= O_ASYNC;

    if (fcntl(in_fd, F_SETFL, fcntl(in_fd, F_GETFL) | mode) < 0)
    {
        printm("Can't set %X for %d: %s\nExiting..\n",
		mode,
		in_fd,
		strerror(errno));
        exit(EXIT_FAILURE);
    }

    tcgetattr(in_fd, &tattr);
    sattr = tattr;
    tattr.c_lflag &= ~(ICANON | ECHO | ISIG);
    tattr.c_cc[VMIN] = 1;
    tattr.c_cc[VTIME] = 0;
    tattr.c_iflag &= ~(INPCK | ISTRIP | IXON | ICRNL);
    tattr.c_oflag |= OPOST | ONLCR;
    tcsetattr(in_fd, TCSANOW, &tattr);

    if (on_console && (ioctl(in_fd, KDSKBMODE, K_RAW) < 0)) {
	printm("Can't set keyboard raw mode: %s\nUsing VT100 emulation..\n", strerror(errno));
	on_console = 0;
    }

#ifdef HAVE_NCURSES
    if (!on_console && biew_scr)
    {
	/* biew_scr was created by __init_vio; here we only put it into
	   key-input mode.  Its fifo completes escape sequences split
	   across reads and its ESCDELAY wait distinguishes a lone ESC
	   from a sequence. */
	    static const int fbnum[12] = { 1,1,1,1,15,17,18,19,20,21,23,24 };
	    static const char flet[4] = "PQRS";
	    int f,m;
	    set_term(biew_scr);
	    cbreak(); noecho(); nonl();
	    intrflush(stdscr,FALSE);
	    keypad(stdscr,TRUE);
	    nodelay(stdscr,TRUE);
	    set_escdelay(25);
	    mousemask(ALL_MOUSE_EVENTS,NULL);
	    /* bind the whole xterm modifier-F matrix even if this
	       terminfo entry lacks it (2=Shift 3=Alt 4=Shift+Alt
	       5=Ctrl 6=Shift+Ctrl) */
	    for (f = 0; f < 12; f++)
		for (m = 2; m <= 6; m++)
		{
		    char sq[24];
		    int kc;
		    switch (m)
		    {
			case 2: kc = KEY_F(f + 13); break;
			case 3: kc = KEY_F(f + 49); break;
			case 4: kc = KEY_F(f + 61); break;
			case 5: kc = KEY_F(f + 25); break;
			default:kc = KEY_F(f + 37); break;
		    }
		    if (f < 4)	snprintf(sq,sizeof(sq),"\033[1;%d%c",m,flet[f]);
		    else	snprintf(sq,sizeof(sq),"\033[%d;%d~",fbnum[f],m);
		    if (!key_defined(sq)) define_key(sq,kc);
		}
	    /* arrows/home/end can arrive in either CSI or SS3 form
	       depending on application mode — bind both explicitly. */
	    static const char *nav[8][2] = {
		{ "\033[A", "\033OA" }, { "\033[B", "\033OB" },
		{ "\033[C", "\033OC" }, { "\033[D", "\033OD" },
		{ "\033[H", "\033OH" }, { "\033[F", "\033OF" },
		{ "\033[1~", "\033[7~" }, { "\033[4~", "\033[8~" },
	    };
	    static const int navkey[8] = {
		KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_LEFT,
		KEY_HOME, KEY_END, KEY_HOME, KEY_END,
	    };
	    for (f = 0; f < 8; f++)
		for (m = 0; m < 2; m++)
		    if (!key_defined(nav[f][m])) define_key(nav[f][m],navkey[f]);
	    kitty_probe();
    }
#endif

#ifdef HAVE_ICONV
    {
    const char *screen_cp;
	screen_cp=nls_get_screen_cp();
	if(strncasecmp(screen_cp,"UTF",3)==0 && !on_console) {
	    is_unicode=1;
	}
	nls_handle=nls_init(user_cp,screen_cp);
	if(nls_handle==NULL) is_unicode=0;
    }
#endif

    if (on_console) {	/* init vt switching */
	struct vt_mode vt;

	signal(SIGUSR1, (void *) console_leave);
	signal(SIGUSR2, (void *) console_enter);
	ioctl(in_fd, VT_GETMODE, &vt);
	vt.mode = VT_PROCESS;
	vt.relsig = SIGUSR1;
	vt.acqsig = SIGUSR2;
	ioctl(in_fd, VT_SETMODE, &vt);
    }

#ifdef	HAVE_MOUSE
    {
	Gpm_Connect gc;

	gc.eventMask = ~0;
	gc.defaultMask = GPM_MOVE|GPM_HARD;
	gc.minMod = gc.maxMod = 0;
	gpmhandle = Gpm_Open(&gc, 0);
	if (gpmhandle < 0) gpmhandle = 0;
    }
#endif

    keybuf.current = 0;

    /* Input is drained only from the __kbdGetKey/__OsGetCBreak poll
       loops.  ReadNextEvent must never run inside SIGIO: a byte arriving
       mid-sequence makes the handler re-enter with its own buffer and
       the two calls split the escape sequence (~30 % of keys lost). */
    signal(SIGIO, dummy_handler);
}

void __FASTCALL__ __term_keyboard(void)
{
#ifdef HAVE_ICONV
    nls_term(nls_handle);
#endif
#ifdef HAVE_NCURSES
    if (kitty_mode && biew_scr)
    {
	set_term(biew_scr);
	putp("\033[<u");		/* pop pushed kitty keyboard flags */
	fflush(stdout);
    }
#endif
    if (on_console) ioctl(in_fd, KDSKBMODE, K_XLATE);
    tcsetattr(in_fd, TCSANOW, &sattr);
    close(in_fd);
#ifdef	HAVE_MOUSE
    if (gpmhandle) Gpm_Close();
#endif
}
