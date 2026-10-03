; BUG 20: the odef<...> pass parameters can never be parsed.
;
; Where: PassBuilder.cpp, parseOdefPassOptions().
;
; The loop never splits Params into a parameter name, so ParamName is always
; empty and every non-empty parameter list is rejected with
; "invalid OverflowDefense pass parameter ''". `odef<recover>` and
; `odef<kernel>` are documented in PassRegistry.def but unusable.
;
; Expected: both pipelines are accepted.
;
; RUN: %opt -opaque-pointers=0 -passes='odef<recover>' -disable-output %s
; RUN: %opt -opaque-pointers=0 -passes='odef<kernel>' -disable-output %s
; RUN: %opt -opaque-pointers=0 -passes='odef<recover;kernel>' -disable-output %s

define void @f() {
  ret void
}
