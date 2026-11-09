#include "Camera/CameraController.h"

#include "Camera/CharacterStateProbes.h"
#include "Hooks/Hooks.h"
#include "Settings/Settings.h"
#include <cmath>
#include <windows.h>

namespace
{
	constexpr float kExplorationFOVClose = 55.f;
	constexpr float kExplorationHorizontalOffsetMult = 0.f;
	constexpr float kExplorationVerticalOffsetMult = 0.8f;
	constexpr float kCombatHorizontalOffsetMult = 0.f;
	constexpr float kCombatVerticalOffsetMult = 0.8f;

	constexpr float kCombatActionPitch = 8.f;
	constexpr float kCombatSelectorPitch = 40.f;
	constexpr float kCombatPitchTransitionSpeed = 6.f;

	constexpr float kCombatSelectorEntryDelay = 0.4f;

	constexpr float kCastVerticalOffset = 0.7f;

	float ScaleBySpeedRamp(float maxValue, float rawRamp)
	{
		if (maxValue >= 0.f) {
			return std::min(maxValue, rawRamp);
		}
		return std::max(maxValue, -rawRamp);
	}

	constexpr float kTallAmplifyBase = 0.653f;
	constexpr float kTallAmplifyExponent = 0.556f;
	constexpr float kShortCompress = 0.32f;
	constexpr float kMinHeightRatio = 0.80f;
	constexpr float kMaxHeightRatio = 1.6f;

	float ApplyHeightRatioCurve(float rawRatio)
	{
		float curved;
		if (rawRatio >= 1.f) {
			curved = 1.f + kTallAmplifyBase * std::pow(rawRatio - 1.f, kTallAmplifyExponent);
		} else {
			curved = 1.f - (1.f - rawRatio) * kShortCompress;
		}
		return std::clamp(curved, kMinHeightRatio, kMaxHeightRatio);
	}

	constexpr float kBeastPushThreshold = 0.85f;
	constexpr float kBeastZoomPushScale = 4.4f;
	constexpr float kBeastVerticalPushScale = 0.88f;

	float ComputeBeastPush(bool isBeastForm, float heightRatio, float scale)
	{
		if (!isBeastForm) {
			return 0.f;
		}
		return std::max(0.f, heightRatio - kBeastPushThreshold) * scale;
	}

	constexpr float kElementalIdleSpeedThreshold = 0.3f; // basically standing still
	constexpr float kElementalMovingOffsetAmount = 0.05f;

}

namespace
{
	void CaptureOne(RE::CameraDefinition* cam, CameraController::CameraDefinitionBaseline& out)
	{
		if (!cam) {
			return;
		}
		out.fovClose = cam->fovClose_84;
		out.fovFar = cam->fovFar_88;
		out.fovCloseAlt = cam->fovCloseAlt_8C;
		out.fovFarAlt = cam->fovFarAlt_90;
		out.tacticalFov = cam->tacticalFov_D0;
		out.horizOffsetMult = cam->camHorizontalOffsetMult_64;
		out.vertOffsetMult = cam->camVerticalOffsetMult_68;
		out.minZoom = cam->minZoom_2C;
		out.maxZoom = cam->maxZoom_28;
		out.tactMinZoom = cam->tactMinZoom_C8;
		out.tactMaxZoom = cam->tactMaxZoom_CC;
		out.altMinZoom = cam->altMinZoomController_34;
		out.altMaxZoom = cam->altMaxZoomController_30;
		out.pitchAdjustSpeedA = cam->pitchAdjustSpeedA_48;
		out.pitchAdjustSpeedB = cam->pitchAdjustSpeedB_F0;
		out.pitchAdjustSpeedC = cam->pitchAdjustSpeedC_F4;
	}

