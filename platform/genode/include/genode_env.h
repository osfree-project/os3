#ifndef __GENODE_ENV_H__
#define __GENODE_ENV_H__

/* Genode includes */
#include <base/allocator.h>
#include <base/component.h>

extern Genode::Env *env_ptr;
extern Genode::Allocator *alloc_ptr;

inline Genode::Env &genode_env()
{
    if (env_ptr)
        return *env_ptr;

    throw 1;
}

inline Genode::Allocator &genode_alloc()
{
    if (alloc_ptr)
        return *alloc_ptr;

    throw 1;
}

inline void init_genode_env(Genode::Env &env, Genode::Allocator &alloc)
{
    env_ptr = &env;
    alloc_ptr = &alloc;
}

#endif /* __GENODE_ENV_H__ */
