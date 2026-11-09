#pragma once
#include "RE/Camera.h"
#include <unordered_map>

struct Vector2
{
	float x;
	float y;
};

class CameraController : public DKUtil::model::Singleton<CameraController>
{
public:
	enum class CameraMode : uint8_t
	{
		kExploration,
		kExplorationTactical,
		kCombat,
		kCombatTactical,
		kFreeCamera
	};

	struct PlayerData
	{
		std::optional<int16_t> nativePlayerId = std::nullopt;

		std::optional<float> pitch = std::nullopt;
		std::optional<float> pitchTransitionTarget = std::nullopt;
		float pitchTransitionSpeed = 4.f;
		bool pitchTransitionCancellable = true;
		float controllerPitchDelta = 0.f;
		bool skipToggleInputMode = false;
		RE::CameraObject* cameraObject = nullptr;

		float closeViewTransitionTimer = 0.f;
		uintptr_t prevActiveCharacter = 0;

		bool closeViewMode = false;
		bool closeViewModeInitialized = false;
		bool combatFocusTrackingStateInitialized = false;
		bool prevCombatFocusTrackingEnabled = false;
		bool prevActiveCloseViewMode = false;
		bool prevVanillaCombatActive = false; // vanilla-combat state last frame, used to detect transitions
		bool actionModeSuspend = false;
		bool l3HoldUsedForZoom = false;
		bool selectorModeActive = false;
		bool genuinelyTargeting = false;
		bool prevNativeCombatOrTB = false;
		bool prevSelectorMode = false;
		bool prevImmersiveCombatCloseView = false;
		bool prevGenuinelyTargetingForPitch = false;
		bool explorationSelectorPitchInitialized = false;
		bool prevExplorationSelectorMode = false;
		float combatSelectorEntryDelayTimer = 0.f;
		bool delayedTargetingForCombat = false;
		bool combatActionPhaseStateInitialized = false;
		bool prevCombatActionPhase = false;
		bool l3SelectorActive = false;
		bool wasL3Pressed = false;
		bool wasBPressed = false;
		bool capsLockFreelook = false;
		bool capsLockWasPhysicallyDown = false;

		float holdTime = 0.f;
		bool holdTriggered = false;
		bool keyboardWasPressed = false;

		bool horizontalOffsetFlipped = false;
		float flipHoldTime = 0.f;
		bool flipHoldTriggered = false;
		bool flipKeyboardWasPressed = false;

		float selectorCooldown = 0.f;
		float suspendCooldown = 0.f;
		float genuinelyTargetingCooldown = 0.f;

		std::optional<uint16_t> inputDeviceId = std::nullopt;

		std::optional<uint8_t> inputPlayerIndex = std::nullopt;
	};

	void ToggleCloseView(RE::CameraObject* cameraObject);

	void CheckAndHandleHoldToggle(int16_t playerId, RE::CameraObject* cameraObject);
	void CheckAndHandleHorizontalFlipToggle(int16_t playerId, RE::CameraObject* cameraObject);
	bool IsHorizontalOffsetFlipped(int16_t playerId) const { return GetPlayerData(playerId).horizontalOffsetFlipped; }
	bool IsControllerButtonPressed(int16_t playerId, int buttonMask) const;
	bool IsLeftStickActive(int16_t playerId) const;
	void NotifyInputDeviceForPlayer(int16_t playerId, uint16_t deviceId);
	std::optional<uint16_t> GetInputDeviceIdForPlayer(int16_t playerId) const { return GetPlayerData(playerId).inputDeviceId; }
	void NotifyPadIndexForPlayer(int16_t playerId, uint8_t padIndex);
	int16_t GetPadIndexForPlayer(int16_t playerId) const;
	bool IsCapsLockFreelookActive() const { return GetPlayerData(CurrentPlayerId()).capsLockFreelook; }
	void MarkL3ZoomGestureUsed() { GetPlayerData(CurrentPlayerId()).l3HoldUsedForZoom = true; }
	bool IsInCloseViewTransition() const { return GetPlayerData(CurrentPlayerId()).closeViewTransitionTimer > 0.f; }
	void NotifyCloseViewTransitionConverged() { GetPlayerData(CurrentPlayerId()).closeViewTransitionTimer = 0.f; }
	void ApplyZoomToggleCameraOverrides();
	bool IsCloseViewMode() const;
	bool ShouldApplyCloseViewFraming() const;
	bool GetBaseCloseViewMode() const;
	bool IsVanillaFarCameraActive(int16_t playerId) const;
	// True while the "Vanilla Camera in Combat" setting is on and the camera is in combat / turn-based mode
	bool IsVanillaCombatCameraActive(int16_t playerId) const;
	// True if either the vanilla far camera or the vanilla combat camera is active (the mod must not touch the camera)
	bool ShouldUseVanillaCamera(int16_t playerId) const;
	// Per-frame: detects entering/leaving vanilla combat and resets/restores the mod's pitch, zoom and selector state
	void UpdateVanillaCombatTransition();

	struct CameraDefinitionBaseline
	{
		float fovClose = 0.f, fovFar = 0.f, fovCloseAlt = 0.f, fovFarAlt = 0.f, tacticalFov = 0.f;
		float horizOffsetMult = 0.f, vertOffsetMult = 0.f;
		float minZoom = 0.f, maxZoom = 0.f, tactMinZoom = 0.f, tactMaxZoom = 0.f, altMinZoom = 0.f, altMaxZoom = 0.f;
		float pitchAdjustSpeedA = 0.f, pitchAdjustSpeedB = 0.f, pitchAdjustSpeedC = 0.f;
	};
	bool vanillaBaselineCaptured_ = false;
	CameraDefinitionBaseline vanillaExplorationBaseline_;
	CameraDefinitionBaseline vanillaCombatBaseline_;
	void CaptureVanillaCameraBaselineIfNeeded();
	void RestoreVanillaCameraBaseline();
	bool GetOriginalPitchAdjustSpeeds(RE::CameraDefinition* cameraDefinition, float& outA, float& outB, float& outC) const;

