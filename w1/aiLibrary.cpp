#include "aiLibrary.h"
#include <flecs.h>
#include "ecsTypes.h"
#include "raylib.h"
#include <cfloat>
#include <cmath>

class AttackEnemyState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &/*ecs*/, flecs::entity /*entity*/) const override {}
};

template<typename T>
T sqr(T a){ return a*a; }

template<typename T, typename U>
static float dist_sq(const T &lhs, const U &rhs) { return float(sqr(lhs.x - rhs.x) + sqr(lhs.y - rhs.y)); }

template<typename T, typename U>
static float dist(const T &lhs, const U &rhs) { return sqrtf(dist_sq(lhs, rhs)); }

template<typename T, typename U>
static int move_towards(const T &from, const U &to)
{
  int deltaX = to.x - from.x;
  int deltaY = to.y - from.y;
  if (abs(deltaX) > abs(deltaY))
    return deltaX > 0 ? EA_MOVE_RIGHT : EA_MOVE_LEFT;
  return deltaY < 0 ? EA_MOVE_UP : EA_MOVE_DOWN;
}

static int inverse_move(int move)
{
  return move == EA_MOVE_LEFT ? EA_MOVE_RIGHT :
         move == EA_MOVE_RIGHT ? EA_MOVE_LEFT :
         move == EA_MOVE_UP ? EA_MOVE_DOWN :
         move == EA_MOVE_DOWN ? EA_MOVE_UP : move;
}


template<typename Callable>
static void on_closest_enemy_pos(flecs::world &ecs, flecs::entity entity, Callable c)
{
  static auto enemiesQuery = ecs.query<const Position, const Team>();
  entity.set([&](const Position &pos, const Team &t, Action &a)
  {
    flecs::entity closestEnemy;
    float closestDist = FLT_MAX;
    Position closestPos;
    enemiesQuery.each([&](flecs::entity enemy, const Position &epos, const Team &et)
    {
      if (t.team == et.team)
        return;
      float curDist = dist(epos, pos);
      if (curDist < closestDist)
      {
        closestDist = curDist;
        closestPos = epos;
        closestEnemy = enemy;
      }
    });
    if (ecs.is_valid(closestEnemy))
      c(a, pos, closestPos);
  });
}

class MoveToEnemyState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    on_closest_enemy_pos(ecs, entity, [&](Action &a, const Position &pos, const Position &enemy_pos)
    {
      a.action = move_towards(pos, enemy_pos);
    });
  }
};

class FleeFromEnemyState : public State
{
public:
  FleeFromEnemyState() {}
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    on_closest_enemy_pos(ecs, entity, [&](Action &a, const Position &pos, const Position &enemy_pos)
    {
      a.action = inverse_move(move_towards(pos, enemy_pos));
    });
  }
};

class PatrolState : public State
{
  float patrolDist;
public:
  PatrolState(float dist) : patrolDist(dist) {}
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    entity.set([&](const Position &pos, const PatrolPos &ppos, Action &a)
    {
      if (dist(pos, ppos) > patrolDist)
        a.action = move_towards(pos, ppos); // do a recovery walk
      else
      {
        // do a random walk
        a.action = GetRandomValue(EA_MOVE_START, EA_MOVE_END - 1);
      }
    });
  }
};

class NopState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override {}
};

class HealState : public State
{
private:
  float heal_speed;
public:
  HealState(float heal_speed) : heal_speed(heal_speed) {}
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override {
    entity.set([&](Hitpoints &hitpoints, Action &a)
    {
      a.action = EA_HEAL; // not required
      hitpoints.hitpoints += heal_speed; 
    });
  }
};

class FindMasterState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override {
    entity.set([&](const Position pos, const Team team){
      flecs::entity closest_ally;
      float min_dist = MAXFLOAT;
      ecs.query<const Position, const Team>().each([&](flecs::entity other_entity, const Position other_pos, const Team other_team){
        if (other_entity == entity)
          return;
        if (team.team == other_team.team && dist(pos, other_pos) < min_dist) {
          min_dist = dist(pos, other_pos);
          closest_ally = other_entity;
        }
      });
      if (min_dist < MAXFLOAT) {
        entity.set<Master>({closest_ally});
      }
    });
  }
};


class MoveToAllyState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    entity.set([&](const Master master, const Position pos, Action& action){
      master.master.get([&](const Position master_pos){
        action.action = move_towards(pos, master_pos);
      });
    });
  }
};


