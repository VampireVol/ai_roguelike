#pragma once
#include <flecs.h>
#include <vector>
#include "math.h"
#include "pathfinderUtils.h"
#include "ecsTypes.h"

struct PortalConnection
{
  size_t connIdx;
  float score;
};

struct PathPortal
{
  size_t startX, startY;
  size_t endX, endY;
  std::vector<PortalConnection> conns;
};

struct DungeonPortals
{
  size_t tileSplit;
  std::vector<PathPortal> portals;
  std::vector<std::vector<size_t>> tilePortalsIndices;
};

// w - is like w for coord_to_idx
template<typename T>
static size_t coord_to_tile_idx(T x, T y, size_t w)
{
  return size_t(y) / pathfinder::splitTiles * w / pathfinder::splitTiles + size_t(x) / pathfinder::splitTiles;
}

std::vector<IVec2> find_path_a_star(const DungeonData &dd, IVec2 from, IVec2 to, IVec2 lim_min, IVec2 lim_max);
void prebuild_map(flecs::world &ecs);
std::vector<PortalConnection> find_path_a_star_tiled(const DungeonData &dd, DungeonPortals dp, IVec2 from, IVec2 to);