	static constexpr float VANILLA_DEADZONE = 0.65f;
	static constexpr float NORMALIZE_DEADZONE = 1.f / (1.f - VANILLA_DEADZONE);
	static constexpr float ZOOM_ADJUST_STEP = 0.01f;

	void SetCameraSettings();

	int16_t GetPlayerIdFromCameraObject(RE::CameraObject* cameraObject) const;
	void SetCameraObjectForPlayer(int16_t playerId, RE::CameraObject* cameraObject);
	int16_t GetOrAssignPlayerSlot(int16_t nativePlayerId);
	static CameraMode GetCurrentCameraMode(RE::CameraObject* cameraObject);
	static CameraMode GetCurrentCameraMode(RE::CameraModeFlags cameraModeFlags);
	bool IsCombatOrTurnBasedOrTacticalMode(RE::CameraObject* cameraObject) const;
	bool IsCameraUnlocked(int16_t playerId, RE::CameraObject* cameraObject) const;
	bool CanAdjustPitch(RE::CameraObject* cameraObject) const;
	bool CanAdjustPitch(CameraController::CameraMode cameraMode) const;

	bool ShouldSkipToggleInputMode(int16_t playerId) const { return GetPlayerData(playerId).skipToggleInputMode; }

	void SetControllerPitchDelta(int16_t playerId, float inputValue);
	void SetSkipToggleInputMode(int16_t playerId, bool skip) { GetPlayerData(playerId).skipToggleInputMode = skip; }

	void SetDeltaTime(float deltaTime) { deltaTime_ = deltaTime; }
	float GetDeltaTime() const { return deltaTime_; }

	void SetActionModeSuspend(bool suspend);
	bool GetActionModeSuspend() const { return GetPlayerData(CurrentPlayerId()).actionModeSuspend; }
	bool IsSelectorModeActive() const { return GetPlayerData(CurrentPlayerId()).selectorModeActive; }
	bool IsGenuinelyTargeting() const { return GetPlayerData(CurrentPlayerId()).genuinelyTargeting; }
	bool ShouldReleasePositionLock() const;
	void UpdateActionModeSuspend(RE::Player* player);
	bool IsCharacterInActionMode(uintptr_t characterPtr, int16_t nativePlayerId) const;
	bool IsCharacterGenuinelyTargeting(uintptr_t characterPtr) const;

	bool FindClientSnapshotForPlayer(bool& outSelectMode, bool& outInputControllerActive, bool& outEnemyTurnActive) const;

	void GetControlledCharacterTaskTypesForDiagnostics(uint32_t& outPreviewType, uint32_t& outRunningType) const;

	bool CalculateCameraPitch(int16_t playerId, RE::CameraObject* cameraObject, float& outPitch);
	void AdjustCameraZoomForPitch(uint64_t unkContext1, uint64_t unkContext2, RE::CameraObject* cameraObject);

	float AdjustInputValueForDeadzone(float inputValue, bool applyMult = true);

	int deltaY_;

	RE::Player* GetCurrentPlayer() { return currentPlayer_; }
	void SetCurrentPlayer(RE::Player* player) { currentPlayer_ = player; }
	RE::CameraObject* GetCurrentCamera() { return currentCamera_; }
	void SetCurrentCamera(RE::CameraObject* camera) { currentCamera_ = camera; }
	void SetCurrentUnkObject(RE::UnkObject* a) { currentUnkObject_ = a; }

	void SetActiveSlot(int16_t slot) { activeSlot_ = slot; }
	void ClearActiveSlot() { activeSlot_ = 0; }
	int16_t GetActiveSlot() const { return activeSlot_; }

protected:
	PlayerData& GetPlayerData(int16_t playerId) const
	{
		static PlayerData dummy;
		if (playerId < 1 || playerId > static_cast<int16_t>(playerData_.size())) {
			return dummy;
		}
		return playerData_[playerId - 1];
	}

	int16_t CurrentPlayerId() const { return GetPlayerIdFromCameraObject(currentCamera_); }

	float GetDeadzone();
	float NormalizeWithinRange(float min, float max, float value);
	float Denormalize(float min, float max, float value);
	float DegreesToRadians(float degrees);
	float InterpTo(float current, float target, float deltaTime, float interpSpeed);

	mutable std::array<PlayerData, 2> playerData_;

	float deltaTime_ = 0.f;
	RE::Player* currentPlayer_ = nullptr;
	RE::CameraObject* currentCamera_ = nullptr;
	RE::UnkObject* currentUnkObject_ = nullptr;
	int16_t activeSlot_ = 0;

	struct CharacterCameraState
	{
		RE::Vector3 prevCameraRootPos{ 0.f, 0.f, 0.f };
		float currentRunningFovOffset = 0.f;
		float currentRunningZoomOffset = 0.f;
		float currentRunningHorizontalOffset = 0.f;
		float currentRunningVerticalOffset = 0.f;
		float currentElementalIdleOffset = 0.f;
		float recentMaxCameraY = -9999.f;
		float currentCrouchOffset = 0.f;
		float currentHeightRatio = 0.f;

		bool heightInitialized = false;
		bool wasBeastForm = false;
		bool wasElementalForm = false;
		bool heightResamplePending = false;
		float heightResampleTimer = 0.f;
		float trackedHeight = 0.f;
		float currentCastBlend = 0.f;
	};
	std::unordered_map<uintptr_t, CharacterCameraState> characterStates_;
};
