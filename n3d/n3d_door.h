#ifndef N3D_DOOR_H
#define N3D_DOOR_H

#include "n3d_world.h"

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace n3d
{

class MapArchive;

enum DoorState
{
    DoorOpen = 0,
    DoorClosed = 1,
    DoorOpening = 2,
    DoorClosing = 3,
    DoorLatchedPassable = 4
};

enum DoorUseResult
{
    DoorUseNone = 0,
    DoorUseToggled,
    DoorUseNeedsKey,
    DoorUseNeedsIdCard,
    DoorUseRemoteOnly,
    DoorUseDirectionalGate,
    DoorUseLatched
};

struct DoorController
{
    int x;
    int y;
    uint8_t wallId;
    uint8_t wallClass;
    DoorState state;
    int timer;
    int motion; // 0..64 world units of opening progress.
    bool latch;
    uint8_t credentialSelector;

    DoorController();
};

class DoorRuntime
{
public:
    DoorRuntime() : world_(0), keyMask_(0), idCardMask_(0) {}
    bool build(const WorldState &world, const MapArchive &map, std::string &error);

    const std::vector<DoorController> &controllers() const { return controllers_; }
    DoorController *find(int x, int y);
    const DoorController *find(int x, int y) const;

    bool allowsPassage(int x, int y) const;
    bool isDoorCell(int x, int y) const;

    DoorUseResult use(int x, int y, int playerSector);

    void setKeyMask(uint8_t mask) { keyMask_ = mask; }
    void setIdCardMask(uint8_t mask) { idCardMask_ = mask; }
    uint8_t keyMask() const { return keyMask_; }
    uint8_t idCardMask() const { return idCardMask_; }
    void grantKey(unsigned selector);
    void grantIdCard(unsigned selector);

    // One original-style geometry/motion update. States 2/3 advance by
    // exactly two Nitemare3D world units.
    void tickMotion();

    // One original-style slow auto-close update. Open non-remote doors count
    // down from 32; occupied door cells retry after 4 slow updates.
    void tickAutoClose(int playerTileX, int playerTileY,
                       bool (*objectOccupied)(int, int, void *),
                       void *userData);

private:
    void propagateState(DoorController &source);
    void propagateOne(DoorController &source, int x, int y);

    const WorldState *world_;
    std::vector<DoorController> controllers_;
    uint8_t keyMask_;
    uint8_t idCardMask_;
};

bool isDoorClass(uint8_t wallClass);
bool isVerticalDoorClass(uint8_t wallClass);
bool doorStateAllowsPassage(DoorState state);

} // namespace n3d

#endif
