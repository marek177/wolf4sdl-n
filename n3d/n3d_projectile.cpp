#include "n3d_projectile.h"

#include "n3d_collision.h"
#include "n3d_door.h"
#include "n3d_data.h"
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
      sequenceWeapon(0), sequenceObjectId(0xff), frame(0),
      animationDeadlineMs(0), firstUpdatePending(false)
{
}

ProjectileRuntime::ProjectileRuntime()
    : world_(0), objects_(0), doors_(0), guards_(0),
      map_(0), img_(0), missileBaseObjectId_(0xff)
{
}

void ProjectileRuntime::bind(WorldState *world,
                             ObjectRuntime *objects,
                             DoorRuntime *doors,
                             GuardRuntime *guards,
                             const MapArchive *map,
                             const ImgArchive *img)
{
    world_ = world;
    objects_ = objects;
    doors_ = doors;
    guards_ = guards;
    map_ = map;
    img_ = img;
    missileBaseObjectId_ = 0xff;

    if(map_)
    {
        for(int id = 0; id < 256; ++id)
        {
            if(map_->objectClass(static_cast<uint8_t>(id)) == 0x05)
            {
                missileBaseObjectId_ = static_cast<uint8_t>(id);
                break;
            }
        }
    }

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

uint8_t ProjectileRuntime::sequenceObjectIdForWeapon(uint8_t weaponId,
                                                     bool impact) const
{
    static const uint8_t flightOffset[4] = {0, 2, 0, 0};
    static const uint8_t impactOffset[4] = {1, 3, 1, 1};

    if(missileBaseObjectId_ == 0xff || weaponId > 3)
        return 0xff;

    const unsigned offset =
        impact ? impactOffset[weaponId] : flightOffset[weaponId];
    const unsigned id =
        static_cast<unsigned>(missileBaseObjectId_) + offset;

    if(id >= 256u)
        return 0xff;

    const uint8_t objectId = static_cast<uint8_t>(id);
    if(!map_ || map_->objectClass(objectId) != 0x05)
        return 0xff;

    return objectId;
}

const ImgSequenceDef *ProjectileRuntime::sequenceFor(
    const ProjectileSlot &slot) const
{
    if(!img_ || slot.sequenceObjectId == 0xff)
        return 0;

    return img_->objectSequence(slot.sequenceObjectId);
}

void ProjectileRuntime::advanceFlightAnimation(ProjectileSlot &slot,
                                               uint32_t nowMs)
{
    const ImgSequenceDef *sequence = sequenceFor(slot);
    if(!sequence || sequence->frameCount == 0)
        return;

    if(slot.animationDeadlineMs > nowMs)
        return;

    unsigned next = static_cast<unsigned>(slot.frame) + 1u;
    if(next >= sequence->frameCount)
        next = 0;

    slot.frame = static_cast<uint8_t>(next);
    slot.animationDeadlineMs =
        nowMs + static_cast<uint32_t>(sequence->intervalMs);
}

bool ProjectileRuntime::advanceImpactAnimation(ProjectileSlot &slot,
                                               uint32_t nowMs)
{
    const ImgSequenceDef *sequence = sequenceFor(slot);
    if(!sequence || sequence->frameCount == 0)
        return true;

    if(slot.animationDeadlineMs > nowMs)
        return false;

    const unsigned next =
        static_cast<unsigned>(slot.frame) + 1u;

    if(next >= sequence->frameCount)
        return true;

    slot.frame = static_cast<uint8_t>(next);
    slot.animationDeadlineMs =
        nowMs + static_cast<uint32_t>(sequence->intervalMs);
    return false;
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

    const uint8_t flightSequenceObject =
        sequenceObjectIdForWeapon(currentWeapon, false);
    if(flightSequenceObject == 0xff)
        return ProjectileFireInvalidWeapon;

    const ImgSequenceDef *flightSequence =
        img_ ? img_->objectSequence(flightSequenceObject) : 0;
    if(!flightSequence || flightSequence->frameCount == 0)
        return ProjectileFireInvalidWeapon;

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
    slot.sequenceObjectId = flightSequenceObject;
    slot.frame = 0;
    slot.animationDeadlineMs = 0;
    slot.firstUpdatePending = true;

    slots_[freeIndex] = slot;
    return ProjectileFireAccepted;
}

bool ProjectileRuntime::collide(ProjectileSlot &slot,
                                int32_t candidateX,
                                int32_t candidateY,
                                uint32_t nowMs,
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

ProjectileUpdateReport ProjectileRuntime::tick(uint32_t nowMs,
                                               unsigned substeps,
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
            if(advanceImpactAnimation(slot, nowMs))
                slot = ProjectileSlot();
            continue;
        }

        advanceFlightAnimation(slot, nowMs);

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
                       nowMs,
                       difficultyCode, viewportCenterY, report))
            {
                const uint8_t currentWeapon =
                    objects_ && objects_->inventory().activeWeapon <= 3
                        ? objects_->inventory().activeWeapon
                        : slot.sequenceWeapon;

                slot.lifecycle = 2;
                slot.sequenceObjectId =
                    sequenceObjectIdForWeapon(currentWeapon, true);
                slot.frame = 0;

                const ImgSequenceDef *impactSequence = sequenceFor(slot);
                slot.animationDeadlineMs =
                    nowMs + static_cast<uint32_t>(
                        impactSequence ? impactSequence->intervalMs : 0);

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
