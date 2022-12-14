#include "pathfinder.h"
#include "dungeonUtils.h"
#include <algorithm>

float heuristic(IVec2 lhs, IVec2 rhs)
{
  return sqrtf(sqr(float(lhs.x - rhs.x)) + sqr(float(lhs.y - rhs.y)));
};

template<typename T>
static size_t coord_to_idx(T x, T y, size_t w)
{
  return size_t(y) * w + size_t(x);
}

static std::vector<IVec2> reconstruct_path(std::vector<IVec2> prev, IVec2 to, size_t width)
{
  IVec2 curPos = to;
  std::vector<IVec2> res = {curPos};
  while (prev[coord_to_idx(curPos.x, curPos.y, width)] != IVec2{-1, -1})
  {
    curPos = prev[coord_to_idx(curPos.x, curPos.y, width)];
    res.insert(res.begin(), curPos);
  }
  return res;
}

static std::vector<PortalConnection> reconstruct_path(const DungeonPortals &dp, std::vector<size_t> prev)
{
  std::vector<PortalConnection> res;
  size_t curPos = dp.portals.size();
  while (prev[curPos] != (size_t)-1)
  {
    size_t nextPos = prev[curPos];
    if (nextPos == dp.portals.size() + 1)
      std::swap(nextPos, curPos);
    for (const auto pc : dp.portals[nextPos].conns)
    {
      if (pc.connIdx == curPos)
      {
        if (curPos == dp.portals.size() + 1)
          res.insert(res.begin(), {nextPos, pc.score});
        else
          res.insert(res.begin(), pc);
        break;
      }
    }
    if (curPos == dp.portals.size() + 1)
      std::swap(nextPos, curPos);
    curPos = nextPos;
    //res.insert(res.begin(), dp.portals);
  }
  return res;
}

std::vector<IVec2> find_path_a_star(const DungeonData &dd, IVec2 from, IVec2 to,
                                           IVec2 lim_min, IVec2 lim_max)
{
  if (from.x < 0 || from.y < 0 || from.x >= int(dd.width) || from.y >= int(dd.height))
    return std::vector<IVec2>();
  size_t inpSize = dd.width * dd.height;

  std::vector<float> g(inpSize, std::numeric_limits<float>::max());
  std::vector<float> f(inpSize, std::numeric_limits<float>::max());
  std::vector<IVec2> prev(inpSize, {-1,-1});

  auto getG = [&](IVec2 p) -> float { return g[coord_to_idx(p.x, p.y, dd.width)]; };
  auto getF = [&](IVec2 p) -> float { return f[coord_to_idx(p.x, p.y, dd.width)]; };

  g[coord_to_idx(from.x, from.y, dd.width)] = 0;
  f[coord_to_idx(from.x, from.y, dd.width)] = heuristic(from, to);

  std::vector<IVec2> openList = {from};
  std::vector<IVec2> closedList;

  while (!openList.empty())
  {
    size_t bestIdx = 0;
    float bestScore = getF(openList[0]);
    for (size_t i = 1; i < openList.size(); ++i)
    {
      float score = getF(openList[i]);
      if (score < bestScore)
      {
        bestIdx = i;
        bestScore = score;
      }
    }
    if (openList[bestIdx] == to)
      return reconstruct_path(prev, to, dd.width);
    IVec2 curPos = openList[bestIdx];
    openList.erase(openList.begin() + bestIdx);
    if (std::find(closedList.begin(), closedList.end(), curPos) != closedList.end())
      continue;
    size_t idx = coord_to_idx(curPos.x, curPos.y, dd.width);
    closedList.emplace_back(curPos);
    auto checkNeighbour = [&](IVec2 p)
    {
      // out of bounds
      if (p.x < lim_min.x || p.y < lim_min.y || p.x >= lim_max.x || p.y >= lim_max.y)
        return;
      size_t idx = coord_to_idx(p.x, p.y, dd.width);
      // not empty
      if (dd.tiles[idx] == dungeon::wall)
        return;
      float edgeWeight = 1.f;
      float gScore = getG(curPos) + 1.f * edgeWeight; // we're exactly 1 unit away
      if (gScore < getG(p))
      {
        prev[idx] = curPos;
        g[idx] = gScore;
        f[idx] = gScore + heuristic(p, to);
      }
      bool found = std::find(openList.begin(), openList.end(), p) != openList.end();
      if (!found)
        openList.emplace_back(p);
    };
    checkNeighbour({curPos.x + 1, curPos.y + 0});
    checkNeighbour({curPos.x - 1, curPos.y + 0});
    checkNeighbour({curPos.x + 0, curPos.y + 1});
    checkNeighbour({curPos.x + 0, curPos.y - 1});
  }
  // empty path
  return std::vector<IVec2>();
}


