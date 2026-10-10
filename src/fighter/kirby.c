#include "mex.h"

// Kirby's Steve copy ability (kbFunction in PlKbCpSv.dat).
// The hat is Steve's haircut; B places a dirt block (Steve's own code does the block work,
// reached through a pointer Steve stores in his ext_attr). After 10 blocks Kirby loses the ability.
// All functions are global on purpose (MexTK mis-relocates static functions).

#define KB_BLOCKS 10
#define STEVE_PLACE_OFS 0x1F8 // ext_attr offset where Steve's OnLoad stores Steve_KirbyPlace
#define STEVE_SHARED_OFS 0x1FC // ext_attr offset of the heap block (KbShared) Steve allocates each match
#include "steve_shared.h"

typedef int (*SteveKirbyPlace)(GOBJ *kirby, int air);

typedef struct KbVars
{
    int placed;      // 0x222c charVar1: blocks placed with this ability
    int var2;        // 0x2230
    int var3;        // 0x2234
    int copy_index;  // 0x2238
    JOBJ *copy_jobj; // 0x223c
    int parts_num;   // 0x2240
    void *parts;     // 0x2244
    void *x2248;
    void *x224c;
    char vis[0x10]; // 0x2250
} KbVars;

KbVars *KV(FighterData *fd) { return (KbVars *)((char *)fd + 0x222C); }
// All state lives in Steve's heap block (rolled back by Slippi); no globals here.
#define ASSET(ext, ofs) (*(JOBJDesc **)((char *)(ext) + 0x100 + (ofs)))

void *Kb_SteveExt(int kind);
KbShared *Kb_Shared(int kind)
{
    void *ext = Kb_SteveExt(kind);
    return ext ? *(KbShared **)((char *)ext + STEVE_SHARED_OFS) : 0;
}

GOBJ *Kb_Spawn(JOBJDesc *desc)
{
    if (!desc) return 0;
    return GOBJ_EZCreator(8, 11, 0, 0, 0, HSD_OBJKIND_JOBJ, desc, 0, 0, GXLink_Common, 6, 0);
}
void Kb_Pos(GOBJ *g, float x, float y, float z, float s)
{
    JOBJ *j = g->hsd_object;
    j->trans.X = x; j->trans.Y = y; j->trans.Z = z;
    j->scale.X = s; j->scale.Y = s; j->scale.Z = s;
    JOBJ_SetMtxDirtySub(j);
}
void Kb_SetAlpha(GOBJ *g, float a)
{
    JOBJ *j = g->hsd_object;
    for (DOBJ *d = j->dobj; d; d = d->next)
        if (d->mobj && d->mobj->mat) d->mobj->mat->alpha = a;
}
void Kb_Clear(KbShared *k, int port)
{
    if (!k) return;
    if (k->tag[port]) GObj_Destroy(k->tag[port]);
    if (k->shadow[port]) GObj_Destroy(k->shadow[port]);
    if (k->dirt[port]) GObj_Destroy(k->dirt[port]);
    k->tag[port] = k->shadow[port] = k->dirt[port] = 0;
}
void *Kb_SteveExt(int kind)
{
    for (int p = 0; p < 6; p++)
    {
        GOBJ *g = Fighter_GetGObj(p);
        if (!g) continue;
        FighterData *f = g->userdata;
        if (f->kind == kind && f->ftData && f->ftData->ext_attr) return f->ftData->ext_attr;
    }
    return 0;
}
void OnKirbyFrame(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    KbVars *kv = KV(fd);
    int port = fd->ply % 6;
    void *ext = Kb_SteveExt(kv->copy_index);
    if (!ext) return;
    KbShared *k = *(KbShared **)((char *)ext + STEVE_SHARED_OFS);
    if (!k) return;
    if (!kv->copy_jobj) { Kb_Clear(k, port); return; }
    GOBJ **kb_tag = k->tag, **kb_shadow = k->shadow, **kb_dirt = k->dirt;
    if (!kb_tag[port]) kb_tag[port] = Kb_Spawn(ASSET(ext, 0x48));
    if (!kb_shadow[port]) kb_shadow[port] = Kb_Spawn(ASSET(ext, 0x4C));
    if (!kb_dirt[port]) kb_dirt[port] = Kb_Spawn(ASSET(ext, 0x50));
    float x = fd->phys.pos.X, y = fd->phys.pos.Y;
    int hidden = fd->flags.invisible;
    if (kb_tag[port]) { Kb_Pos(kb_tag[port], x, y + 15.5f, 0, 1); Kb_SetAlpha(kb_tag[port], hidden ? 0 : 1); }
    // shadow snaps to the ground and fades out by one block (10 units) of height
    if (kb_shadow[port])
    {
        Vec3 pos, nrm;
        int idx, kind;
        float a = 0, gy = y;
        if (GrColl_RaycastGround(&pos, &idx, &kind, &nrm, (Vec3 *)-1, (Vec3 *)-1, (Vec3 *)-1, 0, x, y + 1.0f, x, y - 40.0f, 0))
        {
            gy = pos.Y;
            a = 1.0f - (y - gy) / 10.0f;
            if (a < 0) a = 0;
            if (a > 1) a = 1;
        }
        if (hidden) a = 0;
        Kb_Pos(kb_shadow[port], x, gy + 0.05f, 0, 1);
        Kb_SetAlpha(kb_shadow[port], a * 0.75f);
    }
    // dirt block held out in the hand
    if (kb_dirt[port])
    {
        Vec3 hp = {x + 4.0f * fd->facing_direction, y + 4.0f, 0};
        int bone = fd->ftData && fd->ftData->coll ? fd->ftData->coll->ecb_bone_arm_right : 0;
        if (bone > 0 && fd->bones && fd->bones[bone].joint) JOBJ_GetWorldPosition(fd->bones[bone].joint, 0, &hp);
        Kb_Pos(kb_dirt[port], hp.X, hp.Y - 1.4f, hp.Z, 0.28f);
        JOBJ *j = kb_dirt[port]->hsd_object;
        if (hidden) j->flags |= 0x10; else j->flags &= ~0x10;
    }
}

