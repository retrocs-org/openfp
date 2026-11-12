/* SPDX-License-Identifier: MIT
 * Just enough of AmigaOS 3.x, on the desktop, to run the Amiga frontend (src/platform/amiga/local.c)
 * and look at what it draws: planar bitmaps, the blitter calls it uses, a palette.
 * Hardware timing and the copper are outside this art preview (tools/amiga/preview/README.md). */
#ifndef AMIGA_SHIM_H
#define AMIGA_SHIM_H
#include <stdint.h>
#include <stddef.h>

typedef uint8_t UBYTE; typedef int8_t BYTE; typedef uint16_t UWORD; typedef int16_t WORD;
typedef uint32_t ULONG; typedef int32_t LONG; typedef void *APTR; typedef unsigned char *STRPTR;
typedef const unsigned char *CONST_STRPTR; typedef short BOOL; typedef ULONG Tag; typedef UBYTE *PLANEPTR;
typedef LONG BPTR;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif
#define TAG_DONE 0

/* exec */
struct Node { struct Node *ln_Succ, *ln_Pred; UBYTE ln_Type; BYTE ln_Pri; char *ln_Name; };
struct MsgPort { struct Node mp_Node; void *mp_SigTask; struct Message *head; };
struct Message { struct Node mn_Node; struct MsgPort *mn_ReplyPort; UWORD mn_Length; };
struct Library { struct Node lib_Node; UWORD lib_Version; };
struct ExecBase { struct Library LibNode; UWORD AttnFlags; };
#define AFF_68010 (1<<0)
#define AFF_68020 (1<<1)
#define AFF_68030 (1<<2)
#define AFF_68040 (1<<3)
#define AFF_68881 (1<<4)
#define AFF_68060 (1<<7)
#define MEMF_ANY 0
#define MEMF_PUBLIC 1
#define MEMF_CHIP 2
#define MEMF_FAST 4
#define MEMF_CLEAR (1<<16)
#define MEMF_LARGEST (1<<17)
struct IORequest { struct Message io_Message; struct Device *io_Device; void *io_Unit; UWORD io_Command; UBYTE io_Flags; BYTE io_Error; };
struct Device { struct Library dd_Library; };
APTR AllocMem(ULONG size, ULONG flags); void FreeMem(APTR p, ULONG size);
APTR AllocVec(ULONG size, ULONG flags); void FreeVec(APTR p);
ULONG AvailMem(ULONG flags);
struct Library *OpenLibrary(CONST_STRPTR name, ULONG version); void CloseLibrary(struct Library *l);
struct MsgPort *CreateMsgPort(void); void DeleteMsgPort(struct MsgPort *p);
struct Message *GetMsg(struct MsgPort *p); void ReplyMsg(struct Message *m); struct Message *WaitPort(struct MsgPort *p);
ULONG Wait(ULONG sigs); void Forbid(void); void Permit(void);

/* graphics */
struct BitMap { UWORD BytesPerRow, Rows; UBYTE Flags, Depth; UWORD pad; PLANEPTR Planes[8]; };
#define BMF_CLEAR 1
#define BMF_DISPLAYABLE 2
#define BMF_INTERLEAVED 4
#define BMF_MINPLANES 16
#define BMF_STANDARD 8
struct TextFont { struct Message tf_Message; UWORD tf_YSize, tf_Style, tf_Flags, tf_XSize, tf_Baseline; };
struct TextAttr { STRPTR ta_Name; UWORD ta_YSize; UBYTE ta_Style, ta_Flags; };
#define FS_NORMAL 0
#define FPF_ROMFONT 1
struct RastPort { void *Layer; struct BitMap *BitMap; UWORD *AreaPtrn; void *TmpRas; void *AreaInfo; void *GelsInfo;
                  UBYTE Mask; BYTE FgPen, BgPen, AOlPen, DrawMode; BYTE AreaPtSz, linpatcnt, dummy; UWORD Flags, LinePtrn;
                  WORD cp_x, cp_y; struct TextFont *Font; };
#define JAM1 0
#define JAM2 1
#define COMPLEMENT 2
#define INVERSVID 4
struct ColorMap { UBYTE Flags, Type; UWORD Count; void *ColorTable; };
struct ViewPort { struct ViewPort *Next; struct ColorMap *ColorMap; WORD DWidth, DHeight, DxOffset, DyOffset; UWORD Modes; };
struct GfxBase { struct Library LibNode; UBYTE ChipRevBits0; UWORD DisplayFlags; };
#define GFXB_BIG_BLITS 0
#define GFXB_HR_AGNUS 0
#define GFXB_HR_DENISE 1
#define GFXB_AA_ALICE 2
#define GFXB_AA_LISA 3
#define GFXF_HR_AGNUS 1
#define GFXF_HR_DENISE 2
#define GFXF_AA_ALICE 4
#define GFXF_AA_LISA 8
#define PAL 4
#define PAL_MONITOR_ID 0x00021000
#define LORES_KEY 0
#define INVALID_ID (~0)
struct BitMap *AllocBitMap(ULONG w, ULONG h, ULONG depth, ULONG flags, struct BitMap *friend_bm);
void FreeBitMap(struct BitMap *bm);
void InitBitMap(struct BitMap *bm, LONG depth, LONG w, LONG h);
void InitRastPort(struct RastPort *rp);
LONG BltBitMap(struct BitMap *src, LONG sx, LONG sy, struct BitMap *dst, LONG dx, LONG dy, LONG w, LONG h,
               ULONG minterm, ULONG mask, PLANEPTR temp);
