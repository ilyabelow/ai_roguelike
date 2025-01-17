#include "steering.h"
#include "ecsTypes.h"
#include "roguelike.h"

struct Seeker {};
struct Pursuer {};
struct Evader {};
struct Fleer {};
struct Separation { float threshold = 0.f; float force = 0.f; };
struct Alignment {};
struct Cohesion {};

struct FlowMapFollower {};


flecs::entity steer::create_separation(flecs::entity e, float threshold, float force)
{
  return e.set(Separation{threshold, force});
}

static flecs::entity create_alignment(flecs::entity e)
{
  return e.add<Alignment>();
}

static flecs::entity create_cohesion(flecs::entity e)
{
  return e.add<Cohesion>();
}

static flecs::entity create_steerer(flecs::entity e)
{
  return create_cohesion(
      create_alignment(
        steer::create_separation(e.set(SteerDir{0.f, 0.f}).set(SteerAccel{1.f}), 70.f, 1.f)
        )
      );
}

flecs::entity steer::create_go_with_the_flow_er(flecs::entity e)
{
  return e.add<FlowMapFollower>();
}

flecs::entity steer::create_seeker(flecs::entity e)
{
  return create_steerer(e).add<Seeker>();
}

flecs::entity steer::create_pursuer(flecs::entity e)
{
  return create_steerer(e).add<Pursuer>();
}

flecs::entity steer::create_evader(flecs::entity e)
{
  return create_steerer(e).add<Evader>();
}

flecs::entity steer::create_fleer(flecs::entity e)
{
  return create_steerer(e).add<Fleer>();
}

typedef flecs::entity (*create_foo)(flecs::entity);

flecs::entity steer::create_steer_beh(flecs::entity e, Type type)
{
  create_foo steerFoo[Type::Num] =
  {
    create_seeker,
    create_pursuer,
    create_evader,
    create_fleer
  };
  return steerFoo[type](e);
}


void steer::register_systems(flecs::world &ecs)
{
  static auto playerPosQuery = ecs.query<const Position, const Velocity, const IsPlayer>();
  static auto flowmapQuery = ecs.query<const FlowMapData>();

  ecs.system<Velocity, const MoveSpeed, const SteerDir, const SteerAccel>()
    .each([&](Velocity &vel, const MoveSpeed &ms, const SteerDir &sd, const SteerAccel &sa)
    {
      vel = Velocity{truncate(vel + truncate(sd, ms.speed) * ecs.delta_time() * sa.accel, ms.speed)};
    });

  // reset steer dir
  ecs.system<SteerDir>().each([&](SteerDir &sd) { sd = {0.f, 0.f}; });

  // field steering

  ecs.system<SteerDir, const MoveSpeed, const Velocity, const Position, const FlowMapFollower>()
    .each([&](SteerDir &sd, const MoveSpeed &ms, const Velocity &vel, const Position &p, const FlowMapFollower &)
    {
      flowmapQuery.each([&](const FlowMapData &fmd)
      {
        const float how_far_in_future = .5f;
        Position future = p + vel * how_far_in_future;
        const int x = int(future.x + 0.5);
        const int y = int(future.y + 0.5);
        if (x < 0 || y < 0 || x >= fmd.width || y >= fmd.height)
          return;
        const Vector2 flow = fmd.map[y * fmd.width + x];
        const Position flow_dir{flow.x, flow.y};
        sd += SteerDir{normalize(flow_dir) * ms.speed - vel};
      });
    });

  // seeker
  ecs.system<SteerDir, const MoveSpeed, const Velocity, const Position, const Seeker>()
    .each([&](SteerDir &sd, const MoveSpeed &ms, const Velocity &vel,
              const Position &p, const Seeker &)
    {
      playerPosQuery.each([&](const Position &pp, const Velocity &, const IsPlayer &)
      {
        sd += SteerDir{normalize(pp - p) * ms.speed - vel};
      });
    });

  // fleer
  ecs.system<SteerDir, const MoveSpeed, const Velocity, const Position, const Fleer>()
    .each([&](SteerDir &sd, const MoveSpeed &ms, const Velocity &vel, const Position &p, const Fleer &)
    {
      playerPosQuery.each([&](const Position &pp, const Velocity &, const IsPlayer &)
      {
        sd += SteerDir{normalize(p - pp) * ms.speed - vel};
      });
    });

  // pursuer
  ecs.system<SteerDir, const MoveSpeed, const Velocity, const Position, const Pursuer>()
    .each([&](SteerDir &sd, const MoveSpeed &ms, const Velocity &vel, const Position &p, const Pursuer &)
    {
      playerPosQuery.each([&](const Position &pp, const Velocity &pvel, const IsPlayer &)
      {
        constexpr float predictTime = 4.f;
        const Position targetPos = pp + pvel * predictTime;
        sd += SteerDir{normalize(targetPos - p) * ms.speed - vel};
      });
    });

  // evader
  ecs.system<SteerDir, const MoveSpeed, const Velocity, const Position, const Evader>()
    .each([&](SteerDir &sd, const MoveSpeed &ms, const Velocity &vel, const Position &p, const Evader &)
    {
      playerPosQuery.each([&](const Position &pp, const Velocity &pvel,
                              const IsPlayer &)
      {
        constexpr float maxPredictTime = 4.f;
        const Position dpos = p - pp;
        const float dist = length(dpos);
        const Position dvel = vel - pvel;
        const float dotProduct = (dvel.x * dpos.x + dvel.y * dpos.y) * safeinv(dist);
        const float interceptTime = dotProduct * safeinv(length(dvel));
        const float predictTime = std::max(std::min(maxPredictTime, interceptTime * 0.9f), 1.f);

        const Position targetPos = pp + pvel * predictTime;
        sd += SteerDir{normalize(p - targetPos) * ms.speed - vel};
      });
    });

  static auto otherPosQuery = ecs.query<const Position, const Separation>();

  // separation is expensive!!!
  ecs.system<SteerDir, const Velocity, const MoveSpeed, const Position, const Separation>()
    .each([&](flecs::entity ent, SteerDir &sd, const Velocity &vel, const MoveSpeed &ms,
              const Position &p, const Separation &sep)
    {
      otherPosQuery.each([&](flecs::entity oe, const Position &op, const Separation &)
      {
        if (oe == ent)
          return;
        const float thresDistSq = sep.threshold * sep.threshold;
        const float distSq = length_sq(op - p);
        if (distSq > thresDistSq)
          return;
        sd += SteerDir{(p - op) * safeinv(distSq) * sep.force * ms.speed * sep.threshold - vel};
      });
    });

  static auto otherVelQuery = ecs.query<const Position, const Velocity>();
  ecs.system<SteerDir, const Velocity, const MoveSpeed, const Position, const Alignment>()
    .each([&](flecs::entity ent, SteerDir &sd, const Velocity &vel, const MoveSpeed &ms,
              const Position &p, const Alignment &)
    {
      otherVelQuery.each([&](flecs::entity oe, const Position &op, const Velocity &ovel)
      {
        if (oe == ent)
          return;
        constexpr float thresDist = 100.f;
        constexpr float thresDistSq = thresDist * thresDist;
        const float distSq = length_sq(op - p);
        if (distSq > thresDistSq)
          return;
        sd += SteerDir{ovel * 0.8f};
      });
    });
}

