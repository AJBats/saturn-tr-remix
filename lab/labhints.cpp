// The draw order hints from the scene (labrender.h).

#include "labrender.h"

void hintsLabInit()
{
    uint32_t count;
    const DepthHint* hints = sceneArray<const DepthHint>("HINT", &count);
    hintsInit(hints, count);
    const int32_t* lara = sceneArray<const int32_t>("LARA");
    hintsLara(lara[0], lara[1], lara[2]);
}

int32_t hintsSceneDraws(const DrawThing* things, uint32_t count, HintedDraw* out, int32_t room)
{
    uint32_t masks[2];
    hintsMasks(masks);
    int32_t n = 0;
    HintFaces faces;
    for (uint32_t i = 0; i < count && n < room; i++)
    {
        const DrawThing &t = things[i];
        if (t.kind != NO_THING && (masks[t.kind] >> (t.index & 31) & 1) && hintsOf(t, faces)) {
            out[n++] = { i, t };
        }
    }
    return n;
}
