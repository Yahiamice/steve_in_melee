#include "mex.h"

// Character select: Steve's square starts hidden (and unselectable) until someone holds L+R and presses A.
// Overloads the CSS minor scene; the vanilla/m-ex functions are called first.
#define CSS_THINK ((void (*)())0x802669F4)
#define CSS_LOAD ((void (*)(void *))0x8026688C)
#define CSS_EXIT ((void (*)(void *))0x80266D70)
#define STEVE_EXT_ID 26
#define REVEAL_MAGIC 0x5A
#define LOAD_FRAMES 90 // 1.5 s of "Loading level / Building terrain"
#define LOAD_LINK 5    // gx link only our camera draws

// screen positions in the 640x480 overlay (0,0 = centre, y up)
#define VER_X 222.0f   // version text under Steve's square
#define VER_Y -13.0f
#define HINT_X 222.0f  // "press L+R+A for a surprise..." hint, over the P4 door
#define HINT_Y -90.0f
#define HINT_SCALE 0.62f
#define HINT_IN 150    // frames to fade in (slow)
#define HINT_OUT 25

typedef struct CssLoad
{
    GOBJ *cam, *screen, *ver, *hint;
    JOBJDesc *screen_desc;
    int t;
    int active;
    float hint_a;
} CssLoad;
CssLoad L;

void SetAlpha(GOBJ *g, float a)
{
    if (!g) return;
    JOBJ *j = g->hsd_object;
    for (DOBJ *d = j->dobj; d; d = d->next)
        if (d->mobj && d->mobj->mat) d->mobj->mat->alpha = a;
    if (a <= 0.001f) j->flags |= 0x10;
    else j->flags &= ~0x10;
}
void PlaceAt(GOBJ *g, float x, float y, float s)
{
    if (!g) return;
    JOBJ *j = g->hsd_object;
    j->trans.X = x; j->trans.Y = y; j->trans.Z = 0;
    j->scale.X = s; j->scale.Y = s; j->scale.Z = s;
    JOBJ_SetMtxDirtySub(j);
}

MnSlChrIcon *SteveIcon(int *index)
{
    u8 *base = MEX_GetData(MXDT_FTICONDATA);
    int count = MEX_GetData(MXDT_FTICONNUM);
    if (!base) return 0;
    MnSlChrIcon *icons = (MnSlChrIcon *)(base + 0xDC);
    for (int i = 0; i < count; i++)
        if (icons[i].c_kind == STEVE_EXT_ID) { *index = i; return &icons[i]; }
    return 0;
}

void SetIconHidden(int index, int hidden)
{
    JOBJ *root = *(JOBJ **)(R13 - 0x4728); // mexSelectChr icon model
    if (!root) return;
    JOBJ *j = root->child;
    for (int i = 0; i < index && j; i++) j = j->sibling;
    if (!j) return;
    if (hidden) j->flags |= 0x10;
    else j->flags &= ~0x10;
}

// overlay camera (orthographic, 640x480) with the version text, the hint and the Minecraft loading screen
void Overlay_Init()
{
    L.cam = L.screen = L.ver = L.hint = 0;
    L.screen_desc = 0;
    L.active = 0;
    L.hint_a = 0;
    HSD_Archive *arc = Archive_LoadFile("SvLoad.dat");
    if (!arc) return;
    COBJDesc *cd = Archive_GetPublicAddress(arc, "svLoad_cam");
    L.screen_desc = Archive_GetPublicAddress(arc, "svLoad_joint");
    JOBJDesc *vd = Archive_GetPublicAddress(arc, "svVer_joint");
    JOBJDesc *hd = Archive_GetPublicAddress(arc, "svHint_joint");
    if (!cd) return;
    GOBJ *c = GObj_Create(2, 3, 128);
    COBJ *cobj = COBJ_LoadDesc(cd);
    GObj_AddObject(c, 1, cobj);
    GOBJ_InitCamera(c, CObjThink_Common, 7);
    c->cobj_links = 1 << LOAD_LINK;
    L.cam = c;
    if (vd)
    {
        L.ver = GOBJ_EZCreator(4, 5, 0, 0, 0, HSD_OBJKIND_JOBJ, vd, 0, 0, GXLink_Common, LOAD_LINK, 0);
        PlaceAt(L.ver, VER_X, VER_Y, 1);
    }
    if (hd)
    {
        L.hint = GOBJ_EZCreator(4, 5, 0, 0, 0, HSD_OBJKIND_JOBJ, hd, 0, 0, GXLink_Common, LOAD_LINK, 0);
        PlaceAt(L.hint, HINT_X, HINT_Y, HINT_SCALE);
        SetAlpha(L.hint, 0);
    }
}
void Load_Start()
{
    if (!L.cam || !L.screen_desc) return;
    L.screen = GOBJ_EZCreator(4, 5, 0, 0, 0, HSD_OBJKIND_JOBJ, L.screen_desc, 0, 0, GXLink_Common, LOAD_LINK, 0);
    L.t = 0;
    L.active = 1;
    L.hint_a = 0;
    SetAlpha(L.hint, 0);
    if (L.ver) SetAlpha(L.ver, 0); // hidden behind the loading screen
}
void Load_Stop()
{
    if (L.screen) GObj_Destroy(L.screen);
    L.screen = 0;
    L.active = 0;
}

