#include "n3d/n3d_collision.h"
#include "n3d/n3d_data.h"
#include "n3d/n3d_door.h"
#include "n3d/n3d_guard.h"
#include "n3d/n3d_object.h"
#include "n3d/n3d_projectile.h"
#include "n3d/n3d_world.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{

bool require(bool condition, const char *message)
{
    if(condition)
        return true;
    std::cerr << "FAIL: " << message << "\n";
    return false;
}

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
    bytes.push_back(1); // width
    bytes.push_back(1); // height
    for(int i = 0; i < 8; ++i)
        bytes.push_back(0);
    bytes.push_back(color);
}

bool writeSyntheticImg(const char *path)
{
    std::vector<unsigned char> bytes(n3d::ImgArchive::FrameDataOffset, 0);

    const unsigned char ids[4] = {0xFB, 0xFC, 0xFD, 0xFE};
    const unsigned short intervals[4] = {50, 100, 50, 100};
    const unsigned char counts[4] = {2, 3, 2, 2};

    for(int s = 0; s < 4; ++s)
    {
        const unsigned char id = ids[s];
        const unsigned long stream =
            static_cast<unsigned long>(bytes.size());

        writeU32(bytes, 0x400u + static_cast<unsigned>(id) * 4u,
                 stream);

        const size_t seq =
            static_cast<size_t>(n3d::ImgArchive::HighSequenceBankOffset) +
            static_cast<size_t>(id) *
            static_cast<size_t>(n3d::ImgArchive::SequenceRecordBytes);
        writeU16(bytes, seq, intervals[s]);
        bytes[seq + 2] = counts[s];
        bytes[seq + 3] = 0;

        for(unsigned f = 0; f < counts[s]; ++f)
            appendTinyFrame(bytes,
                            static_cast<unsigned char>(0x30 + s * 4 + f));
    }

    std::ofstream f(path, std::ios::binary);
    if(!f)
        return false;
    f.write(reinterpret_cast<const char *>(&bytes[0]),
            static_cast<std::streamsize>(bytes.size()));
    return !!f;
}

bool writeSyntheticMap(const char *path)
{
    const size_t size =
        n3d::MapArchive::HeaderSize + n3d::MapArchive::LevelBytes;
    std::vector<unsigned char> bytes(size, 0);

    bytes[0] = 1;

    bytes[0x002 + 1] = 0x01; // hard wall
    bytes[0x002 + 2] = 0x31; // dynamic door

    for(int id = 1; id <= 4; ++id)
        bytes[0x102 + id] = 0x02; // START

    for(int id = 0x90; id <= 0x93; ++id)
        bytes[0x102 + id] = 0x0B; // Skeleton GUARD

    for(int id = 0xFB; id <= 0xFE; ++id)
        bytes[0x102 + id] = 0x05; // MISSILE flight/impact family

    const size_t base = n3d::MapArchive::HeaderSize;

    size_t index = static_cast<size_t>(5 * 64 + 2);
    bytes[base + index * 2 + 1] = 2; // player START

    index = static_cast<size_t>(5 * 64 + 5);
    bytes[base + index * 2 + 1] = 0x91; // guard

    std::ofstream f(path, std::ios::binary);
    if(!f)
        return false;
    f.write(reinterpret_cast<const char *>(&bytes[0]),
            static_cast<std::streamsize>(bytes.size()));
    return !!f;
}

struct Fixture
{
    n3d::EpisodeData episode;
    n3d::WorldState world;
    n3d::ObjectRuntime objects;
    n3d::DoorRuntime doors;
    n3d::GuardRuntime guards;
    n3d::ProjectileRuntime projectiles;
};

bool buildFixture(Fixture &f)
{
    const char *path = "n3d_projectile_test.map";
    const char *imgPath = "n3d_projectile_test.img";
    if(!writeSyntheticMap(path) || !writeSyntheticImg(imgPath))
        return false;

    std::string error;
    if(!f.episode.map.load(path, error))
    {
        std::cerr << error << "\n";
        return false;
    }

    if(!f.episode.img.load(imgPath, error))
    {
        std::cerr << error << "\n";
        return false;
    }

    f.episode.episode = 1;

    if(!n3d::buildWorld(f.episode, 0, f.world, error))
    {
        std::cerr << error << "\n";
        return false;
    }

    if(!f.objects.build(f.world, f.episode.map, error))
    {
        std::cerr << error << "\n";
        return false;
    }

    if(!f.doors.build(f.world, f.episode.map, error))
    {
        std::cerr << error << "\n";
        return false;
    }

    f.objects.bindDoors(&f.doors);

    if(!f.guards.build(f.world, f.episode.map, f.objects, &f.doors,
                       f.episode.episode, error))
    {
        std::cerr << error << "\n";
        return false;
    }

    f.projectiles.bind(&f.world, &f.objects, &f.doors, &f.guards,
                       &f.episode.map, &f.episode.img);
    return true;
}

}

