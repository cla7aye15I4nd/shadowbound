; The access size checked at a GEP comes from what is accessed through it, not
; from its (typed-pointer) result type.
;
; Where: ShadowBound.cpp, getAccessSize().
;
; SPEC 520.omnetpp_r: CommentElement::setLocid assigns the 32-byte std::string
; at offset 0x70 of a 0xb0-byte object. The optimizer writes &this->locid as
; `getelementptr %Base, %this, i64 1` (sizeof(%Base) = 0x70), and the check
; demanded room for a whole %Base there (0x70 + 0x70 > 0xb0), aborting.
;
; Expected:
;  * @cast_only: the GEP is only cast to i64*, so the check covers the minimum
;    32 bytes (the reserve), not 112;
;  * @deref_whole: the GEP is accessed as a whole %Base, so the check still
;    covers all 112 bytes.
;
; RUN: %opt -opaque-pointers=0 -passes='shadowbound-module,require<shadowbound-ipo>,function(shadowbound)' -S %s | FileCheck %s

%Base = type { [14 x i64] }

declare void @sink(%Base*)

; CHECK-LABEL: @cast_only(
; CHECK: add i64 %{{[0-9]+}}, 32
; CHECK-NOT: add i64 %{{[0-9]+}}, 112
; CHECK: ret void
define void @cast_only(%Base* %this) {
  %g = getelementptr inbounds %Base, %Base* %this, i64 1
  %s = bitcast %Base* %g to i64*
  store i64 0, i64* %s
  ret void
}

; CHECK-LABEL: @deref_whole(
; CHECK: add i64 %{{[0-9]+}}, 112
; CHECK: ret void
define void @deref_whole(%Base* %this) {
  %g = getelementptr inbounds %Base, %Base* %this, i64 1
  %v = load %Base, %Base* %g
  call void @sink(%Base* %this)
  ret void
}
