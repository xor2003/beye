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

#include <dlfcn.h>
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
#include <sys/kd.h>
#include <sys/vt.h>

#include "biewlib/kbd_code.h"
#include "biewlib/biewlib.h"
#include "console.h"

#ifdef HAVE_MOUSE
#include <gpm.h>
static int gpmhandle;
#endif

#ifdef HAVE_NOTCURSES
#include <notcurses/notcurses.h>
extern struct notcurses *biew_nc;	/* created by __init_vio (non-console) */
extern void __FASTCALL__ BiewScreenResize(void) __attribute__((weak));	/* biew.c: re-layout top-level windows; weak so -lbiew-only tools still link */
#endif

#ifdef HAVE_NCURSES
#include <curses.h>
static SCREEN *nc_scr = NULL;
static FILE *nc_devnull = NULL;
#endif

/* true when a terminal library (ncurses or notcurses) owns input
   decoding on this terminal; in that case SIGIO does not pump and
   waiters like __inputRawInfo must drive ReadNextEvent themselves */
int __NEAR__ termlib_input(void)
{
#ifdef HAVE_NOTCURSES
    if (biew_nc) return 1;
#endif
#ifdef HAVE_NCURSES
    if (nc_scr) return 1;
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

typedef struct {
    char c;
    int key;
} seqtbl;

typedef seqtbl pseq[];
typedef seqtbl p1seq[1]; /**< dummy type to make compiler happy */

typedef struct {
    char pre1;
    char pre2;
    char suf;
    p1seq *s;
} eseq;

/**
    translatable sequences
*/

static const pseq seq0 = {
    {'A',KE_UPARROW},
    {'B',KE_DOWNARROW},
    {'C',KE_RIGHTARROW},
    {'D',KE_LEFTARROW},
    {'P',KE_F(1)},
    {'Q',KE_F(2)},
    {'R',KE_F(3)},
    {'w',KE_F(3)},
    {'y',KE_F(3)},
    {'S',KE_F(4)},
    {'x',KE_F(4)},
    {'t',KE_F(5)},
    {'v',KE_F(5)},
    {'u',KE_F(6)},
    {'l',KE_F(6)},
    {'q',KE_F(7)},
    {'s',KE_F(7)},
    {'r',KE_F(8)},
    {'p',KE_F(9)},
    {'n',KE_F(9)},
    {0,0}},
seq1 = {
    {'A',KE_UPARROW},
    {'B',KE_DOWNARROW},
    {'C',KE_RIGHTARROW},
    {'D',KE_LEFTARROW},
    {'H',KE_HOME},
    {'K',KE_END},
    {0,0}},
seq2 = {
    {'A',KE_F(1)},
    {'B',KE_F(2)},
    {'C',KE_F(3)},
    {'D',KE_F(4)},
    {'E',KE_F(5)},
    {0,0}},
seq3 = {
    {'1',KE_HOME},
    {'2',KE_INS},
    {'3',KE_DEL},
    {'4',KE_END},
    {'5',KE_PGUP},
    {'6',KE_PGDN},
    {'7',KE_HOME},
    {'8',KE_END},
    {'J',KE_CTL_PGDN},
    {0,0}},
seq4 = {
    {'1',KE_F(1)},
    {'2',KE_F(2)},
    {'3',KE_F(3)},
    {'4',KE_F(4)},
    {'5',KE_F(5)},
    {'7',KE_F(6)},
    {'8',KE_F(7)},
    {'9',KE_F(8)},
    {0,0}},
seq5 = {
    {'0',KE_F(9)},
    {'1',KE_F(10)},
    {'3',KE_F(11)},
    {'4',KE_F(12)},
    {'5',KE_SHIFT_F(3)},
    {'6',KE_SHIFT_F(4)},
    {'8',KE_SHIFT_F(5)},
    {'9',KE_SHIFT_F(6)},
    {0,0}},
seq6 = {
    {'1',KE_SHIFT_F(7)},
    {'2',KE_SHIFT_F(8)},
    {'3',KE_SHIFT_F(9)},
    {'4',KE_SHIFT_F(10)},
    {0,0}},
seq7 = {
    {'1',KE_CTL_F(1)},
    {'2',KE_CTL_F(2)},
    {'3',KE_CTL_F(3)},
    {'4',KE_CTL_F(4)},
    {'5',KE_CTL_F(5)},
    {'7',KE_CTL_F(6)},
    {'8',KE_CTL_F(7)},
    {'9',KE_CTL_F(8)},
    {0,0}},
seq8 = {
    {'0',KE_CTL_F(10)},
    {'1',KE_CTL_F(11)},
    {0,0}};

#define SEQ_LEN 10	/**< max sequense length */
#define SEQ_NUM 9	/**< number of sequence categories */

static eseq S[SEQ_NUM] = {
{'O', 0, 0, (p1seq *)seq0 },
{'[', 0, 0, (p1seq *)seq1 },
{'[', '[', 0, (p1seq *)seq2 },
{'[', 0, '~', (p1seq *)seq3 },
{'[', '1', '~', (p1seq *)seq4 },
{'[', '2', '~', (p1seq *)seq5 },
{'[', '3', '~',	(p1seq *)seq6 },
{'[', '1', '^', (p1seq *)seq7 },
{'[', '2', '^', (p1seq *)seq8 },
};

static mevent mouse = {0, 0, 0, 0};

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
	    memmove(keybuf.pool, &keybuf.pool[1], keybuf.current);
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
    memset(&keypressed, 0, KEYNUM * sizeof(int));
    console_restart = 1;
    signal(SIGUSR2, (void *) console_enter);
}

