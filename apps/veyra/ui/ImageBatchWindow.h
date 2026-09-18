#pragma once
#include <filesystem>
#include "veyra/engine/EngineController.h"
namespace veyra::ui {
void showImageBatchWindow(HWND,engine::EngineController&,std::filesystem::path,std::function<engine::EnhancementSettings()>);
}
