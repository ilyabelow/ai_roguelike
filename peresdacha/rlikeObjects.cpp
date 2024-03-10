#include "rlikeObjects.h"
#include "ecsTypes.h"

flecs::entity create_monster(flecs::world &ecs, Position pos, Color col, const char *texture_src)
{
  flecs::entity textureSrc = ecs.entity(texture_src);
  return ecs.entity()
    .set(Position{pos.x, pos.y})
    .set(Velocity{0.f, 0.f})
    .set(MoveSpeed{5.f})
    .set(Hitpoints{100.f})
    .set(Action{EA_NOP})
    .set(Color{col})
    .add<TextureSource>(textureSrc)
    .set(Team{1})
    .set(NumActions{1, 0})
    .set(MeleeDamage{20.f})
    .set(SteerDir{0.f, 0.f})
    .set(SteerAccel{2.f});
}
