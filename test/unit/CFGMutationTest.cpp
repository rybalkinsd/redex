/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gtest/gtest.h>

#include "CFGMutation.h"
#include "ControlFlow.h"
#include "DexAsm.h"
#include "IRAssembler.h"
#include "IRInstruction.h"
#include "RedexTest.h"

#include <functional>
#include <iterator>

using namespace cfg;
using namespace dex_asm;
using namespace opcode;

namespace {

/// \return an iterator to the nth instruction in the control flow graph,
///     assuming it exists.
InstructionIterator nth_insn(ControlFlowGraph& cfg, size_t nth) {
  auto ii = cfg::InstructionIterable(cfg);
  auto it = ii.begin();
  std::advance(it, nth);
  return it;
}

/// Test that the mutation to the control flow graph representation of \p actual
/// results in the \p expected IR.
///
/// \p m A function that mutates the CFG given to it.
/// \p actual The actual state of the IR before the mutation has been applied,
///     as an s-expression.
/// \p expected The expected state of the IR after the mutation has been
///     applied, as an s-expression.
void EXPECT_MUTATION(std::function<void(ControlFlowGraph&)> m,
                     const char* actual,
                     const char* expected) {
  auto actual_ir = assembler::ircode_from_string(actual);
  const auto expected_ir = assembler::ircode_from_string(expected);

  actual_ir->build_cfg(/* editable */ true);
  auto& cfg = actual_ir->cfg();

  // Run body of test (that performs the mutation).
  m(cfg);

  // The mutation may introduce more register uses, so recompute them.
  cfg.recompute_registers_size();

  actual_ir->clear_cfg();
  EXPECT_CODE_EQ(expected_ir.get(), actual_ir.get());
}

class CFGMutationTest : public RedexTest {};

TEST_F(CFGMutationTest, InsertBefore) {
  EXPECT_MUTATION(
      [](ControlFlowGraph& cfg) {
        CFGMutation m(cfg);
        m.add_change(CFGMutation::Insert::Before,
                     nth_insn(cfg, 1),
                     {dasm(OPCODE_CONST, {1_v, 1_L})});
      },
      /* ACTUAL */ R"((
        (const v0 0)
        (const v2 2)
        (return-void)
      ))",
      /* EXPECTED */ R"((
        (const v0 0)
        (const v1 1)
        (const v2 2)
        (return-void)
      ))");
}

TEST_F(CFGMutationTest, InsertAfter) {
  EXPECT_MUTATION(
      [](ControlFlowGraph& cfg) {
        CFGMutation m(cfg);
        m.add_change(CFGMutation::Insert::After,
                     nth_insn(cfg, 0),
                     {dasm(OPCODE_CONST, {1_v, 1_L})});
      },
      /* ACTUAL */ R"((
        (const v0 0)
        (const v2 2)
        (return-void)
      ))",
      /* EXPECTED */ R"((
        (const v0 0)
        (const v1 1)
        (const v2 2)
        (return-void)
      ))");
}

TEST_F(CFGMutationTest, Replacing) {
  EXPECT_MUTATION(
      [](ControlFlowGraph& cfg) {
        CFGMutation m(cfg);
        m.add_change(CFGMutation::Insert::Replacing,
                     nth_insn(cfg, 1),
                     {dasm(OPCODE_CONST, {1_v, 1_L})});
      },
      /* ACTUAL */ R"((
        (const v0 0)
        (const v2 2)
        (return-void)
      ))",
      /* EXPECTED */ R"((
        (const v0 0)
        (const v1 1)
        (return-void)
      ))");
}

TEST_F(CFGMutationTest, AdjacentChanges) {
  EXPECT_MUTATION(
      [](ControlFlowGraph& cfg) {
        CFGMutation m(cfg);
        m.add_change(CFGMutation::Insert::After,
                     nth_insn(cfg, 0),
                     {dasm(OPCODE_CONST, {1_v, 1_L})});
        m.add_change(CFGMutation::Insert::After,
                     nth_insn(cfg, 1),
                     {dasm(OPCODE_CONST, {3_v, 3_L})});
      },
      /* ACTUAL */ R"((
        (const v0 0)
        (const v2 2)
        (return-void)
      ))",
      /* EXPECTED */ R"((
        (const v0 0)
        (const v1 1)
        (const v2 2)
        (const v3 3)
        (return-void)
      ))");
}

