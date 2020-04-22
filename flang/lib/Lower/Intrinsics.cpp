//===-- Intrinsics.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Builder routines for constructing the FIR dialect of MLIR. As FIR is a
// dialect of MLIR, it makes extensive use of MLIR interfaces and MLIR's coding
// style (https://mlir.llvm.org/getting_started/DeveloperGuide/) is used in this
// module.
//
//===----------------------------------------------------------------------===//

#include "RTBuilder.h"
#include "flang/Lower/ConvertType.h"
#include "flang/Lower/FIRBuilder.h"
#include "flang/Lower/Runtime.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <utility>

#define PGMATH_DECLARE
#include "../runtime/pgmath.h.inc"

/// This file implements lowering of Fortran intrinsic procedures.
/// Intrinsics are lowered to a mix of FIR and MLIR operations as
/// well as call to runtime functions or LLVM intrinsics.

/// Lowering of intrinsic procedure calls is based on a map that associates
/// Fortran intrinsic generic names to FIR generator functions.
/// All generator functions are member functions of the IntrinsicLibrary class
/// and have the same interface.
/// If no generator is given for an intrinsic name, a math runtime library
/// is searched for an implementation and, if a runtime function is found,
/// a call is generated for it. LLVM intrinsics are handled as a math
/// runtime library here.

/// Enums used to templatize and share lowering of MIN and MAX.
enum class Extremum { Min, Max };

// There are different ways to deal with NaNs in MIN and MAX.
// Known existing behaviors are listed below and can be selected for
// f18 MIN/MAX implementation.
enum class ExtremumBehavior {
  // Note: the Signaling/quiet aspect of NaNs in the behaviors below are
  // not described because there is no way to control/observe such aspect in
  // MLIR/LLVM yet. The IEEE behaviors come with requirements regarding this
  // aspect that are therefore currently not enforced. In the descriptions
  // below, NaNs can be signaling or quite. Returned NaNs may be signaling
  // if one of the input NaN was signaling but it cannot be guaranteed either.
  // Existing compilers using an IEEE behavior (gfortran) also do not fulfill
  // signaling/quiet requirements.
  IeeeMinMaximumNumber,
  // IEEE minimumNumber/maximumNumber behavior (754-2019, section 9.6):
  // If one of the argument is and number and the other is NaN, return the
  // number. If both arguements are NaN, return NaN.
  // Compilers: gfortran.
  IeeeMinMaximum,
  // IEEE minimum/maximum behavior (754-2019, section 9.6):
  // If one of the argument is NaN, return NaN.
  MinMaxss,
  // x86 minss/maxss behavior:
  // If the second argument is a number and the other is NaN, return the number.
  // In all other cases where at least one operand is NaN, return NaN.
  // Compilers: xlf (only for MAX), ifort, pgfortran -nollvm, and nagfor.
  PgfortranLlvm,
  // "Opposite of" x86 minss/maxss behavior:
  // If the first argument is a number and the other is NaN, return the
  // number.
  // In all other cases where at least one operand is NaN, return NaN.
  // Compilers: xlf (only for MIN), and pgfortran (with llvm).
  IeeeMinMaxNum
  // IEEE minNum/maxNum behavior (754-2008, section 5.3.1):
  // TODO: Not implemented.
  // It is the only behavior where the signaling/quiet aspect of a NaN argument
  // impacts if the result should be NaN or the argument that is a number.
  // LLVM/MLIR do not provide ways to observe this aspect, so it is not
  // possible to implement it without some target dependent runtime.
};

// TODO error handling -> return a code or directly emit messages ?
struct IntrinsicLibrary {

  /// Generate FIR for call to Fortran intrinsic \p name with arguments \p arg
  /// and expected result type \p resultType.
  mlir::Value genIntrinsicCall(llvm::StringRef name, mlir::Type resultType,
                               llvm::ArrayRef<mlir::Value> arg);

  /// Search a runtime function that is associated to the generic intrinsic name
  /// and whose signature matches the intrinsic arguments and result types.
  /// If no such runtime function is found but a runtime function associated
  /// with the Fortran generic exists and has the same number of arguments,
  /// conversions will be inserted before and/or after the call. This is to
  /// mainly to allow 16 bits float support even-though little or no math
  /// runtime is currently available for it.
  mlir::Value genRuntimeCall(llvm::StringRef name, mlir::Type,
                             llvm::ArrayRef<mlir::Value>);

