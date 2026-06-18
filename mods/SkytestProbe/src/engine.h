#pragma once
// Main-thread engine helpers. EVERYTHING here touches live engine state and must
// run on the main thread (via SKSE::GetTaskInterface()->AddTask) — never from the
// command-poll thread. All paths are null-safe: an unresolvable ref / unloaded
// actor degrades to nullptr/empty/an error line, never a crash.
#include "resolve.h"
#include "worldstate.h"
#include "console_exec.h"
#include "actor_setup.h"
#include "dump_actor.h"
#include "facegen.h"
#include "facegen_ramp.h"
#include "mcm.h"