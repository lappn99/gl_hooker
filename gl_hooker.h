#ifndef _GLHOOKER_H
#define _GLHOOKER_H

#include <stdbool.h>
#include <stddef.h>

typedef enum
{
    GLHOOK_INLINE,
    GLHOOK_INTERCEPT
} GLHookerHookType;

typedef struct 
{
    GLHookerHookType hook_type;
    char src_func_name[64];
    void* dst_func;
} GLHookerRegisterHookDesc;

bool glhooker_init(void);
inline char* glhooker_getoriginalname(void*);
bool glhooker_registerhook(const GLHookerRegisterHookDesc*);

#define GLHOOKER_GETHOOKADDR() (__builtin_return_address(0) - 0x2e)

#endif //_GLHOOKER_H