//===-- Lower/IO.h -- lower I/O statements ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// Experimental IO lowering to FIR + runtime. The Runtime design is under
/// design.
///
/// FIXME This interface is also not final. Should it be based on parser::..
/// nodes and lower expressions as needed or should it get every expression
/// already lowered as mlir::Value? (currently second options, not sure it will
/// provide enough information for complex IO statements).
///
//===----------------------------------------------------------------------===//

#ifndef FORTRAN_LOWER_IO_H
#define FORTRAN_LOWER_IO_H

#include "llvm/ADT/DenseMap.h"

namespace mlir {
class Value;
} // namespace mlir

namespace Fortran {
namespace parser {
using Label = std::uint64_t;
struct BackspaceStmt;
struct CloseStmt;
struct EndfileStmt;
struct FlushStmt;
struct InquireStmt;
struct OpenStmt;
struct PrintStmt;
struct ReadStmt;
struct RewindStmt;
struct WaitStmt;
struct WriteStmt;
} // namespace parser

namespace lower {

class AbstractConverter;
class BridgeImpl;

namespace pft {
struct Evaluation;
using LabelEvalMap = llvm::DenseMap<Fortran::parser::Label, Evaluation *>;
} // namespace pft

/// Generate IO call(s) for BACKSPACE; return the IOSTAT code
mlir::Value genBackspaceStatement(AbstractConverter &,
                                  const parser::BackspaceStmt &);

/// Generate IO call(s) for CLOSE; return the IOSTAT code
mlir::Value genCloseStatement(AbstractConverter &, const parser::CloseStmt &);

/// Generate IO call(s) for ENDFILE; return the IOSTAT code
mlir::Value genEndfileStatement(AbstractConverter &,
                                const parser::EndfileStmt &);

/// Generate IO call(s) for FLUSH; return the IOSTAT code
mlir::Value genFlushStatement(AbstractConverter &, const parser::FlushStmt &);

/// Generate IO call(s) for INQUIRE; return the IOSTAT code
mlir::Value genInquireStatement(AbstractConverter &,
                                const parser::InquireStmt &);

/// Generate IO call(s) for OPEN; return the IOSTAT code
mlir::Value genOpenStatement(AbstractConverter &, const parser::OpenStmt &);

/// Generate IO call(s) for PRINT
void genPrintStatement(AbstractConverter &converter,
                       const parser::PrintStmt &stmt,
                       pft::LabelEvalMap &labelMap);

/// Generate IO call(s) for READ; return the IOSTAT code
mlir::Value genReadStatement(AbstractConverter &converter,
                             const parser::ReadStmt &stmt,
                             pft::LabelEvalMap &labelMap);

/// Generate IO call(s) for REWIND; return the IOSTAT code
mlir::Value genRewindStatement(AbstractConverter &, const parser::RewindStmt &);

/// Generate IO call(s) for WAIT; return the IOSTAT code
mlir::Value genWaitStatement(AbstractConverter &, const parser::WaitStmt &);

/// Generate IO call(s) for WRITE; return the IOSTAT code
mlir::Value genWriteStatement(AbstractConverter &converter,
                              const parser::WriteStmt &stmt,
                              pft::LabelEvalMap &labelMap);

} // namespace lower
} // namespace Fortran

#endif // FORTRAN_LOWER_IO_H
