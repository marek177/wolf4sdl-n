#ifndef N3D_DATA_H
#define N3D_DATA_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace n3d
{

struct MapCell
{
    uint8_t wall;
    uint8_t object;

    MapCell() : wall(0), object(0) {}
};

struct LevelMap
{
    enum { Width = 64, Height = 64, CellCount = Width * Height };
    std::vector<MapCell> cells;

    LevelMap();
    const MapCell &at(size_t x, size_t y) const;
};

class MapArchive
{
public:
    enum { HeaderSize = 514, LevelBytes = 8192 };

    MapArchive();
    bool load(const std::string &path, std::string &error);

    uint16_t declaredLevelCount() const { return declaredLevelCount_; }
    const std::vector<LevelMap> &levels() const { return levels_; }
    uint8_t wallClass(uint8_t id) const { return wallClasses_[id]; }
    uint8_t objectClass(uint8_t id) const { return objectClasses_[id]; }

private:
    uint16_t declaredLevelCount_;
    uint8_t wallClasses_[256];
    uint8_t objectClasses_[256];
    std::vector<LevelMap> levels_;
};

struct ImgSequenceDef
{
    uint16_t intervalMs;
    uint8_t frameCount;
    uint8_t extended;

    ImgSequenceDef() : intervalMs(0), frameCount(0), extended(0) {}
};

struct ImgFrame
{
    uint32_t fileOffset;
    uint8_t width;
    uint8_t height;
    uint8_t metadata[8];
    std::vector<uint8_t> pixels;

    ImgFrame();
};

class ImgArchive
{
public:
    enum
    {
        ImageIndexEntries = 256,
        ImageIndexRegionEnd = 0x800,
        SequenceRecordBytes = 90,
        LowSequenceBankOffset = 0x0800,
        HighSequenceBankOffset = 0x6200,
        FrameDataOffset = 0xBC00
    };

    ImgArchive();
    bool load(const std::string &path, std::string &error);

    uint32_t reservedDword() const { return reservedDword_; }
    uint32_t firstDataOffset() const { return firstDataOffset_; }
    const std::vector<uint32_t> &wallSlotOffsets() const { return wallSlotOffsets_; }
    const std::vector<uint32_t> &objectSlotOffsets() const { return objectSlotOffsets_; }
    const std::vector<ImgSequenceDef> &wallSequences() const { return wallSequences_; }
    const std::vector<ImgSequenceDef> &objectSequences() const { return objectSequences_; }
    const std::vector<ImgFrame> &frames() const { return frames_; }

    const ImgSequenceDef *wallSequence(uint8_t id) const;
    const ImgSequenceDef *objectSequence(uint8_t id) const;

    const ImgFrame *frameAtExactOffset(uint32_t offset) const;
    const ImgFrame *wallSequenceFrame(uint8_t wallId, unsigned frameIndex) const;
    const ImgFrame *objectSequenceFrame(uint8_t objectId, unsigned frameIndex) const;

    size_t nonZeroWallSlots() const;
    size_t nonZeroObjectSlots() const;
    size_t exactWallFrameRefs() const;
    size_t exactObjectFrameRefs() const;

private:
    bool hasFrameAtOffset(uint32_t offset) const;
    const ImgFrame *sequenceFrameFromOffset(uint32_t offset,
                                            unsigned frameIndex) const;

    uint32_t reservedDword_;
    uint32_t firstDataOffset_;
    std::vector<uint32_t> wallSlotOffsets_;
    std::vector<uint32_t> objectSlotOffsets_;
    std::vector<ImgSequenceDef> wallSequences_;
    std::vector<ImgSequenceDef> objectSequences_;
    std::vector<ImgFrame> frames_;
};

struct DefinitionRecord
{
    uint16_t id;
    std::string visualCode;
    std::string imageName;
    std::string className;
    std::string description;

    DefinitionRecord() : id(0) {}
};

class DefinitionTable
{
public:
    bool load(const std::string &path, std::string &error);
    const std::vector<DefinitionRecord> &records() const { return records_; }
    const DefinitionRecord *find(uint16_t id) const;

private:
    std::vector<DefinitionRecord> records_;
};

struct EpisodeData
{
    int episode;
    MapArchive map;
    ImgArchive img;
    DefinitionTable walls;
    DefinitionTable objects;

    EpisodeData() : episode(0) {}
};

std::string joinPath(const std::string &root, const std::string &name);
bool loadEpisode(const std::string &root, int episode, EpisodeData &out, std::string &error);

} // namespace n3d

#endif
