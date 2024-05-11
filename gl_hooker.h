#ifndef _GLHOOKER_H
#define _GLHOOKER_H

#include <stdbool.h>
#include <stddef.h>

struct Hook;
typedef struct Hook* HookHandle;

typedef struct 
{
    char src_func_name[64]; //Name of function to hook
    void* dst_func; //Hook function address
    size_t userdata_size; //Size data pointed to by USERDATA
    void* userdata; //Pointer to user supplied data for registered hook
} GLHookerRegisterHookDesc;

//Initialize hooking mechanism. Call this first.
bool glhooker_init(void);
//Get NAME of hooked function from registered HANDLE
inline char* glhooker_gethookname(HookHandle);
//Register a hook based on DESC
bool glhooker_registerhook(const GLHookerRegisterHookDesc*);
//Get HOOK by hooked function NAME
HookHandle glhooker_gethook(const char*);
//Get original function ADDR from HOOK
void* glhooker_getoriginalfunction(HookHandle);
//Get USERDATA from HOOK
void* glhooker_gethookuserdata(HookHandle);


#define GLHOOKER_GETHOOKADDR() (__builtin_return_address(0) - 0x2e)

#endif //_GLHOOKER_H