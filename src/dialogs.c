/**
 * @namespace   biew
 * @file        dialogs.c
 * @brief       This file contains common dialogs of BIEW project.
 * @version     -
 * @remark      this source file is part of Binary vIEW project (BIEW).
 *              The Binary vIEW (BIEW) is copyright (C) 1995 Nickols_K.
 *              All rights reserved. This software is redistributable under the
 *              licence given in the file "Licence.en" ("Licence.ru" in russian
 *              translation) distributed in the BIEW archive.
 * @note        Requires POSIX compatible development system
 *
 * @author      Nickols_K
 * @since       1995
 * @note        Development, fixes and improvements
**/
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "colorset.h"
#include "bmfile.h"
#include "tstrings.h"
#include "biewhelp.h"
#include "bconsole.h"
#include "biewutil.h"
#include "bin_util.h"
#include "biewlib/kbd_code.h"
#include "biewlib/twin.h"

tBool __FASTCALL__ Get2DigitDlg(const char *title,const char * text,unsigned char *xx)
{
 tAbsCoord x1,y1,x2,y2;
 tRelCoord X1,Y1,X2,Y2;
 TWindow * hwnd,*ewnd,*using;
 tBool ret;
 int retval;
 char str[3] = "";
 using = twUsedWin();
 hwnd = CrtDlgWndnls(title,24,1);
 twUseWin(hwnd);
 twGetWinPos(hwnd,&x1,&y1,&x2,&y2);
 twGotoXY(1,1); twPutS(text);
 X1 = x1;
 Y1 = y1;
 X2 = x2;
 Y2 = y2;
 X1 += 22;
 Y1 += 1;
 X2 = X1 + 1;
 Y2 = Y1;
 ewnd = CreateEditor(X1,Y1,X2,Y2,TWS_VISIBLE | TWS_CURSORABLE | TWS_NLSOEM);
 twUseWin(ewnd);
 if(*xx) sprintf(str,"%X",(unsigned int)*xx);
 while(1)
 {
   retval = xeditstring(str,&legalchars[2],2,NULL);
   if(retval == KE_ESCAPE || retval == KE_F(10)) { ret = False; break; }
   else
     if(retval == KE_ENTER) { ret = True; break; }
 }
 CloseWnd(ewnd);
 CloseWnd(hwnd);
 if(ret) *xx = (unsigned char)strtoul(str,NULL,16);
 twUseWin(using);
 return ret;
}

#define HEX     0x00
#define UNSIGN  0x00
#define SIGN    0x01
#define DECIMAL 0x02

tBool __FASTCALL__ Get8DigitDlg(const char *title,const char *text,char attr,unsigned long *xx)
{
 tAbsCoord x1,y1,x2,y2;
 tRelCoord X1,Y1,X2,Y2;
 int key;
 TWindow * hwnd,*ewnd,*using;
 char base = attr & DECIMAL ? 10 : 16;
 char len  = attr & DECIMAL ? 10 : 8;
 tBool ret;
 char decleg[13];
 char str[12] = "";
 char * legals;
 memcpy(decleg,legalchars,12);
 decleg[12] = '\0';
 len += attr & SIGN ? 1 : 0;
 using = twUsedWin();
 hwnd = CrtDlgWndnls(title,34,1);
 twUseWin(hwnd);
 twGetWinPos(hwnd,&x1,&y1,&x2,&y2);
 twGotoXY(1,1); twPutS(text);
 X1 = x1;
 Y1 = y1;
 X2 = x2;
 Y2 = y2;
 Y1 += 1;
 X2 = X1 + 33;
 X1 = X2 - (len - 1);
 Y2 = Y1;
 ewnd = CreateEditor(X1,Y1,X2,Y2,TWS_VISIBLE | TWS_CURSORABLE | TWS_NLSOEM);
 twUseWin(ewnd);
 if(attr & DECIMAL) legals = attr & SIGN ? decleg : &decleg[2];
 else               legals = attr & SIGN ? legalchars : &legalchars[2];
 if(*xx)
 {
   if(attr & SIGN) ltoa(*xx,str,base);
   else            ultoa(*xx,str,base);
 }
 while(1)
 {
   key = xeditstring(str,legals,len,NULL);
   if(key == KE_ESCAPE || key == KE_F(10)) { ret = False; break; }
   else
     if(key == KE_ENTER) { ret = True; break; }
 }
 CloseWnd(ewnd);
 CloseWnd(hwnd);
 if(ret) *xx = attr & SIGN ? (unsigned long)strtol(str,NULL,base) : strtoul(str,NULL,base);
 twUseWin(using);
 return ret;
}

