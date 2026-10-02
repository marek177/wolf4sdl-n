#include "n3d_world.h"

#include <sstream>

namespace n3d
{

PlayerStart::PlayerStart()
    : found(false), x(0), y(0), direction(DirNorth), rawObjectId(0)
{
}

WorldCell::WorldCell()
    : wallId(0), wallClass(0), objectId(0), objectClass(0)
{
}

WorldState::WorldState() : cells(CellCount)
{
}

const WorldCell &WorldState::at(size_t x, size_t y) const
{
    return cells[y * Width + x];
}

const char *directionName(PlayerDirection direction)
{
    switch(direction)
    {
        case DirNorth: return "N";
        case DirEast: return "E";
        case DirSouth: return "S";
        case DirWest: return "W";
    }
    return "?";
}

bool buildWorld(const EpisodeData &episode, size_t levelIndex, WorldState &out, std::string &error)
{
    if(levelIndex >= episode.map.levels().size())
    {
        std::ostringstream ss;
        ss << "Nitemare3D level index " << levelIndex << " is outside episode "
           << episode.episode << " (" << episode.map.levels().size() << " levels)";
        error = ss.str();
        return false;
    }

    WorldState world;
    const LevelMap &map = episode.map.levels()[levelIndex];

    for(size_t y = 0; y < WorldState::Height; ++y)
    {
        for(size_t x = 0; x < WorldState::Width; ++x)
        {
            const size_t index = y * WorldState::Width + x;
            const MapCell &src = map.cells[index];
            WorldCell &dst = world.cells[index];

            dst.wallId = src.wall;
            dst.wallClass = episode.map.wallClass(src.wall);
            dst.objectId = src.object;
            dst.objectClass = episode.map.objectClass(src.object);

            if(dst.objectClass == 0x02)
            {
                if(world.playerStart.found)
                {
                    error = "Nitemare3D level contains more than one START object";
                    return false;
                }

                PlayerDirection direction;
                switch(dst.objectId)
                {
                    case 0x01: direction = DirNorth; break;
                    case 0x02: direction = DirEast; break;
                    case 0x03: direction = DirSouth; break;
                    case 0x04: direction = DirWest; break;
                    default:
                        error = "Nitemare3D START class uses an unknown orientation object ID";
                        return false;
                }

                world.playerStart.found = true;
                world.playerStart.x = x;
                world.playerStart.y = y;
                world.playerStart.direction = direction;
                world.playerStart.rawObjectId = dst.objectId;
            }
        }
    }

    if(!world.playerStart.found)
    {
        error = "Nitemare3D level contains no START object";
        return false;
    }

    out = world;
    return true;
}

} // namespace n3d