	void RestoreOne(RE::CameraDefinition* cam, const CameraController::CameraDefinitionBaseline& in)
	{
		if (!cam) {
			return;
		}
		cam->fovClose_84 = in.fovClose;
		cam->fovFar_88 = in.fovFar;
		cam->fovCloseAlt_8C = in.fovCloseAlt;
		cam->fovFarAlt_90 = in.fovFarAlt;
		cam->tacticalFov_D0 = in.tacticalFov;
		cam->camHorizontalOffsetMult_64 = in.horizOffsetMult;
		cam->camVerticalOffsetMult_68 = in.vertOffsetMult;
		cam->minZoom_2C = in.minZoom;
		cam->maxZoom_28 = in.maxZoom;
		cam->tactMinZoom_C8 = in.tactMinZoom;
		cam->tactMaxZoom_CC = in.tactMaxZoom;
		cam->altMinZoomController_34 = in.altMinZoom;
		cam->altMaxZoomController_30 = in.altMaxZoom;
		cam->pitchAdjustSpeedA_48 = in.pitchAdjustSpeedA;
		cam->pitchAdjustSpeedB_F0 = in.pitchAdjustSpeedB;
		cam->pitchAdjustSpeedC_F4 = in.pitchAdjustSpeedC;
	}
}

void CameraController::CaptureVanillaCameraBaselineIfNeeded()
{
	if (vanillaBaselineCaptured_) {
		return;
	}
	if (!Hooks::Offsets::UnkCameraSingletonPtr || !*Hooks::Offsets::UnkCameraSingletonPtr) {
		return;
	}
	RE::CameraDefinition* expCam = reinterpret_cast<RE::CameraDefinition*>(reinterpret_cast<uintptr_t>(*Hooks::Offsets::UnkCameraSingletonPtr) + Hooks::Offsets::explorationCameraOffset);
	RE::CameraDefinition* comCam = reinterpret_cast<RE::CameraDefinition*>(reinterpret_cast<uintptr_t>(*Hooks::Offsets::UnkCameraSingletonPtr) + Hooks::Offsets::combatCameraOffset);
	if (!expCam || !comCam) {
		return;
	}
	CaptureOne(expCam, vanillaExplorationBaseline_);
	CaptureOne(comCam, vanillaCombatBaseline_);
	vanillaBaselineCaptured_ = true;
}

bool CameraController::GetOriginalPitchAdjustSpeeds(RE::CameraDefinition* cameraDefinition, float& outA, float& outB, float& outC) const
{
	if (!vanillaBaselineCaptured_ || !cameraDefinition || !Hooks::Offsets::UnkCameraSingletonPtr || !*Hooks::Offsets::UnkCameraSingletonPtr) {
		return false;
	}
	RE::CameraDefinition* expCam = reinterpret_cast<RE::CameraDefinition*>(reinterpret_cast<uintptr_t>(*Hooks::Offsets::UnkCameraSingletonPtr) + Hooks::Offsets::explorationCameraOffset);
	RE::CameraDefinition* comCam = reinterpret_cast<RE::CameraDefinition*>(reinterpret_cast<uintptr_t>(*Hooks::Offsets::UnkCameraSingletonPtr) + Hooks::Offsets::combatCameraOffset);

	const CameraDefinitionBaseline* baseline = nullptr;
	if (cameraDefinition == expCam) {
		baseline = &vanillaExplorationBaseline_;
	} else if (cameraDefinition == comCam) {
		baseline = &vanillaCombatBaseline_;
	}
	if (!baseline) {
		return false;
	}

	outA = baseline->pitchAdjustSpeedA;
	outB = baseline->pitchAdjustSpeedB;
	outC = baseline->pitchAdjustSpeedC;
	return true;
}

void CameraController::RestoreVanillaCameraBaseline()
{
	if (!vanillaBaselineCaptured_) {
		return;
	}
	if (!Hooks::Offsets::UnkCameraSingletonPtr || !*Hooks::Offsets::UnkCameraSingletonPtr) {
		return;
	}
	RE::CameraDefinition* expCam = reinterpret_cast<RE::CameraDefinition*>(reinterpret_cast<uintptr_t>(*Hooks::Offsets::UnkCameraSingletonPtr) + Hooks::Offsets::explorationCameraOffset);
	RE::CameraDefinition* comCam = reinterpret_cast<RE::CameraDefinition*>(reinterpret_cast<uintptr_t>(*Hooks::Offsets::UnkCameraSingletonPtr) + Hooks::Offsets::combatCameraOffset);
	RestoreOne(expCam, vanillaExplorationBaseline_);
	RestoreOne(comCam, vanillaCombatBaseline_);
	if (currentCamera_ && Hooks::Offsets::GetCurrentCameraDefinition) {
		RE::CameraDefinition* curCam = Hooks::Offsets::GetCurrentCameraDefinition(currentCamera_);
		if (curCam && curCam != expCam && curCam != comCam) {
			RestoreOne(curCam, vanillaExplorationBaseline_);
		}
	}
}

