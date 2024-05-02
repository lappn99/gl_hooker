#define _GNU_SOURCE
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
    GLHookerHookType hook_type;

} Hook;

typedef struct GLHooker
{
    Hook* hooks;
    size_t num_hooks;
    void* libgl;

} GLHooker;

static bool install_inline_hook(void*, void*);
static void* getprocaddress(const GLubyte*);
static void* generate_relay_function(void*, void*, long);
static void add_hook(void* addr, void* dest, const char* name, GLHookerHookType type);

static GLHooker gl_hooker;

static const char LIBGL_SO[] = "libGL.so";

#ifndef GLHOOKER_NO_LOG
#define GLHOOKER_SYSCALLERROR(FUNC,FMT){fprintf(stderr,FMT);fprintf(stderr," %s(): %s \n", #FUNC, strerror(errno));}
#define GLHOOKER_SYSCALLERRORV(FUNC,FMT, ...){fprintf(stderr,FMT,__VA_ARGS__);fprintf(stderr," %s() : %s\n", #FUNC, strerror(errno));}
#define GLHOOKER_ERROR(FMT, ...){fprintf(stderr," GLHooker::Error: %s\n",__VA_ARGS__);}
#else
#define FATAL_ERROR()
#endif

typedef void* (*GLGetProcAddress)(GLubyte*) ;



bool
glhooker_init(void)
{
    
    gl_hooker.hooks = NULL;
    gl_hooker.num_hooks = 0;

    fprintf(stderr,"Initializing GLHooker\n");

    if((gl_hooker.libgl = dlopen(LIBGL_SO, RTLD_LAZY | RTLD_NODELETE)) == NULL)
    {
        GLHOOKER_SYSCALLERRORV(dlopen, "Could not open %s:", LIBGL_SO);
        return false;
    }

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
            gl_hooker.hooks[i].hook_type = GLHOOK_INLINE;
            

        }
        else 
        {
            return false;
        }
    }

    return true;

}


char* 
glhooker_getoriginalname(void* addr)
{
    for(int i = 0; i < gl_hooker.num_hooks; i++)
    {
        
        if(gl_hooker.hooks[i].relay_addr == addr)
        {
            return &gl_hooker.hooks[i].name[0];
        }
    }
    return NULL;
}

bool 
glhooker_registerhook(const GLHookerRegisterHookDesc* desc)
{
    if(desc->dst_func == NULL)
    {
        return false;
    }
    if(desc->src_func_name[0] == '\0')
    {
        fprintf(stdout,"Registering hook for [ALL]. Address: %p. Type:%d.\n", desc->dst_func,desc->hook_type);
    }
    else 
    {
        fprintf(stdout,"Registering hook for %s. Address: %p. Type:%d.\n", desc->src_func_name, desc->dst_func,desc->hook_type);
    }

    add_hook(NULL,desc->dst_func, desc->src_func_name,desc->hook_type);
    return true;
}

static void 
add_hook(void* addr, void* dest, const char* name, GLHookerHookType type)
{
    
    gl_hooker.num_hooks++;
    gl_hooker.hooks = realloc(gl_hooker.hooks,sizeof(struct Hook) * gl_hooker.num_hooks);
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
    gl_hooker.hooks[gl_hooker.num_hooks - 1].hook_type = type;
}

