/**
 * @namespace   biew_plugins_auto
 * @file        plugins/bin/dos_com.c
 * @brief       This file contains implementation of decoder for DOS .COM
 *              program image.
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
#include <stdio.h>
#include <stddef.h>

#include "bconsole.h"
#include "bmfile.h"
#include "reg_form.h"
#include "biewutil.h"
#include "plugins/disasm.h"

extern char **  ArgVector;

/** .COM is raw 16-bit code loaded at CS:0100h; it carries no signature,
    so the only reliable trait is the file extension. */
static tBool __FASTCALL__ com_check_fmt( void )
{
  const char *name;
  size_t l;
  if(!ArgVector || !ArgVector[1]) return False;
  name = ArgVector[1];
  l = strlen(name);
  if(l <= 4) return False;
  return stricmp(&name[l-4],".com") == 0 ? True : False;
}

static void __FASTCALL__ com_init_fmt( void ) {}
static void __FASTCALL__ com_destroy_fmt(void) {}
static int  __FASTCALL__ com_platform( void) { return DISASM_CPU_IX86; }
static int  __FASTCALL__ com_bitness(__filesize_t pa)
{
  UNUSED(pa);
  return DAB_USE16;
}

static __filesize_t __FASTCALL__ comVA2PA(__filesize_t va)
{
  return va >= 0x100 ? va - 0x100 : 0;
}

static __filesize_t __FASTCALL__ comPA2VA(__filesize_t pa)
{
  return pa + 0x100;
}

REGISTRY_BIN comTable =
{
  "DOS COM-file",
  { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  com_check_fmt,
  com_init_fmt,
  com_destroy_fmt,
  NULL,
  NULL,
  NULL,
  com_platform,
  com_bitness,
  NULL,
  NULL,
  comVA2PA,
  comPA2VA,
  NULL,
  NULL,
  NULL,
  NULL
};
