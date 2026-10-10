#include "mex.h"

// Steve (Minecraft) for m-ex.
// All functions are global on purpose (MexTK mis-relocates static functions).

#define STATE_SPECIALN 343       // mine (ground)
#define STATE_SPECIALNAIR 344    // place block (air)
#define STATE_SPECIALS 345       // minecart
#define STATE_SPECIALSAIR 346
#define STATE_SPECIALHI 347      // TNT launch
#define STATE_SPECIALHIGLIDE 348 // elytra
#define STATE_SPECIALLW 349      // place TNT
#define STATE_SPECIALLWAIR 350
#define STATE_CARTTHROW 351     // minecart carrying a grabbed fighter
#define STATE_DETONATE 352
#define STATE_DETONATEAIR 353
#define STATE_CRAFT 354
#define STATE_DAIR 355          // anvil appears under Steve
#define STATE_DAIRFALL 356      // riding the anvil down
#define STATE_DAIRLAND 357      // landed on the anvil
#define STATE_DAIRNOIRON 358    // no iron: empty swing
#define STATE_CARTLOOP 359      // minecart ride after the first loop (keeps the same hitbox)
#define STATE_CARTLOOPAIR 360
#define MF_SKIPHIT 0x8          // Ft_MF_SkipHit: keep hitboxes (and who they already hit)
#define STATE_DANCE 361         // d-pad right
#define STATE_TNTLOOP 362       // walking back laying redstone
#define STATE_TNTPLATE 363      // placing the pressure plate
#define STATE_TNTFAIL 364
#define STATE_TNTFAILAIR 365
#define STATE_CARTFAIL 366      // no iron: failed minecart
#define STATE_CARTFAILAIR 367
#define MAX_FX 24
#define MAX_DUST 8
#define KIRBY_PLACE_OFS 0x1F8   // ext_attr offset of Steve_KirbyPlace (read by Kirby's copy code)
#define KIRBY_SHARED_OFS 0x1FC  // ext_attr offset of the heap block Kirby's copy code keeps its state in
#define MAX_PART 12
#define MAX_EXPL 8
#define MAX_SPARK 10

#define HALF_PI 1.5707963f
#define SND(i) SFX_Play(560000 + (i)) // Steve's own sound bank

enum { M_DIRT, M_WOOD, M_STONE, M_IRON, M_GOLD, M_RED, M_DIAM, M_NUM };
enum { T_WOOD, T_STONE, T_IRON, T_GOLD, T_DIAM };
#define MAX_BLOCKS 8 // Ultimate: up to 8 blocks per Steve
#define MAX_COLL 10 // 8 blocks + the anvil + the TNT
#define ANVIL_SLOT 8
#define TNT_SLOT 9
#define BLOCK_HALF 5.0f
#define BLOCK_SIZE 10.0f

typedef struct SteveAssets
{
    JOBJDesc *bar;
    JOBJDesc *block[3]; // dirt, wood, stone
    JOBJDesc *tnt;
    JOBJDesc *anvil;
    JOBJDesc *cart;
    JOBJDesc *table;
    JOBJDesc *gcrack; // ground crack stages (mining)
    JOBJDesc *part;   // material particles (7 frames)
    JOBJDesc *expl;   // explosion puff (16 frames)
    JOBJDesc *fw;     // firework rocket sprite
    JOBJDesc *cgui;   // crafting progress panel
    JOBJDesc *iron_block;
    JOBJDesc *fx;       // Minecraft particles (particles.png): 0-7 smoke, 8-15 sparks, 16-23 crosses, 24-39 block bits, 40 redstone
    JOBJDesc *dust;     // redstone dust (flat)
    JOBJDesc *plate;    // stone pressure plate
    JOBJDesc *chat;     // "Steve joined the game"
    JOBJDesc *kb_tag;   // Kirby name tag
    JOBJDesc *kb_shadow;
    JOBJDesc *kb_dirt;
    JOBJDesc *fire;     // flint & steel fire: 8 frames x (cross, sides)
} SteveAssets;

///////////////////////
// Stage collision   //
///////////////////////
// Melee keeps stage collision in fixed-size global pools (2048 verts, 1536 lines, 256 joints)
// that stages never fill; Steve's blocks are appended as extra collision joints.
typedef struct CVtx { float x0, x4, px, py, x10, x14; } CVtx;
typedef struct MLine { u16 v0, v1; s16 prev0, next0, prev1, next1; u16 hi, lo; } MLine;
typedef struct CLine { MLine *x0; u32 flags; } CLine;
typedef struct MRange { s16 start, count; } MRange;
typedef struct MJoint { MRange ranges[5]; float left, bottom, right, top; s16 vtx_start, vtx_count; } MJoint;
typedef struct CJoint { struct CJoint *next; MJoint *inner; u32 flags; s16 xC; u8 xE; u8 pad; float minx, miny, maxx, maxy; void *x20, *cb0, *cbd0, *cb1, *cbd1; } CJoint;
typedef struct MCollData { void *verts; int vert_count; void *lines; int line_count; MRange ranges[5]; void *joints; int joint_count; int x2C; } MCollData;
#define MP_COLLDATA (*(MCollData **)0x804D64B4)
#define MP_VTX (*(CVtx **)0x804D64B8)
#define MP_LINES (*(CLine **)0x804D64BC)
#define MP_JOINTS (*(CJoint **)0x804D64C0)
#define MP_JLIST_END (*(CJoint **)0x804D64C8)
#define LINE_ENABLED (1 << 16)

typedef struct SteveData
{
    int mat[M_NUM];
    int tier;
    int dura;
    int last_tool;
    GOBJ *bar;
    GOBJ *block[MAX_BLOCKS];
    float bx[MAX_BLOCKS], by[MAX_BLOCKS];
    int btimer[MAX_BLOCKS];
    GOBJ *tnt;
    float tx, ty;
    int tnt_timer;
    float lastdmg[4];
    float glide_angle, glide_speed;
    int glide_timer;
    int stand_block;
    int respawned;
    float ground_y, tnt_floor;
    // block collision slots
    void *coll_owner;
    int coll_joint[MAX_COLL], coll_line[MAX_COLL], coll_vtx[MAX_COLL];
    int bkind[MAX_BLOCKS];
    float bhp[MAX_BLOCKS];
    int bhitcd[MAX_BLOCKS];
    int bcoll_off[MAX_BLOCKS]; // collision suspended while someone uses the ledge next to it
    // anvil: mode 0 none, 1 ridden (dair), 2 falling alone, 3 landed (solid), 4 down throw
    GOBJ *anvil;
    float ax, ay, avy, afloor;
    int anvil_timer, anvil_mode;
    int dthrow_state, dthrow_anvil;
    // empty minecart that keeps rolling after Steve jumps out
    GOBJ *cart;
    float cx, cy, cvx, cvy, cdir;
    int cart_timer, cart_air, cart_stop, cart_trapped_once;
    GOBJ *trap;
    int trap_mash, trap_need, trap_timer;
    int cart_ride;
    // walking attacks / effects
    float walk_phase;
    int fire_t;
    GOBJ *fire;          // down tilt fire in the world (falls a little if there's no floor)
    float fire_x, fire_y, fire_vy;
    int glide_snd;
    // crafting table
    GOBJ *table;
    float tbx, tby, tbvy;
    int table_pending, table_air;
    // mining effects
    GOBJ *gcrack;
    GOBJ *part[MAX_PART];
    float px[MAX_PART], py[MAX_PART], pvx[MAX_PART], pvy[MAX_PART];
    int plife[MAX_PART], pkind[MAX_PART];
    // explosion puffs
    GOBJ *expl[MAX_EXPL];
    float ex[MAX_EXPL], ey[MAX_EXPL];
    int et[MAX_EXPL];
    // minecart / misc
    float cart_speed;
    int prev_state;
    float prev_vx, prev_vy;
    int lava_t;
    // elytra firework and its trail of tiny explosions
    GOBJ *fw;
    GOBJ *spark[MAX_SPARK];
    float spx[MAX_SPARK], spy[MAX_SPARK];
    int spt[MAX_SPARK];
    // crafting progress panel
    GOBJ *cgui;
    int kb_active; // this slot holds blocks placed by a Kirby with Steve's ability
    int bstamp;
    int bhold;     // B still held since the last block: keep placing while airborne
    // Minecraft particles
    GOBJ *fx[MAX_FX];
    float fxx[MAX_FX], fxy[MAX_FX], fxvx[MAX_FX], fxvy[MAX_FX], fxg[MAX_FX], fxs[MAX_FX];
    int fxf0[MAX_FX], fxn[MAX_FX], fxt[MAX_FX], fxlife[MAX_FX], fxrate[MAX_FX];
    // TNT: 1 falling, 2 resting (solid); pressure plate and redstone
    int tnt_mode;
    float tvy;
    int tnt_hitcd;
    GOBJ *plate;
    float plx, ply;
    int plate_armed;
    GOBJ *dust[MAX_DUST];
    int ndust;
    float dust_x;
    int boom_t;
    float boom_x, boom_y;
    // entrance
    GOBJ *chat;
    int chat_t;
    int entered;
} SteveData;

// Rollback (Slippi) safety: everything that changes during a match lives in the main heap
// (HSD_MemAlloc at fighter load), which Slippi saves and restores on rollback. The fighter
// file itself (code + .bss) is outside the rolled-back memory, so .bss only holds pointers
// that are set once at load and never change during the match.
#include "steve_shared.h"
typedef struct SteveShared
{
    KbShared kb;          // Kirby's copy-ability state (kirby.c reads this through ext_attr+0x1FC)
    SteveData kirby[6];   // blocks placed by Kirby (by Kirby's port)
} SteveShared;
SteveData *steve_ptr[6];
SteveShared *S;
SteveAssets *g_assets;
#define kirby_data S->kirby
void Steve_BoomHitbox(GOBJ *gobj);
void Steve_UpdateEntrance(GOBJ *gobj);

void Steve_DairStart(GOBJ *gobj);

const int tier_dura[5] = {25, 40, 60, 20, 90};
const float tier_mult[5] = {0.834f, 0.917f, 1.0f, 1.0f, 1.084f};
const int tier_mat[5] = {M_WOOD, M_STONE, M_IRON, M_GOLD, M_DIAM};
// Ultimate's resource table (craft costs: wood 2 + 1 stone / 3 iron / 1 gold / 1 diamond)
const int mat_max[M_NUM] = {100, 100, 100, 100, 12, 15, 5};
const int craft_cost[5] = {2, 1, 3, 1, 1};  // wood tier: 2 wood; others: this much of the tier material + 2 wood
const int repair_cost[5] = {1, 1, 2, 1, 1}; // same tier refresh: this much + 1 wood
const int block_cost[4] = {2, 2, 1, 1};     // dirt, wood, stone, iron
const float block_hp[4] = {8, 12, 16, 20};

Vec3 *GetHUDPos(int ply)
{
    Vec3 *(*f)(int) = (void *)0x802F3424;
    return f(ply);
}

SteveData *SD(FighterData *fd) { return steve_ptr[fd->ply % 6]; }
SteveAssets *SA(FighterData *fd) { return (SteveAssets *)((char *)fd->ftData->ext_attr + 0x100); }

void SetJObjPos(GOBJ *g, float x, float y, float s)
{
    JOBJ *j = g->hsd_object;
    j->trans.X = x;
    j->trans.Y = y;
    j->trans.Z = 0;
    j->scale.X = s;
    j->scale.Y = s;
    j->scale.Z = s;
    JOBJ_SetMtxDirtySub(j);
}

GOBJ *SpawnModel(JOBJDesc *desc, int gx_link)
{
    return GOBJ_EZCreator(8, 11, 0, 0, 0, HSD_OBJKIND_JOBJ, desc, 0, 0, GXLink_Common, gx_link, 0);
}

// flint & steel (down tilt): a Minecraft fire in front of Steve from frame 12 to 40. It sits on the floor there,
// or drops slowly when there is none (off the edge), and flickers through its 8 frames.
#define FIRE_START 12
#define FIRE_END 40
#define FIRE_DIST 13.0f
int Steve_FindFloor(float x, float y_from, float y_to, float *out);
void Steve_FireFrame(GOBJ *g, int k)
{
    JOBJ *j = g->hsd_object;
    int i = 0;
    for (DOBJ *dobj = j->dobj; dobj; dobj = dobj->next, i++)
    {
        if ((i >> 1) == k) dobj->flags &= ~1;
        else dobj->flags |= 1;
    }
}
void Steve_RemoveFire(SteveData *d)
{
    if (d->fire) GObj_Destroy(d->fire);
    d->fire = 0;
}
void Steve_UpdateFire(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    SteveAssets *a = SA(fd);
    int on = fd->state_id == ASID_ATTACKLW3 && fd->state.frame >= FIRE_START && fd->state.frame < FIRE_END;
    if (!on) { Steve_RemoveFire(d); return; }
    if (!a->fire) return;
    if (!d->fire)
    {
        d->fire = GOBJ_EZCreator(8, 11, 0, 0, 0, HSD_OBJKIND_JOBJ, a->fire, 0, 0, GXLink_Common, 6, 0);
        if (!d->fire) return;
        d->fire_x = fd->phys.pos.X + FIRE_DIST * (fd->facing_direction < 0 ? -1.0f : 1.0f);
        d->fire_y = fd->phys.pos.Y;
        d->fire_vy = 0;
        d->fire_t = 0;
        float fy;
        if (Steve_FindFloor(d->fire_x, d->fire_y + 4.0f, d->fire_y - 4.0f, &fy)) d->fire_y = fy;
    }
    // gravity (gentle): only while there's nothing under it
    float fy;
    if (Steve_FindFloor(d->fire_x, d->fire_y + 1.0f, d->fire_y - 1.0f + (d->fire_vy < 0 ? d->fire_vy : 0), &fy))
    {
        d->fire_y = fy;
        d->fire_vy = 0;
    }
    else
    {
        d->fire_vy -= 0.06f;
        if (d->fire_vy < -1.2f) d->fire_vy = -1.2f;
        d->fire_y += d->fire_vy;
    }
    d->fire_t++;
    Steve_FireFrame(d->fire, (d->fire_t / 3) % 8);
    JOBJ *j = d->fire->hsd_object;
    j->trans.X = d->fire_x; j->trans.Y = d->fire_y; j->trans.Z = 0;
    j->scale.X = j->scale.Y = j->scale.Z = 1;
    JOBJ_SetMtxDirtySub(j);
}

