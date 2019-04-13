/* threading interface */

/* OS/2 API includes */
#define  INCL_BASE
#include <os2.h>

/* osFree internal */
#include <os3/io.h>
#include <os3/thread.h>
#include <os3/processmgr.h>

/* l4env includes */
#include <l4/sys/types.h>
#include <l4/env/errno.h>
#include <l4/util/util.h>
#include <l4/thread/thread.h>
#include <l4/generic_ts/generic_ts.h>


BOOL ThreadEqual(l4_os3_thread_t one, l4_os3_thread_t two)
{
    return l4_thread_equal(one.thread, two.thread);
}

BOOL TaskEqual(l4_os3_thread_t one, l4_os3_thread_t two)
{
    return (one.thread.id.task == two.thread.id.task);
}

void ThreadSleep(unsigned long ms)
{
    l4_sleep(ms);
}

void ThreadExit(void)
{
    l4thread_exit();
}

void TaskExit(int result)
{
    l4ts_exit();
}

l4_os3_thread_t ThreadMyself(void)
{
    l4_os3_thread_t id;
    id.thread = l4_myself();
    return id;
}

APIRET ThreadCreate(l4_os3_thread_t *thread,
                    ThreadFunc fn, void *data,
                    ULONG flags)
{
    l4thread_t rc;
    ULONG flag;

    io_log("000\n");
    //if (flags & THREAD_MAP)
    //    flag |= L4THREAD_CREATE_MAP;

    io_log("001\n");
    if ( (rc = l4thread_create((l4thread_fn_t)fn, data, flag)) < 0 )
    {
        switch (-rc)
        {
        case L4_EINVAL:     rc = ERROR_INVALID_PARAMETER; break;
        case L4_ENOTHREAD:  rc = ERROR_MAX_THRDS_REACHED; break;
        case L4_ENOMAP:
        case L4_ENOMEM:     rc = ERROR_NOT_ENOUGH_MEMORY; break;
        default:            rc = ERROR_INVALID_PARAMETER; // ???
        }

        return rc;
    }

    io_log("002\n");
    thread->thread = l4thread_l4_id(rc);
    io_log("003\n");
    return NO_ERROR;
}

APIRET ThreadCreateLong(l4_os3_thread_t *thread, ThreadFunc fn,
                        void *data, ULONG flags,
                        const char *name, ULONG stacksize)
{
    l4thread_t rc;
    ULONG flag;

    if (flags & THREAD_MAP)
        flag |= L4THREAD_CREATE_MAP;

    if ( (rc = l4thread_create_long(L4THREAD_INVALID_ID, fn, name,
                       L4THREAD_INVALID_SP, stacksize, L4THREAD_DEFAULT_PRIO,
                       &data, flag)) < 0)
    {
        switch (-rc)
        {
        case L4_EINVAL:     rc = ERROR_INVALID_PARAMETER; break;
        case L4_ENOTHREAD:  rc = ERROR_MAX_THRDS_REACHED; break;
        case L4_ENOMAP:
        case L4_ENOMEM:     rc = ERROR_NOT_ENOUGH_MEMORY; break;
        default:            rc = ERROR_INVALID_PARAMETER; // ???
        }

        return rc;
    }

    thread->thread = l4thread_l4_id(rc);
    return NO_ERROR;
}

void ThreadKill(l4_os3_thread_t native)
{
  APIRET rc;

  if (! (rc = l4thread_shutdown(l4thread_id(native.thread))) )
    io_log("thread killed\n");
  else
    io_log("thread kill failed!\n");
}

static void wait_func(void)
{
  l4_threadid_t src;
  l4_umword_t dw1, dw2;
  l4_msgdope_t dope;

  for (;;)
  {
    if ( l4_ipc_wait(&src, L4_IPC_SHORT_MSG,
                     &dw1, &dw2, L4_IPC_NEVER,
                     &dope) < 0 )
    {
      io_log("IPC error\n");
    }
  }
}

void ThreadSuspend(l4_os3_thread_t native,
                   ULONG *eip, ULONG *esp)
{
  l4_threadid_t preempter = L4_INVALID_ID;
  l4_threadid_t pager     = L4_INVALID_ID;
  l4_umword_t eflags; //, eip, esp;
  //PTIB tib;
  //TID tid;

  // suspend thread execution: set eip to -1
  l4_thread_ex_regs(native.thread, (l4_umword_t)wait_func, ~0,
                    &preempter, &pager,
                    &eflags, eip, esp);

  //KalGetTIDNative(native, &tid);

  //tib = ptib[tid - 1];

  //tib->tib_eip_saved = eip;
  //tib->tib_esp_saved = esp;
}

void ThreadResume(l4_os3_thread_t native,
                  ULONG *eip, ULONG *esp)
{
  l4_threadid_t preempter = L4_INVALID_ID;
  l4_threadid_t pager     = L4_INVALID_ID;
  l4_umword_t eflags; //, eip, esp, new_eip, new_esp;
  //PTIB tib;
  //TID tid;

  //KalGetTIDNative(native, &tid);

  //tib = ptib[tid - 1];

  //new_eip = tib->tib_eip_saved;
  //new_esp = tib->tib_esp_saved;

  // resume thread
  l4_thread_ex_regs(native.thread, *eip, *esp,
                    &preempter, &pager,
                    &eflags, eip, esp);

  //tib->tib_eip_saved = 0;
  //tib->tib_esp_saved = 0;
}

l4_os3_thread_t ThreadWait(l4_os3_thread_t native)
{
  l4_threadid_t me = l4_myself();
  l4_os3_thread_t src = INVALID_THREAD;
  l4_umword_t   dw1, dw2;
  l4_msgdope_t  dope;
  TID tid;

  //KalGetTIDNative(native, &tid);

  if (l4_thread_equal(native.thread, L4_INVALID_ID))
  {
    return INVALID_THREAD;
  }

  for (;;)
  {
    if (! l4_ipc_wait(&src.thread, L4_IPC_SHORT_MSG,
                      &dw1, &dw2, L4_IPC_NEVER,
                      &dope) &&
        l4_task_equal(src.thread, me) )
    {
      //if (tid)
      //{
        //if (l4_thread_equal(native.thread, L4_INVALID_ID))
        //{
        //  src = INVALID_THREAD;
        //  break;
        //}

        if (l4_thread_equal(src.thread, native.thread))
          break;
      //}
      //else
      //{
        //KalGetTIDNative(src, ptid);
        //break;
      //}
    }
  }

  return src;
}
