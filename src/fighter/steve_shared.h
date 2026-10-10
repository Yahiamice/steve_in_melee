// State shared between Steve (steve.c) and Kirby's Steve copy (kirby.c).
// Lives in the match heap so Slippi rollback saves/restores it.
typedef struct KbShared
{
    int placed[6];       // blocks placed with the copy ability, by Kirby's port
    GOBJ *tag[6];        // name tag
    GOBJ *shadow[6];     // mob shadow
    GOBJ *dirt[6];       // dirt block in hand
} KbShared;
