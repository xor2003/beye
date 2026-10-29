/**
 * @namespace   biewlib
 * @file        biewlib/sysdep/generic/unix/keyboard.c
 * @brief       slang/curses/vt100 keyboard library
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

    $Id: keyboard.c,v 1.9 2009/09/20 13:43:37 nickols_k Exp $
*/

#ifndef lint
static const char rcs_id[] = "$Id: keyboard.c,v 1.9 2009/09/20 13:43:37 nickols_k Exp $";
#endif

#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "biewlib/kbd_code.h"
#include "biewlib/biewlib.h"
#include "console.h"

#ifdef	_SLANG_
#include <slang.h>
char *rawkb_name="Slang";
unsigned rawkb_size=sizeof(int); /* size of rawkb_buf elements 1,2 or 4*/
int rawkb_method=0;
#undef	HAVE_MOUSE
#endif

#ifdef	_CURSES_
#include <curses.h>
char *rawkb_name="Curses";
unsigned rawkb_size=sizeof(int); /* size of rawkb_buf elements 1,2 or 4*/
int rawkb_method=0;
#if defined(NCURSES_MOUSE_VERSION) && !defined(HAVE_MOUSE)
#define	HAVE_MOUSE
#endif
#endif

char rawkb_buf[100];
unsigned rawkb_len; /* length of rawkb_buf*/
unsigned rawkb_size; /* size of rawkb_buf elements 1,2 or 4*/
unsigned rawkb_mode=0;
int rawkb_escape;

#ifdef HAVE_ICONV
static void *nls_handle;
static int is_unicode=0;
#endif

#ifdef	_VT100_

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
char *rawkb_name="VT100";
unsigned rawkb_size=sizeof(char); /* size of rawkb_buf elements 1,2 or 4*/
int rawkb_method=1;
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif

static int in_fd;
static struct termios sattr;


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

const static pseq seq0 = {
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
{'[', '2', '^', (p1seq *)seq8 }
};

#ifdef HAVE_MOUSE
#include <gpm.h>
static int gpmhandle;
#endif

#endif	/* _VT100_ */

/*
    keyboard FIFO
*/

#define KBUFSIZE 64

static struct {
    int pool[KBUFSIZE];
    int current;
} keybuf;

/*
    mouse event
*/

static mevent mouse = {0, 0, 0, 0};

static int	shift_status = 0;	/**< status of shift keys */
static tBool	mouse_status = True;	/**< mouse state */

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
    if (event) {
	if (keybuf.current < KBUFSIZE) {
#ifdef	_SLANG_
	    if (event > 0x100 && event <0x1000) event = _2B(event);
#endif
#ifdef	_CURSES_
	    if (event >= KEY_MIN && event <= KEY_MAX) event = _2B(event);
#endif
	    if (keybuf.current)
		memmove(keybuf.pool, &keybuf.pool[1], keybuf.current);
	    keybuf.pool[0] = event;
	    keybuf.current++;
	}
    } else {
	break_status = True;
    }
}

/**
    ReadNextEvent is non-blocking call
*/