  mlir::Value genAbs(mlir::Type, llvm::ArrayRef<mlir::Value>);
  mlir::Value genAimag(mlir::Type, llvm::ArrayRef<mlir::Value>);
  mlir::Value genConjg(mlir::Type, llvm::ArrayRef<mlir::Value>);
  mlir::Value genCeiling(mlir::Type, llvm::ArrayRef<mlir::Value>);
  template <Extremum, ExtremumBehavior>
  mlir::Value genExtremum(mlir::Type, llvm::ArrayRef<mlir::Value>);
  mlir::Value genIchar(mlir::Type, llvm::ArrayRef<mlir::Value>);
  mlir::Value genLenTrim(mlir::Type, llvm::ArrayRef<mlir::Value>);
  mlir::Value genMerge(mlir::Type, llvm::ArrayRef<mlir::Value>);
  mlir::Value genMod(mlir::Type, llvm::ArrayRef<mlir::Value>);
  mlir::Value genSign(mlir::Type, llvm::ArrayRef<mlir::Value>);
  /// Implement all conversion functions like DBLE, the first argument is
  /// the value to convert. There may be an additional KIND arguments that
  /// is ignored because this is already reflected in the result type.
  mlir::Value genConversion(mlir::Type, llvm::ArrayRef<mlir::Value>);

  /// Define the different FIR generators that can be mapped to intrinsic to
  /// generate the related code.
  using Generator = decltype(&IntrinsicLibrary::genAbs);

  /// All generators can be outlined. This will build a function named
  /// "fir."+ <generic name> + "." + <result type code> and generate the
  /// intrinsic implementation inside instead of at the intrinsic call sites.
  /// This can be used to keep the FIR more readable. Only one function will
  /// be generated for all the similar calls in a program.
  /// If the Generator is nullptr, the wrapper uses genRuntimeCall.
  mlir::Value outlineInWrapper(Generator, llvm::StringRef name,
                               mlir::Type resultType,
                               llvm::ArrayRef<mlir::Value> args);

  Fortran::lower::FirOpBuilder &builder;
  // TODO use builder location instead ?
  // The only case where that would be an issue is when outlining
  // intrinsic into its own function.
  mlir::Location loc;
};

/// Table that drives the fir generation depending on the intrinsic.
/// one to one mapping with Fortran arguments. If no mapping is
/// defined here for a generic intrinsic, genRuntimeCall will be called
/// to look for a match in the runtime a emit a call.
struct IntrinsicHanlder {
  const char *name;
  IntrinsicLibrary::Generator generator;
  /// Code heavy intrinsic can be outlined to make FIR
  /// more readable.
  bool outline = false;
};
using I = IntrinsicLibrary;
static constexpr IntrinsicHanlder handlers[]{
    {"abs", &I::genAbs},
    {"aimag", &I::genAimag},
    {"ceiling", &I::genCeiling},
    {"char", &I::genConversion},
    {"conjg", &I::genConjg},
    {"dble", &I::genConversion},
    {"ichar", &I::genIchar},
    {"len_trim", &I::genLenTrim},
    {"max", &I::genExtremum<Extremum::Max, ExtremumBehavior::MinMaxss>},
    {"min", &I::genExtremum<Extremum::Min, ExtremumBehavior::MinMaxss>},
    {"merge", &I::genMerge},
    {"mod", &I::genMod},
    {"sign", &I::genSign},
};

/// To make fir output more readable for debug, one can outline all intrinsic
/// implementation in wrappers (overrides the IntrinsicHanlder::outline flag).
static llvm::cl::opt<bool> outlineAllIntrinsics(
    "outline-intrinsics",
    llvm::cl::desc(
        "Lower all intrinsic procedure implementation in their own functions"),
    llvm::cl::init(false));

/// Generate a function name for function where intrinsic implementation
/// are outlined. It is not a legal Fortran name and could therefore
/// safely be matched later if needed.
static std::string getIntrinsicWrapperName(const llvm::StringRef &intrinsic,
                                           mlir::FunctionType funTy);
/// Search runtime for the best runtime function given an intrinsic name
/// and interface. The interface may not be a perfect match in which case
/// the caller is responsible to insert argument and return value conversions.
static llvm::Optional<mlir::FuncOp>
getRuntimeFunction(Fortran::lower::FirOpBuilder &builder, llvm::StringRef name,
                   mlir::FunctionType funcType);

//===----------------------------------------------------------------------===//
// Math runtime description and matching utility
//===----------------------------------------------------------------------===//

/// Command line option to modify math runtime version used to implement
/// intrinsics.
enum MathRuntimeVersion {
  fastVersion,
  relaxedVersion,
  preciseVersion,
  llvmOnly
};
llvm::cl::opt<MathRuntimeVersion> mathRuntimeVersion(
    "math_runtime", llvm::cl::desc("Select math runtime version:"),
    llvm::cl::values(
        clEnumValN(fastVersion, "fast", "use pgmath fast runtime"),
        clEnumValN(relaxedVersion, "relaxed", "use pgmath relaxed runtime"),
        clEnumValN(preciseVersion, "precise", "use pgmath precise runtime"),
        clEnumValN(llvmOnly, "llvm",
                   "only use LLVM intrinsics (may be incomplete)")),
    llvm::cl::init(fastVersion));

