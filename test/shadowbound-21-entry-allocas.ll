; BUG 21: a merged check on an argument splits the entry block above its allocas.
;
; Where: OverflowDefense.cpp, collectChunkCheckImpl() (InsertPt for an
;        Argument source is the entry block's first insertion point) and
;        commitClusterCheck().
;
; The heap-range test is split off at the very top of the entry block. Every
; static alloca behind that point moves to a non-entry block and becomes a
; dynamic alloca: it is no longer folded into the frame, and stack coloring /
; SROA no longer treat it as a fixed slot.
;
; Expected: the alloca stays in the entry block.
;
; RUN: %opt -opaque-pointers=0 -passes=shadowbound -S %s | FileCheck %s

declare void @use(i8*)

; CHECK-LABEL: define void @f(
; CHECK-NEXT: entry:
; CHECK-NEXT: %buf = alloca [16 x i8]
define void @f(i8* %p, i64 %n) {
entry:
  %buf = alloca [16 x i8]
  %b = getelementptr [16 x i8], [16 x i8]* %buf, i64 0, i64 0
  call void @use(i8* %b)
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %g = getelementptr i8, i8* %p, i64 %i
  store i8 0, i8* %g
  %i.next = add i64 %i, 1
  %c = icmp slt i64 %i.next, %n
  br i1 %c, label %loop, label %exit

exit:
  ret void
}
