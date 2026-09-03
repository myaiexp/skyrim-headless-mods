#include "uimenu.h"

#include <RE/Skyrim.h>

namespace
{
	// Resolve an open menu's GFx movie, or say why it could not be resolved. Shared by all
	// three entry points so "menu closed" never reaches the engine as a null deref.
	engine::InvokeResult ResolveMovie(const std::string& a_menu, RE::GPtr<RE::GFxMovieView>& a_out)
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return engine::InvokeResult::kNoUI;
		}
		auto menu = ui->GetMenu(a_menu);
		if (!menu) {
			return engine::InvokeResult::kMenuClosed;
		}
		if (!menu->uiMovie) {
			return engine::InvokeResult::kNoMovie;
		}
		a_out = menu->uiMovie;
		return engine::InvokeResult::kOk;
	}
}

engine::InvokeResult engine::InvokeMenuMethod(const std::string& a_menu, const std::string& a_method,
	const std::vector<std::string>& a_args)
{
	RE::GPtr<RE::GFxMovieView> movie;
	if (auto r = ResolveMovie(a_menu, movie); r != InvokeResult::kOk) {
		return r;
	}

	// Every argument goes over as a GFx string. AS2 coerces a string to a number where the
	// callee wants one, so a single string form covers both InvokeString and InvokeNumber
	// without a per-argument type tag in the command JSON.
	std::vector<RE::GFxValue> vals;
	vals.reserve(a_args.size());
	for (const auto& s : a_args) {
		vals.emplace_back(s.c_str());
	}

	const bool ok = movie->Invoke(a_method.c_str(), nullptr,
		vals.empty() ? nullptr : vals.data(), static_cast<std::uint32_t>(vals.size()));
	return ok ? InvokeResult::kOk : InvokeResult::kFailed;
}

engine::InvokeResult engine::SetMenuVariable(const std::string& a_menu, const std::string& a_path,
	bool a_isNumber, double a_number, const std::string& a_text)
{
	RE::GPtr<RE::GFxMovieView> movie;
	if (auto r = ResolveMovie(a_menu, movie); r != InvokeResult::kOk) {
		return r;
	}
	const bool ok = a_isNumber ? movie->SetVariableDouble(a_path.c_str(), a_number)
							   : movie->SetVariable(a_path.c_str(), a_text.c_str());
	return ok ? InvokeResult::kOk : InvokeResult::kFailed;
}

engine::InvokeResult engine::GetMenuVariable(const std::string& a_menu, const std::string& a_path,
	std::string& a_out)
{
	RE::GPtr<RE::GFxMovieView> movie;
	if (auto r = ResolveMovie(a_menu, movie); r != InvokeResult::kOk) {
		return r;
	}
	RE::GFxValue v;
	if (!movie->GetVariable(&v, a_path.c_str())) {
		return InvokeResult::kFailed;
	}
	if (v.IsString()) {
		const char* s = v.GetString();
		a_out = s ? s : "";
	} else if (v.IsNumber()) {
		a_out = std::to_string(v.GetNumber());
	} else if (v.IsBool()) {
		a_out = v.GetBool() ? "true" : "false";
	} else if (v.IsUndefined()) {
		a_out = "undefined";
	} else if (v.IsNull()) {
		a_out = "null";
	} else {
		a_out = "<object>";
	}
	return InvokeResult::kOk;
}
