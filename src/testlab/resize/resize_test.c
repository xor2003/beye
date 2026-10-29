/**
 * @file        testlab/resize/resize_test.c
 * @brief       Unit tests for terminal-resize handling:
 *               - __kbdModFKey   (terminfo modified F-key decode)
 *               - __vioResize    (shadow buffer realloc on geometry change)
 *               - BiewScreenResize (top-level window re-layout)
 * @remark      Runs on a private pty; no terminal emulator required.
*/

#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "biewlib/biewlib.h"
#include "biewlib/kbd_code.h"
#include "biewlib/twin.h"
#include "bconsole.h"

/* windows under test are owned by the app; define them here so
   resize.o (which references them) resolves in this test binary */
TWindow *MainWnd = 0, *HelpWnd = 0, *TitleWnd = 0, *CritErrWnd = 0;

extern unsigned __FASTCALL__ __kbdModFKey(unsigned n);
extern void    __FASTCALL__ __vioResize(unsigned w, unsigned h);
extern void    __FASTCALL__ __init_sys(void);

static FILE *logf;
static int tests, failed;

#define CHECK(cond, name) do { \
    tests++; \
    if(!(cond)) { failed++; fprintf(logf, "FAIL %s\n", name); fflush(logf); } \
    else fprintf(logf, "ok   %s\n", name); \
} while(0)

static void test_modfkey(void)
{
    /* terminfo modified F-keys: F13-24=Shift, F25-36=Ctrl,
       F37-48=Ctrl+Shift, F49-60=Alt (ncurses convention) */
    CHECK(__kbdModFKey(1)  == KE_F(1),                        "F1 plain");
    CHECK(__kbdModFKey(12) == KE_F(12),                       "F12 plain");
    CHECK(__kbdModFKey(13) == (KE_F(1)|ADD_SHIFT),            "F13 = Shift-F1");
    CHECK(__kbdModFKey(24) == (KE_F(12)|ADD_SHIFT),           "F24 = Shift-F12");
    CHECK(__kbdModFKey(25) == (KE_F(1)|ADD_CONTROL),          "F25 = Ctrl-F1");
    CHECK(__kbdModFKey(36) == (KE_F(12)|ADD_CONTROL),         "F36 = Ctrl-F12");
    CHECK(__kbdModFKey(37) == (KE_F(1)|ADD_SHIFT|ADD_CONTROL),"F37 = Ctrl+Shift-F1");
    CHECK(__kbdModFKey(48) == (KE_F(12)|ADD_SHIFT|ADD_CONTROL),"F48 = Ctrl+Shift-F12");
    CHECK(__kbdModFKey(49) == (KE_F(1)|ADD_ALT),              "F49 = Alt-F1");
    CHECK(__kbdModFKey(60) == (KE_F(12)|ADD_ALT),             "F60 = Alt-F12");
    CHECK(__kbdModFKey(0)  == 0,                              "F0 invalid");
    CHECK(__kbdModFKey(61) == 0,                              "F61 invalid");
}

static void fill_row(tAbsCoord x, tAbsCoord y, unsigned char c, unsigned len)
{
    t_vchar chars[256], oem[256];
    ColorAttr attrs[256];
    tvioBuff b;
    memset(chars, c, len); memset(oem, 0, len); memset(attrs, 0x07, len);
    b.chars = chars; b.oem_pg = oem; b.attrs = attrs;
    __vioWriteBuff(x, y, &b, len);
}

static unsigned char read_cell(tAbsCoord x, tAbsCoord y)
{
    t_vchar ch[1], oem[1]; ColorAttr at[1]; tvioBuff b;
    b.chars = ch; b.oem_pg = oem; b.attrs = at;
    __vioReadBuff(x, y, &b, 1);
    return ch[0];
}

static void test_vioresize(void)
{
    unsigned oldw = tvioWidth, oldh = tvioHeight;

    fill_row(0, 0, 'M', 5);
    CHECK(read_cell(0,0) == 'M', "vioWriteBuff/vioReadBuff roundtrip");

    __vioResize(100, 40);
    CHECK((unsigned)tvioWidth == 100 && (unsigned)tvioHeight == 40,
	  "__vioResize updates dims");
    CHECK(read_cell(0,0) == 0, "__vioResize clears shadow buffer");

    /* same-size call must not touch the buffer */
    fill_row(0, 0, 'K', 5);
    __vioResize(100, 40);
    CHECK(read_cell(0,0) == 'K', "__vioResize same-size is no-op");

    /* zero dims keep current size */
    __vioResize(0, 0);
    CHECK((unsigned)tvioWidth == 100 && (unsigned)tvioHeight == 40,
	  "__vioResize(0,0) keeps dims");

    /* out-of-range dims must be clamped to the implementation maximum
       (VMAX_X=255, VMAX_Y=0x400 are private to vio.c; assert behaviorally) */
    __vioResize(0xFFFF, 0xFFFF);
    CHECK((unsigned)tvioWidth <= 255 && (unsigned)tvioHeight <= 0x400 &&
	  (unsigned)tvioWidth > 200 && (unsigned)tvioHeight > 100,
	  "__vioResize clamps huge dims");
    fill_row(tvioWidth - 1, tvioHeight - 1, 'E', 1);
    CHECK(read_cell(tvioWidth - 1, tvioHeight - 1) == 'E',
	  "shadow buffer usable at clamped edge");

    __vioResize(oldw, oldh);
}

