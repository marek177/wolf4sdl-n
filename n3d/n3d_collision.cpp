#include "n3d_collision.h"

namespace n3d
{
namespace
{
enum
{
    WorldUnitsPerTile = 64,
    PlayerHalfExtent = 27,
    PlayerLeadingProbe = 28
};

int absInt(int value)
{
    return value < 0 ? -value : value;
}

int signInt(int value)
{
    return value < 0 ? -1 : (value > 0 ? 1 : 0);
}

bool probeCellBlocks(const WorldState &world,
                     int32_t worldX,
                     int32_t worldY,
                     const CollisionContext &context)
{
    return cellBlocksPlayer(world,
                            static_cast<int>(worldToTile(worldX)),
                            static_cast<int>(worldToTile(worldY)),
                            context);
}

} // namespace

MoveResult::MoveResult()
    : x(0), y(0), movedX(0), movedY(0), changedCell(false)
{
}

uint8_t wallPropertiesForClass(uint8_t type)
{
    uint8_t flags = 0;
    if(type >= 0x01 && type <= 0x30) flags |= 0x04;
    if(type >= 0x2E && type <= 0x2F) flags |= 0x10;
    if(type >= 0x31 && type <= 0x40) flags |= 0x08;
    if((flags & (0x04 | 0x08)) != 0) flags |= 0x01;
    if(type >= 0x01 && type <= 0x40) flags |= 0x02;
    if(type >= 0x47 && type <= 0x48) flags |= 0x40;
    return flags;
}

uint8_t objectPropertiesForClass(uint8_t type)
{
    uint8_t flags = 0;
    if(type >= 0x06 && type <= 0x3D) flags |= 0x01;
    if(type >= 0x08 && type <= 0x2D) flags |= 0x02;
    if(type >= 0x2F && type <= 0x3D) flags |= 0x04;
    if(type >= 0x08 && type <= 0x25) flags |= 0x08;
    if(type == 0x2A) flags |= 0x20;
    if(type == 0x04) flags |= 0x40;
    return flags;
}

int32_t worldToTile(int32_t world)
{
    if(world >= 0)
        return world / WorldUnitsPerTile;

    const int32_t quotient = world / WorldUnitsPerTile;
    const int32_t remainder = world % WorldUnitsPerTile;
    return quotient - (remainder != 0 ? 1 : 0);
}

bool cellBlocksPlayer(const WorldState &world,
                      int tileX,
                      int tileY,
                      const CollisionContext &context)
{
    if(tileX < 0 || tileY < 0 ||
       tileX >= WorldState::Width || tileY >= WorldState::Height)
        return true;

    const WorldCell &cell =
        world.at(static_cast<size_t>(tileX), static_cast<size_t>(tileY));

    const uint8_t wallFlags = wallPropertiesForClass(cell.wallClass);
    if((wallFlags & 0x04) != 0)
        return true;

    if((wallFlags & 0x08) != 0)
    {
        if(!context.doorPassage)
            return true;
        if(!context.doorPassage(tileX, tileY, cell.wallId, context.userData))
            return true;
    }

    // Object-property 0x04 is the original pickup/touch side-effect channel.
    // The preview collision core intentionally leaves the side effect to the
    // later inventory dispatcher, but movement blocking still follows bit 0x02.
    const uint8_t objectFlags = objectPropertiesForClass(cell.objectClass);
    if((objectFlags & 0x02) != 0)
        return true;

    return false;
}

bool tryPlayerAxisStep(const WorldState &world,
                       int32_t &x,
                       int32_t &y,
                       CollisionAxis axis,
                       int direction,
                       const CollisionContext &context)
{
    if(direction != -1 && direction != 1)
        return false;

    if(axis == CollisionX)
    {
        const int32_t probeX = x + direction * PlayerLeadingProbe;
        const int32_t probeY1 = y - PlayerHalfExtent;
        const int32_t probeY2 = y + PlayerHalfExtent;

        if(probeCellBlocks(world, probeX, probeY1, context) ||
           probeCellBlocks(world, probeX, probeY2, context))
            return false;

        x += direction;
        return true;
    }

    const int32_t probeY = y + direction * PlayerLeadingProbe;
    const int32_t probeX1 = x - PlayerHalfExtent;
    const int32_t probeX2 = x + PlayerHalfExtent;

    if(probeCellBlocks(world, probeX1, probeY, context) ||
       probeCellBlocks(world, probeX2, probeY, context))
        return false;

    y += direction;
    return true;
}

MoveResult movePlayer(const WorldState &world,
                      int32_t x,
                      int32_t y,
                      int deltaX,
                      int deltaY,
                      const CollisionContext &context)
{
    MoveResult out;
    out.x = x;
    out.y = y;

    const int oldTileX = static_cast<int>(worldToTile(x));
    const int oldTileY = static_cast<int>(worldToTile(y));

    const int sx = signInt(deltaX);
    const int sy = signInt(deltaY);
    const int ax = absInt(deltaX);
    const int ay = absInt(deltaY);

    if(ax == 0 && ay == 0)
        return out;

    if(ax >= ay)
    {
        int error = ax / 2;
        for(int i = 0; i < ax; ++i)
        {
            if(sx && tryPlayerAxisStep(world, out.x, out.y, CollisionX, sx, context))
                out.movedX += sx;

            error -= ay;
            if(error < 0)
            {
                if(sy && tryPlayerAxisStep(world, out.x, out.y, CollisionY, sy, context))
                    out.movedY += sy;
                error += ax;
            }
        }
    }
    else
    {
        int error = ay / 2;
        for(int i = 0; i < ay; ++i)
        {
            if(sy && tryPlayerAxisStep(world, out.x, out.y, CollisionY, sy, context))
                out.movedY += sy;

            error -= ax;
            if(error < 0)
            {
                if(sx && tryPlayerAxisStep(world, out.x, out.y, CollisionX, sx, context))
                    out.movedX += sx;
                error += ay;
            }
        }
    }

    const int newTileX = static_cast<int>(worldToTile(out.x));
    const int newTileY = static_cast<int>(worldToTile(out.y));
    out.changedCell = newTileX != oldTileX || newTileY != oldTileY;
    return out;
}

} // namespace n3d
