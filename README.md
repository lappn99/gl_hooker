# GL Hooker 

This library provides mechanisms to hook into OpenGL function calls while and leaving (most of them) untouched.

## Building/Installing
`[sudo] make install`, no dependencies needed!

## How it works

This library exploits the unique way in which OpenGL 1.1+ functions are loaded into (most) applications. Under the hood what OpenGL function loader libraries like `GLAD` do is get the address of OpenGL functions from a shared library and assign them to function pointers. Using one of `glXGetProcAddress`, or `glXGetProcAddressARB` ([Khronos doesn't even know which one you should use](https://www.khronos.org/opengl/wiki/Load_OpenGL_Functions#Linux_and_X-Windows)). I dont exactly know what specifically the *implementation* of these do however I imagine it is something similiar to how you normally would do something like this: with functions like `dlopen`, and `dlsym`; to open a shared library and get the address of a symbol respectively. Using this assumption this library hooks into these functions and instead of returning the address of the *actual* function: the address of the hook function is effectively returned instead (see below for more details). Normally to do a hook like this, such as a `trampoline` hook, complicated assembly-foo is needed so that the original function is left intact and can still operate normally before (or after) the hook executes. By exploiting how OpenGL functions are loaded, this makes this *a lot* easier, it also means that it only works for OpenGL however.

The more detailed process for this is as follows:

1. Install `inline`[^1]  hook into `glXGetProcAddress[ARB]`.
2. `glXProcAddress([proc])` is called by the user.
3. In our hook function:
    1. Generate a `relay`[^2] function that simply jumps to the desired hook for `proc`.
    2. Return the address of the `relay` function to the user.
4. When `proc` is called it invokes our relay function which then in turn jumps to the hook function.


## Use
If you wish to use this library for your own purposes, here is a quick example:

> **_NOTE:_** Only for Linux-x64_86

> **_NOTE:_** This library was explicitly used for the purposes of [gltrace](https://github.com/lappn99/gltrace_rs). Check it out!


```

#include <gl_hooker.h>
#include <GL/gl.h> //For function signatures
static void
my_glbindbuffer(GLenum target, GLuint buffer)
{
    fprintf(stderr, "Buffer %d bound to %d\n", buffer, target);

    //Get hook handle
    //Is opaque so access members through glhook_<member>
    HookHandle handle = glhooker_gethook("glBindBuffer");

    //Original function is perserved so we can safely call it with out any side effects
    //However you do need to know the original function signature and function pointer typedef
    PFNGLBINDBUFFERPROC glBindBuffer = (PFNGLBINDBUFFERPROC)glhooker_gethookoriginalfunction(handle);

    //Call original function
    glBindBuffer(target, buffer);
}

int main()
{
    if(!glhooker_init())
    {
        fprintf(stderr,"Could not start glhooker\n");
    }

    GLHookerRegisterHookDesc desc = {
        .src_func_name = "glBindBuffer",
        .dst_func = &my_glbindbuffer,
        
    };
    glhooker_registerhook(&desc);

    /*Load opengl functions, rest of application code*/

}

```
Thats about it!

## Footnotes

[^1]: An inline hook is one where the actual bytes of the target function are overwritten to jump to the hook. \
Typically the start of a function is this:\
`push rbp`\
`mov rpb, rps`\
`<pop args off stack>`\
`<rest of function>`\
After installing the hook it might look something like this:\
`jmpabs 0x7444453399a3c2d6`\
`<rest of function>`\
So the original function is completely destroyed, and yes I am just as surprised as you that you are allowed to do this `hint: mprotect()`. It *may* seem easy to perseve the original function by just exectuing the overwritten instructions after execution of the hook function. But you have to account for the fact that you maybe an instruction was cut in half when you copied in the inline hook. So now you have to account for that; which then means you have to import an dissassembler library and all that crap. No thanks. This is the one time im glad that OpenGL does things so fucked up. I should note that yes, `glXGetProcAddress[ARB]` are, infact, destroyed by inlining the hook for those, but thats okay as my previous assumption I mentioned about them turned out to be true. So we are just implementing our own versions for those ones and theres no real reason to perseve the original function(s). The actual OpenGL functions however we *do* want to perserve.

[^2]: A `relay` (or `trampoline`) function in this context is just a intermediary that transfers control from/to the caller, the hooked function, and the hook. As it is now, *technically* this is not *exactly* needed for the current implementation. As effectievly all its doing is performing a `jmp` instruction. However I am going to leave it as in the future it may be necessary for this relay function to do more than just `jmp` as in previous iterations it was necessary, and may prove to be in the future. Yes this is at the cost of some memory and a few CPU cycles each time an OpenGL function is called. Its also important to note that each hook has its own `relay` function, as the address to jump to is "hardcoded" into it, in a way. You can think of it as a an inline funciton in C (not to be confused with an `inline`[^1] hook!).