void SetJObjFacing(GOBJ *g, float dir)
{
    JOBJ *j = g->hsd_object;
    j->rot.Y = HALF_PI;
    j->scale.Z = dir;
    JOBJ_SetMtxDirtySub(j);
}

// floor under a vertical segment (stage lines and blocks); returns 1 and the floor height if found
int Steve_FindFloor(float x, float y_from, float y_to, float *out)
{
    Vec3 pos, nrm;
    int idx, kind;
    // the three -1s are "no filter" (ignored line / group), as in UnclePunch's training code
    if (GrColl_RaycastGround(&pos, &idx, &kind, &nrm, (Vec3 *)-1, (Vec3 *)-1, (Vec3 *)-1, 0, x, y_from, x, y_to, 0))
    {
        *out = pos.Y;
        return 1;
    }
    return 0;
}

// current minor scene kind (the MexTK helper is static inline, which the relocator does not like)
int Steve_MinorKind()
{
    MajorSceneDesc *major = Scene_GetMajorSceneDesc();
    int major_curr = Scene_GetCurrentMajor();
    int major_last = ((MexData *)MEX_GetData(MXDT_MEXDATA))->metadata->last_major;
    for (; major->major_id != major_last; major++)
    {
        if (major->major_id != major_curr) continue;
        int minor_curr = Scene_GetCurrentMinor();
        for (MinorScene *m = major->minor_scene_arr; m->minor_id != -1; m++)
            if (m->minor_id == minor_curr) return m->minor_kind;
    }
    return -1;
}

///////////////////////
//   Materials       //
///////////////////////
void Steve_ResetMaterials(SteveData *d)
{
    d->mat[M_DIRT] = 36;
    d->mat[M_WOOD] = 18;
    d->mat[M_STONE] = 0;
    d->mat[M_IRON] = 3;
    d->mat[M_GOLD] = 0;
    d->mat[M_RED] = 2;
    d->mat[M_DIAM] = 0;
    d->tier = T_WOOD; // Steve starts the match with wooden tools
    d->dura = tier_dura[T_WOOD];
}
// after a KO Steve keeps his materials, only the tools go back to wood
void Steve_ResetTools(SteveData *d)
{
    d->tier = T_WOOD;
    d->dura = tier_dura[T_WOOD];
}

void AddMat(SteveData *d, int m, int n)
{
    d->mat[m] += n;
    if (d->mat[m] > mat_max[m]) d->mat[m] = mat_max[m];
}

void Steve_SpawnParticle(GOBJ *gobj, int kind, float x, float y);
void Steve_SpawnFx(SteveData *d, SteveAssets *a, int f0, int n, int rate, float x, float y, float vx, float vy, float grav, int life, float scale);
float Rand01();
// one dig: gain materials; each gained material pops out of the ground as a little icon
void Steve_MineOnce(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    // per mine (Ultimate table): dirt 1, wood 1, stone 1; sometimes iron 1, gold 4, redstone 3, diamond 1
    int got[M_NUM] = {1, 1, 1, 0, 0, 0, 0};
    if (HSD_Randi(100) < 35) got[M_IRON] = 1;
    if (HSD_Randi(100) < 12) got[M_GOLD] = 4;
    if (HSD_Randi(100) < 20) got[M_RED] = 3;
    if (HSD_Randi(100) < 5) got[M_DIAM] = 1;
    int rare = got[M_IRON] | got[M_GOLD] | got[M_DIAM];
    float x = fd->phys.pos.X + 8.0f * fd->facing_direction, y = fd->phys.pos.Y + 1.0f;
    for (int m = 0; m < M_NUM; m++)
    {
        if (!got[m] || d->mat[m] >= mat_max[m]) continue;
        AddMat(d, m, got[m]);
        Steve_SpawnParticle(gobj, m, x, y);
    }
    // Minecraft block-breaking bits flying off the dug spot
    for (int k = 0; k < 5; k++)
        Steve_SpawnFx(d, SA(fd), 24 + (k & 1) * 8 + HSD_Randi(4), 1, 1, x + (Rand01() - 0.5f) * 6.0f, y + Rand01() * 2.0f,
                      (Rand01() - 0.5f) * 0.7f, 0.5f + Rand01() * 0.7f, 0.09f, 20 + HSD_Randi(10), 0.5f);
    SND(rare ? 28 : 27);
}

int TierRank(int t)
{
    // wood < stone < gold < iron < diamond
    if (t == T_WOOD) return 0;
    if (t == T_STONE) return 1;
    if (t == T_GOLD) return 2;
    if (t == T_IRON) return 3;
    return 4;
}

// returns 1 if a (better or refreshed) tool set can be / was crafted
int Steve_Craft(SteveData *d, int apply)
{
    const int order[5] = {T_DIAM, T_IRON, T_GOLD, T_STONE, T_WOOD};
    for (int i = 0; i < 5; i++)
    {
        int t = order[i];
        int better = TierRank(t) > TierRank(d->tier);
        int worn = (t == d->tier) && (d->dura * 2 < tier_dura[t]);
        if (!better && !worn) continue;
        int need = better ? craft_cost[t] : repair_cost[t];
        int wood = t == T_WOOD ? 0 : (better ? 2 : 1);
        if (d->mat[tier_mat[t]] < need) continue;
        if (d->mat[M_WOOD] < wood) continue;
        if (!apply) return 1;
        d->mat[tier_mat[t]] -= need;
        d->mat[M_WOOD] -= wood;
        d->tier = t;
        d->dura = tier_dura[t];
        return 1;
    }
    return 0;
}

///////////////////////
//     HUD bar       //
///////////////////////
void Steve_UpdateBar(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    SteveAssets *a = SA(fd);
    if (!a || !a->bar) return;
    if (!d->bar)
        d->bar = GOBJ_EZCreator(14, 15, 0, 0, 0, HSD_OBJKIND_JOBJ, a->bar, 0, 0, GXLink_Common, 11, 0);
    Vec3 *hp = GetHUDPos(fd->ply);
    SetJObjPos(d->bar, hp->X, hp->Y + 9.2f, 0.095f);
    JOBJ *j = d->bar->hsd_object;
    DOBJ *dobj = j->dobj;
    if (!dobj) return;
    dobj = dobj->next; // skip background
    for (int slot = 0; slot < 7; slot++)
    {
        int v = d->mat[slot];
        if (v > 99) v = 99;
        int tens = v / 10, ones = v % 10;
        for (int pos = 0; pos < 2; pos++)
            for (int dg = 0; dg < 10; dg++)
            {
                if (!dobj) return;
                int show = pos == 0 ? (tens > 0 && dg == tens) : (dg == ones);
                if (show) dobj->flags &= ~1;
                else dobj->flags |= 1;
                dobj = dobj->next;
            }
    }
}

///////////////////////
//  Tools and tiers  //
///////////////////////
// fishing rod (vis group 4: 4 = cast rod with line and bobber, 6 = rod): Ultimate reels the line in during the
// first 18 frames of CatchPull and removes the rod; it never stays out after a grab or throw state
void Steve_UpdateRod(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    int v = fd->dobj_toggle[4].index, st = fd->state_id;
    if (st == ASID_CATCHPULL || st == ASID_CATCHDASHPULL)
    {
        int want = fd->state.frame < 18 ? 4 : 0;
        if (v != want) Fighter_SetVisGroupCurrent(gobj, 4, want);
    }
    else if (st != ASID_CATCH && st != ASID_CATCHDASH && st != ASID_THROWB && (v == 4 || v == 6))
        Fighter_SetVisGroupCurrent(gobj, 4, 0);
}

void Steve_UpdateTools(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    Steve_UpdateRod(gobj);
    // vis group 1: scripts pick 1..4 (pick/sword/axe/shovel); remap to the tier-specific state 5 + t*5 + tier
    int idx = fd->dobj_toggle[1].index;
    int tool = 0;
    if (idx >= 1 && idx <= 4) tool = idx;
    else if (idx >= 5 && idx < 25) tool = (idx - 5) / 5 + 1;
    // durability: one use per tool appearance
    if (tool > 0 && d->last_tool == 0 && fd->state_id != STATE_CRAFT)
    {
        d->dura--;
        if (d->dura <= 0)
        {
            SND(35);
            d->tier = T_WOOD;
            d->dura = tier_dura[T_WOOD];
        }
    }
    d->last_tool = tool;
    if (tool > 0)
    {
        int want = 5 + (tool - 1) * 5 + d->tier;
        if (idx != want) Fighter_SetVisGroupCurrent(gobj, 1, want);
    }
    // scale tool hitbox damage by tier (scripts are authored at iron)
    for (int i = 0; i < 4; i++)
    {
        ftHit *h = &fd->hitbox[i];
        if (h->active && tool > 0 && h->dmg_f != d->lastdmg[i])
        {
            float nd = h->dmg_f * tier_mult[d->tier];
            h->dmg_f = nd;
            h->dmg = (int)(nd + 0.5f);
            d->lastdmg[i] = nd;
        }
        else if (!h->active)
            d->lastdmg[i] = -1;
        // wood and stone tools hit with a blunt sound, iron / gold / diamond ones slice
        if (h->active && tool > 0 && (h->hitsound_kind == 1 || h->hitsound_kind == 3))
            h->hitsound_kind = (d->tier == T_WOOD || d->tier == T_STONE) ? 1 : 3;
    }
}

///////////////////////
//  3/4 view facing  //
///////////////////////
void Steve_ApplyFacing(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    if (!fd->bones) return;
    float dir = fd->facing_direction < 0 ? -1.0f : 1.0f;
    JOBJ *j = fd->bones[0].joint;
    if (j)
    {
        j->rot.Y = HALF_PI;
        // the game scales TopN for Super / Poison Mushrooms (and Steve's model scale); TopN is turned 90 degrees,
        // so its Z is the screen's X: keep that size and only flip the sign for facing
        float s = j->scale.X < 0 ? -j->scale.X : j->scale.X;
        j->scale.Z = dir * s;
        JOBJ_SetMtxDirtySub(j);
    }
}

void Steve_SetPitch(FighterData *fd, float pitch)
{
    if (!fd->bones) return;
    JOBJ *j = fd->bones[2].joint;
    if (!j) return;
    j->rot.X = pitch;
    j->trans.Y = 8.0f; // rest height of the Rot joint
    JOBJ_SetMtxDirtySub(j);
}

///////////////////////
//   Blocks / TNT    //
///////////////////////
// The stage's collision header (MP_COLLDATA) points into the stage FILE, and Slippi (and the game's preload cache)
// keeps that file loaded for the next match on the same stage: growing its counts there left the next match reading
// lines and joints that no longer exist (crash on the second game). So the first time in a match, MP_COLLDATA is
// pointed at a copy in the match heap (gone with the match) and only the copy's counts grow.
#define COLL_COPY_MAGIC 0x53764344 // 'SvCD'
typedef struct CollCopy { MCollData cd; u32 magic; MCollData *orig; } CollCopy;
MCollData *Coll_Own()
{
    MCollData *cd = MP_COLLDATA;
    if (!cd) return 0;
    if (((CollCopy *)cd)->magic == COLL_COPY_MAGIC) return cd;
    CollCopy *c = HSD_MemAlloc(sizeof(CollCopy));
    if (!c) return 0;
    memcpy(&c->cd, cd, sizeof(MCollData));
    c->magic = COLL_COPY_MAGIC;
    c->orig = cd;
    MP_COLLDATA = &c->cd;
    return &c->cd;
}

void Coll_Init(SteveData *d)
{
    MCollData *cd = MP_COLLDATA;
    d->coll_owner = cd;
    for (int i = 0; i < MAX_COLL; i++) d->coll_joint[i] = -1;
    if (!cd || !MP_VTX || !MP_LINES || !MP_JOINTS || !MP_JLIST_END) return;
    cd = Coll_Own();
    if (!cd) return;
    d->coll_owner = cd;
    for (int i = 0; i < MAX_COLL; i++)
    {
        if (cd->joint_count + 1 > 256 || cd->line_count + 4 > 1536 || cd->vert_count + 4 > 2048) return;
        int vb = cd->vert_count, lb = cd->line_count, jb = cd->joint_count;
        MLine *ml = HSD_MemAlloc(sizeof(MLine) * 4);
        MJoint *mj = HSD_MemAlloc(sizeof(MJoint));
        // verts: 0 bottom-left, 1 top-left, 2 top-right, 3 bottom-right (clockwise outline like stage data)
        // lines: 0 floor TL->TR, 1 right wall TR->BR, 2 ceiling BR->BL, 3 left wall BL->TL
        const int v0[4] = {1, 2, 3, 0}, v1[4] = {2, 3, 0, 1};
        const u16 kind[4] = {1, 4, 2, 8};
        for (int k = 0; k < 4; k++)
        {
            ml[k].v0 = vb + v0[k];
            ml[k].v1 = vb + v1[k];
            ml[k].prev0 = lb + ((k + 3) & 3);
            ml[k].next0 = lb + ((k + 1) & 3);
            ml[k].prev1 = -1;
            ml[k].next1 = -1;
            ml[k].hi = kind[k];
            ml[k].lo = 3; // dirt material (footstep sounds)
            MP_LINES[lb + k].x0 = &ml[k];
            MP_LINES[lb + k].flags = kind[k];
            CVtx *v = &MP_VTX[vb + k];
            v->x0 = v->px = v->x10 = -30000;
            v->x4 = v->py = v->x14 = -30000;
        }
        memset(mj, 0, sizeof(MJoint));
        mj->ranges[0].start = lb + 0; mj->ranges[0].count = 1; // floor
        mj->ranges[1].start = lb + 2; mj->ranges[1].count = 1; // ceiling
        mj->ranges[2].start = lb + 1; mj->ranges[2].count = 1; // right wall
        mj->ranges[3].start = lb + 3; mj->ranges[3].count = 1; // left wall
        mj->left = mj->right = -30000; mj->bottom = mj->top = -30000;
        mj->vtx_start = vb; mj->vtx_count = 4;
        CJoint *j = &MP_JOINTS[jb];
        memset(j, 0, sizeof(CJoint));
        j->inner = mj;
        j->flags = 1 << 16;
        j->xE = 0x80;
        j->minx = j->maxx = -30000; j->miny = j->maxy = -30000;
        MP_JLIST_END->next = j;
        MP_JLIST_END = j;
        j->next = 0;
        cd->vert_count += 4;
        cd->line_count += 4;
        cd->joint_count += 1;
        d->coll_joint[i] = jb; d->coll_line[i] = lb; d->coll_vtx[i] = vb;
    }
}