/**
    ReadNextEvent is non-blocking
*/

#ifdef HAVE_NOTCURSES
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

static int __NEAR__ map_nc_input(const ncinput *ni)
{
    int k = 0;
    unsigned id = ni->id;
    if (id >= NCKEY_F01 && id <= NCKEY_F60) k = __kbdModFKey(id - NCKEY_F01 + 1);
    else if (id < 0x80) k = (int)id;
    else switch (id)
    {
	case NCKEY_ENTER:	k = KE_ENTER; break;
	case NCKEY_UP:		k = KE_UPARROW; break;
	case NCKEY_DOWN:	k = KE_DOWNARROW; break;
	case NCKEY_LEFT:	k = KE_LEFTARROW; break;
	case NCKEY_RIGHT:	k = KE_RIGHTARROW; break;
	case NCKEY_HOME:	k = KE_HOME; break;
	case NCKEY_END:		k = KE_END; break;
	case NCKEY_INS:		k = KE_INS; break;
	case NCKEY_DEL:		k = KE_DEL; break;
	case NCKEY_PGUP:	k = KE_PGUP; break;
	case NCKEY_PGDOWN:	k = KE_PGDN; break;
	case NCKEY_BACKSPACE:	k = KE_BKSPACE; break;
	case NCKEY_RESIZE:
	    {
		struct winsize wsz;
		/* stddim_yx may lag one event behind; kernel winsize is always current */
		if (ioctl(in_fd, TIOCGWINSZ, &wsz) < 0 || !wsz.ws_col || !wsz.ws_row)
		    return 0;
		if ((unsigned)wsz.ws_col != (unsigned)tvioWidth ||
		    (unsigned)wsz.ws_row != (unsigned)tvioHeight)
		{
		    __vioResize(wsz.ws_col, wsz.ws_row);
		    if (BiewScreenResize) BiewScreenResize();
		}
	    }
	    return 0;
    }
    if (!k) return 0;
    if (ncinput_shift_p(ni)) k |= ADD_SHIFT;
    if (ncinput_alt_p(ni))   k |= ADD_ALT;
    if (ncinput_ctrl_p(ni))  k |= ADD_CONTROL;
    return k;
}
#endif

