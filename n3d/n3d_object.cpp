#include "n3d_object.h"

#include "n3d_collision.h"
#include "n3d_door.h"

namespace n3d
{

RuntimeObject::RuntimeObject()
    : objectId(0), subtype(0), objectClass(0), renderObjectId(0),
      properties(0), guardIndex(0xff),
      tileX(0), tileY(0), worldX(0), worldY(0),
      lastProjectedY(0), verticalOffset(0), active(false)
{
}

InventoryState::InventoryState()
    : keyMask(0), idCardMask(0), pentagramMask(0),
      ownedWeapons(0), auxInventory(0), panelCharge(0), health(100),
      pistolAmmo(0), plasmaAmmo(0), wandAmmo(0),
      crystalBall(0), magicEye(0), meter(0), bonusCounter(0),
      activeWeapon(0xff), pendingWeapon(0xff), weaponSelectionMode(0),
      lastScrollSubtype(0xff), damageFlash(0), gameState(0),
      deathAttackerObjectIndex(0xffff), omnipotent(false),
      deathTransitionPending(false), score(0)
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
            object.renderObjectId = cell.objectId;
            object.subtype = subtypeFor(cell.objectId, cell.objectClass);
            object.properties = properties;
            object.guardIndex = 0xff;
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

PlayerDamageResult ObjectRuntime::applyEnemyDamage(uint8_t damage,
                                                   uint16_t attackerObjectIndex)
{
    if(damage != 0)
        inventory_.damageFlash = 3;

    if(inventory_.omnipotent || inventory_.gameState == 2)
        return PlayerDamageSuppressed;

    if(damage < inventory_.health)
    {
        inventory_.health =
            static_cast<uint8_t>(inventory_.health - damage);
        return PlayerDamageNonLethal;
    }

    inventory_.health = 0;
    inventory_.gameState = 2;
    inventory_.deathTransitionPending = true;
    inventory_.deathAttackerObjectIndex = attackerObjectIndex;
    return PlayerDamageLethal;
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
    for(size_t i = 0; i < objects_.size(); ++i)
    {
        const RuntimeObject &object = objects_[i];
        if(object.active &&
           object.tileX == tileX &&
           object.tileY == tileY &&
           (object.properties & 0x02) != 0)
            return true;
    }
    return false;
}

uint8_t *ObjectRuntime::ammoPoolForWeapon(unsigned weapon)
{
    switch(weapon)
    {
        case 0:
        case 3:
            return &inventory_.plasmaAmmo;
        case 1:
            return &inventory_.wandAmmo;
        case 2:
            return &inventory_.pistolAmmo;
    }
    return 0;
}

uint8_t *ObjectRuntime::ammoPoolForSubtype(unsigned subtype)
{
    // AMMO class ordering in supplied OBJECTS data:
    // 0 = Silver bullets, 1 = Plasma power cell, 2 = Spell book.
    switch(subtype)
    {
        case 0: return &inventory_.pistolAmmo;
        case 1: return &inventory_.plasmaAmmo;
        case 2: return &inventory_.wandAmmo;
    }
    return 0;
}

void ObjectRuntime::clampHudState()
{
    if(inventory_.health > 100) inventory_.health = 100;
    if(inventory_.pistolAmmo > 100) inventory_.pistolAmmo = 100;
    if(inventory_.plasmaAmmo > 100) inventory_.plasmaAmmo = 100;
    if(inventory_.wandAmmo > 100) inventory_.wandAmmo = 100;
    if(inventory_.crystalBall > 100) inventory_.crystalBall = 100;
    if(inventory_.magicEye > 100) inventory_.magicEye = 100;
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

        case 0x31:
            inventory_.score += 200;
            remove(*object);
            return PickupAccepted;

        case 0x32:
        {
            if(inventory_.panelCharge >= 99 || subtype >= 8)
                return PickupRejected;
            const unsigned add = 1u << subtype;
            const unsigned value = static_cast<unsigned>(inventory_.panelCharge) + add;
            inventory_.panelCharge =
                static_cast<uint8_t>(value > 99u ? 99u : value);
            remove(*object);
            return PickupAccepted;
        }

        case 0x33:
        {
            if(inventory_.health >= 100 || subtype >= 8)
                return PickupRejected;
            const unsigned add = 20u >> subtype;
            inventory_.health =
                static_cast<uint8_t>(static_cast<unsigned>(inventory_.health) + add);
            remove(*object);
            return PickupAccepted;
        }

        case 0x34:
            if(inventory_.health >= 100)
                return PickupRejected;
            inventory_.health =
                static_cast<uint8_t>(static_cast<unsigned>(inventory_.health) + 30u);
            inventory_.score += 250;
            remove(*object);
            return PickupAccepted;

        case 0x35:
            inventory_.health = 100;
            inventory_.plasmaAmmo = 100;
            inventory_.score += 500;
            ++inventory_.bonusCounter;
            remove(*object);
            return PickupAccepted;

        case 0x36:
        {
            if(subtype >= 4)
                return PickupRejected;

            inventory_.ownedWeapons =
                static_cast<uint8_t>(inventory_.ownedWeapons | (1u << subtype));
            inventory_.pendingWeapon = static_cast<uint8_t>(subtype);
            inventory_.weaponSelectionMode = subtype == 2 ? 1 : 2;

            uint8_t *ammo = ammoPoolForWeapon(subtype);
            if(ammo)
                *ammo = 50;

            remove(*object);
            return PickupAccepted;
        }

        case 0x37:
            if(subtype >= 8)
                return PickupRejected;
            inventory_.auxInventory =
                static_cast<uint8_t>(inventory_.auxInventory | (1u << subtype));
            remove(*object);
            return PickupAccepted;

        case 0x38:
            if(inventory_.meter >= 100)
                return PickupRejected;
            inventory_.meter =
                static_cast<uint8_t>(static_cast<unsigned>(inventory_.meter) + 20u);
            remove(*object);
            return PickupAccepted;

        case 0x39:
        {
            uint8_t *ammo = ammoPoolForSubtype(subtype);
            if(!ammo || *ammo >= 100)
                return PickupRejected;
            *ammo = static_cast<uint8_t>(static_cast<unsigned>(*ammo) + 20u);
            remove(*object);
            return PickupAccepted;
        }

        case 0x3A:
            if(inventory_.crystalBall >= 100)
                return PickupRejected;
            inventory_.crystalBall =
                static_cast<uint8_t>(static_cast<unsigned>(inventory_.crystalBall) + 20u);
            remove(*object);
            return PickupAccepted;

        case 0x3B:
            if(inventory_.magicEye >= 100)
                return PickupRejected;
            inventory_.magicEye =
                static_cast<uint8_t>(static_cast<unsigned>(inventory_.magicEye) + 20u);
            remove(*object);
            return PickupAccepted;

        case 0x3C:
            if(subtype >= 8)
                return PickupRejected;
            inventory_.pentagramMask =
                static_cast<uint8_t>(inventory_.pentagramMask | (1u << subtype));
            remove(*object);
            return PickupAccepted;

        case 0x3D:
            // The original invokes the scroll/script helper and accepts the
            // collectible. The UI/script presentation is deferred, but the
            // persistent pickup lifecycle and subtype are preserved here.
            inventory_.lastScrollSubtype = static_cast<uint8_t>(subtype);
            remove(*object);
            return PickupAccepted;
    }

    return PickupNone;
}

} // namespace n3d
