#include "n3d_projectile.h"

#include "n3d_collision.h"
#include "n3d_door.h"
#include "n3d_guard.h"
#include "n3d_object.h"
#include "n3d_world.h"

namespace n3d
{
namespace
{

int absInt(int value)
{
    return value < 0 ? -value : value;
}

int signInt(int value)
{
    return value < 0 ? -1 : (value > 0 ? 1 : 0);
}

}

ProjectileSlot::ProjectileSlot()
    : majorAxis(0), error(0), minorIncrement(0), correctionIncrement(0),
      stepX(0), stepY(0), lifecycle(0), reserved(0),
      worldX(0), worldY(0), verticalOffset(0),
      sequenceWeapon(0), frame(0), impactUpdatesRemaining(0),
      firstUpdatePending(false)
{
}

ProjectileRuntime::ProjectileRuntime()
    : world_(0), objects_(0), doors_(0), guards_(0)
{
}

void ProjectileRuntime::bind(WorldState *world,
                             ObjectRuntime *objects,
                             DoorRuntime *doors,
                             GuardRuntime *guards)
{
    world_ = world;
    objects_ = objects;
    doors_ = doors;
    guards_ = guards;
    clear();
}

void ProjectileRuntime::clear()
{
    for(size_t i = 0; i < SlotCount; ++i)
        slots_[i] = ProjectileSlot();
}

unsigned ProjectileRuntime::activeCount() const
{
    unsigned count = 0;
    for(size_t i = 0; i < SlotCount; ++i)
        if(slots_[i].lifecycle != 0)
            ++count;
    return count;
}

ProjectileFireResult ProjectileRuntime::fire(int32_t playerWorldX,
                                             int32_t playerWorldY,
                                             uint8_t currentWeapon,
                                             int directionX,
                                             int directionY)
{
    if(currentWeapon > 3 || currentWeapon == 2)
        return ProjectileFireInvalidWeapon;

    if(directionX == 0 && directionY == 0)
        return ProjectileFireInvalidDirection;

    size_t freeIndex = SlotCount;
    for(size_t i = 0; i < SlotCount; ++i)
    {
        if(slots_[i].lifecycle == 0)
        {
            freeIndex = i;
            break;
        }
    }

    // Original allocator checks the pool before consuming ammunition.
    if(freeIndex == SlotCount)
        return ProjectileFirePoolFull;

    if(!objects_ || !objects_->consumeWeaponAmmo(currentWeapon))
        return ProjectileFireNoAmmo;

    const int ax = absInt(directionX);
    const int ay = absInt(directionY);
    const int minor = ax <= ay ? ax : ay;
    const int major = ax <= ay ? ay : ax;

    ProjectileSlot slot;
    slot.majorAxis = static_cast<int16_t>(ax > ay ? 1 : 0);
    slot.minorIncrement = static_cast<int16_t>(minor * 2);
    slot.error = static_cast<int16_t>(minor * 2 - major);
    slot.correctionIncrement =
        static_cast<int16_t>((minor - major) * 2);
    slot.stepX = static_cast<int16_t>(signInt(directionX));
    slot.stepY = static_cast<int16_t>(signInt(directionY));
    slot.lifecycle = 1;
    slot.worldX = playerWorldX;
    slot.worldY = playerWorldY;
    slot.verticalOffset = 5;
    slot.sequenceWeapon = currentWeapon;
    slot.frame = 0;
    slot.impactUpdatesRemaining = 0;
    slot.firstUpdatePending = true;

    slots_[freeIndex] = slot;
    return ProjectileFireAccepted;
}

bool ProjectileRuntime::collide(ProjectileSlot &slot,
                                int32_t candidateX,
                                int32_t candidateY,
                                int difficultyCode,
                                int viewportCenterY,
                                ProjectileUpdateReport &report)
{
    if(!world_)
        return true;

    const int tileX = static_cast<int>(worldToTile(candidateX));
    const int tileY = static_cast<int>(worldToTile(candidateY));

    if(tileX < 0 || tileY < 0 ||
       tileX >= WorldState::Width || tileY >= WorldState::Height)
        return true;

    const WorldCell &cell =
        world_->at(static_cast<size_t>(tileX),
                   static_cast<size_t>(tileY));

    const uint8_t wallFlags = wallPropertiesForClass(cell.wallClass);

    // Hard/projectile-blocking walls always terminate flight. Explodable-wall
    // conversion is intentionally a separate layer; the collision result is
    // already exact here.
    if((wallFlags & 0x04) != 0)
        return true;

    if((wallFlags & 0x08) != 0)
    {
        if(!doors_ || !doors_->allowsPassage(tileX, tileY))
            return true;
    }

    const uint8_t objectFlags =
        objectPropertiesForClass(cell.objectClass);

    // Object bit 0x40 invokes an interaction helper in the original. The
    // interaction subsystem is not yet ported, so preserve the conservative
    // handled-impact behavior rather than letting the projectile pass through.
    if((objectFlags & 0x40) != 0)
        return true;

    if((objectFlags & 0x08) != 0)
    {
        if(!guards_ || !objects_)
            return false;

        for(size_t i = 0; i < guards_->guards().size(); ++i)
        {
            GuardRuntimeRecord &guard = guards_->guards()[i];
            if(guard.objectIndex >= objects_->objects().size())
                continue;

            RuntimeObject &object = objects_->objects()[guard.objectIndex];
            if(!object.active || guard.hp == 0)
                continue;

            if(object.tileX != tileX || object.tileY != tileY)
                continue;

            // Original 9B64 returns "continue" immediately when the actor is
            // in the visited cell but the projectile has not yet reached the
            // +/-9 world-unit hit box. It does NOT fall through to generic
            // object blocking for that actor cell.
            if(absInt(static_cast<int>(object.worldX - candidateX)) >= 10 ||
               absInt(static_cast<int>(object.worldY - candidateY)) >= 10)
                return false;

            const uint8_t damageWeapon =
                objects_->inventory().activeWeapon <= 3
                    ? objects_->inventory().activeWeapon
                    : slot.sequenceWeapon;

            bool killed = false;
            guards_->applyPlayerWeaponHit(i, damageWeapon,
                                          difficultyCode,
                                          viewportCenterY,
                                          &killed);
            ++report.guardHits;
            if(killed)
                ++report.guardKills;
            return true;
        }

        return false;
    }

    if((objectFlags & 0x02) != 0 &&
       (objectFlags & 0x20) == 0)
        return true;

    return false;
}

ProjectileUpdateReport ProjectileRuntime::tick(unsigned substeps,
                                               int difficultyCode,
                                               int viewportCenterY)
{
    ProjectileUpdateReport report;

    for(size_t i = 0; i < SlotCount; ++i)
    {
        ProjectileSlot &slot = slots_[i];

        if(slot.lifecycle == 0)
            continue;

        if(slot.firstUpdatePending)
        {
            slot.firstUpdatePending = false;
            continue;
        }

        if(slot.lifecycle == 2)
        {
            // The original duration is SEQDEF-driven. Until the sequence cache
            // is connected, keep the impact state for one update and then free
            // the slot. Flight/collision semantics remain independent.
            if(slot.impactUpdatesRemaining > 0)
                --slot.impactUpdatesRemaining;
            if(slot.impactUpdatesRemaining == 0)
                slot = ProjectileSlot();
            continue;
        }

        int32_t x = slot.worldX;
        int32_t y = slot.worldY;

        for(unsigned step = 0; step < substeps; ++step)
        {
            if(slot.majorAxis == 0)
                y += slot.stepY;
            else
                x += slot.stepX;

            if(slot.error < 0)
            {
                slot.error =
                    static_cast<int16_t>(slot.error + slot.minorIncrement);
            }
            else
            {
                slot.error =
                    static_cast<int16_t>(slot.error + slot.correctionIncrement);

                if(slot.majorAxis == 0)
                    x += slot.stepX;
                else
                    y += slot.stepY;
            }

            if(collide(slot, x, y,
                       difficultyCode, viewportCenterY, report))
            {
                slot.lifecycle = 2;
                slot.frame = 0;
                slot.impactUpdatesRemaining = 1;
                ++report.impacts;
                break;
            }
        }

        if(slot.verticalOffset < 20)
            ++slot.verticalOffset;

        slot.worldX = x;
        slot.worldY = y;
    }

    return report;
}

} // namespace n3d
