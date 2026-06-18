// World-readiness snapshot and UI menu-open queries (main-thread only).
#include "worldstate.h"

engine::WorldState engine::GetWorldState()
{
	WorldState w;
	auto* ui = RE::UI::GetSingleton();
	if (ui) {
		w.mainMenu    = ui->IsMenuOpen(RE::MainMenu::MENU_NAME);
		w.loadingMenu = ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
	}
	auto* player = RE::PlayerCharacter::GetSingleton();
	w.is3DLoaded  = player && player->Is3DLoaded();
	w.inWorld     = ui && !w.mainMenu && !w.loadingMenu && w.is3DLoaded;
	return w;
}

bool engine::IsInWorld()
{
	return GetWorldState().inWorld;
}

bool engine::IsMenuOpen(const std::string& a_menu)
{
	auto* ui = RE::UI::GetSingleton();
	if (!ui) {
		return false;  // pre-load: UI singleton not up yet
	}
	return ui->IsMenuOpen(a_menu);
}