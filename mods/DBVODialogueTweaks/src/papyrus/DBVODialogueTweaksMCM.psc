Scriptname DBVODialogueTweaksMCM extends SKI_ConfigBase

Float Property fMsPerWord = 200.0  Auto    ; stock default (= 300 wpm: 60/300*1000)
Float Property fPadMs     = 1400.0 Auto    ; stock default

Int _mspwOID    ; option-IDs captured in OnPageReset for dispatch
Int _padOID

Event OnConfigInit()
	ModName = "DBVO Dialogue Tweaks"
	Pages = new String[1]
	Pages[0] = "Timing"
EndEvent

Event OnPageReset(String page)
	SetCursorFillMode(TOP_TO_BOTTOM)
	_mspwOID = AddSliderOption("Per-word delay", fMsPerWord, "{0} ms")
	_padOID  = AddSliderOption("NPC response pad", fPadMs, "{0} ms")
EndEvent

Event OnOptionSliderOpen(Int oid)
	If oid == _mspwOID
		SetSliderDialogRange(0, 500)
		SetSliderDialogDefaultValue(200)
		SetSliderDialogInterval(10)
		SetSliderDialogStartValue(fMsPerWord)
	ElseIf oid == _padOID
		SetSliderDialogRange(0, 2500)
		SetSliderDialogDefaultValue(1400)
		SetSliderDialogInterval(25)
		SetSliderDialogStartValue(fPadMs)
	EndIf
EndEvent

Event OnOptionSliderAccept(Int oid, Float value)
	If oid == _mspwOID
		fMsPerWord = value
		SetSliderOptionValue(oid, value, "{0} ms")
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
		UI.SetFloat("Dialogue Menu", "_root.DialogueMenu_mc.dbvoMsPerWord", fMsPerWord)
		UI.SetFloat("Dialogue Menu", "_root.DialogueMenu_mc.dbvoPadMs",     fPadMs)
	endif
EndEvent
