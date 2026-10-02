#include "n3d_door.h"
#include "n3d_data.h"

namespace n3d
{

DoorController::DoorController()
    : x(0), y(0), wallId(0), wallClass(0),
      state(DoorClosed), timer(0), motion(0), latch(false),
      credentialSelector(0xff)
{
}

bool isDoorClass(uint8_t wallClass)
{
    return wallClass >= 0x31 && wallClass <= 0x40;
}

bool isVerticalDoorClass(uint8_t wallClass)
{
    // Recovered/editor class family is paired V/H in odd/even order:
    // 31/32, 33/34, ... 3F/40.
    return isDoorClass(wallClass) && (wallClass & 1u) != 0;
}

bool doorStateAllowsPassage(DoorState state)
{
    return state == DoorOpen || state == DoorLatchedPassable;
}

bool DoorRuntime::build(const WorldState &world, const MapArchive &map, std::string &error)
{
    world_ = &world;
    controllers_.clear();
    keyMask_ = 0;
    idCardMask_ = 0;

    for(int y = 0; y < WorldState::Height; ++y)
    {
        for(int x = 0; x < WorldState::Width; ++x)
        {
            const WorldCell &cell =
                world.at(static_cast<size_t>(x), static_cast<size_t>(y));

            if(!isDoorClass(cell.wallClass))
                continue;

            if(controllers_.size() >= 64)
            {
                error = "Nitemare3D level exceeds recovered 64-door controller capacity";
                controllers_.clear();
                world_ = 0;
                return false;
            }

            DoorController door;
            door.x = x;
            door.y = y;
            door.wallId = cell.wallId;
            door.wallClass = cell.wallClass;
            door.state = DoorClosed;
            door.timer = 0;
            door.motion = 0;
            door.latch = false;

            if((door.wallClass >= 0x33 && door.wallClass <= 0x3A))
            {
                int firstId = -1;
                for(int id = 0; id < 256; ++id)
                {
                    if(map.wallClass(static_cast<uint8_t>(id)) == door.wallClass)
                    {
                        firstId = id;
                        break;
                    }
                }

                if(firstId >= 0 && door.wallId >= firstId)
                {
                    const unsigned delta =
                        static_cast<unsigned>(door.wallId - firstId);
                    const unsigned selector = delta / 2u;
                    if(selector < 8u)
                        door.credentialSelector = static_cast<uint8_t>(selector);
                }
            }

            controllers_.push_back(door);
        }
    }

    return true;
}

DoorController *DoorRuntime::find(int x, int y)
{
    for(size_t i = 0; i < controllers_.size(); ++i)
        if(controllers_[i].x == x && controllers_[i].y == y)
            return &controllers_[i];
    return 0;
}

const DoorController *DoorRuntime::find(int x, int y) const
{
    for(size_t i = 0; i < controllers_.size(); ++i)
        if(controllers_[i].x == x && controllers_[i].y == y)
            return &controllers_[i];
    return 0;
}

bool DoorRuntime::allowsPassage(int x, int y) const
{
    const DoorController *door = find(x, y);
    return door != 0 && doorStateAllowsPassage(door->state);
}

bool DoorRuntime::isDoorCell(int x, int y) const
{
    return find(x, y) != 0;
}

DoorUseResult DoorRuntime::use(int x, int y, int playerSector)
{
    DoorController *door = find(x, y);
    if(!door)
        return DoorUseNone;

    if(door->state == DoorLatchedPassable)
        return DoorUseLatched;

    // Credential gates recovered from the USE dispatcher.
    if(door->wallClass >= 0x33 && door->wallClass <= 0x38)
    {
        if(door->credentialSelector >= 8 ||
           (keyMask_ & (1u << door->credentialSelector)) == 0)
            return DoorUseNeedsKey;
    }

    if(door->wallClass == 0x39 || door->wallClass == 0x3A)
    {
        if(door->credentialSelector >= 8 ||
           (idCardMask_ & (1u << door->credentialSelector)) == 0)
            return DoorUseNeedsIdCard;
    }

    if(door->wallClass == 0x3B || door->wallClass == 0x3C)
        return DoorUseRemoteOnly;

    if(door->wallClass == 0x3D || door->wallClass == 0x3E)
    {
        (void)playerSector;
        return DoorUseDirectionalGate;
    }

    const DoorState old = door->state;
    door->latch = true;

    if(old == DoorOpen || old == DoorOpening)
    {
        door->state = DoorClosing;
        door->latch = false;
    }
    else if(old == DoorClosed || old == DoorClosing)
    {
        door->state = DoorOpening;
    }

    if(door->state != old)
    {
        propagateState(*door);
        return DoorUseToggled;
    }

    return DoorUseNone;
}

void DoorRuntime::grantKey(unsigned selector)
{
    if(selector < 8u)
        keyMask_ = static_cast<uint8_t>(keyMask_ | (1u << selector));
}

void DoorRuntime::grantIdCard(unsigned selector)
{
    if(selector < 8u)
        idCardMask_ = static_cast<uint8_t>(idCardMask_ | (1u << selector));
}

void DoorRuntime::propagateState(DoorController &source)
{
    const int dx = isVerticalDoorClass(source.wallClass) ? 0 : 1;
    const int dy = isVerticalDoorClass(source.wallClass) ? 1 : 0;

    propagateOne(source, source.x + dx, source.y + dy);
    propagateOne(source, source.x - dx, source.y - dy);
}

void DoorRuntime::propagateOne(DoorController &source, int x, int y)
{
    DoorController *neighbor = find(x, y);
    if(!neighbor || neighbor == &source)
        return;

    neighbor->state = source.state;
    if(source.state == DoorOpening)
        neighbor->motion = source.motion;
    else if(source.state == DoorClosing)
        neighbor->motion = source.motion;
}

void DoorRuntime::tickMotion()
{
    for(size_t i = 0; i < controllers_.size(); ++i)
    {
        DoorController &door = controllers_[i];

        if(door.state == DoorOpening)
        {
            door.motion += 2;
            if(door.motion >= 64)
            {
                door.motion = 64;
                door.state = DoorOpen;
                door.timer = 32;
            }
        }
        else if(door.state == DoorClosing)
        {
            door.motion -= 2;
            if(door.motion <= 0)
            {
                door.motion = 0;
                door.state = DoorClosed;
                door.timer = 32;
            }
        }
    }
}

void DoorRuntime::tickAutoClose(int playerTileX, int playerTileY,
                                bool (*objectOccupied)(int, int, void *),
                                void *userData)
{
    for(size_t i = 0; i < controllers_.size(); ++i)
    {
        DoorController &door = controllers_[i];

        if(door.state != DoorOpen)
            continue;

        if(door.wallClass == 0x3B || door.wallClass == 0x3C)
            continue;

        --door.timer;
        if(door.timer != 0)
            continue;

        const bool occupied =
            objectOccupied && objectOccupied(door.x, door.y, userData);
        const bool playerHere =
            playerTileX == door.x && playerTileY == door.y;

        if(!occupied && !playerHere)
        {
            door.latch = false;
            door.state = DoorClosing;
            propagateState(door);
        }
        else
        {
            door.timer = 4;
        }
    }
}

} // namespace n3d
