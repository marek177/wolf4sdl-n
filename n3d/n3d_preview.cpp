#include "n3d_preview.h"

#include "n3d_runtime.h"
#include "n3d_collision.h"
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

void FrontCell(const objtype *ob, int &x, int &y)
{
    x = ob->tilex;
    y = ob->tiley;

    int angle = ob->angle;
    if(angle < 0)
        angle += ANGLES;
    if(angle >= ANGLES)
        angle %= ANGLES;

    if(angle < ANGLES / 8 || angle >= 7 * ANGLES / 8)
        ++x;
    else if(angle < 3 * ANGLES / 8)
        --y;
    else if(angle < 5 * ANGLES / 8)
        --x;
    else
        ++y;
}

int PlayerSector(const objtype *ob)
{
    int angle = ob->angle;
    if(angle < 0)
        angle += ANGLES;
    if(angle >= ANGLES)
        angle %= ANGLES;

    return ((angle + ANGLES / 16) / (ANGLES / 8)) & 7;
}

const char *DoorUseName(n3d::DoorUseResult result)
{
    switch(result)
    {
        case n3d::DoorUseNone: return "nothing";
        case n3d::DoorUseToggled: return "door toggled";
        case n3d::DoorUseNeedsKey: return "locked: key required";
        case n3d::DoorUseNeedsIdCard: return "locked: ID card required";
        case n3d::DoorUseRemoteOnly: return "remote-controlled door";
        case n3d::DoorUseDirectionalGate: return "direction-gated door";
        case n3d::DoorUseLatched: return "latched passable door";
    }
    return "unknown";
}