void Coll_Set(SteveData *d, int i, int on, float cx, float by)
{
    if (d->coll_joint[i] < 0 || d->coll_owner != MP_COLLDATA) return;
    float l = cx - BLOCK_HALF, r = cx + BLOCK_HALF, b = by, t = by + BLOCK_SIZE;
    if (!on) { l = r = -30000; b = t = -30000; }
    const float xs[4] = {0, 0, 1, 1}, ys[4] = {0, 1, 1, 0};
    for (int k = 0; k < 4; k++)
    {
        CVtx *v = &MP_VTX[d->coll_vtx[i] + k];
        float x = xs[k] ? r : l, y = ys[k] ? t : b;
        v->x0 = v->px = v->x10 = x;
        v->x4 = v->py = v->x14 = y;
        CLine *ln = &MP_LINES[d->coll_line[i] + k];
        if (on) ln->flags |= LINE_ENABLED; else ln->flags &= ~LINE_ENABLED;
    }
    CJoint *j = &MP_JOINTS[d->coll_joint[i]];
    j->minx = l - 1; j->maxx = r + 1; j->miny = b - 1; j->maxy = t + 1;
    MJoint *mj = j->inner;
    mj->left = l; mj->right = r; mj->bottom = b; mj->top = t;
}

void Steve_RemoveBlock(SteveData *d, int i)
{
    if (!d->block[i]) return;
    GObj_Destroy(d->block[i]);
    d->block[i] = 0;
    Coll_Set(d, i, 0, 0, 0);
}

// blocks take damage from any fighter's hitboxes
void Steve_BlockHits(SteveData *d, int i)
{
    if (d->bhitcd[i] > 0) { d->bhitcd[i]--; return; }
    float cx = d->bx[i], cy = d->by[i] + BLOCK_HALF;
    for (int p = 0; p < 6; p++)
    {
        GOBJ *g = Fighter_GetGObj(p);
        if (!g) continue;
        FighterData *f = g->userdata;
        for (int h = 0; h < 4; h++)
        {
            ftHit *hb = &f->hitbox[h];
            if (!hb->active || hb->dmg_f <= 0) continue;
            float dx = hb->pos.X - cx, dy = hb->pos.Y - cy;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            float reach = BLOCK_HALF + hb->size;
            if (dx < reach && dy < reach)
            {
                d->bhp[i] -= hb->dmg_f;
                d->bhitcd[i] = 12;
                return;
            }
        }
    }
}

void Steve_RemoveAnvil(SteveData *d)
{
    if (d->anvil) GObj_Destroy(d->anvil);
    d->anvil = 0;
    d->anvil_mode = 0;
    Coll_Set(d, ANVIL_SLOT, 0, 0, 0);
}

void Steve_UpdateAnvil(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    if (!d->anvil) return;
    d->anvil_timer++;
    // Steve jumped off: the anvil keeps falling on its own
    if (d->anvil_mode == 1 && fd->state_id != STATE_DAIR && fd->state_id != STATE_DAIRFALL)
    {
        d->anvil_mode = 2;
        if (d->avy > -1.0f) d->avy = -1.0f;
    }
    if (d->anvil_mode == 2)
    {
        d->avy -= 0.25f;
        if (d->avy < -4.0f) d->avy = -4.0f;
        float fy;
        if (Steve_FindFloor(d->ax, d->ay + 2.0f, d->ay + d->avy - 0.5f, &fy))
        {
            d->ay = fy;
            d->anvil_mode = 3;
            d->anvil_timer = 0;
            Coll_Set(d, ANVIL_SLOT, 1, d->ax, d->ay);
            SND(12);
        }
        else d->ay += d->avy;
        if (d->ay < -300) { Steve_RemoveAnvil(d); return; }
    }
    else if (d->anvil_mode == 3)
    {
        if (d->anvil_timer > 100) { Steve_RemoveAnvil(d); return; }
        float fy;
        if (!Steve_FindFloor(d->ax, d->ay + 1.0f, d->ay - 1.0f, &fy))
        {
            d->anvil_mode = 2;
            d->avy = 0;
            Coll_Set(d, ANVIL_SLOT, 0, 0, 0);
        }
    }
    else if (d->anvil_mode == 4)
    {
        // down throw: drops from above onto the thrown fighter
        if (d->ay > d->afloor)
        {
            d->avy -= 0.6f;
            d->ay += d->avy;
            if (d->ay <= d->afloor) { d->ay = d->afloor; SND(12); }
        }
        if (d->anvil_timer > 40) { Steve_RemoveAnvil(d); return; }
    }
    SetJObjPos(d->anvil, d->ax, d->ay, 1);
}

///////////////////////
// Empty minecart    //
///////////////////////
void Cart_Release(SteveData *d)
{
    GOBJ *g = d->trap;
    d->trap = 0;
    if (!g) return;
    FighterData *v = g->userdata;
    if (v->state_id == ASID_DOWNWAITU)
    {
        if (v->phys.air_state) Fighter_EnterFall(g);
        else Fighter_EnterWait(g);
    }
}

void Cart_Remove(SteveData *d)
{
    Cart_Release(d);
    if (d->cart) GObj_Destroy(d->cart);
    d->cart = 0;
}

void Cart_Spawn(GOBJ *gobj, float speed)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    SteveAssets *a = SA(fd);
    Cart_Remove(d);
    if (!a->cart) return;
    d->cart = SpawnModel(a->cart, 6);
    d->cx = fd->phys.pos.X;
    d->cy = fd->phys.pos.Y;
    d->cdir = fd->facing_direction < 0 ? -1.0f : 1.0f;
    d->cvx = speed * d->cdir;
    d->cvy = 0;
    d->cart_timer = 0;
    d->cart_air = fd->phys.air_state;
    d->cart_stop = 0;
    d->cart_trapped_once = 0;
    d->trap = 0;
    SetJObjPos(d->cart, d->cx, d->cy, 1);
    SetJObjFacing(d->cart, d->cdir);
}

int CountBits(int v)
{
    int n = 0;
    while (v) { n += v & 1; v >>= 1; }
    return n;
}

void Steve_UpdateCart(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    if (!d->cart) return;
    d->cart_timer++;
    // roll along the floor, slowing down; fall off edges
    float fy;
    float nx = d->cx + d->cvx;
    if (Steve_FindFloor(nx, d->cy + 6.0f, d->cy - 6.0f + (d->cvy < 0 ? d->cvy : 0), &fy))
    {
        d->cx = nx;
        d->cy = fy;
        d->cvy = 0;
        d->cart_air = 0;
        float dec = 0.02f;
        if (d->cvx > dec) d->cvx -= dec;
        else if (d->cvx < -dec) d->cvx += dec;
        else d->cvx = 0;
    }
    else
    {
        d->cx = nx;
        d->cvy -= 0.12f;
        if (d->cvy < -3.0f) d->cvy = -3.0f;
        d->cy += d->cvy;
        d->cart_air++;
    }
    if (d->cvx == 0) d->cart_stop++;
    SetJObjPos(d->cart, d->cx, d->cy, 1);
    SetJObjFacing(d->cart, d->cdir);

    // catch one grounded fighter along the way
    if (!d->trap && !d->cart_trapped_once && !d->cart_air && (d->cvx > 0.3f || d->cvx < -0.3f))
    {
        for (int p = 0; p < 6; p++)
        {
            GOBJ *g = Fighter_GetGObj(p);
            if (!g || g == gobj) continue;
            FighterData *v = g->userdata;
            if (v->phys.air_state || v->state_id < ASID_WAIT || v->flags.dead) continue;
            float dx = v->phys.pos.X - d->cx, dy = v->phys.pos.Y - d->cy;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx < 6.0f && dy < 5.0f)
            {
                d->trap = g;
                d->cart_trapped_once = 1;
                d->trap_mash = 0;
                d->trap_timer = 0;
                d->trap_need = 6 + (int)(v->dmg.percent / 15.0f);
                SND(17);
                break;
            }
        }
    }
    if (d->trap)
    {
        FighterData *v = d->trap->userdata;
        d->trap_timer++;
        d->trap_mash += CountBits(v->input.down & (PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_X | PAD_BUTTON_Y | PAD_TRIGGER_L | PAD_TRIGGER_R | PAD_TRIGGER_Z));
        if (v->flags.dead || v->state_id < ASID_WAIT || d->cart_air > 3 || d->cart_stop > 0 || v->phys.air_state || d->trap_mash >= d->trap_need || d->trap_timer > 150)
            Cart_Release(d);
        else
        {
            if (v->state_id != ASID_DOWNWAITU) ActionStateChange(0, 1, 0, d->trap, ASID_DOWNWAITU, 0, 0);
            // ride along: steer the fighter's ground speed onto the cart so the game's own
            // ground collision keeps them on the floor
            float dx = d->cx + d->cvx * 3.0f - v->phys.pos.X;
            if (dx > 4.0f) dx = 4.0f;
            if (dx < -4.0f) dx = -4.0f;
            v->phys.self_vel.X = v->phys.self_vel.Y = 0;
            v->phys.kb_vel.X = v->phys.kb_vel.Y = 0;
            v->phys.self_vel_ground.X = dx;
        }
    }
    if (d->cart_stop > 40 || d->cy < -300 || d->cart_timer > 600) Cart_Remove(d);
}

///////////////////////
//  Effects          //
///////////////////////
// show one DOBJ (frame) of a multi-frame model, -1 hides all
void SetFrame(GOBJ *g, int frame)
{
    JOBJ *j = g->hsd_object;
    int i = 0;
    for (DOBJ *d = j->dobj; d; d = d->next, i++)
    {
        if (i == frame) d->flags &= ~1;
        else d->flags |= 1;
    }
}
float Rand01() { return HSD_Randi(1000) / 1000.0f; }

void Steve_SpawnParticle(GOBJ *gobj, int kind, float x, float y)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    SteveAssets *a = SA(fd);
    if (!a->part) return;
    int slot = -1, oldest = 9999;
    for (int i = 0; i < MAX_PART; i++)
    {
        if (!d->part[i]) { slot = i; break; }
        if (d->plife[i] < oldest) { oldest = d->plife[i]; slot = i; }
    }
    if (d->part[slot]) GObj_Destroy(d->part[slot]);
    d->part[slot] = SpawnModel(a->part, 6);
    d->pkind[slot] = kind;
    d->px[slot] = x;
    d->py[slot] = y;
    d->pvx[slot] = (Rand01() - 0.5f) * 0.9f;
    d->pvy[slot] = 1.1f + Rand01() * 0.7f;
    d->plife[slot] = 34 + HSD_Randi(10);
    SetFrame(d->part[slot], kind);
    SetJObjPos(d->part[slot], x, y, 1);
}

void Steve_SpawnExplosion(GOBJ *gobj, float x, float y)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    SteveAssets *a = SA(fd);
    if (!a->expl) return;
    for (int i = 0; i < MAX_EXPL; i++)
    {
        if (d->expl[i]) GObj_Destroy(d->expl[i]);
        d->expl[i] = SpawnModel(a->expl, 6);
        float ang = Rand01() * 6.2831f, r = i == 0 ? 0 : 3.0f + Rand01() * 9.0f;
        d->ex[i] = x + cos(ang) * r;
        d->ey[i] = y + 5.0f + sin(ang) * r * 0.8f;
        d->et[i] = -(i / 2); // staggered start
        SetFrame(d->expl[i], -1);
        SetJObjPos(d->expl[i], d->ex[i], d->ey[i], 1.0f + Rand01() * 0.6f);
    }
}

// generic Minecraft particle: frames f0..f0+n-1 (advancing every `rate` frames), simple ballistic motion
void Steve_SpawnFx(SteveData *d, SteveAssets *a, int f0, int n, int rate, float x, float y, float vx, float vy, float grav, int life, float scale)
{
    if (!a || !a->fx) return;
    int slot = -1, oldest = 9999;
    for (int i = 0; i < MAX_FX; i++)
    {
        if (!d->fx[i]) { slot = i; break; }
        if (d->fxlife[i] < oldest) { oldest = d->fxlife[i]; slot = i; }
    }
    if (d->fx[slot]) GObj_Destroy(d->fx[slot]);
    d->fx[slot] = SpawnModel(a->fx, 6);
    d->fxx[slot] = x; d->fxy[slot] = y; d->fxvx[slot] = vx; d->fxvy[slot] = vy; d->fxg[slot] = grav;
    d->fxf0[slot] = f0; d->fxn[slot] = n; d->fxrate[slot] = rate < 1 ? 1 : rate; d->fxt[slot] = 0;
    d->fxlife[slot] = life; d->fxs[slot] = scale;
    SetFrame(d->fx[slot], f0);
    SetJObjPos(d->fx[slot], x, y, scale);
}
void Steve_UpdateFx(SteveData *d)
{
    for (int i = 0; i < MAX_FX; i++)
    {
        if (!d->fx[i]) continue;
        if (--d->fxlife[i] <= 0) { GObj_Destroy(d->fx[i]); d->fx[i] = 0; continue; }
        d->fxvy[i] -= d->fxg[i];
        d->fxx[i] += d->fxvx[i];
        d->fxy[i] += d->fxvy[i];
        int t = ++d->fxt[i];
        int f = t / d->fxrate[i];
        if (f >= d->fxn[i]) f = d->fxn[i] - 1;
        SetFrame(d->fx[i], d->fxf0[i] + f);
        SetJObjPos(d->fx[i], d->fxx[i], d->fxy[i], d->fxs[i]);
    }
}
// Minecraft "poof": smoke puffs
void Steve_Poof(SteveData *d, float x, float y, int n)
{
    for (int k = 0; k < n; k++)
        Steve_SpawnFx(d, g_assets, 0, 8, 3 + HSD_Randi(3), x + (Rand01() - 0.5f) * 10.0f, y + Rand01() * 14.0f,
                      (Rand01() - 0.5f) * 0.3f, 0.15f + Rand01() * 0.2f, 0, 22 + HSD_Randi(8), 1.0f + Rand01() * 0.6f);
}