void BltBitMapRastPort(struct BitMap *src, LONG sx, LONG sy, struct RastPort *rp, LONG dx, LONG dy, LONG w, LONG h, ULONG minterm);
void BltMaskBitMapRastPort(struct BitMap *src, LONG sx, LONG sy, struct RastPort *rp, LONG dx, LONG dy, LONG w, LONG h,
                           ULONG minterm, PLANEPTR mask);
void BltTemplate(const void *src, LONG srcx, LONG modulo, struct RastPort *rp, LONG dx, LONG dy, LONG w, LONG h);
void RectFill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1);
LONG WritePixel(struct RastPort *rp, LONG x, LONG y);
void SetAPen(struct RastPort *rp, ULONG pen); void SetBPen(struct RastPort *rp, ULONG pen);
void SetDrMd(struct RastPort *rp, ULONG mode); void Move(struct RastPort *rp, LONG x, LONG y);
void Draw(struct RastPort *rp, LONG x, LONG y);
LONG Text(struct RastPort *rp, CONST_STRPTR s, ULONG n); LONG SetFont(struct RastPort *rp, struct TextFont *f);
void WaitBlit(void); void WaitTOF(void);
void LoadRGB4(struct ViewPort *vp, const UWORD *colors, LONG count);
void LoadRGB32(struct ViewPort *vp, const ULONG *table);
void SetRGB32(struct ViewPort *vp, ULONG n, ULONG r, ULONG g, ULONG b);
ULONG GetBitMapAttr(struct BitMap *bm, ULONG attr);
#define BMA_HEIGHT 0
#define BMA_DEPTH 4
#define BMA_WIDTH 8
#define BMA_FLAGS 12

/* intuition */
struct DBufInfo { APTR dbi_Link1; ULONG dbi_Count1; struct Message dbi_SafeMessage; APTR dbi_UserData1;
                  APTR dbi_Link2; ULONG dbi_Count2; struct Message dbi_DispMessage; APTR dbi_UserData2; };
struct ScreenBuffer { struct BitMap *sb_BitMap; struct DBufInfo *sb_DBufInfo; };
#define SB_SCREEN_BITMAP 1
#define SB_COPY_BITMAP 2
struct Screen { struct Screen *NextScreen; struct Window *FirstWindow; WORD LeftEdge, TopEdge, Width, Height;
                struct ViewPort ViewPort; struct RastPort RastPort; struct BitMap *bm; };
struct Window { struct Window *NextWindow; WORD LeftEdge, TopEdge, Width, Height; struct RastPort *RPort;
                struct MsgPort *UserPort; struct Screen *WScreen; ULONG Flags; };
struct IntuiMessage { struct Message ExecMessage; ULONG Class; UWORD Code, Qualifier; APTR IAddress; WORD MouseX, MouseY; };
struct IntuitionBase { struct Library LibNode; };
#define IDCMP_CLOSEWINDOW 0x200
#define IDCMP_EXTENDEDMOUSE 0x08000000
#define IMSGCODE_INTUIWHEELDATA 0x0200
struct IntuiWheelData { WORD WheelX, WheelY; };
#define IDCMP_RAWKEY 0x400
#define IDCMP_ACTIVEWINDOW 0x40000
#define IDCMP_INACTIVEWINDOW 0x80000
#define IDCMP_MOUSEBUTTONS 0x8
#define IDCMP_MOUSEMOVE 0x10
#define IDCMP_VANILLAKEY 0x200000
#define IDCMP_INTUITICKS 0x400000
#define IECODE_UP_PREFIX 0x80
#define IECLASS_RAWKEY 1
#define IEQUALIFIER_LSHIFT 1
#define IEQUALIFIER_RSHIFT 2
#define IEQUALIFIER_CAPSLOCK 4
#define IEQUALIFIER_CONTROL 8
#define IEQUALIFIER_LALT 16
#define IEQUALIFIER_RALT 32
#define IEQUALIFIER_REPEAT 0x200
#define SELECTDOWN 0x68
#define SELECTUP 0xE8
#define MENUDOWN 0x69
#define MENUUP 0xE9
#define WFLG_RMBTRAP 0x00010000
#define TAG_USER 0x80000000u
#define SA_Dummy (TAG_USER + 32)
#define SA_Left (SA_Dummy + 1)
#define SA_Top (SA_Dummy + 2)
#define SA_Width (SA_Dummy + 3)
#define SA_Height (SA_Dummy + 4)
#define SA_Depth (SA_Dummy + 5)
#define SA_DetailPen (SA_Dummy + 6)
#define SA_BlockPen (SA_Dummy + 7)
#define SA_Title (SA_Dummy + 8)
#define SA_Colors (SA_Dummy + 9)
#define SA_Font (SA_Dummy + 10)
#define SA_DisplayID (SA_Dummy + 18)
#define SA_ShowTitle (SA_Dummy + 22)
#define SA_Quiet (SA_Dummy + 24)
#define SA_Colors32 (SA_Dummy + 35)
#define SA_Exclusive (SA_Dummy + 37)
#define SA_Interleaved (SA_Dummy + 42)
#define WA_Dummy (TAG_USER + 99)
#define WA_Left (WA_Dummy + 1)
#define WA_Top (WA_Dummy + 2)
#define WA_Width (WA_Dummy + 3)
#define WA_Height (WA_Dummy + 4)
#define WA_IDCMP (WA_Dummy + 7)
#define WA_CustomScreen (WA_Dummy + 13)
#define WA_Backdrop (WA_Dummy + 34)
#define WA_Borderless (WA_Dummy + 36)
#define WA_Activate (WA_Dummy + 37)
#define WA_RMBTrap (WA_Dummy + 38)
#define WA_ReportMouse (WA_Dummy + 48)
struct Screen *OpenScreenTags(void *ns, ...); void CloseScreen(struct Screen *s);
struct Window *OpenWindowTags(void *nw, ...); void CloseWindow(struct Window *w);
struct ScreenBuffer *AllocScreenBuffer(struct Screen *s, struct BitMap *bm, ULONG flags);
void FreeScreenBuffer(struct Screen *s, struct ScreenBuffer *sb);
ULONG ChangeScreenBuffer(struct Screen *s, struct ScreenBuffer *sb);
void SetPointer(struct Window *w, UWORD *p, LONG h, LONG wd, LONG x, LONG y); void ClearPointer(struct Window *w);
BOOL ModifyIDCMP(struct Window *w, ULONG flags);
void ReportMouse(LONG flag, struct Window *w);
void ScreenToFront(struct Screen *s);

