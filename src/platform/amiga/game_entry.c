/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/intuition.h>

int amiga_game_main(int argc, char **argv);

enum { GAME_STACK_SIZE = 65536 };

/* Keep saved registers on the original stack and only the game's arguments on
 * the new one. StackSwap changes the task bounds as well as the stack pointer. */
#ifdef __VBCC__
static int game_on_stack(__reg("a0") struct StackSwapStruct *stack,
                         __reg("d0") int argc, __reg("a1") char **argv) =
    "\tmovem.l\td2-d3/a2-a3/a6,-(sp)\n"
    "\tmove.l\ta0,a2\n"
    "\tmove.l\td0,d2\n"
    "\tmove.l\ta1,a3\n"
    "\tmove.l\t4.w,a6\n"
    "\tjsr\t-732(a6)\n"
    "\tmove.l\ta3,-(sp)\n"
    "\tmove.l\td2,-(sp)\n"
    "\tjsr\t_amiga_game_main\n"
    "\taddq.l\t#8,sp\n"
    "\tmove.l\td0,d3\n"
    "\tmove.l\ta2,a0\n"
    "\tjsr\t-732(a6)\n"
    "\tmove.l\td3,d0\n"
    "\tmovem.l\t(sp)+,d2-d3/a2-a3/a6";
#else
int game_on_stack(struct StackSwapStruct *stack, int argc, char **argv);
__asm__(
    ".text\n"
    ".even\n"
    "_game_on_stack:\n"
    "\tmovem.l d2-d3/a2-a3/a6,-(sp)\n"
    "\tmove.l 24(sp),a2\n"
    "\tmove.l 28(sp),d2\n"
    "\tmove.l 32(sp),a3\n"
    "\tmove.l 4.w,a6\n"
    "\tmove.l a2,a0\n"
    "\tjsr -732(a6)\n"
    "\tmove.l a3,-(sp)\n"
    "\tmove.l d2,-(sp)\n"
    "\tjsr _amiga_game_main\n"
    "\taddq.l #8,sp\n"
    "\tmove.l d0,d3\n"
    "\tmove.l a2,a0\n"
    "\tjsr -732(a6)\n"
    "\tmove.l d3,d0\n"
    "\tmovem.l (sp)+,d2-d3/a2-a3/a6\n"
    "\trts\n"
);
#endif

static int run_game(int argc, char **argv)
{
    struct Task *task = FindTask(NULL);
    struct StackSwapStruct stack;
    APTR memory;
    int result;
    if ((ULONG)task->tc_SPUpper - (ULONG)task->tc_SPLower >= GAME_STACK_SIZE)
        return amiga_game_main(argc, argv);
    memory = AllocMem(GAME_STACK_SIZE, MEMF_PUBLIC);
    if (!memory) {
        puts("OpenFP could not allocate its 64 KB stack.");
        return 20;
    }
    stack.stk_Lower = memory;
    stack.stk_Upper = (ULONG)memory + GAME_STACK_SIZE;
    stack.stk_Pointer = (APTR)stack.stk_Upper;
    result = game_on_stack(&stack, argc, argv);
    FreeMem(memory, GAME_STACK_SIZE);
    return result;
}

int main(int argc, char **argv)
{
#if OFP_AMIGA_REQUIRED_CPU == 68060
    const UWORD supported = AFF_68060;
    static char message[] = "OpenFP requires a 68060 CPU.";
#else
    const UWORD supported = AFF_68040 | AFF_68060;
    static char message[] = "OpenFP requires a 68040 or 68060 CPU.";
#endif
    if (SysBase->AttnFlags & supported) return run_game(argc, argv);
    if (argc != 0) {
        puts(message);
    } else {
        struct EasyStruct request = {
            sizeof(struct EasyStruct), 0, (STRPTR)"OpenFP",
            (STRPTR)message, (STRPTR)"OK"
        };
        IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 36);
        if (IntuitionBase) {
            EasyRequestArgs(NULL, &request, NULL, NULL);
            CloseLibrary((struct Library *)IntuitionBase);
        }
    }
    return 20;
}
