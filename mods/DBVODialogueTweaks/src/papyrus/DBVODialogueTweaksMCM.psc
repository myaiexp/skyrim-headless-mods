Scriptname DBVODialogueTweaksMCM extends SKI_ConfigBase

Float Property fPadMs = 250.0 Auto    ; ms gap after the player's line actually ends (0 = instant)
Float Property fPlayerVoiceVol = 100.0 Auto    ; percent; 100 = unchanged

Int _padOID    ; option-IDs captured in OnPageReset for dispatch
Int _volOID

Event OnConfigInit()
	ModName = "DBVO Dialogue Tweaks"
	; No tabs (like DBVO's own menu): never populate Pages — SkyUI then shows no page list and renders
	; every option on the landing ("") page, all visible the moment you click the mod. (Papyrus forbids
	; a zero-length array literal, so we leave Pages unset rather than assigning new String[0].)
EndEvent

; Pages and changed-meaning property defaults are persisted in the save; OnConfigInit never
; re-runs, so an existing save keeps its old values. Bumping GetVersion makes SkyUI's CheckVersion
; (run from Parent.OnGameReload every load) call OnVersionUpdate ONCE with the target version, which
; is where we migrate persisted state. History: v3 reseeded fPadMs (its meaning changed from a
; word-count pad to a real post-line-end gap); v4 empties the persisted Pages array so the menu
; loses its tabs (everything moves onto the landing page). Each block touches ONLY what that version
; changed, so a migration never clobbers a value the user has tuned (e.g. fPlayerVoiceVol).
Int Function GetVersion()
	Return 4
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
	If aVersion == 4
		Pages = None    ; drop the persisted tabs on existing saves (fresh saves never set Pages)
	EndIf
EndEvent

; Single tab-less screen: SkyUI renders the landing ("") page on open, so all options sit there.
Event OnPageReset(String page)
	SetCursorFillMode(TOP_TO_BOTTOM)
	_padOID = AddSliderOption("Gap after your line ends", fPadMs, "{0} ms")
	_volOID = AddSliderOption("Player voice volume", fPlayerVoiceVol, "{0}%")
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