SteveKirbyPlace Kb_FindPlace(int kind)
{
    for (int p = 0; p < 6; p++)
    {
        GOBJ *g = Fighter_GetGObj(p);
        if (!g) continue;
        FighterData *f = g->userdata;
        if (f->kind != kind || !f->ftData || !f->ftData->ext_attr) continue;
        SteveKirbyPlace fn = *(SteveKirbyPlace *)((char *)f->ftData->ext_attr + STEVE_PLACE_OFS);
        if (fn) return fn;
    }
    return 0;
}

// same steps as the vanilla copy-hat setup, but with Steve's own cap data
void OnKirbySwallow(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    KbVars *kv = KV(fd);
    KbShared *k = Kb_Shared(kv->copy_index);
    if (k) k->placed[fd->ply % 6] = 0;
    if (kv->copy_jobj) return;
    void **cp = MEX_GetKirbyCpData(kv->copy_index);
    if (!cp || !cp[0]) return;
    void *(*obj_alloc)(void *) = (void *)0x8037ABC8;
    void (*parts_push)() = (void *)0x80074148;
    void (*parts_pop)() = (void *)0x80074170;
    void (*parts_lookup)(GOBJ *, JOBJ *, void *) = (void *)0x80075650;
    void (*parts_vis)(void *, void *, int, void *, void *) = (void *)0x8007487C;
    kv->parts = obj_alloc((void *)0x80459080);
    parts_push();
    kv->copy_jobj = JOBJ_LoadJoint(cp[0]);
    fd->flags.has_model_addition = 1;
    parts_pop();
    parts_lookup(gobj, kv->copy_jobj, &kv->parts_num);
    parts_vis(&cp[1], kv->vis, 0, &kv->parts_num, &kv->parts_num);
}

void OnKirbyLoseAbility(GOBJ *gobj)
{
    FighterData *fd = gobj->userdata;
    KbVars *kv = KV(fd);
    int port = fd->ply % 6;
    Kb_Clear(Kb_Shared(kv->copy_index), port);
    if (!kv->copy_jobj) return;
    void (*jobj_remove_all)(JOBJ *) = (void *)0x80371590;
    void (*obj_free)(void *, void *) = (void *)0x8037AD20;
    jobj_remove_all(kv->copy_jobj);
    kv->copy_jobj = 0;
    obj_free((void *)0x80459080, kv->parts);
    kv->parts = 0;
}

void Kb_Place(GOBJ *gobj, int air)
{
    FighterData *fd = gobj->userdata;
    KbVars *kv = KV(fd);
    KbShared *k = Kb_Shared(kv->copy_index);
    if (!k) return;
    int *placed = &k->placed[fd->ply % 6];
    SteveKirbyPlace place = Kb_FindPlace(kv->copy_index);
    if (place && place(gobj, air))
    {
        (*placed)++;
        if (air) fd->phys.self_vel.Y = 0;
    }
    if (!air) ActionStateChange(0, 1, 0, gobj, ASID_SQUATRV, 0, 0);
    // out of blocks: the ability pops off like a taunt would
    if (*placed >= KB_BLOCKS && kv->copy_jobj)
    {
        *placed = 0;
        void (*lose)(GOBJ *, int) = (void *)0x800F5D04;
        lose(gobj, 1);
    }
}

void SpecialN(GOBJ *gobj) { Kb_Place(gobj, 0); }
void SpecialAirN(GOBJ *gobj) { Kb_Place(gobj, 1); }
