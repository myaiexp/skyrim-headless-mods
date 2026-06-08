Scriptname RapidBowHoldScript extends Quest
{ Hold the Attack control (default: left mouse / Mouse1) while a bow (or crossbow) is equipped.
  Every time the game's bow draw animation reaches the fully-drawn state (the "bowDrawn" / "BowDrawn" animation event),
  the script immediately sends a release event (full-strength shot) and commands the next draw.
  
  This produces repeated full-draw / full-damage shots at the maximum rate the equipped bow's actual animation allows,
  without any hardcoded per-shot timers for the main loop and without any custom animations or OAR data.
  
  Safety timeout and tiny inter-event waits are the *only* places a fixed duration is used (informed by UESP Archery data).
  See: https://en.uesp.net/wiki/Skyrim:Archery for draw times, charge mechanics, and damage scaling.
  
  Requirements (already in this setup):
  - SKSE (for RegisterForControl, RegisterForAnimationEvent, SendAnimationEvent, Weapon natives, etc.)
  - Papyrus Tweaks NG (recommended for script performance)
  
  Installation: see the sibling INSTALL.md (Linux/Proton only workflow using SSEEdit + Proton-invoked PapyrusCompiler).
  
  Removal: delete the .esp + the compiled .pex, remove the matching line from both Plugins.txt files.
}

; Cached player (fetched locally; no CK/SSEEdit property filling required)
Actor PlayerRef

; True while the Attack control is held down
bool isRapid = false

; Conservative safety: if "bowDrawn" is missed for this long while the flag is set, force a release+redraw cycle.
; Adjust downward for snappier fallback or upward for very slow-draw bows/mods. Real per-bow times live in the animation + weapon data.
float safetyMaxDrawSeconds = 2.0

; Track the last time we forced a safety cycle (simple rate-limit on the fallback)
float lastSafetyTime = 0.0

; ---------------------------------------------------------------------------
; Lifecycle
; ---------------------------------------------------------------------------

Event OnInit()
    PlayerRef = Game.GetPlayer()
    ; LMB draws the bow. By default LMB maps to the user-event "Right Attack/Block"
    ; (right hand = main weapon); RMB maps to "Left Attack/Block". "Attack" is NOT a
    ; valid control name. Register both attack controls for robustness against rebinds.
    RegisterForControl("Right Attack/Block")
    RegisterForControl("Left Attack/Block")
    RegisterBowEvents()
    Debug.Trace("[RapidBowHold] Initialized for player " + PlayerRef)
EndEvent

Event OnPlayerLoadGame()
    ; Re-register everything cleanly (saves can clear registrations).
    ; There is no "unregister all animation events" native; re-registering the
    ; same events below is harmless, so we only drop control registrations here.
    UnregisterForAllControls()
    PlayerRef = Game.GetPlayer()
    RegisterForControl("Right Attack/Block")
    RegisterForControl("Left Attack/Block")
    RegisterBowEvents()
    Debug.Trace("[RapidBowHold] Re-registered after load")
EndEvent

; ---------------------------------------------------------------------------
; Input (the "hold mouse 1" part)
; ---------------------------------------------------------------------------

Event OnControlDown(string control)
    if (control == "Right Attack/Block" || control == "Left Attack/Block") && !isRapid
        isRapid = true
        Debug.Trace("[RapidBowHold] Rapid mode engaged (Attack pressed)")

        ; Make sure we have a fighting chance at a first cycle
        if PlayerRef && IsBowEquipped() && !PlayerRef.IsWeaponDrawn()
            PlayerRef.DrawWeapon()
            ; Some setups benefit from an explicit start event too:
            ; PlayerRef.SendAnimationEvent("BowDraw")
        endif

        ; Arm a safety net in case the bowDrawn event is never seen for this draw
        RegisterForSingleUpdate(safetyMaxDrawSeconds)
    endif
EndEvent

Event OnControlUp(string control, float holdTime)
    if control == "Right Attack/Block" || control == "Left Attack/Block"
        isRapid = false
        Debug.Trace("[RapidBowHold] Rapid mode disengaged (Attack released)")
        ; We do NOT cancel an in-flight draw here; let the current animation finish naturally
        ; or let the player manually release. This feels the most "normal".
    endif
EndEvent

; ---------------------------------------------------------------------------
; The heart: react to the game telling us the bow is fully drawn
; ---------------------------------------------------------------------------

Event OnAnimationEvent(ObjectReference akSource, string asEventName)
    if akSource != PlayerRef
        return
    endif

    ; DIAGNOSTIC: log every player animation event we registered for, with timing,
    ; so the Papyrus log reveals this setup's real draw -> full-draw -> release cycle.
    Debug.Trace("[RapidBowHold][anim] '" + asEventName + "' t=" + Utility.GetCurrentRealTime() + " isRapid=" + isRapid)

    if !isRapid
        return
    endif

    ; Only a TRUE full-draw fires a shot. Draw-START events ("bowDraw"/"bowDrawStart")
    ; fire the instant drawing begins, which caused the premature release before.
    if asEventName == "bowDrawn" || asEventName == "BowDrawn"
        if IsValidRapidState()
            FireFullDrawAndRestart()
        endif
    endif
