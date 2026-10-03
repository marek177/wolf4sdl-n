#include "n3d/n3d_collision.h"
#include "n3d/n3d_data.h"
#include "n3d/n3d_guard.h"
#include "n3d/n3d_object.h"
#include "n3d/n3d_world.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{

bool require(bool condition, const char *message)
{
    if(condition)
        return true;
    std::cerr << "FAIL: " << message << "\n";
    return false;
}

void writeU16(std::vector<unsigned char> &bytes,
              size_t offset,
              unsigned value)
{
    bytes[offset] = static_cast<unsigned char>(value & 0xffu);
    bytes[offset + 1] =
        static_cast<unsigned char>((value >> 8) & 0xffu);
}

void writeU32(std::vector<unsigned char> &bytes,
              size_t offset,
              unsigned long value)
{
    bytes[offset] = static_cast<unsigned char>(value & 0xffUL);
    bytes[offset + 1] =
        static_cast<unsigned char>((value >> 8) & 0xffUL);
    bytes[offset + 2] =
        static_cast<unsigned char>((value >> 16) & 0xffUL);
    bytes[offset + 3] =
        static_cast<unsigned char>((value >> 24) & 0xffUL);
}

void appendFrame(std::vector<unsigned char> &bytes,
                 unsigned width,
                 unsigned height,
                 unsigned char color)
{
    bytes.push_back(static_cast<unsigned char>(width));
    bytes.push_back(static_cast<unsigned char>(height));
    for(int i = 0; i < 8; ++i)
        bytes.push_back(0);
    for(unsigned i = 0; i < width * height; ++i)
        bytes.push_back(color);
}

void addObjectSequence(std::vector<unsigned char> &bytes,
                       unsigned char id,
                       unsigned short interval,
                       unsigned char frameCount,
                       unsigned char extended,
                       unsigned width,
                       unsigned height)
{
    const unsigned long stream =
        static_cast<unsigned long>(bytes.size());
    writeU32(bytes, 0x400u + static_cast<unsigned>(id) * 4u,
             stream);

    const size_t seq =
        static_cast<size_t>(n3d::ImgArchive::HighSequenceBankOffset) +
        static_cast<size_t>(id) *
        static_cast<size_t>(n3d::ImgArchive::SequenceRecordBytes);

    writeU16(bytes, seq, interval);
    bytes[seq + 2] = frameCount;
    bytes[seq + 3] = extended;

    for(unsigned frame = 0; frame < frameCount; ++frame)
        appendFrame(bytes, width, height,
                    static_cast<unsigned char>(0x20u + frame));
}

bool writeSyntheticImg(const char *path)
{
    std::vector<unsigned char> bytes(n3d::ImgArchive::FrameDataOffset, 0);

    addObjectSequence(bytes, 0x50, 100, 4, 0, 1, 8);
    addObjectSequence(bytes, 0x51, 100, 8, 1, 1, 8);
    addObjectSequence(bytes, 0x60, 50, 8, 0, 1, 8);
    addObjectSequence(bytes, 0x61, 50, 16, 0, 1, 8);
    addObjectSequence(bytes, 0x70, 0, 1, 0, 1, 16);
    addObjectSequence(bytes, 0x71, 0, 1, 0, 1, 32);

    // Extended alternative table for object 0x51:
    // selector 0 = frame 0 length 1; original RNG's first &7 result = 4,
    // selector 4 = frame 5 length 2.
    const size_t seq51 =
        static_cast<size_t>(n3d::ImgArchive::HighSequenceBankOffset) +
        static_cast<size_t>(0x51) *
        static_cast<size_t>(n3d::ImgArchive::SequenceRecordBytes);
    writeU16(bytes, seq51 + 0x3A + 0 * 2, 0u | (1u << 8));
    writeU16(bytes, seq51 + 0x3A + 4 * 2, 5u | (2u << 8));

    std::ofstream f(path, std::ios::binary);
    if(!f)
        return false;
    f.write(reinterpret_cast<const char *>(&bytes[0]),
            static_cast<std::streamsize>(bytes.size()));
    return !!f;
}

bool writeSyntheticMap(const char *path)
{
    const size_t size =
        n3d::MapArchive::HeaderSize + n3d::MapArchive::LevelBytes;
    std::vector<unsigned char> bytes(size, 0);

    bytes[0] = 1;

    for(int id = 1; id <= 4; ++id)
        bytes[0x102 + id] = 0x02;

    bytes[0x102 + 0x50] = 0x2B;
    bytes[0x102 + 0x51] = 0x2B;
    bytes[0x102 + 0x60] = 0x2C;
    bytes[0x102 + 0x61] = 0x2D;
    bytes[0x102 + 0x70] = 0x2E;
    bytes[0x102 + 0x71] = 0x3B;

    const size_t base = n3d::MapArchive::HeaderSize;

    size_t index = static_cast<size_t>(1 * 64 + 1);
    bytes[base + index * 2 + 1] = 2;

    const unsigned char ids[6] =
        {0x50, 0x51, 0x60, 0x61, 0x70, 0x71};
    for(int i = 0; i < 6; ++i)
    {
        index = static_cast<size_t>((3 + i) * 64 + 3);
        bytes[base + index * 2 + 1] = ids[i];
    }

    std::ofstream f(path, std::ios::binary);
    if(!f)
        return false;
    f.write(reinterpret_cast<const char *>(&bytes[0]),
            static_cast<std::streamsize>(bytes.size()));
    return !!f;
}

