/* osFree internal */
#include <os3/thread.h>

/* local includes */
#include <genode_env.h>

Genode::Env *env_ptr;
Genode::Allocator *alloc_ptr;

extern "C"
void AppClientInitEnv(Genode::Env &env,
                      Genode::Allocator &alloc)
{
    init_genode_env(env, alloc);
}

extern "C"
void AppClientNotify(l4_os3_thread_t client_id)
{
    client_id = client_id;
}