void Steve_UpdateEffects(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    for (int i = 0; i < MAX_PART; i++)
    {
        if (!d->part[i]) continue;
        d->pvy[i] -= 0.09f;
        d->px[i] += d->pvx[i];
        d->py[i] += d->pvy[i];
        if (--d->plife[i] <= 0) { GObj_Destroy(d->part[i]); d->part[i] = 0; continue; }
        SetFrame(d->part[i], d->pkind[i]);
        SetJObjPos(d->part[i], d->px[i], d->py[i], d->plife[i] < 8 ? d->plife[i] / 8.0f : 1.0f);
    }
    Steve_UpdateFx(d);
    for (int i = 0; i < MAX_EXPL; i++)
    {
        if (!d->expl[i]) continue;
        int t = ++d->et[i];
        if (t >= 16) { GObj_Destroy(d->expl[i]); d->expl[i] = 0; continue; }
        SetFrame(d->expl[i], t >= 0 ? t : -1);
    }
    // ground crack under the pickaxe while mining
    SteveAssets *a = SA(fd);
    if (fd->state_id == STATE_SPECIALN && a->gcrack && fd->phys.air_state == 0)
    {
        if (!d->gcrack) d->gcrack = SpawnModel(a->gcrack, 6);
        int stage = (int)(fd->state.frame * 9.0f / 19.0f);
        if (stage > 8) stage = 8;
        SetFrame(d->gcrack, stage);
        SetJObjPos(d->gcrack, fd->phys.pos.X + 8.0f * fd->facing_direction, fd->phys.pos.Y, 1);
    }
    else if (d->gcrack)
    {
        GObj_Destroy(d->gcrack);
        d->gcrack = 0;
    }
}

// elytra: the firework rocket burns behind Steve for the first part of the flight, leaving tiny explosions
#define FW_FRAMES 40
void Steve_UpdateFirework(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    SteveAssets *a = SA(fd);
    int on = fd->state_id == STATE_SPECIALHIGLIDE && d->glide_timer < FW_FRAMES && a->fw;
    float dir = fd->facing_direction < 0 ? -1.0f : 1.0f;
    float vx = cos(d->glide_angle) * dir, vy = sin(d->glide_angle);
    float cx = fd->phys.pos.X, cy = fd->phys.pos.Y + 6.0f;
    if (on)
    {
        if (!d->fw) d->fw = SpawnModel(a->fw, 6);
        float rx = cx - vx * 7.0f, ry = cy - vy * 7.0f;
        SetJObjPos(d->fw, rx, ry, 1);
        JOBJ *j = d->fw->hsd_object;
        float ang = dir > 0 ? d->glide_angle : 3.14159265f - d->glide_angle;
        j->rot.Z = ang - HALF_PI; // the sprite points straight up
        JOBJ_SetMtxDirtySub(j);
        // a tiny explosion puff every other frame from the rocket's tail
        if ((d->glide_timer & 1) == 0 && a->expl)
        {
            int slot = (d->glide_timer / 2) % MAX_SPARK;
            if (d->spark[slot]) GObj_Destroy(d->spark[slot]);
            d->spark[slot] = SpawnModel(a->expl, 6);
            d->spx[slot] = rx - vx * 3.5f + (Rand01() - 0.5f) * 2.0f;
            d->spy[slot] = ry - vy * 3.5f + (Rand01() - 0.5f) * 2.0f;
            d->spt[slot] = 0;
            SetFrame(d->spark[slot], 0);
            SetJObjPos(d->spark[slot], d->spx[slot], d->spy[slot], 0.18f + Rand01() * 0.1f);
        }
    }
    else if (d->fw)
    {
        GObj_Destroy(d->fw);
        d->fw = 0;
    }
    for (int i = 0; i < MAX_SPARK; i++)
    {
        if (!d->spark[i]) continue;
        d->spt[i] += 2;
        if (d->spt[i] >= 16) { GObj_Destroy(d->spark[i]); d->spark[i] = 0; continue; }
        SetFrame(d->spark[i], d->spt[i]);
    }
}

// crafting: Minecraft-style panel over Steve's head (material -> sword of the new tier, green progress bar)
void Steve_UpdateCraftGui(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    SteveAssets *a = SA(fd);
    if (fd->state_id == STATE_CRAFT && fd->state.frame < 32 && a->cgui)
    {
        if (!d->cgui) d->cgui = SpawnModel(a->cgui, 6);
        SetJObjPos(d->cgui, fd->phys.pos.X, fd->phys.pos.Y + 29.0f, 1.35f);
        JOBJ *j = d->cgui->hsd_object;
        int i = 0;
        for (DOBJ *o = j->dobj; o; o = o->next, i++)
        {
            int show = i == 0 || i == 1 + d->tier || i == 6 + d->tier;
            if (show) o->flags &= ~1;
            else o->flags |= 1;
        }
        float p = fd->state.frame / 13.0f;
        if (p > 1) p = 1;
        if (p < 0.02f) p = 0.02f;
        if (j->child)
        {
            j->child->scale.X = p;
            JOBJ_SetMtxDirtySub(j->child);
        }
    }
    else if (d->cgui)
    {
        GObj_Destroy(d->cgui);
        d->cgui = 0;
    }
}

///////////////////////
//  Crafting table   //
///////////////////////
void Steve_TableDepth(GOBJ *g);
void Steve_PlaceTable(GOBJ *gobj, float x, float y)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    SteveAssets *a = SA(fd);
    if (!a->table) return;
    if (!d->table) d->table = SpawnModel(a->table, 6);
    d->tbx = x;
    d->tby = y;
    d->tbvy = 0;
    d->table_air = 0;
    SetJObjPos(d->table, x, y, 1);
    Steve_TableDepth(d->table);
    SND(31);
}
// like Ultimate, the table stands a little behind the fighters' plane
#define TABLE_Z -7.0f
void Steve_TableDepth(GOBJ *g)
{
    JOBJ *j = g->hsd_object;
    j->trans.Z = TABLE_Z;
    JOBJ_SetMtxDirtySub(j);
}
void Steve_UpdateTable(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    // appears at the start of the match and after every respawn, once Steve is on the ground
    if (d->table_pending && fd->phys.air_state == 0 && fd->state_id > ASID_REBIRTHWAIT && (d->entered || fd->state_id != ASID_ENTRY))
    {
        d->table_pending = 0;
        Steve_PlaceTable(gobj, fd->phys.pos.X - 11.0f * fd->facing_direction, fd->phys.pos.Y);
    }
    if (!d->table) return;
    // falls if whatever it stands on disappears (e.g. a broken block)
    float fy;
    if (Steve_FindFloor(d->tbx, d->tby + 1.0f, d->tby - 1.0f + (d->tbvy < 0 ? d->tbvy : 0), &fy))
    {
        d->tby = fy;
        d->tbvy = 0;
    }
    else
    {
        d->tbvy -= 0.2f;
        if (d->tbvy < -3.0f) d->tbvy = -3.0f;
        d->tby += d->tbvy;
        if (d->tby < -300) { GObj_Destroy(d->table); d->table = 0; return; }
    }
    SetJObjPos(d->table, d->tbx, d->tby, 1);
    Steve_TableDepth(d->table);
}
int Steve_NearTable(FighterData *fd)
{
    SteveData *d = SD(fd);
    if (!d->table) return 0;
    float dx = fd->phys.pos.X - d->tbx, dy = fd->phys.pos.Y - d->tby;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx < 6.5f && dy < 3.0f; // Steve has to stand at the table (it is 10 wide)
}

// someone standing on top of the block (or TNT) at cx, top ty
int Steve_StoodOn(float cx, float ty)
{
    for (int p = 0; p < 6; p++)
    {
        GOBJ *g = Fighter_GetGObj(p);
        if (!g) continue;
        FighterData *f = g->userdata;
        if (f->phys.air_state) continue;
        float dx = f->phys.pos.X - cx, dy = f->phys.pos.Y - ty;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx < BLOCK_HALF + 1.5f && dy < 1.0f) return 1;
    }
    return 0;
}
void Steve_BreakBlockFx(SteveData *d, int kind, float x, float y)
{
    SND(32 + (kind > 2 ? 2 : kind)); // Minecraft uses the same sound set for placing and breaking
    for (int k = 0; k < 8; k++)
        Steve_SpawnFx(d, g_assets, 24 + kind * 4 + (k & 3), 1, 1, x + (Rand01() - 0.5f) * 8.0f, y + 2.0f + Rand01() * 7.0f,
                      (Rand01() - 0.5f) * 0.8f, 0.4f + Rand01() * 0.9f, 0.09f, 26 + HSD_Randi(12), 0.55f + Rand01() * 0.3f);
}
// a fighter hanging on / getting up from a ledge right next to this block
int Steve_LedgeUserNear(float cx, float by)
{
    for (int p = 0; p < 6; p++)
    {
        GOBJ *g = Fighter_GetGObj(p);
        if (!g) continue;
        FighterData *f = g->userdata;
        if (f->state_id < ASID_CLIFFCATCH || f->state_id > ASID_CLIFFJUMPQUICK2) continue;
        float dx = f->phys.pos.X - cx, dy = f->phys.pos.Y - (by + BLOCK_HALF);
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx < BLOCK_HALF + 14.0f && dy < BLOCK_HALF + 26.0f) return 1;
    }
    return 0;
}
void Steve_UpdateBlocks(SteveData *d)
{
    for (int i = 0; i < MAX_BLOCKS; i++)
    {
        if (!d->block[i]) continue;
        // blocks built against a ledge would shove a ledge-hanging fighter on top of them
        int off = Steve_LedgeUserNear(d->bx[i], d->by[i]);
        if (off != d->bcoll_off[i])
        {
            d->bcoll_off[i] = off;
            Coll_Set(d, i, !off, d->bx[i], d->by[i]);
        }
        // wears faster while someone stands on it (x4; Ultimate's x8 felt too harsh here)
        d->bhp[i] -= Steve_StoodOn(d->bx[i], d->by[i] + BLOCK_SIZE) ? 0.08f : 0.02f;
        Steve_BlockHits(d, i);
        if (d->bhp[i] <= 0)
        {
            Steve_BreakBlockFx(d, d->bkind[i], d->bx[i], d->by[i]);
            Steve_RemoveBlock(d, i);
            continue;
        }
        float hp = d->bhp[i] / block_hp[d->bkind[i]];
        int stage = (int)((1.0f - hp) * 10.0f) - 1; // -1 = no cracks yet
        JOBJ *j = d->block[i]->hsd_object;
        DOBJ *dobj = j->dobj ? j->dobj->next : 0;
        for (int c = 0; c < 10 && dobj; c++, dobj = dobj->next)
        {
            if (c == stage) dobj->flags &= ~1;
            else dobj->flags |= 1;
        }
    }
}

void Steve_UpdateTNT(GOBJ *gobj);
void Steve_UpdateObjects(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    if (d->coll_owner != MP_COLLDATA)
    {
        for (int i = 0; i < MAX_BLOCKS; i++) d->block[i] = 0; // new match: old objects are gone
        d->tnt = 0; d->anvil = 0; d->anvil_mode = 0; d->cart = 0; d->trap = 0;
        d->table = 0; d->gcrack = 0;
        for (int i = 0; i < MAX_PART; i++) d->part[i] = 0;
        for (int i = 0; i < MAX_EXPL; i++) d->expl[i] = 0;
        for (int i = 0; i < MAX_SPARK; i++) d->spark[i] = 0;
        d->fw = 0; d->cgui = 0;
        for (int i = 0; i < MAX_FX; i++) d->fx[i] = 0;
        for (int i = 0; i < MAX_DUST; i++) d->dust[i] = 0;
        d->plate = 0; d->ndust = 0; d->tnt_mode = 0; d->chat = 0; d->boom_t = 0; d->fire = 0;
        d->table_pending = 1;
        Coll_Init(d);
    }
    Steve_UpdateBlocks(d);
    Steve_UpdateAnvil(gobj);
    Steve_UpdateCart(gobj);
    Steve_UpdateEffects(gobj);
    Steve_UpdateFirework(gobj);
    Steve_UpdateCraftGui(gobj);
    Steve_UpdateTable(gobj);
    Steve_UpdateTNT(gobj);
}

int Block_Place(SteveData *d, SteveAssets *a, int kind, float x, float y);
int Steve_PlaceBlock(GOBJ *gobj, float x, float y)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    SteveAssets *a = SA(fd);
    // dirt first, then wood, stone, iron (costs 2 / 2 / 1 / 1)
    const int bmat[4] = {M_DIRT, M_WOOD, M_STONE, M_IRON};
    int kind = -1;
    for (int k = 0; k < 4 && kind < 0; k++)
        if (d->mat[bmat[k]] >= block_cost[k]) kind = k;
    if (kind < 0) return -1;
    int slot = Block_Place(d, a, kind, x, y);
    if (slot >= 0) d->mat[bmat[kind]] -= block_cost[kind];
    return slot;
}

