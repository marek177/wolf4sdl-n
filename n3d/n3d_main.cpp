#include "n3d_main.h"
#include "n3d_data.h"
#include "n3d_world.h"

#include <stdio.h>
#include <string>

int N3D_RunBootstrap(const char *dataDir, int episode, int level)
{
    const std::string root = (dataDir && *dataDir) ? dataDir : ".";
    n3d::EpisodeData data;
    n3d::WorldState world;
    std::string error;

    if(!n3d::loadEpisode(root, episode, data, error))
    {
        fprintf(stderr, "Nitemare3D bootstrap failed: %s\n", error.c_str());
        return 1;
    }
    if(level < 1 || !n3d::buildWorld(data, static_cast<size_t>(level - 1), world, error))
    {
        fprintf(stderr, "Nitemare3D world build failed: %s\n", error.c_str());
        return 1;
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
    printf("\nStage 2 complete: MAP class tables, world cells and player START are decoded natively.\n");

    return 0;
}
