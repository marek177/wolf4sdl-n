#include "n3d_runtime.h"

#include "n3d_data.h"
#include "n3d_render_bridge.h"
#include "n3d_world.h"

namespace n3d
{
namespace
{
EpisodeData g_episode;
WorldState g_world;
RenderMap g_renderMap;
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
    g_active = true;
    return true;
}

void unloadRuntime()
{
    g_active = false;
    g_episode = EpisodeData();
    g_world = WorldState();
    g_renderMap = RenderMap();
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
            dest[x * RenderMap::Height + y] = cell.opaque ? cell.wallId : 0;
        }
    }
    return true;
}

} // namespace n3d
