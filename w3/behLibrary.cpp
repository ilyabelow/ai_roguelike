#include "aiLibrary.h"
#include "ecsTypes.h"
#include "aiUtils.h"
#include "math.h"
#include "raylib.h"
#include "blackboard.h"
#include <algorithm>

struct CompoundNode : public BehNode
{
  std::vector<BehNode*> nodes;

  virtual ~CompoundNode()
  {
    for (BehNode *node : nodes)
      delete node;
    nodes.clear();
  }

  CompoundNode &pushNode(BehNode *node)
  {
    nodes.push_back(node);
    return *this;
  }
};

struct Sequence : public CompoundNode
{
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    for (BehNode *node : nodes)
    {
      BehResult res = node->update(ecs, entity, bb);
      if (res != BEH_SUCCESS)
        return res;
    }
    return BEH_SUCCESS;
  }
};

struct Selector : public CompoundNode
{
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    for (BehNode *node : nodes)
    {
      BehResult res = node->update(ecs, entity, bb);
      if (res != BEH_FAIL)
        return res;
    }
    return BEH_FAIL;
  }
};

struct UtilitySelector : public BehNode
{
  std::vector<std::pair<BehNode*, utility_function>> utilityNodes;
  bool random = false;
  bool cooldown = false;
  const float cooldownSpeed = 0.7;
  struct {
    float additional = 0.f;
    size_t idx = -1;
  } cooldownState;

  UtilitySelector(bool random, bool cooldown) : random(random), cooldown(cooldown) {}

  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    if (random)
    {
      std::vector<float> utilityScores;
      size_t nonZeroUtilities = 0;
      for (size_t i = 0; i < utilityNodes.size(); ++i)
      {
        const float utilityScore = getScore(i, bb);
        if (utilityScore > 0)
          nonZeroUtilities++;
        utilityScores.push_back(utilityScore);
      }
      while (nonZeroUtilities > 0)
      {
        size_t nodeIdx = weighted_random(utilityScores.data(), utilityScores.size());
        BehResult res = tryUpdate(nodeIdx, ecs, entity, bb);
        if (res != BEH_FAIL)
          return res;
        utilityScores[nodeIdx] = 0.;
        nonZeroUtilities--;
      }
    } else {
      std::vector<std::pair<float, size_t>> utilityScores;
      for (size_t i = 0; i < utilityNodes.size(); ++i)
      {
        const float utilityScore = getScore(i, bb);
        utilityScores.push_back(std::make_pair(utilityScore, i));
      }
      std::sort(utilityScores.begin(), utilityScores.end(), [](auto &lhs, auto &rhs)
      {
        return lhs.first > rhs.first;
      });
      for (const std::pair<float, size_t> &node : utilityScores)
      {
        size_t nodeIdx = node.second;
        BehResult res = tryUpdate(nodeIdx, ecs, entity, bb);
        if (res != BEH_FAIL)
          return res;
      }
    }
    return BEH_FAIL;
  }
private:
  float getScore(size_t i, Blackboard &bb) {
    float add = cooldown && cooldownState.idx == i ? cooldownState.additional : 0.f;
    return utilityNodes[i].second(bb) + add;
  }

  BehResult tryUpdate(size_t i, flecs::world &ecs, flecs::entity entity, Blackboard &bb) {
    BehResult res = utilityNodes[i].first->update(ecs, entity, bb);
    if (res != BEH_FAIL && cooldown) {
      if (cooldownState.idx != i) {
        cooldownState.idx = i;
        cooldownState.additional = 100.f;
      } else {
        cooldownState.additional *= cooldownSpeed;
        if (cooldownState.additional <= 10.f)
          cooldownState.idx = -1;
      }
    }
    return res;
  }
};

struct MoveToEntity : public BehNode
{
  size_t entityBb = size_t(-1); // wraps to 0xff...
  MoveToEntity(flecs::entity entity, const char *bb_name)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.set([&](Action &a, const Position &pos)
    {
      flecs::entity targetEntity = bb.get<flecs::entity>(entityBb);
      if (!targetEntity.is_alive())
      {
        res = BEH_FAIL;
        return;
      }
      targetEntity.get([&](const Position &target_pos)
      {
        if (pos != target_pos)
        {
          a.action = move_towards(pos, target_pos);
          res = BEH_RUNNING;
        }
        else
          res = BEH_SUCCESS;
      });
    });
    return res;
  }
};

struct IsLowHp : public BehNode
{
  float threshold = 0.f;
  IsLowHp(float thres) : threshold(thres) {}

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &) override
  {
    BehResult res = BEH_SUCCESS;
    entity.get([&](const Hitpoints &hp)
    {
      res = hp.hitpoints < threshold ? BEH_SUCCESS : BEH_FAIL;
    });
    return res;
  }
};

