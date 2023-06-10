/*! @file ixfmgr.c

    @brief Instalable eXecutable Format Manager. Provides support for
           different executable file formats.

*/

/* OS/2 API includes */
#define INCL_ERRORS
#include <os2.h>

/* osFree OS/2 personality internal */
#include <os3/io.h>
#include <os3/cfgparser.h>
#include <os3/ixfmgr.h>

/* libc includes */
#include <stdlib.h>
#include <string.h>

unsigned long ixfCopyModule(IXFModule *dst, IXFModule *src);

extern cfg_opts options;

IXFHandler *IXFHandlers;
//  {
//    {NEIdentify,  NELoad,  NEFixup},
//    {LXIdentify,  LXLoad,  LXFixup}
//  };

//#define maxFormats (sizeof(IXFHandlers)/sizeof(IXFHandler))

unsigned long
IXFIdentifyModule(void *addr, unsigned long size, IXFModule *ixfModule)
{
  IXFHandler *p;
  unsigned int rc = 0;

  for (p = IXFHandlers; p; p = p->next)
  {
    rc = *(ULONG *)p;
    rc = p->Identify(addr, size);

    if (rc == NO_ERROR)
    {
      ixfModule->Load = p->Load;
      ixfModule->Fixup = p->Fixup;

      if (options.debugixfmgr)
      {
         io_log("IXFIdentifyModule: Format of module identified\n");
      }

      break;
    }
  }

  return rc;
}

unsigned long
IXFLoadModule(void *addr, unsigned long size, IXFModule *ixfModule)
{
  unsigned long ret;

  if (options.debugixfmgr)
  {
     io_log("IXFLoadModule: Loading module.\n");
  }

  io_log("x000: refcnt=%u\n", ixfModule->refcnt);
  if (ixfModule->refcnt)
  {
      return 0;
  }

  ixfModule->refcnt++;
  io_log("x001\n");

  ret = ixfModule->Load(addr, size, ixfModule);

  return ret;
}

unsigned long IXFFixupModule(IXFModule *ixfModule)
{
  unsigned long rc;

  if (options.debugixfmgr)
     io_log("IXFFixupModule: Fixing up module\n");

  rc = ixfModule->Fixup(ixfModule->FormatStruct);

  return rc;
}

unsigned long IXFAllocModule(IXFModule **ixf)
{
    IXFSYSDEP *sysdep;

    if (! ixf)
        return ERROR_INVALID_PARAMETER;

    *ixf = (IXFModule *)malloc(sizeof(IXFModule));

    if (! *ixf)
        return ERROR_NOT_ENOUGH_MEMORY;

    memset(*ixf, 0, sizeof(IXFModule));

    sysdep = (IXFSYSDEP *)malloc(sizeof(IXFSYSDEP));

    if (! sysdep)
        return ERROR_NOT_ENOUGH_MEMORY;

    memset(sysdep, 0, sizeof(IXFSYSDEP));

    (*ixf)->hdlSysDep = (unsigned long long)sysdep;

    return NO_ERROR;
}

unsigned long IXFFreeModule(IXFModule *ixf)
{
    io_log("y000: refcnt=%u\n", ixf->refcnt);

    if (! ixf || ! ixf->hdlSysDep)
        return ERROR_INVALID_PARAMETER;

    io_log("y001\n");

    if (ixf->refcnt)
    {
        ixf->refcnt--;
        return NO_ERROR;
    }

    io_log("y002\n");

    if (ixf->name)
    {
        free(ixf->name);
    }

    if (ixf->FormatStruct)
    {
        free(ixf->FormatStruct);
    }

    if (ixf->Entries)
    {
        free(ixf->Entries);
    }

    if (ixf->Modules)
    {
        free(ixf->Modules);
    }

    if (ixf->Fixups)
    {
        free(ixf->Fixups);
    }

    if (ixf->hdlSysDep)
    {
        free((void *)ixf->hdlSysDep);
    }

    free(ixf);

    return NO_ERROR;
}

unsigned long IXFCopyModule(IXFModule *dst, IXFModule *src)
{
    return ixfCopyModule(dst, src);
}

/* slist_t *
lastelem (slist_t *e)
{
  slist_t *p, *q = 0;
  for (p = e; p; p = p->next) q = p;
  return q;
} */
