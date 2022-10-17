#include "aiLibrary.h"
#include "ecsTypes.h"
#include "aiUtils.h"
#include "math.h"
#include "raylib.h"
#include "blackboard.h"

struct CompoundNode : public BehNode
{
  std::vector<BehNode*> nodes;
  int cached_idx = -1;

  virtual ~CompoundNode()
  {
    for (BehNode *node : nodes)
      delete node;
    nodes.clear();
  }

  void react(Reaction react, Blackboard &bb) override
  {
    for (BehNode *node : nodes)
    {
      node->react(react, bb);
    }
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
    for (int i = 0; i < nodes.size(); ++i)
    {
      BehResult res = nodes[i]->update(ecs, entity, bb);
      if (res != BEH_SUCCESS)
      {
        cached_idx = i;
        return res;
      }
    }
    cached_idx = -1;
    return BEH_SUCCESS;
  }
};

struct Or : public CompoundNode
{
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    for (int i = 0; i < nodes.size(); ++i)
    {
      BehResult res = nodes[i]->update(ecs, entity, bb);
      if (res != BEH_FAIL)
      {
        cached_idx = i;
        return res;
      }
    }
    cached_idx = -1;
    return BEH_FAIL;
  }
};

struct Parallel : public CompoundNode
{
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    for (int i = 0; i < nodes.size(); ++i)
    {
      BehResult res = nodes[i]->update(ecs, entity, bb);
      if (res != BEH_RUNNING)
      {
        cached_idx = i;
        return res;
      }
    }
    cached_idx = -1;
    return BEH_RUNNING;
  }
};

struct Not : public BehNode
{
  std::unique_ptr<BehNode> node;

  Not(BehNode *node) : node(node) {}

  void react(Reaction reaction, Blackboard &bb) override {}

  BehResult update(flecs::world& ecs, flecs::entity entity, Blackboard& bb) override
  {
    BehResult res = node->update(ecs, entity, bb);
    if (res == BEH_SUCCESS)
      return BEH_FAIL;
    if (res == BEH_FAIL)
      return BEH_SUCCESS;
    assert(res != BEH_RUNNING);
    return BEH_FAIL;
  }
};

struct Selector : public CompoundNode
{
  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    for (int i = 0; i < nodes.size(); ++i)
    {
      BehResult res = nodes[i]->update(ecs, entity, bb);
      if (res != BEH_FAIL)
      {
        cached_idx = i;
        return res;
      }
    }
    cached_idx = -1;
    return BEH_FAIL;
  }
};

struct MoveToEntity : public BehNode
{
  size_t entityBb = size_t(-1); // wraps to 0xff...
  MoveToEntity(flecs::entity entity, const char *bb_name)
    : entityBb(reg_entity_blackboard_var<flecs::entity>(entity, bb_name)) {}