struct RuntimeFunction {
  using Key = llvm::StringRef;
  Key key;
  llvm::StringRef symbol;
  Fortran::lower::FuncTypeBuilderFunc typeGenerator;
};

#define RUNTIME_STATIC_DESCRIPTION(name, func)                                 \
  {#name, #func,                                                               \
   Fortran::lower::RuntimeTableKey<decltype(func)>::getTypeModel()},
static constexpr RuntimeFunction pgmathFast[] = {
#define PGMATH_FAST
#define PGMATH_USE_ALL_TYPES(name, func) RUNTIME_STATIC_DESCRIPTION(name, func)
#include "../runtime/pgmath.h.inc"
};
static constexpr RuntimeFunction pgmathRelaxed[] = {
#define PGMATH_RELAXED
#define PGMATH_USE_ALL_TYPES(name, func) RUNTIME_STATIC_DESCRIPTION(name, func)
#include "../runtime/pgmath.h.inc"
};
static constexpr RuntimeFunction pgmathPrecise[] = {
#define PGMATH_PRECISE
#define PGMATH_USE_ALL_TYPES(name, func) RUNTIME_STATIC_DESCRIPTION(name, func)
#include "../runtime/pgmath.h.inc"
};

static mlir::FunctionType gen1ArgF32FuncType(mlir::MLIRContext *context) {
  auto t = mlir::FloatType::getF32(context);
  return mlir::FunctionType::get({t}, {t}, context);
}
static mlir::FunctionType gen1ArgF64FuncType(mlir::MLIRContext *context) {
  auto t = mlir::FloatType::getF64(context);
  return mlir::FunctionType::get({t}, {t}, context);
}

// TODO : Fill-up this table with more intrinsic.
// Note: These are also defined as operations in LLVM dialect. See if this
// can be use and has advantages.
static constexpr RuntimeFunction llvmIntrinsics[] = {
    {"abs", "llvm.fabs.f32", gen1ArgF32FuncType},
    {"abs", "llvm.fabs.f64", gen1ArgF64FuncType},
    // ceil is used for CEILING but is different, it returns a real.
    {"ceil", "llvm.ceil.f32", gen1ArgF32FuncType},
    {"ceil", "llvm.ceil.f64", gen1ArgF64FuncType},
    {"cos", "llvm.cos.f32", gen1ArgF32FuncType},
    {"cos", "llvm.cos.f64", gen1ArgF64FuncType},
    {"log", "llvm.log.f32", gen1ArgF32FuncType},
    {"log", "llvm.log.f64", gen1ArgF64FuncType},
    {"log10", "llvm.log10.f32", gen1ArgF32FuncType},
    {"log10", "llvm.log10.f64", gen1ArgF64FuncType},
    {"sin", "llvm.sin.f32", gen1ArgF32FuncType},
    {"sin", "llvm.sin.f64", gen1ArgF64FuncType},
    {"sqrt", "llvm.sqrt.f32", gen1ArgF32FuncType},
    {"sqrt", "llvm.sqrt.f64", gen1ArgF64FuncType},
};

// This helper class computes a "distance" between two function types.
// The distance measures how many narrowing conversions of actual arguments
// and result of "from" must be made in order to use "to" instead of "from".
// For instance, the distance between ACOS(REAL(10)) and ACOS(REAL(8)) is
// greater than the one between ACOS(REAL(10)) and ACOS(REAL(16)). This means
// if no implementation of ACOS(REAL(10)) is available, it is better to use
// ACOS(REAL(16)) with casts rather than ACOS(REAL(8)).
// Note that this is not a symmetric distance and the order of "from" and "to"
// arguments matters, d(foo, bar) may not be the same as d(bar, foo) because it
// may be safe to replace foo by bar, but not the opposite.
class FunctionDistance {
public:
  FunctionDistance() : infinite{true} {}
  FunctionDistance(mlir::FunctionType from, mlir::FunctionType to) {
    auto nInputs = from.getNumInputs();
    auto nResults = from.getNumResults();
    if (nResults != to.getNumResults() || nInputs != to.getNumInputs()) {
      infinite = true;
    } else {
      for (decltype(nInputs) i{0}; i < nInputs; ++i)
        addArgumentDistance(from.getInput(i), to.getInput(i));
      for (decltype(nResults) i{0}; i < nResults; ++i)
        addResultDistance(to.getResult(i), from.getResult(i));
    }
  }
  bool isSmallerThan(const FunctionDistance &d) const {
    return d.infinite ||
           (!infinite && std::lexicographical_compare(
                             conversions.begin(), conversions.end(),
                             d.conversions.begin(), d.conversions.end()));
  }
  bool isLosingPrecision() const {
    return conversions[narrowingArg] != 0 || conversions[extendingResult] != 0;
  }
  bool isInfinite() const { return infinite; }

private:
  enum class Conversion { Forbidden, None, Narrow, Extend };

