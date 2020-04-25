/* OS/2 API includes */
#define  INCL_BASE
#include <os2.h>

/* osFree internal includes */
#include <os3/stacksw.h>
#include <os3/loader.h>
#include <os3/dataspace.h>
#include <os3/handlemgr.h>
#include <os3/segment.h>
#include <os3/thread.h>
#include <os3/types.h>
#include <os3/cpi.h>
#include <os3/kal.h>
#include <os3/rm.h>
#include <os3/io.h>

/* libc includes */
#include <stdio.h> // sprintf
#include <string.h>

extern l4_os3_thread_t me;

extern unsigned long __stack;
/* Thread IDs array        */
l4_os3_thread_t ptid[MAX_TID];
/* application info blocks */
PTIB ptib[MAX_TID];
PPIB ppib;
/* entry point, stack and 
   other module parameters */
os2exec_module_t s;
/* old FS selector value   */
extern unsigned short old_sel;
/* new TIB FS selector     */
extern unsigned short tib_sel;

extern vmdata_t *areas_list;

extern ULONG rcCode;

VOID CDECL Exit(ULONG action, ULONG result);

USHORT tramp(PCHAR argv, PCHAR envp, ULONG hmod, USHORT tib_sel, void *eip);

/* OS/2 app main thread */
int
trampoline(struct param *param)
{
  PCHAR argv = param->pib->pib_pchcmd;
  PCHAR envp = param->pib->pib_pchenv;
  ULONG hmod = param->pib->pib_hmte;

  unsigned long     base;
  struct desc       desc;
  int               i;

  /* TIB base */
  base = (unsigned long)param->tib;
  io_log("ptib[0]=%x\n", ptib[0]);

  /* Prepare TIB GDT descriptor */
  desc.limit_lo = 0x30; desc.limit_hi = 0;
  desc.acc_lo   = 0xF3; desc.acc_hi   = 0;
  desc.base_lo1 = base & 0xffff;
  desc.base_lo2 = (base >> 16) & 0xff;
  desc.base_hi  = base >> 24;

  /* Allocate a GDT descriptor */
  segment_gdt_set(&desc, sizeof(struct desc), 0, me);

  /* Get a selector */
  tib_sel = (sizeof(struct desc)) * segment_gdt_get_entry_offset();
  tib_sel |= 3; // ring3 GDT descriptor

  io_log("sel=%x\n", tib_sel);

  /* save the previous stack to __stack
     and set current one to OS/2 app stack */
  STKINIT(s.sp - 0x10)

  io_log("entry point: %x\n", param->eip);
  io_log("stack top: %x\n", param->esp);

  /* We have changed the stack so it now points to our LX image. */
  //enter_kdebug("debug");
  old_sel = tramp(argv, envp, hmod, tib_sel, param->eip);

  STKOUT

  return 0;
}

/* Job File Table (local file handles) */
extern HANDLE_TABLE jft;

/* JFT entry */
typedef struct _Jft_Entry
{
  struct _RTL_HANDLE *pNext;
  HFILE sfn;      /* system file number (global file handle) */
} Jft_Entry;

struct options
{
  char  use_events;
  const char  *progname;
  const char  *term;
};

char buf[1024];
l4_os3_thread_t thread;
struct param param;
APIRET rc;

unsigned long base, size;
struct desc    desc;

HFILE hf0, hf1, hf2;
Jft_Entry *jft_entry;
ULONG ulAction;
unsigned long hmod;
ULONG curdisk, map;
unsigned long ulActual;
/* Error info from LoadModule */
char *p = buf;
int i;