n3d::RuntimeObject *findById(n3d::ObjectRuntime &objects,
                             uint8_t id,
                             size_t *indexOut)
{
    for(size_t i = 0; i < objects.objects().size(); ++i)
    {
        if(objects.objects()[i].objectId == id)
        {
            if(indexOut)
                *indexOut = i;
            return &objects.objects()[i];
        }
    }
    return 0;
}

}

int main()
{
    const char *mapPath = "n3d_object_animation_test.map";
    const char *imgPath = "n3d_object_animation_test.img";

    if(!writeSyntheticMap(mapPath) ||
       !writeSyntheticImg(imgPath))
        return 1;

    n3d::EpisodeData episode;
    std::string error;

    if(!episode.map.load(mapPath, error) ||
       !episode.img.load(imgPath, error))
    {
        std::cerr << error << "\n";
        return 1;
    }

    episode.episode = 1;

    n3d::WorldState world;
    if(!n3d::buildWorld(episode, 0, world, error))
    {
        std::cerr << error << "\n";
        return 1;
    }

    n3d::ObjectRuntime objects;
    if(!objects.build(world, episode.map, error))
    {
        std::cerr << error << "\n";
        return 1;
    }

    n3d::GuardRuntime sharedRng;
    objects.bindAnimation(&episode.img, &sharedRng);

    size_t loopIndex = 0;
    n3d::RuntimeObject *loop =
        findById(objects, 0x50, &loopIndex);
    if(!require(loop != 0, "loop object exists")) return 1;

    if(!require(objects.advanceAnimationForRender(
                    loopIndex, 100, 0, 0),
                "due generic animation advances")) return 1;
    if(!require(loop->animationFrame == 1 &&
                loop->animationDeadlineMs == 200,
                "generic animation advances one frame and schedules deadline")) return 1;

    if(!require(!objects.advanceAnimationForRender(
                    loopIndex, 150, 0, 0),
                "animation waits before deadline")) return 1;
    if(!require(loop->animationFrame == 1,
                "frame unchanged before deadline")) return 1;

    if(!require(objects.advanceAnimationForRender(
                    loopIndex, 450, 0, 0),
                "late update advances only once")) return 1;
    if(!require(loop->animationFrame == 2 &&
                loop->animationDeadlineMs == 550,
                "late update has no catch-up loop")) return 1;

    objects.advanceAnimationForRender(loopIndex, 550, 0, 0);
    objects.advanceAnimationForRender(loopIndex, 650, 0, 0);
    if(!require(loop->animationFrame == 0,
                "generic animation wraps at frame count")) return 1;

    size_t altIndex = 0;
    n3d::RuntimeObject *alt =
        findById(objects, 0x51, &altIndex);
    if(!require(alt != 0, "alternative object exists")) return 1;

    if(!require(objects.advanceAnimationForRender(
                    altIndex, 100, 0, 0),
                "extended alternative animation advances")) return 1;
    if(!require(alt->animationAlternative == 4,
                "extended animation consumes shared original RNG selector 4")) return 1;
    if(!require(alt->animationFrame == 5,
                "extended animation jumps to selected branch start")) return 1;

    size_t dir4Index = 0;
    n3d::RuntimeObject *dir4 =
        findById(objects, 0x60, &dir4Index);
    if(!require(dir4 != 0, "class 2C directional object exists")) return 1;

    const int32_t eastX = dir4->worldX + 128;
    const int32_t sameY = dir4->worldY;
    objects.advanceAnimationForRender(
        dir4Index, 100, eastX, sameY);
    if(!require(dir4->animationFrame == 3,
                "class 2C uses four direction groups with phase divisor")) return 1;

    size_t dir8Index = 0;
    n3d::RuntimeObject *dir8 =
        findById(objects, 0x61, &dir8Index);
    if(!require(dir8 != 0, "class 2D directional object exists")) return 1;

    objects.advanceAnimationForRender(
        dir8Index, 100, dir8->worldX + 128, dir8->worldY);
    if(!require(dir8->animationFrame == 5,
                "class 2D retains eight direction groups")) return 1;

    size_t elevatedIndex = 0;
    n3d::RuntimeObject *elevated =
        findById(objects, 0x70, &elevatedIndex);
    if(!require(elevated != 0, "elevated object exists")) return 1;
    objects.updateVerticalAnchorFromFrame(elevatedIndex, 16);
    if(!require(elevated->verticalOffset == 48,
                "class 2E vertical anchor is 64-frameHeight")) return 1;

    size_t eyeIndex = 0;
    n3d::RuntimeObject *eye =
        findById(objects, 0x71, &eyeIndex);
    if(!require(eye != 0, "Magic Eye object exists")) return 1;
    objects.updateVerticalAnchorFromFrame(eyeIndex, 32);
    if(!require(eye->verticalOffset == 16,
                "Magic Eye vertical anchor is half remaining height")) return 1;

    std::remove(mapPath);
    std::remove(imgPath);
    std::cout << "N3D object animation tests passed\n";
    return 0;
}
