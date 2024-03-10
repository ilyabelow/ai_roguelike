#pragma once
#include <vector>
#include <flecs.h>
#include "raylib.h"

namespace dmaps
{
  void init_query_dungeon_data(flecs::world &ecs);
  void gen_player_approach_map(flecs::world &ecs, std::vector<float> &map);
  void gen_player_flee_map(flecs::world &ecs, std::vector<float> &map);
  void gen_hive_pack_map(flecs::world &ecs, std::vector<float> &map);
  void gen_to_target_map(flecs::world &ecs, std::vector<float> &map, int x, int y);
  std::vector<Vector2> gen_flow_map(std::vector<float> &in_map, int width, int height, int step_count);
};