void prebuild_map(flecs::world &ecs)
{
  auto mapQuery = ecs.query<const DungeonData>();

  size_t splitTiles = pathfinder::splitTiles;
  ecs.defer([&]()
  {
    mapQuery.each([&](flecs::entity e, const DungeonData &dd)
    {
      // go through each super tile
      const size_t width = dd.width / splitTiles;
      const size_t height = dd.height / splitTiles;

      auto check_border = [&](size_t xx, size_t yy,
                              size_t dir_x, size_t dir_y,
                              int offs_x, int offs_y,
                              std::vector<PathPortal> &portals)
      {
        int spanFrom = -1;
        int spanTo = -1;
        for (size_t i = 0; i < splitTiles; ++i)
        {
          size_t x = xx * splitTiles + i * dir_x;
          size_t y = yy * splitTiles + i * dir_y;
          size_t nx = x + offs_x;
          size_t ny = y + offs_y;
          if (dd.tiles[y * dd.width + x] != dungeon::wall &&
              dd.tiles[ny * dd.width + nx] != dungeon::wall)
          {
            if (spanFrom < 0)
              spanFrom = i;
            spanTo = i;
          }
          else if (spanFrom >= 0)
          {
            // write span
            portals.push_back({xx * splitTiles + spanFrom * dir_x + offs_x,
                               yy * splitTiles + spanFrom * dir_y + offs_y,
                               xx * splitTiles + spanTo * dir_x,
                               yy * splitTiles + spanTo * dir_y});
            spanFrom = -1;
          }
        }
        if (spanFrom >= 0)
        {
          portals.push_back({xx * splitTiles + spanFrom * dir_x + offs_x,
                             yy * splitTiles + spanFrom * dir_y + offs_y,
                             xx * splitTiles + spanTo * dir_x,
                             yy * splitTiles + spanTo * dir_y});
        }
      };

      std::vector<PathPortal> portals;
      std::vector<std::vector<size_t>> tilePortalsIndices;

      auto push_portals = [&](size_t x, size_t y,
                              int offs_x, int offs_y,
                              const std::vector<PathPortal> &new_portals)
      {
        for (const PathPortal &portal : new_portals)
        {
          size_t idx = portals.size();
          portals.push_back(portal);
          tilePortalsIndices[y * width + x].push_back(idx);
          tilePortalsIndices[(y + offs_y) * width + x + offs_x].push_back(idx);
        }
      };
      for (size_t y = 0; y < height; ++y)
        for (size_t x = 0; x < width; ++x)
        {
          tilePortalsIndices.push_back(std::vector<size_t>{});
          // check top
          if (y > 0)
          {
            std::vector<PathPortal> topPortals;
            check_border(x, y, 1, 0, 0, -1, topPortals);
            push_portals(x, y, 0, -1, topPortals);
          }
          // left
          if (x > 0)
          {
            std::vector<PathPortal> leftPortals;
            check_border(x, y, 0, 1, -1, 0, leftPortals);
            push_portals(x, y, -1, 0, leftPortals);
          }
        }
      for (size_t tidx = 0; tidx < tilePortalsIndices.size(); ++tidx)
      {
        const std::vector<size_t> &indices = tilePortalsIndices[tidx];
        size_t x = tidx % width;
        size_t y = tidx / width;
        IVec2 limMin{int((x + 0) * splitTiles), int((y + 0) * splitTiles)};
        IVec2 limMax{int((x + 1) * splitTiles), int((y + 1) * splitTiles)};
        for (size_t i = 0; i < indices.size(); ++i)
        {
          PathPortal &firstPortal = portals[indices[i]];
          for (size_t j = i + 1; j < indices.size(); ++j)
          {
            PathPortal &secondPortal = portals[indices[j]];
            // check path from i to j
            // check each position (to find closest dist) (could be made more optimal)
            bool noPath = false;
            size_t minDist = 0xffffffff;
            for (size_t fromY = std::max(firstPortal.startY, size_t(limMin.y));
                        fromY <= std::min(firstPortal.endY, size_t(limMax.y - 1)) && !noPath; ++fromY)
            {
              for (size_t fromX = std::max(firstPortal.startX, size_t(limMin.x));
                          fromX <= std::min(firstPortal.endX, size_t(limMax.x - 1)) && !noPath; ++fromX)
              {
                for (size_t toY = std::max(secondPortal.startY, size_t(limMin.y));
                            toY <= std::min(secondPortal.endY, size_t(limMax.y - 1)) && !noPath; ++toY)
                {
                  for (size_t toX = std::max(secondPortal.startX, size_t(limMin.x));
                              toX <= std::min(secondPortal.endX, size_t(limMax.x - 1)) && !noPath; ++toX)
                  {
                    IVec2 from{int(fromX), int(fromY)};
                    IVec2 to{int(toX), int(toY)};
                    std::vector<IVec2> path = find_path_a_star(dd, from, to, limMin, limMax);
                    if (path.empty() && from != to)
                    {
                      noPath = true; // if we found that there's no path at all - we can break out
                      break;
                    }
                    minDist = std::min(minDist, path.size());
                  }
                }
              }
            }
            // write pathable data and length
            if (noPath)
              continue;
            firstPortal.conns.push_back({indices[j], float(minDist)});
            secondPortal.conns.push_back({indices[i], float(minDist)});
          }
        }
      }
      e.set(DungeonPortals{splitTiles, portals, tilePortalsIndices});
    });
  });
}

