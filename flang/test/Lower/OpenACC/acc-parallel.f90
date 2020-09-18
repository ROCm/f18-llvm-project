! This test checks lowering of OpenACC parallel directive.

! RUN: bbc -fopenacc -emit-fir %s -o - | FileCheck %s

program acc_parallel
  integer :: i, j

  integer :: async = 1
  integer :: wait1 = 1
  integer :: wait2 = 2
  integer :: numGangs = 1
  integer :: numWorkers = 10
  integer :: vectorLength = 128
  logical :: ifCondition = .TRUE.
  real, dimension(10, 10) :: a, b, c

!CHECK: [[A:%.*]] = fir.alloca !fir.array<10x10xf32> {name = "a"}
!CHECK: [[B:%.*]] = fir.alloca !fir.array<10x10xf32> {name = "b"}
!CHECK: [[C:%.*]] = fir.alloca !fir.array<10x10xf32> {name = "c"}

  !$acc parallel
  !$acc end parallel

!CHECK:      acc.parallel {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel async
  !$acc end parallel

!CHECK:      acc.parallel {
!CHECK:        acc.yield
!CHECK-NEXT: } attributes {asyncAttr}

  !$acc parallel async(1)
  !$acc end parallel

!CHECK:      [[ASYNC1:%.*]] = constant 1 : i32
!CHECK:      acc.parallel async([[ASYNC1]]: i32) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel async(async)
  !$acc end parallel

!CHECK:      [[ASYNC2:%.*]] = fir.load %{{.*}} : !fir.ref<i32>
!CHECK:      acc.parallel async([[ASYNC2]]: i32) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel wait
  !$acc end parallel

!CHECK:      acc.parallel {
!CHECK:        acc.yield
!CHECK-NEXT: } attributes {waitAttr}

  !$acc parallel wait(1)
  !$acc end parallel

!CHECK:      [[WAIT1:%.*]] = constant 1 : i32
!CHECK:      acc.parallel wait([[WAIT1]]: i32) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel wait(1, 2)
  !$acc end parallel

!CHECK:      [[WAIT2:%.*]] = constant 1 : i32
!CHECK:      [[WAIT3:%.*]] = constant 2 : i32
!CHECK:      acc.parallel wait([[WAIT2]]: i32, [[WAIT3]]: i32) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel wait(wait1, wait2)
  !$acc end parallel

!CHECK:      [[WAIT4:%.*]] = fir.load %{{.*}} : !fir.ref<i32>
!CHECK:      [[WAIT5:%.*]] = fir.load %{{.*}} : !fir.ref<i32>
!CHECK:      acc.parallel wait([[WAIT4]]: i32, [[WAIT5]]: i32) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel num_gangs(1)
  !$acc end parallel

!CHECK:      [[NUMGANGS1:%.*]] = constant 1 : i32
!CHECK:      acc.parallel num_gangs([[NUMGANGS1]]: i32) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel num_gangs(numGangs)
  !$acc end parallel

!CHECK:      [[NUMGANGS2:%.*]] = fir.load %{{.*}} : !fir.ref<i32>
!CHECK:      acc.parallel num_gangs([[NUMGANGS2]]: i32) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel num_workers(10)
  !$acc end parallel

!CHECK:      [[NUMWORKERS1:%.*]] = constant 10 : i32
!CHECK:      acc.parallel num_workers([[NUMWORKERS1]]: i32) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel num_workers(numWorkers)
  !$acc end parallel

!CHECK:      [[NUMWORKERS2:%.*]] = fir.load %{{.*}} : !fir.ref<i32>
!CHECK:      acc.parallel num_workers([[NUMWORKERS2]]: i32) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel vector_length(128)
  !$acc end parallel

!CHECK:      [[VECTORLENGTH1:%.*]] = constant 128 : i32
!CHECK:      acc.parallel vector_length([[VECTORLENGTH1]]: i32) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel vector_length(vectorLength)
  !$acc end parallel

!CHECK:      [[VECTORLENGTH2:%.*]] = fir.load %{{.*}} : !fir.ref<i32>
!CHECK:      acc.parallel vector_length([[VECTORLENGTH2]]: i32) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel if(.TRUE.)
  !$acc end parallel

!CHECK:      [[IF1:%.*]] = constant true
!CHECK:      acc.parallel if([[IF1]]) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

! NOT WORKING YET
!  !$acc parallel if(ifCondition)
!  !$acc end parallel

  !$acc parallel self(.TRUE.)
  !$acc end parallel

!CHECK:      [[SELF1:%.*]] = constant true
!CHECK:      acc.parallel self([[SELF1]]) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel self
  !$acc end parallel

!CHECK:      acc.parallel {
!CHECK:        acc.yield
!CHECK-NEXT: } attributes {selfAttr}

! NOT WORKING YET
!  !$acc parallel self(ifCondition)
!  !$acc end parallel

  !$acc parallel copy(a, b, c)
  !$acc end parallel

!CHECK:      acc.parallel copy([[A]]: !fir.ref<!fir.array<10x10xf32>>, [[B]]: !fir.ref<!fir.array<10x10xf32>>, [[C]]: !fir.ref<!fir.array<10x10xf32>>) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel copy(a) copy(b) copy(c)
  !$acc end parallel

!CHECK:      acc.parallel copy([[A]]: !fir.ref<!fir.array<10x10xf32>>, [[B]]: !fir.ref<!fir.array<10x10xf32>>, [[C]]: !fir.ref<!fir.array<10x10xf32>>) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel copyin(a) copyin(readonly: b, c)
  !$acc end parallel

!CHECK:      acc.parallel copyin([[A]]: !fir.ref<!fir.array<10x10xf32>>) copyin_readonly([[B]]: !fir.ref<!fir.array<10x10xf32>>, [[C]]: !fir.ref<!fir.array<10x10xf32>>) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel copyout(a) copyout(zero: b) copyout(c)
  !$acc end parallel

!CHECK:      acc.parallel copyout([[A]]: !fir.ref<!fir.array<10x10xf32>>, [[C]]: !fir.ref<!fir.array<10x10xf32>>) copyout_zero([[B]]: !fir.ref<!fir.array<10x10xf32>>) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel create(a, b) create(zero: c)
  !$acc end parallel

!CHECK:      acc.parallel create([[A]]: !fir.ref<!fir.array<10x10xf32>>, [[B]]: !fir.ref<!fir.array<10x10xf32>>) create_zero([[C]]: !fir.ref<!fir.array<10x10xf32>>) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel no_create(a, b) create(zero: c)
  !$acc end parallel

!CHECK:      acc.parallel create_zero([[C]]: !fir.ref<!fir.array<10x10xf32>>) no_create([[A]]: !fir.ref<!fir.array<10x10xf32>>, [[B]]: !fir.ref<!fir.array<10x10xf32>>) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel present(a, b, c)
  !$acc end parallel

!CHECK:      acc.parallel present([[A]]: !fir.ref<!fir.array<10x10xf32>>, [[B]]: !fir.ref<!fir.array<10x10xf32>>, [[C]]: !fir.ref<!fir.array<10x10xf32>>) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel deviceptr(a) deviceptr(c)
  !$acc end parallel

!CHECK:      acc.parallel deviceptr([[A]]: !fir.ref<!fir.array<10x10xf32>>, [[C]]: !fir.ref<!fir.array<10x10xf32>>) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel attach(b, c)
  !$acc end parallel

!CHECK:      acc.parallel attach([[B]]: !fir.ref<!fir.array<10x10xf32>>, [[C]]: !fir.ref<!fir.array<10x10xf32>>) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

  !$acc parallel private(a) firstprivate(b) private(c)
  !$acc end parallel

!CHECK:      acc.parallel private([[A]]: !fir.ref<!fir.array<10x10xf32>>, [[C]]: !fir.ref<!fir.array<10x10xf32>>) firstprivate([[B]]: !fir.ref<!fir.array<10x10xf32>>) {
!CHECK:        acc.yield
!CHECK-NEXT: }{{$}}

end program

