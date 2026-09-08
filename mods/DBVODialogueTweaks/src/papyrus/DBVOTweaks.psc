Scriptname DBVOTweaks Hidden
; factor 0.0-3.0; 1.0 = pass-through. Below 1.0 the DLL attenuates the player line's sound handle
; (the engine clamps that path at 1.0); above 1.0 it applies the factor as gain on the line's
; XAudio2 voice from its SetVolumeImpl hook (v6). Registered by DBVODialogueTweaks.dll.
Function SetPlayerVoiceVolume(Float factor) Global Native
