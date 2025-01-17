/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "LocalDce.h"

#include <array>
#include <iostream>
#include <unordered_set>
#include <vector>

#include "ControlFlow.h"
#include "DexClass.h"
#include "DexUtil.h"
#include "IRCode.h"
#include "IRInstruction.h"
#include "MethodOverrideGraph.h"
#include "Purity.h"
#include "Resolver.h"
#include "Transform.h"
#include "TypeSystem.h"
#include "Walkers.h"

namespace {

/*
 * These instructions have observable side effects so must always be considered
 * live, regardless of whether their output is consumed by another instruction.
 */
static bool has_side_effects(IROpcode opc) {
  switch (opc) {
  case OPCODE_RETURN_VOID:
  case OPCODE_RETURN:
  case OPCODE_RETURN_WIDE:
  case OPCODE_RETURN_OBJECT:
  case OPCODE_MONITOR_ENTER:
  case OPCODE_MONITOR_EXIT:
  case OPCODE_FILL_ARRAY_DATA:
  case OPCODE_THROW:
  case OPCODE_GOTO:
  case OPCODE_SWITCH:
  case OPCODE_IF_EQ:
  case OPCODE_IF_NE:
  case OPCODE_IF_LT:
  case OPCODE_IF_GE:
  case OPCODE_IF_GT:
  case OPCODE_IF_LE:
  case OPCODE_IF_EQZ:
  case OPCODE_IF_NEZ:
  case OPCODE_IF_LTZ:
  case OPCODE_IF_GEZ:
  case OPCODE_IF_GTZ:
  case OPCODE_IF_LEZ:
  case OPCODE_APUT:
  case OPCODE_APUT_WIDE:
  case OPCODE_APUT_OBJECT:
  case OPCODE_APUT_BOOLEAN:
  case OPCODE_APUT_BYTE:
  case OPCODE_APUT_CHAR:
  case OPCODE_APUT_SHORT:
  case OPCODE_IPUT:
  case OPCODE_IPUT_WIDE:
  case OPCODE_IPUT_OBJECT:
  case OPCODE_IPUT_BOOLEAN:
  case OPCODE_IPUT_BYTE:
  case OPCODE_IPUT_CHAR:
  case OPCODE_IPUT_SHORT:
  case OPCODE_SPUT:
  case OPCODE_SPUT_WIDE:
  case OPCODE_SPUT_OBJECT:
  case OPCODE_SPUT_BOOLEAN:
  case OPCODE_SPUT_BYTE:
  case OPCODE_SPUT_CHAR:
  case OPCODE_SPUT_SHORT:
  case OPCODE_INVOKE_VIRTUAL:
  case OPCODE_INVOKE_SUPER:
  case OPCODE_INVOKE_DIRECT:
  case OPCODE_INVOKE_STATIC:
  case OPCODE_INVOKE_INTERFACE:
  case IOPCODE_LOAD_PARAM:
  case IOPCODE_LOAD_PARAM_OBJECT:
  case IOPCODE_LOAD_PARAM_WIDE:
    return true;
  default:
    return false;
  }
  not_reached();
}

template <typename... T>
std::string show(const boost::dynamic_bitset<T...>& bits) {
  std::string ret;
  to_string(bits, ret);
  return ret;
}

/*
 * Update the liveness vector given that `inst` is live.
 */
void update_liveness(const IRInstruction* inst,
                     boost::dynamic_bitset<>& bliveness) {
  // The destination register is killed, so it isn't live before this.
  if (inst->has_dest()) {
    bliveness.reset(inst->dest());
  }
  auto op = inst->opcode();
  // The destination of an `invoke` is its return value, which is encoded as
  // the max position in the bitvector.
  if (is_invoke(op) || is_filled_new_array(op) ||
      inst->has_move_result_pseudo()) {
    bliveness.reset(bliveness.size() - 1);
  }
  // Source registers are live.
  for (size_t i = 0; i < inst->srcs_size(); i++) {
    bliveness.set(inst->src(i));
  }
  // The source of a `move-result` is the return value of the prior call,
  // which is encoded as the max position in the bitvector.
  if (opcode::is_move_result_any(op)) {
    bliveness.set(bliveness.size() - 1);
  }
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

void LocalDce::dce(IRCode* code) {
  bool editable_cfg_built = code->editable_cfg_built();
  if (!editable_cfg_built) {
    code->build_cfg(/* editable */ true);
  }
  auto& cfg = code->cfg();
  const auto& blocks = cfg.blocks_post();
  auto regs = cfg.get_registers_size();
  std::unordered_map<cfg::BlockId, boost::dynamic_bitset<>> liveness;
  for (cfg::Block* b : blocks) {
    liveness.emplace(b->id(), boost::dynamic_bitset<>(regs + 1));
  }
  bool changed;
  std::vector<std::pair<cfg::Block*, IRList::iterator>> dead_instructions;

  TRACE(DCE, 5, "%s", SHOW(cfg));

  // Iterate liveness analysis to a fixed point.
  do {
    changed = false;
    dead_instructions.clear();
    for (auto& b : blocks) {
      auto prev_liveness = liveness.at(b->id());
      auto& bliveness = liveness.at(b->id());
      bliveness.reset();
      TRACE(DCE, 5, "B%lu: %s", b->id(), show(bliveness).c_str());

      // Compute live-out for this block from its successors.
      for (auto& s : b->succs()) {
        if (s->target()->id() == b->id()) {
          bliveness |= prev_liveness;
        }
        TRACE(DCE,
              5,
              "  S%lu: %s",
              s->target()->id(),
              SHOW(liveness.at(s->target()->id())));
        bliveness |= liveness.at(s->target()->id());
      }

      // Compute live-in for this block by walking its instruction list in
      // reverse and applying the liveness rules.
      for (auto it = b->rbegin(); it != b->rend(); ++it) {
        if (it->type != MFLOW_OPCODE) {
          continue;
        }
        bool required = is_required(cfg, b, it->insn, bliveness);
        if (required) {
          update_liveness(it->insn, bliveness);
        } else {
          // move-result-pseudo instructions will be automatically removed
          // when their primary instruction is deleted.
          if (!opcode::is_move_result_pseudo(it->insn->opcode())) {
            auto forward_it = std::prev(it.base());
            dead_instructions.emplace_back(b, forward_it);
          }
        }
        TRACE(CFG, 5, "%s\n%s", show(it->insn).c_str(),
              show(bliveness).c_str());
      }
      if (bliveness != prev_liveness) {
        changed = true;
      }
    }
  } while (changed);

  // Remove dead instructions.
  std::unordered_set<IRInstruction*> seen;
  for (const auto& pair : dead_instructions) {
    cfg::Block* b = pair.first;
    IRList::iterator it = pair.second;
    if (seen.count(it->insn)) {
      continue;
    }
    TRACE(DCE, 2, "DEAD: %s", SHOW(it->insn));
    seen.emplace(it->insn);
    b->remove_insn(it);
  }
  auto unreachable_insn_count = cfg.remove_unreachable_blocks();
  cfg.recompute_registers_size();

  m_stats.dead_instruction_count += dead_instructions.size();
  m_stats.unreachable_instruction_count += unreachable_insn_count;

  TRACE(DCE, 5, "=== Post-DCE CFG ===");
  TRACE(DCE, 5, "%s", SHOW(cfg));

  if (!editable_cfg_built) {
    code->clear_cfg();
  }
}

/*
 * An instruction is required (i.e., live) if it has side effects or if its
 * destination register is live.
 */
bool LocalDce::is_required(cfg::ControlFlowGraph& cfg,
                           cfg::Block* b,
                           IRInstruction* inst,
                           const boost::dynamic_bitset<>& bliveness) {
  if (has_side_effects(inst->opcode())) {
    if (is_invoke(inst->opcode())) {
      const auto meth =
          resolve_method(inst->get_method(), opcode_to_search(inst));
      if (meth == nullptr) {
        return true;
      }
      if (!assumenosideeffects(inst->get_method(), meth)) {
        return true;
      }
      return bliveness.test(bliveness.size() - 1);
    } else if (is_conditional_branch(inst->opcode())) {
      cfg::Edge* goto_edge = cfg.get_succ_edge_of_type(b, cfg::EDGE_GOTO);
      cfg::Edge* branch_edge = cfg.get_succ_edge_of_type(b, cfg::EDGE_BRANCH);
      always_assert(goto_edge != nullptr);
      always_assert(branch_edge != nullptr);
      return goto_edge->target() != branch_edge->target();
    } else if (is_switch(inst->opcode())) {
      cfg::Edge* goto_edge = cfg.get_succ_edge_of_type(b, cfg::EDGE_GOTO);
      always_assert(goto_edge != nullptr);
      auto branch_edges = cfg.get_succ_edges_of_type(b, cfg::EDGE_BRANCH);
      for (cfg::Edge* branch_edge : branch_edges) {
        if (goto_edge->target() != branch_edge->target()) {
          return true;
        }
      }
      return false;
    }
    return true;
  } else if (inst->has_dest()) {
    return bliveness.test(inst->dest());
  } else if (is_filled_new_array(inst->opcode()) ||
             inst->has_move_result_pseudo()) {
    // These instructions pass their dests via the return-value slot, but
    // aren't inherently live like the invoke-* instructions.
    return bliveness.test(bliveness.size() - 1);
  }
  return false;
}

bool LocalDce::assumenosideeffects(DexMethodRef* ref, DexMethod* meth) {
  if (::assumenosideeffects(meth)) {
    return true;
  }
  return m_pure_methods.find(ref) != m_pure_methods.end();
}