  void react(Reaction reaction, Blackboard &bb) override {}

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_RUNNING;
    entity.set([&](Action &a, const Position &pos)
    {
      flecs::entity targetEntity = bb.get<flecs::entity>(entityBb);
      if (!targetEntity.is_alive() || targetEntity.id() == entity.id())
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

  void react(Reaction reaction, Blackboard &bb) override {}

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
  FindEnemy(flecs::entity entity, float in_dist, const char *bb_name) 
    : entityBb(reg_entity_blackboard_var<flecs::entity>(entity, bb_name)), distance(in_dist) {}

  void react(Reaction reaction, Blackboard &bb) override {}

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

struct FindPickUp : public BehNode
{
  size_t nextPickUpBb = size_t(-1);
  FindPickUp(flecs::entity entity, const char* bb_name)
    : nextPickUpBb(reg_entity_blackboard_var<flecs::entity>(entity, bb_name)) {}

  void react(Reaction reaction, Blackboard &bb) override {}

  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_FAIL;
    static auto pickUpQuery = ecs.query<const Position, const IsPickUp>();
    entity.set([&](const Position &pos)
    {
      float closestDist = FLT_MAX;
      Position closestPos;
      flecs::entity closestPickUpEntity;
      pickUpQuery.each([&](flecs::entity pickUpEntity, const Position &hpos, const IsPickUp &)
      {
        float curDist = dist(hpos, pos);
        if (curDist < closestDist)
        {
          closestPos = hpos;
          closestDist = curDist;
          closestPickUpEntity = pickUpEntity;
        }
      });
      if (closestDist < FLT_MAX)
      {
        bb.set<flecs::entity>(nextPickUpBb, closestPickUpEntity);
        res = BEH_SUCCESS;
      }
    });
    return res;
  }
};

struct CheckWaypoint : public BehNode
{
  size_t entityWaypointBb = size_t(-1);
  CheckWaypoint(flecs::entity entity, const char *bb_name)
    : entityWaypointBb(reg_entity_blackboard_var<flecs::entity>(entity, bb_name)) {}

  void react(Reaction reaction, Blackboard &bb) override {}

  BehResult update(flecs::world &, flecs::entity entity, Blackboard &bb) override
  {
    entity.set([&](const Position &pos)
    {
      flecs::entity waypoint = bb.get<flecs::entity>(entityWaypointBb);
      waypoint.get([&](const Position &wpos, const Waypoint &nextWaypoint)
      {
        if (wpos == pos)
        {
          bb.set<flecs::entity>(entityWaypointBb, nextWaypoint.next);
        }
      });
    });
    return BEH_SUCCESS;
  }
};

struct Flee : public BehNode
{
  size_t entityBb = size_t(-1);
  Flee(flecs::entity entity, const char *bb_name)
  {
    entityBb = reg_entity_blackboard_var<flecs::entity>(entity, bb_name);
  }

  void react(Reaction reaction, Blackboard &bb) override {}

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
    : pposBb(reg_entity_blackboard_var<Position>(entity, bb_name)), patrolDist(patrol_dist)
  {
    entity.set([&](Blackboard &bb, const Position &pos)
    {
      bb.set<Position>(pposBb, pos);
    });
  }

  void react(Reaction reaction, Blackboard &bb) override {}

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

struct Roar : public BehNode
{
  size_t roarBb = size_t(-1);
  float roarDist = 0;
  const char *enemy_tag;
  Roar(flecs::entity entity, float roar_dist, const char *bb_name, const char *bb_enemy)
    : roarBb(reg_entity_blackboard_var<flecs::entity>(entity, bb_name)),
      roarDist(roar_dist), enemy_tag(bb_enemy)
  {

  }

  void react(Reaction reaction, Blackboard &bb) override {}

  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    BehResult res = BEH_SUCCESS;
    entity.set([&](const Position &pos)
    {
      flecs::entity targetEntity = bb.get<flecs::entity>(roarBb);
      if (!targetEntity.is_alive())
      {
        res = BEH_FAIL;
        return;
      }
      static auto roarQuery = ecs.query<const Position, BehaviourTree, Blackboard>();
      roarQuery.each([&](flecs::entity e, const Position to_roar_pos, BehaviourTree &bt, Blackboard &to_roar_bb)
      {
        if (dist(pos, to_roar_pos) <= roarDist)
        {
          size_t enemyBb = to_roar_bb.regName<flecs::entity>(enemy_tag);
          to_roar_bb.set<flecs::entity>(enemyBb, targetEntity);
          bt.react(ROAR, to_roar_bb);
        }
      });
    });
    return res;
  }
};

struct ReactRoar : public BehNode
{
  size_t reactBb = size_t(-1);

  ReactRoar(flecs::entity entity, const char *react_bb_name)
    : reactBb(reg_entity_blackboard_var<bool>(entity, react_bb_name)) 
  {
    entity.set([&](Blackboard &bb, const Position &pos)
    {
      bb.set<bool>(reactBb, false);
    });
  }

  void react(Reaction reaction, Blackboard &bb) override
  {
    if (reaction == ROAR)
    {
      bb.set<bool>(reactBb, true);
    }
  }

  BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) override
  {
    bool isReact = bb.get<bool>(reactBb);
    if (isReact)
      return BEH_SUCCESS;
    else
      return BEH_FAIL;
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

BehNode *or_node(const std::vector<BehNode*> &nodes)
{
  Or *orNode = new Or;
  for (BehNode *node : nodes)
    orNode->pushNode(node);
  return orNode;
}

BehNode *parallel(const std::vector<BehNode*> &nodes)
{
  Parallel *parallel = new Parallel;
  for (BehNode *node : nodes)
    parallel->pushNode(node);
  return parallel;
}

BehNode *not_node(BehNode *node)
{
  return new Not(node);  
}

BehNode *move_to_entity(flecs::entity entity, const char *bb_name)
{
  return new MoveToEntity(entity, bb_name);
}

BehNode *check_waypoint(flecs::entity entity, const char *bb_name)
{
  return new CheckWaypoint(entity, bb_name);
}

BehNode *is_low_hp(float thres)
{
  return new IsLowHp(thres);
}

BehNode *find_enemy(flecs::entity entity, float dist, const char *bb_name)
{
  return new FindEnemy(entity, dist, bb_name);
}

BehNode *find_pick_up(flecs::entity entity, const char *bb_name)
{
  return new FindPickUp(entity, bb_name);
}

BehNode *flee(flecs::entity entity, const char *bb_name)
{
  return new Flee(entity, bb_name);
}

BehNode *patrol(flecs::entity entity, float patrol_dist, const char *bb_name)
{
  return new Patrol(entity, patrol_dist, bb_name);
}

BehNode *roar(flecs::entity entity, float roar_dist, const char *bb_name, const char *bb_enemy)
{
  return new Roar(entity, roar_dist, bb_name, bb_enemy);
}

BehNode *react_roar(flecs::entity entity, const char *react_bb_name)
{
  return new ReactRoar(entity, react_bb_name);
}
