#pragma once
#include "veyra/engine/EngineController.h"
#include "veyra/engine/ExportJobManager.h"
#include <vector>
namespace veyra::ui {
engine::EnhancementSettings defaultSettings();
std::vector<std::wstring> presetNames();
bool presetAt(size_t,engine::EnhancementSettings&);
HWND createSettingsPanel(HWND,engine::EngineController&,std::function<void(engine::EnhancementSettings)>);
void settingsEnabled(bool,const engine::EnhancementSettings&);
void settingsVisibility(bool);void settingsPage(int);void settingsDpi();
void exportPanelStatus(const engine::ExportJobSnapshot&,bool canExport,bool canSave);
}
