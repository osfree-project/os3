/* Thread API's */

#define  INCL_BASE
#include <os2.h>

#include <os3/io.h>
#include <os3/kal.h>
#include <os3/thread.h>
#include <os3/segment.h>

/* Last used thread id */
static ULONG ulThread = 1;

void exit_func(l4_os3_thread_t tid, void *data);
static void thread_func(void *data);

/* Thread IDs array               */
extern l4_os3_thread_t ptid[MAX_TID];
/* Thread Info Block pointer      */
extern PTIB ptib[MAX_TID];

l4_os3_thread_t KalNativeID(void)
{
  l4_os3_thread_t id;
  id = ThreadMyself();
  return id;
}

struct start_data
{
  PFNTHREAD pfn;
  ULONG param;
};

static void thread_func(void *data)
{
  struct start_data *start_data = (struct start_data *)data;
  PFNTHREAD pfn = start_data->pfn;
  ULONG param   = start_data->param;
  l4_os3_thread_t   thread;
  unsigned long     base;
  unsigned short    sel;
  struct desc       desc;
  PID pid;
  TID tid;

  // get current process id
  KalGetPID(&pid);
  // get current thread id
  KalGetTID(&tid);
  // get l4thread thread id
  KalGetNativeID(pid, tid, &thread);

  /* initialize tid array */
  ptid[tid - 1] = thread;

  /* TIB base */
  base = (unsigned long)ptib[tid - 1];

  /* Prepare TIB GDT descriptor */
  desc.limit_lo = 0x30; desc.limit_hi = 0;
  desc.acc_lo   = 0xF3; desc.acc_hi   = 0;
  desc.base_lo1 = base & 0xffff;
  desc.base_lo2 = (base >> 16) & 0xff;
  desc.base_hi  = base >> 24;

  /* Allocate a GDT descriptor */
  //fiasco_gdt_set(&desc, sizeof(struct desc), 0, thread.thread);
  segment_gdt_set(&desc, sizeof(struct desc), 0, thread);

  /* Get a selector */
  sel = (sizeof(struct desc)) * segment_gdt_get_entry_offset();

  // set fs register to TIB selector
  //enter_kdebug("debug");
  asm("pushw %%dx \n"
      "movw  %[sel], %%dx \n"
      "movw  %%dx, %%fs \n"
      "popw  %%dx \n"
      :
      :[sel]  "m"  (sel));

  // execute OS/2 thread function
  (*pfn)(param);

  // signal exit to parent thread
  exit_func(thread, data);
}

APIRET CDECL
KalCreateThread(PTID tid,
                PFNTHREAD pfn,
                ULONG param,
                ULONG flag,
                ULONG cbStack)
{
  ULONG flags = THREAD_ASYNC;
  APIRET rc;
  l4_os3_thread_t thread;
  struct start_data data;
  PTIB tib;
  PID pid;

  KalEnter();

  if (flag & STACK_COMMITED)
    flags |= THREAD_MAP;

  data.pfn = pfn;
  data.param = param;

  if ( !(rc = ThreadCreateLong(&thread, thread_func, &data, flags,
                              "OS/2 thread", cbStack)) )
  {
    // @todo watch the thread ids to be in [1..128] range
    ulThread++;
    *tid = ulThread;

    // get pid
    KalGetPID(&pid);
    // crsate TIB, update PTDA
    //thread.thread = l4thread_l4_id(rc);
    KalNewTIB(pid, ulThread, thread);
    // get new TIB
    KalGetTIB(&ptib[ulThread - 1]);
    tib = ptib[ulThread - 1];
    tib->tib_eip_saved = 0;
    tib->tib_esp_saved = 0;

    // suspend thread if needed
    if (flag & CREATE_SUSPENDED)
      KalSuspendThread(ulThread);

    rc = NO_ERROR;
  }
  else
  {
    io_log("Thread creation error: %d\n", rc);
  }

  KalQuit();
  return rc;
}

APIRET CDECL
KalSuspendThread(TID tid)
{
  ULONG eip, esp;
  l4_os3_thread_t id;
  PTIB tib;
  PID pid;

  KalEnter();

  // get pid
  KalGetPID(&pid);
  // get L4 native thread id
  KalGetNativeID(pid, tid, &id);

  if (ThreadEqual(id, INVALID_THREAD))
  {
    KalQuit();
    return ERROR_INVALID_THREADID;
  }

  ThreadSuspend(id, &eip, &esp);

  tib = ptib[tid - 1];
  tib->tib_eip_saved = eip;
  tib->tib_esp_saved = esp;
  KalQuit();
  return NO_ERROR;
}

APIRET CDECL
KalResumeThread(TID tid)
{
  ULONG eip, esp;
  l4_os3_thread_t id;
  PTIB tib;
  PID pid;

  KalEnter();

  // get pid
  KalGetPID(&pid);
  // get L4 native thread id
  KalGetNativeID(pid, tid, &id);

  if (ThreadEqual(id, INVALID_THREAD))
  {
    KalQuit();
    return ERROR_INVALID_THREADID;
  }

  tib = ptib[tid - 1];

  if (! tib->tib_eip_saved)
    return ERROR_NOT_FROZEN;

  eip = tib->tib_eip_saved;
  esp = tib->tib_esp_saved;

  ThreadResume(id, &eip, &esp);

  tib->tib_eip_saved = 0;
  tib->tib_esp_saved = 0;
  KalQuit();
  return NO_ERROR;
}

APIRET CDECL
KalWaitThread(PTID ptid, ULONG option)
{
  l4_os3_thread_t src;
  l4_os3_thread_t id;
  APIRET        rc = NO_ERROR;
  TID           tid = 0;
  PID           pid;

  KalEnter();

  // get pid
  KalGetPID(&pid);

  if (! ptid)
    ptid = &tid;

  // get native L4 id
  KalGetNativeID(pid, *ptid, &id);

  if (ThreadEqual(id, INVALID_THREAD))
  {
    rc = ERROR_INVALID_THREADID;
    KalQuit();
    return rc;
  }

  // wait until needed thread terminates
  switch (option)
  {
    case DCWW_WAIT:
      src = ThreadWait(id);
      if (! ThreadEqual(src, INVALID_THREAD))
      {
        KalGetTIDNative(src, ptid);
      }
      break;

    case DCWW_NOWAIT: // ???
      if (ThreadEqual(id, INVALID_THREAD))
        rc = ERROR_INVALID_THREADID;
      else
        rc = ERROR_THREAD_NOT_TERMINATED;
      break;

    default:
      rc = ERROR_INVALID_PARAMETER;
  }

  KalQuit();
  return rc;
}

APIRET CDECL
KalKillThread(TID tid)
{
  l4_os3_thread_t id;
  PID pid;
  APIRET rc = NO_ERROR;

  KalEnter();

  // get current task pid
  KalGetPID(&pid);
  // get L4 native thread ID
  KalGetNativeID(pid, tid, &id);

  if (ThreadEqual(id, INVALID_THREAD))
  {
    KalQuit();
    return ERROR_INVALID_THREADID;
  }

  ThreadKill(id);

  // free thread TIB
  KalDestroyTIB(pid, tid);

  KalQuit();
  return rc;
}