// segment p0-p1 crosses (or lies in) the box l..r x b..t
int SegHitsBox(float x0, float y0, float x1, float y1, float l, float b, float r, float t)
{
    // Liang-Barsky clip
    float u0 = 0, u1 = 1, dx = x1 - x0, dy = y1 - y0;
    float p[4] = {-dx, dx, -dy, dy}, q[4] = {x0 - l, r - x0, y0 - b, t - y0};
    for (int k = 0; k < 4; k++)
    {
        if (p[k] == 0) { if (q[k] < 0) return 0; continue; }
        float u = q[k] / p[k];
        if (p[k] < 0) { if (u > u1) return 0; if (u > u0) u0 = u; }
        else { if (u < u0) return 0; if (u < u1) u1 = u; }
    }
    return 1;
}
// would a block in the cell (cx +-5, by .. by+10) overlap anything solid? Works on any stage with no per-stage data:
//  - a solid collision line (stage, moving platforms, other blocks, the anvil, TNT) passing through the cell, or
//  - the cell sitting inside solid ground: the first solid line straight above its centre faces up (a floor seen from
//    below = inside), where under a stage it faces down (its ceiling). Drop-through platforms have no inside.
// Touching a surface is fine (the box is shrunk by 0.5), so blocks still stack on floors, hug walls and hang under stages.
int Steve_CellBlocked(float cx, float by)
{
    MCollData *cd = MP_COLLDATA;
    if (!cd || !MP_VTX || !MP_LINES) return 0;
    float l = cx - BLOCK_HALF + 0.5f, r = cx + BLOCK_HALF - 0.5f, b = by + 0.5f, t = by + BLOCK_SIZE - 0.5f;
    float mx = cx + 0.0137f, my = by + BLOCK_HALF; // just off the grid so the ray never runs through a vertex
    float best = 1e9f;
    int inside = 0;
    for (int i = 0; i < cd->line_count; i++)
    {
        CLine *cl = &MP_LINES[i];
        if (!(cl->flags & LINE_ENABLED) || !cl->x0) continue;
        MLine *ml = cl->x0;
        int platform = (ml->lo >> 8) & 1; // drop-through: can't be overlapped, but has no inside
        CVtx *va = &MP_VTX[ml->v0], *vb = &MP_VTX[ml->v1];
        float x0 = va->px, y0 = va->py, x1 = vb->px, y1 = vb->py;
        if (x0 < -20000 || x1 < -20000) continue; // parked (unused) block lines
        if (SegHitsBox(x0, y0, x1, y1, l, b, r, t)) return 1;
        if (!platform && (x0 <= mx) != (x1 <= mx))
        {
            float y = y0 + (y1 - y0) * (mx - x0) / (x1 - x0);
            if (y > my && y - my < best) { best = y - my; inside = x1 > x0; } // clockwise outlines: left-to-right = top side
        }
    }
    return inside;
}

int Block_Place(SteveData *d, SteveAssets *a, int kind, float x, float y)
{
    JOBJDesc *desc = !a ? 0 : (kind == 3 ? a->iron_block : a->block[kind]);
    if (!desc) return -1;
    // snap to a 10 unit grid anchored to the floor Steve last stood on
    float gx = x / BLOCK_SIZE;
    int ix = (int)(gx < 0 ? gx - 0.5f : gx + 0.5f);
    x = ix * BLOCK_SIZE;
    float rel = (y - d->ground_y) / BLOCK_SIZE;
    int iy = (int)(rel < 0 ? rel - 0.999f : rel);
    y = d->ground_y + iy * BLOCK_SIZE;
    // never inside the stage or another solid thing (a block overlapping a ledge would push fighters on top of it)
    if (Steve_CellBlocked(x, y)) return -1;
    // one block per grid cell
    for (int i = 0; i < MAX_BLOCKS; i++)
        if (d->block[i] && d->bx[i] == x && d->by[i] == y) return -1;
    int slot = 0, best = 0x7FFFFFFF;
    for (int i = 0; i < MAX_BLOCKS; i++)
    {
        if (!d->block[i]) { slot = i; break; }
        if (d->btimer[i] < best) { best = d->btimer[i]; slot = i; } // oldest block goes away
    }
    Steve_RemoveBlock(d, slot);
    d->block[slot] = SpawnModel(desc, 6);
    d->bx[slot] = x;
    d->by[slot] = y;
    d->bkind[slot] = kind;
    d->bhp[slot] = block_hp[kind];
    d->bhitcd[slot] = 0;
    d->btimer[slot] = ++d->bstamp;
    SetJObjPos(d->block[slot], x, y, 1);
    Coll_Set(d, slot, 1, x, y);
    d->bcoll_off[slot] = 0;
    SND(32 + (kind > 2 ? 2 : kind));
    return slot;
}

///////////////////////
// Kirby's copy (PlKbCpSv.dat calls this through ext_attr + KIRBY_PLACE_OFS)
///////////////////////
void Kirby_SlotReset(SteveData *kd)
{
    memset(kd, 0, sizeof(SteveData));
    kd->kb_active = 1;
    Coll_Init(kd);
}
int Steve_KirbyPlace(GOBJ *kirby, int air)
{
    FighterData *kf = kirby->userdata;
    SteveData *kd = &kirby_data[kf->ply % 6];
    if (!kd->kb_active || kd->coll_owner != MP_COLLDATA) Kirby_SlotReset(kd);
    if (!air) kd->ground_y = kf->phys.pos.Y;
    float dir = kf->facing_direction < 0 ? -1.0f : 1.0f;
    float x = air ? kf->phys.pos.X : kf->phys.pos.X + 12.0f * dir;
    float y = air ? kf->phys.pos.Y - BLOCK_SIZE - 0.5f : kf->phys.pos.Y + 0.5f;
    return Block_Place(kd, g_assets, 0, x, y) >= 0;
}
// run by the first Steve in the match: Kirby's blocks age, crack and break like Steve's
void Steve_UpdateKirbyBlocks(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    for (int p = 0; p < 6; p++)
    {
        GOBJ *g = Fighter_GetGObj(p);
        if (!g) continue;
        if (g == gobj) break; // an earlier Steve already did it
        if (((FighterData *)g->userdata)->kind == fd->kind) return;
    }
    for (int p = 0; p < 6; p++)
    {
        SteveData *kd = &kirby_data[p];
        if (!kd->kb_active) continue;
        if (kd->coll_owner != MP_COLLDATA) { Kirby_SlotReset(kd); continue; }
        GOBJ *k = Fighter_GetGObj(p);
        if (k && ((FighterData *)k->userdata)->phys.air_state == 0) kd->ground_y = ((FighterData *)k->userdata)->phys.pos.Y;
        Steve_UpdateBlocks(kd);
        Steve_UpdateFx(kd);
    }
}

///////////////////////
//  Fighter hooks    //
///////////////////////
void OnLoad(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    fd->special_attributes = fd->special_attributes2; // vanilla Mario callbacks still use this
    // per-fighter state in the (rolled back) heap
    SteveData *d = HSD_MemAlloc(sizeof(SteveData));
    memset(d, 0, sizeof(SteveData));
    steve_ptr[fd->ply % 6] = d;
    for (int i = 0; i < 4; i++) d->lastdmg[i] = -1;
    Steve_ResetMaterials(d);
    // Kirby slots start empty every match; Kirby's copy code finds Steve_KirbyPlace here
    S = HSD_MemAlloc(sizeof(SteveShared));
    memset(S, 0, sizeof(SteveShared));
    *(void **)((char *)fd->ftData->ext_attr + KIRBY_SHARED_OFS) = (void *)&S->kb;
    g_assets = SA(fd);
    *(void **)((char *)fd->ftData->ext_attr + KIRBY_PLACE_OFS) = (void *)Steve_KirbyPlace;
}

void Steve_OnRespawnX(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    Steve_ResetTools(d);
    Steve_ApplyFacing(gobj);
}

// Minecraft-style leg swing (from Ultimate's walk), used while walking during jab/tilts/mining
#define BONE_LEGL 25
#define BONE_LEGR 29
void Steve_SetLegs(FighterData *fd, float swing)
{
    if (!fd->bones) return;
    JOBJ *l = fd->bones[BONE_LEGL].joint, *r = fd->bones[BONE_LEGR].joint;
    if (!l || !r) return;
    l->rot.X = 0; l->rot.Y = 0; l->rot.Z = 3.14159265f + swing;
    r->rot.X = 0; r->rot.Y = 0; r->rot.Z = -3.14159265f - swing;
    JOBJ_SetMtxDirtySub(l);
    JOBJ_SetMtxDirtySub(r);
}

// Ultimate lets Steve walk (forwards or backwards, no turning) during these
void Steve_WalkAttack(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    if (fd->phys.air_state) return;
    float sx = fd->input.lstick.X;
    if (sx > -0.25f && sx < 0.25f) sx = 0;
    float target = sx * fd->attr.walk_maximum_velocity * 0.8f;
    float v = fd->phys.self_vel_ground.X;
    v += (target - v) * 0.35f;
    fd->phys.self_vel_ground.X = v;
    float sp = v < 0 ? -v : v;
    float maxv = fd->attr.walk_maximum_velocity;
    d->walk_phase += sp / maxv * 0.2f;
    float amp = sp / maxv * 1.1f;
    if (amp > 1.1f) amp = 1.1f;
    Steve_SetLegs(fd, amp * sin(d->walk_phase));
}

int Steve_IsWalkAttack(int s)
{
    return (s >= ASID_ATTACK11 && s <= ASID_ATTACK11 + 2) || (s >= ASID_ATTACKS3HI && s <= ASID_ATTACKHI3);
}

// down throw: anvil onto the thrown fighter (uses one iron)
void Steve_DownThrow(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    SteveAssets *a = SA(fd);
    if (fd->state_id != ASID_THROWLW)
    {
        if (d->dthrow_state && d->anvil_mode == 4) Steve_RemoveAnvil(d);
        d->dthrow_state = 0;
        return;
    }
    if (!d->dthrow_state)
    {
        d->dthrow_state = 1;
        d->dthrow_anvil = d->mat[M_IRON] > 0 && a->anvil;
        if (d->dthrow_anvil) d->mat[M_IRON]--;
    }
    // without iron it is only the fling
    if (!d->dthrow_anvil && fd->throw_hitbox[0].dmg > 8)
    {
        fd->throw_hitbox[0].dmg = 8;
        fd->throw_hitbox[0].dmg_f = 8;
    }
    if (d->dthrow_anvil && d->dthrow_state == 1 && fd->state.frame >= 5)
    {
        d->dthrow_state = 2;
        Steve_RemoveAnvil(d);
        d->anvil = SpawnModel(a->anvil, 6);
        d->anvil_mode = 4;
        d->anvil_timer = 0;
        d->ax = fd->phys.pos.X + 10.0f * fd->facing_direction;
        d->afloor = fd->phys.pos.Y;
        d->ay = d->afloor + 39.0f;
        d->avy = -1.0f;
        SetJObjPos(d->anvil, d->ax, d->ay, 1);
        SetJObjFacing(d->anvil, 1);
    }
}