static void check_layout(unsigned w, unsigned h, const char *tag)
{
    tAbsCoord x1,y1,x2,y2;

    /* twGetWinPos: x1,y1 are 1-based; x2,y2 are the exclusive end */
    twGetWinPos(TitleWnd,&x1,&y1,&x2,&y2);
    CHECK(x1 == 1 && y1 == 1 && x2 == w && y2 == 1, tag);

    twGetWinPos(HelpWnd,&x1,&y1,&x2,&y2);
    CHECK(y1 == h && y2 == h && x2 == w, tag);

    twGetWinPos(MainWnd,&x1,&y1,&x2,&y2);
    CHECK(y1 == 2 && x2 == w && y2 == h - 1, tag);
}

static void test_screenresize(void)
{
    /* windows laid out exactly as biew.c creates them:
       TitleWnd row1, MainWnd rows2..H-1, HelpWnd last row */
    TitleWnd = twCreateWin(1,1,tvioWidth,1,TWS_NONE);
    HelpWnd  = twCreateWin(1,tvioHeight,tvioWidth,1,TWS_NLSOEM);
    MainWnd  = twCreateWin(1,2,tvioWidth,tvioHeight-2,TWS_NONE);
    CHECK(TitleWnd && HelpWnd && MainWnd, "windows created");
    twShowWin(TitleWnd); twShowWin(MainWnd); twShowWin(HelpWnd);

    check_layout(tvioWidth, tvioHeight, "initial layout");

    __vioResize(60, 15); BiewScreenResize();
    check_layout(60, 15, "shrink to 60x15");

    __vioResize(100, 40); BiewScreenResize();
    check_layout(100, 40, "grow to 100x40");

    __vioResize(80, 24); BiewScreenResize();
    check_layout(80, 24, "back to 80x24");

    /* tiny screen: legend still on last row, no overlap crash */
    __vioResize(40, 6); BiewScreenResize();
    check_layout(40, 6, "tiny 40x6");

    /* resize during a visible dialog must not corrupt it */
    __vioResize(80, 24); BiewScreenResize();
    {
	TWindow *dlg = twCreateWin(10,10,30,5,TWS_NONE);
	if(dlg) {
	    twShowWin(dlg);
	    __vioResize(60, 15); BiewScreenResize();
	    twDestroyWin(dlg);
	    CHECK(1, "resize with dialog open");
	} else CHECK(0, "resize with dialog open");
    }
}

int main(void)
{
    int mfd, sfd;
    struct winsize wsz;
    struct termios tio;
    char name[64];

    logf = fopen("/tmp/resize_test.log", "w");
    if(!logf) logf = stderr;

    memset(&wsz, 0, sizeof(wsz));
    wsz.ws_col = 80; wsz.ws_row = 24;
    memset(&tio, 0, sizeof(tio));
    tio.c_cflag = B38400 | CS8 | CREAD;
    if(openpty(&mfd, &sfd, name, &tio, &wsz) < 0) {
	fprintf(logf, "openpty failed\n");
	return 1;
    }
    dup2(sfd, STDIN_FILENO);
    dup2(sfd, STDOUT_FILENO);
    dup2(sfd, STDERR_FILENO);

    /* drain the pty master so vio output never fills the pty buffer
       and blocks write(); child exits when our slave fds close */
    {
	pid_t d = fork();
	if(d == 0) {
	    char dump[4096];
	    while(read(mfd, dump, sizeof(dump)) > 0);
	    _exit(0);
	}
	close(mfd);
    }

    /* TERM=dumb: notcurses_init fails fast -> ANSI path, no query waits */
    setenv("TERM", "dumb", 1);
    __init_sys();          /* sets `terminal` global used by __init_vio */
    twInit(NULL, 0, 0);

    test_modfkey();
    test_vioresize();
    test_screenresize();

    fprintf(logf, "%d tests, %d failed\n", tests, failed);
    fclose(logf);
    return failed ? 1 : 0;
}
