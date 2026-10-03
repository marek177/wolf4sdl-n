#ifndef N3D_PROJECTILE_H
#define N3D_PROJECTILE_H

#include <stddef.h>
#include <stdint.h>

namespace n3d
{

class WorldState;
class ObjectRuntime;
class DoorRuntime;
class GuardRuntime;

enum ProjectileFireResult
{
    ProjectileFireAccepted = 0,
    ProjectileFireNoAmmo,
    ProjectileFirePoolFull,
    ProjectileFireInvalidWeapon,
    ProjectileFireInvalidDirection
};

struct ProjectileSlot
{
    int16_t majorAxis;
    int16_t error;
    int16_t minorIncrement;
    int16_t correctionIncrement;
    int16_t stepX;
    int16_t stepY;
    uint8_t lifecycle;
    uint8_t reserved;
    int32_t worldX;
    int32_t worldY;
    uint8_t verticalOffset;
    uint8_t sequenceWeapon;
    uint8_t frame;
    uint8_t impactUpdatesRemaining;
    bool firstUpdatePending;

    ProjectileSlot();
};

struct ProjectileUpdateReport
{
    unsigned impacts;
    unsigned guardHits;
    unsigned guardKills;

    ProjectileUpdateReport() : impacts(0), guardHits(0), guardKills(0) {}
};

class ProjectileRuntime
{
public:
    enum { SlotCount = 8 };

    ProjectileRuntime();

    void bind(WorldState *world,
              ObjectRuntime *objects,
              DoorRuntime *doors,
              GuardRuntime *guards);

    void clear();

    const ProjectileSlot &slot(size_t index) const { return slots_[index]; }
    ProjectileSlot &slot(size_t index) { return slots_[index]; }

    ProjectileFireResult fire(int32_t playerWorldX,
                              int32_t playerWorldY,
                              uint8_t currentWeapon,
                              int directionX,
                              int directionY);

    ProjectileUpdateReport tick(unsigned substeps,
                                int difficultyCode,
                                int viewportCenterY);

    unsigned activeCount() const;

private:
    bool collide(ProjectileSlot &slot,
                 int32_t candidateX,
                 int32_t candidateY,
                 int difficultyCode,
                 int viewportCenterY,
                 ProjectileUpdateReport &report);

    WorldState *world_;
    ObjectRuntime *objects_;
    DoorRuntime *doors_;
    GuardRuntime *guards_;
    ProjectileSlot slots_[SlotCount];
};

} // namespace n3d

#endif
