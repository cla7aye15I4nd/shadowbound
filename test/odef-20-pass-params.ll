; BUG 20: the odef<...> pass parameters can never be parsed.
;
; Where: PassBuilder.cpp, parseOdefPassOptions().
;
; The loop never split Params into a parameter name, so ParamName was always
; empty and every non-empty parameter list was rejected with
; "invalid ShadowBound pass parameter ''", making `odef<recover>` unusable.
;
; Expected: odef<recover> is accepted and an unknown parameter is rejected.
;
; RUN: %opt -opaque-pointers=0 -passes='shadowbound<recover>' -disable-output %s
; RUN: not %opt -opaque-pointers=0 -passes='shadowbound<bogus>' -disable-output %s 2>&1 | FileCheck %s
;
; CHECK: invalid ShadowBound pass parameter

define void @f() {
  ret void
}
