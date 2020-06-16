//===-- Lower/Support/BoxValue.h -- internal box values ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LOWER_SUPPORT_BOXVALUE_H
#define LOWER_SUPPORT_BOXVALUE_H

#include "mlir/IR/Value.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"
#include <utility>
#include <variant>

namespace fir {
struct CharBoxValue;
struct ArrayBoxValue;
struct CharArrayBoxValue;
struct BoxValue;
struct ProcBoxValue;

llvm::raw_ostream &operator<<(llvm::raw_ostream &, const CharBoxValue &);
llvm::raw_ostream &operator<<(llvm::raw_ostream &, const ArrayBoxValue &);
llvm::raw_ostream &operator<<(llvm::raw_ostream &, const CharArrayBoxValue &);
llvm::raw_ostream &operator<<(llvm::raw_ostream &, const BoxValue &);
llvm::raw_ostream &operator<<(llvm::raw_ostream &, const ProcBoxValue &);

//===----------------------------------------------------------------------===//
//
// Boxed values
//
// Define a set of containers to use internal to the lowering bridge to keep
// track of extended values associated with a Fortran subexpression. These
// associations are maintained during the construction of FIR.
//
//===----------------------------------------------------------------------===//

/// Most expressions of intrinsic type can be passed unboxed. Their properties
/// are known statically.
using UnboxedValue = mlir::Value;

/// Abstract base class.
struct AbstractBox {
  AbstractBox() = delete;
  AbstractBox(mlir::Value addr) : addr{addr} {}
  mlir::Value getAddr() const { return addr; }

  mlir::Value addr;
};

/// Expressions of CHARACTER type have an associated, possibly dynamic LEN
/// value.
struct CharBoxValue : public AbstractBox {
  CharBoxValue(mlir::Value addr, mlir::Value len)
      : AbstractBox{addr}, len{len} {}

  CharBoxValue clone(mlir::Value newBase) const { return {newBase, len}; }

  mlir::Value getLen() const { return len; }
  mlir::Value getBuffer() const { return getAddr(); }

  friend llvm::raw_ostream &operator<<(llvm::raw_ostream &,
                                       const CharBoxValue &);
  void dump() const { llvm::errs() << *this; }

  mlir::Value len;
};

/// Abstract base class.
/// Expressions of type array have at minimum a shape. These expressions may
/// have lbound attributes (dynamic values) that affect the interpretation of
/// indexing expressions.
struct AbstractArrayBox {
  AbstractArrayBox() = default;
  AbstractArrayBox(llvm::ArrayRef<mlir::Value> extents,
                   llvm::ArrayRef<mlir::Value> lbounds)
      : extents{extents.begin(), extents.end()}, lbounds{lbounds.begin(),
                                                         lbounds.end()} {}

  // Every array has extents that describe its shape.
  const llvm::SmallVectorImpl<mlir::Value> &getExtents() const {
    return extents;
  }

  // An array expression may have user-defined lower bound values.
  // If this vector is empty, the default in all dimensions in `1`.
  const llvm::SmallVectorImpl<mlir::Value> &getLBounds() const {
    return lbounds;
  }

  bool lboundsAllOne() const { return lbounds.empty(); }

  llvm::SmallVector<mlir::Value, 4> extents;
  llvm::SmallVector<mlir::Value, 4> lbounds;
};

/// Expressions with rank > 0 have extents. They may also have lbounds that are
/// not 1.
struct ArrayBoxValue : public AbstractBox, public AbstractArrayBox {
  ArrayBoxValue(mlir::Value addr, llvm::ArrayRef<mlir::Value> extents,
                llvm::ArrayRef<mlir::Value> lbounds = {})
      : AbstractBox{addr}, AbstractArrayBox{extents, lbounds} {}

  ArrayBoxValue clone(mlir::Value newBase) const {
    return {newBase, extents, lbounds};
  }

  friend llvm::raw_ostream &operator<<(llvm::raw_ostream &,
                                       const ArrayBoxValue &);
  void dump() const { operator<<(llvm::errs(), *this); }
};

/// Expressions of type CHARACTER and with rank > 0.
struct CharArrayBoxValue : public CharBoxValue, public AbstractArrayBox {
  CharArrayBoxValue(mlir::Value addr, mlir::Value len,
                    llvm::ArrayRef<mlir::Value> extents,
                    llvm::ArrayRef<mlir::Value> lbounds = {})
      : CharBoxValue{addr, len}, AbstractArrayBox{extents, lbounds} {}

