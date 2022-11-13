#include "dijkstraMapGen.h"
#include "ecsTypes.h"
#include "dungeonUtils.h"
#include <cmath>

template<typename Callable>
static void query_dungeon_data(flecs::world &ecs, Callable c)
{
  static auto dungeonDataQuery = ecs.query<const DungeonData>();

  dungeonDataQuery.each(c);
}

template<typename Callable>
static void query_characters_positions(flecs::world &ecs, Callable c)
{
  static auto characterPositionQuery = ecs.query<const Position, const Team>();

  characterPositionQuery.each(c);
}

template<typename Callable>
static void query_player_position(flecs::world &ecs, Callable c)
{
  static auto playerPositionQuery = ecs.query<const Position, const IsPlayer>();

  playerPositionQuery.each(c);
}

constexpr float invalid_tile_value = 1e5f;

static void init_tiles(std::vector<float> &map, const DungeonData &dd)
{
  map.resize(dd.width * dd.height);
  for (float &v : map)
    v = invalid_tile_value;
}

// scan version, could be implemented as Dijkstra version as well
static void process_dmap(std::vector<float> &map, const DungeonData &dd, const DmapParams &params)
{
  bool done = false;
  auto getMapAt = [&](size_t x, size_t y, float def)
  {
    if (x < dd.width && y < dd.width && dd.tiles[y * dd.width + x] == dungeon::floor)
      return map[y * dd.width + x];
    return def;
  };
  auto getMinNei = [&](size_t x, size_t y)
  {
    float val = map[y * dd.width + x];
    val = std::min(val, getMapAt(x - 1, y + 0, val));
    val = std::min(val, getMapAt(x + 1, y + 0, val));
    val = std::min(val, getMapAt(x + 0, y - 1, val));
    val = std::min(val, getMapAt(x + 0, y + 1, val));
    return val;
  };
  while (!done)
  {
    done = true;
    for (size_t y = 0; y < dd.height; ++y)
      for (size_t x = 0; x < dd.width; ++x)
      {
        const size_t i = y * dd.width + x;
        if (dd.tiles[i] != dungeon::floor)
          continue;
        const float myVal = getMapAt(x, y, invalid_tile_value);
        const float minVal = getMinNei(x, y);
        if (minVal < myVal - 1.f)
        {
          map[i] = minVal + 1.f;
          done = false;
        }
      }
  }
  for (size_t y = 0; y < dd.height; ++y)
    for (size_t x = 0; x < dd.width; ++x)
    {
      const size_t i = y * dd.width + x;
      if (map[i] < invalid_tile_value)
      {
        if (params.isAPow)
          map[i] = copysignf(powf(abs(map[i]), params.a), map[i]);
        else
          map[i] = powf(params.a, map[i]);
      }
    }
}

void dmaps::gen_player_approach_map(flecs::world &ecs, std::vector<float> &map, const DmapParams &params)
{
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    init_tiles(map, dd);
    query_characters_positions(ecs, [&](const Position &pos, const Team &t)
    {
      if (t.team == 0) // player team hardcode
        map[pos.y * dd.width + pos.x] = 0.f;
    });
    process_dmap(map, dd, params);
  });
}

void dmaps::gen_player_flee_map(flecs::world &ecs, std::vector<float> &map, const DmapParams &params)
{
  gen_player_approach_map(ecs, map, params);
  for (float &v : map)
    if (v < invalid_tile_value)
      v *= -1.2f;
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    process_dmap(map, dd, params);
  });
}

void dmaps::gen_hive_pack_map(flecs::world &ecs, std::vector<float> &map, const DmapParams &params)
{
  static auto hiveQuery = ecs.query<const Position, const Hive>();
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    init_tiles(map, dd);
    hiveQuery.each([&](const Position &pos, const Hive &)
    {
      map[pos.y * dd.width + pos.x] = 0.f;
    });
    process_dmap(map, dd, params);
  });
}

void dmaps::gen_research_map(flecs::world &ecs, std::vector<float> &map, const DmapParams &params)
{
  static auto dungeonDataQuery = ecs.query<DungeonData>();
  dungeonDataQuery.each([&](DungeonData &dd)
  {
    init_tiles(map, dd);
    query_player_position(ecs, [&](const Position &pos, const IsPlayer)
    {
      constexpr int radius_research = 2;
      constexpr int radius_wallcheck = radius_research + 1;
      for (int i = -radius_research; i <= radius_research; ++i)
      {
        for (int j = -radius_research; j <= radius_research; ++j)
        {
          int idx = (pos.y + i) * dd.width + pos.x + j;
          if (idx > 0 && idx < dd.width * dd.height)
          {
            dd.researchedTiles[idx] = true;
          }
        }
      }
      for (int i = -radius_wallcheck; i <= radius_wallcheck; ++i)
      {
        for (int j = -radius_wallcheck; j <= radius_wallcheck; ++j)
        {
          int idx = (pos.y + i) * dd.width + pos.x + j;
          if (idx > 0 && idx < dd.width * dd.height && dd.tiles[idx] == dungeon::wall)
          {
            dd.researchedTiles[idx] = true;
          }
        }
      }
    });
    for (int i = 0; i < map.size(); ++i)
    {
      if (!dd.researchedTiles[i])
        map[i] = 0.f;
    }
    process_dmap(map, dd, params);
  });
}

void dmaps::gen_archer_map(flecs::world &ecs, std::vector<float> &map, const DmapParams &params)
{
  static auto dungeonDataQuery = ecs.query<DungeonData>();
  dungeonDataQuery.each([&](DungeonData &dd)
  {
    init_tiles(map, dd);
    query_player_position(ecs, [&](const Position &pos, const IsPlayer)
    {
      int i, j;
      for (i = dungeon::rangeDistance, j = 0; i > 0; --i, ++j)
      {
        std::vector<Position> checkPositions
        {
          { pos.x + i, pos.y + j },
          { pos.x - j, pos.y + i },
          { pos.x - i, pos.y - j },
          { pos.x + j, pos.y - i }
        };
        for (auto checkPosition : checkPositions)
        {
          while (!(dungeon::is_tile_walkable(ecs, checkPosition) &&
                   dungeon::is_tile_reahable(ecs, checkPosition, pos)))
          {
            checkPosition = dungeon::get_closer_tile(checkPosition, pos);
          }
          map[checkPosition.y * dd.width + checkPosition.x] = 0.f;
        }
      }
    });
    process_dmap(map, dd, params);
  });
}

void dmaps::gen_flee_from_monster_map(flecs::world &ecs, std::vector<float> &map, const DmapParams &params)
{
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    map.resize(dd.width * dd.height);
    for (int i = 0; i < map.size(); ++i)
      map[i] = invalid_tile_value;

    
    bool haveEnemy = false;
    query_characters_positions(ecs, [&](const Position &pos, const Team &t)
    {
      const int posIdx = pos.y * dd.width + pos.x;
      if (t.team == 1 && dd.researchedTiles[posIdx]) // monster team hardcode
      {
        haveEnemy = true;
        map[posIdx] = 0.f;
      }
    });
    if (haveEnemy)
    {
      process_dmap(map, dd, params);
      for (float &v : map)
        if (v < invalid_tile_value)
          v *= -1.2f;
    }
    else
    {
      for (int i = 0; i < map.size(); ++i)
        if (dd.researchedTiles[i])
          map[i] = 0.0f;
    }
          
  });
}

