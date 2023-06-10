/* OS/2 API includes */
#define  INCL_BASE
#include <os2.h>

/* osFree internal */
#include <os3/lock.h>
#include <os3/io.h>

/* Genode includes */
#include <base/allocator.h>
#include <base/mutex.h>

/* local includes */
#include <genode_env.h>

extern "C" APIRET
LockInit(l4_os3_lock_t **lock, ULONG n)
{
    Genode::Allocator &alloc = genode_alloc();

    try
    {
        **lock = new (alloc) Genode::Mutex();

        if (n)
            ((Genode::Mutex *)(*lock))->acquire();
    }
    catch (...)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    return NO_ERROR;
}

extern "C" void
LockDone(l4_os3_lock_t *lock)
{
    Genode::Allocator &alloc = genode_alloc();

    destroy(alloc, (Genode::Mutex *)(*lock));
}

extern "C" void
LockLock(l4_os3_lock_t *lock)
{
    ((Genode::Mutex *)(*lock))->acquire();
}

extern "C" void
LockUnlock(l4_os3_lock_t *lock)
{
    ((Genode::Mutex *)(*lock))->release();
}
