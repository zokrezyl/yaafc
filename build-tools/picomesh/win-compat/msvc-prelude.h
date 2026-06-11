/* MSVC prelude — force-included ahead of every translation unit on WIN32
 * (see the root CMakeLists /FI). Maps GCC/clang keywords the POSIX-targeted
 * sources use onto their MSVC spelling. Kept deliberately tiny and free of
 * <windows.h> so it can't disturb <winsock2.h>-before-<windows.h> ordering. */

#ifndef PICOMESH_MSVC_PRELUDE_H
#define PICOMESH_MSVC_PRELUDE_H

/* GCC/clang thread-local storage keyword -> MSVC. */
#define __thread __declspec(thread)

#endif /* PICOMESH_MSVC_PRELUDE_H */
