#include "mlir/IR/Block.h"
#include "mlir/IR/Operation.h"
#include "mlir/Support/LLVM.h"
#include "clang/AST/ASTContext.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CIR/Dialect/Builder/CIRBaseBuilder.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/MissingFeatures.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/Path.h"

#include <memory>

namespace mlir {
#define GEN_PASS_DEF_CFGFALLTHROUGHWARNINGS
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir
using namespace mlir;
using namespace cir;
using namespace clang;

namespace {

//===----------------------------------------------------------------------===//
// Check for missing return value.
//===----------------------------------------------------------------------===//

enum ControlFlowKind {
  UnknownFallThrough,
  NeverFallThrough,
  MaybeFallThrough,
  AlwaysFallThrough,
  NeverFallThroughOrReturn
};

struct CheckFallThroughDiagnostics {
  unsigned diagFallThroughHasNoReturn = 0;
  unsigned diagFallThroughReturnsNonVoid = 0;
  unsigned diagNeverFallThroughOrReturn = 0;
  unsigned funKind;
  SourceLocation funcLoc;
  bool checkDiagnostics(DiagnosticsEngine &d, bool returnsVoid,
                        bool hasNoReturn) const {
    if (funKind == diag::FalloffFunctionKind::Function) {
      return (returnsVoid ||
              d.isIgnored(diag::warn_falloff_nonvoid, funcLoc)) &&
             (!hasNoReturn ||
              d.isIgnored(diag::warn_noreturn_has_return_expr, funcLoc)) &&
             (!returnsVoid ||
              d.isIgnored(diag::warn_suggest_noreturn_block, funcLoc));
    }
    if (funKind == diag::FalloffFunctionKind::Coroutine) {
      return (returnsVoid ||
              d.isIgnored(diag::warn_falloff_nonvoid, funcLoc)) &&
             (!hasNoReturn);
    }
    // For blocks / lambdas.
    return returnsVoid && !hasNoReturn;
  }
};
// TODO: Add a class for fall through config later

struct FallThroughWarningPass
    : public impl::CFGFallThroughWarningsBase<FallThroughWarningPass> {
public:
  FallThroughWarningPass() = default;
  void runOnOperation() override;
  void checkFallThroughForFuncBody(Sema &s, cir::FuncOp func,
                                   QualType blockType,
                                   const CheckFallThroughDiagnostics &cd);
  ControlFlowKind checkFallThrough(cir::FuncOp cfg);
  mlir::DenseSet<mlir::Block> getLiveSet(cir::FuncOp cfg, bool getAll) {
    mlir::DenseSet<mlir::Block> liveSet;
    if (cfg.getBody().empty())
      return liveSet;

    auto &body = cfg.getBody();
    auto &first = cfg.getBody().getBlocks().front();

    for (auto &block : body) {
      if (getAll || block.isReachable(&first))
        liveSet.insert(block);
    }
    return liveSet;
  }
};

// TODO: This runs on func op only
void FallThroughWarningPass::runOnOperation() {
  mlir::Operation *op = getOperation();
  if (!isa<cir::FuncOp>(op))
    return;
}

void FallThroughWarningPass::checkFallThroughForFuncBody(
    Sema &s, cir::FuncOp cfg, QualType blockType,
    const CheckFallThroughDiagnostics &cd) {
  bool returnsVoid = false;
  bool hasNoReturn = false;

  // Supposedly all function in cir is FuncOp
  // 1. If normal function (FunctionDecl), check if it's coroutine.
  // 1a. if coroutine -> check the fallthrough handler (idk what this means,
  // TODO for now)
  if (cfg.getCoroutine()) {
    // TODO: Let's not worry about coroutine for now
  } else
    returnsVoid = isa<cir::VoidType>(cfg.getFunctionType().getReturnType());

  // TODO: Do we need to check for InferredNoReturnAttr just like in OG?
  hasNoReturn = cfg.getFunctionType().getReturnTypes().empty();

  DiagnosticsEngine &diags = s.getDiagnostics();
  if (cd.checkDiagnostics(diags, returnsVoid, hasNoReturn)) {
    return;
  }

  // cpu_dispatch functions permit empty function bodies for ICC compatibility.
  // TODO: Do we have isCPUDispatchMultiVersion?



}

ControlFlowKind FallThroughWarningPass::checkFallThrough(cir::FuncOp cfg) {
  assert(!cfg && "there can't be a null func op");

  // TODO: Is no CFG akin to a declaration?
  if (cfg.isDeclaration()) {
    return UnknownFallThrough;
  }

  mlir::DenseSet<mlir::Block> liveSet =
      this->getLiveSet(cfg, /*bool getAll=*/false);

  unsigned count = liveSet.size();

  bool hasLiveReturn = false;
  bool hasFakeEdge = false;
  bool hasPlainEdge = false;
  bool hasAbnormalEdge = false;

  // TODO: Do more here
}
} // namespace

namespace mlir {
std::unique_ptr<Pass> createFallThroughWarningPass() {
  return std::make_unique<FallThroughWarningPass>();
}
} // namespace mlir