void CameraController::ApplyZoomToggleCameraOverrides()
{
	if (!Hooks::Offsets::UnkCameraSingletonPtr || !*Hooks::Offsets::UnkCameraSingletonPtr) {
		return;
	}

	const int16_t playerId = CurrentPlayerId();
	if (playerId == 0) {
		return;
	}
	auto& pd = GetPlayerData(playerId);

	UpdateActionModeSuspend(currentPlayer_);

	if (pd.closeViewTransitionTimer > 0.f) {
		pd.closeViewTransitionTimer -= deltaTime_;
	}

	const auto settings = Settings::Main::GetSingleton();
	ReadLocker locker(settings->Lock);

	if (!pd.closeViewModeInitialized) {
		pd.closeViewModeInitialized = true;
		pd.closeViewMode = settings->PersistedCloseViewMode;
	}

	{
		const bool rawCombatOrTB = currentCamera_ && (((currentCamera_->cameraModeFlags & RE::CameraModeFlags::kCombat) != 0) || settings->ZoomToggleFTBActive);
		// Not reported as enabled in vanilla combat, so the client Lua stops changing the camera targets
		const bool combatFocusTrackingEnabled = pd.closeViewMode && settings->ZoomToggleImmersiveMode && (rawCombatOrTB || settings->HasUnstableBoundsStatus) && !IsVanillaCombatCameraActive(playerId);
		if (!pd.combatFocusTrackingStateInitialized || combatFocusTrackingEnabled != pd.prevCombatFocusTrackingEnabled) {
			pd.combatFocusTrackingStateInitialized = true;
			pd.prevCombatFocusTrackingEnabled = combatFocusTrackingEnabled;
			Settings::Main::SaveCameraState(pd.closeViewMode, combatFocusTrackingEnabled, pd.prevCombatActionPhase);
		}
	}

	// Vanilla far view or vanilla combat: put the game's camera values back and skip all mod overrides
	if (ShouldUseVanillaCamera(playerId)) {
		RestoreVanillaCameraBaseline();
		return;
	}

	const bool nativeCombatOrTB = currentCamera_ && IsCombatOrTurnBasedOrTacticalMode(currentCamera_);
	if (nativeCombatOrTB != pd.prevNativeCombatOrTB) {
		pd.prevNativeCombatOrTB = nativeCombatOrTB;
		if (pd.closeViewMode && currentCamera_) {
			if (nativeCombatOrTB) {
				const float farZoom = settings->ZoomToggleFarValue;
				currentCamera_->desiredZoom = farZoom;
				currentCamera_->currentZoom_160 = farZoom;
				SetActionModeSuspend(false);
			} else {
				const float closeZoom = settings->ZoomToggleCloseValue;
				currentCamera_->desiredZoom = closeZoom;
				currentCamera_->currentZoom_160 = closeZoom;
			}
		}
	}

	const bool activeCloseView = ShouldApplyCloseViewFraming();
	const bool closeViewJustActivated = activeCloseView && !pd.prevActiveCloseViewMode;
	if (activeCloseView != pd.prevActiveCloseViewMode) {
		if (currentCamera_) {
			if (activeCloseView) {
				const float closeZoom = settings->ZoomToggleCloseValue;
				currentCamera_->desiredZoom = closeZoom;
				currentCamera_->currentZoom_160 = closeZoom;
			} else if (pd.closeViewMode) {
				const float farZoom = settings->ZoomToggleFarValue;
				currentCamera_->desiredZoom = farZoom;
				currentCamera_->currentZoom_160 = farZoom;
			}
		}
		pd.prevActiveCloseViewMode = activeCloseView;
	}

	uintptr_t character = 0;
	if (currentPlayer_ && Hooks::Offsets::UnkPlayerSingletonPtr && *Hooks::Offsets::UnkPlayerSingletonPtr && Hooks::Offsets::GetCharacter) {
		uintptr_t playerMgr = reinterpret_cast<uintptr_t>(*Hooks::Offsets::UnkPlayerSingletonPtr);
		int16_t nativeCharPlayerId = currentPlayer_->playerId_38;
		character = Hooks::Offsets::GetCharacter(playerMgr, nativeCharPlayerId);
	}
	const bool characterSwitched = character != 0 && character != pd.prevActiveCharacter;
	pd.prevActiveCharacter = character;

	float runningFovOffset = 0.f;
	float runningZoomOffset = 0.f;
	float runningHorizOffset = 0.f;
	float runningVertOffset = 0.f;
	float elementalIdleOffset = 0.f;
	float crouchVertOffset = 0.f;
	float heightRatio = 1.0f;

	float targetCrouchOffset = 0.f;
	if (character) {
		if (CameraControllerDetail::CheckEclSneaking(character)) {
			targetCrouchOffset = settings->ZoomToggleCrouchVerticalOffset;
		}
	}

	if (character) {
		auto& state = characterStates_[character];

		bool triggerResample = false;
		if (!state.heightInitialized) {
			state.heightInitialized = true;
			triggerResample = true;
		}
		if (closeViewJustActivated || characterSwitched) {
			triggerResample = true;
		}
		if (settings->IsControllingBeastForm != state.wasBeastForm) {
			state.wasBeastForm = settings->IsControllingBeastForm;
			triggerResample = true;
		}
		if (settings->IsControllingElementalForm != state.wasElementalForm) {
			state.wasElementalForm = settings->IsControllingElementalForm;
			triggerResample = true;
		}
		if (triggerResample) {
			state.heightResamplePending = true;
		}

		if (state.heightResamplePending && !settings->HasUnstableBoundsStatus) {
			state.heightResamplePending = false;
			state.heightResampleTimer = 1.0f;
			state.trackedHeight = 0.f;
		} else if (state.heightResamplePending && state.trackedHeight <= 0.f) {
			state.trackedHeight = 2.09f;
		}

		if (state.heightResampleTimer > 0.f && !settings->HasUnstableBoundsStatus) {
			state.heightResampleTimer -= deltaTime_;

			float rawHeight = 0.f;
			CameraControllerDetail::GetCharacterHeightRatio(character, &rawHeight);
			if (state.trackedHeight <= 0.f || rawHeight < state.trackedHeight) {
				state.trackedHeight = rawHeight;
			}
		}

		const float targetHeightRatio = ApplyHeightRatioCurve(CameraControllerDetail::RawHeightToRatio(state.trackedHeight, 2.09f));

		state.currentCrouchOffset = InterpTo(state.currentCrouchOffset, targetCrouchOffset, deltaTime_, 4.0f);
		crouchVertOffset = state.currentCrouchOffset;

		if (state.currentHeightRatio == 0.f) {
			state.currentHeightRatio = targetHeightRatio;
		} else {
			state.currentHeightRatio = InterpTo(state.currentHeightRatio, targetHeightRatio, deltaTime_, 4.0f);
		}
		heightRatio = state.currentHeightRatio;

		{
			bool selectMode = false, inputActive = true, enemyTurn = false;
			if (FindClientSnapshotForPlayer(selectMode, inputActive, enemyTurn) && enemyTurn) {
				heightRatio = 1.0f;
			}
		}
	}

	if (character && currentCamera_) {
		auto& state = characterStates_[character];
		if (ShouldApplyCloseViewFraming()) {
			RE::Vector3 currentPos = currentCamera_->desiredCameraRootPos;
			if (state.prevCameraRootPos.x != 0.f || state.prevCameraRootPos.y != 0.f || state.prevCameraRootPos.z != 0.f) {
				float dx = currentPos.x - state.prevCameraRootPos.x;
				float dy = currentPos.y - state.prevCameraRootPos.y;
				float dz = currentPos.z - state.prevCameraRootPos.z;
				float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

				float speed = deltaTime_ > 0.f ? dist / deltaTime_ : 0.f;

				float rawRamp = speed > 2.6f ? (speed - 2.6f) * 10.0f : 0.f;

				float targetFovOffset = ScaleBySpeedRamp(settings->ZoomToggleRunningFOVIncrease, rawRamp);
				float targetZoomOffset = ScaleBySpeedRamp(settings->ZoomToggleRunningZoomIncrease, rawRamp);
				float targetHorizOffset = ScaleBySpeedRamp(settings->ZoomToggleRunningHorizontalOffsetIncrease, rawRamp);
				float targetVertOffset = ScaleBySpeedRamp(settings->ZoomToggleRunningVerticalOffsetIncrease, rawRamp);

				state.currentRunningFovOffset = InterpTo(state.currentRunningFovOffset, targetFovOffset, deltaTime_, 4.0f);
				state.currentRunningZoomOffset = InterpTo(state.currentRunningZoomOffset, targetZoomOffset, deltaTime_, 4.0f);
				state.currentRunningHorizontalOffset = InterpTo(state.currentRunningHorizontalOffset, targetHorizOffset, deltaTime_, 4.0f);
				state.currentRunningVerticalOffset = InterpTo(state.currentRunningVerticalOffset, targetVertOffset, deltaTime_, 4.0f);

				const bool elevateOnFly = settings->IsControllingElementalForm || settings->PlanarAllyElevateOnFly;
				const float targetElementalIdleOffset = (elevateOnFly && speed >= kElementalIdleSpeedThreshold) ? kElementalMovingOffsetAmount : 0.f;
				state.currentElementalIdleOffset = InterpTo(state.currentElementalIdleOffset, targetElementalIdleOffset, deltaTime_, 4.0f);
			}

			state.prevCameraRootPos = currentPos;
		} else {
			state.prevCameraRootPos = { 0.f, 0.f, 0.f };
			state.currentRunningFovOffset = 0.f;
			state.currentRunningZoomOffset = 0.f;
			state.currentRunningHorizontalOffset = 0.f;
			state.currentRunningVerticalOffset = 0.f;
			state.currentElementalIdleOffset = 0.f;
		}
		runningFovOffset = state.currentRunningFovOffset;
		runningZoomOffset = state.currentRunningZoomOffset;
		runningHorizOffset = state.currentRunningHorizontalOffset;
		runningVertOffset = state.currentRunningVerticalOffset;
		elementalIdleOffset = state.currentElementalIdleOffset;
	}

	RE::CameraDefinition* expCam = reinterpret_cast<RE::CameraDefinition*>(reinterpret_cast<uintptr_t>(*Hooks::Offsets::UnkCameraSingletonPtr) + Hooks::Offsets::explorationCameraOffset);
	RE::CameraDefinition* comCam = reinterpret_cast<RE::CameraDefinition*>(reinterpret_cast<uintptr_t>(*Hooks::Offsets::UnkCameraSingletonPtr) + Hooks::Offsets::combatCameraOffset);
	RE::CameraDefinition* curCam = nullptr;
	if (currentCamera_) {
		if (Hooks::Offsets::GetCurrentCameraDefinition) {
			curCam = Hooks::Offsets::GetCurrentCameraDefinition(currentCamera_);
		}
	}

	if (ShouldApplyCloseViewFraming()) {
		const float beastVertPush = (settings->BeastVerticalPushOverride >= 0.f)
		                                 ? settings->BeastVerticalPushOverride
		                                 : ComputeBeastPush(settings->IsControllingBeastForm, heightRatio, kBeastVerticalPushScale);
		const bool rawCombatOrTB = currentCamera_ && (((currentCamera_->cameraModeFlags & RE::CameraModeFlags::kCombat) != 0) || settings->ZoomToggleFTBActive);
		const float baseFov = rawCombatOrTB ? settings->ZoomToggleCombatFOV : settings->ZoomToggleCloseFOV;
		const float closeFov = baseFov + runningFovOffset;
		const float planarHorizPush = settings->PlanarAllyActive ? settings->PlanarAllyHorizontalPush : 0.f;
		const float planarVertPush = settings->PlanarAllyActive ? settings->PlanarAllyVerticalPush : 0.f;
		const float horizFlipSign = pd.horizontalOffsetFlipped ? -1.f : 1.f;
		const float closeHoriz = (settings->ZoomToggleCloseHorizontalOffset + runningHorizOffset + planarHorizPush) * horizFlipSign;
		const float closeVert = (settings->ZoomToggleCloseVerticalOffset + crouchVertOffset + runningVertOffset) * heightRatio + beastVertPush + elementalIdleOffset + planarVertPush;

		if (expCam) {
			expCam->fovClose_84 = closeFov;
			expCam->camHorizontalOffsetMult_64 = closeHoriz;
			expCam->camVerticalOffsetMult_68 = closeVert;
		}
		if (comCam) {
			comCam->fovClose_84 = closeFov;
			comCam->camHorizontalOffsetMult_64 = closeHoriz;
			comCam->camVerticalOffsetMult_68 = closeVert;
		}
		if (curCam) {
			curCam->fovClose_84 = closeFov;
			curCam->camHorizontalOffsetMult_64 = closeHoriz;
			curCam->camVerticalOffsetMult_68 = closeVert;
		}
	} else {
		const auto cameraMode = currentCamera_ ? GetCurrentCameraMode(currentCamera_) : CameraMode::kExploration;
		const float farFov = settings->ZoomToggleFarFOV;
		if (expCam) {
			expCam->fovClose_84 = kExplorationFOVClose;
			expCam->fovFar_88 = farFov;
			expCam->camHorizontalOffsetMult_64 = kExplorationHorizontalOffsetMult;
			expCam->camVerticalOffsetMult_68 = kExplorationVerticalOffsetMult;
		}
		if (comCam) {
			comCam->fovClose_84 = settings->ZoomToggleCombatFOV;
			comCam->fovFar_88 = farFov;
			comCam->camHorizontalOffsetMult_64 = kCombatHorizontalOffsetMult;
			comCam->camVerticalOffsetMult_68 = kCombatVerticalOffsetMult;
		}
		if (curCam) {
			curCam->fovFar_88 = farFov;
			if (cameraMode == CameraMode::kExploration || cameraMode == CameraMode::kExplorationTactical) {
				curCam->fovClose_84 = kExplorationFOVClose;
				curCam->camHorizontalOffsetMult_64 = kExplorationHorizontalOffsetMult;
				curCam->camVerticalOffsetMult_68 = kExplorationVerticalOffsetMult;
			} else {
				curCam->fovClose_84 = settings->ZoomToggleCombatFOV;
				curCam->camHorizontalOffsetMult_64 = kCombatHorizontalOffsetMult;
				curCam->camVerticalOffsetMult_68 = kCombatVerticalOffsetMult;
			}
		}
	}

	if (currentCamera_ && pd.closeViewMode && !IsCombatOrTurnBasedOrTacticalMode(currentCamera_)) {
		const bool reliableSelector = currentCamera_ &&
		                               (((currentCamera_->cameraModeFlags & RE::CameraModeFlags::kCombat) != 0) || settings->ZoomToggleFTBActive);
		const bool selectorMode = ShouldReleasePositionLock() && (reliableSelector || !IsInCloseViewTransition());

		if (!reliableSelector) {
			if (!pd.explorationSelectorPitchInitialized || selectorMode != pd.prevExplorationSelectorMode) {
				pd.explorationSelectorPitchInitialized = true;
				pd.prevExplorationSelectorMode = selectorMode;
				pd.pitchTransitionTarget = selectorMode ? kCombatSelectorPitch : kCombatActionPitch;
				pd.pitchTransitionSpeed = 4.f;
				pd.pitchTransitionCancellable = true;

				Settings::Main::SaveCameraState(pd.closeViewMode, pd.prevCombatFocusTrackingEnabled, !selectorMode);
			}
		}

		if (selectorMode) {
			currentCamera_->desiredZoom = reliableSelector ? settings->ZoomToggleSelectorCloseValue : settings->ZoomToggleCastMinZoom;
		} else {
			const float beastZoomPush = (settings->BeastZoomPushOverride >= 0.f)
			                                 ? settings->BeastZoomPushOverride
			                                 : ComputeBeastPush(settings->IsControllingBeastForm, heightRatio, kBeastZoomPushScale);
			const float planarZoomPush = settings->PlanarAllyActive ? settings->PlanarAllyZoomPush : 0.f;
			const float closeZoomValue = settings->ZoomToggleCloseValue + runningZoomOffset + beastZoomPush + planarZoomPush;
			currentCamera_->desiredZoom = closeZoomValue;
			currentCamera_->currentZoom_160 = closeZoomValue;
		}
		pd.prevSelectorMode = selectorMode;
	} else {
		pd.prevSelectorMode = false;
	}

	{
		const bool rawCombatOrTBForDelay = currentCamera_ && (((currentCamera_->cameraModeFlags & RE::CameraModeFlags::kCombat) != 0) || settings->ZoomToggleFTBActive);
		if (pd.closeViewMode && settings->ZoomToggleImmersiveMode && rawCombatOrTBForDelay) {
			if (pd.genuinelyTargeting) {
				pd.combatSelectorEntryDelayTimer += deltaTime_;
				if (pd.combatSelectorEntryDelayTimer >= kCombatSelectorEntryDelay) {
					pd.delayedTargetingForCombat = true;
				}
			} else {
				pd.combatSelectorEntryDelayTimer = 0.f;
				pd.delayedTargetingForCombat = false;
			}
		}
	}

	{
		const bool rawCombatOrTBForPitch = currentCamera_ && (((currentCamera_->cameraModeFlags & RE::CameraModeFlags::kCombat) != 0) || settings->ZoomToggleFTBActive);
		const bool immersiveCombatCloseView = pd.closeViewMode && settings->ZoomToggleImmersiveMode && rawCombatOrTBForPitch;

		if (immersiveCombatCloseView) {
			if (!pd.prevImmersiveCombatCloseView || pd.delayedTargetingForCombat != pd.prevGenuinelyTargetingForPitch) {
				pd.pitchTransitionTarget = pd.delayedTargetingForCombat ? kCombatSelectorPitch : kCombatActionPitch;
				pd.pitchTransitionSpeed = kCombatPitchTransitionSpeed;
				pd.pitchTransitionCancellable = true;
			}
			pd.prevGenuinelyTargetingForPitch = pd.delayedTargetingForCombat;
		}
		pd.prevImmersiveCombatCloseView = immersiveCombatCloseView;

		const bool combatActionPhaseNow = immersiveCombatCloseView && !pd.delayedTargetingForCombat;
		if (!pd.combatActionPhaseStateInitialized || combatActionPhaseNow != pd.prevCombatActionPhase) {
			pd.combatActionPhaseStateInitialized = true;
			pd.prevCombatActionPhase = combatActionPhaseNow;
			Settings::Main::SaveCameraState(pd.closeViewMode, pd.prevCombatFocusTrackingEnabled, combatActionPhaseNow);
		}
	}

	if (character && currentCamera_ && settings->ZoomToggleImmersiveMode) {
		auto& state = characterStates_[character];

		const bool rawCombatOrTB = currentCamera_ && (((currentCamera_->cameraModeFlags & RE::CameraModeFlags::kCombat) != 0) || settings->ZoomToggleFTBActive);
		const bool combatActionCamera = pd.closeViewMode && settings->ZoomToggleImmersiveMode && rawCombatOrTB;

		const float blendSpeed = combatActionCamera ? 30.0f : 6.0f;
		const bool targetingSignal = combatActionCamera ? pd.delayedTargetingForCombat : pd.genuinelyTargeting;
		state.currentCastBlend = InterpTo(state.currentCastBlend, targetingSignal ? 1.f : 0.f, deltaTime_, blendSpeed);

		if (state.currentCastBlend > 0.001f) {
			const float castFov = combatActionCamera ? settings->ZoomToggleCombatActionFOV : settings->ZoomToggleCloseFOV;
			const float castVert = kCastVerticalOffset * heightRatio + crouchVertOffset;
			const float castZoom = combatActionCamera ? settings->ZoomToggleCombatActionZoom : settings->ZoomToggleCastMinZoom;
			const float blend = state.currentCastBlend;

			if (curCam) {
				curCam->fovClose_84 = InterpTo(curCam->fovClose_84, castFov, blend, 1.f);
				curCam->camVerticalOffsetMult_68 = InterpTo(curCam->camVerticalOffsetMult_68, castVert, blend, 1.f);
			}
			const float lerpedZoom = InterpTo(currentCamera_->currentZoom_160, castZoom, blend, 1.f);
			currentCamera_->desiredZoom = lerpedZoom;
			currentCamera_->currentZoom_160 = lerpedZoom;
		}

		if (combatActionCamera) {
			const float maxCombatZoom = std::max(settings->ZoomToggleSelectorCloseValue, settings->ZoomToggleCombatActionZoom);
			if (currentCamera_->currentZoom_160 > maxCombatZoom) {
				currentCamera_->currentZoom_160 = maxCombatZoom;
			}
			if (currentCamera_->desiredZoom > maxCombatZoom) {
				currentCamera_->desiredZoom = maxCombatZoom;
			}
			if (currentCamera_->currentZoomA > maxCombatZoom) {
				currentCamera_->currentZoomA = maxCombatZoom;
			}
			if (currentCamera_->currentZoomB > maxCombatZoom) {
				currentCamera_->currentZoomB = maxCombatZoom;
			}
		}
	}
}