void OnFrame(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    // results screen: just the model (the game turns it to the camera), no HUD or stage objects
    if (Steve_MinorKind() == MNRKIND_RST)
    {
        if (d->bar) { GObj_Destroy(d->bar); d->bar = 0; }
        return;
    }
    Steve_ApplyFacing(gobj);
    g_assets = SA(fd);
    Steve_UpdateEntrance(gobj);
    Steve_UpdateKirbyBlocks(gobj);
    // respawn platform: reset materials and tools; the crafting table comes back once he lands
    if (fd->state_id == 12 && !d->respawned) { Steve_ResetTools(d); d->respawned = 1; d->table_pending = 1; }
    if (fd->state_id != 12) d->respawned = 0;
    Steve_UpdateTools(gobj);
    if (fd->phys.air_state == 0) d->ground_y = fd->phys.pos.Y;
    Steve_UpdateObjects(gobj);
    Steve_UpdateBar(gobj);

    // no tap jump: a jump only counts when X or Y is held
    int st = fd->state_id;
    if (st != d->prev_state && !(fd->input.held & (PAD_BUTTON_X | PAD_BUTTON_Y)))
    {
        if (st == ASID_KNEEBEND)
        {
            d->prev_state = st;
            Fighter_EnterWait(gobj);
            d->prev_state = fd->state_id;
            return;
        }
        if ((st == ASID_JUMPAERIALF || st == ASID_JUMPAERIALF + 1) && fd->jump.jumps_used > 0)
        {
            fd->jump.jumps_used--;
            Fighter_EnterFall(gobj);
            fd->phys.self_vel.X = d->prev_vx;
            fd->phys.self_vel.Y = d->prev_vy;
            d->prev_state = fd->state_id;
            return;
        }
    }
    d->prev_state = fd->state_id;
    d->prev_vx = fd->phys.self_vel.X;
    d->prev_vy = fd->phys.self_vel.Y;

    // jumps: legs swing like Minecraft walking, but only when jumping forwards / backwards
    if (st == ASID_JUMPF || st == ASID_JUMPB || st == ASID_JUMPAERIALF || st == ASID_JUMPAERIALB)
    {
        float vx = fd->phys.self_vel.X < 0 ? -fd->phys.self_vel.X : fd->phys.self_vel.X;
        float amp = vx / (fd->attr.aerial_drift_max > 0.1f ? fd->attr.aerial_drift_max : 1.0f);
        if (amp > 1) amp = 1;
        d->walk_phase += 0.16f + 0.1f * amp;
        if (amp > 0.08f) Steve_SetLegs(fd, 0.42f * amp * sin(d->walk_phase));
    }
    // keep building while B stays held: walking or falling off the last block places the next one
    if (!(fd->input.held & PAD_BUTTON_B)) d->bhold = 0;
    else if (d->bhold && fd->phys.air_state && (st == ASID_FALL || st == ASID_FALLAERIAL || st == ASID_JUMPF || st == ASID_JUMPB ||
                                                  st == ASID_JUMPAERIALF || st == ASID_JUMPAERIALB))
    {
        SpecialAirN(gobj);
        return;
    }
    // d-pad right: dance
    if ((st == ASID_WAIT || (st >= ASID_WALKSLOW && st <= ASID_WALKFAST) || st == ASID_SQUATWAIT) && (fd->input.down & PAD_BUTTON_DPAD_RIGHT))
    {
        ActionStateChange(0, 1, 0, gobj, STATE_DANCE, 0, 0);
        Fighter_AdvanceScript(gobj);
        return;
    }
    // shield + B: the crafting table appears right where Steve stands (2 wood, or 2 stone, 2 dirt, 2 iron)
    if ((st == ASID_GUARD || st == ASID_GUARDON) && (fd->input.down & PAD_BUTTON_B))
    {
        const int pay[4] = {M_WOOD, M_STONE, M_DIRT, M_IRON};
        int ok = 0;
        for (int k = 0; k < 4 && !ok; k++)
            if (d->mat[pay[k]] >= 2) { d->mat[pay[k]] -= 2; ok = 1; }
        if (ok) Steve_PlaceTable(gobj, fd->phys.pos.X, fd->phys.pos.Y);
        else SND(39);
    }
    // down smash: lava only after the charge, animated lava frames
    if (st == ASID_ATTACKLW4)
    {
        float f = fd->state.frame;
        int want = 0;
        d->lava_t++;
        if (f >= 7 && f < 20) want = 1 + (d->lava_t / 3) % 8;
        else if (f >= 24 && f < 38) want = 9 + (d->lava_t / 3) % 8;
        if (fd->dobj_toggle[5].index != want) Fighter_SetVisGroupCurrent(gobj, 5, want);
    }

    // walking jab / tilts (jump-cancelable like in Ultimate)
    if (Steve_IsWalkAttack(fd->state_id))
    {
        Steve_WalkAttack(gobj);
        if (fd->state.frame > 2 && Fighter_IASACheck_JumpF(gobj)) return;
        // holding A keeps swinging (jab and up tilt)
        int jab = fd->state_id >= ASID_ATTACK11 && fd->state_id <= ASID_ATTACK11 + 2;
        if ((jab || fd->state_id == ASID_ATTACKHI3) && fd->state.frame >= 14 && (fd->input.held & PAD_BUTTON_A))
        {
            ActionStateChange(0, 1, 0, gobj, fd->state_id == ASID_ATTACKHI3 ? ASID_ATTACKHI3 : ASID_ATTACK11, 0, 0);
            Fighter_AdvanceScript(gobj);
            return;
        }
    }

    // down air starts our own anvil states
    if (fd->state_id == ASID_ATTACKAIRLW) { Steve_DairStart(gobj); return; }

    Steve_DownThrow(gobj);
    // elytra: the boost hitbox (script 300) only counts on the first loop of the flight
    if (fd->state_id == STATE_SPECIALHIGLIDE && SD(fd)->glide_timer >= 30) Fighter_HitboxDisableAll(gobj);

    Steve_UpdateFire(gobj);

    Steve_BoomHitbox(gobj);
}

// entrance: Steve pops in out of thin air with a click, and the chat says he joined
#define ENTRY_APPEAR 36
void Steve_UpdateEntrance(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    SteveAssets *a = SA(fd);
    if (!d->entered)
    {
        if (fd->state_id == ASID_ENTRY && fd->state.frame < ENTRY_APPEAR) fd->flags.invisible = 1;
        else
        {
            d->entered = 1;
            fd->flags.invisible = 0;
            // only at the start of the match (entry), not on later loads
            {
                Steve_Poof(d, fd->phys.pos.X, fd->phys.pos.Y, 10);
                SND(66);
                if (a->chat && !d->chat)
                {
                    d->chat = GOBJ_EZCreator(14, 15, 0, 0, 0, HSD_OBJKIND_JOBJ, a->chat, 0, 0, GXLink_Common, 11, 0);
                    d->chat_t = 0;
                }
            }
        }
    }
    if (d->chat)
    {
        // Minecraft chat line, bottom left; stays 2 s then fades over 1 s
        int t = ++d->chat_t;
        Vec3 *hp = GetHUDPos(0);
        SetJObjPos(d->chat, hp->X - 6.0f, hp->Y + 13.0f + fd->ply * 1.8f, 0.068f);
        float alpha = t < 120 ? 1.0f : 1.0f - (t - 120) / 60.0f;
        if (alpha <= 0) { GObj_Destroy(d->chat); d->chat = 0; return; }
        JOBJ *j = d->chat->hsd_object;
        if (j->dobj && j->dobj->mobj && j->dobj->mobj->mat) j->dobj->mobj->mat->alpha = alpha;
    }
}

void OnActionStateChange(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    if (Steve_MinorKind() == MNRKIND_RST) return;
    Steve_ApplyFacing(gobj);
    if (fd->state_id != STATE_SPECIALHIGLIDE) Steve_SetPitch(fd, 0);
}

// shared helpers
void Air_Phys(GOBJ *gobj) { Fighter_PhysAir_ApplyGravityFastfall(gobj); }
void Ground_ToWait(GOBJ *gobj) { if (FrameTimerCheck(gobj) == 0) Fighter_EnterWait(gobj); }
void Ground_Phys(GOBJ *gobj) { Fighter_PhysGround_ApplyFriction(gobj); }

void ToAir(GOBJ *gobj, int state)
{
    FighterData *fd = gobj->userdata;
    Fighter_SetAirborne(fd);
    ActionStateChange(fd->state.frame, 1, 0, gobj, state, 0x5000, 0);
}
void ToGround(GOBJ *gobj, int state)
{
    FighterData *fd = gobj->userdata;
    Fighter_SetGrounded2(fd);
    ActionStateChange(fd->state.frame, 1, 0, gobj, state, 0x5000, 0);
}
void Land_ToLanding(GOBJ *gobj) { Fighter_EnterLanding(gobj); }

///////////////////////
// Neutral B: Mine / Block
///////////////////////
void SpecialN(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    // next to the crafting table: craft the best tools the materials allow
    if (Steve_NearTable(fd) && Steve_Craft(d, 1))
    {
        ActionStateChange(0, 1, 0, gobj, STATE_CRAFT, 0, 0);
        Fighter_AdvanceScript(gobj);
        return;
    }
    ActionStateChange(0, 1, 0, gobj, STATE_SPECIALN, 0, 0);
    Fighter_AdvanceScript(gobj);
}
#define PLACE_RATE 1.6f  // block placement plays fast
#define PLACE_FRAME 3    // anim frame the block appears on
#define PLACE_IASA 7     // anim frame Steve can act again
void SpecialAirN(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    fd->state_var.state_var1 = 0;
    ActionStateChange(0, PLACE_RATE, 0, gobj, STATE_SPECIALNAIR, 0, 0);
    Fighter_AdvanceScript(gobj);
}
void Mine_Anim(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    if (fd->state.frame >= 5 && fd->state.frame < 6) SND(23);
    if (FrameTimerCheck(gobj) == 0)
    {
        Steve_MineOnce(gobj);
        if (fd->input.held & PAD_BUTTON_B)
        {
            ActionStateChange(0, 1, 0, gobj, STATE_SPECIALN, 0, 0);
            Fighter_AdvanceScript(gobj);
        }
        else
            Fighter_EnterWait(gobj);
    }
}
void Ground_Coll(GOBJ *gobj) { if (Fighter_CollGround_PassLedge(gobj) == 0) Fighter_EnterFall(gobj); }
void Mine_Phys(GOBJ *gobj)
{
    Steve_WalkAttack(gobj);
    Fighter_PhysGround_ApplyVelocity(gobj);
}

void PlaceBlock_Anim(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    if (fd->state_var.state_var1 == 0 && fd->state.frame >= PLACE_FRAME)
    {
        fd->state_var.state_var1 = 1;
        // block appears right under Steve's feet, he lands on it
        if (Steve_PlaceBlock(gobj, fd->phys.pos.X, fd->phys.pos.Y - BLOCK_SIZE - 0.5f) >= 0)
        {
            d->bhold = 1;
            fd->state_var.state_var1 = 2;
        }
    }
    // holding B keeps building: another block as soon as this one is down
    if (fd->state_var.state_var1 == 2 && fd->state.frame >= PLACE_IASA && (fd->input.held & PAD_BUTTON_B))
    {
        SpecialAirN(gobj);
        return;
    }
    if (FrameTimerCheck(gobj) == 0) { Fighter_EnterFall(gobj); return; }
}
void PlaceBlock_IASA(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    if (fd->state.frame >= PLACE_IASA) Fighter_IASACheck_AllAerial(gobj);
}
void PlaceBlock_Phys(GOBJ *gobj)
{
    // exactly the normal fall physics (Ultimate's block placement doesn't change Steve's fall or drift)
    ((void (*)(GOBJ *))0x800CCD58)(gobj);
}
void Land_ToWait(GOBJ *gobj) { Fighter_SetGrounded2(gobj->userdata); Fighter_EnterWait(gobj); }
void AirN_Coll(GOBJ *gobj) { Fighter_CollAir_GrabFacingLedgeWalljump(gobj, Fighter_Coll_CheckToPass, Land_ToWait); }

///////////////////////
// Side B: Minecart  //
///////////////////////
#define MINECART_FRAMES 150
#define MINECART_SPEED (SD((FighterData *)gobj->userdata)->cart_speed)
void CartThrow_Phys(GOBJ *gobj)
{
    void (*throw_phys)(GOBJ *) = (void *)0x800DD930;
    throw_phys(gobj);
}
// powered rail (iron + gold + redstone) is fast; iron alone gives a slow cart; no iron, no cart
int Cart_Pay(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    if (d->mat[M_IRON] > 0 && d->mat[M_GOLD] > 0 && d->mat[M_RED] > 0)
    {
        d->mat[M_IRON]--; d->mat[M_GOLD]--; d->mat[M_RED]--;
        d->cart_speed = 2.1f;
        return 1;
    }
    if (d->mat[M_IRON] > 0)
    {
        d->mat[M_IRON]--;
        d->cart_speed = 1.2f;
        return 1;
    }
    SND(39); // same "no iron" sound as the down air
    return 0;
}
// no iron: Steve turns to the input direction and fails to place a cart (a little sparkle where it would be)
void Cart_Fail(GOBJ *gobj, int air)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    if (fd->input.lstick.X * fd->facing_direction < -0.3f) fd->facing_direction = -fd->facing_direction;
    ActionStateChange(0, 1, 0, gobj, air ? STATE_CARTFAILAIR : STATE_CARTFAIL, 0, 0);
    Fighter_AdvanceScript(gobj);
    for (int k = 0; k < 6; k++)
        Steve_SpawnFx(d, SA(fd), 8, 8, 3, fd->phys.pos.X + (9.0f + (Rand01() - 0.5f) * 8.0f) * fd->facing_direction,
                      fd->phys.pos.Y + 1.0f + Rand01() * 7.0f, 0, 0.12f + Rand01() * 0.1f, 0, 22 + HSD_Randi(10), 0.9f);
}
void CartFail_Coll(GOBJ *gobj) { if (Fighter_CollGround_PassLedge(gobj) == 0) ToAir(gobj, STATE_CARTFAILAIR); }
void CartFailAir_Coll(GOBJ *gobj) { if (Fighter_CollAir_IgnoreLedge_NoCB(gobj) != 0) ToGround(gobj, STATE_CARTFAIL); }
void SpecialS(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    if (!Cart_Pay(gobj)) { Cart_Fail(gobj, 0); return; }
    fd->state_var.state_var1 = 0;
    ActionStateChange(0, 1, 0, gobj, STATE_SPECIALS, 0, 0);
    Fighter_AdvanceScript(gobj);
    SND(36);
}
void SpecialAirS(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    if (!Cart_Pay(gobj)) { Cart_Fail(gobj, 1); return; }
    fd->state_var.state_var1 = 0;
    fd->phys.self_vel.Y = 0.8f;
    ActionStateChange(0, 1, 0, gobj, STATE_SPECIALSAIR, 0, 0);
    Fighter_AdvanceScript(gobj);
    SND(36);
}
int Cart_Tick(GOBJ *gobj, int state)
{
    FighterData *fd = gobj->userdata;
    int t = ++fd->state_var.state_var1;
    if (FrameTimerCheck(gobj) == 0)
    {
        // loop the ride anim without re-creating the hitbox, so a fighter is only hit once per ride
        ActionStateChange(0, 1, 0, gobj, state, MF_SKIPHIT, 0);
        Fighter_AdvanceScript(gobj);
        fd->state_var.state_var1 = t;
    }
    return t;
}
void SpecialS_Anim(GOBJ *gobj)
{
    if (Cart_Tick(gobj, STATE_CARTLOOP) >= MINECART_FRAMES)
    {
        // Steve hops out, the cart rolls on empty
        Cart_Spawn(gobj, MINECART_SPEED);
        Fighter_EnterWait(gobj);
    }
}
void SpecialAirS_Anim(GOBJ *gobj)
{
    if (Cart_Tick(gobj, STATE_CARTLOOPAIR) >= MINECART_FRAMES)
    {
        Cart_Spawn(gobj, MINECART_SPEED * 0.85f);
        Fighter_EnterSpecialFall(gobj, 0, 0, 0, 0.8f, 20);
    }
}
void SpecialS_IASA(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    if (fd->state_var.state_var1 > 8)
    {
        float x = fd->phys.pos.X, y = fd->phys.pos.Y;
        if (Fighter_IASACheck_JumpF(gobj))
        {
            SteveData *d = SD(fd);
            Cart_Spawn(gobj, MINECART_SPEED);
            d->cx = x; d->cy = y;
        }
    }
}
void SpecialAirS_IASA(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    if (fd->state_var.state_var1 > 8)
    {
        float x = fd->phys.pos.X, y = fd->phys.pos.Y;
        if (Fighter_IASACheck_JumpAerial(gobj))
        {
            SteveData *d = SD(fd);
            Cart_Spawn(gobj, MINECART_SPEED * 0.85f);
            d->cx = x; d->cy = y; d->cart_air = 1;
        }
    }
}
void SpecialS_Phys(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    fd->phys.self_vel_ground.X = MINECART_SPEED * fd->facing_direction;
    Fighter_PhysGround_ApplyVelocity(gobj);
}
void SpecialAirS_Phys(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    fd->phys.self_vel.X = MINECART_SPEED * 0.85f * fd->facing_direction;
    Fighter_PhysAir_ApplyGravity(fd, fd->attr.gravity, fd->attr.terminal_velocity);
}
void SpecialS_Coll(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    if (Fighter_CollGround_PassLedge(gobj) == 0)
    {
        int t = fd->state_var.state_var1;
        Fighter_SetAirborne(fd);
        ActionStateChange(fd->state.frame, 1, 0, gobj, STATE_CARTLOOPAIR, 0x5000 | MF_SKIPHIT, 0);
        fd->state_var.state_var1 = t;
    }
}
void Cart_Land(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    int t = fd->state_var.state_var1;
    Fighter_SetGrounded2(fd);
    ActionStateChange(fd->state.frame, 1, 0, gobj, STATE_CARTLOOP, 0x5000 | MF_SKIPHIT, 0);
    fd->state_var.state_var1 = t;
}
void SpecialAirS_Coll(GOBJ *gobj) { Fighter_CollAir_GrabFacingLedgeWalljump(gobj, Fighter_Coll_CheckToPass, Cart_Land); }

