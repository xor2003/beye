/**
 * @namespace   biew
 * @file        resize.c
 * @brief       Terminal resize: re-layout of top-level windows
 * @version     -
 * @remark      this source file is part of Binary vIEW project (BIEW).
 *              The Binary vIEW (BIEW) is copyright (C) 1995 Nickols_K.
 *              All rights reserved. This software is redistributable under the
 *              licence given in the file "Licence.en" ("Licence.ru" in russian
 *              translation) distributed in the BIEW archive.
*/

#include "bconsole.h"
#include "biewlib/biewlib.h"

/** Re-layouts top-level windows after a terminal resize.
    HelpWnd (the function-key legend) must always sit on the last
    screen row; TitleWnd/MainWnd span the full width. */
void __FASTCALL__ BiewScreenResize( void )
{
    tAbsCoord x1,y1,x2,y2;

    if(TitleWnd)
    {
	twGetWinPos(TitleWnd,&x1,&y1,&x2,&y2);
	if(x2 != tvioWidth) twResizeWin(TitleWnd,tvioWidth,1);
    }
    if(HelpWnd)
    {
	int dy;
	twGetWinPos(HelpWnd,&x1,&y1,&x2,&y2);
	/* twGetWinPos returns 1-based y1 (win->Y1+1); internal Y1 must
	   become tvioHeight-1 so the legend sits on the last row */
	dy = (int)tvioHeight - (int)y1;
	/* tAbsCoord is unsigned so twMoveWin cannot express a negative
	   delta; hide, shift and show the legend window manually. */
	if(dy)
	{
	    twHideWin(HelpWnd);
	    HelpWnd->Y1 += dy;
	    HelpWnd->Y2 += dy;
	}
	if(x2 != tvioWidth) twResizeWin(HelpWnd,tvioWidth,1);
	if(dy) twShowWin(HelpWnd);
    }
    /* WindowOpen(1,2,W,H-1) means height == tvioHeight-2 */
    if(MainWnd) twResizeWin(MainWnd,tvioWidth,tvioHeight - 2);
    /* repaint order: legend last so it stays on top */
    if(TitleWnd) twRefreshFullWin(TitleWnd);
    if(MainWnd)  twRefreshFullWin(MainWnd);
    if(HelpWnd)  twRefreshFullWin(HelpWnd);
}
