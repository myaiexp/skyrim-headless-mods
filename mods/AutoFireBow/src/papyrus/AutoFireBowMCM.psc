Scriptname AutoFireBowMCM extends SKI_ConfigBase
; In-game config for AutoFireBow. Settings are per-save (Auto properties), pushed one-way to the
; DLL via the AutoFireBow.* global natives. Defaults match the DLL's atomic defaults so behavior is
; identical before the first push (a fresh load, or no MCM interaction yet).

Bool  Property bEnabled      = True  Auto    ; master on/off
Float Property fDamageBonus  = 10.0  Auto    ; percent; auto-arrow damage bump (10 => x1.10)
Float Property fMinShotDelay = 0.0   Auto    ; ms between auto-shots (0 => fastest)
Int   Property iToggleKey    = -1    Auto    ; DXScanCode for the master-toggle hotkey; -1 = unbound

Int _enabledOID    ; option-IDs captured in OnPageReset for dispatch
Int _keyOID
Int _dmgOID
Int _delayOID

Event OnConfigInit()
	ModName = "AutoFireBow"
	Pages = new String[1]
	Pages[0] = "Settings"
EndEvent

Event OnPageReset(String page)
	SetCursorFillMode(TOP_TO_BOTTOM)
	_enabledOID = AddToggleOption("Enabled", bEnabled)
	_keyOID     = AddKeyMapOption("Toggle hotkey", iToggleKey)
	_dmgOID     = AddSliderOption("Auto-arrow damage bonus", fDamageBonus, "{0}%")
	_delayOID   = AddSliderOption("Min shot delay", fMinShotDelay, "{0} ms")
EndEvent

Event OnOptionSelect(Int oid)
	If oid == _enabledOID
		bEnabled = !bEnabled
		SetToggleOptionValue(oid, bEnabled)
		AutoFireBow.SetEnabled(bEnabled)
	EndIf
EndEvent

Event OnOptionKeyMapChange(Int oid, Int keyCode, String conflictControl, String conflictName)
	If oid == _keyOID
		If iToggleKey >= 0
			UnregisterForKey(iToggleKey)
		EndIf
		iToggleKey = keyCode
		SetKeyMapOptionValue(oid, keyCode)
		RegisterForKey(keyCode)
	EndIf
EndEvent

Event OnOptionSliderOpen(Int oid)
	If oid == _dmgOID
		SetSliderDialogRange(0, 100)
		SetSliderDialogDefaultValue(10)
		SetSliderDialogInterval(5)
		SetSliderDialogStartValue(fDamageBonus)
	ElseIf oid == _delayOID
		SetSliderDialogRange(0, 1000)
		SetSliderDialogDefaultValue(0)
		SetSliderDialogInterval(25)
		SetSliderDialogStartValue(fMinShotDelay)
	EndIf
EndEvent

Event OnOptionSliderAccept(Int oid, Float value)
	If oid == _dmgOID
		fDamageBonus = value
		SetSliderOptionValue(oid, value, "{0}%")
		AutoFireBow.SetDamageBonus(1.0 + value / 100.0)
	ElseIf oid == _delayOID
		fMinShotDelay = value
		SetSliderOptionValue(oid, value, "{0} ms")
		AutoFireBow.SetMinShotDelay(value)
	EndIf
EndEvent

Event OnKeyDown(Int keyCode)
	; The master-toggle hotkey, fired in gameplay. Guard against menu mode so it never toggles
	; while a menu/console is up (the MCM itself captures keys, so it won't fire there anyway).
	If keyCode == iToggleKey && !Utility.IsInMenuMode()
		bEnabled = !bEnabled
		AutoFireBow.SetEnabled(bEnabled)
		Debug.Notification("AutoFireBow " + GetEnabledText())
	EndIf
EndEvent

String Function GetEnabledText()
	If bEnabled
		Return "enabled"
	EndIf
	Return "disabled"
EndFunction

Event OnGameReload()
	Parent.OnGameReload()
	; The DLL is stateless across saves — re-push every setting, and re-establish the hotkey
	; registration (RegisterForKey does not survive a reload).
	AutoFireBow.SetEnabled(bEnabled)
	AutoFireBow.SetDamageBonus(1.0 + fDamageBonus / 100.0)
	AutoFireBow.SetMinShotDelay(fMinShotDelay)
	If iToggleKey >= 0
		RegisterForKey(iToggleKey)
	EndIf
EndEvent
