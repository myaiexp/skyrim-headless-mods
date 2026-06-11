Scriptname DBVODialogueTweaksMCM extends SKI_ConfigBase

Float Property fMsPerWord = 200.0  Auto    ; stock default (= 300 wpm: 60/300*1000)
Float Property fPadMs     = 1400.0 Auto    ; stock default
Float Property fPlayerVoiceVol = 100.0 Auto    ; percent; 100 = unchanged

Int _mspwOID    ; option-IDs captured in OnPageReset for dispatch
Int _padOID
Int _volOID

Event OnConfigInit()
	ModName = "DBVO Dialogue Tweaks"
	Pages = new String[2]
	Pages[0] = "Timing"
	Pages[1] = "Voice"
EndEvent

; v2 saves registered this MCM at version 1 with only the "Timing" page; OnConfigInit
; never re-runs, so the persisted Pages array stays 1-wide. Bumping GetVersion makes
; SkyUI's CheckVersion (run from Parent.OnGameReload every load) call OnVersionUpdate,
; which migrates the page list — and seeds fPlayerVoiceVol, since a newly-added Auto
; property isn't guaranteed its declared 100.0 default on an existing save (0.0 = mute).
Int Function GetVersion()
	Return 2
EndFunction

Event OnVersionUpdate(Int aVersion)
	If aVersion == 2
		fPlayerVoiceVol = 100.0
		Pages = new String[2]
		Pages[0] = "Timing"
		Pages[1] = "Voice"
	EndIf
EndEvent

Event OnPageReset(String page)
	If page == "Timing"
		SetCursorFillMode(TOP_TO_BOTTOM)
		_mspwOID = AddSliderOption("Per-word delay", fMsPerWord, "{0} ms")
		_padOID  = AddSliderOption("NPC response pad", fPadMs, "{0} ms")
	ElseIf page == "Voice"
		SetCursorFillMode(TOP_TO_BOTTOM)
		_volOID = AddSliderOption("Player voice volume", fPlayerVoiceVol, "{0}%")
	EndIf
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
	ElseIf oid == _volOID
		SetSliderDialogRange(0, 100)
		SetSliderDialogDefaultValue(100)
		SetSliderDialogInterval(5)
		SetSliderDialogStartValue(fPlayerVoiceVol)
	EndIf
EndEvent

Event OnOptionSliderAccept(Int oid, Float value)
	If oid == _mspwOID
		fMsPerWord = value
		SetSliderOptionValue(oid, value, "{0} ms")
	ElseIf oid == _padOID
		fPadMs = value
		SetSliderOptionValue(oid, value, "{0} ms")
	ElseIf oid == _volOID
		fPlayerVoiceVol = value
		SetSliderOptionValue(oid, value, "{0}%")
		DBVOTweaks.SetPlayerVoiceVolume(fPlayerVoiceVol / 100.0)
	EndIf
EndEvent

Event OnGameReload()
	Parent.OnGameReload()
	RegisterForMenu("Dialogue Menu")
	DBVOTweaks.SetPlayerVoiceVolume(fPlayerVoiceVol / 100.0)
EndEvent

Event OnMenuOpen(String menuName)
	if menuName == "Dialogue Menu"
		UI.SetFloat("Dialogue Menu", "_root.DialogueMenu_mc.dbvoMsPerWord", fMsPerWord)
		UI.SetFloat("Dialogue Menu", "_root.DialogueMenu_mc.dbvoPadMs",     fPadMs)
	endif
EndEvent