///////////////////////
//   Up B: Elytra    //
///////////////////////
#define GLIDE_FRAMES 80
void Elytra_Start(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    if (fd->phys.air_state == 0) Fighter_SetAirborne(fd);
    // B-reverse
    if (fd->input.lstick.X * fd->facing_direction < -0.3f) fd->facing_direction = -fd->facing_direction;
    fd->phys.self_vel.X = 0;
    fd->phys.self_vel.Y = 3.2f;
    ActionStateChange(0, 1, 0, gobj, STATE_SPECIALHI, 0, 0);
    Fighter_AdvanceScript(gobj);
}
void SpecialHi(GOBJ *gobj) { Elytra_Start(gobj); }
void SpecialAirHi(GOBJ *gobj) { Elytra_Start(gobj); }

void SpecialHi_Anim(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    if (FrameTimerCheck(gobj) == 0 || fd->phys.self_vel.Y < 0.4f)
    {
        d->glide_angle = 0.15f;
        d->glide_speed = 1.7f;
        d->glide_timer = 0;
        ActionStateChange(0, 1, 0, gobj, STATE_SPECIALHIGLIDE, 0, 0);
        Fighter_AdvanceScript(gobj);
        SND(42);
    }
}
void SpecialHi_Phys(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    // pick the flight direction while rising
    if (fd->input.lstick.X * fd->facing_direction < -0.5f) fd->facing_direction = -fd->facing_direction;
    Fighter_PhysAir_ApplyGravity(fd, 0.16f, 3.0f);
    fd->phys.self_vel.X *= 0.9f;
}
void Elytra_Land(GOBJ *gobj) { Fighter_EnterSpecialLanding(gobj, 0, 20); }
void SpecialHi_Coll(GOBJ *gobj) { Fighter_CollAir_GrabBothLedgesWalljump(gobj, Elytra_Land); }

void Glide_Anim(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    if (FrameTimerCheck(gobj) == 0) ActionStateChange(0, 1, 0, gobj, STATE_SPECIALHIGLIDE, 0, 0);
    if (++d->glide_timer >= GLIDE_FRAMES) Fighter_EnterSpecialFall(gobj, 0, 0, 0, 0.8f, 20);
}
void Glide_Phys(GOBJ *gobj)
{
    // elytra flight: stick up/down pitches, diving builds speed, climbing bleeds it
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    float sy = fd->input.lstick.Y;
    d->glide_angle += sy * 0.045f;
    d->glide_angle += (-0.25f - d->glide_angle) * 0.02f; // drift nose-down
    if (d->glide_angle > 0.9f) d->glide_angle = 0.9f;
    if (d->glide_angle < -1.1f) d->glide_angle = -1.1f;
    d->glide_speed += -sin(d->glide_angle) * 0.07f - 0.008f;
    if (d->glide_timer < FW_FRAMES) d->glide_speed += 0.035f; // firework thrust
    if (d->glide_speed < 0.5f) { d->glide_speed = 0.5f; d->glide_angle -= 0.05f; }
    if (d->glide_speed > 2.6f) d->glide_speed = 2.6f;
    fd->phys.self_vel.X = cos(d->glide_angle) * d->glide_speed * fd->facing_direction;
    fd->phys.self_vel.Y = sin(d->glide_angle) * d->glide_speed;
    // lean the body forward like flying
    Steve_SetPitch(fd, -d->glide_angle);
}

///////////////////////
//   Down B: TNT     //
///////////////////////
int Steve_TNTPoints(SteveData *d)
{
    return d->mat[M_DIRT] * 2 + d->mat[M_WOOD] * 5 + d->mat[M_STONE] * 5 + d->mat[M_IRON] * 10;
}
void Steve_DownB(GOBJ *gobj, int air)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    fd->state_var.state_var1 = 0;
    // one TNT at a time, and it costs 50 points of materials
    if (d->tnt || Steve_TNTPoints(d) < 50)
    {
        ActionStateChange(0, 1, 0, gobj, air ? STATE_TNTFAILAIR : STATE_TNTFAIL, 0, 0);
        Fighter_AdvanceScript(gobj);
        SND(39);
        return;
    }
    ActionStateChange(0, 1, 0, gobj, air ? STATE_SPECIALLWAIR : STATE_SPECIALLW, 0, 0);
    Fighter_AdvanceScript(gobj);
}
void SpecialLw(GOBJ *gobj) { Steve_DownB(gobj, 0); }
void SpecialAirLw(GOBJ *gobj) { Steve_DownB(gobj, 1); }

int Steve_PayTNT(SteveData *d)
{
    const int kinds[4] = {M_DIRT, M_WOOD, M_STONE, M_IRON};
    const int pts[4] = {2, 5, 5, 10};
    if (Steve_TNTPoints(d) < 50) return 0;
    int need = 50;
    for (int k = 0; k < 4 && need > 0; k++)
        while (need > 0 && d->mat[kinds[k]] > 0)
        {
            d->mat[kinds[k]]--;
            need -= pts[k];
        }
    return 1;
}
void Steve_RemovePlate(SteveData *d)
{
    if (d->plate) GObj_Destroy(d->plate);
    d->plate = 0;
    for (int i = 0; i < MAX_DUST; i++)
        if (d->dust[i]) { GObj_Destroy(d->dust[i]); d->dust[i] = 0; }
    d->ndust = 0;
}
void Steve_RemoveTNT(SteveData *d)
{
    if (d->tnt) GObj_Destroy(d->tnt);
    d->tnt = 0;
    d->tnt_mode = 0;
    Coll_Set(d, TNT_SLOT, 0, 0, 0);
}
// highest damage of any fighter hitbox touching a box centred at cx, cy
float Steve_HitsAt(float cx, float cy, float half)
{
    float best = 0;
    for (int p = 0; p < 6; p++)
    {
        GOBJ *g = Fighter_GetGObj(p);
        if (!g) continue;
        FighterData *f = g->userdata;
        for (int h = 0; h < 4; h++)
        {
            ftHit *hb = &f->hitbox[h];
            if (!hb->active || hb->dmg_f <= 0) continue;
            float dx = hb->pos.X - cx, dy = hb->pos.Y - cy;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            float reach = half + hb->size;
            if (dx < reach && dy < reach && hb->dmg_f > best) best = hb->dmg_f;
        }
    }
    return best;
}
void Steve_TNTExplode(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    float x = d->tx, y = d->ty;
    Steve_SpawnExplosion(gobj, x, y);
    Steve_Poof(d, x, y, 6);
    SND(41);
    d->boom_x = x;
    d->boom_y = y + 5.0f;
    d->boom_t = 4;
    // Minecraft TNT breaks the blocks around it
    for (int i = 0; i < MAX_BLOCKS; i++)
    {
        if (!d->block[i]) continue;
        float dx = d->bx[i] - x, dy = d->by[i] - y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx < 22.0f && dy < 22.0f) d->bhp[i] = 0;
    }
    Steve_RemoveTNT(d);
    Steve_RemovePlate(d);
}
// the blast is Steve's hitbox 3 aimed at the TNT, so it works in any action state
void Steve_BoomHitbox(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    if (d->boom_t <= 0) return;
    ftHit *h = &fd->hitbox[3];
    float dir = fd->facing_direction < 0 ? -1.0f : 1.0f;
    float oz = (d->boom_x - fd->phys.pos.X) * dir, oy = d->boom_y - fd->phys.pos.Y;
    if (d->boom_t == 4)
    {
        // hitbox command: id 3, TopN, 16%, size 13, 55 deg, kbg 75, bkb 55, fire, fire L sound
        u32 cmd[6];
        cmd[0] = (0x0Bu << 26) | (3u << 23) | 16u;
        cmd[1] = ((u32)(13 * 256) << 16);
        cmd[2] = 0;
        cmd[3] = (55u << 23) | (75u << 14) | 0x13u;
        cmd[4] = (55u << 23) | (1u << 18) | (0x48u << 2) | 3u;
        cmd[5] = 0;
        int *save = fd->script.script_current;
        fd->script.script_current = (int *)cmd;
        ((void (*)(GOBJ *, void *))0x8007121C)(gobj, &fd->script);
        fd->script.script_current = save;
        h->offset.X = 0;
        h->offset.Y = oy;
        h->offset.Z = oz;
        Fighter_UpdateAllHitboxPos(gobj);
        h->pos_prev = h->pos;
    }
    else if (h->active)
    {
        h->offset.X = 0;
        h->offset.Y = oy;
        h->offset.Z = oz;
    }
    if (--d->boom_t == 0) h->active = 0;
}
// fighters (not counting the ones already on it) stepping onto the pressure plate
int Steve_PlateOccupied(SteveData *d)
{
    for (int p = 0; p < 6; p++)
    {
        GOBJ *g = Fighter_GetGObj(p);
        if (!g) continue;
        FighterData *f = g->userdata;
        if (f->phys.air_state) continue;
        float dx = f->phys.pos.X - d->plx, dy = f->phys.pos.Y - d->ply;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx < 5.5f && dy < 2.0f) return 1;
    }
    return 0;
}
void Steve_UpdateTNT(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    if (d->plate)
    {
        float fy;
        if (Steve_FindFloor(d->plx, d->ply + 1.0f, d->ply - 1.0f, &fy)) d->ply = fy;
        SetJObjPos(d->plate, d->plx, d->ply, 1);
        int occ = Steve_PlateOccupied(d);
        if (!occ) d->plate_armed = 1;
        else if (d->plate_armed && d->tnt) { SND(34); Steve_TNTExplode(gobj); return; }
    }
    if (!d->tnt) return;
    d->tnt_timer++;
    if (d->tnt_mode == 1)
    {
        // falls like the anvil, lands on floors and blocks
        d->tvy -= 0.25f;
        if (d->tvy < -4.0f) d->tvy = -4.0f;
        float fy;
        if (Steve_FindFloor(d->tx, d->ty + 2.0f, d->ty + d->tvy - 0.5f, &fy))
        {
            d->ty = fy;
            d->tvy = 0;
            d->tnt_mode = 2;
            Coll_Set(d, TNT_SLOT, 1, d->tx, d->ty);
        }
        else d->ty += d->tvy;
        if (d->ty < -300) { Steve_RemoveTNT(d); Steve_RemovePlate(d); return; }
    }
    else if (d->tnt_mode == 2)
    {
        float fy;
        if (!Steve_FindFloor(d->tx, d->ty + 1.0f, d->ty - 1.0f, &fy))
        {
            d->tnt_mode = 1;
            Coll_Set(d, TNT_SLOT, 0, 0, 0);
        }
    }
    // any hit, or time, sets it off
    if (d->tnt_hitcd > 0) d->tnt_hitcd--;
    else if (Steve_HitsAt(d->tx, d->ty + BLOCK_HALF, BLOCK_HALF) > 0) { Steve_TNTExplode(gobj); return; }
    if (d->tnt_timer > 900) { Steve_TNTExplode(gobj); return; }
    float sc = ((d->tnt_timer & 8) && d->tnt_timer > 780) ? 1.08f : 1.0f;
    SetJObjPos(d->tnt, d->tx, d->ty, sc);
}
void PlaceTNT_Anim(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    SteveAssets *a = SA(fd);
    if (fd->state_var.state_var1 == 0 && fd->state.frame >= 10)
    {
        fd->state_var.state_var1 = 1;
        if (!d->tnt && Steve_PayTNT(d))
        {
            d->tnt = SpawnModel(a->tnt, 6);
            d->tnt_timer = 0;
            d->tnt_hitcd = 10;
            d->tnt_mode = 1;
            d->tvy = 0;
            d->tx = fd->phys.pos.X + 9.0f * fd->facing_direction;
            d->ty = fd->phys.pos.Y + (fd->phys.air_state ? 0 : 1.0f);
            SetJObjPos(d->tnt, d->tx, d->ty, 1);
            Steve_RemovePlate(d);
        }
    }
    if (FrameTimerCheck(gobj) == 0)
    {
        if (fd->phys.air_state) Fighter_EnterFall(gobj);
        // holding B: walk back laying redstone, then place the pressure plate
        else if (d->tnt && (fd->input.held & PAD_BUTTON_B))
        {
            d->ndust = 0;
            d->dust_x = fd->phys.pos.X;
            fd->state_var.state_var1 = 0;
            ActionStateChange(0, 1, 0, gobj, STATE_TNTLOOP, 0, 0);
            Fighter_AdvanceScript(gobj);
        }
        else Fighter_EnterWait(gobj);
    }
}
void Steve_LayDust(GOBJ *gobj, float x)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    SteveAssets *a = SA(fd);
    if (d->ndust >= MAX_DUST || !a->dust) return;
    GOBJ *g = SpawnModel(a->dust, 6);
    d->dust[d->ndust++] = g;
    SetJObjPos(g, x, fd->phys.pos.Y, 1);
    Steve_SpawnFx(d, a, 40, 1, 1, x, fd->phys.pos.Y + 1.0f, 0, 0.3f, 0.02f, 14, 0.6f);
}
void TNTLoop_Anim(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    float moved = fd->phys.pos.X - d->dust_x;
    if (moved < 0) moved = -moved;
    if (fd->state_var.state_var1 == 0 || moved >= BLOCK_SIZE)
    {
        if (fd->state_var.state_var1 != 0) d->dust_x = fd->phys.pos.X;
        fd->state_var.state_var1 = 1;
        Steve_LayDust(gobj, fd->phys.pos.X);
    }
    if (!(fd->input.held & PAD_BUTTON_B) || d->ndust >= MAX_DUST || !d->tnt)
    {
        fd->state_var.state_var1 = 0;
        ActionStateChange(0, 1, 0, gobj, STATE_TNTPLATE, 0, 0);
        Fighter_AdvanceScript(gobj);
        return;
    }
    if (FrameTimerCheck(gobj) == 0) ActionStateChange(0, 1, 0, gobj, STATE_TNTLOOP, 0, 0);
}
void TNTLoop_Phys(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    fd->phys.self_vel_ground.X = -0.55f * fd->facing_direction;
    Fighter_PhysGround_ApplyVelocity(gobj);
}
void TNTPlate_Anim(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    SteveAssets *a = SA(fd);
    if (fd->state_var.state_var1 == 0 && fd->state.frame >= 6)
    {
        fd->state_var.state_var1 = 1;
        if (d->tnt && a->plate)
        {
            if (d->plate) GObj_Destroy(d->plate);
            d->plate = SpawnModel(a->plate, 6);
            d->plx = fd->phys.pos.X;
            d->ply = fd->phys.pos.Y;
            d->plate_armed = 0; // Steve is standing on it
            SetJObjPos(d->plate, d->plx, d->ply, 1);
        }
    }
    if (FrameTimerCheck(gobj) == 0) Fighter_EnterWait(gobj);
}
void Fail_Anim(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    if (FrameTimerCheck(gobj) == 0)
    {
        if (fd->phys.air_state) Fighter_EnterFall(gobj);
        else Fighter_EnterWait(gobj);
    }
}
void TNTFail_Coll(GOBJ *gobj) { if (Fighter_CollGround_PassLedge(gobj) == 0) ToAir(gobj, STATE_TNTFAILAIR); }
void TNTFailAir_Coll(GOBJ *gobj) { if (Fighter_CollAir_IgnoreLedge_NoCB(gobj) != 0) ToGround(gobj, STATE_TNTFAIL); }
void SpecialLw_Coll(GOBJ *gobj) { if (Fighter_CollGround_PassLedge(gobj) == 0) ToAir(gobj, STATE_SPECIALLWAIR); }
void SpecialAirLw_Coll(GOBJ *gobj) { if (Fighter_CollAir_IgnoreLedge_NoCB(gobj) != 0) ToGround(gobj, STATE_SPECIALLW); }
void Detonate_Coll(GOBJ *gobj) { if (Fighter_CollGround_PassLedge(gobj) == 0) ToAir(gobj, STATE_DETONATEAIR); }
void DetonateAir_Coll(GOBJ *gobj) { if (Fighter_CollAir_IgnoreLedge_NoCB(gobj) != 0) ToGround(gobj, STATE_DETONATE); }