  void addArgumentDistance(mlir::Type from, mlir::Type to) {
    switch (conversionBetweenTypes(from, to)) {
    case Conversion::Forbidden:
      infinite = true;
      break;
    case Conversion::None:
      break;
    case Conversion::Narrow:
      conversions[narrowingArg]++;
      break;
    case Conversion::Extend:
      conversions[nonNarrowingArg]++;
      break;
    }
  }
  void addResultDistance(mlir::Type from, mlir::Type to) {
    switch (conversionBetweenTypes(from, to)) {
    case Conversion::Forbidden:
      infinite = true;
      break;
    case Conversion::None:
      break;
    case Conversion::Narrow:
      conversions[nonExtendingResult]++;
      break;
    case Conversion::Extend:
      conversions[extendingResult]++;
      break;
    }
  }
  // Floating point can be mlir::FloatType or fir::real
  static unsigned getFloatingPointWidth(mlir::Type t) {
    if (auto f{t.dyn_cast<mlir::FloatType>()})
      return f.getWidth();
    // FIXME: Get width another way for fir.real/complex
    // - use fir/KindMapping.h and llvm::Type
    // - or use evaluate/type.h
    if (auto r{t.dyn_cast<fir::RealType>()})
      return r.getFKind() * 4;
    if (auto cplx{t.dyn_cast<fir::CplxType>()})
      return cplx.getFKind() * 4;
    llvm_unreachable("not a floating-point type");
  }
  static Conversion conversionBetweenTypes(mlir::Type from, mlir::Type to) {
    if (from == to) {
      return Conversion::None;
    }
    if (auto fromIntTy{from.dyn_cast<mlir::IntegerType>()}) {
      if (auto toIntTy{to.dyn_cast<mlir::IntegerType>()}) {
        return fromIntTy.getWidth() > toIntTy.getWidth() ? Conversion::Narrow
                                                         : Conversion::Extend;
      }
    }
    if (fir::isa_real(from) && fir::isa_real(to)) {
      return getFloatingPointWidth(from) > getFloatingPointWidth(to)
                 ? Conversion::Narrow
                 : Conversion::Extend;
    }
    if (auto fromCplxTy{from.dyn_cast<fir::CplxType>()}) {
      if (auto toCplxTy{to.dyn_cast<fir::CplxType>()}) {
        return getFloatingPointWidth(fromCplxTy) >
                       getFloatingPointWidth(toCplxTy)
                   ? Conversion::Narrow
                   : Conversion::Extend;
      }
    }
    // Notes:
    // - No conversion between character types, specialization of runtime
    // functions should be made instead.
    // - It is not clear there is a use case for automatic conversions
    // around Logical and it may damage hidden information in the physical
    // storage so do not do it.
    return Conversion::Forbidden;
  }

  // Below are indexes to access data in conversions.
  // The order in data does matter for lexicographical_compare
  enum {
    narrowingArg = 0,   // usually bad
    extendingResult,    // usually bad
    nonExtendingResult, // usually ok
    nonNarrowingArg,    // usually ok
    dataSize
  };
  std::array<int, dataSize> conversions{/* zero init*/};
  bool infinite{false}; // When forbidden conversion or wrong argument number
};

static mlir::FuncOp getFuncOp(Fortran::lower::FirOpBuilder &builder,
                              const RuntimeFunction &runtime) {
  auto function = builder.addNamedFunction(
      runtime.symbol, runtime.typeGenerator(builder.getContext()));
  function.setAttr("fir.runtime", builder.getUnitAttr());
  return function;
}

// Select runtime function that has the smallest distance to the intrinsic
// function type and that will not imply narrowing arguments or extending the
// result.
template <std::size_t N>
llvm::Optional<mlir::FuncOp>
searchFunctionInLibrary(Fortran::lower::FirOpBuilder &builder,
                        const RuntimeFunction (&lib)[N], llvm::StringRef name,
                        mlir::FunctionType funcType,
                        const RuntimeFunction **bestNearMatch,
                        FunctionDistance &bestMatchDistance) {
  auto range = Fortran::lower::StaticMultimapView(lib).equal_range(name);
  for (auto iter{range.first}; iter != range.second && iter; ++iter) {
    const auto &impl = *iter;
    auto implType = impl.typeGenerator(builder.getContext());
    if (funcType == implType) {
      return getFuncOp(builder, impl); // exact match
    } else {
      FunctionDistance distance(funcType, implType);
      if (distance.isSmallerThan(bestMatchDistance)) {
        *bestNearMatch = &impl;
        bestMatchDistance = std::move(distance);
      }
    }
  }
  return {};
}