/* lowlevel */
#define JP_TYPE_MASK 0xF0000000u
#define JP_TYPE_NOTAVAIL 0x00000000u
#define JP_TYPE_GAMECTLR 0x10000000u
#define JP_TYPE_MOUSE 0x20000000u
#define JP_TYPE_JOYSTK 0x30000000u
#define JP_TYPE_UNKNOWN 0x40000000u
#define JPF_BUTTON_RED (1u<<22)
#define JPF_BUTTON_BLUE (1u<<23)
#define JPF_BUTTON_PLAY (1u<<17)
#define JPF_BUTTON_REVERSE (1u<<18)
#define JPF_BUTTON_FORWARD (1u<<19)
#define JPF_BUTTON_GREEN (1u<<20)
#define JPF_BUTTON_YELLOW (1u<<21)
#define JPF_JOY_UP (1u<<3)
#define JPF_JOY_DOWN (1u<<2)
#define JPF_JOY_LEFT (1u<<1)
#define JPF_JOY_RIGHT (1u<<0)
ULONG ReadJoyPort(ULONG port);

/* dos */
struct DosLibrary { struct Library dl_lib; };
struct FileInfoBlock { LONG fib_DiskKey, fib_DirEntryType; char fib_FileName[108]; LONG fib_Protection, fib_EntryType, fib_Size; };
#define ACCESS_READ (-2)
#define DOS_FIB 2
BPTR Lock(CONST_STRPTR name, LONG mode); void UnLock(BPTR l); BPTR CreateDir(CONST_STRPTR n);
LONG Examine(BPTR l, struct FileInfoBlock *f); LONG ExNext(BPTR l, struct FileInfoBlock *f);
APTR AllocDosObject(ULONG type, void *tags); void FreeDosObject(ULONG type, APTR p);
void Delay(LONG ticks);

/* keymap */
struct InputEvent { struct InputEvent *ie_NextEvent; UBYTE ie_Class, ie_SubClass; UWORD ie_Code, ie_Qualifier; APTR ie_EventAddress; };
WORD MapRawKey(struct InputEvent *e, STRPTR buf, WORD len, void *keymap);

/* sprites (graphics/sprite.h, videocontrol.h) */
struct SimpleSprite { UWORD *posctldata; UWORD height; UWORD x, y; UWORD num; };
struct TagItem { ULONG ti_Tag; ULONG ti_Data; };
#define VTAG_SPEVEN_BASE_SET 0x8000002b
#define VTAG_SPODD_BASE_SET 0x8000002c
#define SA_ColorMapEntries (SA_Dummy + 33)
WORD GetSprite(struct SimpleSprite *s, WORD num); void FreeSprite(WORD num);
void ChangeSprite(struct ViewPort *vp, struct SimpleSprite *s, void *data);
void MoveSprite(struct ViewPort *vp, struct SimpleSprite *s, WORD x, WORD y);
ULONG VideoControl(struct ColorMap *cm, struct TagItem *tags);
LONG MakeScreen(struct Screen *s); LONG RethinkDisplay(void);

/* the preview's own: what to do, and the screen as it would look */
void shim_dump(const char *path);
#endif
