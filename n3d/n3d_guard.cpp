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

}

GuardRuntimeRecord::GuardRuntimeRecord()
    : renderStamp(0), timer(0), objectIndex(0), strategy(0),
      state(7), nextState(2), savedMapObjectId(0), areaId(0),
      syncFlag(0), hp(0xff), facing(0), directionCache(8),
      moveX(0), moveY(0), perceptionMode(1), losResult(0),
      proximityResult(0)
{
}

GuardRuntime::GuardRuntime()
    : world_(0), map_(0), objects_(0), doors_(0)
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
    // Recovered movement table: adjacent octants share the same cardinal
    // movement component pair.
    static const int8_t dx[8] = {0, 1, 1, 0, 0, -1, -1, 0};
    static const int8_t dy[8] = {-1, 0, 0, 1, 1, 0, 0, -1};

    const unsigned i = guard.facing & 7u;
    const int scale = guard.strategy == 2 ? 16 : 8;
    guard.moveX = static_cast<int8_t>(dx[i] * scale);
    guard.moveY = static_cast<int8_t>(dy[i] * scale);
}

bool GuardRuntime::build(WorldState &world,
                         const MapArchive &map,
                         ObjectRuntime &objects,
                         DoorRuntime *doors,
                         std::string &error)
{
    world_ = &world;
    map_ = &map;
    objects_ = &objects;
    doors_ = doors;
    guards_.clear();

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
        setMovementFromFacing(guard);

        const InitialProfile p = initialProfile(object.objectClass);
        guard.strategy = p.strategy;
        guard.state = p.state;
        guard.nextState = p.nextState;
        guard.perceptionMode = p.perceptionMode;

        // The original common tail promotes the initial state to 8 when the
        // spawn movement vector is non-zero.
        if((guard.moveX != 0 || guard.moveY != 0) &&
           object.objectClass != 0x19 &&
           object.objectClass != 0x21)
            guard.state = 8;
        else if(object.objectClass == 0x19 || object.objectClass == 0x21)
        {
            guard.moveX = 0;
            guard.moveY = 0;
        }

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

            setMovementFromFacing(guard);
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

void GuardRuntime::tickPreviewMovement(int32_t playerWorldX, int32_t playerWorldY)
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
        if(!object.active || guard.hp == 0)
            continue;

        // First integration milestone: move states that already carry a
        // recovered movement vector. Perception/attack transitions are not
        // synthesized here.
        if(guard.state != 6 && guard.state != 7 && guard.state != 8)
            continue;

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
            commitObjectPosition(i, object.worldX, object.worldY, newX, newY);
    }
}

} // namespace n3d