#ifdef HAVE_NCURSES
static int __NEAR__ map_curses_key(int ch)
{
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
#endif

void __FASTCALL__ ReadNextEvent(void)
{
#define get(x)	read(in_fd,&(x),1)
#define ret(x)	pushEvent((x)); return;
#define set_s(x) shift_status &= (x); shift_status ^= (x); ret(KE_SHIFTKEYS);

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
    VT100 emulation — ncurses decodes the input stream when available
    (its internal fifo completes escape sequences split across reads);
    the hand-rolled parser below remains as a fallback.
*/
	    unsigned char c[SEQ_LEN];

#ifdef HAVE_NOTCURSES
	    if (biew_nc)
	    {
		ncinput ni;
		while (notcurses_get_nblock(biew_nc, &ni))
		{
		    int kc;
		    if (ni.evtype == NCTYPE_RELEASE) continue;
		    if (ni.id >= NCKEY_BUTTON1 && ni.id <= NCKEY_BUTTON3)
		    {
			if (ni.evtype != NCTYPE_RELEASE)
			{
			    mouse.x = ni.x; mouse.y = ni.y;
			    mouse.pressed = 1;
			    if (ni.id == NCKEY_BUTTON1) mouse.buttons |= MS_LEFTPRESS;
			    if (ni.id == NCKEY_BUTTON2) mouse.buttons |= MS_MIDDLEPRESS;
			    if (ni.id == NCKEY_BUTTON3) mouse.buttons |= MS_RIGHTPRESS;
			    ret(KE_MOUSE);
			}
			continue;
		    }
		    kc = map_nc_input(&ni);
		    if (!kc) continue;
		    key = kc;
		    if(rawkb_mode) { rawkb_buf[0]=(unsigned char)ni.id; rawkb_len=1; }
		    goto place_key;
		}
		return;
	    }
#endif
#ifdef HAVE_NCURSES
	    if (nc_scr)
	    {
		int ch;
		while ((ch = getch()) != ERR)
		{
		    if (ch == KEY_MOUSE)
		    {
			MEVENT me;
			if (getmouse(&me) == OK)
			{
			    int m = 0;
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
			    if (m) ret(KE_MOUSE);
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

	    if (get(c[0]) < 0) return;
	    
	    switch(c[0]) {
		case KE_ESCAPE		: break;
		case KE_STATUS_RESET	: if(!rawkb_mode) {set_s(0); } else { rawkb_mode=0; return; }
		case KE_STATUS_ALT	: if(!rawkb_mode) {set_s(KS_ALT); } else { rawkb_mode=0; return; }
		case KE_STATUS_SHIFT	: if(!rawkb_mode) {set_s(KS_SHIFT); } else { rawkb_mode=0; return; }
		case KE_STATUS_CONTROL	: if(!rawkb_mode) {set_s(KS_CTRL); } else { rawkb_mode=0; return; }
		case 0			: break_status = True; break;
		case KE_ENTER2		: key = KE_ENTER; break;
		case KE_BKSPACE2	: key = KE_BKSPACE; break;
		case KE_C_O		: key = KE_CTL_(O); break;
		default			: key = c[0];
	    }
	    if (key)
	    {
		if(rawkb_mode)
		{
		    rawkb_buf[0]=c[0];
		    rawkb_len=1;
		}
		goto place_key;
	    }
	    for (i = 1; i < SEQ_LEN - 1; i++) {
		if(get(c[i]) < 0) break;
	    }
	    if(rawkb_mode)
	    {
		memcpy(rawkb_buf,c,i);
		rawkb_len=i;
	    }
	    if (i < 3) {
		key = c[0];
		goto place_key;
	    }

    if (c[1] == '[' && c[2] == 'M' && i == 6) {
	switch (c[3] & 0x03) {
	    case 0: mouse.buttons |= MS_LEFTPRESS; break;
	    case 1: mouse.buttons |= MS_MIDDLEPRESS; break;
	    case 2: mouse.buttons |= MS_RIGHTPRESS; break;
	    default: mouse.pressed = 0; return;
	}
	mouse.x = c[4] - '!';
	mouse.y = c[5] - '!';
	mouse.pressed = 1;
	key = KE_MOUSE;
#ifdef	DEBUG
	printm("mouse at %02X, %02X - %02X\n", mouse.x, mouse.y, mouse.buttons);
#endif
	goto place_key;
    }

/*
    translate escape sequence
*/

    /*
       xterm/modern terminal modifier form: ESC [ <num> ; <mod> <final>
       where mod: 2=Shift 3=Alt 4=Shift+Alt 5=Ctrl 6=Shift+Ctrl
                  7=Alt+Ctrl 8=Shift+Alt+Ctrl
    */
    if (!key && c[1] == '[')
    {
	int j;
	for (j = 2; j < i && c[j] != ';'; j++) ;
	if (j < i)
	{
	    int num = 0, mod = 0, base = 0;
	    int k;
	    for (k = 2; k < j; k++)
		if (c[k] >= '0' && c[k] <= '9') num = num * 10 + (c[k] - '0');
	    for (k = j + 1; k < i - 1; k++)
		if (c[k] >= '0' && c[k] <= '9') mod = mod * 10 + (c[k] - '0');
	    mod--;	/* modifier param is 1 + bitmask */
	    if (mod >= 0 && mod <= 7)
	    {
		switch (c[i - 1])
		{
		    case '~':
			switch (num)
			{
			    case 11: base = KE_F(1); break;
			    case 12: base = KE_F(2); break;
			    case 13: base = KE_F(3); break;
			    case 14: base = KE_F(4); break;
			    case 15: base = KE_F(5); break;
			    case 17: base = KE_F(6); break;
			    case 18: base = KE_F(7); break;
			    case 19: base = KE_F(8); break;
			    case 20: base = KE_F(9); break;
			    case 21: base = KE_F(10); break;
			    case 23: base = KE_F(11); break;
			    case 24: base = KE_F(12); break;
			    case 2:  base = KE_INS; break;
			    case 3:  base = KE_DEL; break;
			    case 5:  base = KE_PGUP; break;
			    case 6:  base = KE_PGDN; break;
			    case 7:  base = KE_HOME; break;
			    case 8:  base = KE_END; break;
			}
			break;
		    case 'P': base = (num <= 1) ? KE_F(1) : 0; break;
		    case 'Q': base = (num <= 1) ? KE_F(2) : 0; break;
		    case 'R': base = (num <= 1) ? KE_F(3) : 0; break;
		    case 'S': base = (num <= 1) ? KE_F(4) : 0; break;
		    case 'A': base = (num <= 1) ? KE_UPARROW : 0; break;
		    case 'B': base = (num <= 1) ? KE_DOWNARROW : 0; break;
		    case 'C': base = (num <= 1) ? KE_RIGHTARROW : 0; break;
		    case 'D': base = (num <= 1) ? KE_LEFTARROW : 0; break;
		    case 'H': base = (num <= 1) ? KE_HOME : 0; break;
		    case 'F': base = (num <= 1) ? KE_END : 0; break;
		}
		if (base)
		{
		    key = base;
		    if (mod & 1) key |= ADD_SHIFT;
		    if (mod & 2) key |= ADD_ALT;
		    if (mod & 4) key |= ADD_CONTROL;
		}
	    }
	}
    }

#define S S[i]
	    for (i = 0; i < SEQ_NUM && !key; i++) {
		int j, n;

		if (c[1] != S.pre1) continue;
		n = 2;
		if (S.pre2) {
		    if (c[n] != S.pre2) continue;
		    n++;
		}
		for (j = 0; S.s[j]->c; j++) if (S.s[j]->c == c[n]) {
			if (!(S.suf && S.suf != c[n + 1])) {
			    key = S.s[j]->key; break;
			 }
	        }
	    }
#undef S
	}
c_end:
place_key:
	if(!rawkb_mode) { ret(key); }
	else { rawkb_escape=(key==KE_ESCAPE&&rawkb_len==1); rawkb_mode=0; }
#undef set_s
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

/*
   When not on a Linux console (xterm/Konsole/ssh) the terminal protocol
   never reports bare Shift/Ctrl/Alt presses, so the prompt bar cannot
   track modifier rows. If DISPLAY is set we can ask the X server for
   the physical modifier mask directly via XQueryPointer. libX11 is
   dlopen'd so there is no hard link dependency; on Wayland XWayland
   reports the same state.
*/
static void __NEAR__ poll_x11_modifiers(void)
{
    static int x11_state = 0; /* 0=untried 1=up -1=unavailable */
    static void *x11_dpy = NULL;
    static int (*x11_qp)(void *,unsigned long,void *,void *,
			 int *,int *,int *,int *,unsigned int *) = NULL;
    static unsigned long (*x11_root)(void *,int) = NULL;
    static int (*x11_scr)(void *) = NULL;
    static long x11_ms = 0;
    if (x11_state == 0)
    {
	x11_state = -1;
	if (getenv("DISPLAY"))
	{
	    void *h = dlopen("libX11.so.6",RTLD_LAZY|RTLD_LOCAL);
	    void *(*xop)(const char *);
	    if (!h) h = dlopen("libX11.so",RTLD_LAZY|RTLD_LOCAL);
	    if (h &&
		(xop = (void *(*)(const char *))dlsym(h,"XOpenDisplay")) &&
		(x11_qp = (int (*)(void *,unsigned long,void *,void *,
				   int *,int *,int *,int *,
				   unsigned int *))dlsym(h,"XQueryPointer")) &&
		(x11_root = (unsigned long (*)(void *,int))dlsym(h,"XRootWindow")) &&
		(x11_scr = (int (*)(void *))dlsym(h,"XDefaultScreen")) &&
		(x11_dpy = xop(NULL)))
		x11_state = 1;
	}
    }
    if (x11_state == 1)
    {
	struct timeval tv;
	long now;
	gettimeofday(&tv,NULL);
	now = tv.tv_sec * 1000 + tv.tv_usec / 1000;
	if (now - x11_ms >= 50)
	{
	    unsigned int mask = 0;
	    void *wr = NULL,*wc = NULL;
	    int rx = 0,ry = 0,wx = 0,wy = 0;
	    x11_ms = now;
	    if (x11_qp(x11_dpy,
		       x11_root(x11_dpy,x11_scr(x11_dpy)),
		       &wr,&wc,&rx,&ry,&wx,&wy,&mask))
	    {
		int ns = 0;
		if (mask & 0x01) ns |= KS_SHIFT; /* ShiftMask */
		if (mask & 0x08) ns |= KS_ALT;   /* Mod1Mask */
		if (mask & 0x04) ns |= KS_CTRL;  /* ControlMask */
		if (ns != shift_status)
		{
		    shift_status = ns;
		    pushEvent(KE_SHIFTKEYS);
		}
	    }
	}
    }
}

int __FASTCALL__ __kbdGetKey (unsigned long flg)
{
    int key = 0, s = 0;

    if (__MsGetBtns() && flg == KBD_NONSTOP_ON_MOUSE_PRESS) return KE_MOUSE;

    while (!keybuf.current) {
	__OsYield();
	if (!on_console) { poll_x11_modifiers();
#if defined(HAVE_NCURSES) || defined(HAVE_NOTCURSES)
	    if (termlib_input()) ReadNextEvent();
#endif
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
	if (!on_console) shift_status = 0;
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
    signal(SIGIO, dummy_handler);
    in_fd = open(ttyname(STDIN_FILENO), O_RDONLY);
    if (in_fd < 0) in_fd = STDIN_FILENO;

#define _MODE_ O_NONBLOCK | O_ASYNC

    if (fcntl(in_fd, F_SETFL, fcntl(in_fd, F_GETFL) | _MODE_) < 0)
    {
        printm("Can't set %X for %d: %s\nExiting..\n",
		_MODE_,
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
    if (!on_console
#ifdef HAVE_NOTCURSES
	&& !biew_nc	/* notcurses already owns input+output */
#endif
	)
    {
	/* ncurses owns input decoding on non-console terminals: its
	   fifo completes escape sequences split across reads and its
	   ESCDELAY wait distinguishes a lone ESC from a sequence.
	   Output stays with the twin layer, so curses gets /dev/null. */
	nc_devnull = fopen("/dev/null","w");
	if (nc_devnull) nc_scr = newterm(NULL,nc_devnull,stdin);
	if (nc_scr)
	{
	    static const int fbnum[12] = { 1,1,1,1,15,17,18,19,20,21,23,24 };
	    static const char flet[4] = "PQRS";
	    int f,m;
	    set_term(nc_scr);
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
	}
	else
	{
	    if (nc_devnull) { fclose(nc_devnull); nc_devnull = NULL; }
	}
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

    /* With a terminal library decoding (ncurses/notcurses), input is
       drained only from the __kbdGetKey poll loop — never inside a
       signal, which would re-enter the library and keybuf/pushEvent. */
#if defined(HAVE_NCURSES) || defined(HAVE_NOTCURSES)
    if (termlib_input()) { signal(SIGIO, dummy_handler); }
    else
#endif
    signal(SIGIO, (void *) ReadNextEvent);
}

void __FASTCALL__ __term_keyboard(void)
{
#ifdef HAVE_ICONV
    nls_term(nls_handle);
#endif
#ifdef HAVE_NCURSES
    if (nc_scr)
    {
	mousemask(0,NULL);
	endwin();
	delscreen(nc_scr);
	nc_scr = NULL;
	if (nc_devnull) { fclose(nc_devnull); nc_devnull = NULL; }
    }
#endif
    if (on_console) ioctl(in_fd, KDSKBMODE, K_XLATE);
    tcsetattr(in_fd, TCSANOW, &sattr);
    close(in_fd);
#ifdef	HAVE_MOUSE
    if (gpmhandle) Gpm_Close();
#endif
}
