#pragma once

#include <flecs.h>
#include <memory>
#include "blackboard.h"

enum BehResult
{
  BEH_SUCCESS,
  BEH_FAIL,
  BEH_RUNNING
};

enum Reaction
{
  ROAR
};


struct BehNode
{
  virtual ~BehNode() {}
  virtual bool react(Reaction reaction, Blackboard &bb, bool search_deeper)
  {
    return false;
  }
  virtual BehResult update(flecs::world &ecs, flecs::entity entity, Blackboard &bb) = 0;
};

struct BehaviourTree
{
  std::unique_ptr<BehNode> root = nullptr;

  BehaviourTree() = default;
  BehaviourTree(BehNode *r) : root(r) {}

  BehaviourTree(const BehaviourTree &bt) = delete;
  BehaviourTree(BehaviourTree &&bt) = default;

  BehaviourTree &operator=(const BehaviourTree &bt) = delete;
  BehaviourTree &operator=(BehaviourTree &&bt) = default;

  ~BehaviourTree() = default;

  void update(flecs::world &ecs, flecs::entity entity, Blackboard &bb)
  {
    root->update(ecs, entity, bb);
  }
  void react(Reaction reaction, Blackboard &bb)
  {
    root->react(reaction, bb, true);
  }
};

