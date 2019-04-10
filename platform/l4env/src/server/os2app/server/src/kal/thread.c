/* Thread API's */

#define  INCL_BASE
#include <os2.h>

#include <os3/io.h>
#include <os3/kal.h>
#include <os3/thread.h>
#include <os3/segment.h>

#include <l4/sys/syscalls.h>
#include <l4/sys/types.h>
#include <l4/env/errno.h>

/* Last used thread id */
static ULONG ulThread = 1;

void exit_func(l4thread_t tid, void *data);
static void thread_func(void *data);

/* Thread IDs array               */
extern l4_os3_thread_t ptid[MAX_TID];
/* Thread Info Block pointer      */
extern PTIB ptib[MAX_TID];

l4_os3_thread_t KalNativeID(void)
{
  l4_os3_thread_t id;

  id.thread = l4_myself();
  return id;
}

void exit_func(l4thread_t tid, void *data)
{
  l4_threadid_t t = l4thread_l4_id(l4thread_get_parent());
  l4_msgdope_t dope;

  // notify parent about our termination
  l4_ipc_send(t, (void *)(L4_IPC_SHORT_MSG | L4_IPC_DECEIT_MASK),
              tid, 0, L4_IPC_SEND_TIMEOUT_0, &dope);
}
L4THREAD_EXIT_FN_STATIC(fn, exit_func);