EndEvent

; Safety fallback: the bowDrawn event can occasionally be missed (race with other animation systems,
; certain replacers, very high script load, etc.). After safetyMaxDrawSeconds we force a cycle
; so the "hold to rapid" feeling never gets stuck.
Event OnUpdate()
    if isRapid && IsValidRapidState()
        float now = Utility.GetCurrentRealTime()
        if (now - lastSafetyTime) > (safetyMaxDrawSeconds * 0.9)
            Debug.Trace("[RapidBowHold] Safety timer fired - forcing release + redraw (event may have been missed)")
            lastSafetyTime = now
            FireFullDrawAndRestart()
        endif
        ; Re-arm while still holding
        RegisterForSingleUpdate(safetyMaxDrawSeconds)
    endif
EndEvent

; ---------------------------------------------------------------------------
; Core action + guards
; ---------------------------------------------------------------------------

function FireFullDrawAndRestart()
    ; Release the fully-drawn arrow. "attackRelease" is confirmed working on this setup.
    Debug.Trace("[RapidBowHold] >>> FIRE attackRelease t=" + Utility.GetCurrentRealTime())
    Debug.SendAnimationEvent(PlayerRef, "attackRelease")

    ; Let the engine process the release / spawn the arrow before commanding a new draw.
    Utility.Wait(0.05)

    ; Redraw: the correct draw-initiation event for this setup is still unknown, so send the
    ; most likely candidates. The [anim] diagnostic trace will show which one actually kicks a
    ; new draw (i.e. which is followed by a fresh "bowDrawn"); we then keep only that one.
    Debug.Trace("[RapidBowHold] >>> REDRAW bowAttackStart + BowDrawStart + bowDraw")
    Debug.SendAnimationEvent(PlayerRef, "bowAttackStart")
    Debug.SendAnimationEvent(PlayerRef, "BowDrawStart")
    Debug.SendAnimationEvent(PlayerRef, "bowDraw")
endFunction

; Register every bow event we either act on (bowDrawn/BowDrawn) or want to observe while
; hunting for the real redraw trigger. Registering for events that never fire is harmless.
function RegisterBowEvents()
    RegisterForAnimationEvent(PlayerRef, "bowDrawn")
    RegisterForAnimationEvent(PlayerRef, "BowDrawn")
    RegisterForAnimationEvent(PlayerRef, "bowDraw")
    RegisterForAnimationEvent(PlayerRef, "bowDrawStart")
    RegisterForAnimationEvent(PlayerRef, "BowDrawStart")
    RegisterForAnimationEvent(PlayerRef, "bowAttackStart")
    RegisterForAnimationEvent(PlayerRef, "attackStart")
    RegisterForAnimationEvent(PlayerRef, "arrowAttach")
    RegisterForAnimationEvent(PlayerRef, "attackRelease")
    RegisterForAnimationEvent(PlayerRef, "arrowRelease")
    RegisterForAnimationEvent(PlayerRef, "BowRelease")
    RegisterForAnimationEvent(PlayerRef, "bowReset")
    RegisterForAnimationEvent(PlayerRef, "BowReset")
    RegisterForAnimationEvent(PlayerRef, "attackStop")
    RegisterForAnimationEvent(PlayerRef, "weaponSwing")
endFunction

bool function IsBowEquipped()
    if !PlayerRef
        return false
    endif
    Weapon w = PlayerRef.GetEquippedWeapon()
    if !w
        return false
    endif
    ; Animation weapon-type enum: 7 = bow, 9 = crossbow. IsBow() is an SKSE helper;
    ; there is no IsCrossbow() native, so crossbows are matched via GetWeaponType() == 9.
    return w.IsBow() || w.GetWeaponType() == 7 || w.GetWeaponType() == 9
endFunction

bool function IsValidRapidState()
    if !PlayerRef || !PlayerRef.IsWeaponDrawn()
        return false
    endif

    ; Block in situations where spamming arrows would be annoying or dangerous
    if UI.IsMenuOpen("Dialogue Menu") || \
       UI.IsMenuOpen("TweenMenu") || \
       UI.IsMenuOpen("Crafting Menu") || \
       UI.IsMenuOpen("MagicMenu") || \
       UI.IsMenuOpen("InventoryMenu") || \
       UI.IsTextInputEnabled() || \
       Utility.IsInMenuMode() || \
       PlayerRef.GetSitState() != 0 || \
       PlayerRef.IsOnMount()
        return false
    endif

    return IsBowEquipped()
endFunction

; ---------------------------------------------------------------------------
; Optional future polish hooks (left here as comments for later)
; ---------------------------------------------------------------------------
; - Add an MCM (MCM Helper is already installed) with a slider for safetyMaxDrawSeconds and/or a master enable toggle.
; - Expose a GlobalVariable or spell that forces isRapid on/off for "toggle mode" fans.
; - Per-weapon-type different tiny wait or different event pairs (very rarely needed).
; - Count shots or implement a "rapid archery" perk that only enables the behavior when the perk is present.
;
; None of that is required for the core request: hold LMB → full-draw loop as fast as the bow allows.
