#include "Hooks/Hooks.h"

#include "Camera/CameraController.h"
#include "Settings/Settings.h"
#include "Utils/Utils.h"

namespace
{
	constexpr bool kUseRightStickPressForZoom = false;
	constexpr bool kSwapZoomAndPitch = false;
	constexpr float kKeyboardCameraRotationMult = 2.0f;
	constexpr bool kResetZoomOnZoneChange = false;
}

namespace Hooks
{
	void* HookManager::Hook_HandleCameraInput(uint64_t unused1, uint64_t unused2, RE::UnkObject* cameraContext, uintptr_t inputEvent)
	{
		const Offsets::InputID inputId = *reinterpret_cast<Offsets::InputID*>(inputEvent);
		const bool isInControllerMode = *Offsets::isInControllerMode;

		auto* settings = Settings::Main::GetSingleton();

		if (isInControllerMode) {
			const auto nativePlayerIdForCapture = Utils::GetPlayerID(cameraContext);
			const auto playerIdForCapture = CameraController::GetSingleton()->GetOrAssignPlayerSlot(nativePlayerIdForCapture);
			const uint16_t deviceIdForCapture = *reinterpret_cast<uint16_t*>(inputEvent + 0x04);
			CameraController::GetSingleton()->NotifyInputDeviceForPlayer(playerIdForCapture, deviceIdForCapture);

			const uint8_t padIndexForCapture = *reinterpret_cast<uint8_t*>(inputEvent + 0x06);
			CameraController::GetSingleton()->NotifyPadIndexForPlayer(playerIdForCapture, padIndexForCapture);
		}

		{
			const auto cameraController = CameraController::GetSingleton();
			const auto playerIdForVanillaCheck = cameraController->GetOrAssignPlayerSlot(Utils::GetPlayerID(cameraContext));
			// Vanilla far view or vanilla combat: let the game handle camera input untouched
			if (cameraController->ShouldUseVanillaCamera(playerIdForVanillaCheck)) {
				return handleCameraInput_(unused1, unused2, cameraContext, inputEvent);
			}
		}

		switch (inputId) {
		case Offsets::InputID::kZoomIn:
		case Offsets::InputID::kZoomOut:
			{
				ReadLocker locker(settings->Lock);

				const auto cameraObject = cameraContext->currentCameraObject2;
				if (isInControllerMode) {
					const auto nativePlayerId = Utils::GetPlayerID(cameraContext);
					const auto playerId = CameraController::GetSingleton()->GetOrAssignPlayerSlot(nativePlayerId);
					float* pInputValue = reinterpret_cast<float*>(inputEvent + 0x18);

					const auto cameraController = CameraController::GetSingleton();

					bool doZoom;
					bool canAdjustPitch = cameraController->CanAdjustPitch(cameraObject);

					if (!canAdjustPitch) {
						doZoom = true;
					} else if (kUseRightStickPressForZoom) {
						doZoom = Offsets::ShouldShowSneakCones(*Offsets::UnkSingletonPtr, nativePlayerId);
					} else {
						const auto playerController = Offsets::GetPlayerController(*Offsets::UnkPlayerSingletonPtr, nativePlayerId);
						auto toggleInputId = Offsets::InputID::kToggleInputMode;
						RE::InputValue inputValue;
						Offsets::GetInputValue(*Offsets::UnkInputSingletonPtr, inputValue, toggleInputId, playerController);
						doZoom = inputValue.bIsPressed;
					}

					if (canAdjustPitch && kSwapZoomAndPitch) {
						doZoom = !doZoom;
					}

					const bool shouldSkipToggleInputMode = kSwapZoomAndPitch ? !doZoom : doZoom;
					if (canAdjustPitch && shouldSkipToggleInputMode && !kUseRightStickPressForZoom && *pInputValue > CameraController::VANILLA_DEADZONE) {
						cameraController->SetSkipToggleInputMode(playerId, true);
					}

					if (doZoom) {
						if (std::abs(*pInputValue) > CameraController::VANILLA_DEADZONE) {
							cameraController->MarkL3ZoomGestureUsed();
						}
						if (cameraController->IsCloseViewMode()) {
							cameraObject->zoomDelta = 0.f;
							*pInputValue = 0.f;
						}
						cameraController->SetControllerPitchDelta(playerId, 0.f);
					} else {
						cameraObject->zoomDelta = 0.f;

						const float sign = inputId == Offsets::InputID::kZoomIn ? -1.f : 1.f;

						cameraController->SetControllerPitchDelta(playerId, *pInputValue * sign);

						*pInputValue = 0.f;
						return handleCameraInput_(unused1, unused2, cameraContext, inputEvent);
					}
				}

				if (CameraController::GetSingleton()->IsCloseViewMode()) {
					cameraObject->zoomDelta = 0.f;
					float* pInputValue = reinterpret_cast<float*>(inputEvent + 0x18);
					if (pInputValue) {
						*pInputValue = 0.f;
					}
					return handleCameraInput_(unused1, unused2, cameraContext, inputEvent);
				}

				auto ret = handleCameraInput_(unused1, unused2, cameraContext, inputEvent);
				cameraObject->zoomDelta *= isInControllerMode ? settings->ControllerZoomMult : settings->MouseZoomMult;
				return ret;
			}
		case Offsets::InputID::kRotateLeft:
		case Offsets::InputID::kRotateRight:
			{
				float* pInputValue = reinterpret_cast<float*>(inputEvent + 0x18);
				if (isInControllerMode) {
					*pInputValue = CameraController::GetSingleton()->AdjustInputValueForDeadzone(*pInputValue);
				} else {
					const auto cameraObject = cameraContext->currentCameraObject2;
					auto ret = handleCameraInput_(unused1, unused2, cameraContext, inputEvent);
					cameraObject->currentAngleDelta *= kKeyboardCameraRotationMult;
					return ret;
				}
				break;
			}
		case Offsets::InputID::kMouseRotateLeft:
		case Offsets::InputID::kMouseRotateRight:
			{
				ReadLocker locker(settings->Lock);
				float* pInputValue = reinterpret_cast<float*>(inputEvent + 0x14);
				*pInputValue *= settings->MouseCameraRotationMult;
				break;
			}
		}

		return handleCameraInput_(unused1, unused2, cameraContext, inputEvent);
	}

