#include "aiLibrary.h"
#include <flecs.h>
#include "ecsTypes.h"
#include <bx/rng.h>

static bx::RngShr3 rng;

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
  return deltaY > 0 ? EA_MOVE_UP : EA_MOVE_DOWN;
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
        a.action = EA_MOVE_START + (rng.gen() % (EA_MOVE_END - EA_MOVE_START));
      }
    });
  }
};

class HealState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    entity.set([&](Hitpoints &hp, HealPotion &potion)
    {
      hp.hitpoints += potion.amount;
      potion.count--;
    });
  }
};

class HealPlayerState : public State
{
public:
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world &ecs, flecs::entity entity) const override
  {
    static auto turn = ecs.query<const Turn>();
    static auto player = ecs.query<const IsPlayer, Hitpoints>();
    entity.set([&](HealSpell& hs)
    {
      turn.each([&](const Turn& turn)
      {
        hs.lastUse = turn.turn;
      });
      player.each([&](const IsPlayer&, Hitpoints& hp)
      {
        hp.hitpoints = hs.amount;
      });
    });    
  }
};

class PatrolPlayerState : public State
{
  float patrolDist;
public:
  PatrolPlayerState(float dist) : patrolDist(dist) {}
  void enter() const override {}
  void exit() const override {}
  void act(float/* dt*/, flecs::world& ecs, flecs::entity entity) const override
  {
    static auto player = ecs.query<const IsPlayer, const Position>();
    entity.set([&](const Position& pos, PatrolPos& ppos, Action& a)
    {
      player.each([&](const IsPlayer&, const Position& playerPos)
      {
        ppos.x = playerPos.x;
        ppos.y = playerPos.y;
      });
      if (dist(pos, ppos) > patrolDist)
        a.action = move_towards(pos, ppos); // do a recovery walk
      else
      {
        // do a random walk
        a.action = EA_MOVE_START + (rng.gen() % (EA_MOVE_END - EA_MOVE_START));
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

class PlayerAvailableTransition : public StateTransition
{
  float triggerDist;
public:
  PlayerAvailableTransition(float in_dist) : triggerDist(in_dist) {}
  bool isAvailable(flecs::world& ecs, flecs::entity entity) const override
  {
    static auto player = ecs.query<const IsPlayer, const Position>();
    bool playerFound = false;
    entity.get([&](const Position& pos, const Team& t)
    {
      player.each([&](flecs::entity player, const IsPlayer&, const Position& ppos)
      {
        float curDist = dist(ppos, pos);
        playerFound |= curDist <= triggerDist;
      });
    });
    return playerFound;
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

class PlayerHitpointsLessThenTransition : public StateTransition
{
  float threshold;
public:
  PlayerHitpointsLessThenTransition(float in_thres) : threshold(in_thres) {}
  bool isAvailable(flecs::world& ecs, flecs::entity entity) const override
  {
    static auto player = ecs.query<const IsPlayer, const Hitpoints>();
    bool hitpointsThresholdReached = false;
    player.each([&](const IsPlayer, const Hitpoints &hp)
    {
        hitpointsThresholdReached |= hp.hitpoints < threshold;
    });
    return hitpointsThresholdReached;
  }
};

class IsHealSpellReady : public StateTransition
{
public:
  bool isAvailable(flecs::world& ecs, flecs::entity entity) const override
  {
    static auto turn = ecs.query<const Turn>();
    bool spellReady = false;
    entity.get([&](const HealSpell &hs)
    {
      turn.each([&](const Turn &turn)
      {
        spellReady |= hs.lastUse < 1 || (turn.turn - hs.lastUse >= hs.cooldown);
      });
    });
    return spellReady;
  }
};

class HavePotionTransition : public StateTransition
{
public:
  bool isAvailable(flecs::world& ecs, flecs::entity entity) const override
  {
    bool havePotion = false;
    entity.get([&](const HealPotion& potion)
    {
        havePotion |= potion.count > 0;
    });
    return havePotion;
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

State *create_heal_state()
{
  return new HealState();
}

State *create_patrol_state(float patrol_dist)
{
  return new PatrolState(patrol_dist);
}

State *create_nop_state()
{
  return new NopState();
}

State *create_patrol_player_state(float dist)
{
  return new PatrolPlayerState(dist);
}

State *create_heal_player_state()
{
  return new HealPlayerState();
}

// transitions
StateTransition *create_enemy_available_transition(float dist)
{
  return new EnemyAvailableTransition(dist);
}

StateTransition *create_player_available_transition(float dist)
{
  return new PlayerAvailableTransition(dist);
}

StateTransition *create_enemy_reachable_transition()
{
  return new EnemyReachableTransition();
}

StateTransition *create_hitpoints_less_than_transition(float thres)
{
  return new HitpointsLessThanTransition(thres);
}

StateTransition *create_player_hitpoints_less_than_transition(float thres)
{
  return new PlayerHitpointsLessThenTransition(thres);
}

StateTransition *create_is_heal_spell_ready_transition()
{
  return new IsHealSpellReady();
}

StateTransition *create_have_potion_transition()
{
  return new HavePotionTransition();
}

StateTransition *create_negate_transition(StateTransition *in)
{
  return new NegateTransition(in);
}
StateTransition *create_and_transition(StateTransition *lhs, StateTransition *rhs)
{
  return new AndTransition(lhs, rhs);
}

