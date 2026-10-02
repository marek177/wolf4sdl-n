#ifndef N3D_RUNTIME_H
#define N3D_RUNTIME_H

#include "n3d_door.h"

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace n3d
{
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

bool runtimeDoorPassageQuery(int tileX, int tileY, uint8_t wallId, void *userData);
DoorUseResult runtimeUseDoor(int tileX, int tileY, int playerSector);
void runtimeTickDoorMotion();
void runtimeTickDoorAutoClose(int playerTileX, int playerTileY);

const uint8_t *runtimeWallColumn(uint8_t wallId,
                                 int32_t alongWallFixed,
                                 bool reverse,
                                 unsigned *uOut,
                                 unsigned *widthOut);

// Copies the current N3D wall occupancy into Wolf4SDL's x-major 64x64
// tilemap layout. Renderable walls keep their original 0..255 wall ID.
bool copyWolfTileMap(uint8_t *dest, size_t destBytes);

} // namespace n3d

#endif
