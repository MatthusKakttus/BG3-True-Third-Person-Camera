#include "Settings.h"
#include <nlohmann/json.hpp>

namespace Settings
{
	using json = nlohmann::json;

	struct ReloadDebounce
	{
		std::mutex mutex;
		std::chrono::steady_clock::time_point lastClaim{};
		bool pendingFollowup = false;

		bool Claim()
		{
			std::lock_guard<std::mutex> lock(mutex);
			const auto now = std::chrono::steady_clock::now();
			if (now - lastClaim < std::chrono::milliseconds(300)) {
				pendingFollowup = true;
				return false;
			}
			lastClaim = now;
			pendingFollowup = false;
			return true;
		}

		bool ConsumePendingFollowup()
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (!pendingFollowup) {
				return false;
			}
			pendingFollowup = false;
			lastClaim = std::chrono::steady_clock::now();
			return true;
		}
	};

	static int VkCodeFromName(const std::string& name)
	{
		static const std::unordered_map<std::string, int> map = {
			{ "BACKSPACE", 8 }, { "TAB", 9 }, { "ENTER", 13 }, { "RETURN", 13 }, { "SHIFT", 16 }, { "LSHIFT", 160 }, { "RSHIFT", 161 },
			{ "CTRL", 17 }, { "CONTROL", 17 }, { "LCTRL", 162 }, { "RCTRL", 163 }, { "ALT", 18 }, { "LALT", 164 }, { "RALT", 165 },
			{ "PAUSE", 19 }, { "CAPSLOCK", 20 }, { "ESCAPE", 27 }, { "SPACE", 32 }, { "PAGEUP", 33 }, { "PAGEDOWN", 34 },
			{ "END", 35 }, { "HOME", 36 }, { "LEFT", 37 }, { "UP", 38 }, { "RIGHT", 39 }, { "DOWN", 40 }, { "INSERT", 45 }, { "DELETE", 46 }, { "DEL", 46 },
			{ "0", 48 }, { "1", 49 }, { "2", 50 }, { "3", 51 }, { "4", 52 }, { "5", 53 }, { "6", 54 }, { "7", 55 }, { "8", 56 }, { "9", 57 },
			{ "A", 65 }, { "B", 66 }, { "C", 67 }, { "D", 68 }, { "E", 69 }, { "F", 70 }, { "G", 71 }, { "H", 72 }, { "I", 73 },
			{ "J", 74 }, { "K", 75 }, { "L", 76 }, { "M", 77 }, { "N", 78 }, { "O", 79 }, { "P", 80 }, { "Q", 81 }, { "R", 82 },
			{ "S", 83 }, { "T", 84 }, { "U", 85 }, { "V", 86 }, { "W", 87 }, { "X", 88 }, { "Y", 89 }, { "Z", 90 },
			{ "NUMPAD0", 96 }, { "NUMPAD1", 97 }, { "NUMPAD2", 98 }, { "NUMPAD3", 99 }, { "NUMPAD4", 100 },
			{ "NUMPAD5", 101 }, { "NUMPAD6", 102 }, { "NUMPAD7", 103 }, { "NUMPAD8", 104 }, { "NUMPAD9", 105 },
			{ "NUM_0", 96 }, { "NUM_1", 97 }, { "NUM_2", 98 }, { "NUM_3", 99 }, { "NUM_4", 100 },
			{ "NUM_5", 101 }, { "NUM_6", 102 }, { "NUM_7", 103 }, { "NUM_8", 104 }, { "NUM_9", 105 },
			{ "KP_0", 96 }, { "KP_1", 97 }, { "KP_2", 98 }, { "KP_3", 99 }, { "KP_4", 100 },
			{ "KP_5", 101 }, { "KP_6", 102 }, { "KP_7", 103 }, { "KP_8", 104 }, { "KP_9", 105 },
			{ "MULTIPLY", 106 }, { "KP_MULTIPLY", 106 }, { "ADD", 107 }, { "KP_PLUS", 107 }, { "SUBTRACT", 109 }, { "KP_MINUS", 109 },
			{ "DECIMAL", 110 }, { "KP_PERIOD", 110 }, { "DIVIDE", 111 }, { "KP_DIVIDE", 111 }, { "KP_ENTER", 13 },
			{ "F1", 112 }, { "F2", 113 }, { "F3", 114 }, { "F4", 115 }, { "F5", 116 }, { "F6", 117 },
			{ "F7", 118 }, { "F8", 119 }, { "F9", 120 }, { "F10", 121 }, { "F11", 122 }, { "F12", 123 },
			{ "NUMLOCK", 144 }, { "SCROLLLOCK", 145 }, { "OEM_1", 186 }, { "OEM_PLUS", 187 }, { "OEM_COMMA", 188 },
			{ "OEM_MINUS", 189 }, { "OEM_PERIOD", 190 }, { "OEM_2", 191 }, { "OEM_3", 192 }, { "TILDE", 192 },
			{ "OEM_4", 219 }, { "OEM_5", 220 }, { "OEM_6", 221 }, { "OEM_7", 222 },
			{ "SEMICOLON", 186 }, { "EQUALS", 187 }, { "COMMA", 188 }, { "MINUS", 189 }, { "PERIOD", 190 }, { "SLASH", 191 },
			{ "GRAVE", 192 }, { "LEFTBRACKET", 219 }, { "BACKSLASH", 220 }, { "RIGHTBRACKET", 221 }, { "APOSTROPHE", 222 }
		};

		std::string upper = name;
		std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
		const auto it = map.find(upper);
		return it != map.end() ? it->second : 106;
	}

	static std::filesystem::path McmSettingsPath()
	{
		wchar_t localAppData[MAX_PATH];
		if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) == 0) {
			return {};
		}
		return std::filesystem::path(localAppData) / L"Larian Studios" / L"Baldur's Gate 3" / L"Script Extender" / L"BG3MCM" / L"Profiles" / L"Default" / L"TrueThirdPersonCamera" / L"settings.json";
	}

	static std::filesystem::path StatePath()
	{
		wchar_t localAppData[MAX_PATH];
		if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) == 0) {
			return {};
		}
		return std::filesystem::path(localAppData) / L"Larian Studios" / L"Baldur's Gate 3" / L"Script Extender" / L"TrueThirdPersonCamera" / L"state.json";
	}

	static std::filesystem::path ClientStatePath()
	{
		wchar_t localAppData[MAX_PATH];
		if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) == 0) {
			return {};
		}
		return std::filesystem::path(localAppData) / L"Larian Studios" / L"Baldur's Gate 3" / L"Script Extender" / L"TrueThirdPersonCamera" / L"client_state.json";
	}

	static std::filesystem::path CameraStatePath()
	{
		wchar_t localAppData[MAX_PATH];
		if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) == 0) {
			return {};
		}
		return std::filesystem::path(localAppData) / L"Larian Studios" / L"Baldur's Gate 3" / L"Script Extender" / L"TrueThirdPersonCamera" / L"camera_state.json";
	}

	static bool ReadFile(const std::filesystem::path& path, std::string& outContent)
	{
		std::error_code ec;
		if (path.empty() || !std::filesystem::exists(path, ec)) {
			return false;
		}
		std::ifstream file(path);
		if (!file.is_open()) {
			return false;
		}
		outContent.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		file.close();
		return !outContent.empty();
	}

	static void BackupCorruptFile(const std::filesystem::path& path)
	{
		std::error_code renameEc;
		const auto now = std::chrono::system_clock::now().time_since_epoch().count();
		auto backupPath = path;
		backupPath += L".corrupted-" + std::to_wstring(now) + L".bak";
		std::filesystem::rename(path, backupPath, renameEc);
		if (!renameEc) {
			WARN("Corrupted config backed up to: {}", backupPath.string());
		}
	}

	template <typename T>
	static void ApplyIfPresent(const json& section, const char* key, T& out)
	{
		if (!section.is_object()) {
			return;
		}
		const auto it = section.find(key);
		if (it == section.end() || it->is_null()) {
			return;
		}
		if constexpr (std::is_same_v<T, bool>) {
			if (it->is_boolean()) {
				out = it->get<bool>();
			}
		} else if constexpr (std::is_same_v<T, int>) {
			if (it->is_number()) {
				out = it->get<int>();
			}
		} else {
			if (it->is_number()) {
				out = it->get<T>();
			}
		}
	}

	void Main::Load() noexcept
	{
		WriteLocker locker(Lock);

		bool loadedMcm = false;
		try {
			std::string content;
			const auto mcmPath = McmSettingsPath();
			if (ReadFile(mcmPath, content)) {
				json root = json::parse(content);

				const auto& global = root["global_settings"]["general"];
				ApplyIfPresent(global, "InvertPitch", InvertPitch);
				ApplyIfPresent(global, "ZoomToggleCharacterSwitchTransitionDuration", ZoomToggleCharacterSwitchTransitionDuration);

				const auto& mouseSection = root["global_settings"]["mouse_settings"];
				ApplyIfPresent(mouseSection, "MouseCameraRotationMult", MouseCameraRotationMult);
				ApplyIfPresent(mouseSection, "MousePitchMult", MousePitchMult);
				ApplyIfPresent(mouseSection, "MouseZoomMult", MouseZoomMult);

				const auto& controllerSection = root["global_settings"]["controller_settings"];
				ApplyIfPresent(controllerSection, "ControllerCameraRotationMult", ControllerCameraRotationMult);
				ApplyIfPresent(controllerSection, "ControllerPitchMult", ControllerPitchMult);
				ApplyIfPresent(controllerSection, "ControllerZoomMult", ControllerZoomMult);
				if (global.is_object() && global.contains("ZoomToggleKeyboardKey")) {
					const auto& kb = global["ZoomToggleKeyboardKey"];
					if (kb.is_object() && kb.contains("Keyboard") && kb["Keyboard"].is_object() && kb["Keyboard"].contains("Key") && kb["Keyboard"]["Key"].is_string()) {
						ZoomToggleKeyboardKey = VkCodeFromName(kb["Keyboard"]["Key"].get<std::string>());
					}
				}
				if (global.is_object() && global.contains("ZoomToggleHorizontalFlipKey")) {
					const auto& kb = global["ZoomToggleHorizontalFlipKey"];
					if (kb.is_object() && kb.contains("Keyboard") && kb["Keyboard"].is_object() && kb["Keyboard"].contains("Key") && kb["Keyboard"]["Key"].is_string()) {
						ZoomToggleHorizontalFlipKey = VkCodeFromName(kb["Keyboard"]["Key"].get<std::string>());
					}
				}

				const auto& close = root["close_camera_settings"]["close_camera"];
				ApplyIfPresent(close, "ZoomToggleCloseValue", ZoomToggleCloseValue);
				ApplyIfPresent(close, "ZoomToggleCloseFOV", ZoomToggleCloseFOV);
				ApplyIfPresent(close, "ZoomToggleCloseHorizontalOffset", ZoomToggleCloseHorizontalOffset);
				ApplyIfPresent(close, "ZoomToggleCloseVerticalOffset", ZoomToggleCloseVerticalOffset);
				ApplyIfPresent(close, "ZoomToggleCrouchVerticalOffset", ZoomToggleCrouchVerticalOffset);

				const auto& closeAdvanced = root["close_camera_settings"]["close_camera_advanced"];
				ApplyIfPresent(closeAdvanced, "ZoomToggleRunningFOVIncrease", ZoomToggleRunningFOVIncrease);
				ApplyIfPresent(closeAdvanced, "ZoomToggleRunningZoomIncrease", ZoomToggleRunningZoomIncrease);
				ApplyIfPresent(closeAdvanced, "ZoomToggleRunningHorizontalOffsetIncrease", ZoomToggleRunningHorizontalOffsetIncrease);
				ApplyIfPresent(closeAdvanced, "ZoomToggleRunningVerticalOffsetIncrease", ZoomToggleRunningVerticalOffsetIncrease);

				const auto& combatCamera = root["combat_camera_settings"]["combat_camera"];
				ApplyIfPresent(combatCamera, "ZoomToggleSelectorCloseValue", ZoomToggleSelectorCloseValue);
				ApplyIfPresent(combatCamera, "ZoomToggleCombatFOV", ZoomToggleCombatFOV);
				// Opt-out toggle for the mod's combat camera (see CameraController::IsVanillaCombatCameraActive)
				ApplyIfPresent(combatCamera, "VanillaCombatCamera", VanillaCombatCamera);

				const auto& castCamera = root["close_camera_settings"]["cast_camera"];
				ApplyIfPresent(castCamera, "ZoomToggleCastMinZoom", ZoomToggleCastMinZoom);

				const auto& combatActionCamera = root["combat_camera_settings"]["combat_action_camera"];
				ApplyIfPresent(combatActionCamera, "ZoomToggleCombatActionFOV", ZoomToggleCombatActionFOV);
				ApplyIfPresent(combatActionCamera, "ZoomToggleCombatActionZoom", ZoomToggleCombatActionZoom);

				const auto& farSection = root["far_camera_settings"]["far_camera"];
				ApplyIfPresent(farSection, "ZoomToggleFarValue", ZoomToggleFarValue);
				ApplyIfPresent(farSection, "UnlockedPitchLimitClipping", UnlockedPitchLimitClipping);
				ApplyIfPresent(farSection, "ZoomToggleFarFOV", ZoomToggleFarFOV);
				ApplyIfPresent(farSection, "VanillaFarCamera", VanillaFarCamera);

				INFO("MCM config loaded from: {}", mcmPath.string());
				loadedMcm = true;
			}
		} catch (const std::exception& e) {
			WARN("MCM config parse failed (corrupted/partial JSON?): {} - using built-in defaults for MCM settings", e.what());
			BackupCorruptFile(McmSettingsPath());
		} catch (...) {
			WARN("MCM config parse failed (unknown error) - using built-in defaults for MCM settings"sv);
			BackupCorruptFile(McmSettingsPath());
		}
		if (!loadedMcm) {
			INFO("No MCM settings.json found yet - using built-in defaults."sv);
		}

		try {
			std::string content;
			const auto statePath = StatePath();
			if (ReadFile(statePath, content)) {
				json root = json::parse(content);
				ApplyIfPresent(root, "ZoomToggleFTBActive", ZoomToggleFTBActive);
				ApplyIfPresent(root, "IsControllingBeastForm", IsControllingBeastForm);
				ApplyIfPresent(root, "IsControllingElementalForm", IsControllingElementalForm);
				ApplyIfPresent(root, "HasUnstableBoundsStatus", HasUnstableBoundsStatus);
				ApplyIfPresent(root, "BeastZoomPushOverride", BeastZoomPushOverride);
				ApplyIfPresent(root, "BeastVerticalPushOverride", BeastVerticalPushOverride);
				ApplyIfPresent(root, "PlanarAllyActive", PlanarAllyActive);
				ApplyIfPresent(root, "PlanarAllyZoomPush", PlanarAllyZoomPush);
				ApplyIfPresent(root, "PlanarAllyVerticalPush", PlanarAllyVerticalPush);
				ApplyIfPresent(root, "PlanarAllyHorizontalPush", PlanarAllyHorizontalPush);
				ApplyIfPresent(root, "PlanarAllyElevateOnFly", PlanarAllyElevateOnFly);
			}
		} catch (const std::exception& e) {
			WARN("State config parse failed (corrupted/partial JSON?): {} - using built-in defaults for runtime state", e.what());
			BackupCorruptFile(StatePath());
		} catch (...) {
			WARN("State config parse failed (unknown error) - using built-in defaults for runtime state"sv);
			BackupCorruptFile(StatePath());
		}

		try {
			std::string content;
			const auto clientStatePath = ClientStatePath();
			if (ReadFile(clientStatePath, content)) {
				json root = json::parse(content);

				for (auto& snap : ClientCameraSnapshots) {
					snap = {};
				}
				const auto camerasIt = root.find("Cameras");
				if (camerasIt != root.end() && camerasIt->is_array()) {
					for (size_t i = 0; i < ClientCameraSnapshots.size() && i < camerasIt->size(); ++i) {
						const auto& elem = (*camerasIt)[i];
						if (!elem.is_object()) {
							continue;
						}
						auto& snap = ClientCameraSnapshots[i];
						ApplyIfPresent(elem, "UserID", snap.userId);
						ApplyIfPresent(elem, "SelectMode", snap.selectMode);
						ApplyIfPresent(elem, "InputControllerActive", snap.inputControllerActive);
						ApplyIfPresent(elem, "EnemyTurnActive", snap.enemyTurnActive);
						snap.valid = snap.userId >= 0 && snap.userId <= 255;
					}
				}
			}
		} catch (const std::exception& e) {
			WARN("Client state parse failed (corrupted/partial JSON?): {} - using built-in defaults for runtime state", e.what());
			BackupCorruptFile(ClientStatePath());
		} catch (...) {
			WARN("Client state parse failed (unknown error) - using built-in defaults for runtime state"sv);
			BackupCorruptFile(ClientStatePath());
		}

		try {
			std::string content;
			const auto cameraStatePath = CameraStatePath();
			if (ReadFile(cameraStatePath, content)) {
				json root = json::parse(content);
				ApplyIfPresent(root, "CloseViewMode", PersistedCloseViewMode);
			}
		} catch (const std::exception& e) {
			WARN("Camera state parse failed (corrupted/partial JSON?): {} - defaulting to far view", e.what());
			BackupCorruptFile(CameraStatePath());
		} catch (...) {
			WARN("Camera state parse failed (unknown error) - defaulting to far view"sv);
			BackupCorruptFile(CameraStatePath());
		}

		changed = true;
	}

	void Main::SaveCameraState(bool closeViewMode, bool combatFocusTrackingEnabled, bool combatActionPhase) noexcept
	{
		try {
			const auto path = CameraStatePath();
			if (path.empty()) {
				return;
			}
			std::error_code ec;
			std::filesystem::create_directories(path.parent_path(), ec);
			json root;
			root["CloseViewMode"] = closeViewMode;
			root["CombatFocusTrackingEnabled"] = combatFocusTrackingEnabled;
			root["CombatActionPhase"] = combatActionPhase;
			std::ofstream file(path, std::ios::trunc);
			if (file.is_open()) {
				file << root.dump(1, '\t');
			}
		} catch (const std::exception& e) {
			WARN("Failed to save camera state: {}", e.what());
		} catch (...) {
			WARN("Failed to save camera state (unknown error)"sv);
		}
	}

	static void WatchDirectory(std::wstring folderPath, std::wstring cfgFilename, ReloadDebounce& debounce)
	{
		HANDLE file = CreateFileW(folderPath.c_str(),
			FILE_LIST_DIRECTORY,
			FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
			NULL,
			OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
			NULL);

		if (!file || file == INVALID_HANDLE_VALUE) {
			return;
		}

		OVERLAPPED overlapped;
		std::memset(&overlapped, 0, sizeof(overlapped));
		overlapped.hEvent = CreateEvent(NULL, FALSE, 0, NULL);
		if (!overlapped.hEvent) {
			CloseHandle(file);
			return;
		}

		uint8_t change_buf[1024];
		BOOL success = ReadDirectoryChangesW(
			file, change_buf, sizeof(change_buf), TRUE,
			FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
			NULL, &overlapped, NULL);

		while (success) {
			DWORD result = WaitForSingleObject(overlapped.hEvent, INFINITE);
			if (result == WAIT_OBJECT_0) {
				DWORD bytes_transferred;
				if (GetOverlappedResult(file, &overlapped, &bytes_transferred, FALSE)) {
					auto* evt = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(change_buf);
					for (;;) {
						if (evt->Action == FILE_ACTION_MODIFIED || evt->Action == FILE_ACTION_ADDED) {
							std::wstring name(evt->FileName, evt->FileNameLength / sizeof(WCHAR));
							if (_wcsicmp(name.c_str(), cfgFilename.c_str()) == 0) {
								if (debounce.Claim()) {
									Sleep(100);
									Settings::Main::GetSingleton()->Load();
									Sleep(220);
									if (debounce.ConsumePendingFollowup()) {
										Settings::Main::GetSingleton()->Load();
									}
								}
								break;
							}
						}
						if (evt->NextEntryOffset)
							*(uint8_t**)&evt += evt->NextEntryOffset;
						else
							break;
					}
				}

				std::memset(&overlapped, 0, sizeof(overlapped));
				overlapped.hEvent = CreateEvent(NULL, FALSE, 0, NULL);
				success = ReadDirectoryChangesW(
					file, change_buf, sizeof(change_buf), TRUE,
					FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
					NULL, &overlapped, NULL);
			}
		}

		CloseHandle(file);
		if (overlapped.hEvent)
			CloseHandle(overlapped.hEvent);
	}

	void Main::WatchForChanges()
	{
		static ReloadDebounce mcmDebounce;
		static ReloadDebounce stateDebounce;
		static ReloadDebounce clientStateDebounce;

		const auto mcmPath = McmSettingsPath();
		if (!mcmPath.empty()) {
			std::error_code ec;
			std::filesystem::create_directories(mcmPath.parent_path(), ec);
			std::thread([folder = mcmPath.parent_path().wstring(), name = mcmPath.filename().wstring()]() {
				WatchDirectory(folder, name, mcmDebounce);
			}).detach();
		}

		const auto statePath = StatePath();
		if (!statePath.empty()) {
			std::error_code ec;
			std::filesystem::create_directories(statePath.parent_path(), ec);
			std::thread([folder = statePath.parent_path().wstring(), name = statePath.filename().wstring()]() {
				WatchDirectory(folder, name, stateDebounce);
			}).detach();
		}

		const auto clientStatePath = ClientStatePath();
		if (!clientStatePath.empty()) {
			std::error_code ec;
			std::filesystem::create_directories(clientStatePath.parent_path(), ec);
			std::thread([folder = clientStatePath.parent_path().wstring(), name = clientStatePath.filename().wstring()]() {
				WatchDirectory(folder, name, clientStateDebounce);
			}).detach();
		}
	}
}
