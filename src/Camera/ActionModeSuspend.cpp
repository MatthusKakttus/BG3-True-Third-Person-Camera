#include "Camera/CameraController.h"

#include "Camera/CharacterStateProbes.h"
#include "Hooks/Hooks.h"
#include "Settings/Settings.h"
#include <cmath>
#include <windows.h>

namespace
{
	constexpr float kZoomToggleActionZoomValue = 7.0f;
	// Gamepad L3 (left stick) free look is owned by the game: the client snapshot's SelectMode (written
	// by the Script Extender side) mirrors it. It turns on ~0.1 s after the L3 press and turns off when
	// the game leaves free look. B/Circle is not a reliable "leave free look" signal, because it also
	// closes containers and the attack wheel without leaving free look, and no native signal
	// (selector flag, preview task, input controller state) changes when those UIs open or close.
	// The mod's own free-look flag therefore follows SelectMode instead of reacting to B.

	// SelectMode must stay off this long (after having been on) before the mod concludes that the
	// game left free look. Debounces single-frame flickers of the snapshot.
	constexpr float kFreeLookSelectOffConfirmSeconds = 0.15f;
	// If SelectMode never turns on after the L3 toggle, the game ignored the press (for example L3 was
	// pressed while a UI had focus), so the mod drops its own flag after this long to stay in sync.
	constexpr float kFreeLookEntryTimeoutSeconds = 1.0f;
}

// Tracks the gamepad L3 free-look toggle for the current player. Must run every update, before any
// early return in IsCharacterInActionMode, so L3 edges are never missed while a selector or
// tactical view is open.
void CameraController::UpdateControllerSelectorToggle() const
{
	const int16_t playerId = CurrentPlayerId();
	if (playerId == 0) {
		return;
	}
	auto& pd = GetPlayerData(playerId);

	if (!Hooks::Offsets::isInControllerMode || !*Hooks::Offsets::isInControllerMode) {
		pd.l3SelectorActive = false;
		pd.wasBPressed = false;
		pd.freeLookEntryTimer = 0.f;
		pd.freeLookSelectModeSeen = false;
		pd.freeLookSelectOffTimer = 0.f;
		return;
	}

	bool snapSelectMode = false, snapInputActive = true, snapEnemyTurn = false;
	const bool haveSnapshot = FindClientSnapshotForPlayer(snapSelectMode, snapInputActive, snapEnemyTurn);
	const float dt = deltaTime_;

	const bool isL3Pressed = IsControllerButtonPressed(playerId, 0x0040);  // XINPUT_GAMEPAD_LEFT_THUMB (L3)
	if (isL3Pressed && !pd.wasL3Pressed) {
		pd.l3HoldUsedForZoom = false;
	}
	if (isL3Pressed && currentCamera_ && std::abs(currentCamera_->zoomDelta) > 0.01f) {
		pd.l3HoldUsedForZoom = true;
	}
	if (!isL3Pressed && pd.wasL3Pressed && !pd.l3HoldUsedForZoom) {
		// L3 tap (not a zoom hold) toggles free look; restart the SelectMode tracking for the new state.
		pd.l3SelectorActive = !pd.l3SelectorActive;
		pd.freeLookEntryTimer = 0.f;
		pd.freeLookSelectModeSeen = false;
		pd.freeLookSelectOffTimer = 0.f;
	}
	pd.wasL3Pressed = isL3Pressed;

	const bool isBPressed = IsControllerButtonPressed(playerId, 0x2000);  // XINPUT_GAMEPAD_B (Cancel)
	const bool bPressedNow = isBPressed && !pd.wasBPressed;
	pd.wasBPressed = isBPressed;

	if (pd.l3SelectorActive) {
		if (haveSnapshot) {
			// Follow the game: the mod's free-look flag lives as long as SelectMode does.
			// B is deliberately not used here, because it may only be closing a container or wheel.
			if (snapSelectMode) {
				pd.freeLookSelectModeSeen = true;
				pd.freeLookSelectOffTimer = 0.f;
			} else if (pd.freeLookSelectModeSeen) {
				pd.freeLookSelectOffTimer += dt;
				if (pd.freeLookSelectOffTimer >= kFreeLookSelectOffConfirmSeconds) {
					pd.l3SelectorActive = false;
				}
			} else {
				pd.freeLookEntryTimer += dt;
				if (pd.freeLookEntryTimer >= kFreeLookEntryTimeoutSeconds) {
					pd.l3SelectorActive = false;
				}
			}
		} else if (bPressedNow) {
			// No snapshot from the Script Extender side: fall back to B leaving free look.
			pd.l3SelectorActive = false;
		}
	}
}

void CameraController::SetActionModeSuspend(bool suspend)
{
	const int16_t playerId = CurrentPlayerId();
	if (playerId == 0) {
		return;
	}
	auto& pd = GetPlayerData(playerId);

	if (pd.actionModeSuspend != suspend) {
		pd.actionModeSuspend = suspend;
		const auto settings = Settings::Main::GetSingleton();

		if (settings && settings->ZoomToggleImmersiveMode) {
			return;
		}

		const bool inNativeCombatOrTB = currentCamera_ && IsCombatOrTurnBasedOrTacticalMode(currentCamera_);
		if (pd.closeViewMode && currentCamera_ && settings && !inNativeCombatOrTB) {
			if (suspend) {
				currentCamera_->desiredZoom = kZoomToggleActionZoomValue;
			} else {
				currentCamera_->desiredZoom = settings->ZoomToggleCloseValue;
			}
		}
	}
}

