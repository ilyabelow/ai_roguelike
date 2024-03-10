#include "roguelike.h"
#include "ecsTypes.h"
#include "raylib.h"
#include "math.h"
#include "dungeonUtils.h"
#include "dijkstraMapGen.h"
#include "steering.h"
#include "rlikeObjects.h"

static Position find_free_dungeon_tile(flecs::world &ecs)
{
  static auto findMonstersQuery = ecs.query<const Position, const Hitpoints>();
  bool done = false;
  while (!done)
  {
    done = true;
    Position pos = dungeon::find_walkable_tile(ecs);
    findMonstersQuery.each([&](const Position &p, const Hitpoints&)
    {
      if (int(p.x) == int(pos.x) && int(p.y) == int(pos.y))
        done = false;
    });
    if (done)
      return pos;
  };
  return {0, 0};
}


static void register_roguelike_systems(flecs::world &ecs)
{
  static auto dungeonDataQuery = ecs.query<const DungeonData>();
  steer::register_systems(ecs);

  ecs.system<Position, const Velocity>()
    .each([&](Position &pos, const Velocity &vel)
    {
      pos += vel * ecs.delta_time();
    });

  // DRAWING
  ecs.system<const Position, const Color>()
    .term<TextureSource>(flecs::Wildcard)
    .term<BackgroundTile>()
    .each([&](flecs::entity e, const Position &pos, const Color color)
    {
      const auto textureSrc = e.target<TextureSource>();
      DrawTextureQuad(*textureSrc.get<Texture2D>(),
          Vector2{1, 1}, Vector2{0, 0},
          Rectangle{float(pos.x) * tile_size, float(pos.y) * tile_size, tile_size, tile_size}, color);
    });
  ecs.system<const Position, const Color>()
    .term<TextureSource>(flecs::Wildcard).not_()
    .each([&](const Position &pos, const Color color)
    {
      const Rectangle rect = {float(pos.x) * tile_size, float(pos.y) * tile_size, tile_size, tile_size};
      DrawRectangleRec(rect, color);
    });
  ecs.system<const Position, const Color>()
    .term<TextureSource>(flecs::Wildcard)
    .term<BackgroundTile>().not_()
    .each([&](flecs::entity e, const Position &pos, const Color color)
    {
      const auto textureSrc = e.target<TextureSource>();
      DrawTextureQuad(*textureSrc.get<Texture2D>(),
          Vector2{1, 1}, Vector2{0, 0},
          Rectangle{float(pos.x) * tile_size, float(pos.y) * tile_size, tile_size, tile_size}, color);
    });
  ecs.system<Texture2D>()
    .each([&](Texture2D &tex)
    {
      SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    });

  // MAPS
  ecs.system<const DmapWeights>()
    .term<VisualiseMap>()
    .each([&](const DmapWeights &wt)
    {
      dungeonDataQuery.each([&](const DungeonData &dd)
      {
        for (size_t y = 0; y < dd.height; ++y)
          for (size_t x = 0; x < dd.width; ++x)
          {
            float sum = 0.f;
            for (const auto &pair : wt.weights)
            {
              ecs.entity(pair.first.c_str()).get([&](const DijkstraMapData &dmap)
              {
                float v = dmap.map[y * dd.width + x];
                if (v < 1e5f)
                  sum += powf(v * pair.second.mult, pair.second.pow);
                else
                  sum += v;
              });
            }
            if (sum < 1e5f)
              DrawText(TextFormat("%.1f", sum),
                  (float(x) + 0.2f) * tile_size, (float(y) + 0.5f) * tile_size, 150, WHITE);
          }
      });
    });
  ecs.system<const DijkstraMapData>()
    .term<VisualiseMap>()
    .each([](const DijkstraMapData &dmap)
    {
      dungeonDataQuery.each([&](const DungeonData &dd)
      {
        for (size_t y = 0; y < dd.height; ++y)
          for (size_t x = 0; x < dd.width; ++x)
          {
            const float val = dmap.map[y * dd.width + x];
            if (val < 1e5f)
              DrawText(TextFormat("%.1f", val),
                  (float(x) + 0.2f) * tile_size, (float(y) + 0.5f) * tile_size, 150, WHITE);
          }
      });
    });

  const int step_count = 2;

  // a little more drawing
  ecs.system<const FlowMapData>()
    .term<VisualiseMap>()
    .each([](const FlowMapData &fmap)
    {
      dungeonDataQuery.each([&](const DungeonData &dd)
      {
        for (size_t y = 0; y < dd.height; ++y)
          for (size_t x = 0; x < dd.width; ++x)
          {
            const Vector2 val = fmap.map[y * dd.width + x];
            Vector2 origin = {(x + 0.5) * tile_size, (y + 0.5) * tile_size};
            float length_mult = 1. / float(step_count) * 0.8;
            Vector2 point = {origin.x + val.x * tile_size * length_mult, origin.y + val.y * tile_size * length_mult};
            Color semi_red = {255, 0, 0, 64};
            DrawCircle(origin.x, origin.y, 30, semi_red);
            DrawLineEx(origin, point, 20, semi_red);
            DrawCircle(point.x, point.y, 50, semi_red);
          }
      });
    });

  ecs.system<TargetSelector>()
    .each([&](TargetSelector &ts)
    {
      if (!IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        return;
      Vector2 mouse = GetMousePosition();
      mouse = Vector2{mouse.x - ts.camera->offset.x, mouse.y - ts.camera->offset.y};
      mouse = Vector2{mouse.x / ts.camera->zoom, mouse.y / ts.camera->zoom};
      mouse = Vector2{mouse.x + ts.camera->target.x, mouse.y + ts.camera->target.y};
      int tile_x = int(mouse.x / tile_size);
      int tile_y = int(mouse.y / tile_size);

      bool isWall = false;
      dungeonDataQuery.each([&](const DungeonData &dd)
      {
        isWall = dd.tiles[tile_y * dd.width + tile_x] == dungeon::wall;
      });
      if (tile_x >= 0 && tile_x < ts.w && tile_y >= 0 && tile_y < ts.w && !isWall)
      {
        bool samePlace = false;
        if (ts.target.is_alive()) {
          ts.target.get([&](const Position& pos) {
            samePlace = pos.x == tile_x && pos.y == tile_y;
          });
          ts.target.destruct();
        }
        if (samePlace) {
          ecs.entity("target_map").destruct();
          ecs.entity("flow_map").destruct();
          return;
        }
        ts.target = ecs.entity()
          .set(Position{tile_x, tile_y})
          .set(Color{0xff, 0xff, 0xff, 0xff})
          .add<Target>()
          .add<TextureSource>(ecs.entity("target_tex"));

        std::vector<float> targetMap;
        dmaps::gen_to_target_map(ecs, targetMap, tile_x, tile_y);
        ecs.entity("target_map")
          .set(DijkstraMapData{targetMap})
          .add<VisualiseMap>();

        std::vector<Vector2> flowMap = dmaps::gen_flow_map(targetMap, ts.w, ts.h, step_count);
        ecs.entity("flow_map")
          .set(FlowMapData{flowMap, ts.w, ts.h})
          .add<VisualiseMap>();
      }
    });
}


void init_roguelike(flecs::world &ecs)
{
  register_roguelike_systems(ecs);

  ecs.entity("minotaur_tex")
    .set(Texture2D{LoadTexture("assets/minotaur.png")});
  ecs.entity("target_tex")
    .set(Texture2D{LoadTexture("assets/target.png")});

  ecs.observer<Texture2D>()
    .event(flecs::OnRemove)
    .each([](Texture2D texture)
      {
        UnloadTexture(texture);
      });

  const int monsters = 30;
  for (int i = 0; i < monsters; ++i)
  {
    Position pos = find_free_dungeon_tile(ecs);
    steer::create_go_with_the_flow_er(create_monster(ecs, pos, Color{0x1f, 0xaf, 0xff, 0xff}, "minotaur_tex"));
  }

  // query creation inside of another query does not work
  dmaps::init_query_dungeon_data(ecs);
}

void init_dungeon(flecs::world &ecs, char *tiles, size_t w, size_t h)
{
  flecs::entity wallTex = ecs.entity("wall_tex")
    .set(Texture2D{LoadTexture("assets/wall.png")});
  flecs::entity floorTex = ecs.entity("floor_tex")
    .set(Texture2D{LoadTexture("assets/floor.png")});

  std::vector<char> dungeonData;
  dungeonData.resize(w * h);
  for (size_t y = 0; y < h; ++y)
    for (size_t x = 0; x < w; ++x)
      dungeonData[y * w + x] = tiles[y * w + x];
  ecs.entity("dungeon")
    .set(DungeonData{dungeonData, w, h});

  for (size_t y = 0; y < h; ++y)
    for (size_t x = 0; x < w; ++x)
    {
      char tile = tiles[y * w + x];
      flecs::entity tileEntity = ecs.entity()
        .add<BackgroundTile>()
        .set(Position{int(x), int(y)})
        .set(Color{255, 255, 255, 255});
      if (tile == dungeon::wall)
        tileEntity.add<TextureSource>(wallTex);
      else if (tile == dungeon::floor)
        tileEntity.add<TextureSource>(floorTex);
    }
}
