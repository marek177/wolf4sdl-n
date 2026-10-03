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

struct GuardRuntimeRecord
{
    uint32_t renderStamp;
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
               ObjectRuntime &objects,
               DoorRuntime *doors,
               std::string &error);

    const std::vector<GuardRuntimeRecord> &guards() const { return guards_; }
    std::vector<GuardRuntimeRecord> &guards() { return guards_; }

    void tickPreviewMovement(int32_t playerWorldX, int32_t playerWorldY);

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
    void updateRenderFacing(RuntimeObject &object,
                            const GuardRuntimeRecord &guard);
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
    ObjectRuntime *objects_;
    DoorRuntime *doors_;
    std::vector<GuardRuntimeRecord> guards_;
};

} // namespace n3d

#endif
