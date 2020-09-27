! This test checks lowering of OpenMP parallel Directive with
! `SHARED` clause present.

! RUN: bbc -fopenmp -emit-fir %s -o - | \
! RUN:   FileCheck %s --check-prefix=FIRDialect
! RUN: bbc -fopenmp -emit-llvm %s -o - | \
! RUN:   FileCheck %s --check-prefix=LLVMIRDialect

!FIRDialect: func @_QPshared_clause(%[[ARG1:.*]]: !fir.ref<i32>, %[[ARG2:.*]]: !fir.ref<!fir.array<10xi32>>) {
!FIRDialect-DAG: %[[ALPHA:.*]] = fir.alloca i32 {name = "alpha"}
!FIRDialect-DAG: %[[BETA:.*]] = fir.alloca i32 {name = "beta"}
!FIRDialect-DAG: %[[GAMA:.*]] = fir.alloca i32 {name = "gama"}
!FIRDialect-DAG: %[[ALPHA_ARRAY:.*]] = fir.alloca !fir.array<10xi32> {name = "alpha_array"}
!FIRDialect:  omp.parallel shared(%[[ALPHA]] : !fir.ref<i32>, %[[BETA]] : !fir.ref<i32>, %[[GAMA]] : !fir.ref<i32>, %[[ALPHA_ARRAY]]
!: !fir.ref<!fir.array<10xi32>>, %[[ARG1]] : !fir.ref<i32>, %[[ARG2]] : !fir.ref<!fir.array<10xi32>>)) {
!FIRDialect:    omp.terminator
!FIRDialect:  }

!LLVMDialect: llvm.func @_QPshared_clause(%[[ARG1:.*]]: !llvm.ptr<i32>, %[[ARG2:.*]]: !llvm.ptr<array<10 x i32>>) {
!LLVMIRDialect-DAG:  %[[ALPHA:.*]] = llvm.alloca %{{.*}} x !llvm.i32 {in_type = i32, name = "alpha"} : (!llvm.i64) -> !llvm.ptr<i32>
!LLVMIRDialect-DAG:  %[[BETA:.*]] = llvm.alloca %{{.*}} x !llvm.i32 {in_type = i32, name = "beta"} : (!llvm.i64) -> !llvm.ptr<i32>
!LLVMIRDialect-DAG:  %[[GAMA:.*]] = llvm.alloca %{{.*}} x !llvm.i32 {in_type = i32, name = "gama"} : (!llvm.i64) -> !llvm.ptr<i32>
!LLVMIRDialect-DAG: %[[ALPHA_ARRAY:.*]] = llvm.alloca %{{.*}} x !llvm.array<10 x i32> {in_type = !fir.array<10xi32>, name = "alpha_array"} : (!llvm.i64) -> !llvm.ptr<array<10 x i32>>
!LLVMIRDialect:  omp.parallel shared(%[[ALPHA]] : !llvm.ptr<i32>, %[[BETA]] : !llvm.ptr<i32>, %[[GAMA]] : !llvm.ptr<i32>,
!%[[ALPHA_ARRAY]] : !llvm.ptr<array<10 x i32>>, %[[ARG1]] : !llvm.ptr<i32>, %[[ARG2]] : !llvm.ptr<array<10 x i32>>) {
!LLVMIRDialect:    omp.terminator
!LLVMIRDialect:  }

subroutine shared_clause(arg1, arg2)

        integer :: arg1, arg2(10)
        integer :: alpha, beta, gama
        integer :: alpha_array(10)

!$OMP PARALLEL SHARED(alpha, beta, gama, alpha_array, arg1, arg2)
        print*, "SHARED"
        print*, alpha, beta, gama
!$OMP END PARALLEL

end subroutine
