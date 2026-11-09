#include "Hooks/Hooks.h"

#include "Camera/CameraController.h"
#include "Hooks/CameraAnchorState.h"
#include "Settings/Settings.h"
#include <algorithm>
#include <cmath>

namespace Hooks
{
	std::array<AnchorState::PerPlayerAnchorState, 2> AnchorState::perPlayer;

	AnchorState::PerPlayerAnchorState& AnchorState::Get(int16_t playerId)
	{
		static PerPlayerAnchorState dummy;
		if (playerId < 1 || playerId > static_cast<int16_t>(perPlayer.size())) {
			return dummy;
		}
		return perPlayer[playerId - 1];
	}

	void HookManager::Hook_AfterUpdateCameraZoom(uint64_t unkContext1, uint64_t unkContext2, RE::UnkObject* cameraContext, uint64_t unused4)
	{
		afterUpdateCameraZoom_(unkContext1, unkContext2, cameraContext, unused4);

		const auto settings = Settings::Main::GetSingleton();
		ReadLocker locker(settings->Lock);
		const auto cameraController = CameraController::GetSingleton();

		auto* currentCam = cameraController->GetCurrentCamera();
		const int16_t playerId = cameraController->GetPlayerIdFromCameraObject(currentCam);

		// Vanilla far view or vanilla combat: no mod zoom/anchor handling
		if (cameraController->ShouldUseVanillaCamera(playerId)) {
			AnchorState::Get(playerId).floorClipEasedZoom = -1.f;
			return;
		}

		if (settings->UnlockedPitchLimitClipping) {
			if (!cameraController->IsCloseViewMode() && !cameraController->IsSelectorModeActive()) {
				cameraController->AdjustCameraZoomForPitch(unkContext1, unkContext2, cameraController->GetCurrentCamera());
			}
		}

		auto& anchor = AnchorState::Get(playerId);

		{
			const bool allowFloorClip = cameraController->IsCloseViewMode() || cameraController->IsSelectorModeActive();
			auto* cam = currentCam;
			if (allowFloorClip && cam) {
				const float targetZoom = cam->desiredZoom;
				if (targetZoom > 0.05f && targetZoom < 500.0f) {
					if (anchor.floorClipEasedZoom < 0.f) {
						anchor.floorClipEasedZoom = targetZoom;
					} else {
						constexpr float kZoomTau = 0.12f;
						const float deltaTime = std::clamp(cameraController->GetDeltaTime(), 0.f, 0.1f);
						const float alpha = 1.f - std::exp(-deltaTime / kZoomTau);
						anchor.floorClipEasedZoom += (targetZoom - anchor.floorClipEasedZoom) * alpha;
					}

					cam->currentZoomA = anchor.floorClipEasedZoom;
					cam->currentZoomB = anchor.floorClipEasedZoom;
				}
			} else if (cam) {
				anchor.floorClipEasedZoom = cam->currentZoomA;
			}
		}

		{
			auto* cam = currentCam;
			const bool anchorActionMode = cameraController->IsSelectorModeActive();
			const bool rawCombatOrTB = cam && (((cam->cameraModeFlags & RE::CameraModeFlags::kCombat) != 0) || settings->ZoomToggleFTBActive);

			if (cam && !anchorActionMode) {
				anchor.lastLockedAnchor = cam->desiredCameraRootPos;
				anchor.hasLastLockedAnchor = true;
				anchor.hasEasedAnchor = false;
				anchor.wasCursorActive = false;
				anchor.cursorActivationRamp = 0.f;
			} else if (cam && anchorActionMode && anchor.hasLastLockedAnchor) {
				constexpr float kMaxAnchorSpeed = 30.0f; // meters/second

				const bool keyboardCombatFreelook = rawCombatOrTB && Offsets::isInControllerMode && !(*Offsets::isInControllerMode);
				const bool cursorActive = cameraController->IsLeftStickActive(playerId) || cameraController->IsCapsLockFreelookActive() || keyboardCombatFreelook;

				if (!anchor.hasEasedAnchor) {
					anchor.hasEasedAnchor = true;
					anchor.easedAnchor = anchor.lastLockedAnchor;
					anchor.idlePinAnchor = anchor.lastLockedAnchor;
					anchor.cursorActivationRamp = 0.f;
				}

				if (anchor.wasCursorActive && !cursorActive) {
					anchor.idlePinAnchor = anchor.easedAnchor;
				}
				if (!anchor.wasCursorActive && cursorActive) {
					anchor.cursorActivationRamp = 0.f;
				}
				anchor.wasCursorActive = cursorActive;

				const float deltaTime = cameraController->GetDeltaTime();
				if (cursorActive) {
					anchor.cursorActivationRamp += std::max(deltaTime, 0.f);
				}

				constexpr float kCursorRampTime = 0.5f;
				const bool ramping = cursorActive && anchor.cursorActivationRamp < kCursorRampTime;
				const float rampT = ramping ? (anchor.cursorActivationRamp / kCursorRampTime) : 1.f;
				const float rampEase = rampT * rampT * (3.f - 2.f * rampT);

				const RE::Vector3 target = cam->desiredCameraRootPos;
				float dx = target.x - anchor.easedAnchor.x;
				float dy = target.y - anchor.easedAnchor.y;
				float dz = target.z - anchor.easedAnchor.z;
				float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
				float maxStep = kMaxAnchorSpeed * rampEase * std::max(deltaTime, 0.f);
				if (dist > maxStep && dist > 0.0001f) {
					float scale = maxStep / dist;
					anchor.easedAnchor.x += dx * scale;
					anchor.easedAnchor.y += dy * scale;
					anchor.easedAnchor.z += dz * scale;
				} else {
					anchor.easedAnchor = target;
				}

				constexpr float kMaxActiveDistance = 40.0f;
				const RE::Vector3& ceilingRef = (cursorActive && !ramping) ? anchor.lastLockedAnchor : anchor.idlePinAnchor;
				const float kMaxAnchorDistance = cursorActive ? (kMaxActiveDistance * rampEase) : 0.0f;
				float rdx = anchor.easedAnchor.x - ceilingRef.x;
				float rdy = anchor.easedAnchor.y - ceilingRef.y;
				float rdz = anchor.easedAnchor.z - ceilingRef.z;
				float refDist = std::sqrt(rdx * rdx + rdy * rdy + rdz * rdz);
				if (refDist > kMaxAnchorDistance) {
					float scale = (refDist > 0.0001f) ? (kMaxAnchorDistance / refDist) : 0.f;
					anchor.easedAnchor.x = ceilingRef.x + rdx * scale;
					anchor.easedAnchor.y = ceilingRef.y + rdy * scale;
					anchor.easedAnchor.z = ceilingRef.z + rdz * scale;
				}

				cam->desiredCameraRootPos = anchor.easedAnchor;
				cam->cameraRootPos = anchor.easedAnchor;
				cam->unkCameraRootPosA = anchor.easedAnchor;
				cam->unkCameraRootPosB = anchor.easedAnchor;
			}
		}

	}
}
