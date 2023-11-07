#pragma once

#include "stateMachine.h"

// states
State *create_attack_enemy_state();
State *create_move_to_enemy_state();
State *create_flee_from_enemy_state();
State *create_patrol_state(float patrol_dist);
State *create_nop_state();
State *create_heal_state(float health_per_turn);
State *create_find_master_state();
State *create_move_to_ally_state();
State *create_heal_master_state(float heal_amount);

// transitions
StateTransition *create_enemy_available_transition(float dist);
StateTransition *create_enemy_reachable_transition();
StateTransition *create_hitpoints_less_than_transition(float thres);
StateTransition *create_negate_transition(StateTransition *in);
StateTransition *create_and_transition(StateTransition *lhs, StateTransition *rhs);
StateTransition *create_has_master_transition();
StateTransition *create_master_hitpoints_less_than_transition(float thres);
StateTransition *create_heal_cooleddown_transition();

