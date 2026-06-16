Scriptname RapidBowHoldScript extends Quest
{ Hold the attack control with a bow/crossbow equipped -> auto-fire shots in a loop.

  PROOF-OF-CONCEPT / SHELVED. This is the last working *loop* version: it fires on the
  full-draw event, waits a beat for the release to process, then commands the next draw. It
  loops correctly, but the arrows are UNCHARGED (weak) -- animation events are cosmetic and
  cannot drive the engine's bow charge. See docs/papyrus-limits.md. Kept as a
  reference for the Papyrus pipeline (control hooks + animation events + save-bake behavior).

  Requires SKSE. }

Actor PlayerRef
bool isRapid = false

; Beat between releasing and restarting the draw, so the release animation completes before
; the next draw is commanded. Removing it breaks the loop (no full-draw event fires).
float releaseToRedraw = 0.05

; Safety fallback only: force a cycle if a full-draw event is somehow missed.
float safetyMaxDrawSeconds = 2.0
float lastSafetyTime = 0.0

; ---------------------------------------------------------------------------
; Lifecycle
; ---------------------------------------------------------------------------

Event OnInit()
    PlayerRef = Game.GetPlayer()
    RegisterControls()
    RegisterBowEvents()
    Debug.Trace("[RBH] Initialized for player " + PlayerRef)
EndEvent

Event OnPlayerLoadGame()
    UnregisterForAllControls()
    PlayerRef = Game.GetPlayer()
    RegisterControls()
    RegisterBowEvents()
    Debug.Trace("[RBH] Re-registered after load")
EndEvent

function RegisterControls()
    ; LMB = user-event "Right Attack/Block" (right hand = main weapon); RMB = "Left Attack/Block".
    RegisterForControl("Right Attack/Block")
    RegisterForControl("Left Attack/Block")
endFunction

function RegisterBowEvents()
    RegisterForAnimationEvent(PlayerRef, "bowDrawn")
    RegisterForAnimationEvent(PlayerRef, "BowDrawn")
endFunction

; ---------------------------------------------------------------------------
; Input
; ---------------------------------------------------------------------------

Event OnControlDown(string control)
    if (control == "Right Attack/Block" || control == "Left Attack/Block") && !isRapid
        isRapid = true
        if PlayerRef && IsBowEquipped() && !PlayerRef.IsWeaponDrawn()
            PlayerRef.DrawWeapon()
        endif
        lastSafetyTime = Utility.GetCurrentRealTime()
        RegisterForSingleUpdate(safetyMaxDrawSeconds)
    endif
EndEvent

Event OnControlUp(string control, float holdTime)
    if control == "Right Attack/Block" || control == "Left Attack/Block"
        isRapid = false
    endif
EndEvent

; ---------------------------------------------------------------------------
; Fire on full draw, then restart
; ---------------------------------------------------------------------------

Event OnAnimationEvent(ObjectReference akSource, string asEventName)
    if akSource != PlayerRef || !isRapid
        return
    endif
    if asEventName == "bowDrawn" || asEventName == "BowDrawn"
        if IsValidRapidState()
            FireAndRestart()
        endif
    endif
EndEvent

Event OnUpdate()
    if isRapid
        ; Re-arm the self-perpetuating timer whenever the hold is still active, even if the
        ; state is transiently invalid (menu open, mounted, sheathed) -- otherwise a single
        ; bad poll would kill the timer for the rest of the hold. The fire decision stays
        ; gated on a valid state + elapsed time.
        if IsValidRapidState()
            float now = Utility.GetCurrentRealTime()
            if (now - lastSafetyTime) > (safetyMaxDrawSeconds * 0.9)
                Debug.Trace("[RBH] SAFETY fired (full-draw event missed)")
                lastSafetyTime = now
                FireAndRestart()
            endif
        endif
        RegisterForSingleUpdate(safetyMaxDrawSeconds)
    endif
EndEvent

; ---------------------------------------------------------------------------
; Core action + guards
; ---------------------------------------------------------------------------

function FireAndRestart()
    ; Stamp the draw cycle so the safety timer measures elapsed time since the last actual
    ; fire (not since save load) -- otherwise the first poll always trips the safety.
    lastSafetyTime = Utility.GetCurrentRealTime()
    Debug.SendAnimationEvent(PlayerRef, "attackRelease")
    Utility.Wait(releaseToRedraw)
    Debug.SendAnimationEvent(PlayerRef, "bowAttackStart")
    Debug.SendAnimationEvent(PlayerRef, "BowDrawStart")
    Debug.SendAnimationEvent(PlayerRef, "bowDraw")
endFunction

bool function IsBowEquipped()
    if !PlayerRef
        return false
    endif
    Weapon w = PlayerRef.GetEquippedWeapon()
    if !w
        return false
    endif
    ; Animation weapon-type enum: 7 = bow, 9 = crossbow.
    return w.IsBow() || w.GetWeaponType() == 7 || w.GetWeaponType() == 9
endFunction

bool function IsValidRapidState()
    if !PlayerRef || !PlayerRef.IsWeaponDrawn()
        return false
    endif
    if UI.IsMenuOpen("Dialogue Menu") || \
       UI.IsMenuOpen("Crafting Menu") || \
       UI.IsMenuOpen("InventoryMenu") || \
       UI.IsTextInputEnabled() || \
       Utility.IsInMenuMode() || \
       PlayerRef.GetSitState() != 0 || \
       PlayerRef.IsOnMount()
        return false
    endif
    return IsBowEquipped()
endFunction
