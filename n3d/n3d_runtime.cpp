#include "n3d_runtime.h"

#include "n3d_data.h"
#include "n3d_door.h"
#include "n3d_object.h"
#include "n3d_render_bridge.h"
#include "n3d_world.h"

namespace n3d
{
namespace
{
EpisodeData g_episode;
WorldState g_world;
RenderMap g_renderMap;
DoorRuntime g_doors;
ObjectRuntime g_objects;
bool g_active = false;
uint8_t g_fallbackColumn[64] = {0};
}

bool loadRuntime(const std::string &root, int episode, int level, std::string &error)
{
    unloadRuntime();

    EpisodeData episodeData;
    WorldState world;
    RenderMap renderMap;

    if(!loadEpisode(root, episode, episodeData, error))
        return false;
    if(level < 1 || !buildWorld(episodeData, static_cast<size_t>(level - 1), world, error))
        return false;
    if(!buildRenderMap(episodeData, world, renderMap, error))
        return false;

    g_episode = episodeData;
    g_world = world;
    g_renderMap = renderMap;

    if(!g_doors.build(g_world, g_episode.map, error))
    {
        unloadRuntime();
        return false;
    }

    if(!g_objects.build(g_world, g_episode.map, error))
    {
        unloadRuntime();
        return false;
    }
    g_objects.bindDoors(&g_doors);

    g_active = true;
    return true;
}

void unloadRuntime()
{
    g_active = false;
    g_episode = EpisodeData();
    g_world = WorldState();
    g_renderMap = RenderMap();
    g_doors = DoorRuntime();
    g_objects = ObjectRuntime();
}

bool runtimeActive()
{
    return g_active;
}

const EpisodeData *runtimeEpisode()
{
    return g_active ? &g_episode : 0;
}

const WorldState *runtimeWorld()
{
    return g_active ? &g_world : 0;
}

const RenderMap *runtimeRenderMap()
{
    return g_active ? &g_renderMap : 0;
}

DoorRuntime *runtimeDoors()
{
    return g_active ? &g_doors : 0;
}

const DoorRuntime *runtimeDoorsConst()
{
    return g_active ? &g_doors : 0;
}

ObjectRuntime *runtimeObjects()
{
    return g_active ? &g_objects : 0;
}

const ObjectRuntime *runtimeObjectsConst()
{
    return g_active ? &g_objects : 0;
}

bool runtimeDoorPassageQuery(int tileX, int tileY, uint8_t wallId, void *userData)
{
    (void)wallId;
    (void)userData;
    return g_active && g_doors.allowsPassage(tileX, tileY);
}

void runtimeObjectTouchQuery(int tileX, int tileY,
                             uint8_t objectId, uint8_t objectClass,
                             void *userData)
{
    (void)objectId;
    (void)objectClass;
    (void)userData;
    if(g_active)
        g_objects.touch(tileX, tileY);
}

bool runtimeObjectOccupiedQuery(int tileX, int tileY, void *userData)
{
    (void)userData;
    return g_active && g_objects.occupiedAt(tileX, tileY);
}

DoorUseResult runtimeUseDoor(int tileX, int tileY, int playerSector)
{
    if(!g_active)
        return DoorUseNone;
    return g_doors.use(tileX, tileY, playerSector);
}

void runtimeTickDoorMotion()
{
    if(g_active)
        g_doors.tickMotion();
}

void runtimeTickDoorAutoClose(int playerTileX, int playerTileY)
{
    if(g_active)
        g_doors.tickAutoClose(playerTileX, playerTileY,
                              &runtimeObjectOccupiedQuery, 0);
}

const uint8_t *runtimeWallColumn(uint8_t wallId,
                                 int32_t alongWallFixed,
                                 bool reverse,
                                 unsigned *uOut,
                                 unsigned *widthOut)
{
    if(!g_active)
        return g_fallbackColumn;

    WallTextureBridge bridge(g_episode.img);
    const uint8_t *column =
        bridge.columnFromWolfFixed(wallId, alongWallFixed, reverse, uOut, widthOut);
    return column ? column : g_fallbackColumn;
}

bool copyWolfTileMap(uint8_t *dest, size_t destBytes)
{
    if(!g_active || !dest || destBytes < RenderMap::CellCount)
        return false;

    for(size_t x = 0; x < RenderMap::Width; ++x)
    {
        for(size_t y = 0; y < RenderMap::Height; ++y)
        {
            const RenderCell &cell = g_renderMap.at(x, y);
            uint8_t value = cell.opaque ? cell.wallId : 0;

            if(g_doors.isDoorCell(static_cast<int>(x), static_cast<int>(y)) &&
               g_doors.allowsPassage(static_cast<int>(x), static_cast<int>(y)))
                value = 0;

            dest[x * RenderMap::Height + y] = value;
        }
    }
    return true;
}

} // namespace n3d