static llvm::Optional<mlir::FuncOp>
getRuntimeFunction(Fortran::lower::FirOpBuilder &builder, llvm::StringRef name,
                   mlir::FunctionType funcType) {
  const RuntimeFunction *bestNearMatch = nullptr;
  FunctionDistance bestMatchDistance{};

  if (mathRuntimeVersion == fastVersion)
    if (auto exactMatch =
            searchFunctionInLibrary(builder, pgmathFast, name, funcType,
                                    &bestNearMatch, bestMatchDistance))
      return exactMatch;
  if (mathRuntimeVersion == relaxedVersion)
    if (auto exactMatch =
            searchFunctionInLibrary(builder, pgmathRelaxed, name, funcType,
                                    &bestNearMatch, bestMatchDistance))
      return exactMatch;
  if (mathRuntimeVersion == preciseVersion)
    if (auto exactMatch =
            searchFunctionInLibrary(builder, pgmathPrecise, name, funcType,
                                    &bestNearMatch, bestMatchDistance))
      return exactMatch;

  // Go through llvm intrinsics if not exact match in libpgmath or if
  // mathRuntimeVersion == llvmOnly
  if (auto exactMatch =
          searchFunctionInLibrary(builder, llvmIntrinsics, name, funcType,
                                  &bestNearMatch, bestMatchDistance))
    return exactMatch;

  if (bestNearMatch != nullptr) {
    assert(!bestMatchDistance.isLosingPrecision() &&
           "runtime selection loses precision");
    return getFuncOp(builder, *bestNearMatch);
  }
  return {};
}

/// Helpers to get function type from arguments and result type.
static mlir::FunctionType
getFunctionType(mlir::Type resultType, llvm::ArrayRef<mlir::Value> arguments,
                Fortran::lower::FirOpBuilder &builder) {
  llvm::SmallVector<mlir::Type, 2> argumentTypes;
  for (auto &arg : arguments) {
    if (arg)
      argumentTypes.push_back(arg.getType());
  }
  return mlir::FunctionType::get(argumentTypes, resultType,
                                 builder.getModule().getContext());
}

/// Helper to encode type into string for intrinsic wrapper name.
// TODO find nicer type to string infra or move this in a mangling utility
// mlir as Type::dump(ostream) methods but it may adds !
static std::string typeToString(mlir::Type t) {
  if (auto i{t.dyn_cast<mlir::IntegerType>()}) {
    return "i" + std::to_string(i.getWidth());
  }
  if (auto cplx{t.dyn_cast<fir::CplxType>()}) {
    return "z" + std::to_string(cplx.getFKind());
  }
  if (auto real{t.dyn_cast<fir::RealType>()}) {
    return "r" + std::to_string(real.getFKind());
  }
  if (auto f{t.dyn_cast<mlir::FloatType>()}) {
    return "f" + std::to_string(f.getWidth());
  }
  if (auto logical{t.dyn_cast<fir::LogicalType>()}) {
    return "l" + std::to_string(logical.getFKind());
  }
  if (auto character{t.dyn_cast<fir::CharacterType>()}) {
    return "c" + std::to_string(character.getFKind());
  }
  llvm_unreachable("no mangling for type");
}

static std::string getIntrinsicWrapperName(const llvm::StringRef &intrinsic,
                                           mlir::FunctionType funTy) {
  std::string name{"fir." + intrinsic.str() + "."};
  assert(funTy.getNumResults() == 1 && "only function mangling supported");
  name += typeToString(funTy.getResult(0));
  auto e = funTy.getNumInputs();
  for (decltype(e) i = 0; i < e; ++i) {
    name += "." + typeToString(funTy.getInput(i));
  }
  return name;
}

//===----------------------------------------------------------------------===//
// IntrinsicLibrary
//===----------------------------------------------------------------------===//

mlir::Value
IntrinsicLibrary::genIntrinsicCall(llvm::StringRef name, mlir::Type resultType,
                                   llvm::ArrayRef<mlir::Value> args) {
  for (auto &handler : handlers)
    if (name == handler.name) {
      assert(handler.generator != nullptr);
      return handler.outline || outlineAllIntrinsics
                 ? outlineInWrapper(handler.generator, name, resultType, args)
                 : std::invoke(handler.generator, *this, resultType, args);
    }
  // Try the runtime if no special handler was defined for the
  // intrinsic being called.
  return outlineInWrapper(nullptr, name, resultType, args);
}

