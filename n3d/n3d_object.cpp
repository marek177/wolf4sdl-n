#include "n3d_object.h"

#include "n3d_collision.h"
#include "n3d_door.h"

namespace n3d
{

RuntimeObject::RuntimeObject()
    : objectId(0), subtype(0), objectClass(0), properties(0),
      tileX(0), tileY(0), worldX(0), worldY(0), active(false)
{
}

InventoryState::InventoryState()
    : keyMask(0), idCardMask(0), pentagramMask(0), health(100),
      pistolAmmo(0), plasmaAmmo(0), wandAmmo(0),
      crystalBall(0), magicEye(0), meter(0), score(0)
{
}

ObjectRuntime::ObjectRuntime()
    : world_(0), map_(0), doors_(0)
{
}

bool ObjectRuntime::build(WorldState &world,
                          const MapArchive &map,
                          std::string &error)
{
    world_ = &world;
    map_ = &map;
    doors_ = 0;
    objects_.clear();
    inventory_ = InventoryState();

    for(int y = 0; y < WorldState::Height; ++y)
    {
        for(int x = 0; x < WorldState::Width; ++x)
        {
            const WorldCell &cell =
                world.at(static_cast<size_t>(x), static_cast<size_t>(y));

            const uint8_t properties =
                objectPropertiesForClass(cell.objectClass);

            // Original spawn scanner only creates world OBJECT records for
            // classes whose derived property byte contains bit 0x01.
            if((properties & 0x01) == 0)
                continue;

            if(objects_.size() >= 350)
            {
                error = "Nitemare3D level exceeds recovered 350 OBJECT capacity";
                objects_.clear();
                world_ = 0;
                map_ = 0;
                return false;
            }

            RuntimeObject object;
            object.objectId = cell.objectId;
            object.objectClass = cell.objectClass;
            object.subtype = subtypeFor(cell.objectId, cell.objectClass);
            object.properties = properties;
            object.tileX = x;
            object.tileY = y;
            object.worldX = x * 64 + 32;
            object.worldY = y * 64 + 32;
            object.active = true;
            objects_.push_back(object);
        }
    }

    return true;
}

uint8_t ObjectRuntime::subtypeFor(uint8_t objectId, uint8_t objectClass) const
{
    if(!map_)
        return 0;

    for(unsigned id = 0; id < 256; ++id)
    {
        if(map_->objectClass(static_cast<uint8_t>(id)) == objectClass)
            return static_cast<uint8_t>(objectId - static_cast<uint8_t>(id));
    }

    return 0;
}

RuntimeObject *ObjectRuntime::findAt(int tileX, int tileY)
{
    for(size_t i = 0; i < objects_.size(); ++i)
        if(objects_[i].active &&
           objects_[i].tileX == tileX &&
           objects_[i].tileY == tileY)
            return &objects_[i];
    return 0;
}

const RuntimeObject *ObjectRuntime::findAt(int tileX, int tileY) const
{
    for(size_t i = 0; i < objects_.size(); ++i)
        if(objects_[i].active &&
           objects_[i].tileX == tileX &&
           objects_[i].tileY == tileY)
            return &objects_[i];
    return 0;
}

bool ObjectRuntime::occupiedAt(int tileX, int tileY) const
{
    const RuntimeObject *object = findAt(tileX, tileY);
    return object != 0 && (object->properties & 0x02) != 0;
}

void ObjectRuntime::remove(RuntimeObject &object)
{
    object.active = false;
    object.properties = static_cast<uint8_t>(object.properties & ~0x01u);

    if(world_ && object.tileX >= 0 && object.tileY >= 0 &&
       object.tileX < WorldState::Width && object.tileY < WorldState::Height)
    {
        WorldCell &cell =
            world_->at(static_cast<size_t>(object.tileX),
                       static_cast<size_t>(object.tileY));
        cell.objectId = 0;
        cell.objectClass = map_ ? map_->objectClass(0) : 0;
    }
}

PickupResult ObjectRuntime::touch(int tileX, int tileY)
{
    RuntimeObject *object = findAt(tileX, tileY);
    if(!object || (object->properties & 0x04) == 0)
        return PickupNone;

    const unsigned subtype = object->subtype;

    switch(object->objectClass)
    {
        case 0x2F: // KEY
            if(subtype >= 8)
                return PickupRejected;
            inventory_.keyMask =
                static_cast<uint8_t>(inventory_.keyMask | (1u << subtype));
            if(doors_)
                doors_->grantKey(subtype);
            remove(*object);
            return PickupAccepted;

        case 0x30: // IDCARD
            if(subtype >= 8)
                return PickupRejected;
            inventory_.idCardMask =
                static_cast<uint8_t>(inventory_.idCardMask | (1u << subtype));
            if(doors_)
                doors_->grantIdCard(subtype);
            remove(*object);
            return PickupAccepted;

        // The remaining collectible classes have a statically recovered
        // dispatcher, but are intentionally left active until their complete
        // player/HUD state is installed in this Wolf4SDL port.
        case 0x31:
        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
        case 0x38:
        case 0x39:
        case 0x3A:
        case 0x3B:
        case 0x3C:
        case 0x3D:
            return PickupUnsupported;
    }

    return PickupNone;
}

} // namespace n3d