static bool
install_inline_hook(void* src, void* dst)
{
  
    long pagesize = sysconf(_SC_PAGE_SIZE);
    char jmp[] = {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    memcpy(&jmp[6], &dst, sizeof(void*));
    char* p = (char*)src;
    p = (char*) ((size_t)p & ~(pagesize - 1));
    if(mprotect(p,pagesize,PROT_WRITE | PROT_READ | PROT_EXEC) == -1)
    {
        goto install_inline_hook_fail;
    }
    memcpy(src,jmp,sizeof(jmp));
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
    long pagesize = sysconf(_SC_PAGE_SIZE);
    void* proc_address = dlsym(gl_hooker.libgl, (const char*) proc);

    if(proc_address == NULL)
    {
        GLHOOKER_SYSCALLERRORV(dlsym, "Could not get original OpenGL function: %s", proc);
        return NULL;
    }

    Hook* hook = NULL;
    
    int num_hooks = gl_hooker.num_hooks;
    for(int i = 0; i < num_hooks; i++)
    {
        hook = &gl_hooker.hooks[i];
        if(strlen(gl_hooker.hooks[i].name) == 0)
        {
            add_hook(proc_address,gl_hooker.hooks[i].dest,(const char*) proc,gl_hooker.hooks[i].hook_type);
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

    fprintf(stdout,"Installing hook on: %s\n", hook->name);
    memcpy(&hook->src, &proc_address, sizeof(void*));

    if(hook->hook_type == GLHOOK_INLINE)
    {
        hook->relay_addr = NULL;
        if(!install_inline_hook(hook->src, hook->dest))
        {
            GLHOOKER_ERROR("Could not install inline hook on: %s", proc);
        }
        return proc_address;
    }
    void* relay_func = generate_relay_function(proc_address,hook->dest,pagesize);
    if(relay_func == NULL)
    {
        goto hook_install_fail;
    }

    char* page = relay_func;
    hook->relay_addr = relay_func;
    page = (char*) ((size_t)page & ~(pagesize - 1));
    fprintf(stdout,"Redirecting %s to %p. Original address: %p\n", proc,relay_func,proc_address);

    return relay_func;
hook_install_fail:
    GLHOOKER_ERROR("Failed installing hook for %s. Using original function.\n", proc);
    return proc_address;
}





//Very cursed
static void* 
generate_relay_function(void* src, void* dst, long pagesize)
{   
    char save_args[] = {0x49, 0xBB, 0xA0, 0x16, 0xB6, 0xF7, 0xFF, 0x7F, 0x00, 0x00, 0x49, 0x89, 0x3B, 0x49, 0x89, 0x73, 0x08, 0x49, 0x89, 0x53, 0x10, 0x49, 0x89, 0x4B, 0x18, 0x4D, 0x89, 0x43, 0x20, 0x4D, 0x89, 0x4B, 0x28};
    
    char absolute_jump[] = {
        
        0x49, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //mov r10, addr
        0x41, 0xFF, 0xD2, //Call
        0x49, 0xBB, 0xA0, 0x16, 0xB6, 0xF7, 0xFF, 0x7F, 0x00, 0x00, 0x49, 0x8B, 0x3B, 0x49, 0x8B, 0x73, 0x08, 0x49, 0x8B, 0x53, 0x10, 0x49, 0x8B, 0x4B, 0x18, 0x4D, 0x8B, 0x43, 0x20, 0x4D, 0x8B, 0x4B, 0x28, 
        0x49, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x41, 0xFF, 0xE2,

    };

    char data[sizeof(save_args) + sizeof(absolute_jump)] = {0x90};
    char* func = mmap(NULL, sizeof(data) + sizeof(void*) * 9, PROT_EXEC | PROT_READ | PROT_WRITE,MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if(func == MAP_FAILED)
    {
        perror("mmap()");
        return NULL;
    }

    void* stack_address = func + sizeof(data);
    memcpy(&absolute_jump[2], &dst,sizeof(void*));
    memcpy(&absolute_jump[33 + 13 + 2],&src,sizeof(void*));
    memcpy(&absolute_jump[13 + 2], &stack_address,sizeof(void*));
    memcpy(&save_args[2], &stack_address, sizeof(void*));
    memcpy(&data[0],&save_args[0],sizeof(save_args));
    memcpy(&data[sizeof(save_args)], &absolute_jump[0],sizeof(absolute_jump));
    memcpy(func, data,sizeof(data));
    
    return func;

}