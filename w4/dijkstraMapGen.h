#pragma once
#include <vector>
#include <flecs.h>
#include "ecsTypes.h"

namespace dmaps
{
  void gen_player_approach_map(flecs::world &ecs, std::vector<float> &map, const DmapParams &params);
  void gen_player_flee_map(flecs::world &ecs, std::vector<float> &map, const DmapParams &params);
  void gen_hive_pack_map(flecs::world &ecs, std::vector<float> &map, const DmapParams &params);
  void gen_research_map(flecs::world &ecs, std::vector<float> &map, const DmapParams &params);
  void gen_archer_map(flecs::world &ecs, std::vector<float> &map, const DmapParams &params);
  
  //this is special map only for research mode
  void gen_flee_from_monster_map(flecs::world &ecs, std::vector<float> &map, const DmapParams &params);
  //void gen_gold_map(flecs::world &ecs, std::vector<float> &map, const DmapParams &params);
};

