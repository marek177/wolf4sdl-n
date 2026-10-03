#include "n3d/n3d_data.h"
#include "n3d/n3d_door.h"
#include "n3d/n3d_guard.h"
#include "n3d/n3d_object.h"
#include "n3d/n3d_world.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{

bool writeSyntheticMap(const char *path)
{
    const size_t size = n3d::MapArchive::HeaderSize + n3d::MapArchive::LevelBytes;
    std::vector<unsigned char> bytes(size, 0);

    bytes[0] = 1;
    bytes[1] = 0;

    // wall ID 1 -> ordinary blocking wall class 1
    bytes[0x002 + 1] = 0x01;

    // object IDs 1..4 -> START class 2 (N/E/S/W)
    for(int id = 1; id <= 4; ++id)
        bytes[0x102 + id] = 0x02;

    // object IDs 0x90..0x93 -> GUARD4/Skeleton class 0x0B.
    for(int id = 0x90; id <= 0x93; ++id)
        bytes[0x102 + id] = 0x0B;

    const size_t base = n3d::MapArchive::HeaderSize;

    // Player START at 8,5. Raw ID 2 faces east but only the cell matters here.
    size_t index = static_cast<size_t>(5 * 64 + 8);
    bytes[base + index * 2 + 1] = 2;

    // Guard at 5,5, ID 0x91 = class-relative subtype 1 -> facing 2/east.
    index = static_cast<size_t>(5 * 64 + 5);
    bytes[base + index * 2 + 1] = 0x91;

    std::ofstream f(path, std::ios::binary);
    if(!f)
        return false;
    f.write(reinterpret_cast<const char *>(&bytes[0]),
            static_cast<std::streamsize>(bytes.size()));
    return !!f;
}

bool buildFixture(n3d::EpisodeData &episode,
                  n3d::WorldState &world,
                  n3d::ObjectRuntime &objects,
                  n3d::DoorRuntime &doors,
                  n3d::GuardRuntime &guards)
{
    const char *path = "n3d_guard_ai_test.map";
    if(!writeSyntheticMap(path))
        return false;

    std::string error;
    if(!episode.map.load(path, error))
    {
        std::cerr << error << "\n";
        return false;
    }

    episode.episode = 1;
    if(!n3d::buildWorld(episode, 0, world, error))
    {
        std::cerr << error << "\n";
        return false;
    }

    if(!objects.build(world, episode.map, error))
    {
        std::cerr << error << "\n";
        return false;
    }

    if(!doors.build(world, episode.map, error))
    {
        std::cerr << error << "\n";
        return false;
    }

    objects.bindDoors(&doors);

    if(!guards.build(world, episode.map, objects, &doors, error))
    {
        std::cerr << error << "\n";
        return false;
    }

    return true;
}

bool require(bool condition, const char *message)
{
    if(condition)
        return true;

    std::cerr << "FAIL: " << message << "\n";
    return false;
}

}

int main()
{
    const int32_t playerX = 8 * 64 + 32;
    const int32_t playerY = 5 * 64 + 32;

    // Clear LOS: initial state 8 moves once, sees the player and reacquires.
    {
        n3d::EpisodeData episode;
        n3d::WorldState world;
        n3d::ObjectRuntime objects;
        n3d::DoorRuntime doors;
        n3d::GuardRuntime guards;
        if(!buildFixture(episode, world, objects, doors, guards))
            return 1;

        if(!require(guards.guards().size() == 1, "one GUARD spawned")) return 1;
        n3d::GuardRuntimeRecord &g = guards.guards()[0];
        if(!require(g.hp == 255, "GUARD starts at 255 HP")) return 1;
        if(!require(g.facing == 2, "spawn subtype 1 maps to east/facing 2")) return 1;
        if(!require(g.state == 8, "moving spawn promoted to state 8")) return 1;

        guards.tickPreviewAI(playerX, playerY, 1);

        if(!require(g.losResult == 1, "clear east LOS detected")) return 1;
        if(!require(g.state == 2, "state 8 reacquires into state 2")) return 1;
    }

    // Hard wall in the intermediate LOS cell blocks perception.
    {
        n3d::EpisodeData episode;
        n3d::WorldState world;
        n3d::ObjectRuntime objects;
        n3d::DoorRuntime doors;
        n3d::GuardRuntime guards;
        if(!buildFixture(episode, world, objects, doors, guards))
            return 1;

        world.at(6, 5).wallId = 1;
        world.at(6, 5).wallClass = 1;

        n3d::GuardRuntimeRecord &g = guards.guards()[0];
        guards.tickPreviewAI(playerX, playerY, 1);

        if(!require(g.losResult == 0, "hard wall blocks LOS")) return 1;
        if(!require(g.state == 8, "blocked state-8 perception does not reacquire")) return 1;
    }

    // State 7 is a reacquire state, not an ordinary movement state.
    {
        n3d::EpisodeData episode;
        n3d::WorldState world;
        n3d::ObjectRuntime objects;
        n3d::DoorRuntime doors;
        n3d::GuardRuntime guards;
        if(!buildFixture(episode, world, objects, doors, guards))
            return 1;

        n3d::GuardRuntimeRecord &g = guards.guards()[0];
        n3d::RuntimeObject &o = objects.objects()[g.objectIndex];
        g.state = 7;

        const int32_t beforeX = o.worldX;
        const int32_t beforeY = o.worldY;
        guards.tickPreviewAI(playerX, playerY, 1);

        if(!require(o.worldX == beforeX && o.worldY == beforeY,
                    "state 7 does not perform ordinary movement")) return 1;
        if(!require(g.state == 2, "state 7 clear perception reacquires")) return 1;
    }

    // Facing filter rejects a player behind the GUARD.
    {
        n3d::EpisodeData episode;
        n3d::WorldState world;
        n3d::ObjectRuntime objects;
        n3d::DoorRuntime doors;
        n3d::GuardRuntime guards;
        if(!buildFixture(episode, world, objects, doors, guards))
            return 1;

        n3d::GuardRuntimeRecord &g = guards.guards()[0];
        g.state = 7;
        g.facing = 6; // west, while player is east
        guards.tickPreviewAI(playerX, playerY, 1);

        if(!require(g.losResult == 0, "three-sector facing filter rejects rear target")) return 1;
        if(!require(g.state == 7, "rear target does not reacquire")) return 1;
    }

    // State 3 perception failure jumps directly into strategy-0 planning.
    {
        n3d::EpisodeData episode;
        n3d::WorldState world;
        n3d::ObjectRuntime objects;
        n3d::DoorRuntime doors;
        n3d::GuardRuntime guards;
        if(!buildFixture(episode, world, objects, doors, guards))
            return 1;

        world.at(6, 5).wallId = 1;
        world.at(6, 5).wallClass = 1;

        n3d::GuardRuntimeRecord &g = guards.guards()[0];
        g.state = 3;
        g.moveX = 0;
        g.moveY = 0;

        guards.tickPreviewAI(playerX, playerY, 1);

        if(!require(g.losResult == 0, "state 3 stores failed LOS")) return 1;
        if(!require(g.state == 6, "state 3 failure enters strategy-0 state 6")) return 1;
        if(!require(g.timer == 0x18, "unseen strategy-0 timer is 0x18")) return 1;
    }

    std::remove("n3d_guard_ai_test.map");
    std::cout << "N3D GUARD AI core tests passed\n";
    return 0;
}