void FirePreviewWeapon(objtype *ob)
{
    n3d::ObjectRuntime *objects = n3d::runtimeObjects();
    if(!objects)
        return;

    n3d::InventoryState &inv = objects->inventory();
    const uint8_t weapon = inv.activeWeapon;

    if(weapon == 2)
    {
        if(!objects->consumeWeaponAmmo(2))
        {
            printf("PISTOL: no ammo\n");
            return;
        }

        const n3d::PlayerHitReport hit =
            n3d::runtimeFireHitscan(ob->x >> 10,
                                   ob->y >> 10,
                                   2, 1, 80);
        printf("PISTOL: hits=%u kills=%u scoreDelta=%ld ammo=%u\n",
               hit.hitCount,
               hit.killCount,
               (long)hit.scoreDelta,
               (unsigned)inv.pistolAmmo);
        return;
    }

    if(weapon != 0 && weapon != 1 && weapon != 3)
        return;

    const int directionX =
        static_cast<int>(costable[ob->angle] / 4);
    const int directionY =
        -static_cast<int>(sintable[ob->angle] / 4);

    const n3d::ProjectileFireResult fire =
        n3d::runtimeFireProjectile(ob->x >> 10,
                                   ob->y >> 10,
                                   weapon,
                                   directionX,
                                   directionY);

    if(fire == n3d::ProjectileFireAccepted)
    {
        printf("PROJECTILE weapon %u: accepted, active=%u plasma=%u wand=%u\n",
               (unsigned)weapon,
               n3d::runtimeProjectilesConst()
                   ? n3d::runtimeProjectilesConst()->activeCount()
                   : 0u,
               (unsigned)inv.plasmaAmmo,
               (unsigned)inv.wandAmmo);
    }
    else if(fire == n3d::ProjectileFirePoolFull)
    {
        printf("PROJECTILE: pool full, ammo preserved\n");
    }
    else if(fire == n3d::ProjectileFireNoAmmo)
    {
        printf("PROJECTILE: no ammo\n");
    }
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

    printf("Nitemare3D Wolf4SDL preview: E%dM%d\n", episode, level);
    printf("Controls: W/Up forward, S/Down backward, A/D strafe, Left/Right turn, Shift fast, E/Space use, 1-4 weapon, F fire, Esc quit\n");
    printf("Collision: recovered 27/28-unit probes; door states 0/4 pass, states 1/2/3 block\n");
    printf("GUARD preview: recovered LOS/proximity, strategy-0 chase and state-4 direct player damage\n");

    Uint32 nextDoorMotion = SDL_GetTicks() + 39;
    Uint32 nextDoorSlow = SDL_GetTicks() + 122;
    Uint32 nextProjectile = SDL_GetTicks() + 40;

    bool running = true;
    bool deathReported = false;
    bool previousFireDown = false;
    while(running)
    {
        SDL_Event event;
        while(SDL_PollEvent(&event))
        {
            if(event.type == SDL_QUIT)
                running = false;
            else if(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
                running = false;
            else if(event.type == SDL_KEYDOWN &&
                    (event.key.keysym.sym == SDLK_e ||
                     event.key.keysym.sym == SDLK_SPACE) &&
                    (!n3d::runtimeObjectsConst() ||
                     n3d::runtimeObjectsConst()->inventory().gameState != 2))
            {
                int useX, useY;
                FrontCell(player, useX, useY);
                const n3d::DoorUseResult useResult =
                    n3d::runtimeUseDoor(useX, useY, PlayerSector(player));
                if(useResult != n3d::DoorUseNone)
                    printf("USE %d,%d: %s\n", useX, useY, DoorUseName(useResult));
            }
            else if(event.type == SDL_KEYDOWN)
            {
                n3d::ObjectRuntime *objects = n3d::runtimeObjects();
                if(!objects || objects->inventory().gameState == 2)
                    continue;

                int requestedWeapon = -1;
                if(event.key.keysym.sym == SDLK_1) requestedWeapon = 0;
                else if(event.key.keysym.sym == SDLK_2) requestedWeapon = 1;
                else if(event.key.keysym.sym == SDLK_3) requestedWeapon = 2;
                else if(event.key.keysym.sym == SDLK_4) requestedWeapon = 3;

                if(requestedWeapon >= 0)
                {
                    n3d::InventoryState &inv = objects->inventory();
                    if((inv.ownedWeapons & (1u << requestedWeapon)) != 0)
                    {
                        // Preview shortcut for the already-recovered switch
                        // transition machine; HUD raise/lower animation follows
                        // in the dedicated weapon-overlay layer.
                        inv.activeWeapon = static_cast<uint8_t>(requestedWeapon);
                        inv.pendingWeapon = static_cast<uint8_t>(requestedWeapon);
                        inv.weaponSelectionMode =
                            requestedWeapon == 2 ? 1 : 2;
                        printf("WEAPON selected: %d\n", requestedWeapon);
                    }
                }


            }
        }

        const Uint8 *keys = SDL_GetKeyState(0);
        const n3d::ObjectRuntime *playerRuntime = n3d::runtimeObjectsConst();
        const bool playerAlive =
            !playerRuntime || playerRuntime->inventory().gameState != 2;

        const bool fireDown = playerAlive && keys[SDLK_f];
        n3d::ObjectRuntime *fireRuntime = n3d::runtimeObjects();
        if(fireDown && fireRuntime &&
           fireRuntime->acceptFireAttempt(!previousFireDown))
        {
            FirePreviewWeapon(player);
        }
        previousFireDown = fireDown;

        if(playerAlive && keys[SDLK_LEFT])
        {
            player->angle += 2;
            if(player->angle >= ANGLES)
                player->angle -= ANGLES;
        }
        if(playerAlive && keys[SDLK_RIGHT])
        {
            player->angle -= 2;
            if(player->angle < 0)
                player->angle += ANGLES;
        }

        const int moveSpeed =
            (keys[SDLK_LSHIFT] || keys[SDLK_RSHIFT]) ? 4 : 2;

        int desiredX = 0;
        int desiredY = 0;

        if(keys[SDLK_w] || keys[SDLK_UP])
        {
            desiredX += static_cast<int>(
                (static_cast<int32_t>(costable[player->angle]) * moveSpeed) >> 16);
            desiredY -= static_cast<int>(
                (static_cast<int32_t>(sintable[player->angle]) * moveSpeed) >> 16);
        }
        if(keys[SDLK_s] || keys[SDLK_DOWN])
        {
            desiredX -= static_cast<int>(
                (static_cast<int32_t>(costable[player->angle]) * moveSpeed) >> 16);
            desiredY += static_cast<int>(
                (static_cast<int32_t>(sintable[player->angle]) * moveSpeed) >> 16);
        }

        int strafeAngle = player->angle + ANGLES / 4;
        if(strafeAngle >= ANGLES)
            strafeAngle -= ANGLES;

        if(keys[SDLK_a])
        {
            desiredX += static_cast<int>(
                (static_cast<int32_t>(costable[strafeAngle]) * moveSpeed) >> 16);
            desiredY -= static_cast<int>(
                (static_cast<int32_t>(sintable[strafeAngle]) * moveSpeed) >> 16);
        }
        if(keys[SDLK_d])
        {
            desiredX -= static_cast<int>(
                (static_cast<int32_t>(costable[strafeAngle]) * moveSpeed) >> 16);
            desiredY += static_cast<int>(
                (static_cast<int32_t>(sintable[strafeAngle]) * moveSpeed) >> 16);
        }

        if(playerAlive && (desiredX != 0 || desiredY != 0))
        {
            const n3d::ObjectRuntime *objectsBefore = n3d::runtimeObjectsConst();
            const uint8_t oldKeyMask =
                objectsBefore ? objectsBefore->inventory().keyMask : 0;
            const uint8_t oldIdCardMask =
                objectsBefore ? objectsBefore->inventory().idCardMask : 0;
            const uint8_t oldPentagramMask =
                objectsBefore ? objectsBefore->inventory().pentagramMask : 0;
            const uint8_t oldOwnedWeapons =
                objectsBefore ? objectsBefore->inventory().ownedWeapons : 0;
            const uint32_t oldScore =
                objectsBefore ? objectsBefore->inventory().score : 0;

            n3d::CollisionContext collision;
            collision.doorPassage = &n3d::runtimeDoorPassageQuery;
            collision.objectTouch = &n3d::runtimeObjectTouchQuery;
            collision.userData = 0;

            const int32_t currentX = player->x >> 10;
            const int32_t currentY = player->y >> 10;
            const n3d::MoveResult moved =
                n3d::movePlayer(*world,
                                currentX,
                                currentY,
                                desiredX,
                                desiredY,
                                collision);

            player->x = moved.x << 10;
            player->y = moved.y << 10;
            player->tilex = static_cast<short>(n3d::worldToTile(moved.x));
            player->tiley = static_cast<short>(n3d::worldToTile(moved.y));

            const n3d::ObjectRuntime *objectsAfter = n3d::runtimeObjectsConst();
            if(objectsAfter)
            {
                const n3d::InventoryState &inventory = objectsAfter->inventory();
                if(inventory.keyMask != oldKeyMask)
                    printf("KEY pickup: mask 0x%02X -> 0x%02X\n",
                           (unsigned)oldKeyMask, (unsigned)inventory.keyMask);
                if(inventory.idCardMask != oldIdCardMask)
                    printf("ID CARD pickup: mask 0x%02X -> 0x%02X\n",
                           (unsigned)oldIdCardMask, (unsigned)inventory.idCardMask);
                if(inventory.pentagramMask != oldPentagramMask)
                    printf("PENTAGRAM pickup: mask 0x%02X -> 0x%02X\n",
                           (unsigned)oldPentagramMask, (unsigned)inventory.pentagramMask);
                if(inventory.ownedWeapons != oldOwnedWeapons)
                    printf("WEAPON pickup: owned 0x%02X -> 0x%02X, pending=%u\n",
                           (unsigned)oldOwnedWeapons,
                           (unsigned)inventory.ownedWeapons,
                           (unsigned)inventory.pendingWeapon);
                if(inventory.score != oldScore)
                    printf("SCORE: %lu -> %lu\n",
                           (unsigned long)oldScore,
                           (unsigned long)inventory.score);
            }
        }

        const Uint32 now = SDL_GetTicks();

        // DOS-reference preview cadence only: missed buckets are deliberately
        // not replayed. Runtime scheduling will later replace these clocks.
        if(static_cast<Sint32>(now - nextDoorMotion) >= 0)
        {
            n3d::runtimeTickDoorMotion();
            nextDoorMotion = now + 39;
        }

        if(static_cast<Sint32>(now - nextProjectile) >= 0)
        {
            const n3d::ProjectileUpdateReport projectile =
                n3d::runtimeTickProjectiles(now, 20, 1, 80);
            if(projectile.guardHits != 0 || projectile.impacts != 0)
            {
                printf("PROJECTILES: impacts=%u guardHits=%u kills=%u active=%u\n",
                       projectile.impacts,
                       projectile.guardHits,
                       projectile.guardKills,
                       n3d::runtimeProjectilesConst()
                           ? n3d::runtimeProjectilesConst()->activeCount()
                           : 0u);
            }
            nextProjectile = now + 40;
        }

        if(static_cast<Sint32>(now - nextDoorSlow) >= 0)
        {
            n3d::runtimeTickDoorAutoClose(player->tilex, player->tiley);

            n3d::ObjectRuntime *cadenceRuntime = n3d::runtimeObjects();
            if(cadenceRuntime)
                cadenceRuntime->tickWeaponCadence();

            const n3d::ObjectRuntime *beforeCombat = n3d::runtimeObjectsConst();
            const uint8_t oldHealth =
                beforeCombat ? beforeCombat->inventory().health : 0;
            const uint16_t oldGameState =
                beforeCombat ? beforeCombat->inventory().gameState : 0;

            n3d::runtimeTickGuards(player->x >> 10, player->y >> 10, 1);

            const n3d::ObjectRuntime *afterCombat = n3d::runtimeObjectsConst();
            if(afterCombat)
            {
                const n3d::InventoryState &state = afterCombat->inventory();
                if(state.health != oldHealth)
                    printf("PLAYER HP: %u -> %u\n",
                           (unsigned)oldHealth, (unsigned)state.health);

                if(oldGameState != 2 && state.gameState == 2 && !deathReported)
                {
                    printf("PLAYER DEAD: attacker OBJECT index=%u, gameState=2\n",
                           (unsigned)state.deathAttackerObjectIndex);
                    deathReported = true;
                }
            }

            nextDoorSlow = now + 122;
        }

        // Door terminal state changes alter both visibility and collision.
        n3d::copyWolfTileMap(reinterpret_cast<uint8_t *>(&tilemap[0][0]), sizeof(tilemap));

        N3D_WallPreviewRefresh();
        SDL_Delay(16);
    }

    SDL_Quit();
    return 0;
}
