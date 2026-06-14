Scriptname AutoFireBow Hidden
; One-way Papyrus -> DLL config push from AutoFireBowMCM. Registered by AutoFireBow.dll
; (class string "AutoFireBow" in RegisterFunction). Each setter just stores into an atomic
; the bow-loop hooks read.
Function SetEnabled(Bool abEnabled) Global Native
Function SetDamageBonus(Float aMult) Global Native    ; finished multiplier: 1.0 + pct/100 (1.10 = +10%)
Function SetMinShotDelay(Float aMs) Global Native