bool CameraController::ShouldReleasePositionLock() const
{
	if (GetPlayerData(CurrentPlayerId()).actionModeSuspend) {
		return true;
	}

	const auto settings = Settings::Main::GetSingleton();
	if (!settings->ZoomToggleImmersiveMode || !currentCamera_) {
		return false;
	}

	return ((currentCamera_->cameraModeFlags & RE::CameraModeFlags::kCombat) != 0) || settings->ZoomToggleFTBActive;
}

bool CameraController::IsCharacterGenuinelyTargeting(uintptr_t characterPtr) const
{
	if (!characterPtr)
		return false;
	__try {
		if (currentCamera_ && (currentCamera_->cameraModeFlags & RE::CameraModeFlags::kTactical)) {
			return true;
		}

		uintptr_t inputController = *reinterpret_cast<uintptr_t*>(characterPtr + 0xA0);
		if (inputController < 0x10000000 || inputController > 0x7FFF00000000ULL || (inputController & 7)) {
			return false;
		}

		uintptr_t previewTask = *reinterpret_cast<uintptr_t*>(inputController + 0x58);
		bool targetingHit = CameraControllerDetail::CheckCharacterTargetingTask(previewTask);

		const bool rawCombat = currentCamera_ && (currentCamera_->cameraModeFlags & RE::CameraModeFlags::kCombat) != 0;

		if (targetingHit && !rawCombat) {
			bool selectMode = false, inputActive = true, enemyTurn = false;
			targetingHit = FindClientSnapshotForPlayer(selectMode, inputActive, enemyTurn) && selectMode;
		}

		const bool rawCombatOrTB = rawCombat || Settings::Main::GetSingleton()->ZoomToggleFTBActive;

		const bool moveOrderHit = rawCombatOrTB && CameraControllerDetail::CheckCharacterMovementOrderTask(previewTask);
		const bool itemHoverHit = rawCombatOrTB && CameraControllerDetail::CheckCharacterItemHoverPreviewTask(previewTask);
		bool result = targetingHit || moveOrderHit || itemHoverHit;

		if (rawCombatOrTB && !result) {
			bool selectMode = false, inputActive = true, enemyTurn = false;
			FindClientSnapshotForPlayer(selectMode, inputActive, enemyTurn);
			result = inputActive;
		}

		return result;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
	return false;
}

void CameraController::GetControlledCharacterTaskTypesForDiagnostics(uint32_t& outPreviewType, uint32_t& outRunningType) const
{
	outPreviewType = 0xFFFFFFFFu;
	outRunningType = 0xFFFFFFFFu;
	__try {
		if (!currentPlayer_ || !Hooks::Offsets::UnkPlayerSingletonPtr || !*Hooks::Offsets::UnkPlayerSingletonPtr || !Hooks::Offsets::GetCharacter) {
			return;
		}
		uintptr_t playerMgr = reinterpret_cast<uintptr_t>(*Hooks::Offsets::UnkPlayerSingletonPtr);
		uintptr_t characterPtr = Hooks::Offsets::GetCharacter(playerMgr, currentPlayer_->playerId_38);
		if (!characterPtr)
			return;

		uintptr_t inputController = *reinterpret_cast<uintptr_t*>(characterPtr + 0xA0);
		if (inputController < 0x10000000 || inputController > 0x7FFF00000000ULL || (inputController & 7)) {
			return;
		}

		uintptr_t runningTask = *reinterpret_cast<uintptr_t*>(inputController + 0x50);
		uintptr_t previewTask = *reinterpret_cast<uintptr_t*>(inputController + 0x58);

		if (previewTask >= 0x10000000 && previewTask <= 0x7FFF00000000ULL && !(previewTask & 7)) {
			outPreviewType = *reinterpret_cast<uint32_t*>(previewTask + 0x80);
		}
		if (runningTask >= 0x10000000 && runningTask <= 0x7FFF00000000ULL && !(runningTask & 7)) {
			outRunningType = *reinterpret_cast<uint32_t*>(runningTask + 0x80);
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
}

bool CameraController::IsCharacterInActionMode(uintptr_t characterPtr, int16_t nativePlayerId) const
{
	if (!characterPtr)
		return false;

	// Update the gamepad L3 free-look state every update, before any early return below, so L3
	// presses and SelectMode changes that happen while a selector/wheel/tactical view is open
	// are never missed (the old inline L3/B block sat below those early returns).
	UpdateControllerSelectorToggle();

	__try {
		if (currentCamera_ && (currentCamera_->cameraModeFlags & RE::CameraModeFlags::kTactical)) {
			return true;
		}

		uintptr_t inputController = *reinterpret_cast<uintptr_t*>(characterPtr + 0xA0);
		if (inputController < 0x10000000 || inputController > 0x7FFF00000000ULL || (inputController & 7)) {
			return false;
		}

		bool selectorMode = *reinterpret_cast<bool*>(inputController + 0x20);
		if (selectorMode) {
			return true;
		}

		uintptr_t runningTask = *reinterpret_cast<uintptr_t*>(inputController + 0x50);
		(void)runningTask;
		uintptr_t previewTask = *reinterpret_cast<uintptr_t*>(inputController + 0x58);

		bool foundActive = CameraControllerDetail::CheckCharacterTargetingTask(previewTask);

		if (foundActive) {
			const bool rawCombatForGate = currentCamera_ && (currentCamera_->cameraModeFlags & RE::CameraModeFlags::kCombat) != 0;
			if (!rawCombatForGate) {
				bool selectMode = false, inputActive = true, enemyTurn = false;
				foundActive = FindClientSnapshotForPlayer(selectMode, inputActive, enemyTurn) && selectMode;
			}
		}

		if (!foundActive) {
			const bool rawCombat = currentCamera_ && (currentCamera_->cameraModeFlags & RE::CameraModeFlags::kCombat) != 0;
			const bool fTB = Settings::Main::GetSingleton()->ZoomToggleFTBActive;
			if ((rawCombat || fTB) && CameraControllerDetail::CheckCharacterHasTask(previewTask)) {
				foundActive = true;
			}
		}

		bool camBool = false;
		if (Hooks::Offsets::UnkCameraSingletonPtr && *Hooks::Offsets::UnkCameraSingletonPtr) {
			uintptr_t camSingleton = reinterpret_cast<uintptr_t>(*Hooks::Offsets::UnkCameraSingletonPtr);
			camBool = *reinterpret_cast<bool*>(camSingleton + Hooks::Offsets::cameraBoolOffset);
		}

		if (camBool) {
			foundActive = true;
		}

		const int16_t playerId = CurrentPlayerId();
		auto& pd = GetPlayerData(playerId);

		if (pd.l3SelectorActive) {
			foundActive = true;
		}

		return foundActive;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
	return false;
}

void CameraController::UpdateActionModeSuspend(RE::Player* player)
{
	const int16_t playerId = CurrentPlayerId();
	if (playerId == 0) {
		return;
	}
	auto& pd = GetPlayerData(playerId);

	bool actionMode = false;
	bool genuinelyTargetingNow = false;
	if (player && Hooks::Offsets::UnkPlayerSingletonPtr && *Hooks::Offsets::UnkPlayerSingletonPtr && Hooks::Offsets::GetCharacter) {
		uintptr_t playerMgr = reinterpret_cast<uintptr_t>(*Hooks::Offsets::UnkPlayerSingletonPtr);
		uintptr_t character = Hooks::Offsets::GetCharacter(playerMgr, player->playerId_38);
		genuinelyTargetingNow = IsCharacterGenuinelyTargeting(character);
		actionMode = IsCharacterInActionMode(character, player->playerId_38);
	}

	const bool nativeCombat = currentCamera_ && (currentCamera_->cameraModeFlags & RE::CameraModeFlags::kCombat) != 0;
	bool fTBActive = false;
	{
		const auto ftbSettings = Settings::Main::GetSingleton();
		ReadLocker locker(ftbSettings->Lock);
		fTBActive = ftbSettings->ZoomToggleFTBActive;
	}
	const bool inCombatOrTB = nativeCombat || fTBActive;
	if (inCombatOrTB) {
		pd.capsLockFreelook = false;
	} else {
		const bool capsLockPhysicallyDown = (GetAsyncKeyState(VK_CAPITAL) & 0x8000) != 0;
		if (capsLockPhysicallyDown && !pd.capsLockWasPhysicallyDown) {
			pd.capsLockFreelook = !pd.capsLockFreelook;
		}
		pd.capsLockWasPhysicallyDown = capsLockPhysicallyDown;
	}
	if (pd.capsLockFreelook) {
		actionMode = true;
	}

	if (actionMode) {
		pd.selectorCooldown = 0.15f;
		pd.selectorModeActive = true;
	} else if (pd.selectorCooldown > 0.f) {
		pd.selectorCooldown -= deltaTime_;
		pd.selectorModeActive = true;
	} else {
		pd.selectorModeActive = false;
	}

	if (!player || !pd.closeViewMode) {
		SetActionModeSuspend(false);
		pd.genuinelyTargeting = false;
		return;
	}

	if (actionMode) {
		pd.suspendCooldown = 0.15f;
		SetActionModeSuspend(true);
	} else if (pd.suspendCooldown > 0.f) {
		pd.suspendCooldown -= deltaTime_;
		SetActionModeSuspend(true);
	} else {
		SetActionModeSuspend(false);
	}

	if (genuinelyTargetingNow) {
		pd.genuinelyTargetingCooldown = 0.5f;
		pd.genuinelyTargeting = true;
	} else if (pd.genuinelyTargetingCooldown > 0.f) {
		pd.genuinelyTargetingCooldown -= deltaTime_;
		pd.genuinelyTargeting = true;
	} else {
		pd.genuinelyTargeting = false;
	}
}
