#include "Camera/CameraController.h"

#include "Settings/Settings.h"

namespace
{
	constexpr float kCloseViewCenterPitch = 8.f;
	constexpr float kFarViewOverheadPitch = 40.f;
}

bool CameraController::IsCloseViewMode() const
{
	const int16_t playerId = CurrentPlayerId();
	if (playerId == 0) {
		return false;
	}
	const auto& pd = GetPlayerData(playerId);

	if (!pd.closeViewMode || pd.actionModeSuspend || IsVanillaCombatCameraActive(playerId)) {
		return false;
	}

	if (currentCamera_ && IsCombatOrTurnBasedOrTacticalMode(currentCamera_)) {
		return false;
	}

	return true;
}

bool CameraController::ShouldApplyCloseViewFraming() const
{
	const int16_t playerId = CurrentPlayerId();
	if (playerId == 0) {
		return false;
	}
	const auto& pd = GetPlayerData(playerId);

	if (!pd.closeViewMode) {
		return false;
	}

	// No mod framing in vanilla combat
	if (IsVanillaCombatCameraActive(playerId)) {
		return false;
	}

	if (pd.actionModeSuspend) {
		const auto settings = Settings::Main::GetSingleton();
		ReadLocker locker(settings->Lock);
		if (!settings->ZoomToggleImmersiveMode) {
			return false;
		}
	}

	if (currentCamera_ && IsCombatOrTurnBasedOrTacticalMode(currentCamera_)) {
		return false;
	}

	return true;
}

bool CameraController::GetBaseCloseViewMode() const
{
	// No mod close-view in vanilla combat
	if (IsVanillaCombatCameraActive(CurrentPlayerId())) {
		return false;
	}

	const int16_t playerId = CurrentPlayerId();
	if (playerId == 0) {
		return false;
	}
	const auto& pd = GetPlayerData(playerId);

	if (!pd.closeViewMode) {
		return false;
	}

	if (currentCamera_ && IsCombatOrTurnBasedOrTacticalMode(currentCamera_)) {
		return false;
	}

	return true;
}

bool CameraController::IsVanillaFarCameraActive(int16_t playerId) const
{
	if (playerId == 0) {
		return false;
	}
	const auto& pd = GetPlayerData(playerId);
	if (pd.closeViewMode) {
		return false;
	}

	const auto settings = Settings::Main::GetSingleton();
	ReadLocker locker(settings->Lock);
	return settings->VanillaFarCamera;
}

// "Use Vanilla Camera in Combat": when enabled, the mod leaves the camera alone while in combat / turn-based mode
bool CameraController::IsVanillaCombatCameraActive(int16_t playerId) const
{
	if (playerId == 0 || !currentCamera_) {
		return false;
	}

	const auto settings = Settings::Main::GetSingleton();
	ReadLocker locker(settings->Lock);
	if (!settings->VanillaCombatCamera) {
		return false;
	}

	return ((currentCamera_->cameraModeFlags & RE::CameraModeFlags::kCombat) != 0) || settings->ZoomToggleFTBActive;
}

// Single check used by the hooks: true when either vanilla mode wants the game's untouched camera
bool CameraController::ShouldUseVanillaCamera(int16_t playerId) const
{
	return IsVanillaFarCameraActive(playerId) || IsVanillaCombatCameraActive(playerId);
}

// Runs every frame before the camera update. Only acts when the vanilla-combat state flips.
void CameraController::UpdateVanillaCombatTransition()
{
	const int16_t playerId = CurrentPlayerId();
	if (playerId == 0 || !currentCamera_) {
		return;
	}
	auto& pd = GetPlayerData(playerId);

	const bool vanillaCombat = IsVanillaCombatCameraActive(playerId);
	if (vanillaCombat == pd.prevVanillaCombatActive) {
		return;
	}
	pd.prevVanillaCombatActive = vanillaCombat;

	const auto settings = Settings::Main::GetSingleton();
	if (vanillaCombat) {
		// Entering vanilla combat: drop the mod's pitch/selector state so the game's camera takes over cleanly
		pd.pitch.reset();
		pd.pitchTransitionTarget.reset();
		pd.controllerPitchDelta = 0.f;
		pd.selectorModeActive = false;
		pd.genuinelyTargeting = false;
		pd.delayedTargetingForCombat = false;
		pd.combatSelectorEntryDelayTimer = 0.f;
		pd.prevImmersiveCombatCloseView = false;
		SetActionModeSuspend(false);
	} else {
		// Leaving vanilla combat: restore the mod's close/far zoom and ease the pitch back
		{
			ReadLocker locker(settings->Lock);
			const float zoom = pd.closeViewMode ? settings->ZoomToggleCloseValue : settings->ZoomToggleFarValue;
			currentCamera_->desiredZoom = zoom;
			currentCamera_->currentZoom_160 = zoom;
		}
		pd.prevActiveCloseViewMode = false;
		pd.pitch = currentCamera_->currentPitch_164;
		pd.pitchTransitionTarget = pd.closeViewMode ? kCloseViewCenterPitch : kFarViewOverheadPitch;
		pd.pitchTransitionSpeed = 4.f;
		pd.pitchTransitionCancellable = true;
		pd.closeViewTransitionTimer = 2.0f;
	}

	WriteLocker writeLocker(settings->Lock);
	settings->changed = true;
}

void CameraController::ToggleCloseView(RE::CameraObject* cameraObject)
{
	if (!cameraObject) {
		return;
	}

	const int16_t playerId = GetPlayerIdFromCameraObject(cameraObject);
	if (playerId == 0) {
		return;
	}
	auto& pd = GetPlayerData(playerId);

	// The view toggle is ignored while the game's own combat camera is in use
	if (IsVanillaCombatCameraActive(playerId)) {
		return;
	}

	if (!pd.closeViewMode && IsCombatOrTurnBasedOrTacticalMode(cameraObject)) {
		return;
	}

	const auto settings = Settings::Main::GetSingleton();
	{
		ReadLocker locker(settings->Lock);

		pd.closeViewMode = !pd.closeViewMode;
		Settings::Main::SaveCameraState(pd.closeViewMode, pd.closeViewMode && settings->ZoomToggleImmersiveMode, pd.prevCombatActionPhase);

		cameraObject->zoomDelta = 0.f;

		pd.closeViewTransitionTimer = 2.0f;

		if (pd.closeViewMode) {
			const float closeZoom = settings->ZoomToggleCloseValue;
			cameraObject->desiredZoom = closeZoom;
			cameraObject->currentZoom_160 = closeZoom;
		} else {
			const float farZoom = settings->ZoomToggleFarValue;
			cameraObject->desiredZoom = farZoom;
			cameraObject->currentZoom_160 = farZoom;
		}

		pd.pitch = cameraObject->currentPitch_164;

		pd.pitchTransitionTarget = pd.closeViewMode ? kCloseViewCenterPitch : kFarViewOverheadPitch;
		pd.pitchTransitionSpeed = 4.f;
		pd.pitchTransitionCancellable = true;
	}

	{
		WriteLocker writeLocker(settings->Lock);
		settings->changed = true;
	}

	ApplyZoomToggleCameraOverrides();
}