size_t find_dist_to_protal(const DungeonData &dd, const PathPortal &portal, IVec2 p)
{
  size_t out = 0xFFFFFF;
  size_t splitTiles = pathfinder::splitTiles;
  size_t tileIdx = coord_to_tile_idx(p.x, p.y, dd.width);
  size_t tilePerRow = dd.width / splitTiles;
  size_t row = tileIdx / tilePerRow;
  size_t col = tileIdx % tilePerRow;
  IVec2 limMin{ int((col + 0) * splitTiles), int((row + 0) * splitTiles) };
  IVec2 limMax{ int((col + 1) * splitTiles), int((row + 1) * splitTiles) };
  for (size_t startX = std::max(portal.startX, size_t(limMin.x));
              startX <= std::min(portal.endX, size_t(limMax.x - 1)); ++startX)
    for (size_t startY = std::max(portal.startY, size_t(limMin.y));
                startY <= std::min(portal.endY, size_t(limMax.y - 1)); ++startY)
    {
      IVec2 toPortal{ int(startX), int(startY) };
      std::vector<IVec2> path = find_path_a_star(dd, p, toPortal, limMin, limMax);
      if (path.empty() && toPortal != p)
      {
        break;
      }
      out = std::min(out, path.size());
    }
  return out;
}

//Use copy DungeonPortals for modification
std::vector<PortalConnection> find_path_a_star_tiled(const DungeonData &dd, DungeonPortals dp, IVec2 from, IVec2 to)
{
  if (from.x < 0 || from.y < 0 || from.x >= int(dd.width) || from.y >= int(dd.height))
    return std::vector<PortalConnection>();
  size_t inpSize = dp.portals.size() + 2;

  size_t toIdx = dp.portals.size();
  size_t fromIdx = dp.portals.size() + 1;

  std::vector<float> g(inpSize, std::numeric_limits<float>::max());
  std::vector<float> f(inpSize, std::numeric_limits<float>::max());
  std::vector<size_t> prev(inpSize, -1);

  g[fromIdx] = 0;
  f[fromIdx] = heuristic(from, to);

  std::vector<size_t> openList;
  std::vector<size_t> closedList = { fromIdx };

  size_t fromTileIdx = coord_to_tile_idx(from.x, from.y, dd.width);
  for (const int idx : dp.tilePortalsIndices[fromTileIdx])
  {
    PathPortal &portal = dp.portals[idx];
    size_t gScore = find_dist_to_protal(dd, portal, from);
    IVec2 portalPos = { (portal.startX + portal.endX + 1) * 0.5f,
                        (portal.startY + portal.endY + 1) * 0.5f };
    prev[idx] = fromIdx;
    g[idx] = gScore;
    f[idx] = gScore + heuristic(portalPos, to);
    openList.emplace_back(idx);
    portal.conns.push_back({ fromIdx, (float)gScore });
  }

  size_t toTileIdx = coord_to_tile_idx(to.x, to.y, dd.width);
  for (const int idx : dp.tilePortalsIndices[toTileIdx])
  {
    PathPortal &portal = dp.portals[idx];
    float dist = find_dist_to_protal(dd, portal, to);
    portal.conns.push_back({ toIdx, dist });
  }

  while (!openList.empty())
  {
    size_t bestIdx = 0;
    float bestScore = f[openList[0]];
    for (size_t i = 1; i < openList.size(); ++i)
    {
      float score = f[openList[i]];
      if (score < bestScore)
      {
        bestIdx = i;
        bestScore = score;
      }
    }
    if (openList[bestIdx] == toIdx)
      return reconstruct_path(dp, prev);
    size_t curPos = openList[bestIdx];
    openList.erase(openList.begin() + bestIdx);
    if (std::find(closedList.begin(), closedList.end(), curPos) != closedList.end())
      continue;
    closedList.emplace_back(curPos);

    auto checkNeighbour = [&](const PortalConnection &pc)
    {
      float gScore = g[curPos] + pc.score;
      if (gScore < g[pc.connIdx])
      {
        IVec2 portalPos;
        if (pc.connIdx != dp.portals.size())
        {
          const PathPortal &portal = dp.portals[pc.connIdx];
          portalPos.x = (portal.startX + portal.endX + 1) * 0.5f;
          portalPos.y = (portal.startY + portal.endY + 1) * 0.5f;
        }
        else
        {
          portalPos = to;
        }
        
        prev[pc.connIdx] = curPos;
        g[pc.connIdx] = gScore;
        f[pc.connIdx] = gScore + heuristic(portalPos, to);
      }
      bool found = std::find(openList.begin(), openList.end(), pc.connIdx) != openList.end();
      if (!found)
        openList.emplace_back(pc.connIdx);
    };

    const auto &curPortal = dp.portals[curPos];
    for (const auto &pc : curPortal.conns)
    {
      checkNeighbour(pc);
    }
  }
}