TEST_F(CFGMutationTest, Flush) {
  EXPECT_MUTATION(
      [](ControlFlowGraph& cfg) {
        CFGMutation m(cfg);
        m.add_change(CFGMutation::Insert::After,
                     nth_insn(cfg, 0),
                     {dasm(OPCODE_CONST, {1_v, 1_L})});
        m.flush();
        m.add_change(CFGMutation::Insert::After,
                     nth_insn(cfg, 2),
                     {dasm(OPCODE_CONST, {3_v, 3_L})});
      },
      /* ACTUAL */ R"((
        (const v0 0)
        (const v2 2)
        (return-void)
      ))",
      /* EXPECTED */ R"((
        (const v0 0)
        (const v1 1)
        (const v2 2)
        (const v3 3)
        (return-void)
      ))");
}

TEST_F(CFGMutationTest, InsertReturn) {
  EXPECT_MUTATION(
      [](ControlFlowGraph& cfg) {
        CFGMutation m(cfg);

        auto const_1 = nth_insn(cfg, 2);
        EXPECT_EQ(const_1->insn->opcode(), OPCODE_CONST);
        EXPECT_EQ(const_1->insn->get_literal(), 1);

        auto const_2 = nth_insn(cfg, 4);
        EXPECT_EQ(const_2->insn->opcode(), OPCODE_CONST);
        EXPECT_EQ(const_2->insn->get_literal(), 2);

        m.add_change(CFGMutation::Insert::Before,
                     const_2,
                     {dasm(OPCODE_CONST, {1_v, 1_L})});

        m.add_change(
            CFGMutation::Insert::Before, const_1, {dasm(OPCODE_RETURN_VOID)});
      },
      /* ACTUAL */ R"((
        (const v0 0)
        (if-eqz v0 :l1)
        (const v1 1)
        (return-void)
        (:l1)
        (const v2 2)
        (return-void)
      ))",
      /* EXPECTED */ R"((
        (const v0 0)
        (if-eqz v0 :l1)
        (return-void)
        (:l1)
        (const v1 1)
        (const v2 2)
        (return-void)
      ))");
}

TEST_F(CFGMutationTest, InsertMayThrow) {
  DexType* const Object = DexType::make_type("Ljava/lang/Object;");

  // Need a may_throw instruction to work with.
  ASSERT_TRUE(opcode::may_throw(OPCODE_INSTANCE_OF));

  EXPECT_MUTATION(
      [Object](ControlFlowGraph& cfg) {
        CFGMutation m(cfg);

        m.add_change(CFGMutation::Insert::After,
                     nth_insn(cfg, 0),
                     {dasm(OPCODE_INSTANCE_OF, Object, {0_v}),
                      dasm(IOPCODE_MOVE_RESULT_PSEUDO, {1_v})});

        m.add_change(CFGMutation::Insert::Replacing,
                     nth_insn(cfg, 1),
                     {dasm(OPCODE_CONST, {2_v, 2_L})});

        m.add_change(CFGMutation::Insert::Before,
                     nth_insn(cfg, 2),
                     {dasm(OPCODE_CONST, {3_v, 3_L})});
      },
      /* ACTUAL */ R"((
        (const v0 0)
        (const v1 1)
        (return-void)
      ))",
      /* EXPECTED */ R"((
        (const v0 0)
        (instance-of v0 "Ljava/lang/Object;")
        (move-result-pseudo v1)
        (const v2 2)
        (const v3 3)
        (return-void)
      ))");
}

