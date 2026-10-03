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

void writeU16(std::vector<unsigned char> &bytes,
              size_t offset,
              unsigned value)
{
    bytes[offset] = static_cast<unsigned char>(value & 0xffu);
    bytes[offset + 1] =
        static_cast<unsigned char>((value >> 8) & 0xffu);
}

void writeU32(std::vector<unsigned char> &bytes,
              size_t offset,
              unsigned long value)
{
    bytes[offset] = static_cast<unsigned char>(value & 0xffUL);
    bytes[offset + 1] =
        static_cast<unsigned char>((value >> 8) & 0xffUL);
    bytes[offset + 2] =
        static_cast<unsigned char>((value >> 16) & 0xffUL);
    bytes[offset + 3] =
        static_cast<unsigned char>((value >> 24) & 0xffUL);
}

void appendTinyFrame(std::vector<unsigned char> &bytes,
                     unsigned char color)
{
    bytes.push_back(1);
    bytes.push_back(1);
    for(int i = 0; i < 8; ++i)
        bytes.push_back(0);
    bytes.push_back(color);
}

bool writeSyntheticImg(const char *path)
{
    std::vector<unsigned char> bytes(n3d::ImgArchive::FrameDataOffset, 0);

    const unsigned char id = 0x91;
    const unsigned long stream =
        static_cast<unsigned long>(bytes.size());
    writeU32(bytes, 0x400u + static_cast<unsigned>(id) * 4u, stream);

    const size_t seq =
        static_cast<size_t>(n3d::ImgArchive::HighSequenceBankOffset) +
        static_cast<size_t>(id) *
        static_cast<size_t>(n3d::ImgArchive::SequenceRecordBytes);

    writeU16(bytes, seq + 0, 100);
    bytes[seq + 2] = 48;
    bytes[seq + 3] = 1;

    // Direction tables +04/+14/+24: packed low=start frame, high=count.
    for(unsigned table = 0; table < 3; ++table)
    {
        for(unsigned dir = 0; dir < 8; ++dir)
        {
            const unsigned start = table * 12u + dir;
            const unsigned token = start | (2u << 8);
            writeU16(bytes, seq + 0x04u + table * 0x10u + dir * 2u,
                     token);
        }
    }

    // State 2/3/4 setup tokens.
    writeU16(bytes, seq + 0x34, 30u | (2u << 8));
    writeU16(bytes, seq + 0x36, 32u | (2u << 8));
    writeU16(bytes, seq + 0x38, 34u | (2u << 8));

    // Pain/death alternative banks. Keep every candidate enabled and equal so
    // deterministic RNG choice cannot affect expected frame range.
    for(unsigned i = 0; i < 7; ++i)
    {
        writeU16(bytes, seq + 0x3A + i * 2u, 36u | (2u << 8));
        writeU16(bytes, seq + 0x4A + i * 2u, 40u | (2u << 8));
    }

    for(unsigned frame = 0; frame < 48; ++frame)
        appendTinyFrame(bytes, static_cast<unsigned char>(0x20 + frame));

    // Dancers presentation family class 0x21 at object ID 0xA0.
    const unsigned char dancerId = 0xA0;
    const unsigned long dancerStream =
        static_cast<unsigned long>(bytes.size());
    writeU32(bytes,
             0x400u + static_cast<unsigned>(dancerId) * 4u,
             dancerStream);

    const size_t dancerSeq =
        static_cast<size_t>(n3d::ImgArchive::HighSequenceBankOffset) +
        static_cast<size_t>(dancerId) *
        static_cast<size_t>(n3d::ImgArchive::SequenceRecordBytes);

    writeU16(bytes, dancerSeq + 0, 100);
    bytes[dancerSeq + 2] = 32;
    bytes[dancerSeq + 3] = 0;

    for(unsigned dir = 0; dir < 8; ++dir)
    {
        writeU16(bytes, dancerSeq + 0x04u + dir * 2u,
                 (2u + dir) | (2u << 8));
        writeU16(bytes, dancerSeq + 0x14u + dir * 2u,
                 (10u + dir) | (2u << 8));
        writeU16(bytes, dancerSeq + 0x24u + dir * 2u,
                 (20u + dir) | (2u << 8));
    }

    for(unsigned frame = 0; frame < 32; ++frame)
        appendTinyFrame(bytes,
                        static_cast<unsigned char>(0x60 + frame));

    std::ofstream f(path, std::ios::binary);
    if(!f)
        return false;
    f.write(reinterpret_cast<const char *>(&bytes[0]),
            static_cast<std::streamsize>(bytes.size()));
    return !!f;
}

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
    bytes[0x002 + 20] = 0x46; // ACTIONSPOT
    bytes[0x002 + 0x30] = 0x3B; // DOORVR remote group 0
    bytes[0x002 + 0x32] = 0x3B; // DOORVR remote group 1
    bytes[0x002 + 0x31] = 0x3C; // DOORHR remote group 0
    bytes[0x002 + 0x33] = 0x3C; // DOORHR remote group 1

    // object IDs 1..4 -> START class 2 (N/E/S/W)
    for(int id = 1; id <= 4; ++id)
        bytes[0x102 + id] = 0x02;

    // object IDs 0x90..0x93 -> GUARD4/Skeleton class 0x0B.
    for(int id = 0x90; id <= 0x93; ++id)
        bytes[0x102 + id] = 0x0B;
    bytes[0x102 + 0xA0] = 0x21; // Dancers presentation family

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
    const char *imgPath = "n3d_guard_ai_test.img";
    if(!writeSyntheticMap(path) || !writeSyntheticImg(imgPath))
        return false;

    std::string error;
    if(!episode.map.load(path, error))
    {
        std::cerr << error << "\n";
        return false;
    }

    if(!episode.img.load(imgPath, error))
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

    if(!guards.build(world, episode.map, episode.img, objects, &doors, episode.episode, error))
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

        n3d::RuntimeObject &o = objects.objects()[g.objectIndex];
        guards.tickPreviewAI(playerX, playerY, 1);

        if(!require(g.sequenceToken == (15u | (2u << 8)),
                    "state-8 directional table selects relative direction token")) return 1;
        if(!require(o.animationFrame == 15,
                    "first state-8 refresh installs directional token after movement")) return 1;

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
        if(!guards.build(world, episode.map, episode.img, objects, &doors, episode.episode, error))
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

    // Bat / Dracula-Bat / Ghost vertical bob uses GUARD+0x15 and clamps 10..35.
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

        o.objectClass = 0x08;
        o.properties = n3d::objectPropertiesForClass(0x08);
        g.state = 6;
        g.timer = 10;
        g.verticalBobStep = 0;
        o.verticalOffset = 0;

        guards.tickPreviewAI(playerX, playerY, 1);
        if(!require(o.verticalOffset == 10,
                    "flying GUARD initializes/clamps bob to minimum 10")) return 1;
        if(!require(g.verticalBobStep == 1,
                    "flying GUARD initializes bob direction to +1")) return 1;

        g.state = 6;
        g.timer = 10;
        o.verticalOffset = 34;
        g.verticalBobStep = 1;
        guards.tickPreviewAI(playerX, playerY, 1);
        if(!require(o.verticalOffset == 35,
                    "flying GUARD reaches upper bob bound 35")) return 1;
        if(!require(g.verticalBobStep == -1,
                    "upper bob bound reverses direction")) return 1;

        g.state = 6;
        g.timer = 10;
        o.verticalOffset = 11;
        g.verticalBobStep = -1;
        guards.tickPreviewAI(playerX, playerY, 1);
        if(!require(o.verticalOffset == 10,
                    "flying GUARD reaches lower bob bound 10")) return 1;
        if(!require(g.verticalBobStep == 1,
                    "lower bob bound reverses direction")) return 1;
    }

    // Remote door groups are class-relative #1/#2 selectors.
    // Open accepts states 1/3 -> 2, close accepts 0/2 -> 3, and the
    // selected group bit toggles after either command.
    {
        n3d::EpisodeData episode;
        n3d::WorldState world;
        n3d::ObjectRuntime objects;
        n3d::DoorRuntime doors;
        n3d::GuardRuntime guards;
        if(!buildFixture(episode, world, objects, doors, guards))
            return 1;

        world.at(2, 2).wallId = 0x30;
        world.at(2, 2).wallClass = 0x3B;

        world.at(10, 2).wallId = 0x32;
        world.at(10, 2).wallClass = 0x3B;

        world.at(20, 10).wallId = 0x33;
        world.at(20, 10).wallClass = 0x3C;

        std::string error;
        if(!doors.build(world, episode.map, error))
        {
            std::cerr << error << "\n";
            return 1;
        }

        const n3d::DoorController *group0 = doors.find(2, 2);
        const n3d::DoorController *group1v = doors.find(10, 2);
        const n3d::DoorController *group1h = doors.find(20, 10);

        if(!require(group0 && group0->remoteGroup == 0,
                    "remote DOORVR #1 derives group 0")) return 1;
        if(!require(group1v && group1v->remoteGroup == 1,
                    "remote DOORVR #2 derives group 1")) return 1;
        if(!require(group1h && group1h->remoteGroup == 1,
                    "remote DOORHR #2 derives group 1")) return 1;

        const unsigned opened = doors.applyRemoteGroup(1, true);
        if(!require(opened == 2,
                    "remote group-1 open changes both matching records")) return 1;
        if(!require(doors.find(10, 2)->state == n3d::DoorOpening &&
                    doors.find(20, 10)->state == n3d::DoorOpening,
                    "remote open maps states 1/3 to opening state 2")) return 1;
        if(!require(doors.find(2, 2)->state == n3d::DoorClosed,
                    "remote group command leaves other groups unchanged")) return 1;
        if(!require(doors.remoteGroupMask() == 0x02,
                    "remote open toggles selected group bit on")) return 1;

        const unsigned closed = doors.applyRemoteGroup(1, false);
        if(!require(closed == 2,
                    "remote group-1 close changes both matching records")) return 1;
        if(!require(doors.find(10, 2)->state == n3d::DoorClosing &&
                    doors.find(20, 10)->state == n3d::DoorClosing,
                    "remote close maps states 0/2 to closing state 3")) return 1;
        if(!require(doors.remoteGroupMask() == 0,
                    "remote close toggles selected group bit off")) return 1;

        guards.setCannonAttackEnabled(true);
        guards.toggleCannonAttackEnabled();
        if(!require(!guards.cannonAttackEnabled(),
                    "remote Cannon command XOR disables enabled cycle")) return 1;
        guards.toggleCannonAttackEnabled();
        if(!require(guards.cannonAttackEnabled(),
                    "second raw Cannon command XOR re-enables cycle")) return 1;
    }

    // Strategy-1 low-HP FLEE chooses the nearest LOS-valid door,
    // moves toward its cell center, uses timer 0x10 and moves immediately.
    {
        n3d::EpisodeData episode;
        n3d::WorldState world;
        n3d::ObjectRuntime objects;
        n3d::DoorRuntime doors;
        n3d::GuardRuntime guards;
        if(!buildFixture(episode, world, objects, doors, guards))
            return 1;

        world.at(3, 5).wallId = 2;
        world.at(3, 5).wallClass = 0x31;

        std::string error;
        if(!doors.build(world, episode.map, error))
        {
            std::cerr << error << "\n";
            return 1;
        }

        n3d::GuardRuntimeRecord &g = guards.guards()[0];
        n3d::RuntimeObject &o = objects.objects()[g.objectIndex];

        g.strategy = 1;
        g.state = 5;
        g.hp = 100;
        g.moveX = 0;
        g.moveY = 0;

        const int32_t beforeX = o.worldX;
        guards.tickPreviewAI(playerX, playerY, 1);

        if(!require(g.state == 6 && g.timer == 0x10,
                    "FLEE enters state 6 with timer 0x10")) return 1;
        if(!require(g.moveX == -8 && g.moveY == 0,
                    "FLEE points toward nearest west door center")) return 1;
        if(!require(o.worldX == beforeX - 8,
                    "FLEE performs immediate first movement step")) return 1;
    }

    // ACTIONSPOT/Dancers activation uses the Dancers resource family and
    // class-specific +0x24 token for original Skeleton class 0x0B.
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

        world.at(5, 5).wallId = 20;
        world.at(5, 5).wallClass = 0x46;

        const uint8_t oldSequence = o.sequenceObjectId;
        const unsigned activated =
            guards.activateActionSpotDancers();

        if(!require(activated == 1,
                    "ACTIONSPOT activates exactly one synthetic guard")) return 1;
        if(!require(g.state == 0x14 && g.timer == 0x70,
                    "ACTIONSPOT enters state 0x14 with timer 0x70")) return 1;
        if(!require(g.savedSequenceObjectId == oldSequence,
                    "ACTIONSPOT saves original sequence selector")) return 1;
        if(!require(o.sequenceObjectId == 0xA0,
                    "ACTIONSPOT switches to Dancers class 0x21 resource")) return 1;
        if(!require(g.sequenceToken == (20u | (2u << 8)) &&
                    o.animationFrame == 20,
                    "Skeleton Dancer selects Dancers +0x24 token")) return 1;
        if(!require(world.at(5, 5).objectId == 0,
                    "ACTIONSPOT activation clears MAP object byte")) return 1;
        if(!require(g.moveX == 3,
                    "ACTIONSPOT sets scripted X movement component to 3")) return 1;

        g.timer = 0x61;
        const int32_t beforeWait = o.worldX;
        guards.tickPreviewAI(playerX, playerY, 1);
        if(!require(g.timer == 0x60 && o.worldX == beforeWait,
                    "ACTIONSPOT does not move at timer 0x60")) return 1;

        guards.tickPreviewAI(playerX, playerY, 1);
        if(!require(g.timer == 0x5F && o.worldX == beforeWait + 3,
                    "ACTIONSPOT begins movement below timer 0x60")) return 1;

        g.timer = 1;
        const int32_t beforeLastMove = o.worldX;
        guards.tickPreviewAI(playerX, playerY, 1);
        if(!require(g.state == 0x14 && g.timer == 0 &&
                    o.worldX == beforeLastMove + 3,
                    "ACTIONSPOT timer 1 performs final movement before restore")) return 1;

        guards.tickPreviewAI(playerX, playerY, 1);
        if(!require(g.state == 6 && g.timer == 1 && g.strategy == 0,
                    "ACTIONSPOT expiry restores generic state 6 timer 1")) return 1;
        if(!require(o.sequenceObjectId == oldSequence,
                    "ACTIONSPOT restore returns original sequence selector")) return 1;
        if(!require(g.sequenceToken == (24u | (2u << 8)),
                    "ACTIONSPOT restore selects original +0x24 movement token")) return 1;
    }

    // Strategy-3 perception enters state 0x13 with RNG timer 8..87 and
    // cardinal displacement derived from the current facing.
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

        o.objectClass = 0x12;
        o.properties = n3d::objectPropertiesForClass(0x12);
        g.strategy = 3;
        g.state = 7;
        g.perceptionMode = 0;
        g.facing = 2;

        const int32_t nearPlayerX = 6 * 64 + 32;
        const int32_t nearPlayerY = 5 * 64 + 32;

        guards.tickPreviewAI(nearPlayerX, nearPlayerY, 1);

        if(!require(g.state == 0x13,
                    "strategy-3 perception enters state 0x13")) return 1;
        if(!require(g.timer >= 8 && g.timer <= 87,
                    "state-13 initializer timer is RNG % 80 + 8")) return 1;
        if(!require(g.moveX == 8 && g.moveY == 0,
                    "state-13 facing 2 uses cardinal +8 X displacement")) return 1;
    }

    // State-13 uses old/new timer semantics: new timer 8 is event-only, then
    // values 7..0 yield exactly eight 8-unit move attempts.
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

        o.objectClass = 0x12;
        o.properties = n3d::objectPropertiesForClass(0x12);
        g.strategy = 3;
        g.state = 0x13;
        g.facing = 2;
        g.moveX = 8;
        g.moveY = 0;
        g.timer = 9;

        const int32_t startX = o.worldX;
        const int32_t startY = o.worldY;
        const int32_t farPlayerX = 20 * 64 + 32;
        const int32_t farPlayerY = 20 * 64 + 32;

        guards.tickPreviewAI(farPlayerX, farPlayerY, 1);
        if(!require(g.timer == 8,
                    "state-13 old timer 9 decrements to event timer 8")) return 1;
        if(!require(o.worldX == startX && o.worldY == startY,
                    "state-13 timer 8 event tick does not move")) return 1;

        for(int i = 0; i < 8; ++i)
            guards.tickPreviewAI(farPlayerX, farPlayerY, 1);

        if(!require(g.timer == 0,
                    "state-13 eight move attempts consume timers 7..0")) return 1;
        if(!require(o.worldX == startX + 64 && o.worldY == startY,
                    "state-13 eight successful attempts move exactly one tile")) return 1;
        if(!require(o.tileX == 6 && o.tileY == 5,
                    "state-13 successful displacement updates MAP cell")) return 1;

        guards.tickPreviewAI(farPlayerX, farPlayerY, 1);
        if(!require(g.strategy == 0 && g.state == 2,
                    "state-13 old timer zero clears strategy and returns state 2")) return 1;
    }

    // Cannon state 0x0E/0x0F/0x10 cycle uses attack-enable, a table-B
    // directional token via state 0, then fixed Cannon contact damage.
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

        o.objectClass = 0x19;
        o.properties = n3d::objectPropertiesForClass(0x19);
        g.strategy = 4;
        g.state = 0x0E;
        g.facing = 2;
        g.timer = 99;
        objects.inventory().health = 200;

        guards.setCannonAttackEnabled(true);
        guards.tickPreviewAI(playerX, playerY, 1);

        if(!require(g.state == 0x0F && g.timer == 0,
                    "Cannon state 0x0E enabled enters 0x0F with zero timer")) return 1;

        guards.tickPreviewAI(playerX, playerY, 1);
        if(!require(g.state == 0 && g.nextState == 0x10,
                    "Cannon ready state 0x0F schedules state 0 before 0x10")) return 1;
        if(!require(g.sequenceToken == (15u | (2u << 8)),
                    "Cannon 0x0F selects table-B directional token")) return 1;
        if(!require(g.timer == 2,
                    "Cannon 0x0F uses token high byte as state-0 timer")) return 1;

        guards.tickPreviewAI(playerX, playerY, 1);
        guards.tickPreviewAI(playerX, playerY, 1);
        if(!require(g.state == 0x10,
                    "Cannon attack pre-sequence completes into state 0x10")) return 1;

        guards.tickPreviewAI(playerX, playerY, 1);
        if(!require(objects.inventory().health == 100,
                    "Cannon state 0x10 applies fixed medium damage 100")) return 1;
        if(!require(g.state == 0x0F && g.timer == 8,
                    "Cannon state 0x10 returns to 0x0F with timer 8")) return 1;

        guards.setCannonAttackEnabled(false);
        guards.tickPreviewAI(playerX, playerY, 1);
        if(!require(g.state == 0x0E,
                    "disabled Cannon state 0x0F returns to idle 0x0E")) return 1;
    }

    // State 0x11 performs its final movement before clearing deltas and
    // returning to ordinary state 7.
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

        g.state = 0x11;
        g.strategy = 1;
        g.timer = 1;
        g.moveX = 8;
        g.moveY = 0;

        const int32_t beforeX = o.worldX;
        guards.tickPreviewAI(playerX, playerY, 1);

        if(!require(o.worldX == beforeX + 8,
                    "state 0x11 commits final movement before reset")) return 1;
        if(!require(g.state == 7 && g.strategy == 0,
                    "state 0x11 returns to state 7 with generic strategy")) return 1;
        if(!require(g.moveX == 0 && g.moveY == 0,
                    "state 0x11 clears movement deltas after final step")) return 1;
    }

    // States 2/3/4 use the dedicated SEQDEF words at +34/+36/+38.
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

        g.state = 2;
        guards.tickPreviewAI(playerX, playerY, 1);
        if(!require(g.state == 0 && g.nextState == 3,
                    "state 2 enters timed token state 0 -> 3")) return 1;
        if(!require(g.sequenceToken == (30u | (2u << 8)) &&
                    o.animationFrame == 30,
                    "state 2 selects SEQDEF +34 token")) return 1;

        guards.tickPreviewAI(playerX, playerY, 1);
        if(!require(g.state == 3 && o.animationFrame == 31,
                    "state-0 token advances frame then restores state 3")) return 1;

        guards.tickPreviewAI(playerX, playerY, 1);
        if(!require(g.state == 0 && g.nextState == 4,
                    "state 3 perception selects timed attack pre-sequence")) return 1;
        if(!require(g.sequenceToken == (32u | (2u << 8)) &&
                    o.animationFrame == 32,
                    "state 3 selects SEQDEF +36 token")) return 1;
    }

    // Pain and lethal routes use the two seven-way alternate token banks.
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
        guards.applyPlayerWeaponHit(0, 2, 1, 80, &killed);

        if(!require(!killed && g.sequenceToken == (36u | (2u << 8)),
                    "nonlethal pain chooses first alternate token bank")) return 1;
        if(!require(g.state == 0 && g.nextState == 5 &&
                    o.animationFrame == 36,
                    "state-7 pain enters timed token state 0 -> 5")) return 1;

        g.hp = 1;
        o.lastProjectedY = 120;
        guards.applyPlayerWeaponHit(0, 2, 1, 80, &killed);
        if(!require(killed && g.sequenceToken == (40u | (2u << 8)),
                    "lethal hit chooses second alternate death token bank")) return 1;
        if(!require(o.animationFrame == 40,
                    "lethal sequence begins at death token low byte")) return 1;
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
        if(!require(g.state == 0 && g.nextState == 5,
                    "surviving state-4 attack enters +38 timed token before state 5")) return 1;
        if(!require(g.sequenceToken == (34u | (2u << 8)),
                    "state-4 tail selects SEQDEF +38 token")) return 1;
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
        if(!require(g.state == 0 && g.nextState == 5,
                    "suppressed state-4 attack still schedules +38 token before state 5")) return 1;
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
    std::remove("n3d_guard_ai_test.img");
    std::cout << "N3D GUARD AI core tests passed\n";
    return 0;
}