#if __WORDSIZE >= 32
tBool        __FASTCALL__ Get16DigitDlg(const char *title,const char *text,char attr,
					unsigned long long int *xx)
{
 tAbsCoord x1,y1,x2,y2;
 tRelCoord X1,Y1,X2,Y2;
 int key;
 TWindow * hwnd,*ewnd,*using;
 char base = attr & DECIMAL ? 10 : 16;
 char len  = attr & DECIMAL ? 20 : 16;
 tBool ret;
 char decleg[13];
 char str[20] = "";
 char * legals;
 memcpy(decleg,legalchars,12);
 decleg[12] = '\0';
 len += attr & SIGN ? 1 : 0;
 using = twUsedWin();
 hwnd = CrtDlgWndnls(title,44,1);
 twUseWin(hwnd);
 twGetWinPos(hwnd,&x1,&y1,&x2,&y2);
 twGotoXY(1,1); twPutS(text);
 X1 = x1;
 Y1 = y1;
 X2 = x2;
 Y2 = y2;
 Y1 += 1;
 X2 = X1 + 43;
 X1 = X2 - (len - 1);
 Y2 = Y1;
 ewnd = CreateEditor(X1,Y1,X2,Y2,TWS_VISIBLE | TWS_CURSORABLE | TWS_NLSOEM);
 twUseWin(ewnd);
 if(attr & DECIMAL) legals = attr & SIGN ? decleg : &decleg[2];
 else               legals = attr & SIGN ? legalchars : &legalchars[2];
 if(*xx)
 {
   if(attr & SIGN) lltoa(*xx,str,base);
   else            ulltoa(*xx,str,base);
 }
 while(1)
 {
   key = xeditstring(str,legals,len,NULL);
   if(key == KE_ESCAPE || key == KE_F(10)) { ret = False; break; }
   else
     if(key == KE_ENTER) { ret = True; break; }
 }
 CloseWnd(ewnd);
 CloseWnd(hwnd);
 if(ret) *xx = attr & SIGN ? (unsigned long long int)strtoll(str,NULL,base) : strtoull(str,NULL,base);
 twUseWin(using);
 return ret;
}
#endif

static void __NEAR__ __FASTCALL__ paintJumpDlg(TWindow *wdlg,unsigned long flags)
{
  TWindow *usd;
  usd = twUsedWin();
  twUseWin(wdlg);
  twSetColorAttr(dialog_cset.group.active);
  twGotoXY(4,2); twPutChar(flags == GJDLG_ABSOLUTE ? TWC_RADIO_CHAR : TWC_DEF_FILLER);
  twGotoXY(4,3); twPutChar(flags == GJDLG_RELATIVE ? TWC_RADIO_CHAR : TWC_DEF_FILLER);
  twGotoXY(4,4); twPutChar(flags == GJDLG_REL_EOF  ? TWC_RADIO_CHAR : TWC_DEF_FILLER);
  twGotoXY(4,5); twPutChar(flags == GJDLG_VIRTUAL  ? TWC_RADIO_CHAR : TWC_DEF_FILLER);
  twGotoXY(4,6); twPutChar(flags == GJDLG_PERCENTS ? TWC_RADIO_CHAR : TWC_DEF_FILLER);
  twUseWin(usd);
}

static const char * jmptxt[] =
{
  "      ",
  "Mode  ",
  "      ",
  "      ",
  "UsrNam",
  "      ",
  "      ",
  "      ",
  "      ",
  "Escape"
};

static void drawJumpPrompt( void )
{
   __drawSinglePrompt(jmptxt);
}


