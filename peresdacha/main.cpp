//
#include "raylib.h"
#include <flecs.h>
#include <algorithm>
#include "ecsTypes.h"
#include "roguelike.h"
#include "dungeonGen.h"
#include <cmath>

static void update_camera(Camera2D &cam, flecs::world &ecs)
{
  if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON))
  {
    Vector2 delta = GetMouseDelta();
    cam.target.x -= delta.x / cam.zoom;
    cam.target.y -= delta.y / cam.zoom;
  }
  cam.zoom *= std::pow(2.,  GetMouseWheelMove() * 0.5);
}

int main(int /*argc*/, const char ** /*argv*/)
{
  int width = 1920;
  int height = 1080;
  InitWindow(width, height, "peresda AI MIPT");

  const int scrWidth = GetMonitorWidth(0);
  const int scrHeight = GetMonitorHeight(0);
  if (scrWidth < width || scrHeight < height)
  {
    width = std::min(scrWidth, width);
    height = std::min(scrHeight - 150, height);
    SetWindowSize(width, height);
  }

  flecs::world ecs;
  // DUNGeon :)
  constexpr size_t dungWidth = 50;
  constexpr size_t dungHeight = 50;
  {
    char *tiles = new char[dungWidth * dungHeight];
    gen_drunk_dungeon(tiles, dungWidth, dungHeight);
    init_dungeon(ecs, tiles, dungWidth, dungHeight);
  }
  init_roguelike(ecs);

  Camera2D camera = { {0, 0}, {0, 0}, 0.f, 1.f };
  camera.target = Vector2{ dungWidth * 0.5f * tile_size, dungHeight * 0.5f * tile_size};
  camera.offset = Vector2{ width * 0.5f, height * 0.5f };
  camera.rotation = 0.f;
  camera.zoom = 0.04;

  SetTargetFPS(60);               // Set our game to run at 60 frames-per-second
  while (!WindowShouldClose())
  {
    process_turn(ecs);
    update_camera(camera, ecs);

    BeginDrawing();
      ClearBackground(BLACK);
      BeginMode2D(camera);
        ecs.progress();
      EndMode2D();
      print_stats(ecs);
      // Advance to next frame. Process submitted rendering primitives.
    EndDrawing();
  }

  CloseWindow();

  return 0;
}
