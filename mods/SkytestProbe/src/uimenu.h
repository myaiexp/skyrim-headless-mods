#pragma once
// Call a Scaleform (GFx) method inside an open game menu.
//
// Why this exists: a menu-side mod is normally driven by its own Papyrus, which reaches the
// swf through the SKSE native UI.InvokeString/InvokeNumber. When that Papyrus can't run — a
// dead framework DLL, a missing master, a mod whose scripts you deliberately left out to
// isolate the C++ half — the swf sits in a state no input can reach, and the mod under test
// looks broken when it is only unstimulated. This is the direct-call substitute: it invokes
// the same method with the same arguments, from C++, so a test can supply exactly the one
// call the missing Papyrus would have made.
//
// It is a general instrument, not a DBVO one: any `_root.<Menu>_mc.<method>` in any open menu
// is reachable. (`skytest drive` can reach the console's command table but nothing inside a
// menu's ActionScript; this closes that gap the same way `placeatme` closed console staging.)
#include <string>
#include <vector>

namespace engine
{
	enum class InvokeResult
	{
		kOk,
		kNoUI,        // RE::UI singleton unavailable (called before the UI exists)
		kMenuClosed,  // that menu is not currently open
		kNoMovie,     // the menu is open but carries no GFx movie (a native menu)
		kFailed       // the movie rejected the call (bad path / wrong arity)
	};

	// Invoke a_method (a full AS path, e.g. "_root.DialogueMenu_mc.startTopicClickedTimer") on
	// the open menu named a_menu (the CommonLib MENU_NAME, e.g. "Dialogue Menu"), passing every
	// entry of a_args as a GFx STRING argument. Main-thread only; null-safe at every step —
	// a closed menu is a reported result, never a crash. Return value is discarded (the AS side
	// of a menu mod returns nothing); this is a stimulus, not a query.
	InvokeResult InvokeMenuMethod(const std::string& a_menu, const std::string& a_method,
		const std::vector<std::string>& a_args);

	// Set an AS variable on an open menu — the direct-call form of the SKSE natives
	// UI.SetFloat / UI.SetString, which is how an MCM normally pushes its settings into a menu
	// swf. Needed for the same reason as InvokeMenuMethod: with the mod's Papyrus absent the
	// swf runs on whatever defaults it compiled in, and a test that wants a specific value
	// (a distinctive gap, a flag) has no other way to set one. a_number takes precedence when
	// supplied; otherwise a_text is written as a string.
	InvokeResult SetMenuVariable(const std::string& a_menu, const std::string& a_path,
		bool a_isNumber, double a_number, const std::string& a_text);

	// Read an AS variable back as a string (numbers stringified). Writes nothing; a_out is
	// left untouched on any non-kOk result. The query half — use it to confirm a SetMenuVariable
	// landed, or to read swf state a screenshot cannot show.
	InvokeResult GetMenuVariable(const std::string& a_menu, const std::string& a_path,
		std::string& a_out);
}
