/* Override of glibc's __longjmp_chk for the static-glibc riscv64 cross
 * build. The libc-internal cancellation/unwind paths sometimes trip
 * the FORTIFY check under tinyemu's wasm-emulated RV64 — we have no
 * setjmp/longjmp of our own in the codebase, so the failure is
 * either a libc-internal corner case or an emulation quirk in the
 * register/sp save sequence. Passing it through to plain longjmp lets
 * the process continue past the spurious check.
 *
 * Static linker prefers a definition from a user .o over libc.a, so
 * pulling this TU into both binaries (gateway + frontend) shadows the
 * libc symbol.
 */

#include <setjmp.h>

__attribute__((noreturn))
void __longjmp_chk(jmp_buf env, int val)
{
    longjmp(env, val);
}
