#include "Hooks/Hooks.h"

#include "Camera/CameraController.h"

namespace Hooks
{
	float HookManager::Hook_CalculateCameraPitch(RE::CameraObject* cameraObject, uint8_t unused1, uint8_t unused2)
	{
		float pitch = calculateCameraPitch_(cameraObject, unused1, unused2);

		auto cameraController = CameraController::GetSingleton();
		auto playerId = cameraController->GetPlayerIdFromCameraObject(cameraObject);
		if (playerId == 0) {
			return pitch;
		}

		// Vanilla far view or vanilla combat: keep the game's own pitch
		if (cameraController->ShouldUseVanillaCamera(playerId)) {
			return pitch;
		}

		cameraController->CalculateCameraPitch(playerId, cameraObject, pitch);

		return pitch;
	}

	void HookManager::Hook_UpdateCameraPitch(uint64_t unused1, uint64_t unused2, RE::CameraObject* cameraObject, uint64_t frameContext)
	{
		const float deltaTime = *reinterpret_cast<float*>(frameContext + 0x8);
		auto cameraController = CameraController::GetSingleton();
		cameraController->SetDeltaTime(deltaTime);

		const int16_t playerId = cameraController->GetPlayerIdFromCameraObject(cameraObject);
		if (playerId == 0) {
			updateCameraPitch_(unused1, unused2, cameraObject, frameContext);
			return;
		}

		// Vanilla far view or vanilla combat: run the game's pitch update unmodified
		if (cameraController->ShouldUseVanillaCamera(playerId)) {
			updateCameraPitch_(unused1, unused2, cameraObject, frameContext);
			return;
		}

		if (cameraController->IsCameraUnlocked(playerId, cameraObject)) {
			const auto cameraDefinition = Offsets::GetCurrentCameraDefinition(cameraObject);

			float originalPitchAdjustSpeedA, originalPitchAdjustSpeedB, originalPitchAdjustSpeedC;
			const bool haveBaseline = cameraController->GetOriginalPitchAdjustSpeeds(cameraDefinition, originalPitchAdjustSpeedA, originalPitchAdjustSpeedB, originalPitchAdjustSpeedC);
			if (!haveBaseline) {
				originalPitchAdjustSpeedA = cameraDefinition->pitchAdjustSpeedA_48;
				originalPitchAdjustSpeedB = cameraDefinition->pitchAdjustSpeedB_F0;
				originalPitchAdjustSpeedC = cameraDefinition->pitchAdjustSpeedC_F4;
			}

			cameraDefinition->pitchAdjustSpeedA_48 = 100000.f;
			cameraDefinition->pitchAdjustSpeedB_F0 = 100000.f;
			cameraDefinition->pitchAdjustSpeedC_F4 = 100000.f;

			updateCameraPitch_(unused1, unused2, cameraObject, frameContext);

			cameraDefinition->pitchAdjustSpeedA_48 = originalPitchAdjustSpeedA;
			cameraDefinition->pitchAdjustSpeedB_F0 = originalPitchAdjustSpeedB;
			cameraDefinition->pitchAdjustSpeedC_F4 = originalPitchAdjustSpeedC;
		} else {
			updateCameraPitch_(unused1, unused2, cameraObject, frameContext);
		}
	}
}
