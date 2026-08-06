; ===========================================================================
; ---------------------------------------------------------------------------
; Macro functions
; ---------------------------------------------------------------------------

	; --- Padding/Alignment ---

align		macro	Amount, Value
		dcb.b	Amount-(*%Amount),Value
		endm

	; --- Compare non-immediate immediate ---

d0 = %000
d1 = %001
d2 = %010
d3 = %011
d4 = %100
d5 = %101
d6 = %110
d7 = %111

cmpnb		macro	Value,Dest
		dc.b	%10110000|(Dest<<$01)
		dc.b	%00111100
		dc.w	Value
		endm

cmpnw		macro	Value,Dest
		dc.b	%10110000|(Dest<<$01)
		dc.b	%01111100
		dc.w	Value
		endm

cmpnl		macro	Value,Dest
		dc.b	%10110000|(Dest<<$01)
		dc.b	%10111100
		dc.l	Value
		endm

; ===========================================================================