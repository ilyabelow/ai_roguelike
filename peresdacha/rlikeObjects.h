#pragma once
#include <flecs.h>
#include "raylib.h"
#include "ecsTypes.h"

flecs::entity create_monster(flecs::world &ecs, Position pos, Color col, const char *texture_src);
