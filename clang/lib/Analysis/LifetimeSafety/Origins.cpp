//===- Origins.cpp - Origin Implementation -----------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/LifetimeSafety/Origins.h"
#include "clang/Analysis/Analyses/LifetimeSafety/LifetimeAnnotations.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"

namespace clang::lifetimes::internal {

bool isPointerLikeType(QualType QT) {
  return QT->isPointerOrReferenceType() || isGslPointerType(QT);
}

void OriginManager::dump(OriginID OID, llvm::raw_ostream &OS) const {
  OS << OID << " (";
  Origin O = getOrigin(OID);
  if (const ValueDecl *VD = O.getDecl()) {
    OS << "Decl: " << VD->getNameAsString();
  } else if (const Expr *E = O.getExpr()) {
    OS << "Expr: " << E->getStmtClassName();
    if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
      if (const ValueDecl *VD = DRE->getDecl())
        OS << "(" << VD->getNameAsString() << ")";
    }
  } else {
    OS << "Unknown";
  }
  OS << ")";
}

template <typename T>
OriginTree *OriginManager::buildTreeForType(QualType QT, const T *Node) {
  assert(isPointerLikeType(QT) && "buildTreeForType called for non-pointer type");
  OriginTree *Root = createNode(createOrigin(Node));
  if (QT->isPointerOrReferenceType()) {
    QualType PointeeTy = QT->getPointeeType();
    // We recurse if the pointee type is pointer-like, to build the next
    // level in the origin tree. E.g., for T*& / View&.
    if (isPointerLikeType(PointeeTy))
      Root->Pointee = buildTreeForType(PointeeTy, Node);
  }
  return Root;
}

OriginTree *OriginManager::getOrCreateTree(const ValueDecl *D) {
  if (!isPointerLikeType(D->getType()))
    return nullptr;
  auto It = DeclToTreeMap.find(D);
  if (It != DeclToTreeMap.end())
    return It->second;
  return DeclToTreeMap[D] = buildTreeForType(D->getType(), D);
}

OriginTree *OriginManager::getOrCreateTree(const Expr *E, ASTContext &Ctx) {
  // if (E->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNotNull))
  //   return nullptr;
  if (auto *ParenIgnored = E->IgnoreParens(); ParenIgnored != E)
    return getOrCreateTree(ParenIgnored, Ctx);
  // Rvalues of non-pointer type do not have origins.
  if (!E->isGLValue() && !isPointerLikeType(E->getType()))
    return nullptr;

  auto It = ExprToTreeMap.find(E);
  if (It != ExprToTreeMap.end())
    return It->second;

  QualType Type = E->getType();

  // TODO: Doc.
  // Why does reference type does not
  if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    OriginTree *Root = nullptr;
    if (DRE->getDecl()->getType()->isReferenceType()) {
      Root = getOrCreateTree(DRE->getDecl());
    } else {
      Root = createNode(createOrigin(DRE));
      Root->Pointee = getOrCreateTree(DRE->getDecl());
    }
    // Create outer origin. Use inner tree from the underlying decl.
    return ExprToTreeMap[E] = Root;
  }
  // If E is an lvalue , it refers to storage. We model this storage as the
  // first level of origin tree, as if it were a reference, because l-values are
  // addressable.
  if (E->isGLValue() && !Type->isReferenceType()) {
    Type = Ctx.getLValueReferenceType(Type);
  }
  ExprToTreeMap[E] = buildTreeForType(Type, E);
  return ExprToTreeMap[E];
}

const Origin &OriginManager::getOrigin(OriginID ID) const {
  assert(ID.Value < AllOrigins.size());
  return AllOrigins[ID.Value];
}

} // namespace clang::lifetimes::internal