struct FindEnemy : public BehNode
{
  size_t entityBb = size_t(-1);
  float distance = 0;
  FindEnemy(flecs::entity entity, float in_dist, const char *bb_name) : distance(in_dist)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_FAIL;
    static auto enemiesQuery = ecs.query<const Position, const Team>();
    entity.set([&](const Position &pos, const Team &t)
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
      if (ecs.is_valid(closestEnemy) && closestDist <= distance)
      {
        bb.set<flecs::entity>(entityBb, closestEnemy);
        res = BEH_SUCCESS;
      }
    });
    return res;
  }
};

struct Flee : public BehNode
{
  size_t entityBb = size_t(-1);
  Flee(flecs::entity entity, const char *bb_name)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.set([&](Action &a, const Position &pos)
    {
      flecs::entity targetEntity = bb.get<flecs::entity>(entityBb);
      if (!targetEntity.is_alive())
      {
        res = BEH_FAIL;
        return;
      }
      targetEntity.get([&](const Position &target_pos)
      {
        a.action = inverse_move(move_towards(pos, target_pos));
      });
    });
    return res;
  }
};

struct Patrol : public BehNode
{
  size_t pposBb = size_t(-1);
  float patrolDist = 1.f;
  Patrol(flecs::entity entity, float patrol_dist, const char *bb_name)
    : patrolDist(patrol_dist)
  {
    pposBb = reg_entity_blackboard_var<Position>(entity, bb_name);
    entity.set([&](Blackboard &bb, const Position &pos)
    {
      bb.set<Position>(pposBb, pos);
    });
  }

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.set([&](Action &a, const Position &pos)
    {
      Position patrolPos = bb.get<Position>(pposBb);
      if (dist(pos, patrolPos) > patrolDist)
        a.action = move_towards(pos, patrolPos);
      else
        a.action = GetRandomValue(EA_MOVE_START, EA_MOVE_END - 1); // do a random walk
    });
    return res;
  }
};

struct PatchUp : public BehNode
{
  float hpThreshold = 100.f;
  PatchUp(float threshold) : hpThreshold(threshold) {}

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &) override
  {
    BehResult res = BEH_SUCCESS;
    entity.set([&](Action &a, Hitpoints &hp)
    {
      if (hp.hitpoints >= hpThreshold)
        return;
      res = BEH_RUNNING;
      a.action = EA_HEAL_SELF;
    });
    return res;
  }
};

struct StikyExplore : public BehNode
{
  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    entity.set([&](Action &a)
    {
      // left, right, down, up
      std::array<float, 4> weights;
      const float same_dir = 1.f;
      const float opp_dir = 0.1f;
      const float prerp_dir = 0.4f;
      switch (a.action)
      {
      case EA_MOVE_LEFT:
        //            L      R     D     U
        weights = { same_dir, opp_dir,   prerp_dir,  prerp_dir };
        break;
      case EA_MOVE_RIGHT:
        //            L      R     D     U
        weights = { opp_dir, same_dir,   prerp_dir,  prerp_dir };
        break;
      case EA_MOVE_DOWN:
        //            L      R     D     U
        weights = { prerp_dir, prerp_dir, same_dir,   opp_dir };
        break;
      case EA_MOVE_UP:
        //            L      R     D     U
        weights = { prerp_dir, prerp_dir, opp_dir, same_dir };
        break;
      default:
        //            L      R     D     U
        weights = { 0.25f, 0.25f, 0.25f, 0.25f };
        break;
      }
      a.action = EA_MOVE_START + weighted_random(weights.data(), weights.size());
    });

    return BEH_RUNNING;
  }
};


BehNode *sequence(const std::vector<BehNode*> &nodes)
{
  Sequence *seq = new Sequence;
  for (BehNode *node : nodes)
    seq->pushNode(node);
  return seq;
}

BehNode *selector(const std::vector<BehNode*> &nodes)
{
  Selector *sel = new Selector;
  for (BehNode *node : nodes)
    sel->pushNode(node);
  return sel;
}

BehNode *utility_selector(const std::vector<std::pair<BehNode*, utility_function>> &nodes, bool random, bool cooldown)
{
  UtilitySelector *usel = new UtilitySelector(random, cooldown);
  usel->utilityNodes = std::move(nodes);
  return usel;
}

BehNode *move_to_entity(flecs::entity entity, const char *bb_name)
{
  return new MoveToEntity(entity, bb_name);
}

BehNode *is_low_hp(float thres)
{
  return new IsLowHp(thres);
}

BehNode *find_enemy(flecs::entity entity, float dist, const char *bb_name)
{
  return new FindEnemy(entity, dist, bb_name);
}

BehNode *flee(flecs::entity entity, const char *bb_name)
{
  return new Flee(entity, bb_name);
}

BehNode *patrol(flecs::entity entity, float patrol_dist, const char *bb_name)
{
  return new Patrol(entity, patrol_dist, bb_name);
}

BehNode *patch_up(float thres)
{
  return new PatchUp(thres);
}

BehNode *stiky_explore()
{
  return new StikyExplore;
}