class HealMasterState : public State
{
private:
  float heal_amount;
public:
  HealMasterState(float heal_amount) : heal_amount(heal_amount) {}
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override {
    entity.set([&](Action &a, Master master, HealCooldown& cooldown)
    {
      if (cooldown.turns_left > 0)
        return;
      cooldown.turns_left = cooldown.recharge_turns;
      a.action = EA_HEAL; // not required ig
      master.master.set([&](Hitpoints &hitpoints){
        hitpoints.hitpoints += heal_amount; 
      });
    });
  }
};

class EnemyAvailableTransition : public StateTransition
{
  float triggerDist;
public:
  EnemyAvailableTransition(float in_dist) : triggerDist(in_dist) {}
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    static auto enemiesQuery = ecs.query<const Position, const Team>();
    bool enemiesFound = false;
    entity.get([&](const Position &pos, const Team &t)
    {
      enemiesQuery.each([&](flecs::entity enemy, const Position &epos, const Team &et)
      {
        if (t.team == et.team)
          return;
        float curDist = dist(epos, pos);
        enemiesFound |= curDist <= triggerDist;
      });
    });
    return enemiesFound;
  }
};

class HitpointsLessThanTransition : public StateTransition
{
  float threshold;
public:
  HitpointsLessThanTransition(float in_thres) : threshold(in_thres) {}
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    bool hitpointsThresholdReached = false;
    entity.get([&](const Hitpoints &hp)
    {
      hitpointsThresholdReached |= hp.hitpoints < threshold;
    });
    return hitpointsThresholdReached;
  }
};

class EnemyReachableTransition : public StateTransition
{
public:
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    return false;
  }
};

class NegateTransition : public StateTransition
{
  const StateTransition *transition; // we own it
public:
  NegateTransition(const StateTransition *in_trans) : transition(in_trans) {}
  ~NegateTransition() override { delete transition; }

  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    return !transition->isAvailable(ecs, entity);
  }
};

class AndTransition : public StateTransition
{
  const StateTransition *lhs; // we own it
  const StateTransition *rhs; // we own it
public:
  AndTransition(const StateTransition *in_lhs, const StateTransition *in_rhs) : lhs(in_lhs), rhs(in_rhs) {}
  ~AndTransition() override
  {
    delete lhs;
    delete rhs;
  }

  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    return lhs->isAvailable(ecs, entity) && rhs->isAvailable(ecs, entity);
  }
};

class HasMasterTransition : public StateTransition
{
public:
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    return entity.has<Master>();
  }
};

class MasterHitpointsLessThanTransition : public StateTransition
{
  float threshold;
public:
  MasterHitpointsLessThanTransition(float in_thres) : threshold(in_thres) {}
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {   
    bool hitpointsThresholdReached = false;

    entity.set([&](Master master){
      master.master.get([&](const Hitpoints &hp)  {
        hitpointsThresholdReached |= hp.hitpoints < threshold;
      });
    });

    return hitpointsThresholdReached;
  }
};


class HealCooledDownTransition : public StateTransition
{
public:
  bool isAvailable(flecs::world &ecs, flecs::entity entity) const override
  {
    bool cooled = false;
    entity.set([&](const HealCooldown cooldown){
      cooled = cooldown.turns_left == 0;
    });
    return cooled;
  }
};

// states
State *create_attack_enemy_state()
{
  return new AttackEnemyState();
}
State *create_move_to_enemy_state()
{
  return new MoveToEnemyState();
}

State *create_flee_from_enemy_state()
{
  return new FleeFromEnemyState();
}


State *create_patrol_state(float patrol_dist)
{
  return new PatrolState(patrol_dist);
}

State *create_nop_state()
{
  return new NopState();
}

State *create_heal_state(float heal_speed)
{
  return new HealState(heal_speed);
}

State *create_find_master_state()
{
  return new FindMasterState();
}

State *create_move_to_ally_state()
{
  return new MoveToAllyState();
}

State *create_heal_master_state(float heal_amount)
{
  return new HealMasterState(heal_amount);
}

// transitions
StateTransition *create_enemy_available_transition(float dist)
{
  return new EnemyAvailableTransition(dist);
}

StateTransition *create_enemy_reachable_transition()
{
  return new EnemyReachableTransition();
}

StateTransition *create_hitpoints_less_than_transition(float thres)
{
  return new HitpointsLessThanTransition(thres);
}

StateTransition *create_negate_transition(StateTransition *in)
{
  return new NegateTransition(in);
}
StateTransition *create_and_transition(StateTransition *lhs, StateTransition *rhs)
{
  return new AndTransition(lhs, rhs);
}

StateTransition *create_has_master_transition()
{
  return new HasMasterTransition();
}

StateTransition *create_master_hitpoints_less_than_transition(float thres)
{
  return new MasterHitpointsLessThanTransition(thres);
}


StateTransition *create_heal_cooleddown_transition() {
  return new HealCooledDownTransition();
}
