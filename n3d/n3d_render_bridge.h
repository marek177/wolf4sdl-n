#ifndef N3D_RENDER_BRIDGE_H
#define N3D_RENDER_BRIDGE_H

#include "n3d_data.h"
#include "n3d_world.h"

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace n3d
{

struct WallTextureView
{
    const uint8_t *pixels;
    unsigned width;
    unsigned height;
    uint32_t fileOffset;

    WallTextureView() : pixels(0), width(0), height(0), fileOffset(0) {}
    bool valid() const { return pixels != 0 && width != 0 && height == 64; }
};

struct ObjectTextureView
{
    const uint8_t *pixels;
    unsigned width;
    unsigned height;
    uint32_t fileOffset;

    ObjectTextureView() : pixels(0), width(0), height(0), fileOffset(0) {}
    bool valid() const { return pixels != 0 && width != 0 && height != 0; }
};

class ObjectTextureBridge
{
public:
    explicit ObjectTextureBridge(const ImgArchive &img) : img_(img) {}
    bool texture(uint8_t objectId, ObjectTextureView &out) const;
    bool sequenceTexture(uint8_t objectId,
                         unsigned frameIndex,
                         ObjectTextureView &out) const;

private:
    const ImgArchive &img_;
};

struct RenderCell
{
    uint8_t wallId;
    uint8_t wallClass;
    bool opaque;
    unsigned textureWidth;

    RenderCell() : wallId(0), wallClass(0), opaque(false), textureWidth(0) {}
};

struct RenderMap
{
    enum { Width = WorldState::Width, Height = WorldState::Height, CellCount = Width * Height };
    std::vector<RenderCell> cells;

    RenderMap();
    const RenderCell &at(size_t x, size_t y) const;
};

class WallTextureBridge
{
public:
    explicit WallTextureBridge(const ImgArchive &img) : img_(img) {}

    bool texture(uint8_t wallId, WallTextureView &out) const;

    // Wolf4SDL uses 16.16 tile coordinates where one tile is 65536 units.
    // Nitemare3D wall sampling is 64 texels per tile vertically; wall images
    // may be 64 or 128 texels wide. The horizontal U therefore uses one
    // texture texel per 1/64 tile and wraps by the actual frame width.
    const uint8_t *columnFromWolfFixed(uint8_t wallId,
                                       int32_t alongWallFixed,
                                       bool reverse,
                                       unsigned *uOut,
                                       unsigned *widthOut) const;
    const uint8_t *columnFromWolfFixedFrame(uint8_t wallId,
                                            unsigned frameIndex,
                                            int32_t alongWallFixed,
                                            bool reverse,
                                            unsigned *uOut,
                                            unsigned *widthOut) const;

private:
    const ImgArchive &img_;
};

bool buildRenderMap(const EpisodeData &episode,
                    const WorldState &world,
                    RenderMap &out,
                    std::string &error);

} // namespace n3d

#endif
