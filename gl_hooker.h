#ifndef _GLHOOKER_H
#define _GLHOOKER_H

#include <stdbool.h>
#include <stddef.h>

struct Hook;
typedef struct Hook* HookHandle;

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
    size_t userdata_size;
    void* userdata;
} GLHookerRegisterHookDesc;

bool glhooker_init(void);
inline char* glhooker_gethookname(HookHandle);
bool glhooker_registerhook(const GLHookerRegisterHookDesc*);
HookHandle glhooker_gethook(const char*);
void* glhooker_getoriginalfunction(HookHandle);
void* glhooker_gethookuserdata(HookHandle);


#define GLHOOKER_GETHOOKADDR() (__builtin_return_address(0) - 0x2e)

#endif //_GLHOOKER_H