mlir::Value
IntrinsicLibrary::outlineInWrapper(Generator generator, llvm::StringRef name,
                                   mlir::Type resultType,
                                   llvm::ArrayRef<mlir::Value> args) {
  auto module = builder.getModule();
  auto *mlirContext = module.getContext();
  auto funcType = getFunctionType(resultType, args, builder);
  std::string wrapperName = getIntrinsicWrapperName(name, funcType);
  auto function = builder.getNamedFunction(wrapperName);
  if (!function) {
    // First time this wrapper is needed, build it.
    function = builder.createFunction(wrapperName, funcType);
    function.setAttr("fir.intrinsic", builder.getUnitAttr());
    function.addEntryBlock();

    // Create local context to emit code into the newly created function
    // This new function is not linked to a source file location, only
    // its calls will be.
    auto localBuilder =
        std::make_unique<Fortran::lower::FirOpBuilder>(function);
    localBuilder->setInsertionPointToStart(&function.front());
    llvm::SmallVector<mlir::Value, 2> localArguments;
    for (mlir::BlockArgument bArg : function.front().getArguments())
      localArguments.push_back(bArg);

    auto localLoc = mlir::UnknownLoc::get(mlirContext);
    IntrinsicLibrary localLib{*localBuilder, localLoc};
    mlir::Value result =
        generator ? std::invoke(generator, localLib, resultType, localArguments)
                  : std::invoke(&IntrinsicLibrary::genRuntimeCall, localLib,
                                name, resultType, localArguments);
    localBuilder->create<mlir::ReturnOp>(localLoc, result);
  } else {
    // Wrapper was already built, ensure it has the sought type
    assert(function.getType() == funcType);
  }
  auto call = builder.create<mlir::CallOp>(loc, function, args);
  return call.getResult(0);
}

mlir::Value IntrinsicLibrary::genRuntimeCall(llvm::StringRef name,
                                             mlir::Type resultType,
                                             llvm::ArrayRef<mlir::Value> args) {
  // Look up runtime
  mlir::FunctionType soughtFuncType =
      getFunctionType(resultType, args, builder);
  if (auto funcOp = getRuntimeFunction(builder, name, soughtFuncType)) {
    mlir::FunctionType actualFuncType = funcOp->getType();
    if (actualFuncType.getNumResults() != soughtFuncType.getNumResults() ||
        actualFuncType.getNumInputs() != soughtFuncType.getNumInputs() ||
        actualFuncType.getNumInputs() != args.size() ||
        actualFuncType.getNumResults() != 1) {
      llvm_unreachable("Bad intrinsic match");
    }
    llvm::SmallVector<mlir::Value, 2> convertedArguments;
    int i = 0;
    for (mlir::Value arg : args) {
      auto actualType = actualFuncType.getInput(i);
      if (soughtFuncType.getInput(i) != actualType) {
        auto castedArg = builder.create<fir::ConvertOp>(loc, actualType, arg);
        convertedArguments.push_back(castedArg.getResult());
      } else {
        convertedArguments.push_back(arg);
      }
      ++i;
    }
    auto call = builder.create<mlir::CallOp>(loc, *funcOp, convertedArguments);
    mlir::Type soughtType = soughtFuncType.getResult(0);
    mlir::Value res = call.getResult(0);
    if (actualFuncType.getResult(0) != soughtType) {
      auto castedRes = builder.create<fir::ConvertOp>(loc, soughtType, res);
      return castedRes.getResult();
    } else {
      return res;
    }
  } else {
    // could not find runtime function
    llvm::errs() << "missing intrinsic: " << name << "\n";
    llvm_unreachable("no runtime found for this intrinsics");
    // TODO: better error handling ?
    //  - Try to have compile time check of runtime completeness ?
  }
  return {}; // gets rid of warnings
}

mlir::Value IntrinsicLibrary::genConversion(mlir::Type resultType,
                                            llvm::ArrayRef<mlir::Value> args) {
  // There can be an optional kind in second argument.
  assert(args.size() >= 1);
  return builder.create<fir::ConvertOp>(loc, resultType, args[0]);
}

// ABS
mlir::Value IntrinsicLibrary::genAbs(mlir::Type resultType,
                                     llvm::ArrayRef<mlir::Value> args) {
  assert(args.size() == 1);
  auto arg = args[0];
  auto type = arg.getType();
  if (fir::isa_real(type)) {
    // Runtime call to fp abs. An alternative would be to use mlir AbsFOp
    // but it does not support all fir floating point types.
    return genRuntimeCall("abs", resultType, args);
  }
  if (auto intType = type.dyn_cast<mlir::IntegerType>()) {
    // At the time of this implementation there is no abs op in mlir.
    // So, implement abs here without branching.
    auto shift = builder.createIntegerConstant(intType, intType.getWidth() - 1);
    auto mask = builder.create<mlir::SignedShiftRightOp>(loc, arg, shift);
    auto xored = builder.create<mlir::XOrOp>(loc, arg, mask);
    return builder.create<mlir::SubIOp>(loc, xored, mask);
  }
  if (fir::isa_complex(type)) {
    // Use HYPOT to fulfill the no underflow/overflow requirement.
    auto parts = builder.extractParts(arg);
    llvm::SmallVector<mlir::Value, 2> args = {parts.first, parts.second};
    return genIntrinsicCall("hypot", resultType, args);
  }
  llvm_unreachable("unexpected type in ABS argument");
}