///////////////////////
// Down air: anvil   //
///////////////////////
void Steve_DairStart(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    SteveAssets *a = SA(fd);
    fd->state_var.state_var1 = 0;
    if (d->mat[M_IRON] > 0 && a->anvil)
        ActionStateChange(0, 1, 0, gobj, STATE_DAIR, 0, 0);
    else
        ActionStateChange(0, 1, 0, gobj, STATE_DAIRNOIRON, 0, 0);
    Fighter_AdvanceScript(gobj);
}
// the ridden anvil sits right under Steve's feet; it lands (and turns solid) when it reaches a floor
void Dair_Anvil(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    if (!d->anvil || d->anvil_mode != 1) return;
    float ny = fd->phys.pos.Y + fd->phys.self_vel.Y - BLOCK_SIZE, fy;
    if (fd->phys.self_vel.Y < 0 && Steve_FindFloor(fd->phys.pos.X, fd->phys.pos.Y - BLOCK_SIZE + 2.0f, ny - 0.5f, &fy))
    {
        d->ax = fd->phys.pos.X;
        d->ay = fy;
        d->anvil_mode = 3;
        d->anvil_timer = 0;
        Coll_Set(d, ANVIL_SLOT, 1, d->ax, d->ay);
        SND(12);
    }
    else
    {
        d->ax = fd->phys.pos.X + fd->phys.self_vel.X;
        d->ay = ny;
    }
}
void Dair_Anim(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    SteveAssets *a = SA(fd);
    if (fd->state_var.state_var1 == 0 && fd->state.frame >= 12)
    {
        fd->state_var.state_var1 = 1;
        if (d->mat[M_IRON] > 0) d->mat[M_IRON]--;
        Steve_RemoveAnvil(d);
        d->anvil = SpawnModel(a->anvil, 6);
        d->anvil_mode = 1;
        d->anvil_timer = 0;
        d->ax = fd->phys.pos.X;
        d->ay = fd->phys.pos.Y - BLOCK_SIZE;
        // too close to the floor: the anvil lands right away and Steve pops up onto it
        float fy;
        if (Steve_FindFloor(d->ax, fd->phys.pos.Y + 1.0f, fd->phys.pos.Y - BLOCK_SIZE - 0.5f, &fy))
        {
            d->ay = fy;
            d->anvil_mode = 3;
            Coll_Set(d, ANVIL_SLOT, 1, d->ax, d->ay);
            SND(12);
            fd->phys.pos.Y = fy + BLOCK_SIZE + 0.2f;
            fd->phys.self_vel.Y = 0;
        }
        SetJObjPos(d->anvil, d->ax, d->ay, 1);
        SetJObjFacing(d->anvil, 1);
    }
    if (FrameTimerCheck(gobj) == 0)
    {
        ActionStateChange(0, 1, 0, gobj, STATE_DAIRFALL, MF_SKIPHIT, 0);
        Fighter_AdvanceScript(gobj);
    }
}
void Dair_Phys(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    if (fd->state.frame < 12)
    {
        // brief stall while the anvil is made
        fd->phys.self_vel.Y *= 0.75f;
        fd->phys.self_vel.X *= 0.92f;
    }
    else
    {
        fd->phys.self_vel.Y -= 0.3f;
        if (fd->phys.self_vel.Y < -4.0f) fd->phys.self_vel.Y = -4.0f;
        fd->phys.self_vel.X *= 0.95f;
        Dair_Anvil(gobj);
    }
}
void DairFall_Anim(GOBJ *gobj) {}
void DairFall_IASA(GOBJ *gobj) { Fighter_IASACheck_JumpAerial(gobj); } // jump off the anvil
void DairFall_Phys(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    fd->phys.self_vel.Y -= 0.3f;
    if (fd->phys.self_vel.Y < -4.0f) fd->phys.self_vel.Y = -4.0f;
    Fighter_PhysAir_ApplyAerialDrift(fd);
    float mx = fd->attr.aerial_drift_max * 0.4f;
    if (fd->phys.self_vel.X > mx) fd->phys.self_vel.X = mx;
    if (fd->phys.self_vel.X < -mx) fd->phys.self_vel.X = -mx;
    Dair_Anvil(gobj);
}
void Dair_Land(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    SteveData *d = SD(fd);
    if (d->anvil_mode == 1) Steve_RemoveAnvil(d); // landed before the anvil found a floor
    Fighter_SetGrounded2(fd);
    ActionStateChange(0, 1, 0, gobj, STATE_DAIRLAND, 0, 0);
    Fighter_AdvanceScript(gobj);
}
void Dair_Coll(GOBJ *gobj)
{
    if (Fighter_CollAir_IgnoreLedge_NoCB(gobj)) Dair_Land(gobj);
}
void DairNoIron_Anim(GOBJ *gobj)
{
    if (FrameTimerCheck(gobj) == 0) Fighter_EnterFall(gobj);
}
void DairNoIron_Phys(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    Fighter_PhysAir_ApplyGravityFastfall(gobj);
    Fighter_PhysAir_ApplyAerialDrift(fd);
}

void Dance_Anim(GOBJ *gobj) { if (FrameTimerCheck(gobj) == 0) Fighter_EnterWait(gobj); }

///////////////////////
//    State table    //
///////////////////////
__attribute__((used)) struct FtState move_logic[] = {
    {-1, 0x0, 0x1, 0x0, 0, 0, 0, 0, 0},
    {-1, 0x0, 0x1, 0x0, 0, 0, 0, 0, 0},
    // 343 mine
    {295, 0x340111, 0x12, 0x0, Mine_Anim, 0, Mine_Phys, Ground_Coll, Fighter_UpdateCameraBox},
    // 344 place block (air)
    {296, 0x340511, 0x12, 0x0, PlaceBlock_Anim, PlaceBlock_IASA, PlaceBlock_Phys, AirN_Coll, Fighter_UpdateCameraBox},
    // 345 minecart
    {297, 0x341012, 0x13, 0x0, SpecialS_Anim, SpecialS_IASA, SpecialS_Phys, SpecialS_Coll, Fighter_UpdateCameraBox},
    // 346 minecart air
    {298, 0x341012, 0x13, 0x0, SpecialAirS_Anim, SpecialAirS_IASA, SpecialAirS_Phys, SpecialAirS_Coll, Fighter_UpdateCameraBox},
    // 347 TNT launch
    {299, 0x340613, 0x14, 0x0, SpecialHi_Anim, 0, SpecialHi_Phys, SpecialHi_Coll, Fighter_UpdateCameraBox},
    // 348 elytra glide
    {300, 0x340613, 0x14, 0x0, Glide_Anim, 0, Glide_Phys, SpecialHi_Coll, Fighter_UpdateCameraBox},
    // 349 place TNT
    {301, 0x340214, 0x15, 0x0, PlaceTNT_Anim, 0, Ground_Phys, SpecialLw_Coll, Fighter_UpdateCameraBox},
    // 350 place TNT air
    {302, 0x340614, 0x15, 0x0, PlaceTNT_Anim, 0, Air_Phys, SpecialAirLw_Coll, Fighter_UpdateCameraBox},
    // 351 minecart carry + launch
    {54, 0x341012, 0x13, 0x0, (void *)0x800DD8C4, (void *)0x800DD92C, CartThrow_Phys, (void *)0x800DD990, Fighter_UpdateCameraBox},
    // 352 detonate
    {50, 0x340214, 0x15, 0x0, Fail_Anim, 0, Ground_Phys, Detonate_Coll, Fighter_UpdateCameraBox},
    // 353 detonate air
    {50, 0x340614, 0x15, 0x0, Fail_Anim, 0, Air_Phys, DetonateAir_Coll, Fighter_UpdateCameraBox},
    // 354 craft
    {49, 0x340111, 0x12, 0x0, Ground_ToWait, 0, Ground_Phys, Ground_Coll, Fighter_UpdateCameraBox},
    // 355 down air: anvil appears
    {72, 0x340613, 0x15, 0x0, Dair_Anim, 0, Dair_Phys, Dair_Coll, Fighter_UpdateCameraBox},
    // 356 down air: riding the anvil down
    {51, 0x340613, 0x15, 0x0, DairFall_Anim, DairFall_IASA, DairFall_Phys, Dair_Coll, Fighter_UpdateCameraBox},
    // 357 down air: landed on the anvil
    {77, 0x340214, 0x15, 0x0, Ground_ToWait, 0, Ground_Phys, Ground_Coll, Fighter_UpdateCameraBox},
    // 358 down air without iron
    {56, 0x340613, 0x15, 0x0, DairNoIron_Anim, 0, DairNoIron_Phys, AirN_Coll, Fighter_UpdateCameraBox},
    // 359 minecart ride loop
    {235, 0x341012, 0x13, 0x0, SpecialS_Anim, SpecialS_IASA, SpecialS_Phys, SpecialS_Coll, Fighter_UpdateCameraBox},
    // 360 minecart ride loop air
    {235, 0x341012, 0x13, 0x0, SpecialAirS_Anim, SpecialAirS_IASA, SpecialAirS_Phys, SpecialAirS_Coll, Fighter_UpdateCameraBox},
    // 361 dance (d-pad right)
    {236, 0x340111, 0x12, 0x0, Dance_Anim, 0, Ground_Phys, Ground_Coll, Fighter_UpdateCameraBox},
    // 362 TNT: walk back laying redstone
    {237, 0x340214, 0x15, 0x0, TNTLoop_Anim, 0, TNTLoop_Phys, Ground_Coll, Fighter_UpdateCameraBox},
    // 363 TNT: pressure plate
    {50, 0x340214, 0x15, 0x0, TNTPlate_Anim, 0, Ground_Phys, Ground_Coll, Fighter_UpdateCameraBox},
    // 364/365 TNT failure
    {229, 0x340214, 0x15, 0x0, Fail_Anim, 0, Ground_Phys, TNTFail_Coll, Fighter_UpdateCameraBox},
    {229, 0x340614, 0x15, 0x0, Fail_Anim, 0, Air_Phys, TNTFailAir_Coll, Fighter_UpdateCameraBox},
    // 366/367 minecart failure (no iron)
    {241, 0x341012, 0x13, 0x0, Fail_Anim, 0, Ground_Phys, CartFail_Coll, Fighter_UpdateCameraBox},
    {241, 0x341012, 0x13, 0x0, Fail_Anim, 0, Air_Phys, CartFailAir_Coll, Fighter_UpdateCameraBox},
};