TEST_F(CFGMutationTest, ReplaceHasMovePseudo) {
  // When an instruction with an associated move-result-pseudo is replaced, the
  // move-result is also removed. The flushing logic needs to be mindful of this
  // detail.

  EXPECT_MUTATION(
      [](ControlFlowGraph& cfg) {
        CFGMutation m(cfg);

        m.add_change(CFGMutation::Insert::Replacing,
                     nth_insn(cfg, 1),
                     {dasm(OPCODE_CONST, {1_v, 1_L})});
      },
      /* ACTUAL */ R"((
        (const v0 0)
        (instance-of v0 "Ljava/lang/Object;")
        (move-result-pseudo v1)
        (return-void)
      ))",
      /* EXPECTED */ R"((
        (const v0 0)
        (const v1 1)
        (return-void)
      ))");
}

TEST_F(CFGMutationTest, MultipleInsertsAfter) {
  EXPECT_MUTATION(
      [](ControlFlowGraph& cfg) {
        CFGMutation m(cfg);
        m.add_change(CFGMutation::Insert::After,
                     nth_insn(cfg, 0),
                     {dasm(OPCODE_CONST, {1_v, 1_L})});
        m.add_change(CFGMutation::Insert::After,
                     nth_insn(cfg, 0),
                     {dasm(OPCODE_CONST, {2_v, 2_L})});
      },
      /* ACTUAL */ R"((
        (const v0 0)
        (const v3 3)
        (return-void)
      ))",
      /* EXPECTED */ R"((
        (const v0 0)
        (const v1 1)
        (const v2 2)
        (const v3 3)
        (return-void)
      ))");
}

TEST_F(CFGMutationTest, MultipleInsertsBefore) {
  EXPECT_MUTATION(
      [](ControlFlowGraph& cfg) {
        CFGMutation m(cfg);
        m.add_change(CFGMutation::Insert::Before,
                     nth_insn(cfg, 1),
                     {dasm(OPCODE_CONST, {1_v, 1_L})});
        m.add_change(CFGMutation::Insert::Before,
                     nth_insn(cfg, 1),
                     {dasm(OPCODE_CONST, {2_v, 2_L})});
      },
      /* ACTUAL */ R"((
        (const v0 0)
        (const v3 3)
        (return-void)
      ))",
      /* EXPECTED */ R"((
        (const v0 0)
        (const v1 1)
        (const v2 2)
        (const v3 3)
        (return-void)
      ))");
}

TEST_F(CFGMutationTest, MultipleChanges) {
  EXPECT_MUTATION(
      [](ControlFlowGraph& cfg) {
        CFGMutation m(cfg);
        m.add_change(CFGMutation::Insert::After,
                     nth_insn(cfg, 0),
                     {dasm(OPCODE_CONST, {2_v, 2_L})});
        m.add_change(CFGMutation::Insert::Before,
                     nth_insn(cfg, 1),
                     {dasm(OPCODE_CONST, {3_v, 3_L})});
        m.add_change(CFGMutation::Insert::Replacing,
                     nth_insn(cfg, 0),
                     {dasm(OPCODE_CONST, {1_v, 1_L})});
      },
      /* ACTUAL */ R"((
        (const v0 0)
        (return-void)
      ))",
      /* EXPECTED */ R"((
        (const v1 1)
        (const v2 2)
        (const v3 3)
        (return-void)
      ))");
}

TEST_F(CFGMutationTest, InsertBeforeInstanceOf) {
  EXPECT_MUTATION(
      [](ControlFlowGraph& cfg) {
        CFGMutation m(cfg);

        m.add_change(CFGMutation::Insert::Before,
                     nth_insn(cfg, 0),
                     {dasm(OPCODE_CONST, {0_v, 0_L})});
      },
      /* ACTUAL */ R"((
        (instance-of v0 "Ljava/lang/Object;")
        (move-result-pseudo v1)
        (return-void)
      ))",
      /* EXPECTED */ R"((
        (const v0 0)
        (instance-of v0 "Ljava/lang/Object;")
        (move-result-pseudo v1)
        (return-void)
      ))");
}

} // namespace
