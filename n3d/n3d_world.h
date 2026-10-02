#ifndef N3D_WORLD_H
#define N3D_WORLD_H

#include "n3d_data.h"

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace n3d
{

enum PlayerDirection
{
    DirNorth = 0,
    DirEast = 1,
    DirSouth = 2,
    DirWest = 3
};

struct PlayerStart
{
    bool found;
    size_t x;
    size_t y;
    PlayerDirection direction;
    uint8_t rawObjectId;

    PlayerStart();
};

struct WorldCell
{
    uint8_t wallId;
    uint8_t wallClass;
    uint8_t objectId;
    uint8_t objectClass;

    WorldCell();
};

struct WorldState
{
    enum { Width = LevelMap::Width, Height = LevelMap::Height, CellCount = Width * Height };
    std::vector<WorldCell> cells;
    PlayerStart playerStart;

    WorldState();
    const WorldCell &at(size_t x, size_t y) const;
    WorldCell &at(size_t x, size_t y);
};

bool buildWorld(const EpisodeData &episode, size_t levelIndex, WorldState &out, std::string &error);
const char *directionName(PlayerDirection direction);

} // namespace n3d

#endif
