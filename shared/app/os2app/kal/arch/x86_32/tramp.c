#define  INCL_BASE
#include <os2.h>

#include <os3/stacksw.h>

/* old FS selector value          */
extern unsigned short old_sel;
/* TIB new FS selector value      */
extern unsigned short tib_sel;

USHORT tramp(PCHAR argv, PCHAR envp, ULONG hmod,
             USHORT tib_sel, void *eip)
{
  USHORT old_sel;

  asm(
      "pushw  %%fs \n"
      "pushw  %%gs \n"
      "pushl  %%eax \n"
      "pushl  %%ebx \n"
      "pushl  %%ecx \n"
      "pushl  %%edx \n"
      "pushl  %%esi \n"
      "pushl  %%edi \n"
      "movw  %%fs, %%dx \n"
      "movw  %%dx, %[old_sel] \n"
      "movw  %[tib_sel], %%dx \n"
      "movw  %%dx, %%fs \n"              /* fs == TIB selector */
      "pushl %%ebp \n"                   /* save ebp on the old stack      */
      "movl  %[eip_data], %%edx \n"
      "pushl %%edx \n"                   /* entry point */
      "movl  %[argv], %%edx \n"
      "pushl %%edx \n"                   /* argv  */
      "movl  %[envp], %%edx \n"
      "pushl %%edx \n"                   /* envp  */
      "xorl  %%edx, %%edx \n"
      "pushl %%edx \n"                   /* sizec */
      "movl  %[hmod], %%edx \n"
      "pushl %%edx \n"                   /* hmod  */
      "xorl  %%ebp, %%ebp \n"            /* ebp == 0 */
      "xorl  %%eax, %%eax \n"            /* eax == 0 */
      "movl  %%eax, %%ebx \n"            /* ebx == 0 */
      "movl  %%eax, %%ecx \n"            /* ecx == 0 */
      "movl  %%eax, %%edx \n"            /* edx == 0 */
      "movl  %%eax, %%esi \n"            /* esi == 0 */
      "movl  %%eax, %%edi \n"            /* edi == 0 */
      "call  *0x10(%%esp) \n"            /* Call the entry point of an OS/2 executable */
      "addl  $0x10, %%esp \n"            /* clear function arguments */
      "popl  %%ebp \n"                   /* popped the entry point */
      "popl  %%ebp \n"                   /* restored the old ebp   */
      "popl  %%edi \n"
      "popl  %%esi \n"
      "popl  %%edx \n"
      "popl  %%ecx \n"
      "popl  %%ebx \n"
      "popl  %%eax \n"
      "popw  %%gs \n"
      "popw  %%fs \n"
      :[old_sel]  "=r" (old_sel)
      :[argv]     "m"  (argv),
       [envp]     "m"  (envp),
       [hmod]     "m"  (hmod),
       [tib_sel]  "m"  (tib_sel),
       [eip_data] "m"  (eip));

  return old_sel;
}

/* The following two routines are needed because of
   collision between Fiasco.OC and OS/2: Fiasco.OC
   stores the UTCB selector in fs, and OS/2 stores
   TIB selector there. So, a workaround: save/restore
   these selectors when entering/exiting to/from
   L4 world / OS/2 world. */

void KalEnter(void)
{
  STKIN

  /* Transition from OS/2 world to L4 world:
     save TIB selector to tib_sel and restore
     host kernel FS selector from old_sel */
  asm ("pushl %%eax\n"
       "movw  %[old_sel], %%ax \n"
       "movw  %%ax, %%fs \n"
       "popl  %%eax \n"
       :
       :[old_sel]  "m"  (old_sel));
}

void KalQuit(void)
{
  /* Transition from L4 world to OS/2 world:
     save an old FS selector to old_sel and restore
     TIB selector in FS from tib_sel     */
  asm ("pushl %%eax \n"
       "movw  %[tib_sel], %%ax \n"
       "movw  %%ax, %%fs \n"
       "popl  %%eax \n"
       :
       :[tib_sel]  "m"  (tib_sel));

  STKOUT
}
