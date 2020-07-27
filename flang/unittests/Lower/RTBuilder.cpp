//===- RTBuilder.cpp -- Runtime Interface unit tests ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../../lib/Lower/RTBuilder.h"
#include "gtest/gtest.h"
#include "flang/Optimizer/Dialect/FIRDialect.h"
#include <complex>

// Check that it is possible to make a difference between complex runtime
// function using C99 complex and C++ std::complex. This is important since
// they are layout compatible but not link time compatible (returned differently
// in X86 32 ABI for instance). At high level fir, we need to convey that the
// signature are different regardless of the target ABI.

// Fake runtime header to be introspected.
c_float_complex_t c99_cacosf(c_float_complex_t);

TEST(RTBuilderTest, ComplexRuntimeInterface) {
  fir::registerFIR();
  mlir::MLIRContext ctx;
  mlir::Type c99_cacosf_signature{
      Fortran::lower::RuntimeTableKey<decltype(c99_cacosf)>::getTypeModel()(
          &ctx)};
  auto c99_cacosf_funcTy = c99_cacosf_signature.cast<mlir::FunctionType>();
  EXPECT_EQ(c99_cacosf_funcTy.getNumInputs(), 1u);
  EXPECT_EQ(c99_cacosf_funcTy.getNumResults(), 1u);
  auto cplx_ty = fir::CplxType::get(&ctx, 4);
  EXPECT_EQ(c99_cacosf_funcTy.getInput(0), cplx_ty);
  EXPECT_EQ(c99_cacosf_funcTy.getResult(0), cplx_ty);
}