void __FASTCALL__ ReadNextEvent(void)
{
    unsigned key = 0;

#define ret(x)	pushEvent(x); return;
#define set_s(x) shift_status &= (x); shift_status ^= (x); ret(KE_SHIFTKEYS);

#ifdef	_SLANG_
    if (!SLang_input_pending(0)) return;

    /* single escape */
    key = SLang_getkey();
    if (SLang_input_pending(0)) {
	SLang_ungetkey(key);
	key = SLkp_getkey();
    }
    if(rawkb_mode)
    {
	rawkb_buf[0]=key;
	rawkb_len=1;
    }
    switch(key) {
	case SL_KEY_ERR		: return; /* don't reset rawkb_mode here */
	case KE_STATUS_RESET	: if(!rawkb_mode) { set_s(0); } else { rawkb_mode=0; return; }
	case KE_STATUS_ALT	: if(!rawkb_mode) { set_s(KS_ALT); } else { rawkb_mode=0; return; }
	case KE_STATUS_SHIFT	: if(!rawkb_mode) { set_s(KS_SHIFT); } else { rawkb_mode=0; return; }
	case KE_STATUS_CONTROL	: if(!rawkb_mode) { set_s(KS_CTRL); } else { rawkb_mode=0; return; }
	case KE_ENTER2		: key = KE_ENTER; break;
	case KE_BKSPACE1	: key = KE_BKSPACE; break;
	case KE_BKSPACE2	: key = KE_BKSPACE; break;
	case KE_C_O		: key = KE_CTL_(O); break;
	case 0			: if(!rawkb_mode) { ret(0); } else { rawkb_mode=0; return; }
    }
    goto place_key;
#endif

#ifdef	_CURSES_
    key = getch();
    if(rawkb_mode)
    {
	rawkb_buf[0]=key;
	rawkb_len=1;
    }
    switch(key) {
	case ERR		: return; /* don't reset rawkb_mode here */
	case KE_STATUS_RESET	: if(!rawkb_mode) { set_s(0); } else { rawkb_mode=0; return; }
	case KE_STATUS_ALT	: if(!rawkb_mode) { set_s(KS_ALT); } else { rawkb_mode=0; return; }
	case KE_STATUS_SHIFT	: if(!rawkb_mode) { set_s(KS_SHIFT); } else { rawkb_mode=0; return; }
	case KE_STATUS_CONTROL	: if(!rawkb_mode) { set_s(KS_CTRL); } else { rawkb_mode=0; return; }
	case KE_ENTER2		: key = KE_ENTER; break;
	case KE_BKSPACE1	: key = KE_BKSPACE; break;
	case KE_BKSPACE2	: key = KE_BKSPACE; break;
	case KE_C_O		: key = KE_CTL_(O); break;
	case 0			: if(!rawkb_mode) { ret(0); } else { rawkb_mode=0; return; }
#ifdef NCURSES_MOUSE_VERSION
        case KEY_MOUSE		:
	    {
		MEVENT me;
		int m = 0;

		getmouse(&me);

		if (me.bstate & BUTTON1_CLICKED || me.bstate & BUTTON1_PRESSED)
		    { m = 1; mouse.buttons |= MS_LEFTPRESS; }
		if (me.bstate & BUTTON2_CLICKED || me.bstate & BUTTON2_PRESSED)
		    { m = 1; mouse.buttons |= MS_MIDDLEPRESS; }
		if (me.bstate & BUTTON3_CLICKED || me.bstate & BUTTON3_PRESSED)
		    { m = 1; mouse.buttons |= MS_RIGHTPRESS; }

		mouse.x = me.x;
		mouse.y = me.y;
		mouse.pressed = m;
		if (!mouse.buttons) mouse.pressed = 0;

		if (mouse.pressed) { if(!rawkb_mode) { ret(KE_MOUSE); } else { rawkb_mode=0; return; } }
	    }
#endif
    }
    goto place_key;
#endif

#ifdef	_VT100_
#define get(x) read(in_fd,&(x),1)

    int i;
    unsigned char c[SEQ_LEN];

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

#ifdef __linux__
    /* Linux console reports modifier state via TIOCL_GETSHIFTSTATE —
       poll it so the prompt bar updates when Shift/Ctrl/Alt is pressed
       alone (xterm-class terminals have no way to report this). */
    {
	static int con_fd = -2;
	if (con_fd == -2)
	{
	    char sc = 6;
	    con_fd = -1;
	    if (isatty(in_fd) && ioctl(in_fd, 0x541C /*TIOCLINUX*/, &sc) >= 0)
		con_fd = in_fd;
	}
	if (con_fd >= 0)
	{
	    char sc = 6;
	    int st = ioctl(con_fd, 0x541C, &sc);
	    if (st >= 0)
	    {
		int ns = 0;
		if (st & 0x01 || st & 0x10 || st & 0x20) ns |= KS_SHIFT;
		if (st & 0x08 || st & 0x02)                ns |= KS_ALT;
		if (st & 0x04 || st & 0x40 || st & 0x80)   ns |= KS_CTRL;
		if (ns != shift_status)
		{
		    shift_status = ns;
		    ret(KE_SHIFTKEYS);
		}
	    }
	}
	else
	{
	    /* Not a Linux console: ask the X server for the physical
	       modifier state. xterm-class terminals never report bare
	       Shift/Ctrl/Alt presses, but when DISPLAY is set we can
	       poll XQueryPointer which returns the live modifier mask.
	       libX11 is dlopen'd so there is no hard link dependency. */
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
			    ret(KE_SHIFTKEYS);
			}
		    }
		}
	    }
	}
    }
