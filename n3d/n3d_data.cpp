#include "n3d_data.h"

#include <fstream>
#include <sstream>

namespace n3d
{
namespace
{

typedef std::vector<uint8_t> ByteVector;

bool readFile(const std::string &path, ByteVector &bytes, std::string &error)
{
    std::ifstream f(path.c_str(), std::ios::binary | std::ios::ate);
    if(!f)
    {
        error = "Unable to open file: " + path;
        return false;
    }

    const std::streamoff end = f.tellg();
    if(end < 0)
    {
        error = "Unable to determine file size: " + path;
        return false;
    }

    bytes.resize(static_cast<size_t>(end));
    f.seekg(0, std::ios::beg);
    if(!bytes.empty())
        f.read(reinterpret_cast<char *>(&bytes[0]), static_cast<std::streamsize>(bytes.size()));

    if(!f && !bytes.empty())
    {
        error = "Failed while reading file: " + path;
        return false;
    }
    return true;
}

bool requireRange(const ByteVector &bytes, size_t offset, size_t length)
{
    return offset <= bytes.size() && length <= bytes.size() - offset;
}

uint16_t readU16LE(const ByteVector &bytes, size_t offset)
{
    return static_cast<uint16_t>(bytes[offset]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

uint32_t readU32LE(const ByteVector &bytes, size_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

std::string numberString(size_t value)
{
    std::ostringstream ss;
    ss << value;
    return ss.str();
}

} // namespace

LevelMap::LevelMap() : cells(CellCount)
{
}

const MapCell &LevelMap::at(size_t x, size_t y) const
{
    return cells[y * Width + x];
}

MapArchive::MapArchive() : declaredLevelCount_(0)
{
    for(size_t i = 0; i < 256; ++i)
    {
        wallClasses_[i] = 0;
        objectClasses_[i] = 0;
    }
}

bool MapArchive::load(const std::string &path, std::string &error)
{
    ByteVector bytes;
    levels_.clear();
    declaredLevelCount_ = 0;
    for(size_t i = 0; i < 256; ++i)
    {
        wallClasses_[i] = 0;
        objectClasses_[i] = 0;
    }

    if(!readFile(path, bytes, error))
        return false;
    if(bytes.size() < HeaderSize)
    {
        error = "MAP file smaller than 514-byte header: " + path;
        return false;
    }

    const size_t payloadSize = bytes.size() - HeaderSize;
    if(payloadSize % LevelBytes != 0)
    {
        error = "MAP payload is not an integer number of 8192-byte levels: " + path;
        return false;
    }

    declaredLevelCount_ = readU16LE(bytes, 0);
    for(size_t i = 0; i < 256; ++i)
    {
        wallClasses_[i] = bytes[0x002 + i];
        objectClasses_[i] = bytes[0x102 + i];
    }

    const size_t actualLevelCount = payloadSize / LevelBytes;
    if(declaredLevelCount_ != actualLevelCount)
    {
        error = "MAP header level count does not match file length: " + path;
        return false;
    }

    levels_.resize(actualLevelCount);
    for(size_t level = 0; level < actualLevelCount; ++level)
    {
        const size_t base = HeaderSize + level * LevelBytes;
        for(size_t i = 0; i < LevelMap::CellCount; ++i)
        {
            levels_[level].cells[i].wall = bytes[base + i * 2];
            levels_[level].cells[i].object = bytes[base + i * 2 + 1];
        }
    }
    return true;
}

ImgFrame::ImgFrame() : fileOffset(0), width(0), height(0)
{
    for(size_t i = 0; i < sizeof(metadata); ++i)
        metadata[i] = 0;
}

ImgArchive::ImgArchive() : reservedDword_(0), firstDataOffset_(0)
{
}

bool ImgArchive::load(const std::string &path, std::string &error)
{
    ByteVector bytes;
    wallSlotOffsets_.clear();
    objectSlotOffsets_.clear();
    frames_.clear();
    reservedDword_ = 0;
    firstDataOffset_ = 0;

    if(!readFile(path, bytes, error))
        return false;
    if(bytes.size() < ImageIndexRegionEnd)
    {
        error = "IMG file is too small for both 256-entry image directories: " + path;
        return false;
    }

    reservedDword_ = readU32LE(bytes, 0);
    firstDataOffset_ = readU32LE(bytes, 4);
    if(firstDataOffset_ < ImageIndexRegionEnd || firstDataOffset_ > bytes.size())
    {
        error = "IMG first image offset is invalid: " + path;
        return false;
    }

    wallSlotOffsets_.reserve(ImageIndexEntries);
    objectSlotOffsets_.reserve(ImageIndexEntries);
    for(size_t i = 0; i < ImageIndexEntries; ++i)
    {
        const uint32_t wallOffset = readU32LE(bytes, i * 4);
        const uint32_t objectOffset = readU32LE(bytes, 0x400 + i * 4);
        if(wallOffset != 0 && (wallOffset < firstDataOffset_ || wallOffset >= bytes.size()))
        {
            error = "IMG wall directory entry points outside image data: " + path;
            return false;
        }
        if(objectOffset != 0 && (objectOffset < firstDataOffset_ || objectOffset >= bytes.size()))
        {
            error = "IMG object directory entry points outside image data: " + path;
            return false;
        }
        wallSlotOffsets_.push_back(wallOffset);
        objectSlotOffsets_.push_back(objectOffset);
    }

    size_t pos = static_cast<size_t>(firstDataOffset_);
    while(pos < bytes.size())
    {
        if(!requireRange(bytes, pos, 10))
        {
            error = "Truncated IMG frame header: " + path;
            return false;
        }

        ImgFrame frame;
        frame.fileOffset = static_cast<uint32_t>(pos);
        frame.width = bytes[pos];
        frame.height = bytes[pos + 1];
        for(size_t i = 0; i < 8; ++i)
            frame.metadata[i] = bytes[pos + 2 + i];

        const size_t pixelCount = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
        if(!requireRange(bytes, pos + 10, pixelCount))
        {
            error = "IMG frame extends past EOF: " + path;
            return false;
        }

        frame.pixels.resize(pixelCount);
        for(size_t i = 0; i < pixelCount; ++i)
            frame.pixels[i] = bytes[pos + 10 + i];
        frames_.push_back(frame);
        pos += 10 + pixelCount;
    }

    return true;
}

const ImgFrame *ImgArchive::frameAtExactOffset(uint32_t offset) const
{
    for(size_t i = 0; i < frames_.size(); ++i)
        if(frames_[i].fileOffset == offset)
            return &frames_[i];
    return 0;
}

bool ImgArchive::hasFrameAtOffset(uint32_t offset) const
{
    return frameAtExactOffset(offset) != 0;
}

size_t ImgArchive::nonZeroWallSlots() const
{
    size_t count = 0;
    for(size_t i = 0; i < wallSlotOffsets_.size(); ++i)
        if(wallSlotOffsets_[i] != 0)
            ++count;
    return count;
}

size_t ImgArchive::nonZeroObjectSlots() const
{
    size_t count = 0;
    for(size_t i = 0; i < objectSlotOffsets_.size(); ++i)
        if(objectSlotOffsets_[i] != 0)
            ++count;
    return count;
}

size_t ImgArchive::exactWallFrameRefs() const
{
    size_t count = 0;
    for(size_t i = 0; i < wallSlotOffsets_.size(); ++i)
        if(wallSlotOffsets_[i] != 0 && hasFrameAtOffset(wallSlotOffsets_[i]))
            ++count;
    return count;
}

size_t ImgArchive::exactObjectFrameRefs() const
{
    size_t count = 0;
    for(size_t i = 0; i < objectSlotOffsets_.size(); ++i)
        if(objectSlotOffsets_[i] != 0 && hasFrameAtOffset(objectSlotOffsets_[i]))
            ++count;
    return count;
}

bool DefinitionTable::load(const std::string &path, std::string &error)
{
    std::ifstream f(path.c_str());
    if(!f)
    {
        error = "Unable to open definitions: " + path;
        return false;
    }

    records_.clear();
    std::string line;
    size_t lineNo = 0;
    while(std::getline(f, line))
    {
        ++lineNo;
        if(!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        if(line.empty())
            continue;

        std::istringstream ss(line);
        std::string idHex;
        DefinitionRecord rec;
        if(!(ss >> idHex >> rec.visualCode >> rec.imageName >> rec.className))
        {
            error = "Malformed definition line " + numberString(lineNo) + " in " + path;
            return false;
        }

        std::istringstream idStream(idHex);
        unsigned long id = 0;
        idStream >> std::hex >> id;
        if(!idStream || id > 0xffffUL)
        {
            error = "Invalid hexadecimal ID on line " + numberString(lineNo) + " in " + path;
            return false;
        }
        rec.id = static_cast<uint16_t>(id);

        std::getline(ss, rec.description);
        while(!rec.description.empty() &&
              (rec.description[0] == ' ' || rec.description[0] == '\t'))
            rec.description.erase(0, 1);

        records_.push_back(rec);
    }
    return true;
}

const DefinitionRecord *DefinitionTable::find(uint16_t id) const
{
    for(size_t i = 0; i < records_.size(); ++i)
        if(records_[i].id == id)
            return &records_[i];
    return 0;
}

std::string joinPath(const std::string &root, const std::string &name)
{
    if(root.empty() || root == ".")
        return root.empty() ? name : root + "/" + name;

    const char last = root[root.size() - 1];
    if(last == '/' || last == '\\')
        return root + name;
    return root + "/" + name;
}

bool loadEpisode(const std::string &root, int episode, EpisodeData &out, std::string &error)
{
    if(episode < 1 || episode > 3)
    {
        error = "Nitemare3D episode must be between 1 and 3";
        return false;
    }

    std::ostringstream suffix;
    suffix << episode;

    EpisodeData loaded;
    loaded.episode = episode;
    if(!loaded.map.load(joinPath(root, "MAP." + suffix.str()), error))
        return false;
    if(!loaded.img.load(joinPath(root, "IMG." + suffix.str()), error))
        return false;
    if(!loaded.walls.load(joinPath(root, "WALLS." + suffix.str()), error))
        return false;
    if(!loaded.objects.load(joinPath(root, "OBJECTS." + suffix.str()), error))
        return false;

    out = loaded;
    return true;
}

} // namespace n3d
