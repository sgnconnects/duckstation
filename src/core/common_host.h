// SPDX-FileCopyrightText: 2019-2022 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#pragma once

#include "system.h"

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class SettingsInterface;

class AudioStream;
enum class AudioStretchMode : u8;

namespace CommonHost {
/// Initializes configuration.
void UpdateLogSettings();

void Initialize();
void Shutdown();

void SetDefaultSettings(SettingsInterface& si);
void SetDefaultControllerSettings(SettingsInterface& si);
void SetDefaultHotkeyBindings(SettingsInterface& si);
void LoadSettings(SettingsInterface& si, std::unique_lock<std::mutex>& lock);
void CheckForSettingsChanges(const Settings& old_settings);
void OnSystemStarting();
void OnSystemStarted();
void OnSystemDestroyed();
void OnSystemPaused();
void OnSystemResumed();
void OnGameChanged(const std::string& disc_path, const std::string& game_serial, const std::string& game_name);
void PumpMessagesOnCPUThread();
bool CreateHostDisplayResources();
void ReleaseHostDisplayResources();

/// Returns the time elapsed in the current play session.
u64 GetSessionPlayedTime();

} // namespace CommonHost

namespace ImGuiManager {
void RenderDebugWindows();
}
