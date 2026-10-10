#include "mex.h"

// Main menu: shows "Yahiamice's Melee Steve vX.YY" in the corner (overlay camera from SvLoad.dat).
#define MM_THINK ((void (*)())0x8022DD38)
#define MM_LOAD ((void (*)(void *))0x8022DDA8)
#define OVL_LINK 5
#define VER_X 196.0f
#define VER_Y -222.0f
#define VER_SCALE 1.5f

GOBJ *mm_cam, *mm_ver;

void minor_load(void *data)
{
    MM_LOAD(data);
    mm_cam = mm_ver = 0;
    HSD_Archive *arc = Archive_LoadFile("SvLoad.dat");
    if (!arc) return;
    COBJDesc *cd = Archive_GetPublicAddress(arc, "svLoad_cam");
    JOBJDesc *vd = Archive_GetPublicAddress(arc, "svVer_joint");
    if (!cd || !vd) return;
    GOBJ *c = GObj_Create(2, 3, 128);
    COBJ *cobj = COBJ_LoadDesc(cd);
    GObj_AddObject(c, 1, cobj);
    GOBJ_InitCamera(c, CObjThink_Common, 7);
    c->cobj_links = 1 << OVL_LINK;
    mm_cam = c;
    mm_ver = GOBJ_EZCreator(4, 5, 0, 0, 0, HSD_OBJKIND_JOBJ, vd, 0, 0, GXLink_Common, OVL_LINK, 0);
    JOBJ *j = mm_ver->hsd_object;
    j->trans.X = VER_X; j->trans.Y = VER_Y;
    j->scale.X = VER_SCALE; j->scale.Y = VER_SCALE; j->scale.Z = 1;
    JOBJ_SetMtxDirtySub(j);
}

void minor_think()
{
    MM_THINK();
}

void minor_exit(void *data)
{
    mm_cam = mm_ver = 0;
}
