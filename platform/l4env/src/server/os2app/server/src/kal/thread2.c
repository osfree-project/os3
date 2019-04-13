/* Thread API's */

#include <os3/kal.h>
#include <os3/thread.h>

#include <l4/sys/syscalls.h>
#include <l4/sys/types.h>
#include <l4/thread/thread.h>
#include <l4/env/errno.h>

void exit_func(l4_os3_thread_t tid, void *data)
{
  l4_threadid_t t = l4thread_l4_id(l4thread_get_parent());
  l4_msgdope_t dope;

  // notify parent about our termination
  l4_ipc_send(t, (void *)(L4_IPC_SHORT_MSG | L4_IPC_DECEIT_MASK),
              l4thread_id(tid.thread), 0, L4_IPC_SEND_TIMEOUT_0, &dope);
}
L4THREAD_EXIT_FN_STATIC(fn, exit_func);