tBool __FASTCALL__ GetJumpDlg( __filesize_t * addr,unsigned long *flags)
{
 tAbsCoord x1,y1,x2,y2;
 tRelCoord X1,Y1,X2,Y2;
 int key;
 TWindow * hwnd,*ewnd,*using;
 unsigned len = HA_LEN-1,stx = 0;
 tBool ret,update;
 static char str[24] = "";
 char * legals;
 char declegals[13];
 unsigned attr;
 using = twUsedWin();
 hwnd = CrtDlgWndnls(" Jump within file ",(BMFileFlags&BMFF_USE64)?34:26,6);
 twUseWin(hwnd);
 memcpy(declegals,legalchars,12);
 twGetWinPos(hwnd,&x1,&y1,&x2,&y2);
 twGotoXY(2,1); twPutS("Enter offset :");
 twSetColorAttr(dialog_cset.group.active);
 twGotoXY(2,2); twPutS(" ( ) - Absolute       ");
 twGotoXY(2,3); twPutS(" ( ) - Relative       ");
 twGotoXY(2,4); twPutS(" ( ) - Relatively EOF ");
 twGotoXY(2,5); twPutS(" ( ) - Virtual        ");
 twGotoXY(2,6); twPutS(" ( ) - Percents       ");
 twSetColorAttr(dialog_cset.main);
 X1 = x1;
 Y1 = y1;
 X2 = x2;
 Y2 = y2;
 Y1 += 1;
 X1 += 17;
 X2 = X1 + len - 1;
 Y2 = Y1;
 ewnd = CreateEditor(X1,Y1,X2,Y2,TWS_VISIBLE | TWS_CURSORABLE | TWS_NLSOEM);
 twUseWin(ewnd);
 legals = *flags == GJDLG_RELATIVE ? legalchars : &legalchars[2];
 paintJumpDlg(hwnd,*flags);
 update = True;
 while(1)
 {
   attr = __ESS_NOTUPDATELEN | __ESS_WANTRETURN | __ESS_ENABLEINSERT;
   if(!update) attr |= __ESS_NOREDRAW;
   key = eeditstring(str,legals,&len,1,&stx,
                     attr,NULL,drawJumpPrompt);
   if(key == KE_ESCAPE || key == KE_F(10)) { ret = False; break; }
   else
     if(key == KE_ENTER) { ret = True; break; }
   update = True;
   switch(key)
   {
      case KE_F(1):  hlpDisplay(6);
                     update = False;
                     break;
      case KE_F(5):  if(udnSelectName(addr)) {
			if(BMFileFlags&BMFF_USE64) sprintf(str,"%016llX",*addr);
			else			   sprintf(str,"%08lX",(unsigned long)*addr);
		     }
		     break;
      case KE_F(2):  if(((*flags)&0xFF) < GJDLG_PERCENTS) (*flags)++;
                     else                                 (*flags) = 0;
                     legals = (*flags) == GJDLG_RELATIVE ||
                              (*flags) == GJDLG_REL_EOF ? legalchars :
                              (*flags) == GJDLG_PERCENTS ? declegals : &legalchars[2];
                     update = False;
                     break;
      case KE_LEFTARROW:
      case KE_RIGHTARROW:
                     update = False;
                     break;
      default:       break;
   }
   paintJumpDlg(hwnd,*flags);
 }
 CloseWnd(ewnd);
 CloseWnd(hwnd);
 if(ret)
 {
#if __WORDSIZE >= 32
 if(BMFileFlags&BMFF_USE64)
    *addr = (*flags) == GJDLG_RELATIVE ||
            (*flags) == GJDLG_REL_EOF ? (unsigned long long int)strtoll(str,NULL,(*flags)==GJDLG_PERCENTS?10:16):
                                        strtoull(str,NULL,(*flags)==GJDLG_PERCENTS?10:16);
 else
#endif
    *addr = (*flags) == GJDLG_RELATIVE ||
            (*flags) == GJDLG_REL_EOF ? (unsigned long)strtol(str,NULL,(*flags)==GJDLG_PERCENTS?10:16):
            strtoul(str,NULL,(*flags)==GJDLG_PERCENTS?10:16);
 }
 twUseWin(using);
 return ret;
}

tBool __FASTCALL__ GetStringDlg(char * buff,const char * title,const char *subtitle,const char *prompt)
{
  tAbsCoord x1,y1,x2,y2;
  tRelCoord X1,Y1,X2,Y2;
  int key;
  tBool ret;
  TWindow * wdlg,*ewnd;
  char estr[81];
  wdlg = CrtDlgWndnls(title,78,2);
  if(subtitle) twSetFooterAttr(wdlg,subtitle,TW_TMODE_RIGHT,dialog_cset.footer);
  twGetWinPos(wdlg,&x1,&y1,&x2,&y2);
  X1 = x1;
  Y1 = y1;
  X2 = x2;
  Y2 = y2;
  X1 += 2;
  X2 -= 2;
  Y1 += 2;
  Y2 = Y1;
  ewnd = CreateEditor(X1,Y1,X2,Y2,TWS_CURSORABLE | TWS_NLSOEM);
  twUseWin(wdlg);
  twGotoXY(2,1); twPutS(prompt);
  strcpy(estr,buff);
  twShowWin(ewnd);
  twUseWin(ewnd);
  while(1)
  {
   key = xeditstring(estr,NULL,76,NULL);
   if(key == KE_ESCAPE || key == KE_F(10)) { ret = False; break; }
   else
     if(key == KE_ENTER) { ret = True; break; }
  }
  if(ret) strcpy(buff,estr);
  CloseWnd(ewnd);
  CloseWnd(wdlg);
  return ret;
}