  CharArrayBoxValue clone(mlir::Value newBase) const {
    return {newBase, len, extents, lbounds};
  }

  friend llvm::raw_ostream &operator<<(llvm::raw_ostream &,
                                       const CharArrayBoxValue &);
  void dump() const { operator<<(llvm::errs(), *this); }
};

/// Expressions that are procedure POINTERs may need a set of references to
/// variables in the host scope.
struct ProcBoxValue : public AbstractBox {
  ProcBoxValue(mlir::Value addr, mlir::Value context)
      : AbstractBox{addr}, hostContext{context} {}

  ProcBoxValue clone(mlir::Value newBase) const {
    return {newBase, hostContext};
  }

  friend llvm::raw_ostream &operator<<(llvm::raw_ostream &,
                                       const ProcBoxValue &);
  void dump() const { operator<<(llvm::errs(), *this); }

  mlir::Value hostContext;
};

/// In the generalized form, a boxed value can have a dynamic size, be an array
/// with dynamic extents and lbounds, and take dynamic type parameters.
struct BoxValue : public AbstractBox, public AbstractArrayBox {
  BoxValue(mlir::Value addr) : AbstractBox{addr}, AbstractArrayBox{} {}
  BoxValue(mlir::Value addr, mlir::Value len)
      : AbstractBox{addr}, AbstractArrayBox{}, len{len} {}
  BoxValue(mlir::Value addr, llvm::ArrayRef<mlir::Value> extents,
           llvm::ArrayRef<mlir::Value> lbounds = {})
      : AbstractBox{addr}, AbstractArrayBox{extents, lbounds} {}
  BoxValue(mlir::Value addr, mlir::Value len,
           llvm::ArrayRef<mlir::Value> params,
           llvm::ArrayRef<mlir::Value> extents,
           llvm::ArrayRef<mlir::Value> lbounds = {})
      : AbstractBox{addr}, AbstractArrayBox{extents, lbounds}, len{len},
        params{params.begin(), params.end()} {}

  BoxValue clone(mlir::Value newBase) const {
    return {newBase, len, params, extents, lbounds};
  }

  friend llvm::raw_ostream &operator<<(llvm::raw_ostream &, const BoxValue &);
  void dump() const { operator<<(llvm::errs(), *this); }

  mlir::Value len;
  llvm::SmallVector<mlir::Value, 2> params;
};

/// Used for triple notation (array slices)
using RangeBoxValue = std::tuple<mlir::Value, mlir::Value, mlir::Value>;

class ExtendedValue;

mlir::Value getBase(const ExtendedValue &exv);
llvm::raw_ostream &operator<<(llvm::raw_ostream &, const ExtendedValue &);
ExtendedValue substBase(const ExtendedValue &exv, mlir::Value base);

/// An extended value is a box of values pertaining to a discrete entity. It is
/// used in lowering to track all the runtime values related to an entity. For
/// example, an entity may have an address in memory that contains its value(s)
/// as well as various attribute values that describe the shape and starting
/// indices if it is an array entity.
class ExtendedValue {
public:
  template <typename A>
  constexpr ExtendedValue(A &&box) : box{std::forward<A>(box)} {}

  constexpr const CharBoxValue *getCharBox() const {
    return std::get_if<CharBoxValue>(&box);
  }

  constexpr const UnboxedValue *getUnboxed() const {
    return std::get_if<UnboxedValue>(&box);
  }

  /// LLVM style debugging of extended values
  void dump() const { llvm::errs() << *this << '\n'; }

  friend llvm::raw_ostream &operator<<(llvm::raw_ostream &,
                                       const ExtendedValue &);
  friend mlir::Value getBase(const ExtendedValue &exv);
  friend ExtendedValue substBase(const ExtendedValue &exv, mlir::Value base);

private:
  std::variant<UnboxedValue, CharBoxValue, ArrayBoxValue, CharArrayBoxValue,
               BoxValue, ProcBoxValue>
      box;
};
} // namespace fir

namespace Fortran::lower {

using ExValue = fir::ExtendedValue;

} // namespace Fortran::lower

#endif // LOWER_SUPPORT_BOXVALUE_H
