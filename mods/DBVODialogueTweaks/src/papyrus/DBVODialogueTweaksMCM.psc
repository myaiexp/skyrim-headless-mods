Scriptname DBVODialogueTweaksMCM extends SKI_ConfigBase

Float Property fPadMs = 250.0 Auto    ; ms gap after the player's line actually ends (0 = instant)
Float Property fPlayerVoiceVol = 100.0 Auto    ; percent; 100 = unchanged

Int _padOID    ; option-IDs captured in OnPageReset for dispatch
Int _volOID

Event OnConfigInit()
	ModName = "DBVO Dialogue Tweaks"
	Pages = new String[2]
	Pages[0] = "Timing"
	Pages[1] = "Voice"
EndEvent

; v2 saves registered this MCM at version 1 with only the "Timing" page; OnConfigInit
; never re-runs, so the persisted Pages array stays 1-wide. Bumping GetVersion makes
; SkyUI's CheckVersion (run from Parent.OnGameReload every load) call OnVersionUpdate
; ONCE with the target version — it migrates the page list and seeds a property whose
; default/meaning changed (an Auto property isn't guaranteed its declared default on an
; existing save). v5 (version 3) reseeds fPadMs: it used to pad a word-count guess (1400);
; it's now a gap after the line's REAL end, so the persisted old value is stale. The v3
; bump touches fPadMs ONLY — a stored-v2 save already has the page list + fPlayerVoiceVol,
; and re-seeding the volume would wipe a tuned value.
Int Function GetVersion()
	Return 3
EndFunction

Event OnVersionUpdate(Int aVersion)
	If aVersion == 2
		fPlayerVoiceVol = 100.0
		Pages = new String[2]
		Pages[0] = "Timing"
		Pages[1] = "Voice"
	EndIf
	If aVersion == 3
		fPadMs = 250.0
	EndIf
EndEvent

Event OnPageReset(String page)
	If page == "Timing"
		SetCursorFillMode(TOP_TO_BOTTOM)
		_padOID = AddSliderOption("Gap after your line ends", fPadMs, "{0} ms")
	ElseIf page == "Voice"
		SetCursorFillMode(TOP_TO_BOTTOM)
		_volOID = AddSliderOption("Player voice volume", fPlayerVoiceVol, "{0}%")
	EndIf
EndEvent

Event OnOptionSliderOpen(Int oid)
	If oid == _padOID
		SetSliderDialogRange(0, 1000)
		SetSliderDialogDefaultValue(250)
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
	If oid == _padOID
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
		UI.SetFloat("Dialogue Menu", "_root.DialogueMenu_mc.dbvoPadMs", fPadMs)
	endif
EndEvent
