Scriptname DBVODialogueTweaksMCM extends SKI_ConfigBase

Float Property fWpm   = 300.0  Auto    ; stock default
Float Property fPadMs = 1400.0 Auto    ; stock default

Int _wpmOID    ; option-IDs captured in OnPageReset for dispatch
Int _padOID

Event OnConfigInit()
	ModName = "DBVO Dialogue Tweaks"
	Pages = new String[1]
	Pages[0] = "Timing"
EndEvent

Event OnPageReset(String page)
	SetCursorFillMode(TOP_TO_BOTTOM)
	_wpmOID = AddSliderOption("Voice-pack speed", fWpm, "{0} wpm")
	_padOID = AddSliderOption("NPC response pad", fPadMs, "{0} ms")
EndEvent

Event OnOptionSliderOpen(Int oid)
	If oid == _wpmOID
		SetSliderDialogRange(150, 600)
		SetSliderDialogDefaultValue(300)
		SetSliderDialogInterval(10)
		SetSliderDialogStartValue(fWpm)
	ElseIf oid == _padOID
		SetSliderDialogRange(0, 2500)
		SetSliderDialogDefaultValue(1400)
		SetSliderDialogInterval(25)
		SetSliderDialogStartValue(fPadMs)
	EndIf
EndEvent

Event OnOptionSliderAccept(Int oid, Float value)
	If oid == _wpmOID
		fWpm = value
		SetSliderOptionValue(oid, value, "{0} wpm")
	ElseIf oid == _padOID
		fPadMs = value
		SetSliderOptionValue(oid, value, "{0} ms")
	EndIf
EndEvent

Event OnGameReload()
	Parent.OnGameReload()
	RegisterForMenu("Dialogue Menu")
EndEvent

Event OnMenuOpen(String menuName)
	if menuName == "Dialogue Menu"
		If fWpm > 0
			UI.SetFloat("Dialogue Menu", "_root.DialogueMenu_mc.dbvoWpm",   fWpm)
			UI.SetFloat("Dialogue Menu", "_root.DialogueMenu_mc.dbvoPadMs", fPadMs)
		EndIf
	endif
EndEvent
