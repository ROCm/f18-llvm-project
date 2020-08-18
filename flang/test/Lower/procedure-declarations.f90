! RUN: bbc -emit-fir %s -o - | FileCheck %s

! Test procedure declarations. Change appearance order of definition and usages
! (passing a procedure and calling it), with and without definitions.
! Check that the definition type prevail if available and that casts are inserted to
! accommodate for the signature mismatch in the different location due to implicit
! typing rules and Fortran loose interface compatibility rule history. 


! Note: all the cases where their is a definition are exactly the same,
! since definition should be processed first regardless.

! pass, call, define
! CHECK-LABEL: func @_QPpass_foo() {
subroutine pass_foo()
  external :: foo
  ! CHECK: %[[f:.*]] = fir.address_of(@_QPfoo)
  ! CHECK: fir.convert %[[f]] : ((!fir.ref<!fir.array<2x5xi32>>) -> ()) -> (() -> ())
  call bar(foo)
end subroutine
! CHECK-LABEL: func @_QPcall_foo(%arg0: !fir.ref<!fir.array<10xi32>>) {
subroutine call_foo(i)
  integer :: i(10)
  ! %[[argconvert:*]] = fir.convert %arg0 :
  ! fir.call @_QPfoo(%[[argconvert]]) : (!fir.ref<!fir.array<2x5xi32>>) -> ()
  call foo(i)
end subroutine 
! CHECK-LABEL: func @_QPfoo(%arg0: !fir.ref<!fir.array<2x5xi32>>) {
subroutine foo(i)
  integer :: i(2, 5)
  call do_something(i)
end subroutine

! call, pass, define
! CHECK-LABEL: func @_QPcall_foo2(%arg0: !fir.ref<!fir.array<10xi32>>) {
subroutine call_foo2(i)
  integer :: i(10)
  ! %[[argconvert:*]] = fir.convert %arg0 :
  ! fir.call @_QPfoo2(%[[argconvert]]) : (!fir.ref<!fir.array<2x5xi32>>) -> ()
  call foo2(i)
end subroutine 
! CHECK-LABEL: func @_QPpass_foo2() {
subroutine pass_foo2()
  external :: foo2
  ! CHECK: %[[f:.*]] = fir.address_of(@_QPfoo2)
  ! CHECK: fir.convert %[[f]] : ((!fir.ref<!fir.array<2x5xi32>>) -> ()) -> (() -> ())
  call bar(foo2)
end subroutine
! CHECK-LABEL: func @_QPfoo2(%arg0: !fir.ref<!fir.array<2x5xi32>>) {
subroutine foo2(i)
  integer :: i(2, 5)
  call do_something(i)
end subroutine

! call, define, pass
! CHECK-LABEL: func @_QPcall_foo3(%arg0: !fir.ref<!fir.array<10xi32>>) {
subroutine call_foo3(i)
  integer :: i(10)
  ! %[[argconvert:*]] = fir.convert %arg0 :
  ! fir.call @_QPfoo3(%[[argconvert]]) : (!fir.ref<!fir.array<2x5xi32>>) -> ()
  call foo3(i)
end subroutine 
! CHECK-LABEL: func @_QPfoo3(%arg0: !fir.ref<!fir.array<2x5xi32>>) {
subroutine foo3(i)
  integer :: i(2, 5)
  call do_something(i)
end subroutine
! CHECK-LABEL: func @_QPpass_foo3() {
subroutine pass_foo3()
  external :: foo3
  ! CHECK: %[[f:.*]] = fir.address_of(@_QPfoo3)
  ! CHECK: fir.convert %[[f]] : ((!fir.ref<!fir.array<2x5xi32>>) -> ()) -> (() -> ())
  call bar(foo3)
end subroutine

! define, call, pass
! CHECK-LABEL: func @_QPfoo4(%arg0: !fir.ref<!fir.array<2x5xi32>>) {
subroutine foo4(i)
  integer :: i(2, 5)
  call do_something(i)
end subroutine
! CHECK-LABEL: func @_QPcall_foo4(%arg0: !fir.ref<!fir.array<10xi32>>) {
subroutine call_foo4(i)
  integer :: i(10)
  ! %[[argconvert:*]] = fir.convert %arg0 :
  ! fir.call @_QPfoo4(%[[argconvert]]) : (!fir.ref<!fir.array<2x5xi32>>) -> ()
  call foo4(i)
end subroutine 
! CHECK-LABEL: func @_QPpass_foo4() {
subroutine pass_foo4()
  external :: foo4
  ! CHECK: %[[f:.*]] = fir.address_of(@_QPfoo4)
  ! CHECK: fir.convert %[[f]] : ((!fir.ref<!fir.array<2x5xi32>>) -> ()) -> (() -> ())
  call bar(foo4)
end subroutine

! define, pass, call
! CHECK-LABEL: func @_QPfoo5(%arg0: !fir.ref<!fir.array<2x5xi32>>) {
subroutine foo5(i)
  integer :: i(2, 5)
  call do_something(i)
end subroutine
! CHECK-LABEL: func @_QPpass_foo5() {
subroutine pass_foo5()
  external :: foo5
  ! CHECK: %[[f:.*]] = fir.address_of(@_QPfoo5)
  ! CHECK: fir.convert %[[f]] : ((!fir.ref<!fir.array<2x5xi32>>) -> ()) -> (() -> ())
  call bar(foo5)
end subroutine
! CHECK-LABEL: func @_QPcall_foo5(%arg0: !fir.ref<!fir.array<10xi32>>) {
subroutine call_foo5(i)
  integer :: i(10)
  ! %[[argconvert:*]] = fir.convert %arg0 :
  ! fir.call @_QPfoo5(%[[argconvert]]) : (!fir.ref<!fir.array<2x5xi32>>) -> ()
  call foo5(i)
end subroutine 


! Test when there is no definition (declaration at the end of the mlir module)
! First use gives the function type

! call, pass
! CHECK-LABEL: func @_QPcall_foo6(%arg0: !fir.ref<!fir.array<10xi32>>) {
subroutine call_foo6(i)
  integer :: i(10)
  ! CHECK-NOT: convert
  call foo6(i)
end subroutine 
! CHECK-LABEL: func @_QPpass_foo6() {
subroutine pass_foo6()
  external :: foo6
  ! CHECK: %[[f:.*]] = fir.address_of(@_QPfoo6) : (!fir.ref<!fir.array<10xi32>>) -> ()
  ! CHECK: fir.convert %[[f]] : ((!fir.ref<!fir.array<10xi32>>) -> ()) -> (() -> ())
  call bar(foo6)
end subroutine

! pass, call
! CHECK-LABEL: func @_QPpass_foo7() {
subroutine pass_foo7()
  external :: foo7
  ! CHECK-NOT: convert
  call bar(foo7)
end subroutine
! CHECK-LABEL: func @_QPcall_foo7(%arg0: !fir.ref<!fir.array<10xi32>>) -> f32 {
function call_foo7(i)
  integer :: i(10)
  ! CHECK: %[[f:.*]] = fir.address_of(@_QPfoo7) : () -> ()
  ! CHECK: %[[funccast:.*]] = fir.convert %[[f]] : (() -> ()) -> ((!fir.ref<!fir.array<10xi32>>) -> f32)
  ! CHECK: fir.call %[[funccast]](%arg0) : (!fir.ref<!fir.array<10xi32>>) -> f32
  call_foo7 =  foo7(i)
end function 


! call, call with different type
! CHECK-LABEL: func @_QPcall_foo8(%arg0: !fir.ref<!fir.array<10xi32>>) {
subroutine call_foo8(i)
  integer :: i(10)
  ! CHECK-NOT: convert
  call foo8(i)
end subroutine 
! CHECK-LABEL: func @_QPcall_foo8_2(%arg0: !fir.ref<!fir.array<2x5xi32>>) {
subroutine call_foo8_2(i)
  integer :: i(2, 5)
  ! %[[argconvert:*]] = fir.convert %arg0 :
  call foo8(i)
end subroutine 

! CHECK: func @_QPfoo6(!fir.ref<!fir.array<10xi32>>)
! CHECK: func @_QPfoo7()