int main()
{
    const int32_t startX = 3 * 64 + 32;
    const int32_t startY = 5 * 64 + 32;

    // Fire cadence thresholds are [2,1,3,1]. Weapons 0..2 require a
    // fresh FIRE edge; weapon 3 may repeat while held once its threshold is met.
    {
        Fixture f;
        if(!buildFixture(f)) return 1;

        n3d::InventoryState &inv = f.objects.inventory();

        inv.activeWeapon = 0;
        if(!require(!f.objects.acceptFireAttempt(true),
                    "W0 rejects before cadence threshold")) return 1;
        f.objects.tickWeaponCadence();
        if(!require(!f.objects.acceptFireAttempt(true),
                    "W0 still rejects at cadence 1")) return 1;
        f.objects.tickWeaponCadence();
        if(!require(f.objects.acceptFireAttempt(true),
                    "W0 accepts new edge at cadence 2")) return 1;
        if(!require(inv.fireCadenceCounter == 0,
                    "accepted cadence attempt resets counter")) return 1;

        f.objects.tickWeaponCadence();
        f.objects.tickWeaponCadence();
        if(!require(!f.objects.acceptFireAttempt(false),
                    "W0 held FIRE does not auto-repeat")) return 1;

        inv.activeWeapon = 2;
        inv.fireCadenceCounter = 0;
        f.objects.tickWeaponCadence();
        f.objects.tickWeaponCadence();
        if(!require(!f.objects.acceptFireAttempt(true),
                    "W2 rejects before cadence 3")) return 1;
        f.objects.tickWeaponCadence();
        if(!require(f.objects.acceptFireAttempt(true),
                    "W2 accepts at cadence 3")) return 1;

        inv.activeWeapon = 3;
        inv.fireCadenceCounter = 0;
        f.objects.tickWeaponCadence();
        if(!require(f.objects.acceptFireAttempt(false),
                    "W3 auto-repeats while FIRE stays held")) return 1;
    }

    // Pool is exactly eight slots and a full pool must not consume ammo.
    {
        Fixture f;
        if(!buildFixture(f)) return 1;

        f.objects.inventory().plasmaAmmo = 20;
        f.objects.inventory().activeWeapon = 0;

        for(int i = 0; i < 8; ++i)
        {
            const n3d::ProjectileFireResult result =
                f.projectiles.fire(startX, startY, 0, 16384, 0);
            if(!require(result == n3d::ProjectileFireAccepted,
                        "first eight projectile allocations succeed")) return 1;
        }

        if(!require(f.projectiles.activeCount() == 8,
                    "projectile pool contains exactly eight active slots")) return 1;
        if(!require(f.objects.inventory().plasmaAmmo == 12,
                    "eight accepted plasma shots consume eight ammo")) return 1;

        const n3d::ProjectileFireResult ninth =
            f.projectiles.fire(startX, startY, 0, 16384, 0);
        if(!require(ninth == n3d::ProjectileFirePoolFull,
                    "ninth projectile is rejected by full pool")) return 1;
        if(!require(f.objects.inventory().plasmaAmmo == 12,
                    "full-pool rejection preserves ammo")) return 1;
    }

    // Weapon 3 intentionally shares weapon-0 plasma ammunition.
    {
        Fixture f;
        if(!buildFixture(f)) return 1;

        f.objects.inventory().plasmaAmmo = 2;
        const n3d::ProjectileFireResult result =
            f.projectiles.fire(startX, startY, 3, 16384, 0);

        if(!require(result == n3d::ProjectileFireAccepted,
                    "weapon 3 projectile allocation succeeds")) return 1;
        if(!require(f.objects.inventory().plasmaAmmo == 1,
                    "weapon 3 consumes shared plasma ammo")) return 1;
    }

    // No-ammo rejection leaves the pool untouched.
    {
        Fixture f;
        if(!buildFixture(f)) return 1;

        f.objects.inventory().wandAmmo = 0;
        const n3d::ProjectileFireResult result =
            f.projectiles.fire(startX, startY, 1, 16384, 0);
        if(!require(result == n3d::ProjectileFireNoAmmo,
                    "wand with zero ammo is rejected")) return 1;
        if(!require(f.projectiles.activeCount() == 0,
                    "no-ammo rejection allocates no slot")) return 1;
    }

    // New projectile waits until the next projectile-update call before moving.
    {
        Fixture f;
        if(!buildFixture(f)) return 1;

        f.objects.inventory().plasmaAmmo = 2;
        f.projectiles.fire(startX, startY, 0, 16384, 0);

        const int32_t before = f.projectiles.slot(0).worldX;
        f.projectiles.tick(40, 20, 1, 80);
        if(!require(f.projectiles.slot(0).worldX == before,
                    "new projectile does not move in allocator frame")) return 1;

        f.projectiles.tick(40, 20, 1, 80);
        if(!require(f.projectiles.slot(0).worldX == before + 20,
                    "east projectile advances one world unit per substep")) return 1;
        if(!require(f.projectiles.slot(0).verticalOffset == 6,
                    "flying projectile elevation increments after movement")) return 1;
    }

    // Hard wall collision switches flight to impact before reaching the GUARD.
    {
        Fixture f;
        if(!buildFixture(f)) return 1;

        f.world.at(4, 5).wallId = 1;
        f.world.at(4, 5).wallClass = 1;

        f.objects.inventory().plasmaAmmo = 4;
        f.objects.inventory().activeWeapon = 0;
        f.projectiles.fire(startX, startY, 0, 16384, 0);

        f.projectiles.tick(40, 20, 1, 80); // pending first update
        n3d::ProjectileUpdateReport total;
        for(int i = 0; i < 4; ++i)
        {
            const n3d::ProjectileUpdateReport r =
                f.projectiles.tick(static_cast<uint32_t>(80 + i * 40),
                                   20, 1, 80);
            total.impacts += r.impacts;
            total.guardHits += r.guardHits;
            total.guardKills += r.guardKills;
            if(total.impacts != 0)
                break;
        }

        if(!require(total.impacts == 1,
                    "hard wall terminates projectile flight")) return 1;
        if(!require(total.guardHits == 0,
                    "wall impact occurs before GUARD")) return 1;
    }

    // Projectile route does not require a current render stamp. Damage uses
    // the CURRENT weapon selector at impact, not the spawn weapon.
    {
        Fixture f;
        if(!buildFixture(f)) return 1;

        n3d::GuardRuntimeRecord &g = f.guards.guards()[0];
        n3d::RuntimeObject &o = f.objects.objects()[g.objectIndex];

        // Ghost class: weapon 1 damages, non-wand weapons are hard-zero.
        o.objectClass = 0x1A;
        o.properties = n3d::objectPropertiesForClass(0x1A);
        o.lastProjectedY = 100;
        f.world.at(5, 5).objectClass = 0x1A;

        g.hp = 255;
        g.renderStamp = 0; // explicitly stale / never rendered

        f.objects.inventory().plasmaAmmo = 4;
        f.objects.inventory().activeWeapon = 0;
        f.projectiles.fire(4 * 64 + 32, startY, 0, 16384, 0);

        // Switch after firing. Original shared damage helper reads the current
        // selector when the projectile actually hits.
        f.objects.inventory().activeWeapon = 1;

        f.projectiles.tick(40, 20, 1, 80); // pending first update

        n3d::ProjectileUpdateReport total;
        for(int i = 0; i < 4; ++i)
        {
            const n3d::ProjectileUpdateReport r =
                f.projectiles.tick(static_cast<uint32_t>(80 + i * 40),
                                   20, 1, 80);
            total.impacts += r.impacts;
            total.guardHits += r.guardHits;
            total.guardKills += r.guardKills;
            if(total.guardHits != 0)
                break;
        }

        if(!require(total.guardHits == 1,
                    "projectile hits GUARD without render-generation gate")) return 1;
        if(!require(g.hp < 255,
                    "projectile impact uses current wand selector for Ghost damage")) return 1;
    }

    std::remove("n3d_projectile_test.map");
    std::remove("n3d_projectile_test.img");
    std::cout << "N3D projectile core tests passed\n";
    return 0;
}
