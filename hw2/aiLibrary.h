#pragma once

#include "stateMachine.h"
#include "behaviourTree.h"

// states
State *create_attack_enemy_state();
State *create_move_to_enemy_state();
State *create_flee_from_enemy_state();
State *create_patrol_state(float patrol_dist);
State *create_nop_state();

// transitions
StateTransition *create_enemy_available_transition(float dist);
StateTransition *create_enemy_reachable_transition();
StateTransition *create_hitpoints_less_than_transition(float thres);
StateTransition *create_negate_transition(StateTransition *in);
StateTransition *create_and_transition(StateTransition *lhs, StateTransition *rhs);

BehNode *sequence(const std::vector<BehNode*> &nodes);
BehNode *selector(const std::vector<BehNode*> &nodes);
BehNode *or_node(const std::vector<BehNode*> &nodes);
BehNode *parallel(const std::vector<BehNode*> &nodes);
BehNode *not_node(BehNode *node);

BehNode *move_to_entity(flecs::entity entity, const char *bb_name);
BehNode *check_waypoint(flecs::entity entity, const char *bb_name);
BehNode *is_low_hp(float thres);
BehNode *find_enemy(flecs::entity entity, float dist, const char *bb_name);
BehNode *find_pick_up(flecs::entity entity, const char *bb_name);
BehNode *flee(flecs::entity entity, const char *bb_name);
BehNode *patrol(flecs::entity entity, float patrol_dist, const char *bb_name);
BehNode *roar(flecs::entity entity, float roar_dist, const char *bb_name, const char *bb_enemy);
BehNode *react_roar(flecs::entity entity, const char *payload_bb_name, const char *react_bb_name);