APIRET CDECL KalStartApp(struct options *opts, char *pszLoadError, ULONG cbLoadError)
{
  // create LDT for tiled area
  for (i = 0; i < 8192; i++)
  {
      int size = 0x10000;

      base = i * size;

      desc.limit_lo = size & 0xffff; desc.limit_hi = size >> 16;
      desc.acc_lo   = 0xF3;          desc.acc_hi   = 0;
      desc.base_lo1 = base & 0xffff;
      desc.base_lo2 = (base >> 16) & 0xff;
      desc.base_hi  = base >> 24;

      segment_ldt_set(&desc, sizeof(struct desc), i, me);
  }

  /* Load the LX executable */
  rc = KalPvtLoadModule(pszLoadError, &cbLoadError,
                        opts->progname, &s, &hmod);

  rcCode = rc;

  if (rc)
  {
    io_log("LX load error!\n");
    CPClientAppNotify2(&s, "os2app", &thread,
                       pszLoadError, cbLoadError, rcCode);
    KalExit(1, 1);
  }

  io_log("LX loaded successfully\n");

  io_log("eip: %x\n", s.ip);
  io_log("esp: %x\n", s.sp);

  param.eip = s.ip;
  param.esp = s.sp;

  strcpy(s.path, opts->progname);

  /* notify OS/2 server about parameters got from execsrv */
  CPClientAppNotify2(&s, "os2app", &thread,
                     pszLoadError, cbLoadError, rcCode);

  STKINIT(__stack - 0x800)

  /* notify OS/2 server about parameters got from execsrv */
  CPClientAppNotify1();

  rc = KalQueryCurrentDisk(&curdisk, &map);

  if (rc)
  {
    io_log("Cannot get the current disk!\n");
    return rc;
  }

  param.curdisk = curdisk;

  io_log("Attached to a terminal: %s\n", opts->term);

  /* open file descriptors for stdin/stdout/stderr */
  if ( KalOpenL((char *)opts->term,
                &hf0,
                &ulAction,
                0,
                0,
                OPEN_ACTION_FAIL_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS,
                OPEN_FLAGS_FAIL_ON_ERROR | OPEN_SHARE_DENYNONE |
                OPEN_ACCESS_READONLY,
                NULL) ||
       KalOpenL((char *)opts->term,
                &hf1,
                &ulAction,
                0,
                0,
                OPEN_ACTION_FAIL_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS,
                OPEN_FLAGS_FAIL_ON_ERROR | OPEN_SHARE_DENYNONE |
                OPEN_ACCESS_WRITEONLY,
                NULL) ||
       KalOpenL((char *)opts->term,
                &hf2,
                &ulAction,
                0,
                0,
                OPEN_ACTION_FAIL_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS,
                OPEN_FLAGS_FAIL_ON_ERROR | OPEN_SHARE_DENYNONE |
                OPEN_ACCESS_WRITEONLY,
                NULL) )
  {
    io_log("Can't open stdio file descriptors, exiting...\n");
    Exit(1, 1);
  }

  io_log("Successfully allocated stdio file descriptors.\n");

  ptid[0] = KalNativeID();

  io_log("@@@ ptid[0].thread.id.task=%u, ptid[0].thread.id.lthread=%u\n",
         ptid[0].thread.id.task, ptid[0].thread.id.lthread);

  // initialize TIDs array
  for (i = 1; i < MAX_TID; i++)
    ptid[i] = INVALID_THREAD;

  /* get the info blocks (needed by C startup code) */
  rc = KalMapInfoBlocks(&ptib[0], &ppib);

  // initialize TIB pointers array
  for (i = 1; i < MAX_TID; i++)
    ptib[i] = NULL;

  param.pib = ppib;
  param.tib = ptib[0];

  io_log("ppib=%lx, ptib=%lx\n", ppib, ptib[0]);
  io_log("ppib->pib_pchcmd=%lx\n", ppib->pib_pchcmd);
  io_log("ppib->pib_pchenv=%lx\n", ppib->pib_pchenv);

  // dump region mapper regions
  RegDumpRegions();

  // write PID to the screen
  sprintf(p, "The process id is %lx\n", ppib->pib_ulpid);
  KalWrite(1, p, strlen(p) + 1, &ulActual);

  io_log("Starting %s LX exe...\n", opts->progname);
  rc = trampoline (&param);
  io_log("... %s finished.\n", opts->progname);

  // unload exe module
  KalFreeModule(hmod);

  STKOUT

  /* wait for our termination */
  KalExit(1, 0); // successful return

  /* Free all the memory allocated by the process */
  //for (ptr = areas_list; ptr; ptr = ptr->next)
    //KalFreeMem(ptr->addr);

  return NO_ERROR;
}
