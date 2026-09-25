/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SETTINGKEYS_H
#define SETTINGKEYS_H

// ONE OWNER PER SETTINGS KEY (STUDIO-CRUD-1 item 10). A preference used to be a
// string literal and a default typed again at every site that read it — three
// or four sites for show_fps, shadow_mesh_optimization, the shader warm-up pair,
// the MCP pair, auto_save, default_directory, … — and main.cpp re-typed keys
// that already had an owner. A key is declared ONCE here, with its ONE default,
// and read and written through SettingsManager::get / set. `source.settings_keys`
// fails the build's gate on a key literal read with a default at a second site.
//
// Keys that belong to a subsystem with its own key function keep it:
// ThemeManager::settingsKey() ("appearance/theme"), framepacing::settingsKey()
// ("viewport/pacing"), loadingcover::settingsKey(), the shortcut registry's
// "shortcut/<id>" and the camera settings of cameraapi.

#include <QtGlobal>

/// A persisted preference: its QSettings name and the value it has when unset.
template <typename T>
struct SettingKey
{
    const char *name = nullptr;
    T fallback{};
};

namespace settingkeys {

// ---- the viewport and the engine ----------------------------------------
/// The engine-drawn frame-stats readout (F3; STATS_OVERLAY_SPEC §5.3).
inline constexpr SettingKey<bool> showFps{ "show_fps", false };
inline constexpr SettingKey<bool> shadowMeshOptimization{ "shadow_mesh_optimization", true };
inline constexpr SettingKey<int>  shaderWarmupSamples{ "shader_warmup_samples", 1 };
inline constexpr SettingKey<bool> shaderWarmupShadows{ "shader_warmup_shadows", true };
inline constexpr SettingKey<bool> shaderWarmupOnOpen{ "shader_warmup_on_open", true };
inline constexpr SettingKey<bool> shaderCacheEnabled{ "shader_cache_enabled", true };
/// "default" | "jahshaka" (left-drag orbits in the Jahshaka scheme).
inline constexpr SettingKey<const char *> mouseControls{ "mouse_controls", "default" };

// ---- the shell ----------------------------------------------------------
inline constexpr SettingKey<bool> watchdogEnabled{ "watchdog_enabled", true };
inline constexpr SettingKey<bool> autoSave{ "auto_save", true };
inline constexpr SettingKey<bool> openInPlayer{ "open_in_player", false };
inline constexpr SettingKey<bool> scriptFeedbackLive{ "script_feedback_live", true };
/// Where new projects go; empty = AppPaths' default projects root.
inline constexpr SettingKey<const char *> defaultDirectory{ "default_directory", "" };
/// The Desktop grid's tile size name ("Small" | "Normal" | "Large").
inline constexpr SettingKey<const char *> tileSize{ "tileSize", "Normal" };
inline constexpr SettingKey<int> sliderRows{ "slider_rows", 6 };

// ---- scripting ----------------------------------------------------------
inline constexpr SettingKey<bool> mcpEnabled{ "mcp_enabled", false };
/// McpServer::kDefaultPort is this value.
inline constexpr SettingKey<int>  mcpPort{ "mcp_port", 8639 };
/// The Claude chat's pinned model (ClaudeLaunchConfig::defaultModel() is this
/// value); an explicit empty string means "inherit the CLI's default".
inline constexpr SettingKey<const char *> claudeModel{ "claude_model", "fable" };

// ---- the performance recorder ------------------------------------------
inline constexpr SettingKey<const char *> perfCaptureRoot{ "perf/captureRoot", "" };
inline constexpr SettingKey<double> perfKeepDays{ "perf/keepDays", 14.0 };
inline constexpr SettingKey<qint64> perfKeepBytes{ "perf/keepBytes",
                                                   qint64(2) * 1024 * 1024 * 1024 };
/// The periodic perf sample's interval: a minute in the daily Debug driver,
/// five in a shipped build (0 = off).
#ifdef QT_DEBUG
inline constexpr SettingKey<int> perfSampleSeconds{ "log/perfSampleSeconds", 60 };
#else
inline constexpr SettingKey<int> perfSampleSeconds{ "log/perfSampleSeconds", 300 };
#endif

}   // namespace settingkeys

#endif // SETTINGKEYS_H