// AIMAG
mlir::Value IntrinsicLibrary::genAimag(mlir::Type resultType,
                                       llvm::ArrayRef<mlir::Value> args) {
  assert(args.size() == 1);
  return builder.extractComplexPart(args[0], true /* isImagPart */);
}

// CEILING
mlir::Value IntrinsicLibrary::genCeiling(mlir::Type resultType,
                                         llvm::ArrayRef<mlir::Value> args) {
  // Optional KIND argument.
  assert(args.size() >= 1);
  auto arg = args[0];
  // Use ceil that is not an actual Fortran intrinsic but that is
  // an llvm intrinsic that does the same, but return a floating
  // point.
  auto ceil = genIntrinsicCall("ceil", arg.getType(), {arg});
  return builder.create<fir::ConvertOp>(loc, resultType, ceil);
}

// CONJG
mlir::Value IntrinsicLibrary::genConjg(mlir::Type resultType,
                                       llvm::ArrayRef<mlir::Value> args) {
  assert(args.size() == 1);
  if (resultType != args[0].getType())
    llvm_unreachable("argument type mismatch");
  builder.setLocation(loc);

  mlir::Value cplx = args[0];
  auto imag = builder.extractComplexPart(cplx, /*isImagPart=*/true);
  auto negImag = builder.create<fir::NegfOp>(loc, imag);
  return builder.insertComplexPart(cplx, negImag, /*isImagPart=*/true);
}

// ICHAR
mlir::Value IntrinsicLibrary::genIchar(mlir::Type resultType,
                                       llvm::ArrayRef<mlir::Value> args) {
  // There can be an optional kind in second argument.
  assert(args.size() >= 1);

  auto arg = args[0];
  auto dataAndLen = builder.createUnboxChar(arg);
  auto charType = fir::CharacterType::get(
      builder.getContext(), builder.getCharacterKind(arg.getType()));
  auto refType = fir::ReferenceType::get(charType);
  auto charAddr =
      builder.create<fir::ConvertOp>(loc, refType, dataAndLen.first);
  auto charVal = builder.create<fir::LoadOp>(loc, charType, charAddr);
  return builder.create<fir::ConvertOp>(loc, resultType, charVal);
}

// LEN_TRIM
mlir::Value IntrinsicLibrary::genLenTrim(mlir::Type,
                                         llvm::ArrayRef<mlir::Value> args) {
  // Optional KIND argument reflected in result type.
  assert(args.size() >= 1);
  // FIXME: LEN_TRIM needs actual runtime and to be define in CharRT.h
  llvm_unreachable("LEN_TRIM TODO");
  // Fake implementation for debugging:
  // return builder.createIntegerConstant(resultType, 0);
}

// MERGE
mlir::Value IntrinsicLibrary::genMerge(mlir::Type,
                                       llvm::ArrayRef<mlir::Value> args) {
  assert(args.size() == 3);

  auto i1Type = mlir::IntegerType::get(1, builder.getContext());
  auto mask = builder.create<fir::ConvertOp>(loc, i1Type, args[2]);
  return builder.create<mlir::SelectOp>(loc, mask, args[0], args[1]);
}

// MOD
mlir::Value IntrinsicLibrary::genMod(mlir::Type resultType,
                                     llvm::ArrayRef<mlir::Value> args) {
  assert(args.size() == 2);
  if (resultType.isa<mlir::IntegerType>())
    return builder.create<mlir::SignedRemIOp>(loc, args[0], args[1]);

  // Use runtime. Note that mlir::RemFOp alos implement floating point
  // remainder, but it does not work with fir::Real type.
  return genRuntimeCall("mod", resultType, args);
}

// SIGN
mlir::Value IntrinsicLibrary::genSign(mlir::Type resultType,
                                      llvm::ArrayRef<mlir::Value> args) {
  assert(args.size() == 2);
  auto abs = genAbs(resultType, {args[0]});
  if (resultType.isa<mlir::IntegerType>()) {
    auto zero = builder.createIntegerConstant(resultType, 0);
    auto neg = builder.create<mlir::SubIOp>(loc, zero, abs);
    auto cmp = builder.create<mlir::CmpIOp>(loc, mlir::CmpIPredicate::slt,
                                            args[1], zero);
    return builder.create<mlir::SelectOp>(loc, cmp, neg, abs);
  }
  // TODO: Requirements when second argument is +0./0.
  auto zeroAttr = builder.getZeroAttr(resultType);
  auto zero = builder.create<mlir::ConstantOp>(loc, resultType, zeroAttr);
  auto neg = builder.create<fir::NegfOp>(loc, abs);
  auto cmp =
      builder.create<fir::CmpfOp>(loc, mlir::CmpFPredicate::OLT, args[1], zero);
  return builder.create<mlir::SelectOp>(loc, cmp, neg, abs);
}