	int16_t* HookManager::Hook_HandleToggleInputMode(uint64_t unkContext, int16_t& outResult, Offsets::InputID* inputId)
	{
		if (*inputId == Offsets::InputID::kToggleInputMode) {
			const auto cameraController = CameraController::GetSingleton();
			const auto nativePlayerId = *reinterpret_cast<int16_t*>(unkContext + 0x168);
			const auto playerId = cameraController->GetOrAssignPlayerSlot(nativePlayerId);
			if (cameraController->ShouldSkipToggleInputMode(playerId)) {
				cameraController->SetSkipToggleInputMode(playerId, false);
				outResult = 0;
				return &outResult;
			}
		}

		return handleToggleInputMode_(unkContext, outResult, inputId);
	}

	void HookManager::Hook_SetDefaultZoom(RE::CameraObject* cameraObject)
	{
		float desiredZoom = cameraObject->desiredZoom;

		setDefaultZoom_(cameraObject);

		const auto cameraController = CameraController::GetSingleton();
		const auto playerId = cameraController->GetPlayerIdFromCameraObject(cameraObject);
		// Keep the game's default zoom behaviour in vanilla modes
		if (cameraController->ShouldUseVanillaCamera(playerId)) {
			return;
		}

		if (!kResetZoomOnZoneChange) {
			cameraObject->desiredZoom = desiredZoom;
			cameraObject->currentZoom_160 = desiredZoom;
		}
	}

	bool HookManager::Hook_SDLMouseYHook(uint64_t unused1, uint64_t unused2, bool unused3, int deltaY)
	{
		CameraController::GetSingleton()->deltaY_ = deltaY;

		return sdlMouseYHook_(unused1, unused2, unused3, deltaY);
	}
}
