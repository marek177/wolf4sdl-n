#include "n3d_render_bridge.h"

namespace n3d
{

bool ObjectTextureBridge::texture(uint8_t objectId, ObjectTextureView &out) const
{
    out = ObjectTextureView();

    const std::vector<uint32_t> &slots = img_.objectSlotOffsets();
    if(static_cast<size_t>(objectId) >= slots.size())
        return false;

    const uint32_t offset = slots[objectId];
    if(offset == 0)
        return false;

    const ImgFrame *frame = img_.frameAtExactOffset(offset);
    if(!frame || frame->width == 0 || frame->height == 0 || frame->pixels.empty())
        return false;

    const size_t expected =
        static_cast<size_t>(frame->width) * static_cast<size_t>(frame->height);
    if(frame->pixels.size() != expected)
        return false;

    out.pixels = &frame->pixels[0];
    out.width = frame->width;
    out.height = frame->height;
    out.fileOffset = frame->fileOffset;
    return true;
}

RenderMap::RenderMap() : cells(CellCount)
{
}

const RenderCell &RenderMap::at(size_t x, size_t y) const
{
    return cells[y * Width + x];
}

bool WallTextureBridge::texture(uint8_t wallId, WallTextureView &out) const
{
    out = WallTextureView();

    const std::vector<uint32_t> &slots = img_.wallSlotOffsets();
    if(static_cast<size_t>(wallId) >= slots.size())
        return false;

    const uint32_t offset = slots[wallId];
    if(offset == 0)
        return false;

    const ImgFrame *frame = img_.frameAtExactOffset(offset);
    if(!frame || frame->height != 64 || frame->width == 0 || frame->pixels.empty())
        return false;

    const size_t expected = static_cast<size_t>(frame->width) * 64u;
    if(frame->pixels.size() != expected)
        return false;

    out.pixels = &frame->pixels[0];
    out.width = frame->width;
    out.height = frame->height;
    out.fileOffset = frame->fileOffset;
    return true;
}

const uint8_t *WallTextureBridge::columnFromWolfFixed(uint8_t wallId,
                                                       int32_t alongWallFixed,
                                                       bool reverse,
                                                       unsigned *uOut,
                                                       unsigned *widthOut) const
{
    WallTextureView view;
    if(!texture(wallId, view))
        return 0;

    // 16.16 Wolf coordinate -> 64 N3D world/texture units per tile.
    const uint32_t fixed = static_cast<uint32_t>(alongWallFixed);
    unsigned u = static_cast<unsigned>(fixed >> 10);
    u %= view.width;

    if(reverse)
        u = view.width - 1u - u;

    if(uOut)
        *uOut = u;
    if(widthOut)
        *widthOut = view.width;

    // IMG pixel payloads are x-major/column-major. A wall column is 64 bytes.
    return view.pixels + static_cast<size_t>(u) * 64u;
}

bool buildRenderMap(const EpisodeData &episode,
                    const WorldState &world,
                    RenderMap &out,
                    std::string &error)
{
    if(world.cells.size() != RenderMap::CellCount)
    {
        error = "Nitemare3D world has an invalid cell count";
        return false;
    }

    RenderMap render;
    WallTextureBridge textures(episode.img);

    for(size_t i = 0; i < RenderMap::CellCount; ++i)
    {
        const WorldCell &src = world.cells[i];
        RenderCell &dst = render.cells[i];

        dst.wallId = src.wallId;
        dst.wallClass = src.wallClass;

        WallTextureView view;
        if(textures.texture(src.wallId, view))
        {
            dst.opaque = true;
            dst.textureWidth = view.width;
        }
        else
        {
            // FLOOR/NULL/control cells normally have no wall frame and remain
            // non-opaque in the static bridge. Dynamic/special class handling
            // is added by the door/trigger runtime rather than guessed here.
            dst.opaque = false;
            dst.textureWidth = 0;
        }
    }

    out = render;
    return true;
}

} // namespace n3d
