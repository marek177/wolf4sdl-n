#ifndef N3D_GUARD_H
#define N3D_GUARD_H

#include "n3d_data.h"
#include "n3d_object.h"
#include "n3d_world.h"

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace n3d
{

class DoorRuntime;

struct PlayerHitReport
{
    unsigned hitCount;
    unsigned killCount;
    int32_t scoreDelta;

    PlayerHitReport() : hitCount(0), killCount(0), scoreDelta(0) {}
};

struct GuardRuntimeRecord
{
    uint32_t renderStamp;
    uint16_t sequenceToken;
    uint16_t timer;
    uint16_t objectIndex;
    uint8_t strategy;
    uint8_t state;
    uint8_t nextState;
    uint8_t savedMapObjectId;
    uint8_t areaId;
    uint8_t syncFlag;
    uint8_t hp;
    uint8_t facing;
    uint8_t directionCache;
    int8_t moveX;
    int8_t moveY;
    int8_t verticalBobStep;
    uint8_t perceptionMode;
    uint8_t losResult;
    uint8_t proximityResult;

    GuardRuntimeRecord();
};

class GuardRuntime
{
public:
    GuardRuntime();

    bool build(WorldState &world,
               const MapArchive &map,
               const ImgArchive &img,
               ObjectRuntime &objects,
               DoorRuntime *doors,
               int episode,
               std::string &error);

    const std::vector<GuardRuntimeRecord> &guards() const { return guards_; }
    std::vector<GuardRuntimeRecord> &guards() { return guards_; }

    void tickPreviewAI(int32_t playerWorldX, int32_t playerWorldY,
                       int difficultyCode);

    uint32_t beginRenderGeneration();
    void markProjectedObject(size_t objectIndex,
                             int projectedBaselineY,
                             int spriteLeft,
                             int spriteRight,
                             int centerX);

    PlayerHitReport fireHitscan(int32_t playerWorldX,
                                int32_t playerWorldY,
                                uint8_t weaponId,
                                int difficultyCode,
                                int viewportCenterY);

    uint8_t applyPlayerWeaponHit(size_t guardIndex,
                                 uint8_t weaponId,
                                 int difficultyCode,
                                 int viewportCenterY,
                                 bool *killed);

    uint32_t nextGameplayRandom();

    void setCannonAttackEnabled(bool enabled)
    {
        cannonAttackEnabled_ = enabled;
    }
    bool cannonAttackEnabled() const
    {
        return cannonAttackEnabled_;
    }

private:
    struct InitialProfile
    {
        uint8_t strategy;
        uint8_t state;
        uint8_t nextState;
        uint8_t perceptionMode;
    };

    InitialProfile initialProfile(uint8_t objectClass) const;
    void setMovementFromFacing(GuardRuntimeRecord &guard);
    uint8_t computeDirectionToPlayer(bool wide,
                                     const RuntimeObject &object,
                                     int32_t playerWorldX,
                                     int32_t playerWorldY) const;
    void refreshDirectionalSequence(GuardRuntimeRecord &guard,
                                    RuntimeObject &object,
                                    int32_t playerWorldX,
                                    int32_t playerWorldY,
                                    bool force);
    const ImgSequenceDef *objectSequence(
        const RuntimeObject &object) const;
    void setSequenceToken(GuardRuntimeRecord &guard,
                          RuntimeObject &object,
                          uint16_t token,
                          uint8_t currentState,
                          uint8_t nextState);
    bool advanceTokenFrame(GuardRuntimeRecord &guard,
                           RuntimeObject &object,
                           bool loop);
    uint16_t chooseAlternativeToken(const GuardRuntimeRecord &guard,
                                    const RuntimeObject &object,
                                    bool secondTable);
    void updateRenderFacing(RuntimeObject &object,
                            const GuardRuntimeRecord &guard);
    bool testLineOfSight(const GuardRuntimeRecord &guard,
                         const RuntimeObject &object,
                         int32_t playerWorldX,
                         int32_t playerWorldY,
                         bool bypassFacing,
                         bool testObjectPlane) const;
    bool traceGridLine(int startX, int startY,
                       int deltaX, int deltaY,
                       int maxSteps,
                       bool testObjectPlane) const;
    bool updatePerception(GuardRuntimeRecord &guard,
                          const RuntimeObject &object,
                          int32_t playerWorldX,
                          int32_t playerWorldY,
                          bool bypassFacing,
                          bool testObjectPlane) const;
    void applyNavigationMarker(GuardRuntimeRecord &guard,
                               const RuntimeObject &object);
    void updateFlyingBob(GuardRuntimeRecord &guard,
                         RuntimeObject &object);
    void beginState13Displacement(GuardRuntimeRecord &guard);
    void updateState13Displacement(GuardRuntimeRecord &guard,
                                   RuntimeObject &object,
                                   int32_t playerWorldX,
                                   int32_t playerWorldY);
    int firstWallIdForClass(uint8_t wallClass) const;
    void planStrategy0(GuardRuntimeRecord &guard,
                       const RuntimeObject &object,
                       int32_t playerWorldX,
                       int32_t playerWorldY,
                       int difficultyCode);
    uint32_t nextPreviewRandom();
    unsigned distanceMetric(int dxCells, int dyCells) const;
    uint8_t computeContactDamage(const RuntimeObject &object,
                                 int32_t playerWorldX,
                                 int32_t playerWorldY,
                                 int difficultyCode);
    uint8_t computeWeaponDamage(const RuntimeObject &object,
                                uint8_t weaponId,
                                int difficultyCode,
                                int viewportCenterY);
    int32_t killScore(uint8_t objectClass) const;
    void enterPainState(GuardRuntimeRecord &guard,
                        RuntimeObject &object);
    void beginDeath(GuardRuntimeRecord &guard,
                    RuntimeObject &object);
    void finalizeDeath(GuardRuntimeRecord &guard,
                       RuntimeObject &object);
    int firstObjectIdForClass(uint8_t objectClass) const;
    PlayerDamageResult attackPlayer(GuardRuntimeRecord &guard,
                                    const RuntimeObject &object,
                                    int32_t playerWorldX,
                                    int32_t playerWorldY,
                                    int difficultyCode);

    bool candidateBlocked(size_t guardIndex,
                          int32_t worldX,
                          int32_t worldY,
                          int32_t playerWorldX,
                          int32_t playerWorldY) const;
    void commitObjectPosition(size_t guardIndex,
                              int32_t oldX,
                              int32_t oldY,
                              int32_t newX,
                              int32_t newY);

    WorldState *world_;
    const MapArchive *map_;
    const ImgArchive *img_;
    ObjectRuntime *objects_;
    DoorRuntime *doors_;
    std::vector<GuardRuntimeRecord> guards_;
    uint32_t previewRng_;
    int episode_;
    bool hamersteinOverride_;
    bool cannonAttackEnabled_;
    uint32_t renderGeneration_;
};

} // namespace n3d

#endif
