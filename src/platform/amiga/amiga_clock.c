/* SPDX-License-Identifier: MIT
 * The clock and sleeping on AmigaOS 3.x, for src/common/net.h: timer.device's system time (to the
 * microsecond, UNIT_MICROHZ), and dos.library's Delay (a fiftieth of a second at a time). Apart from
 * the sockets (net_amiga.c): the NDK's devices/timer.h and Roadshow's headers both define a struct
 * timeval of their own. */
#ifdef __amigaos__
#include <stddef.h>
#include <stdlib.h>
#include <exec/types.h>
#include <exec/io.h>
#include <exec/devices.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "common/net.h"

/* GetSysTime fills two ULONGs, the seconds and the microseconds: our own struct, as the NDKs name them
 * differently (NDK 3.2's struct timeval has unnamed unions for tv_secs/tv_sec, which C99 compilers such
 * as vbcc don't take) */
struct ofp_systime { ULONG secs, micro; };
#define OFP_TIMERNAME  "timer.device"
#define OFP_UNIT_MICROHZ 0
#define OFP_TIMEREQ_SIZE (sizeof(struct IORequest) + sizeof(struct ofp_systime))   /* (a timerequest) */

#ifdef __VBCC__
/* (vbcc: GetSysTime declared here, timer.device's call -66, its result's address in a0: so NDK 3.2's
 *  devices/timer.h, whose unnamed unions vbcc warns about, isn't needed at all) */
void __ofp_GetSysTime(__reg("a6") struct Device *, __reg("a0") struct ofp_systime *dest) = "\tjsr\t-66(a6)";
#define GetSysTime(t) __ofp_GetSysTime(TimerBase, (t))
#else
#include <devices/timer.h>
#include <proto/timer.h>
#endif

struct Device *TimerBase;                    /* timer.device (opened when first needed) */
static struct MsgPort *timer_port;
static struct IORequest *timer_req;
static int timer_tried;

static void timer_close(void)
{
    if (TimerBase) { CloseDevice(timer_req); TimerBase = NULL; }
    if (timer_req) { DeleteIORequest(timer_req); timer_req = NULL; }
    if (timer_port) { DeleteMsgPort(timer_port); timer_port = NULL; }
}

static int timer_open(void)
{
    if (TimerBase) return 0;
    if (timer_tried) return -1;
    timer_tried = 1;
    atexit(timer_close);
    if (!(timer_port = CreateMsgPort())) return -1;
    if (!(timer_req = (struct IORequest *)CreateIORequest(timer_port, OFP_TIMEREQ_SIZE))) return -1;
    if (OpenDevice((CONST_STRPTR)OFP_TIMERNAME, OFP_UNIT_MICROHZ, timer_req, 0) != 0) {
        DeleteIORequest(timer_req); timer_req = NULL;
        return -1;
    }
    TimerBase = timer_req->io_Device;
    return 0;
}

uint32_t amiga_us(void)
{
    struct ofp_systime tv;
    if (timer_open() != 0) {                  /* (no timer.device: the dos clock, a fiftieth of a second) */
        struct DateStamp ds;
        DateStamp(&ds);
        return (uint32_t)ds.ds_Days * 86400u * 1000000u + (uint32_t)ds.ds_Minute * 60000000u + (uint32_t)ds.ds_Tick * 20000u;
    }
#ifdef __VBCC__
    GetSysTime(&tv);
#else
    GetSysTime((struct timeval *)(void *)&tv);
#endif
    return (uint32_t)tv.secs * 1000000u + (uint32_t)tv.micro;
}

uint32_t net_ms(void)
{
    struct ofp_systime tv;
    if (timer_open() != 0) {
        struct DateStamp ds;
        DateStamp(&ds);
        return (uint32_t)ds.ds_Days * 86400000u + (uint32_t)ds.ds_Minute * 60000u + (uint32_t)ds.ds_Tick * 20u;
    }
#ifdef __VBCC__
    GetSysTime(&tv);
#else
    GetSysTime((struct timeval *)(void *)&tv);
#endif
    return (uint32_t)tv.secs * 1000u + (uint32_t)(tv.micro / 1000u);
}

void net_sleep(int ms)
{
    if (ms <= 0) return;
    Delay((ms + 19) / 20);
}
#endif /* __amigaos__ */
