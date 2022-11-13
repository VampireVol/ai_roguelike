#pragma once
#include "ecsTypes.h"
#include <flecs.h>

namespace dungeon
{
  constexpr char wall = '#';
  constexpr char floor = ' ';
  constexpr int rangeDistance = 4; //maybe not good place for here

  Position find_walkable_tile(flecs::world &ecs);
  bool is_tile_walkable(flecs::world &ecs, Position pos);
  bool is_tile_reahable(flecs::world &ecs, Position from, Position to);
  Position get_closer_tile(Position from, Position to);
};
