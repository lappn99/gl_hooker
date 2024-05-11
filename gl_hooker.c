#define _GNU_SOURCE
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <alloca.h>
#include <gnu/lib-names.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "gl_hooker.h"

typedef XID GLXContextID;
typedef XID GLXPixmap;
typedef XID GLXDrawable;
typedef XID GLXPbuffer;
typedef XID GLXWindow;
typedef XID GLXFBConfigID;
typedef struct __GLXcontextRec *GLXContext;
typedef struct __GLXFBConfigRec *GLXFBConfig;

#include <GL/gl.h>
#include <GL/glxext.h>

typedef struct Hook
{
    void* src;
    void* dest;
    void* relay_addr;
    char name[64];
    size_t userdata_size;
    void* userdata;

} Hook;

typedef struct GLHooker
{
    Hook* hooks;
    size_t num_hooks;
    void* libgl;
} GLHooker;

static bool install_inline_hook(void*, void*);
static void* getprocaddress(const GLubyte*);
static void* generate_relay_function(void*, void*);
static void add_hook(void* addr, void* dest, const char* name, size_t, void*);

static GLHooker gl_hooker;

static const char LIBGL_SO[] = "libGL.so";

#ifndef GLHOOKER_NO_LOG
#define GLHOOKER_SYSCALLERROR(FUNC,FMT){fprintf(stderr,FMT);fprintf(stderr," %s(): %s \n", #FUNC, strerror(errno));}
#define GLHOOKER_SYSCALLERRORV(FUNC,FMT, ...){fprintf(stderr,FMT,__VA_ARGS__);fprintf(stderr," %s() : %s\n", #FUNC, strerror(errno));}
#define GLHOOKER_ERROR(FMT, ...){fprintf(stderr,FMT,__VA_ARGS__);}
#else
#define FATAL_ERROR()
#endif

//Catch-all type for glXGetProcAddress[ARB]
typedef void* (*GLGetProcAddress)(GLubyte*);



bool
glhooker_init(void)
{
    
    gl_hooker.hooks = NULL;
    gl_hooker.num_hooks = 0;
    if((gl_hooker.libgl = dlopen(LIBGL_SO, RTLD_LAZY | RTLD_NODELETE)) == NULL)
    {
        GLHOOKER_SYSCALLERRORV(dlopen, "Could not open %s:", LIBGL_SO);
        return false;
    }

    //Need to hook both loader funcs as different libraries will use different ones
    GLGetProcAddress get_proc_address_funcs[2] = {
        dlsym(gl_hooker.libgl,"glXGetProcAddressARB"),
        dlsym(gl_hooker.libgl,"glXGetProcAddress")
    };

    gl_hooker.hooks = malloc(sizeof(struct Hook) * 2);
    for(int i = 0; i < 2; i++)
    {
        if(get_proc_address_funcs[i] == NULL)
        {
            GLHOOKER_SYSCALLERROR(dlsym,"Could not get handle");
            return false;
        }
        else if(install_inline_hook(get_proc_address_funcs[i],getprocaddress))
        {
            gl_hooker.num_hooks++;
            strcpy(&gl_hooker.hooks[i].name[0], "glXGetProcAddress");
            memcpy(&gl_hooker.hooks[i].dest, &getprocaddress,sizeof(void*));
            memcpy(&gl_hooker.hooks[i].src, &get_proc_address_funcs[i], sizeof(void*));
            

        }
        else 
        {
            return false;
        }
    }

    return true;

}


char* 
glhooker_gethookname(HookHandle handle)
{
    if(handle == NULL)
    {
        return NULL;
    }
    return handle->name;
    
}

bool 
glhooker_registerhook(const GLHookerRegisterHookDesc* desc)
{
    
    
    if(gl_hooker.hooks == NULL)
    {
        return false;
    }
    if(desc->dst_func == NULL)
    {
        return false;
    }
    add_hook(NULL,desc->dst_func, desc->src_func_name,desc->userdata_size, desc->userdata);
    return true;
}

void* 
glhooker_gethookoriginalfunction(HookHandle handle)
{
    if(handle == NULL)
    {
        return NULL;
    }
    return handle->src;
}

void* 
glhooker_gethookuserdata(HookHandle handle)
{
    if(handle == NULL)
    {
        return NULL;
    }
    return handle->userdata;
}

HookHandle 
glhooker_gethook(const char* name)
{
    for(int i = 0; i < gl_hooker.num_hooks; i++)
    {
        if(strcmp(gl_hooker.hooks[i].name,name) == 0)
        {
            return &gl_hooker.hooks[i];
        }
    }
    return NULL;
}

