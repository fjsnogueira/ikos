/*******************************************************************************
 *
 * \file
 * \brief Dead code checker implementation
 *
 * Author: Maxime Arthaud
 *
 * Contact: ikos@lists.nasa.gov
 *
 * Notices:
 *
 * Copyright (c) 2018 United States Government as represented by the
 * Administrator of the National Aeronautics and Space Administration.
 * All Rights Reserved.
 *
 * Disclaimers:
 *
 * No Warranty: THE SUBJECT SOFTWARE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY OF
 * ANY KIND, EITHER EXPRESSED, IMPLIED, OR STATUTORY, INCLUDING, BUT NOT LIMITED
 * TO, ANY WARRANTY THAT THE SUBJECT SOFTWARE WILL CONFORM TO SPECIFICATIONS,
 * ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE,
 * OR FREEDOM FROM INFRINGEMENT, ANY WARRANTY THAT THE SUBJECT SOFTWARE WILL BE
 * ERROR FREE, OR ANY WARRANTY THAT DOCUMENTATION, IF PROVIDED, WILL CONFORM TO
 * THE SUBJECT SOFTWARE. THIS AGREEMENT DOES NOT, IN ANY MANNER, CONSTITUTE AN
 * ENDORSEMENT BY GOVERNMENT AGENCY OR ANY PRIOR RECIPIENT OF ANY RESULTS,
 * RESULTING DESIGNS, HARDWARE, SOFTWARE PRODUCTS OR ANY OTHER APPLICATIONS
 * RESULTING FROM USE OF THE SUBJECT SOFTWARE.  FURTHER, GOVERNMENT AGENCY
 * DISCLAIMS ALL WARRANTIES AND LIABILITIES REGARDING THIRD-PARTY SOFTWARE,
 * IF PRESENT IN THE ORIGINAL SOFTWARE, AND DISTRIBUTES IT "AS IS."
 *
 * Waiver and Indemnity:  RECIPIENT AGREES TO WAIVE ANY AND ALL CLAIMS AGAINST
 * THE UNITED STATES GOVERNMENT, ITS CONTRACTORS AND SUBCONTRACTORS, AS WELL
 * AS ANY PRIOR RECIPIENT.  IF RECIPIENT'S USE OF THE SUBJECT SOFTWARE RESULTS
 * IN ANY LIABILITIES, DEMANDS, DAMAGES, EXPENSES OR LOSSES ARISING FROM SUCH
 * USE, INCLUDING ANY DAMAGES FROM PRODUCTS BASED ON, OR RESULTING FROM,
 * RECIPIENT'S USE OF THE SUBJECT SOFTWARE, RECIPIENT SHALL INDEMNIFY AND HOLD
 * HARMLESS THE UNITED STATES GOVERNMENT, ITS CONTRACTORS AND SUBCONTRACTORS,
 * AS WELL AS ANY PRIOR RECIPIENT, TO THE EXTENT PERMITTED BY LAW.
 * RECIPIENT'S SOLE REMEDY FOR ANY SUCH MATTER SHALL BE THE IMMEDIATE,
 * UNILATERAL TERMINATION OF THIS AGREEMENT.
 *
 ******************************************************************************/

#include <llvm/IR/Instructions.h>

#include <ikos/analyzer/checker/dead_code.hpp>
#include <ikos/analyzer/support/cast.hpp>

namespace ikos {
namespace analyzer {

DeadCodeChecker::DeadCodeChecker(Context& ctx) : Checker(ctx) {}

CheckerName DeadCodeChecker::name() const {
  return CheckerName::DeadCode;
}

const char* DeadCodeChecker::description() const {
  return "Dead code checker";
}

void DeadCodeChecker::enter(ar::BasicBlock* /*bb*/,
                            const value::AbstractDomain& /*inv*/,
                            CallContext* /*call_context*/) {
  this->_prev_stmt = nullptr;
}

void DeadCodeChecker::check(ar::Statement* stmt,
                            const value::AbstractDomain& inv,
                            CallContext* call_context) {
  if (skip_check(stmt)) {
    return;
  }

  // Update _prev_stmt
  ar::Statement* prev_stmt = this->_prev_stmt;
  this->_prev_stmt = stmt;

  // Check if the current statement needs a check
  if (!needs_check(prev_stmt, stmt->parent())) {
    return;
  }

  Result result =
      inv.is_normal_flow_bottom() ? Result::Unreachable : Result::Ok;
  this->display_dead_code_check(result, stmt);
  this->display_invariant(result, stmt, inv);
  this->_checks.insert(CheckKind::Unreachable,
                       CheckerName::DeadCode,
                       result,
                       stmt,
                       call_context);
}

bool DeadCodeChecker::skip_check(ar::Statement* stmt) {
  if (!stmt->has_frontend()) {
    // No checks on statements without debug info
    return true;
  }

  if (ar::isa< ar::Assignment >(stmt) || ar::isa< ar::UnaryOperation >(stmt)) {
    auto value = stmt->frontend< llvm::Value >();

    if (llvm::isa< llvm::PHINode >(value) ||
        llvm::isa< llvm::CmpInst >(value)) {
      // No checks on assignments for phi nodes and comparisons
      return true;
    }
  }

  return false;
}

bool DeadCodeChecker::needs_check(ar::Statement* prev_stmt,
                                  ar::BasicBlock* bb) {
  if (prev_stmt != nullptr) {
    return isa< ar::Comparison >(prev_stmt) || isa< ar::CallBase >(prev_stmt);
  }

  // First statement of the basic block, look for predecessors
  std::vector< ar::BasicBlock* > preds{bb->predecessor_begin(),
                                       bb->predecessor_end()};

  while (!preds.empty()) {
    ar::BasicBlock* pred = preds.back();
    preds.pop_back();

    auto it = pred->rbegin(), et = pred->rend();

    // Skip statements without debug info, as well as assignments
    while (it != et && skip_check(*it)) {
      ++it;
    }

    if (it == et) {
      // Predecessor is empty, look further up
      preds.insert(preds.end(),
                   pred->predecessor_begin(),
                   pred->predecessor_end());
      continue;
    }

    ar::Statement* last_stmt = *it;
    if (isa< ar::Comparison >(last_stmt) || isa< ar::CallBase >(last_stmt)) {
      return true;
    }
  }

  return false;
}

void DeadCodeChecker::display_dead_code_check(Result result,
                                              ar::Statement* stmt) const {
  if (this->display_check(result, stmt)) {
    out() << "check_dead_code(";
    stmt->dump(out());
    out() << ")" << std::endl;
  }
}

} // end namespace analyzer
} // end namespace ikos
