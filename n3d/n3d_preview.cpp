#include "n3d_preview.h"

#include "n3d_runtime.h"
#include "n3d_world.h"
#include "../wl_def.h"

#include <fstream>
#include <stdio.h>
#include <string>
#include <string.h>
#include <vector>

namespace
{

bool LoadN3DPalette(const std::string &path, SDL_Color palette[256], std::string &error)
{
    std::ifstream f(path.c_str(), std::ios::binary | std::ios::ate);
    if(!f)
    {
        error = "Unable to open Nitemare3D palette: " + path;
        return false;
    }

    const std::streamoff end = f.tellg();
    if(end < 769)
    {
        error = "GAME.PAL is too small for a PCX 256-color palette trailer";
        return false;
    }

    std::vector<unsigned char> bytes(static_cast<size_t>(end));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char *>(&bytes[0]), static_cast<std::streamsize>(bytes.size()));
    if(!f)
    {
        error = "Unable to read Nitemare3D palette: " + path;
        return false;
    }

    const size_t marker = bytes.size() - 769;
    if(bytes[marker] != 0x0c)
    {
        error = "GAME.PAL has no PCX 256-color palette trailer";
        return false;
    }

    const size_t base = marker + 1;
    for(size_t i = 0; i < 256; ++i)
    {
        palette[i].r = bytes[base + i * 3 + 0];
        palette[i].g = bytes[base + i * 3 + 1];
        palette[i].b = bytes[base + i * 3 + 2];
        palette[i].unused = 0;
    }
    return true;
}

int StartAngle(n3d::PlayerDirection direction)
{
    switch(direction)
    {
        case n3d::DirNorth: return 90;
        case n3d::DirEast: return 0;
        case n3d::DirSouth: return 270;
        case n3d::DirWest: return 180;
    }
    return 0;
}

}

extern void BuildTables(void);
extern boolean SetViewSize(unsigned width, unsigned height);
extern void N3D_WallPreviewRefresh(void);

int N3D_RunPreview(const char *dataDir, int episode, int level)
{
    const std::string root = (dataDir && *dataDir) ? dataDir : ".";
    std::string error;

    if(!n3d::loadRuntime(root, episode, level, error))
    {
        fprintf(stderr, "Nitemare3D preview load failed: %s\n", error.c_str());
        return 1;
    }

    const n3d::WorldState *world = n3d::runtimeWorld();
    if(!world || !world->playerStart.found)
    {
        fprintf(stderr, "Nitemare3D preview has no player START\n");
        return 1;
    }

    if(SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        fprintf(stderr, "Unable to init SDL video: %s\n", SDL_GetError());
        return 1;
    }

    // First renderer parity checkpoint uses the original indexed framebuffer.
    screenWidth = 320;
    screenHeight = 200;
    screenBits = 8;
    usedoublebuffering = false;

    VL_SetVGAPlaneMode();
    SDL_WM_SetCaption("Nitemare 3D - Wolf4SDL renderer preview", 0);

    SDL_Color palette[256];
    if(!LoadN3DPalette(n3d::joinPath(root, "GAME.PAL"), palette, error))
    {
        fprintf(stderr, "Nitemare3D preview palette failed: %s\n", error.c_str());
        SDL_Quit();
        return 1;
    }
    SDL_SetColors(screen, palette, 0, 256);
    SDL_SetColors(screenBuffer, palette, 0, 256);

    BuildTables();
    SetViewSize(304, 152);

    if(!n3d::copyWolfTileMap(reinterpret_cast<uint8_t *>(&tilemap[0][0]), sizeof(tilemap)))
    {
        fprintf(stderr, "Nitemare3D preview could not install the render map\n");
        SDL_Quit();
        return 1;
    }

    static objtype previewPlayer;
    memset(&previewPlayer, 0, sizeof(previewPlayer));
    player = &previewPlayer;
    player->tilex = static_cast<short>(world->playerStart.x);
    player->tiley = static_cast<short>(world->playerStart.y);
    player->x = (static_cast<int32_t>(world->playerStart.x) << TILESHIFT) + TILEGLOBAL / 2;
    player->y = (static_cast<int32_t>(world->playerStart.y) << TILESHIFT) + TILEGLOBAL / 2;
    player->angle = static_cast<short>(StartAngle(world->playerStart.direction));

    bool running = true;
    while(running)
    {
        SDL_Event event;
        while(SDL_PollEvent(&event))
        {
            if(event.type == SDL_QUIT)
                running = false;
            else if(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
                running = false;
        }

        const Uint8 *keys = SDL_GetKeyState(0);
        if(keys[SDLK_LEFT])
        {
            player->angle += 2;
            if(player->angle >= ANGLES)
                player->angle -= ANGLES;
        }
        if(keys[SDLK_RIGHT])
        {
            player->angle -= 2;
            if(player->angle < 0)
                player->angle += ANGLES;
        }

        N3D_WallPreviewRefresh();
        SDL_Delay(16);
    }

    SDL_Quit();
    return 0;
}