// the reveal is our own flag; the icon state and its model follow it every frame, since other code
// (Slippi's online CSS unlocks every icon when it loads) may set them
int IsRevealed(MnSlChrIcon *ic) { return ((u8 *)ic)[7] == 1; }
void Revealed(MnSlChrIcon *ic, int on) { ((u8 *)ic)[7] = on ? 1 : 0; }
void Enforce(MnSlChrIcon *ic, int idx)
{
    int on = IsRevealed(ic);
    if (on && ic->state < 2) ic->state = 2;
    if (!on) ic->state = 0;
    SetIconHidden(idx, !on);
    // version text under the square only once Steve is there
    if (L.ver && !L.active) SetAlpha(L.ver, on ? 1.0f : 0.0f);
}

// any player's hand pointing at Steve's (hidden) square
int Hovering(MnSlChrIcon *ic)
{
    CSSCursor **cur = (CSSCursor **)0x804A0BC0;
    float l = ic->bound_l < ic->bound_r ? ic->bound_l : ic->bound_r, r = ic->bound_l < ic->bound_r ? ic->bound_r : ic->bound_l;
    float b = ic->bound_u < ic->bound_d ? ic->bound_u : ic->bound_d, t = ic->bound_u < ic->bound_d ? ic->bound_d : ic->bound_u;
    for (int p = 0; p < 4; p++)
    {
        CSSCursor *c = cur[p];
        if (!c || c->state == 3) continue;
        if (c->pos.X >= l - 2.0f && c->pos.X <= r + 1.0f && c->pos.Y >= b - 1.0f && c->pos.Y <= t + 3.0f) return 1;
    }
    return 0;
}

void minor_load(void *data)
{
    CSS_LOAD(data);
    Overlay_Init();
    int idx;
    MnSlChrIcon *ic = SteveIcon(&idx);
    if (!ic) return;
    u8 *flag = (u8 *)ic + 6; // unused padding bytes: 6 = initialised, 7 = revealed (kept until the game is reset)
    if (*flag != REVEAL_MAGIC)
    {
        *flag = REVEAL_MAGIC;
        Revealed(ic, 0);
    }
    Enforce(ic, idx);
}

void minor_think()
{
    CSS_THINK();
    int idx;
    MnSlChrIcon *ic = SteveIcon(&idx);
    if (!ic) return;
    if (L.active)
    {
        L.t++;
        float p = L.t / (float)LOAD_FRAMES;
        if (p > 1) p = 1;
        JOBJ *j = L.screen ? L.screen->hsd_object : 0;
        if (j && j->child)
        {
            j->child->scale.X = p < 0.005f ? 0.005f : p;
            JOBJ_SetMtxDirtySub(j->child);
        }
        if (L.t >= LOAD_FRAMES)
        {
            Load_Stop();
            Revealed(ic, 1);
            Enforce(ic, idx);
            SFX_PlayCommon(1);
        }
        return;
    }
    HSD_Pad *pads0 = (HSD_Pad *)0x804c1fac;
    // hint fades in slowly while someone hovers over the hidden square
    {
        int show = !IsRevealed(ic) && Hovering(ic);
        if (show) L.hint_a += 1.0f / HINT_IN;
        else L.hint_a -= 1.0f / HINT_OUT;
        if (L.hint_a < 0) L.hint_a = 0;
        if (L.hint_a > 1) L.hint_a = 1;
        SetAlpha(L.hint, L.hint_a);
    }
    if (IsRevealed(ic))
    {
        // same combo again hides Steve's square
        for (int p = 0; p < 4; p++)
        {
            HSD_Pad *pad = &pads0[p];
            if ((pad->held & PAD_TRIGGER_L) && (pad->held & PAD_TRIGGER_R) && (pad->down & PAD_BUTTON_A))
            {
                Revealed(ic, 0);
                SFX_PlayCommon(0);
                break;
            }
        }
        Enforce(ic, idx);
        return;
    }
    Enforce(ic, idx);
    HSD_Pad *pads = (HSD_Pad *)0x804c1fac;
    for (int p = 0; p < 4; p++)
    {
        HSD_Pad *pad = &pads[p];
        if ((pad->held & PAD_TRIGGER_L) && (pad->held & PAD_TRIGGER_R) && (pad->down & PAD_BUTTON_A))
        {
            Load_Start();
            if (!L.active)
            {
                Revealed(ic, 1);
                Enforce(ic, idx);
                SFX_PlayCommon(1);
            }
            break;
        }
    }
}

void minor_exit(void *data)
{
    L.active = 0; L.cam = 0; L.screen = 0; L.ver = 0; L.hint = 0;
    CSS_EXIT(data);
}
