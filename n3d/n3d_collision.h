#ifndef N3D_COLLISION_H
#define N3D_COLLISION_H

#include "n3d_world.h"

#include <stdint.h>

namespace n3d
{

enum CollisionAxis
{
    CollisionX,
    CollisionY
};

typedef bool (*DoorPassageQuery)(int tileX, int tileY, uint8_t wallId, void *userData);
typedef void (*ObjectTouchQuery)(int tileX, int tileY,
                                 uint8_t objectId, uint8_t objectClass,
                                 void *userData);

struct CollisionContext
{
    DoorPassageQuery doorPassage;
    ObjectTouchQuery objectTouch;
    void *userData;

    CollisionContext() : doorPassage(0), objectTouch(0), userData(0) {}
};

struct MoveResult
{
    int32_t x;
    int32_t y;
    int movedX;
    int movedY;
    bool changedCell;

    MoveResult();
};

uint8_t wallPropertiesForClass(uint8_t wallClass);
uint8_t objectPropertiesForClass(uint8_t objectClass);
int32_t worldToTile(int32_t world);

bool cellBlocksPlayer(const WorldState &world,
                      int tileX,
                      int tileY,
                      const CollisionContext &context);

bool tryPlayerAxisStep(const WorldState &world,
                       int32_t &x,
                       int32_t &y,
                       CollisionAxis axis,
                       int direction,
                       const CollisionContext &context);

// Applies integer Nitemare3D world-unit movement using axis-separated
// Bresenham-style stepping. Each component collision is independent, so a
// blocked axis can still slide along the unblocked axis.
MoveResult movePlayer(const WorldState &world,
                      int32_t x,
                      int32_t y,
                      int deltaX,
                      int deltaY,
                      const CollisionContext &context);

} // namespace n3d

#endif
