; ===========================================================================
; ---------------------------------------------------------------------------
; Equates
; ---------------------------------------------------------------------------

R_StackSP	=	$00FFFFF6				; stack address

; ---------------------------------------------------------------------------
; In-game stat RAM - derived from GENS savestate offsets, verified in code
; ---------------------------------------------------------------------------

R_CrowdPeak	=	$00FFC314				; peak crowd level byte
R_GoalsHome	=	$00FFC6DA				; home goals word
R_ShotsHome	=	$00FFC6CE				; home shots word
R_GoalsAway	=	$00FFCA3E				; away goals word
R_ShotsAway	=	$00FFCA32				; away shots word

; ---------------------------------------------------------------------------
; Save game and menu RAM - verified against SRAM and menu routines
; ---------------------------------------------------------------------------

R_SaveBuffer	=	$00FF0000				; save game buffer, $2000 bytes
R_SaveBadFlag	=	$00FFD458				; save checksum failed flag
R_MenuSelections	=	$00FFD048				; per menu row selection words

; ===========================================================================
