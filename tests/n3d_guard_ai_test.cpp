#include "n3d/n3d_data.h"
#include "n3d/n3d_collision.h"
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
    // wall ID 2 -> normal vertical door class 0x31
    bytes[0x002 + 2] = 0x31;
    // TURN class base 3 with eight directional variants.
    for(int id = 3; id <= 10; ++id)
        bytes[0x002 + id] = 0x41;
    // RETREAT class base 11 with eight directions plus variant 8.
    for(int id = 11; id <= 19; ++id)
        bytes[0x002 + id] = 0x42;

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
                  n3d::GuardRuntime &guards,
                  int episodeNumber = 1)
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

    episode.episode = episodeNumber;
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

    if(!guards.build(world, episode.map, objects, &doors, episode.episode, error))
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

    // Second object plane blocks ordinary occupancy but permits class 0x2A.
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

        world.at(6, 5).objectId = 0x40;
        world.at(6, 5).objectClass = 0x2B;
        guards.tickPreviewAI(playerX, playerY, 1);

        if(!require(g.losResult == 0, "blocking object plane stops LOS")) return 1;

        g.state = 7;
        world.at(6, 5).objectClass = 0x2A;
        guards.tickPreviewAI(playerX, playerY, 1);

        if(!require(g.losResult == 1, "PERMEABLE object class passes LOS")) return 1;
        if(!require(g.state == 2, "permeable LOS can reacquire")) return 1;
    }

    // Dynamic door validator: closed blocks LOS, open passes.
    {
        n3d::EpisodeData episode;
        n3d::WorldState world;
        n3d::ObjectRuntime objects;
        n3d::DoorRuntime doors;
        n3d::GuardRuntime guards;
        if(!buildFixture(episode, world, objects, doors, guards))
            return 1;

        world.at(6, 5).wallId = 2;
        world.at(6, 5).wallClass = 0x31;

        std::string error;
        if(!doors.build(world, episode.map, error))
        {
            std::cerr << error << "\n";
            return 1;
        }
        objects.bindDoors(&doors);
        if(!guards.build(world, episode.map, objects, &doors, episode.episode, error))
        {
            std::cerr << error << "\n";
            return 1;
        }

        n3d::GuardRuntimeRecord &g = guards.guards()[0];
        g.state = 7;
        guards.tickPreviewAI(playerX, playerY, 1);
        if(!require(g.losResult == 0, "closed dynamic door blocks LOS")) return 1;

        n3d::DoorController *door = doors.find(6, 5);
        if(!require(door != 0, "door controller exists")) return 1;
        door->state = n3d::DoorOpen;

        g.state = 7;
        guards.tickPreviewAI(playerX, playerY, 1);
        if(!require(g.losResult == 1, "open dynamic door passes LOS")) return 1;
        if(!require(g.state == 2, "open-door LOS can reacquire")) return 1;
    }

    // State-8 TURN marker selects its class-relative eight-way facing.
    {
        n3d::EpisodeData episode;
        n3d::WorldState world;
        n3d::ObjectRuntime objects;
        n3d::DoorRuntime doors;
        n3d::GuardRuntime guards;
        if(!buildFixture(episode, world, objects, doors, guards))
            return 1;

        world.at(5, 5).wallId = 7;      // TURN base 3 + variant 4
        world.at(5, 5).wallClass = 0x41;

        n3d::GuardRuntimeRecord &g = guards.guards()[0];
        n3d::RuntimeObject &o = objects.objects()[g.objectIndex];
        const int32_t beforeY = o.worldY;

        guards.tickPreviewAI(playerX, playerY, 1);

        if(!require(g.facing == 4, "TURN variant 4 selects facing 4")) return 1;
        if(!require(o.worldY > beforeY, "TURN-facing movement commits southward")) return 1;
    }

    // Moving RETREAT marker stops movement, enters state 3 and reverses facing.
    {
        n3d::EpisodeData episode;
        n3d::WorldState world;
        n3d::ObjectRuntime objects;
        n3d::DoorRuntime doors;
        n3d::GuardRuntime guards;
        if(!buildFixture(episode, world, objects, doors, guards))
            return 1;

        world.at(5, 5).wallId = 11;
        world.at(5, 5).wallClass = 0x42;

        n3d::GuardRuntimeRecord &g = guards.guards()[0];
        n3d::RuntimeObject &o = objects.objects()[g.objectIndex];
        const int32_t beforeX = o.worldX;
        const int32_t beforeY = o.worldY;

        guards.tickPreviewAI(playerX, playerY, 1);

        if(!require(g.moveX == 0 && g.moveY == 0,
                    "RETREAT moving marker clears movement")) return 1;
        if(!require(g.facing == 6, "RETREAT reverses east facing to west")) return 1;
        if(!require(g.state == 3, "RETREAT enters state 3 when rear perception fails")) return 1;
        if(!require(o.worldX == beforeX && o.worldY == beforeY,
                    "RETREAT stop does not move object")) return 1;
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

    // State 4 applies class-specific contact damage and enters state 5.
    {
        n3d::EpisodeData episode;
        n3d::WorldState world;
        n3d::ObjectRuntime objects;
        n3d::DoorRuntime doors;
        n3d::GuardRuntime guards;
        if(!buildFixture(episode, world, objects, doors, guards))
            return 1;

        n3d::GuardRuntimeRecord &g = guards.guards()[0];
        g.state = 4;
        objects.inventory().health = 100;

        guards.tickPreviewAI(playerX, playerY, 1);

        // Skeleton class 0x0B at three cells: distance=3, base=33, /4=8.
        if(!require(objects.inventory().health == 92,
                    "state 4 applies Skeleton medium damage 8")) return 1;
        if(!require(objects.inventory().damageFlash == 3,
                    "nonzero enemy hit sets damage feedback to 3")) return 1;
        if(!require(g.state == 5,
                    "surviving state-4 attack enters state 5")) return 1;
    }

    // Difficulty direction is easy /2, medium x1, hard x2.
    {
        const int expected[3] = {96, 92, 84};
        for(int difficulty = 0; difficulty < 3; ++difficulty)
        {
            n3d::EpisodeData episode;
            n3d::WorldState world;
            n3d::ObjectRuntime objects;
            n3d::DoorRuntime doors;
            n3d::GuardRuntime guards;
            if(!buildFixture(episode, world, objects, doors, guards))
                return 1;

            n3d::GuardRuntimeRecord &g = guards.guards()[0];
            g.state = 4;
            objects.inventory().health = 100;

            guards.tickPreviewAI(playerX, playerY, difficulty);

            if(!require(objects.inventory().health == expected[difficulty],
                        "enemy damage difficulty scaling")) return 1;
        }
    }

    // Lethal damage sets HP/state/death attacker and freezes the killing GUARD.
    {
        n3d::EpisodeData episode;
        n3d::WorldState world;
        n3d::ObjectRuntime objects;
        n3d::DoorRuntime doors;
        n3d::GuardRuntime guards;
        if(!buildFixture(episode, world, objects, doors, guards))
            return 1;

        n3d::GuardRuntimeRecord &g = guards.guards()[0];
        g.state = 4;
        objects.inventory().health = 8;

        guards.tickPreviewAI(playerX, playerY, 1);

        if(!require(objects.inventory().health == 0,
                    "lethal enemy damage saturates player HP at zero")) return 1;
        if(!require(objects.inventory().gameState == 2,
                    "lethal enemy damage enters game state 2")) return 1;
        if(!require(objects.inventory().deathTransitionPending,
                    "lethal enemy damage raises death transition flag")) return 1;
        if(!require(objects.inventory().deathAttackerObjectIndex == g.objectIndex,
                    "death stores attacking OBJECT index")) return 1;
        if(!require(g.state == 0x0B,
                    "killing GUARD enters terminal state 0x0B")) return 1;
    }

    // Original distance helper promotes sqrt(2) to distance 2.
    {
        n3d::EpisodeData episode;
        n3d::WorldState world;
        n3d::ObjectRuntime objects;
        n3d::DoorRuntime doors;
        n3d::GuardRuntime guards;
        if(!buildFixture(episode, world, objects, doors, guards))
            return 1;

        n3d::GuardRuntimeRecord &g = guards.guards()[0];
        g.state = 4;
        g.facing = 3;
        objects.inventory().health = 100;

        const int32_t diagonalPlayerX = 6 * 64 + 32;
        const int32_t diagonalPlayerY = 6 * 64 + 32;
        guards.tickPreviewAI(diagonalPlayerX, diagonalPlayerY, 1);

        // sqrt(1^2+1^2) uses original metric -> 2; base=50; Skeleton /4 = 12.
        if(!require(objects.inventory().health == 88,
                    "original distance metric promotes diagonal sqrt(2) to 2")) return 1;
    }

    // Existing game state 2 suppresses damage and preserves state-4 tail.
    {
        n3d::EpisodeData episode;
        n3d::WorldState world;
        n3d::ObjectRuntime objects;
        n3d::DoorRuntime doors;
        n3d::GuardRuntime guards;
        if(!buildFixture(episode, world, objects, doors, guards))
            return 1;

        n3d::GuardRuntimeRecord &g = guards.guards()[0];
        g.state = 4;
        objects.inventory().health = 50;
        objects.inventory().gameState = 2;

        guards.tickPreviewAI(playerX, playerY, 1);

        if(!require(objects.inventory().health == 50,
                    "game state 2 suppresses repeated enemy damage")) return 1;
        if(!require(g.state == 4,
                    "game state 2 returns before state-5 scheduling")) return 1;
    }

    // Omnipotent suppresses HP/state mutation after damage is computed.
    {
        n3d::EpisodeData episode;
        n3d::WorldState world;
        n3d::ObjectRuntime objects;
        n3d::DoorRuntime doors;
        n3d::GuardRuntime guards;
        if(!buildFixture(episode, world, objects, doors, guards))
            return 1;

        n3d::GuardRuntimeRecord &g = guards.guards()[0];
        g.state = 4;
        objects.inventory().health = 100;
        objects.inventory().omnipotent = true;

        guards.tickPreviewAI(playerX, playerY, 1);

        if(!require(objects.inventory().health == 100,
                    "Omnipotent suppresses enemy HP damage")) return 1;
        if(!require(objects.inventory().gameState == 0,
                    "Omnipotent suppresses death state")) return 1;
        if(!require(objects.inventory().damageFlash == 3,
                    "Omnipotent still observes nonzero damage feedback ordering")) return 1;
        if(!require(g.state == 5,
                    "suppressed state-4 attack continues to fallback state 5")) return 1;
    }

    // Current-generation Silver Pistol hitscan uses aim stamp + 16-cell LOS.
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
        g.hp = 100;

        guards.beginRenderGeneration();
        guards.markProjectedObject(g.objectIndex, 100, 140, 164, 152);

        const n3d::PlayerHitReport hit =
            guards.fireHitscan(playerX, playerY, 2, 1, 80);

        if(!require(hit.hitCount == 1, "current render stamp admits hitscan target")) return 1;
        if(!require(hit.killCount == 1, "high projected baseline can kill target")) return 1;
        if(!require(hit.scoreDelta == 100, "Skeleton kill score is 100")) return 1;
        if(!require(g.hp == 0, "lethal hitscan clears GUARD HP")) return 1;
        if(!require(g.state == 0 && g.nextState == 9,
                    "ground lethal hit enters state 0 then 9")) return 1;

        guards.tickPreviewAI(playerX, playerY, 1);
        if(!require(g.state == 9, "death sequence timer advances to state 9")) return 1;
        guards.tickPreviewAI(playerX, playerY, 1);
        if(!require(g.state == 0x0A, "state 9 finalizes to terminal 0x0A")) return 1;

        (void)o;
    }

    // A render stamp becomes stale as soon as a new projection generation begins.
    {
        n3d::EpisodeData episode;
        n3d::WorldState world;
        n3d::ObjectRuntime objects;
        n3d::DoorRuntime doors;
        n3d::GuardRuntime guards;
        if(!buildFixture(episode, world, objects, doors, guards))
            return 1;

        n3d::GuardRuntimeRecord &g = guards.guards()[0];
        guards.beginRenderGeneration();
        guards.markProjectedObject(g.objectIndex, 100, 140, 164, 152);
        guards.beginRenderGeneration();

        const n3d::PlayerHitReport hit =
            guards.fireHitscan(playerX, playerY, 2, 1, 80);
        if(!require(hit.hitCount == 0, "stale render generation rejects hitscan")) return 1;
    }

    // Horizontal center overlap uses the recovered +/-4-pixel expansion.
    {
        n3d::EpisodeData episode;
        n3d::WorldState world;
        n3d::ObjectRuntime objects;
        n3d::DoorRuntime doors;
        n3d::GuardRuntime guards;
        if(!buildFixture(episode, world, objects, doors, guards))
            return 1;

        n3d::GuardRuntimeRecord &g = guards.guards()[0];
        guards.beginRenderGeneration();
        guards.markProjectedObject(g.objectIndex, 100, 100, 120, 152);

        const n3d::PlayerHitReport miss =
            guards.fireHitscan(playerX, playerY, 2, 1, 80);
        if(!require(miss.hitCount == 0, "off-center sprite gets no hitscan stamp")) return 1;

        guards.beginRenderGeneration();
        guards.markProjectedObject(g.objectIndex, 100, 149, 149, 152);
        const n3d::PlayerHitReport slack =
            guards.fireHitscan(playerX, playerY, 2, 1, 80);
        if(!require(slack.hitCount == 1, "+/-4 pixel aim slack admits near-center sprite")) return 1;
    }

    // Nonlethal hit invalidates direction cache and enters recovered pain route.
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
        g.hp = 255;
        o.lastProjectedY = 81;

        bool killed = false;
        const uint8_t damage =
            guards.applyPlayerWeaponHit(0, 2, 1, 80, &killed);

        if(!require(damage != 0 && !killed, "nonlethal weapon hit returns damage")) return 1;
        if(!require(g.hp < 255, "nonlethal hit subtracts GUARD HP")) return 1;
        if(!require(g.directionCache == 8, "hit invalidates direction cache with 8")) return 1;
        if(!require(g.state == 0 && g.nextState == 5,
                    "state-7 hit uses sequence state 0 -> next 5")) return 1;
    }

    // Dracula humanoid fatal path transforms into Dracula-Bat after state 9.
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
        o.objectClass = 0x11;
        o.properties = n3d::objectPropertiesForClass(0x11);
        o.lastProjectedY = 120;
        g.hp = 1;

        bool killed = false;
        guards.applyPlayerWeaponHit(0, 2, 1, 80, &killed);
        if(!require(killed, "Dracula humanoid phase can reach fatal path")) return 1;

        guards.tickPreviewAI(playerX, playerY, 1);
        guards.tickPreviewAI(playerX, playerY, 1);

        if(!require(o.objectClass == 0x14, "Dracula transforms to class 0x14")) return 1;
        if(!require(g.hp == 255, "Dracula-Bat resets HP to 255")) return 1;
        if(!require(g.state == 8 && g.nextState == 2,
                    "Dracula-Bat resumes state 8 -> next 2")) return 1;
        if(!require(g.timer == 1, "Dracula-Bat transform timer is 1")) return 1;
        if(!require(o.verticalOffset == 0x23,
                    "Dracula-Bat vertical anchor becomes 0x23")) return 1;
        if(!require(world.at(static_cast<size_t>(o.tileX),
                             static_cast<size_t>(o.tileY)).objectClass == 0x14,
                    "Dracula-Bat is re-linked into MAP occupancy")) return 1;
    }

    // Hamerstein fatal state-9 finalization raises a separate ending request.
    {
        n3d::EpisodeData episode;
        n3d::WorldState world;
        n3d::ObjectRuntime objects;
        n3d::DoorRuntime doors;
        n3d::GuardRuntime guards;
        if(!buildFixture(episode, world, objects, doors, guards, 3))
            return 1;

        n3d::GuardRuntimeRecord &g = guards.guards()[0];
        n3d::RuntimeObject &o = objects.objects()[g.objectIndex];
        o.objectClass = 0x16;
        o.properties = n3d::objectPropertiesForClass(0x16);
        o.lastProjectedY = 120;
        g.hp = 1;

        bool killed = false;
        guards.applyPlayerWeaponHit(0, 2, 1, 80, &killed);
        guards.tickPreviewAI(playerX, playerY, 1);
        guards.tickPreviewAI(playerX, playerY, 1);

        if(!require(killed, "Hamerstein reaches fatal GUARD path")) return 1;
        if(!require(objects.inventory().endingRequested,
                    "Hamerstein raises ending request")) return 1;
        if(!require(objects.inventory().gameState == 0,
                    "Hamerstein resets ordinary game state before ending")) return 1;
    }

    std::remove("n3d_guard_ai_test.map");
    std::cout << "N3D GUARD AI core tests passed\n";
    return 0;
}
