#include "Hooks/Hooks.h"

#include "Camera/CameraController.h"
#include "Hooks/CameraAnchorState.h"
#include "Settings/Settings.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace Hooks
{
	struct OneEuroFilter
	{
		bool initialized = false;
		float xPrev = 0.f;
		float dxPrev = 0.f;

		static float Alpha(float cutoff, float dt)
		{
			const float tau = 1.0f / (2.0f * 3.14159265358979f * cutoff);
			return 1.0f / (1.0f + tau / dt);
		}

		float Filter(float x, float dt, float minCutoff, float beta, float dCutoff)
		{
			if (!initialized) {
				initialized = true;
				xPrev = x;
				dxPrev = 0.f;
				return x;
			}
			if (dt <= 0.f) {
				return xPrev;
			}
			const float dx = (x - xPrev) / dt;
			const float aD = Alpha(dCutoff, dt);
			dxPrev = dxPrev + aD * (dx - dxPrev);
			const float cutoff = minCutoff + beta * std::abs(dxPrev);
			const float a = Alpha(cutoff, dt);
			xPrev = xPrev + a * (x - xPrev);
			return xPrev;
		}

		void Reset() { initialized = false; }
	};

	struct PerPlayerUpdateState
	{
		bool inCharacterSwitchGlide = false;
		float switchGlideElapsed = 0.f;
		RE::Vector3 switchGlideStartRootPos{};
		RE::Vector3 switchGlideStartUnkA{};
		RE::Vector3 switchGlideStartUnkB{};
		uintptr_t lastControlledCharacter = 0;

		bool hasSmoothedDt = false;
		float smoothedDt = 0.f;

		static constexpr int kDtHistorySize = 5;
		float dtHistory[kDtHistorySize] = { 0.f, 0.f, 0.f, 0.f, 0.f };
		int dtHistoryCount = 0;
		int dtHistoryIdx = 0;

		OneEuroFilter oneEuroX;
		OneEuroFilter oneEuroY;
		OneEuroFilter oneEuroZ;
	};
	static std::array<PerPlayerUpdateState, 2> s_perPlayerUpdateState;

	void HookManager::Hook_UpdateCamera(uint64_t unused1, uint64_t unused2, uint64_t unused3, RE::UnkObject* cameraContext)
	{
		const auto cameraController = CameraController::GetSingleton();

		cameraController->CaptureVanillaCameraBaselineIfNeeded();

		cameraController->SetCurrentPlayer(cameraContext->currentPlayer);
		cameraController->SetCurrentCamera(cameraContext->currentCameraObject);
		cameraController->SetCurrentUnkObject(cameraContext);

		int16_t playerId = 0;
		if (cameraContext->currentPlayer) {
			playerId = cameraController->GetOrAssignPlayerSlot(cameraContext->currentPlayer->playerId_38);
			if (cameraContext->currentCameraObject) {
				cameraController->SetCameraObjectForPlayer(playerId, cameraContext->currentCameraObject);
			}
		}

		cameraController->SetActiveSlot(playerId);

		static PerPlayerUpdateState dummyUpdateState;
		PerPlayerUpdateState& ups = (playerId >= 1 && playerId <= static_cast<int16_t>(s_perPlayerUpdateState.size()))
		                                ? s_perPlayerUpdateState[playerId - 1]
		                                : dummyUpdateState;

		bool controlledCharacterChanged = false;
		if (cameraContext->currentPlayer && Offsets::UnkPlayerSingletonPtr && *Offsets::UnkPlayerSingletonPtr && Offsets::GetCharacter) {
			uintptr_t playerMgr = reinterpret_cast<uintptr_t>(*Offsets::UnkPlayerSingletonPtr);
			uintptr_t currentCharacter = Offsets::GetCharacter(playerMgr, cameraContext->currentPlayer->playerId_38);
			if (currentCharacter != 0) {
				if (ups.lastControlledCharacter != 0 && currentCharacter != ups.lastControlledCharacter) {
					controlledCharacterChanged = true;
				}
				ups.lastControlledCharacter = currentCharacter;
			}
		}

		// Handle entering/leaving the vanilla combat camera before settings and overrides are applied
		cameraController->UpdateVanillaCombatTransition();
		cameraController->SetCameraSettings();
		cameraController->ApplyZoomToggleCameraOverrides();

		if (playerId != 0 && cameraContext->currentCameraObject) {
			cameraController->CheckAndHandleHoldToggle(playerId, cameraContext->currentCameraObject);
			cameraController->CheckAndHandleHorizontalFlipToggle(playerId, cameraContext->currentCameraObject);
		}

		updateCamera_(unused1, unused2, unused3, cameraContext);

		if (cameraContext->currentCameraObject) {
			auto* cam = cameraContext->currentCameraObject;
			constexpr float kMaxSaneZoom = 500.0f;
			const bool desiredSane = cam->desiredZoom >= -kMaxSaneZoom && cam->desiredZoom <= kMaxSaneZoom;
			const bool currentSane = cam->currentZoom_160 >= -kMaxSaneZoom && cam->currentZoom_160 <= kMaxSaneZoom;
			if (!desiredSane || !currentSane) {
				const auto settings = Settings::Main::GetSingleton();
				const float fallbackZoom = cameraController->ShouldApplyCloseViewFraming() ? settings->ZoomToggleCloseValue : settings->ZoomToggleFarValue;
				cam->desiredZoom = fallbackZoom;
				cam->currentZoom_160 = fallbackZoom;
			}
		}

		if (cameraContext->currentCameraObject && cameraContext->currentPlayer && cameraController->GetBaseCloseViewMode()) {
			constexpr bool kZoomToggleLockCameraToCharacter = true;
			if (kZoomToggleLockCameraToCharacter) {
				auto* cam = cameraContext->currentCameraObject;
				float deltaTime = cameraController->GetDeltaTime();

				const bool shouldReleaseLock = cameraController->ShouldReleasePositionLock();

				if (!shouldReleaseLock) {
					if (deltaTime > 0.f && deltaTime < 0.2f) {
						float preDt = deltaTime;
						{
							ups.dtHistory[ups.dtHistoryIdx] = deltaTime;
							ups.dtHistoryIdx = (ups.dtHistoryIdx + 1) % PerPlayerUpdateState::kDtHistorySize;
							if (ups.dtHistoryCount < PerPlayerUpdateState::kDtHistorySize) {
								ups.dtHistoryCount++;
							}
							float sorted[PerPlayerUpdateState::kDtHistorySize];
							for (int i = 0; i < ups.dtHistoryCount; ++i) {
								sorted[i] = ups.dtHistory[i];
							}
							for (int i = 1; i < ups.dtHistoryCount; ++i) {
								const float key = sorted[i];
								int j = i - 1;
								while (j >= 0 && sorted[j] > key) {
									sorted[j + 1] = sorted[j];
									--j;
								}
								sorted[j + 1] = key;
							}
							preDt = sorted[ups.dtHistoryCount / 2];
						}

						constexpr float kDtSmoothingStrength = 0.05f;
						float trackingDt;
						if (!ups.hasSmoothedDt) {
							ups.smoothedDt = preDt;
							ups.hasSmoothedDt = true;
						} else {
							ups.smoothedDt += (preDt - ups.smoothedDt) * kDtSmoothingStrength;
						}
						trackingDt = ups.smoothedDt;

						const RE::Vector3& filteredTarget = cam->desiredCameraRootPos;

						const float switchTransitionDuration = Settings::Main::GetSingleton()->ZoomToggleCharacterSwitchTransitionDuration;

						if (controlledCharacterChanged) {
							ups.oneEuroX.Reset();
							ups.oneEuroY.Reset();
							ups.oneEuroZ.Reset();
						}

						if (controlledCharacterChanged && switchTransitionDuration > 0.f) {
							ups.inCharacterSwitchGlide = true;
							ups.switchGlideElapsed = 0.f;
							ups.switchGlideStartRootPos = cam->cameraRootPos;
							ups.switchGlideStartUnkA = cam->unkCameraRootPosA;
							ups.switchGlideStartUnkB = cam->unkCameraRootPosB;
						}

						if (ups.inCharacterSwitchGlide) {
							ups.switchGlideElapsed += deltaTime;
							float t = std::clamp(ups.switchGlideElapsed / switchTransitionDuration, 0.0f, 1.0f);
							float smoothT = t * t * (3.0f - 2.0f * t);

							auto durationLerp = [smoothT](RE::Vector3& current, const RE::Vector3& start, const RE::Vector3& target) {
								current.x = start.x + (target.x - start.x) * smoothT;
								current.y = start.y + (target.y - start.y) * smoothT;
								current.z = start.z + (target.z - start.z) * smoothT;
							};

							durationLerp(cam->cameraRootPos, ups.switchGlideStartRootPos, filteredTarget);
							durationLerp(cam->unkCameraRootPosA, ups.switchGlideStartUnkA, filteredTarget);
							durationLerp(cam->unkCameraRootPosB, ups.switchGlideStartUnkB, filteredTarget);

							if (t >= 1.0f) {
								ups.inCharacterSwitchGlide = false;
							}
						} else {
							constexpr float kOneEuroMinCutoff = 2.0f;
							constexpr float kOneEuroBeta = 0.4f;
							constexpr float kOneEuroDCutoff = 1.0f;

							RE::Vector3 filtered;
							filtered.x = ups.oneEuroX.Filter(filteredTarget.x, trackingDt, kOneEuroMinCutoff, kOneEuroBeta, kOneEuroDCutoff);
							filtered.y = ups.oneEuroY.Filter(filteredTarget.y, trackingDt, kOneEuroMinCutoff, kOneEuroBeta, kOneEuroDCutoff);
							filtered.z = ups.oneEuroZ.Filter(filteredTarget.z, trackingDt, kOneEuroMinCutoff, kOneEuroBeta, kOneEuroDCutoff);

							cam->cameraRootPos = filtered;
							cam->unkCameraRootPosA = filtered;
							cam->unkCameraRootPosB = filtered;
						}
					} else {
						cam->cameraRootPos = cam->desiredCameraRootPos;
						cam->unkCameraRootPosA = cam->desiredCameraRootPos;
						cam->unkCameraRootPosB = cam->desiredCameraRootPos;
						ups.inCharacterSwitchGlide = false;
						ups.hasSmoothedDt = false;
						ups.dtHistoryCount = 0;
						ups.dtHistoryIdx = 0;
					}
					cam->horizontalPanDelta = 0.f;
					cam->verticalPanDelta = 0.f;

					auto& anchor = AnchorState::Get(playerId);
					anchor.hasLastLockedAnchor = true;
					anchor.lastLockedAnchor = cam->desiredCameraRootPos;
					anchor.hasEasedAnchor = false;
				}
			}
		}

		cameraController->ClearActiveSlot();
	}
}