#endif

    if (get(c[0]) < 0) return;

    switch(c[0]) {
	case KE_ESCAPE		: break;
	case KE_STATUS_RESET	: if(!rawkb_mode) { set_s(0); } else { rawkb_mode=0; return; }
	case KE_STATUS_ALT	: if(!rawkb_mode) { set_s(KS_ALT); } else { rawkb_mode=0; return; }
	case KE_STATUS_SHIFT	: if(!rawkb_mode) { set_s(KS_SHIFT); } else { rawkb_mode=0; return; }
	case KE_STATUS_CONTROL	: if(!rawkb_mode) { set_s(KS_CTRL); } else { rawkb_mode=0; return; }
	case KE_ENTER2		: key = KE_ENTER; break;
	case KE_BKSPACE2	: key = KE_BKSPACE; break;
	case KE_C_O		: key = KE_CTL_(O); break;
	case 0			: if(!rawkb_mode) { ret(0); } else { rawkb_mode=0; return; }
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
    for (i = 1; i < SEQ_LEN - 1; i++) if(get(c[i]) < 0) break;
    if(rawkb_mode)
    {
	memcpy(rawkb_buf,c,i);
	rawkb_len=i;
    }

    if (i < 3) {
	key = c[0];
	goto place_key;
    }


/*
    track mouse
*/

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
#endif

place_key:
    if (key)
    {
	if(!rawkb_mode) { ret(key); }
	else { rawkb_escape=(key==KE_ESCAPE&&rawkb_len==1); rawkb_mode=0; }
    }

#undef set_s
#undef ret
}

inline int __FASTCALL__ __kbdGetShiftsKey(void)
{
    return shift_status;
}

int __FASTCALL__ __kbdTestKey(unsigned long flg)
{
    if(__MsGetBtns() && flg == KBD_NONSTOP_ON_MOUSE_PRESS) return KE_MOUSE;
    return keybuf.current;
}

int __FASTCALL__ __kbdGetKey(unsigned long flg)
{
    int key = 0, s = 0;

    if (__kbdTestKey(flg) == KE_MOUSE) return KE_MOUSE;

    while (!keybuf.current) { __OsYield(); ReadNextEvent(); }
    key = keybuf.pool[--keybuf.current];
    if (!(key == KE_MOUSE || key == KE_SHIFTKEYS)) {
	if ((shift_status & KS_ALT) == KS_ALT)		s |= ADD_ALT;
	if ((shift_status & KS_SHIFT) == KS_SHIFT)	s |= ADD_SHIFT;
	if ((shift_status & KS_CTRL) == KS_CTRL) {
	    s |= ADD_CONTROL;
	    if (key == 'o' || key == 'O') key = KE_CTL_(O);    /* CTRL+O */
	}
	shift_status = 0;
    }
    return key | s;
}

/*

*/

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
#ifdef HAVE_MOUSE
    ReadNextEvent();
#endif
    return mouse.pressed ? mouse.buttons : 0;
}


void __FASTCALL__ __init_keyboard(const char *user_cp)
{
#ifdef	_CURSES_
    raw();
    noecho();
    intrflush(stdscr, FALSE);
    meta(stdscr, TRUE);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
/*    notimeout(stdscr,TRUE); */ /* somewhy it blocks read function */
#ifdef NCURSES_MOUSE_VERSION
    mousemask(REPORT_MOUSE_POSITION | ALL_MOUSE_EVENTS, NULL);
#endif
#endif

#ifdef	_VT100_
    struct termios tattr;

#ifdef	__ENABLE_SIGIO
#define _MODE_ O_NONBLOCK | O_ASYNC
#else
#define _MODE_ O_NONBLOCK
#endif

#ifdef HAVE_ICONV
    {
    const char *screen_cp;
	screen_cp=nls_get_screen_cp();
	if(strncasecmp(screen_cp,"UTF",3)==0) {
	    is_unicode=1;
	}
	nls_handle=nls_init(user_cp,screen_cp);
	if(nls_handle==NULL) is_unicode=0;
    }
#endif

    in_fd = open(ttyname(STDIN_FILENO), O_RDONLY);
    if (in_fd < 0) in_fd = STDIN_FILENO;

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
    tattr.c_iflag &= ~(INPCK | ISTRIP | IXON);
    tattr.c_oflag |= OPOST | ONLCR;
    tcsetattr(in_fd, TCSANOW, &tattr);

#ifdef	HAVE_MOUSE
    {
	Gpm_Connect gc = { ~0, GPM_MOVE|GPM_HARD, 0, 0};
	gpmhandle = Gpm_Open(&gc, 0);
	if (gpmhandle < 0) gpmhandle = 0;
    }
#endif

    keybuf.current = 0;

#ifdef	__ENABLE_SIGIO
    /* everything is ready, start to receive SIGIO */
    signal(SIGIO, (void *)(int) ReadNextEvent);
#endif

#endif	/* _VT100_ */

}

void __FASTCALL__ __term_keyboard(void)
{
#ifdef HAVE_ICONV
    nls_term(nls_handle);
#endif
#ifdef	_VT100_
    tcsetattr(in_fd, TCSANOW, &sattr);
    close(in_fd);
#ifdef	HAVE_MOUSE
    if (gpmhandle) Gpm_Close();
#endif
#endif
}
