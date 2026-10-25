// Saturn platform entry: hardware setup, then OpenLara's game loop.

#include "game.h"
#include "system.h"

int32 fps;

void updateInput()
{
    keys = 0; // TODO SMPC pad
}

int main()
{
    systemInit();

    gameInit();

    while (1)
    {
        updateInput();

        gameUpdate(1);

        gameRender();

        // game logic runs at 30 Hz
        waitVblank();
        renderSwap();
        waitVblank();
    }

    return 0;
}
