#ifndef N3D_OBJECT_H
#define N3D_OBJECT_H

#include "n3d_data.h"
#include "n3d_world.h"

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace n3d
{

class DoorRuntime;

struct RuntimeObject
{
    uint8_t objectId;
    uint8_t subtype;
    uint8_t objectClass;
    uint8_t renderObjectId;
    uint8_t properties;
    uint8_t guardIndex;
    int tileX;
    int tileY;
    int32_t worldX;
    int32_t worldY;
    bool active;

    RuntimeObject();
};

struct InventoryState
{
    uint8_t keyMask;
    uint8_t idCardMask;
    uint8_t pentagramMask;
    uint8_t ownedWeapons;
    uint8_t auxInventory;
    uint8_t panelCharge;
    uint8_t health;
    uint8_t pistolAmmo;
    uint8_t plasmaAmmo;
    uint8_t wandAmmo;
    uint8_t crystalBall;
    uint8_t magicEye;
    uint8_t meter;
    uint8_t bonusCounter;
    uint8_t activeWeapon;
    uint8_t pendingWeapon;
    uint8_t weaponSelectionMode;
    uint8_t lastScrollSubtype;
    uint32_t score;

    InventoryState();
};

enum PickupResult
{
    PickupNone = 0,
    PickupAccepted,
    PickupRejected,
    PickupUnsupported
};

class ObjectRuntime
{
public:
    ObjectRuntime();

    bool build(WorldState &world,
               const MapArchive &map,
               std::string &error);

    void bindDoors(DoorRuntime *doors) { doors_ = doors; }

    const std::vector<RuntimeObject> &objects() const { return objects_; }
    const InventoryState &inventory() const { return inventory_; }

    RuntimeObject *findAt(int tileX, int tileY);
    const RuntimeObject *findAt(int tileX, int tileY) const;

    bool occupiedAt(int tileX, int tileY) const;
    PickupResult touch(int tileX, int tileY);
    void clampHudState();

private:
    uint8_t subtypeFor(uint8_t objectId, uint8_t objectClass) const;
    uint8_t *ammoPoolForWeapon(unsigned weapon);
    uint8_t *ammoPoolForSubtype(unsigned subtype);
    void remove(RuntimeObject &object);

    WorldState *world_;
    const MapArchive *map_;
    DoorRuntime *doors_;
    std::vector<RuntimeObject> objects_;
    InventoryState inventory_;
};

} // namespace n3d

#endif
