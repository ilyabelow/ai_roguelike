#include "dijkstraMapGen.h"
#include "ecsTypes.h"
#include "dungeonUtils.h"

static flecs::query<const DungeonData> dungeonDataQuery;

void dmaps::init_query_dungeon_data(flecs::world &ecs)
{
  dungeonDataQuery = ecs.query<const DungeonData>();
}

template<typename Callable>
static void query_dungeon_data(flecs::world &ecs, Callable c)
{
  dungeonDataQuery.each(c);
}

template<typename Callable>
static void query_characters_positions(flecs::world &ecs, Callable c)
{
  static auto characterPositionQuery = ecs.query<const Position, const Team>();

  characterPositionQuery.each(c);
}

constexpr float invalid_tile_value = 1e5f;

static void init_tiles(std::vector<float> &map, const DungeonData &dd)
{
  map.resize(dd.width * dd.height);
  for (float &v : map)
    v = invalid_tile_value;
}

// scan version, could be implemented as Dijkstra version as well
static void process_dmap(std::vector<float> &map, const DungeonData &dd)
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
}

void dmaps::gen_player_approach_map(flecs::world &ecs, std::vector<float> &map)
{
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    init_tiles(map, dd);
    query_characters_positions(ecs, [&](const Position &pos, const Team &t)
    {
      if (t.team == 0) // player team hardcode
        map[pos.y * dd.width + pos.x] = 0.f;
    });
    process_dmap(map, dd);
  });
}

void dmaps::gen_player_flee_map(flecs::world &ecs, std::vector<float> &map)
{
  gen_player_approach_map(ecs, map);
  for (float &v : map)
    if (v < invalid_tile_value)
      v *= -1.2f;
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    process_dmap(map, dd);
  });
}

void dmaps::gen_hive_pack_map(flecs::world &ecs, std::vector<float> &map)
{
  static auto hiveQuery = ecs.query<const Position, const Hive>();
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    init_tiles(map, dd);
    hiveQuery.each([&](const Position &pos, const Hive &)
    {
      map[pos.y * dd.width + pos.x] = 0.f;
    });
    process_dmap(map, dd);
  });
}

void dmaps::gen_to_target_map(flecs::world &ecs, std::vector<float> &map, int x, int y)
{
  query_dungeon_data(ecs, [&](const DungeonData &dd)
  {
    init_tiles(map, dd);
    // for some reason I cannot access components of newly created target
    map[y * dd.width + x] = 0.f;
    process_dmap(map, dd);
  });
}

struct int2
{
  int x, y;
};

int2 step_on_dijkstra_map(const std::vector<float> &map, int width, int height, int2 v)
{
  int2 res = v;
  float minVal = map[v.y * width + v.x];
  auto tryStep = [&](int x, int y)
  {
    if (x < 0 || y < 0 || x >= width || y >= height)
      return;
    const float val = map[y * width + x];
    if (val < minVal)
    {
      minVal = val;
      res.x = x;
      res.y = y;
    }
  };
  tryStep(v.x - 1, v.y + 0);
  tryStep(v.x + 1, v.y + 0);
  tryStep(v.x + 0, v.y - 1);
  tryStep(v.x + 0, v.y + 1);
  return res;
}

std::vector<Vector2> dmaps::gen_flow_map(std::vector<float> &in_map, int width, int height, int step_count)
{
  std::vector<Vector2> res(width * height);

  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
    {
      if (in_map[y * width + x] < invalid_tile_value)
      {
        int2 v = {x, y};
        for (int i = 0; i < step_count; ++i)
          v = step_on_dijkstra_map(in_map, width, height, v);
        res[y * width + x] = {float(v.x - x), float(v.y - y)};
      } else {
        res[y * width + x] = {0., 0.};
      }
    }
  return res;
}