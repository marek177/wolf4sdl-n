#include "n3d_main.h"
#include "n3d_data.h"
#include "n3d_world.h"
#include "n3d_render_bridge.h"
#include "n3d_runtime.h"
#include "n3d_door.h"

#include <stdio.h>
#include <string>

int N3D_RunBootstrap(const char *dataDir, int episode, int level)
{
    const std::string root = (dataDir && *dataDir) ? dataDir : ".";
    std::string error;

    if(!n3d::loadRuntime(root, episode, level, error))
    {
        fprintf(stderr, "Nitemare3D bootstrap failed: %s\n", error.c_str());
        return 1;
    }

    const n3d::EpisodeData &data = *n3d::runtimeEpisode();
    const n3d::WorldState &world = *n3d::runtimeWorld();
    const n3d::RenderMap &renderMap = *n3d::runtimeRenderMap();

    unsigned opaqueCells = 0;
    unsigned width64Cells = 0;
    unsigned width128Cells = 0;
    unsigned otherWidthCells = 0;
    for(size_t i = 0; i < renderMap.cells.size(); ++i)
    {
        if(!renderMap.cells[i].opaque)
            continue;
        ++opaqueCells;
        if(renderMap.cells[i].textureWidth == 64)
            ++width64Cells;
        else if(renderMap.cells[i].textureWidth == 128)
            ++width128Cells;
        else
            ++otherWidthCells;
    }

    printf("Nitemare3D resource bootstrap OK\n");
    printf("  data directory : %s\n", root.c_str());
    printf("  episode/level  : E%dM%d\n", data.episode, level);
    printf("  levels         : %u\n", (unsigned)data.map.levels().size());
    printf("  map grid       : %dx%d\n", n3d::LevelMap::Width, n3d::LevelMap::Height);
    printf("  player start   : x=%u y=%u dir=%s raw=0x%02X (1-based x=%u y=%u)\n",
           (unsigned)world.playerStart.x,
           (unsigned)world.playerStart.y,
           n3d::directionName(world.playerStart.direction),
           (unsigned)world.playerStart.rawObjectId,
           (unsigned)(world.playerStart.x + 1),
           (unsigned)(world.playerStart.y + 1));
    printf("  IMG frames     : %u\n", (unsigned)data.img.frames().size());
    printf("  IMG first data : 0x%08lx\n", (unsigned long)data.img.firstDataOffset());
    printf("  wall slots     : %u non-zero, %u exact frame refs\n",
           (unsigned)data.img.nonZeroWallSlots(),
           (unsigned)data.img.exactWallFrameRefs());
    printf("  object slots   : %u non-zero, %u exact frame refs\n",
           (unsigned)data.img.nonZeroObjectSlots(),
           (unsigned)data.img.exactObjectFrameRefs());
    printf("  WALLS records  : %u\n", (unsigned)data.walls.records().size());
    printf("  OBJECTS records: %u\n", (unsigned)data.objects.records().size());
    printf("  renderable cells: %u\n", opaqueCells);
    printf("  texture widths : 64=%u 128=%u other=%u\n",
           width64Cells, width128Cells, otherWidthCells);

    const n3d::DoorRuntime *doors = n3d::runtimeDoorsConst();
    printf("  door controllers: %u / 64\n",
           doors ? (unsigned)doors->controllers().size() : 0u);

    const n3d::ObjectRuntime *objects = n3d::runtimeObjectsConst();
    unsigned activeObjects = 0;
    unsigned collectibleObjects = 0;
    if(objects)
    {
        for(size_t i = 0; i < objects->objects().size(); ++i)
        {
            const n3d::RuntimeObject &object = objects->objects()[i];
            if(!object.active)
                continue;
            ++activeObjects;
            if((object.properties & 0x04) != 0)
                ++collectibleObjects;
        }
    }
    printf("  world OBJECTs   : %u / 350 (%u collectible)\n",
           activeObjects, collectibleObjects);
    printf("\nStage 6 complete: persistent Nitemare3D runtime now includes collision, doors and world OBJECT pickups.\n");

    return 0;
}
