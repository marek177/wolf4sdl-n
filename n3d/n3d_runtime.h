#ifndef N3D_RUNTIME_H
#define N3D_RUNTIME_H

#include "n3d_door.h"
#include "n3d_guard.h"
#include "n3d_object.h"
#include "n3d_projectile.h"
#include "n3d_render_bridge.h"

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace n3d
{

enum RemoteControlCommand
{
    RemoteOpenDoors = 0x1E,
    RemoteCloseDoors = 0x1F,
    RemoteEnableCannons = 0x20,
    RemoteDisableCannons = 0x21
};

struct RemoteControlResult
{
    unsigned doorsChanged;
    uint8_t doorGroupMask;
    bool cannonEnabled;

    RemoteControlResult()
        : doorsChanged(0), doorGroupMask(0), cannonEnabled(false) {}
};
struct EpisodeData;
struct WorldState;
struct RenderMap;

bool loadRuntime(const std::string &root, int episode, int level, std::string &error);
void unloadRuntime();
bool runtimeActive();

const EpisodeData *runtimeEpisode();
const WorldState *runtimeWorld();
const RenderMap *runtimeRenderMap();
DoorRuntime *runtimeDoors();
const DoorRuntime *runtimeDoorsConst();
ObjectRuntime *runtimeObjects();
const ObjectRuntime *runtimeObjectsConst();
GuardRuntime *runtimeGuards();
const GuardRuntime *runtimeGuardsConst();
ProjectileRuntime *runtimeProjectiles();
const ProjectileRuntime *runtimeProjectilesConst();
bool runtimeObjectTexture(uint8_t objectId, ObjectTextureView &out);
bool runtimeWorldObjectTexture(size_t objectIndex,
                               ObjectTextureView &out);
bool runtimeAdvanceWorldObjectAnimation(size_t objectIndex,
                                        uint32_t nowMs,
                                        int32_t playerWorldX,
                                        int32_t playerWorldY);

bool runtimeDoorPassageQuery(int tileX, int tileY, uint8_t wallId, void *userData);
void runtimeObjectTouchQuery(int tileX, int tileY,
                             uint8_t objectId, uint8_t objectClass,
                             void *userData);
bool runtimeObjectOccupiedQuery(int tileX, int tileY, void *userData);
DoorUseResult runtimeUseDoor(int tileX, int tileY, int playerSector);
void runtimeTickDoorMotion();
void runtimeTickDoorAutoClose(int playerTileX, int playerTileY);
void runtimeTickGuards(int32_t playerWorldX, int32_t playerWorldY,
                       int difficultyCode);
void runtimeSetRemoteCannonEnabled(bool enabled);
bool runtimeRemoteCannonEnabled();
RemoteControlResult runtimeApplyRemoteControl(RemoteControlCommand command,
                                              unsigned group);
unsigned runtimeActivateActionSpotDancers();

uint32_t runtimeBeginRenderGeneration();
void runtimeMarkProjectedObject(size_t objectIndex,
                                int projectedBaselineY,
                                int spriteLeft,
                                int spriteRight,
                                int centerX);
PlayerHitReport runtimeFireHitscan(int32_t playerWorldX,
                                   int32_t playerWorldY,
                                   uint8_t weaponId,
                                   int difficultyCode,
                                   int viewportCenterY);
ProjectileFireResult runtimeFireProjectile(int32_t playerWorldX,
                                           int32_t playerWorldY,
                                           uint8_t weaponId,
                                           int directionX,
                                           int directionY);
ProjectileUpdateReport runtimeTickProjectiles(uint32_t nowMs,
                                              unsigned substeps,
                                              int difficultyCode,
                                              int viewportCenterY);
bool runtimeProjectileTexture(size_t slotIndex,
                              ObjectTextureView &out);

const uint8_t *runtimeWallColumn(uint8_t wallId,
                                 int tileX,
                                 int tileY,
                                 int32_t alongWallFixed,
                                 bool reverse,
                                 unsigned *uOut,
                                 unsigned *widthOut);

// Copies the current N3D wall occupancy into Wolf4SDL's x-major 64x64
// tilemap layout. Renderable walls keep their original 0..255 wall ID.
bool copyWolfTileMap(uint8_t *dest, size_t destBytes);

} // namespace n3d

#endif
