; Intermediate-pointer elision must not drop a lower-bound check that only the
; intermediate pointer carries.
;
; Where: ShadowBound.cpp, dropIntermediateChecks().
;
; A pointer whose users are all checked GEPs needs no check of its own: each
; GEP's check covers every direction of the offset path from the source. A
; bitcast's check, however, is an access-size check on the upper bound only.
; Here `%q = %p - 8` is only used by a bitcast to a 64-byte struct; if %q's own
; check were dropped, nothing would check that %q is not below the object.
;
; Expected: %q keeps its check (lower bound: icmp ugt Begin, %q), and the
; bitcast keeps its access-size check.
;
; RUN: %opt -opaque-pointers=0 -passes='shadowbound-module,require<shadowbound-ipo>,function(shadowbound)' -S %s | FileCheck %s

%struct.big = type { [8 x i64] }

declare void @use(%struct.big*)

; CHECK-LABEL: @f(
; CHECK: %q = getelementptr i8, i8* %p, i64 -8
; CHECK: call void @__shadowbound_abort()
; CHECK: call void @__shadowbound_abort()
; CHECK: ret void
define void @f(i8* %p) {
  %q = getelementptr i8, i8* %p, i64 -8
  %b = bitcast i8* %q to %struct.big*
  call void @use(%struct.big* %b)
  ret void
}
