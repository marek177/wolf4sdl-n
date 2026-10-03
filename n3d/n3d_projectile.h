#ifndef N3D_PROJECTILE_H
#define N3D_PROJECTILE_H

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace n3d
{

class WorldState;
class ObjectRuntime;
class DoorRuntime;
class GuardRuntime;
class MapArchive;
class ImgArchive;
struct ImgSequenceDef;

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
    uint8_t sequenceObjectId;
    uint8_t frame;
    uint32_t animationDeadlineMs;
    bool firstUpdatePending;

    ProjectileSlot();
};

struct ExplodingWallRecord
{
    int tileX;
    int tileY;
    uint8_t sourceWallId;
    uint8_t sourceWallClass;
    uint8_t sequenceWallId;
    uint8_t frame;
    uint32_t animationDeadlineMs;
    bool active;

    ExplodingWallRecord();
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
              GuardRuntime *guards,
              const MapArchive *map,
              const ImgArchive *img);

    void clear();

    const ProjectileSlot &slot(size_t index) const { return slots_[index]; }
    ProjectileSlot &slot(size_t index) { return slots_[index]; }

    const std::vector<ExplodingWallRecord> &explodingWalls() const
    {
        return explodingWalls_;
    }
    const ExplodingWallRecord *explodingWallAt(int tileX,
                                               int tileY) const;
    bool explodingWallVisual(int tileX, int tileY,
                             uint8_t &wallId,
                             unsigned &frameIndex) const;

    ProjectileFireResult fire(int32_t playerWorldX,
                              int32_t playerWorldY,
                              uint8_t currentWeapon,
                              int directionX,
                              int directionY);

    ProjectileUpdateReport tick(uint32_t nowMs,
                                unsigned substeps,
                                int difficultyCode,
                                int viewportCenterY);

    unsigned activeCount() const;

private:
    bool startExplodingWall(int tileX, int tileY,
                            uint8_t wallId,
                            uint8_t wallClass,
                            uint32_t nowMs);
    void updateExplodingWalls(uint32_t nowMs);
    int firstWallIdForClass(uint8_t wallClass) const;

    bool collide(ProjectileSlot &slot,
                 int32_t candidateX,
                 int32_t candidateY,
                 uint32_t nowMs,
                 int difficultyCode,
                 int viewportCenterY,
                 ProjectileUpdateReport &report);
    uint8_t sequenceObjectIdForWeapon(uint8_t weaponId,
                                      bool impact) const;
    const ImgSequenceDef *sequenceFor(const ProjectileSlot &slot) const;
    void advanceFlightAnimation(ProjectileSlot &slot, uint32_t nowMs);
    bool advanceImpactAnimation(ProjectileSlot &slot, uint32_t nowMs);

    WorldState *world_;
    ObjectRuntime *objects_;
    DoorRuntime *doors_;
    GuardRuntime *guards_;
    const MapArchive *map_;
    const ImgArchive *img_;
    uint8_t missileBaseObjectId_;
    ProjectileSlot slots_[SlotCount];
    std::vector<ExplodingWallRecord> explodingWalls_;
};

} // namespace n3d

#endif
