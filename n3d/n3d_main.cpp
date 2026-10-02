#include "n3d_main.h"
#include "n3d_data.h"

#include <stdio.h>
#include <string>

int N3D_RunBootstrap(const char *dataDir, int episode)
{
    const std::string root = (dataDir && *dataDir) ? dataDir : ".";
    n3d::EpisodeData data;
    std::string error;

    if(!n3d::loadEpisode(root, episode, data, error))
    {
        fprintf(stderr, "Nitemare3D bootstrap failed: %s\n", error.c_str());
        return 1;
    }

    printf("Nitemare3D resource bootstrap OK\n");
    printf("  data directory : %s\n", root.c_str());
    printf("  episode        : %d\n", data.episode);
    printf("  levels         : %u\n", (unsigned)data.map.levels().size());
    printf("  map grid       : %dx%d\n", n3d::LevelMap::Width, n3d::LevelMap::Height);
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
    printf("\nStage 1 complete: native Nitemare3D data can be loaded without Wolf3D VSWAP/VGA/MAPHEAD resources.\n");

    return 0;
}
