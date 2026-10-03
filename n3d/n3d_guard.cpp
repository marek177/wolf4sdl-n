#include "n3d_guard.h"

#include "n3d_collision.h"
#include "n3d_door.h"

namespace n3d
{
namespace
{

int absInt(int value)
{
    return value < 0 ? -value : value;
}

int arithmeticShiftRight(int value, unsigned bits)
{
    if(bits == 0)
        return value;

    if(value >= 0)
        return value >> bits;

    const unsigned magnitude = static_cast<unsigned>(-value);
    const unsigned bias = (1u << bits) - 1u;
    return -static_cast<int>((magnitude + bias) >> bits);
}

uint8_t facingFromVector(int dx, int dy, uint8_t fallback)
{
    if(dx == 0 && dy < 0) return 0;
    if(dx > 0 && dy < 0) return 1;
    if(dx > 0 && dy == 0) return 2;
    if(dx > 0 && dy > 0) return 3;
    if(dx == 0 && dy > 0) return 4;
    if(dx < 0 && dy > 0) return 5;
    if(dx < 0 && dy == 0) return 6;
    if(dx < 0 && dy < 0) return 7;
    return fallback;
}

}

GuardRuntimeRecord::GuardRuntimeRecord()
    : renderStamp(0), sequenceToken(0), timer(0), objectIndex(0), strategy(0),
      state(7), nextState(2), savedMapObjectId(0), areaId(0),
      syncFlag(0), hp(0xff), facing(0), directionCache(8),
      moveX(0), moveY(0), perceptionMode(1), losResult(0),
      proximityResult(0)
{
}

GuardRuntime::GuardRuntime()
    : world_(0), map_(0), img_(0), objects_(0), doors_(0),
      previewRng_(0x4e334431UL), episode_(1), hamersteinOverride_(false),
      renderGeneration_(1)
{
}

GuardRuntime::InitialProfile GuardRuntime::initialProfile(uint8_t objectClass) const
{
    InitialProfile p;
    p.strategy = 0;
    p.state = 7;
    p.nextState = 2;
    p.perceptionMode = 1;

    switch(objectClass)
    {
        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x11:
        case 0x14:
        case 0x1A:
            p.perceptionMode = 0;
            break;

        case 0x12:
        case 0x13:
            p.strategy = 3;
            p.perceptionMode = 0;
            break;

        case 0x15:
        case 0x16:
            p.nextState = 0;
            break;

        case 0x19:
            p.strategy = 4;
            p.state = 0x0E;
            break;

        case 0x21:
            p.state = 0;
            p.nextState = 0;
            break;
    }

    return p;
}

void GuardRuntime::setMovementFromFacing(GuardRuntimeRecord &guard)
{
    // Recovered eight-way movement signs. Diagonal facings intentionally
    // carry both components; the original does not normalize diagonal speed.
    static const int8_t dx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int8_t dy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

    const unsigned i = guard.facing & 7u;
    const int scale = guard.strategy == 2 ? 16 : 8;
    guard.moveX = static_cast<int8_t>(dx[i] * scale);
    guard.moveY = static_cast<int8_t>(dy[i] * scale);
}

uint8_t GuardRuntime::computeDirectionToPlayer(
    bool wide,
    const RuntimeObject &object,
    int32_t playerWorldX,
    int32_t playerWorldY) const
{
    const int dx = static_cast<int>(playerWorldX - object.worldX);
    const int dy = static_cast<int>(playerWorldY - object.worldY);
    const int ax = absInt(dx);
    const int ay = absInt(dy);

    if(!wide)
    {
        if(dx >= 0)
        {
            if(dy < 0)
                return ay <= ax ? 1u : 0u;
            return ax < ay ? 3u : 2u;
        }

        if(dy >= 0)
            return ay <= ax ? 5u : 4u;

        return ay <= ax ? 6u : 7u;
    }

    if(ay * 2 < ax)
        return dx < 0 ? 6u : 2u;

    if(ax * 2 < ay)
        return dy > 0 ? 4u : 0u;

    if(dx > 0)
        return dy < 1 ? 1u : 3u;

    return dy > 0 ? 5u : 7u;
}

const ImgSequenceDef *GuardRuntime::objectSequence(
    const RuntimeObject &object) const
{
    if(!img_ || object.sequenceObjectId == 0xff)
        return 0;

    return img_->objectSequence(object.sequenceObjectId);
}

void GuardRuntime::setSequenceToken(GuardRuntimeRecord &guard,
                                    RuntimeObject &object,
                                    uint16_t token,
                                    uint8_t currentState,
                                    uint8_t nextState)
{
    guard.sequenceToken = token;
    object.animationFrame = static_cast<uint8_t>(token & 0xffu);

    const unsigned count = (token >> 8) & 0xffu;
    guard.timer = static_cast<uint16_t>((count - 1u) & 0xffffu);
    guard.state = currentState;
    guard.nextState = nextState;
}

bool GuardRuntime::advanceTokenFrame(GuardRuntimeRecord &guard,
                                     RuntimeObject &object,
                                     bool loop)
{
    const unsigned start =
        static_cast<unsigned>(guard.sequenceToken & 0xffu);
    const unsigned count =
        static_cast<unsigned>((guard.sequenceToken >> 8) & 0xffu);

    if(count == 0u)
        return true;

    const unsigned end = start + count - 1u;
    unsigned frame = object.animationFrame;

    if(frame < end)
    {
        ++frame;
        object.animationFrame = static_cast<uint8_t>(frame);
        return false;
    }

    if(loop)
    {
        object.animationFrame = static_cast<uint8_t>(start);
        return false;
    }

    return true;
}

uint16_t GuardRuntime::chooseAlternativeToken(
    const GuardRuntimeRecord &guard,
    const RuntimeObject &object,
    bool secondTable)
{
    const ImgSequenceDef *sequence = objectSequence(object);
    if(!sequence)
        return 0;

    if(guard.directionCache == 0 &&
       sequence->shortcutFlag(secondTable) != 0)
    {
        return sequence->alternativeToken(secondTable, 7u);
    }

    for(unsigned attempts = 0; attempts < 64u; ++attempts)
    {
        const unsigned index =
            static_cast<unsigned>(nextPreviewRandom() % 7u);
        const uint16_t token =
            sequence->alternativeToken(secondTable, index);
        if((token >> 8) != 0)
            return token;
    }

    return 0;
}

void GuardRuntime::refreshDirectionalSequence(
    GuardRuntimeRecord &guard,
    RuntimeObject &object,
    int32_t playerWorldX,
    int32_t playerWorldY,
    bool force)
{
    const ImgSequenceDef *sequence = objectSequence(object);
    if(!sequence)
    {
        updateRenderFacing(object, guard);
        return;
    }

    const bool wide = guard.syncFlag != 0;
    const uint8_t toPlayer =
        computeDirectionToPlayer(wide, object,
                                 playerWorldX, playerWorldY);

    const uint8_t relative =
        static_cast<uint8_t>(
            ((wide ? 4 : 3) +
             static_cast<int>(guard.facing) -
             static_cast<int>(toPlayer)) & 7);

    if(!force && guard.directionCache == relative)
        return;

    guard.directionCache = relative;

    unsigned table = 0;
    if(guard.state == 6 && guard.strategy != 2)
        table = 2;
    else if(guard.state == 8 ||
            guard.state == 0x10 ||
            guard.state == 0x11)
        table = 1;

    const uint16_t token =
        sequence->directionalToken(table, relative);
    if(token == 0)
    {
        updateRenderFacing(object, guard);
        return;
    }

    guard.sequenceToken = token;
    object.animationFrame =
        static_cast<uint8_t>(token & 0xffu);
}

bool GuardRuntime::build(WorldState &world,
                         const MapArchive &map,
                         const ImgArchive &img,
                         ObjectRuntime &objects,
                         DoorRuntime *doors,
                         int episode,
                         std::string &error)
{
    world_ = &world;
    map_ = &map;
    img_ = &img;
    objects_ = &objects;
    doors_ = doors;
    guards_.clear();
    previewRng_ = 0x4e334431UL;
    renderGeneration_ = 1;
    episode_ = episode;
    hamersteinOverride_ = false;

    std::vector<RuntimeObject> &runtimeObjects = objects.objects();

    for(size_t i = 0; i < runtimeObjects.size(); ++i)
    {
        RuntimeObject &object = runtimeObjects[i];
        if(!object.active || (object.properties & 0x08) == 0)
            continue;

        if(guards_.size() >= 100)
        {
            error = "Nitemare3D level exceeds recovered 100 GUARD capacity";
            guards_.clear();
            return false;
        }

        GuardRuntimeRecord guard;
        guard.objectIndex = static_cast<uint16_t>(i);

        const unsigned spawnSelector = object.subtype;
        guard.facing = static_cast<uint8_t>((spawnSelector & 3u) * 2u);

        const InitialProfile p = initialProfile(object.objectClass);
        guard.strategy = p.strategy;
        guard.state = p.state;
        guard.nextState = p.nextState;
        guard.perceptionMode = p.perceptionMode;

        // Navigation markers beneath the actor alter the initial strategy.
        if(object.tileX >= 0 && object.tileY >= 0 &&
           object.tileX < WorldState::Width &&
           object.tileY < WorldState::Height)
        {
            const WorldCell &cell =
                world.at(static_cast<size_t>(object.tileX),
                         static_cast<size_t>(object.tileY));
            if(cell.wallClass == 0x42 || cell.wallClass == 0x46)
                guard.strategy = 2;
            else if(cell.wallClass == 0x43)
                guard.strategy = 1;
        }

        setMovementFromFacing(guard);

        // Cannon and Dancers have explicit non-moving initialization states.
        if(object.objectClass == 0x19 || object.objectClass == 0x21)
        {
            guard.moveX = 0;
            guard.moveY = 0;
        }
        else if(guard.moveX != 0 || guard.moveY != 0)
        {
            // The original common tail promotes moving spawns to state 8.
            guard.state = 8;
        }

        object.guardIndex = static_cast<uint8_t>(guards_.size());
        guards_.push_back(guard);
        updateRenderFacing(object, guards_.back());
    }

    return true;
}

void GuardRuntime::updateRenderFacing(RuntimeObject &object,
                                      const GuardRuntimeRecord &guard)
{
    if(!map_)
        return;

    int firstId = -1;
    for(int id = 0; id < 256; ++id)
    {
        if(map_->objectClass(static_cast<uint8_t>(id)) == object.objectClass)
        {
            firstId = id;
            break;
        }
    }

    if(firstId < 0)
        return;

    // Map the eight runtime facings to the four cardinal N/E/S/W placement
    // sprites. Full eight-way sequence-bank animation is a later layer.
    const unsigned cardinal =
        static_cast<unsigned>(((guard.facing + 1u) >> 1) & 3u);
    const unsigned candidate =
        static_cast<unsigned>(firstId) + cardinal;

    if(candidate < 256 &&
       map_->objectClass(static_cast<uint8_t>(candidate)) == object.objectClass)
        object.renderObjectId = static_cast<uint8_t>(candidate);
}


uint32_t GuardRuntime::beginRenderGeneration()
{
    ++renderGeneration_;
    if(renderGeneration_ == 0)
        ++renderGeneration_;
    return renderGeneration_;
}

void GuardRuntime::markProjectedObject(size_t objectIndex,
                                       int projectedBaselineY,
                                       int spriteLeft,
                                       int spriteRight,
                                       int centerX)
{
    if(!objects_ || objectIndex >= objects_->objects().size())
        return;

    RuntimeObject &object = objects_->objects()[objectIndex];
    object.lastProjectedY = static_cast<int16_t>(projectedBaselineY);

    if(object.guardIndex == 0xff ||
       static_cast<size_t>(object.guardIndex) >= guards_.size())
        return;

    // Exact hitscan freshness/aim condition from the recovered projection path:
    // expanded horizontal interval must straddle the view center.
    if((spriteLeft - 4) < centerX && (spriteRight + 4) > centerX)
        guards_[object.guardIndex].renderStamp = renderGeneration_;
}

uint32_t GuardRuntime::nextPreviewRandom()
{
    // Original DOS/Win16 32-bit LCG. Arithmetic intentionally wraps mod 2^32.
    previewRng_ = previewRng_ * 0x343FDUL + 0x269EC3UL;
    return (previewRng_ >> 16) & 0x7FFFUL;
}

bool GuardRuntime::traceGridLine(int startX, int startY,
                                 int deltaX, int deltaY,
                                 int maxSteps,
                                 bool testObjectPlane) const
{
    if(!world_)
        return false;

    const int targetX = startX + deltaX;
    const int targetY = startY + deltaY;

    const int stepX = deltaX < 0 ? -1 : 1;
    const int stepY = deltaY < 0 ? -1 : 1;
    const int absX = absInt(deltaX);
    const int absY = absInt(deltaY);
    const bool yMajor = absX <= absY;

    int doubledMinor;
    int error;
    int doubledError;

    if(yMajor)
    {
        doubledMinor = absX * 2;
        error = doubledMinor - absY;
        doubledError = (absX - absY) * 2;
    }
    else
    {
        doubledMinor = absY * 2;
        error = doubledMinor - absX;
        doubledError = (absY - absX) * 2;
    }

    int x = startX;
    int y = startY;

    for(int step = 0; step < maxSteps; ++step)
    {
        if(yMajor)
            y += stepY;
        else
            x += stepX;

        if(error < 0)
            error += doubledMinor;
        else
        {
            error += doubledError;
            if(yMajor)
                x += stepX;
            else
                y += stepY;
        }

        // The original returns success as soon as the target cell is reached,
        // before testing the target cell itself for obstruction.
        if(x == targetX && y == targetY)
            return true;

        if(x < 0 || y < 0 ||
           x >= WorldState::Width || y >= WorldState::Height)
            return false;

        const WorldCell &cell =
            world_->at(static_cast<size_t>(x), static_cast<size_t>(y));

        const uint8_t wallFlags = wallPropertiesForClass(cell.wallClass);
        if((wallFlags & 0x02) != 0)
        {
            if((wallFlags & 0x04) != 0)
                return false;

            if((wallFlags & 0x08) != 0)
            {
                if(!doors_ || !doors_->allowsPassage(x, y))
                    return false;
            }
        }

        if(testObjectPlane)
        {
            const uint8_t objectFlags =
                objectPropertiesForClass(cell.objectClass);
            if((objectFlags & 0x02) != 0 &&
               (objectFlags & 0x20) == 0)
                return false;
        }
    }

    return false;
}

bool GuardRuntime::testLineOfSight(const GuardRuntimeRecord &guard,
                                   const RuntimeObject &object,
                                   int32_t playerWorldX,
                                   int32_t playerWorldY,
                                   bool bypassFacing,
                                   bool testObjectPlane) const
{
    const int guardCellX = static_cast<int>(worldToTile(object.worldX));
    const int guardCellY = static_cast<int>(worldToTile(object.worldY));
    const int playerCellX = static_cast<int>(worldToTile(playerWorldX));
    const int playerCellY = static_cast<int>(worldToTile(playerWorldY));

    const int dx = playerCellX - guardCellX;
    const int dy = playerCellY - guardCellY;
    const int absX = absInt(dx);
    const int absY = absInt(dy);

    if(absX > 8 || absY > 8)
        return false;

    if(!bypassFacing)
    {
        const uint8_t majorMask = absX < absY ? 0x99u : 0x66u;
        const uint8_t ySignMask = dy < 0 ? 0xc3u : 0x3cu;
        const uint8_t xSignMask = dx < 0 ? 0xf0u : 0x0fu;
        const unsigned facing = guard.facing & 7u;
        const uint8_t facingMask = static_cast<uint8_t>(
            (1u << ((facing + 7u) & 7u)) |
            (1u << facing) |
            (1u << ((facing + 1u) & 7u)));

        if((facingMask & majorMask & ySignMask & xSignMask) == 0)
            return false;
    }

    return traceGridLine(guardCellX, guardCellY,
                         dx, dy, 8, testObjectPlane);
}

bool GuardRuntime::updatePerception(GuardRuntimeRecord &guard,
                                    const RuntimeObject &object,
                                    int32_t playerWorldX,
                                    int32_t playerWorldY,
                                    bool bypassFacing,
                                    bool testObjectPlane) const
{
    guard.losResult =
        testLineOfSight(guard, object,
                        playerWorldX, playerWorldY,
                        bypassFacing, testObjectPlane) ? 1 : 0;

    const int dx =
        absInt(static_cast<int>(playerWorldX - object.worldX));
    const int dy =
        absInt(static_cast<int>(playerWorldY - object.worldY));
    guard.proximityResult = (dx <= 64 && dy <= 64) ? 1 : 0;

    if(guard.perceptionMode == 0)
        return guard.proximityResult != 0;
    if(guard.perceptionMode == 1 || guard.perceptionMode == 2)
        return guard.losResult != 0;

    return false;
}

int GuardRuntime::firstWallIdForClass(uint8_t wallClass) const
{
    if(!map_)
        return -1;

    for(int id = 0; id < 256; ++id)
        if(map_->wallClass(static_cast<uint8_t>(id)) == wallClass)
            return id;

    return -1;
}

void GuardRuntime::applyNavigationMarker(GuardRuntimeRecord &guard,
                                         const RuntimeObject &object)
{
    if(!world_ || !map_)
        return;

    // TURN/RETREAT markers are consumed only when the actor is centered in a
    // 64-unit map cell.
    if((object.worldX & 0x3f) != 0x20 ||
       (object.worldY & 0x3f) != 0x20)
        return;

    const int tileX = static_cast<int>(worldToTile(object.worldX));
    const int tileY = static_cast<int>(worldToTile(object.worldY));
    if(tileX < 0 || tileY < 0 ||
       tileX >= WorldState::Width || tileY >= WorldState::Height)
        return;

    const WorldCell &cell =
        world_->at(static_cast<size_t>(tileX), static_cast<size_t>(tileY));

    if(cell.wallClass == 0x41)
    {
        const int base = firstWallIdForClass(0x41);
        if(base >= 0)
        {
            const int variant = static_cast<int>(cell.wallId) - base;
            if(variant >= 0 && variant < 8)
            {
                guard.facing = static_cast<uint8_t>(variant);
                setMovementFromFacing(guard);
            }
        }
        return;
    }

    if(cell.wallClass != 0x42)
        return;

    if(guard.moveX != 0 || guard.moveY != 0)
    {
        guard.moveX = 0;
        guard.moveY = 0;
        guard.state = 3;
        guard.facing = static_cast<uint8_t>((guard.facing + 4u) & 7u);
        return;
    }

    const int base = firstWallIdForClass(0x42);
    if(base < 0)
        return;

    const int variant = static_cast<int>(cell.wallId) - base;
    if(variant == 8)
    {
        guard.state = 3;
        return;
    }

    if(variant >= 0 && variant < 8)
    {
        guard.facing = static_cast<uint8_t>(variant);
        setMovementFromFacing(guard);
    }
}

void GuardRuntime::planStrategy0(GuardRuntimeRecord &guard,
                                 const RuntimeObject &object,
                                 int32_t playerWorldX,
                                 int32_t playerWorldY,
                                 int difficultyCode)
{
    int coarseX = static_cast<int>(playerWorldX - object.worldX);
    int coarseY = static_cast<int>(playerWorldY - object.worldY);

    // Original signed divide-by-32 uses truncation toward zero.
    coarseX /= 32;
    coarseY /= 32;

    const unsigned randomChoice =
        static_cast<unsigned>(nextPreviewRandom()) &
        (guard.losResult == 0 ? 3u : 7u);

    if(randomChoice == 0)
    {
        if(coarseX == 0) guard.moveX = 8;
        if(coarseY == 0) guard.moveY = 8;
    }
    else if(randomChoice == 1)
    {
        if(coarseX == 0) guard.moveX = -8;
        if(coarseY == 0) guard.moveY = -8;
    }
    else
    {
        guard.moveX = coarseX < 0 ? -8 : (coarseX > 0 ? 8 : 0);
        guard.moveY = coarseY < 0 ? -8 : (coarseY > 0 ? 8 : 0);
    }

    if(guard.proximityResult != 0)
    {
        guard.timer = 8;
    }
    else if(guard.losResult == 0)
    {
        guard.timer = 0x18;
    }
    else
    {
        unsigned timer =
            static_cast<unsigned>(nextPreviewRandom() % 8u) + 8u;

        // Nitemare3D difficulty codes: 0 doubles, 2 halves, 1 unchanged.
        if(difficultyCode == 2)
            timer >>= 1;
        else if(difficultyCode == 0)
            timer <<= 1;

        guard.timer = static_cast<uint16_t>(timer);
    }

    guard.state = 6;
    guard.facing = facingFromVector(guard.moveX, guard.moveY, guard.facing);
}

unsigned GuardRuntime::distanceMetric(int dxCells, int dyCells) const
{
    const unsigned ax =
        static_cast<unsigned>(dxCells < 0 ? -dxCells : dxCells);
    const unsigned ay =
        static_cast<unsigned>(dyCells < 0 ? -dyCells : dyCells);
    const unsigned n = ax * ax + ay * ay;

    if(n < 2u)
        return n;

    unsigned root = 0;
    while((root + 1u) * (root + 1u) <= n)
        ++root;

    // Port of FUN_1018_324A's final rounding rule. It is deliberately not
    // std::round(sqrt(n)); e.g. sqrt(2) is promoted to 2 by the original.
    const unsigned remainder = n - root * root;
    if(root > 0u && root - 1u <= remainder)
        ++root;

    return root;
}

uint8_t GuardRuntime::computeContactDamage(const RuntimeObject &object,
                                           int32_t playerWorldX,
                                           int32_t playerWorldY,
                                           int difficultyCode)
{
    const int guardCellX = static_cast<int>(worldToTile(object.worldX));
    const int guardCellY = static_cast<int>(worldToTile(object.worldY));
    const int playerCellX = static_cast<int>(worldToTile(playerWorldX));
    const int playerCellY = static_cast<int>(worldToTile(playerWorldY));

    const unsigned distance =
        distanceMetric(guardCellX - playerCellX,
                       guardCellY - playerCellY);

    unsigned damage = distance < 1u ? 100u : 100u / distance;

    switch(object.objectClass)
    {
        case 0x08:
            damage = static_cast<unsigned>(nextPreviewRandom()) & 7u;
            break;

        case 0x09:
        case 0x0A:
            damage = static_cast<unsigned>(nextPreviewRandom()) & 15u;
            break;

        case 0x0B:
            damage >>= 2;
            break;

        case 0x0C:
        case 0x1D:
        case 0x1E:
            break;

        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
            damage = static_cast<unsigned>(nextPreviewRandom()) & 31u;
            break;

        case 0x16:
            damage = (episode_ != 3 && !hamersteinOverride_) ? 0x21u : 100u;
            break;

        case 0x19:
            damage = 100u;
            break;

        default:
            damage >>= 1;
            break;
    }

    if(difficultyCode == 2)
        damage *= 2u;
    else if(difficultyCode == 0)
        damage >>= 1;

    if(damage > 255u)
        damage = 255u;

    return static_cast<uint8_t>(damage);
}

PlayerDamageResult GuardRuntime::attackPlayer(GuardRuntimeRecord &guard,
                                              const RuntimeObject &object,
                                              int32_t playerWorldX,
                                              int32_t playerWorldY,
                                              int difficultyCode)
{
    if(!objects_)
        return PlayerDamageSuppressed;

    const uint8_t damage =
        computeContactDamage(object,
                             playerWorldX,
                             playerWorldY,
                             difficultyCode);

    const PlayerDamageResult result =
        objects_->applyEnemyDamage(damage, guard.objectIndex);

    if(result == PlayerDamageLethal)
        guard.state = 0x0B;

    return result;
}

int GuardRuntime::firstObjectIdForClass(uint8_t objectClass) const
{
    if(!map_)
        return -1;

    for(int id = 0; id < 256; ++id)
        if(map_->objectClass(static_cast<uint8_t>(id)) == objectClass)
            return id;

    return -1;
}

int32_t GuardRuntime::killScore(uint8_t objectClass) const
{
    switch(objectClass)
    {
        case 0x08:
        case 0x1A: return 25;

        case 0x09: return 75;

        case 0x0A:
        case 0x20: return 50;

        case 0x0B:
        case 0x0F:
        case 0x10:
        case 0x17:
        case 0x1B:
        case 0x1C: return 100;

        case 0x0C:
        case 0x1D:
        case 0x1E: return 250;

        case 0x0D:
        case 0x12:
        case 0x13: return 150;

        case 0x0E:
        case 0x14:
        case 0x18:
        case 0x1F: return 200;

        case 0x15: return -1000;
        case 0x16: return 1000;
    }

    return 0;
}

uint8_t GuardRuntime::computeWeaponDamage(const RuntimeObject &object,
                                          uint8_t weaponId,
                                          int difficultyCode,
                                          int viewportCenterY)
{
    int damage =
        8 * (static_cast<int>(object.lastProjectedY) - viewportCenterY) +
        static_cast<int>(nextPreviewRandom() % 25u);

    switch(object.objectClass)
    {
        case 0x0C:
        case 0x1D:
            damage = arithmeticShiftRight(damage, 3);
            break;

        case 0x0D:
            damage = arithmeticShiftRight(damage, weaponId == 1 ? 1 : 3);
            break;

        case 0x0E:
        case 0x11:
        case 0x14:
            damage = arithmeticShiftRight(damage, weaponId == 2 ? 1 : 3);
            break;

        case 0x0F:
        case 0x10:
            damage = arithmeticShiftRight(damage, weaponId == 1 ? 8 : 1);
            break;

        case 0x12:
        case 0x13:
            damage = arithmeticShiftRight(damage, 2);
            break;

        case 0x15:
            // Original also invokes a special text/event path.
            damage = 0;
            break;

        case 0x16:
            damage = episode_ == 3 ? 3 : 0;
            break;

        case 0x17:
            damage = arithmeticShiftRight(damage, weaponId == 1 ? 8 : 2);
            break;

        case 0x18:
            if(weaponId == 1)
                damage = arithmeticShiftRight(damage, 8);
            else if(weaponId == 2)
                damage = arithmeticShiftRight(damage, 4);
            else
                damage = arithmeticShiftRight(damage, 3);
            break;

        case 0x19:
            damage = 0;
            break;

        case 0x1A:
            if(weaponId == 1)
                damage = arithmeticShiftRight(damage, 1);
            else
                damage = 0;
            break;

        case 0x1B:
        case 0x1C:
            damage = arithmeticShiftRight(damage, 1);
            break;

        case 0x1E:
            if(weaponId == 1)
                damage = 0;
            else
                damage = arithmeticShiftRight(damage, 3);
            break;

        case 0x1F:
            if(weaponId == 1)
                damage = 0;
            else
                damage = arithmeticShiftRight(damage, 2);
            break;

        default:
            break;
    }

    if(difficultyCode == 2)
        damage = arithmeticShiftRight(damage, 1);
    else if(difficultyCode == 0)
        damage *= 2;

    if(damage > 255)
        damage = 255;

    // The original caller consumes the low byte. There is intentionally no
    // lower clamp here, preserving stale-cache/SAR behavior for negatives.
    return static_cast<uint8_t>(damage & 0xff);
}

void GuardRuntime::enterPainState(GuardRuntimeRecord &guard,
                                  RuntimeObject &object)
{
    guard.directionCache = 8;

    if(guard.strategy == 4)
        return;

    const uint16_t token =
        chooseAlternativeToken(guard, object, false);

    if(token == 0)
    {
        // Safe compatibility fallback for incomplete/mismatched resource sets.
        if(guard.strategy == 2)
        {
            guard.state = 0;
            guard.nextState = 8;
            guard.timer = 1;
            return;
        }

        if(guard.state == 3 || guard.state == 4 || guard.state == 0x0B)
            return;

        if(guard.state == 7 || guard.state == 8 || guard.state == 0x15)
        {
            guard.state = 0;
            guard.nextState = 5;
            guard.timer = 1;
            return;
        }

        if(guard.state != 0)
            guard.nextState = guard.state;
        guard.state = 0x15;
        guard.timer = 1;
        return;
    }

    if(guard.strategy == 2)
    {
        setSequenceToken(guard, object, token, 0, 8);
        return;
    }

    if(guard.state == 3 || guard.state == 4 || guard.state == 0x0B)
        return;

    if(guard.state == 7 || guard.state == 8 || guard.state == 0x15)
    {
        setSequenceToken(guard, object, token, 0, 5);
        return;
    }

    guard.sequenceToken = token;
    object.animationFrame =
        static_cast<uint8_t>(token & 0xffu);

    if(guard.state != 0)
        guard.nextState = guard.state;

    guard.state = 0x15;
}

void GuardRuntime::beginDeath(GuardRuntimeRecord &guard,
                              RuntimeObject &object)
{
    guard.hp = 0;
    guard.directionCache = 8;

    const uint16_t token =
        chooseAlternativeToken(guard, object, true);

    if(token != 0)
    {
        setSequenceToken(guard, object, token,
                         object.verticalOffset > 0 ? 0x12 : 0,
                         9);
    }
    else
    {
        guard.state = object.verticalOffset > 0 ? 0x12 : 0;
        guard.nextState = 9;
        guard.timer = 1;
    }

    if(objects_)
    {
        const int32_t score = killScore(object.objectClass);
        objects_->inventory().score += static_cast<uint32_t>(score);
    }

    // Restore the saved underlying map byte immediately, matching the fatal
    // hit path before state-9 finalization.
    if(world_ && map_ &&
       object.tileX >= 0 && object.tileY >= 0 &&
       object.tileX < WorldState::Width && object.tileY < WorldState::Height)
    {
        WorldCell &cell =
            world_->at(static_cast<size_t>(object.tileX),
                       static_cast<size_t>(object.tileY));
        cell.objectId = guard.savedMapObjectId;
        cell.objectClass = map_->objectClass(guard.savedMapObjectId);
    }
}

void GuardRuntime::finalizeDeath(GuardRuntimeRecord &guard,
                                 RuntimeObject &object)
{
    object.properties = static_cast<uint8_t>(object.properties | 0x01u);
    guard.state = 0x0A;

    switch(object.objectClass)
    {
        case 0x09:
        case 0x0A:
        case 0x12:
        case 0x13:
        case 0x1A:
        case 0x1E:
        case 0x1F:
            object.properties =
                static_cast<uint8_t>(object.properties & ~0x01u);
            object.active = false;
            return;

        case 0x11:
        {
            // Dracula humanoid -> Dracula-Bat.
            object.objectClass = 0x14;
            const int firstBat = firstObjectIdForClass(0x14);
            if(firstBat >= 0)
            {
                object.renderObjectId = static_cast<uint8_t>(firstBat);
                object.sequenceObjectId = static_cast<uint8_t>(firstBat);
                object.animationFrame = 0;
            }
            object.verticalOffset = 0x23;
            object.active = true;
            object.properties =
                objectPropertiesForClass(object.objectClass);

            guard.hp = 0xff;
            guard.state = 8;
            guard.nextState = 2;
            guard.timer = 1;
            guard.perceptionMode = 0;
            guard.directionCache = 8;
            setMovementFromFacing(guard);

            // Fatal handling restored the saved underlying MAP byte before
            // state 9. Dracula's transform explicitly re-links the live actor.
            if(world_ &&
               object.tileX >= 0 && object.tileY >= 0 &&
               object.tileX < WorldState::Width &&
               object.tileY < WorldState::Height)
            {
                WorldCell &cell =
                    world_->at(static_cast<size_t>(object.tileX),
                               static_cast<size_t>(object.tileY));
                cell.objectId = object.objectId;
                cell.objectClass = object.objectClass;
            }
            return;
        }

        case 0x16:
            // Hamerstein's original path resets the ordinary game state and
            // raises a separate high-level ending transition request.
            if(objects_)
            {
                objects_->inventory().gameState = 0;
                objects_->inventory().endingRequested = true;
            }
            return;
    }
}

uint8_t GuardRuntime::applyPlayerWeaponHit(size_t guardIndex,
                                           uint8_t weaponId,
                                           int difficultyCode,
                                           int viewportCenterY,
                                           bool *killed)
{
    if(killed)
        *killed = false;

    if(!objects_ || guardIndex >= guards_.size())
        return 0;

    GuardRuntimeRecord &guard = guards_[guardIndex];
    if(guard.objectIndex >= objects_->objects().size() || guard.hp == 0)
        return 0;

    RuntimeObject &object = objects_->objects()[guard.objectIndex];
    const uint8_t damage =
        computeWeaponDamage(object, weaponId,
                            difficultyCode, viewportCenterY);

    if(damage >= guard.hp)
    {
        beginDeath(guard, object);
        if(killed)
            *killed = true;
        return damage;
    }

    if(damage == 0)
        return 0;

    guard.hp = static_cast<uint8_t>(guard.hp - damage);
    enterPainState(guard, object);
    return damage;
}

PlayerHitReport GuardRuntime::fireHitscan(int32_t playerWorldX,
                                          int32_t playerWorldY,
                                          uint8_t weaponId,
                                          int difficultyCode,
                                          int viewportCenterY)
{
    PlayerHitReport report;

    if(!objects_ || weaponId != 2)
        return report;

    const int playerCellX = static_cast<int>(worldToTile(playerWorldX));
    const int playerCellY = static_cast<int>(worldToTile(playerWorldY));

    for(size_t i = 0; i < guards_.size(); ++i)
    {
        GuardRuntimeRecord &guard = guards_[i];
        if(guard.renderStamp != renderGeneration_)
            continue;

        if(guard.state == 0 || guard.state == 9 || guard.state == 0x0A)
            continue;

        if(guard.objectIndex >= objects_->objects().size())
            continue;

        RuntimeObject &object = objects_->objects()[guard.objectIndex];
        if(!object.active || guard.hp == 0)
            continue;

        const int targetCellX =
            static_cast<int>(worldToTile(object.worldX));
        const int targetCellY =
            static_cast<int>(worldToTile(object.worldY));

        if(!traceGridLine(playerCellX, playerCellY,
                          targetCellX - playerCellX,
                          targetCellY - playerCellY,
                          16, true))
            continue;

        bool killed = false;
        applyPlayerWeaponHit(i, weaponId, difficultyCode,
                             viewportCenterY, &killed);

        ++report.hitCount;
        if(killed)
        {
            ++report.killCount;
            report.scoreDelta += killScore(object.objectClass);
        }
    }

    return report;
}

bool GuardRuntime::candidateBlocked(size_t guardIndex,
                                    int32_t worldX,
                                    int32_t worldY,
                                    int32_t playerWorldX,
                                    int32_t playerWorldY) const
{
    if(!world_ || !objects_)
        return true;

    // Recovered GUARD/player exclusion box is approximately 42 world units
    // per axis in the candidate-position helper.
    if(absInt(static_cast<int>(worldX - playerWorldX)) <= 42 &&
       absInt(static_cast<int>(worldY - playerWorldY)) <= 42)
        return true;

    const int tileX = static_cast<int>(worldToTile(worldX));
    const int tileY = static_cast<int>(worldToTile(worldY));

    if(tileX < 0 || tileY < 0 ||
       tileX >= WorldState::Width || tileY >= WorldState::Height)
        return true;

    const WorldCell &cell =
        world_->at(static_cast<size_t>(tileX), static_cast<size_t>(tileY));

    const uint8_t wallFlags = wallPropertiesForClass(cell.wallClass);
    if((wallFlags & 0x04) != 0)
        return true;

    if((wallFlags & 0x08) != 0)
    {
        if(!doors_ || !doors_->allowsPassage(tileX, tileY))
            return true;
    }

    const std::vector<RuntimeObject> &runtimeObjects = objects_->objects();
    for(size_t i = 0; i < runtimeObjects.size(); ++i)
    {
        if(i == guards_[guardIndex].objectIndex)
            continue;

        const RuntimeObject &other = runtimeObjects[i];
        if(other.active &&
           other.tileX == tileX &&
           other.tileY == tileY &&
           (other.properties & 0x02) != 0)
            return true;
    }

    return false;
}

void GuardRuntime::commitObjectPosition(size_t guardIndex,
                                        int32_t oldX,
                                        int32_t oldY,
                                        int32_t newX,
                                        int32_t newY)
{
    if(!world_ || !map_ || !objects_)
        return;

    GuardRuntimeRecord &guard = guards_[guardIndex];
    std::vector<RuntimeObject> &runtimeObjects = objects_->objects();
    RuntimeObject &object = runtimeObjects[guard.objectIndex];

    const int oldTileX = static_cast<int>(worldToTile(oldX));
    const int oldTileY = static_cast<int>(worldToTile(oldY));
    const int newTileX = static_cast<int>(worldToTile(newX));
    const int newTileY = static_cast<int>(worldToTile(newY));

    if(oldTileX != newTileX || oldTileY != newTileY)
    {
        if(oldTileX >= 0 && oldTileY >= 0 &&
           oldTileX < WorldState::Width && oldTileY < WorldState::Height)
        {
            WorldCell &oldCell =
                world_->at(static_cast<size_t>(oldTileX),
                           static_cast<size_t>(oldTileY));
            oldCell.objectId = guard.savedMapObjectId;
            oldCell.objectClass = map_->objectClass(guard.savedMapObjectId);
        }

        if(newTileX >= 0 && newTileY >= 0 &&
           newTileX < WorldState::Width && newTileY < WorldState::Height)
        {
            WorldCell &newCell =
                world_->at(static_cast<size_t>(newTileX),
                           static_cast<size_t>(newTileY));
            guard.savedMapObjectId = newCell.objectId;
            newCell.objectId = object.objectId;
            newCell.objectClass = object.objectClass;
        }
    }

    object.worldX = newX;
    object.worldY = newY;
    object.tileX = newTileX;
    object.tileY = newTileY;
    updateRenderFacing(object, guard);
}

void GuardRuntime::tickPreviewAI(int32_t playerWorldX,
                                 int32_t playerWorldY,
                                 int difficultyCode)
{
    if(!objects_)
        return;

    std::vector<RuntimeObject> &runtimeObjects = objects_->objects();

    for(size_t i = 0; i < guards_.size(); ++i)
    {
        GuardRuntimeRecord &guard = guards_[i];
        if(guard.objectIndex >= runtimeObjects.size())
            continue;

        RuntimeObject &object = runtimeObjects[guard.objectIndex];
        if(!object.active)
            continue;

        bool shouldMove = false;
        bool state8Reacquire = false;

        switch(guard.state)
        {
            case 0:
                advanceTokenFrame(guard, object, true);
                if(guard.timer > 0)
                    --guard.timer;
                if(guard.timer == 0)
                    guard.state = guard.nextState;
                break;

            case 1:
                if(guard.timer > 0)
                    --guard.timer;
                if(guard.timer == 0)
                    guard.state = 2;
                break;

            case 2:
            {
                const ImgSequenceDef *sequence = objectSequence(object);
                const uint16_t token =
                    sequence ? sequence->stateToken(0) : 0;
                if(token != 0)
                    setSequenceToken(guard, object, token, 0, 3);
                else
                    guard.state = 3;
                break;
            }

            case 3:
                if(updatePerception(guard, object,
                                    playerWorldX, playerWorldY,
                                    false, true))
                {
                    const ImgSequenceDef *sequence =
                        objectSequence(object);
                    const uint16_t token =
                        sequence ? sequence->stateToken(1) : 0;
                    if(token != 0)
                        setSequenceToken(guard, object,
                                         token, 0, 4);
                    else
                        guard.state = 4;
                }
                else if(guard.strategy == 0 ||
                        (guard.strategy == 1 && guard.hp >= 0x7f))
                {
                    // The recovered dispatcher jumps directly into the
                    // fallback planner on state-3 perception failure.
                    planStrategy0(guard, object,
                                  playerWorldX, playerWorldY,
                                  difficultyCode);
                    shouldMove = true;
                }
                else
                {
                    guard.state = 5;
                }
                break;

            case 4:
            {
                const bool canAttack =
                    updatePerception(guard, object,
                                     playerWorldX, playerWorldY,
                                     false, true);

                if(canAttack)
                    attackPlayer(guard, object,
                                 playerWorldX, playerWorldY,
                                 difficultyCode);

                // Original dispatcher checks player game state after the
                // optional damage call. State 2 returns immediately, leaving
                // a non-killing guard in state 4 and the killing guard in 0x0B.
                if(objects_->inventory().gameState == 2)
                    break;

                const ImgSequenceDef *sequence =
                    objectSequence(object);
                const uint16_t token =
                    sequence ? sequence->stateToken(2) : 0;
                if(token != 0)
                    setSequenceToken(guard, object,
                                     token, 0, 5);
                else
                    guard.state = 5;
                break;
            }

            case 5:
                if(guard.strategy == 0 ||
                   (guard.strategy == 1 && guard.hp >= 0x7f))
                {
                    planStrategy0(guard, object,
                                  playerWorldX, playerWorldY,
                                  difficultyCode);
                    shouldMove = true; // original planner moves immediately
                }
                break;

            case 6:
                refreshDirectionalSequence(guard, object,
                                           playerWorldX, playerWorldY,
                                           false);
                shouldMove = true;
                if(guard.timer > 0)
                    --guard.timer;
                if(guard.timer == 0)
                    guard.state = 3;
                break;

            case 7:
                refreshDirectionalSequence(guard, object,
                                           playerWorldX, playerWorldY,
                                           false);
                // Reacquire state: no ordinary movement in the recovered
                // dispatcher. Strategy-3 state-13 branch is deferred.
                if(guard.strategy != 3 &&
                   updatePerception(guard, object,
                                    playerWorldX, playerWorldY,
                                    false, true))
                    guard.state = 2;
                break;

            case 8:
                // Original ordering: centered TURN/RETREAT marker first,
                // movement second, perception/reacquire afterwards. The
                // handler continues this tail even if the marker changed
                // the current state byte.
                state8Reacquire = true;
                applyNavigationMarker(guard, object);
                shouldMove = true;
                break;

            case 9:
                finalizeDeath(guard, object);
                break;

            case 0x12:
                advanceTokenFrame(guard, object, false);

                if(object.verticalOffset >= 5)
                    object.verticalOffset =
                        static_cast<uint8_t>(object.verticalOffset - 5);
                else
                    object.verticalOffset = 0;

                if(guard.timer > 0)
                    --guard.timer;

                if(guard.timer == 0 && object.verticalOffset == 0)
                    guard.state = guard.nextState;
                break;

            case 0x15:
                if(advanceTokenFrame(guard, object, false))
                    guard.state = guard.nextState;
                break;

            default:
                break;
        }

        if(shouldMove)
        {
            int32_t newX = object.worldX;
            int32_t newY = object.worldY;

            if(guard.moveX != 0)
            {
                const int32_t candidateX = newX + guard.moveX;
                if(!candidateBlocked(i, candidateX, newY,
                                     playerWorldX, playerWorldY))
                    newX = candidateX;
            }

            if(guard.moveY != 0)
            {
                const int32_t candidateY = newY + guard.moveY;
                if(!candidateBlocked(i, newX, candidateY,
                                     playerWorldX, playerWorldY))
                    newY = candidateY;
            }

            if(newX != object.worldX || newY != object.worldY)
            {
                const int actualDx = static_cast<int>(newX - object.worldX);
                const int actualDy = static_cast<int>(newY - object.worldY);
                guard.facing =
                    facingFromVector(actualDx, actualDy, guard.facing);
                commitObjectPosition(i, object.worldX, object.worldY,
                                     newX, newY);
                advanceTokenFrame(guard, object, true);
            }
        }

        if(state8Reacquire)
        {
            refreshDirectionalSequence(guard, object,
                                       playerWorldX, playerWorldY,
                                       false);

            if(guard.nextState == 2 &&
               updatePerception(guard, object,
                                playerWorldX, playerWorldY,
                                false, true))
                guard.state = 2;
        }

        updateRenderFacing(object, guard);
    }
}

} // namespace n3d