static void 
add_hook(void* addr, void* dest, const char* name, size_t userdata_size, void* userdata)
{
    
    gl_hooker.num_hooks++;
    gl_hooker.hooks = realloc(gl_hooker.hooks,sizeof(struct Hook) * gl_hooker.num_hooks);
    //If name is NULL that means the hook should be registered for all of them
    if(name == NULL)
    {
        strcpy(gl_hooker.hooks[gl_hooker.num_hooks - 1].name,"\0");
    }
    else
    {
        strcpy(gl_hooker.hooks[gl_hooker.num_hooks - 1].name, (const char*) name);
    }
    
    memcpy(&gl_hooker.hooks[gl_hooker.num_hooks - 1].src, &addr, sizeof(void*));
    memcpy(&gl_hooker.hooks[gl_hooker.num_hooks - 1].dest,&dest,sizeof(void*));
    gl_hooker.hooks[gl_hooker.num_hooks - 1].userdata_size = userdata_size;
    gl_hooker.hooks[gl_hooker.num_hooks - 1].userdata = calloc(1, userdata_size);
    memcpy(gl_hooker.hooks[gl_hooker.num_hooks - 1].userdata, userdata, userdata_size);
}


static bool
install_inline_hook(void* src, void* dst)
{
    //Get system page size (typically 4096), this is because the address passed into mprotect() needs to be aligned to page boundaries
    long pagesize = sysconf(_SC_PAGE_SIZE);
    //Absoulte jump to 64 bit address
    char jmp[] = {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    memcpy(&jmp[6], &dst, sizeof(void*));

    //Re-align the pointer passed into the SRC paramter to page boundaries
    char* p = (char*)src;
    p = (char*) ((size_t)p & ~(pagesize - 1));
    //Change protections so we can write into it
    if(mprotect(p,pagesize,PROT_WRITE | PROT_READ | PROT_EXEC) == -1)
    {
        goto install_inline_hook_fail;
    }
    //Install hook
    memcpy(src,jmp,sizeof(jmp));
    //Change them back
    if(mprotect(p,pagesize,PROT_READ | PROT_EXEC) == -1)
    {
        goto install_inline_hook_fail;
    }
    
    return true;
install_inline_hook_fail:
    GLHOOKER_SYSCALLERROR(mprotect, "Could not install inline hook");
    return false;
    
}

static void* 
getprocaddress(const GLubyte* proc)
{

    //Get actual address
    //Normal loader function is mangled up so we have to use syscalls.
    void* proc_address = dlsym(gl_hooker.libgl, (const char*) proc);
    
    if(proc_address == NULL)
    {
        GLHOOKER_ERROR("Could not get original OpenGL function: %s: %s", proc, dlerror());
        return NULL;
    }

    Hook* hook = NULL;
    
    //Search for if the asked for function is registered
    int num_hooks = gl_hooker.num_hooks;
    for(int i = 0; i < num_hooks; i++)
    {
        //If hook is supposed to be for all functions
        hook = &gl_hooker.hooks[i];
        if(strlen(gl_hooker.hooks[i].name) == 0)
        {
            //Recursively add them all
            add_hook(proc_address,gl_hooker.hooks[i].dest,(const char*) proc,hook->userdata_size, hook->userdata);
            hook = &gl_hooker.hooks[gl_hooker.num_hooks - 1];
            break;
        }
        if(strcmp((const char*) proc,gl_hooker.hooks[i].name) == 0)
        {
            hook = &gl_hooker.hooks[i];
            break;
        }
        hook = NULL;
        
    }
    if(hook == NULL)
    {
        return proc_address;
    }
    
    //Set original func of registered hook
    //When its first registered the address is not known
    memcpy(&hook->src, &proc_address, sizeof(void*));

    //Generate our stub function
    void* relay_func = generate_relay_function(proc_address,hook->dest);
    if(relay_func == NULL)
    {
        goto hook_install_fail;
    }

    hook->relay_addr = relay_func;

    return relay_func;
hook_install_fail:
    GLHOOKER_ERROR("Failed installing hook for %s. Using original function.\n", proc);
    return proc_address;
}





//Very cursed
static void* 
generate_relay_function(void* src, void* dst)
{   
    char absolute_jump[] = {
        0x49, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //mov r10, addr
        0x41, 0xFF, 0xE2, //Jump
    };

    char data[sizeof(absolute_jump)];
    memset(&data[0], 0x90, sizeof(data)); //Set NOOP
    char* func = mmap(NULL, sizeof(data), PROT_EXEC | PROT_READ | PROT_WRITE,MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if(func == MAP_FAILED)
    {
        perror("mmap()");
        return NULL;
    }

    memcpy(&absolute_jump[2], &dst,sizeof(void*));
    memcpy(data, absolute_jump, sizeof(absolute_jump));
    memcpy(func,data,sizeof(data));

    return func;

}