// Compare two FIR values and return boolean result as i1.
template <Extremum extremum, ExtremumBehavior behavior>
static mlir::Value createExtremumCompare(mlir::Location loc,
                                         Fortran::lower::FirOpBuilder &builder,
                                         mlir::Value left, mlir::Value right) {
  static constexpr auto integerPredicate = extremum == Extremum::Max
                                               ? mlir::CmpIPredicate::sgt
                                               : mlir::CmpIPredicate::slt;
  static constexpr auto orderedCmp = extremum == Extremum::Max
                                         ? mlir::CmpFPredicate::OGT
                                         : mlir::CmpFPredicate::OLT;
  auto type = left.getType();
  mlir::Value result;
  if (type.isa<mlir::FloatType>() || type.isa<fir::RealType>()) {
    // Note: the signaling/quit aspect of the result required by IEEE
    // cannot currently be obtained with LLVM without ad-hoc runtime.
    if constexpr (behavior == ExtremumBehavior::IeeeMinMaximumNumber) {
      // Return the number if one of the inputs is NaN and the other is
      // a number.
      auto leftIsResult =
          builder.create<fir::CmpfOp>(loc, orderedCmp, left, right);
      auto rightIsNan = builder.create<fir::CmpfOp>(
          loc, mlir::CmpFPredicate::UNE, right, right);
      result = builder.create<mlir::OrOp>(loc, leftIsResult, rightIsNan);
    } else if constexpr (behavior == ExtremumBehavior::IeeeMinMaximum) {
      // Always return NaNs if one the input is NaNs
      auto leftIsResult =
          builder.create<fir::CmpfOp>(loc, orderedCmp, left, right);
      auto leftIsNan = builder.create<fir::CmpfOp>(
          loc, mlir::CmpFPredicate::UNE, left, left);
      result = builder.create<mlir::OrOp>(loc, leftIsResult, leftIsNan);
    } else if constexpr (behavior == ExtremumBehavior::MinMaxss) {
      // If the left is a NaN, return the right whatever it is.
      result = builder.create<fir::CmpfOp>(loc, orderedCmp, left, right);
    } else if constexpr (behavior == ExtremumBehavior::PgfortranLlvm) {
      // If one of the operand is a NaN, return left whatever it is.
      static constexpr auto unorderedCmp = extremum == Extremum::Max
                                               ? mlir::CmpFPredicate::UGT
                                               : mlir::CmpFPredicate::ULT;
      result = builder.create<fir::CmpfOp>(loc, unorderedCmp, left, right);
    } else {
      // TODO: ieeeMinNum/ieeeMaxNum
      static_assert(behavior == ExtremumBehavior::IeeeMinMaxNum,
                    "ieeeMinNum/ieeeMaxNum behavior not implemented");
    }
  } else if (type.isa<mlir::IntegerType>()) {
    result = builder.create<mlir::CmpIOp>(loc, integerPredicate, left, right);
  } else if (type.isa<fir::CharacterType>()) {
    // TODO: ! character min and max is tricky because the result
    // length is the length of the longest argument!
    // So we may need a temp.
  }
  assert(result);
  return result;
}

// MIN and MAX
template <Extremum extremum, ExtremumBehavior behavior>
mlir::Value IntrinsicLibrary::genExtremum(mlir::Type,
                                          llvm::ArrayRef<mlir::Value> args) {
  assert(args.size() >= 2);
  mlir::Value result = args[0];
  for (auto arg : args.drop_front()) {
    auto mask =
        createExtremumCompare<extremum, behavior>(loc, builder, result, arg);
    result = builder.create<mlir::SelectOp>(loc, mask, result, arg);
  }
  return result;
}

//===----------------------------------------------------------------------===//
// IntrinsicCallOpsBuilder
//===----------------------------------------------------------------------===//

template <typename T>
mlir::Value Fortran::lower::IntrinsicCallOpsBuilder<T>::genIntrinsicCall(
    llvm::StringRef name, mlir::Type resultType,
    llvm::ArrayRef<mlir::Value> args) {
  return IntrinsicLibrary{impl(), impl().getLoc()}.genIntrinsicCall(
      name, resultType, args);
}
template mlir::Value
    Fortran::lower::IntrinsicCallOpsBuilder<Fortran::lower::FirOpBuilder>::
        genIntrinsicCall(llvm::StringRef, mlir::Type,
                         llvm::ArrayRef<mlir::Value>);
