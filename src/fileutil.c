/**
 * @namespace   biew
 * @file        fileutil.c
 * @brief       This file contains file utilities of BIEW project.
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
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "bmfile.h"
#include "tstrings.h"
#include "biewutil.h"
#include "bconsole.h"
#include "reg_form.h"
#include "biewlib/bbio.h"
#include "biewlib/twin.h"
#include "biewlib/kbd_code.h"

static tBool ChSize( void )
{
 __fileoff_t psize,tile = 0;
#if __WORDSIZE >= 32
 if(Get16DigitDlg(" Change size of file ","Num. of bytes (+-dec):",3,(__fileoff_t *)&tile))
#else
 if(Get8DigitDlg(" Change size of file ","Num. of bytes (+-dec):",3,(unsigned long *)&tile))
#endif
 {
  if(tile != 0)
  {
    psize = BMGetFLength();
    psize += tile;
    if(psize > 0)
    {
       tBool ret;
       int my_errno = 0;
       char *fname = BMName();
       BGLOBAL bHandle;
       bHandle = biewOpenRW(fname,BBIO_SMALL_CACHE_SIZE);
       if(bHandle == &bNull)
       {
         err:
         errnoMessageBox(RESIZE_FAIL,NULL,my_errno);
         return False;
       }
       ret = bioChSize(bHandle,psize);
       my_errno = errno;
       bioClose(bHandle);
       if(ret == False) goto err;
       BMReRead();
       return ret;
    }
    else ErrMessageBox("Invalid new length",NULL);
  }
 }
 return False;
}

extern char shortname[];

static tBool FileInfo( void )
{
  TWindow* wnd;
  struct stat statbuf;
  unsigned evt;
  char attr[14];
  char stimes[3][80];
  memset(&statbuf,0,sizeof(struct stat));
  stat(BMName(),&statbuf);
  memset(attr,'-',sizeof(attr));
  attr[sizeof(attr)-1] = 0;
#ifdef S_IXOTH /** Execute by other */
  if((statbuf.st_mode & S_IXOTH) == S_IXOTH) attr[12] = 'X';
#endif
#ifdef S_IWOTH /** Write by other */
  if((statbuf.st_mode & S_IWOTH) == S_IWOTH) attr[11] = 'W';
#endif
#ifdef S_IROTH /** Read by other */
  if((statbuf.st_mode & S_IROTH) == S_IROTH) attr[10] = 'R';
#endif
#ifdef S_IXGRP /** Execute by group */
  if((statbuf.st_mode & S_IXGRP) == S_IXGRP) attr[9] = 'X';
#endif
#ifdef S_IWGRP /** Write by group */
  if((statbuf.st_mode & S_IWGRP) == S_IWGRP) attr[8] = 'W';
#endif
#ifdef S_IRGRP /** Read by group */
  if((statbuf.st_mode & S_IRGRP) == S_IRGRP) attr[7] = 'R';
#endif
#ifdef S_IEXEC /** Execute by owner */
  if((statbuf.st_mode & S_IEXEC) == S_IEXEC) attr[6] = 'X';
#endif
#ifdef S_IWRITE /** Write by owner */
  if((statbuf.st_mode & S_IWRITE) == S_IWRITE) attr[5] = 'W';
#endif
#ifdef S_IREAD /** Read by owner */
  if((statbuf.st_mode & S_IREAD) == S_IREAD) attr[4] = 'R';
#endif
#ifdef S_ISVTX /** Save swapped text after use (obsolete) */
  if((statbuf.st_mode & S_ISVTX) == S_ISVTX) attr[3] = 'V';
#endif
#ifdef S_ISGID /** Set GID on execution */
  if((statbuf.st_mode & S_ISGID) == S_ISGID) attr[2] = 'G';
#endif
#ifdef S_ISUID /** Set UID on execution */
  if((statbuf.st_mode & S_ISUID) == S_ISUID) attr[1] = 'U';
#endif
#ifdef S_ISFIFO /** it FIFO */
  if(S_ISFIFO(statbuf.st_mode)) attr[0] = 'F';
#endif
#ifdef S_IFCHR /** it character device */
  if((statbuf.st_mode & S_IFCHR) == S_IFCHR) attr[0] = 'C';
#endif
#ifdef S_IFDIR /** it directory */
  if((statbuf.st_mode & S_IFDIR) == S_IFDIR) attr[0] = 'D';
#endif
#ifdef S_IFBLK /** it block device */
  if((statbuf.st_mode & S_IFBLK) == S_IFBLK) attr[0] = 'B';
#endif
#ifdef S_IFREG /** it regular file (not dir or node) */
  if((statbuf.st_mode & S_IFREG) == S_IFREG) attr[0] = 'f';
#endif
#ifdef S_IFLNK /** it symbolic link */
  if((statbuf.st_mode & S_IFLNK) == S_IFLNK) attr[0] = 'L';
#endif
#ifdef S_IFSOCK /** it socket */
  if((statbuf.st_mode & S_IFSOCK) == S_IFSOCK) attr[0] = 'S';
#endif
  wnd = CrtDlgWndnls(" File information: ",tvioWidth-5,13);
  twGotoXY(1,1);
  strcpy(stimes[0],ctime(&statbuf.st_ctime));
  strcpy(stimes[1],ctime(&statbuf.st_mtime));
  strcpy(stimes[2],ctime(&statbuf.st_atime));
  twPrintF("Name                          = %s\n"
           "Type                          = %s\n"
#if (__WORDSIZE >= 32) && !defined(__QNX4__)
           "Length                        = %llu bytes\n"
#else
           "Length                        = %lu bytes\n"
#endif
           "Attributes                    = %s\n"
           "                                TUGVOwnGrpOth\n"
           "Creation time                 = %s"
           "Modification time             = %s"
           "Last access time              = %s"
           "Device containing file        = %u\n"
           "File serial (inode) number    = %u\n"
           "Number of hard links to file  = %u\n"
           "User ID of the file owner     = %u\n"
           "Group ID of the file owner    = %u"
           ,shortname
           ,detectedFormat->name ? detectedFormat->name : "Not detected"
           ,BMGetFLength()
           ,attr
           ,stimes[0]
           ,stimes[1]
           ,stimes[2]
           ,(unsigned)statbuf.st_dev
           ,(unsigned)statbuf.st_ino
           ,(unsigned)statbuf.st_nlink
           ,(unsigned)statbuf.st_uid
           ,(unsigned)statbuf.st_gid);
  do
  {
    evt = GetEvent(drawEmptyPrompt,NULL,wnd);
  }
  while(!(evt == KE_ESCAPE || evt == KE_F(10)));
  CloseWnd(wnd);
  return False;
}

static const char * fu_names[] =
{
  "~File information...",
  "C~hange size of file"
};

typedef tBool (*FileFunc)( void );

static FileFunc fu_funcs[] =
{
  FileInfo,
  ChSize
};

tBool FileUtils( void )
{
  size_t nUtils;
  int retval;
  tBool ret;
  static unsigned def_sel = 0;
  nUtils = sizeof(fu_names)/sizeof(char *);
  retval = SelBoxA(fu_names,nUtils," File utilities: ",def_sel);
  if(retval != -1)
  {
     TWindow * w;
     w = PleaseWaitWnd();
     ret = (*fu_funcs[retval])();
     CloseWnd(w);
     def_sel = retval;
     return ret;
  }
  return False;
}
