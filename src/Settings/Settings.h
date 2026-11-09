#pragma once
#include "PCH.h"

namespace Settings
{
	class Main : public DKUtil::model::Singleton<Main>
	{
	public:
		bool InvertPitch = false;
		bool UnlockedPitchLimitClipping = true;
		bool ZoomToggleImmersiveMode = true;
		float ZoomToggleCharacterSwitchTransitionDuration = 0.7f;
		float ControllerCameraRotationMult = 2.0f;
		float MouseCameraRotationMult = 1.0f;
		float ControllerPitchMult = 0.5f;
		float ControllerZoomMult = 0.5f;
		float MousePitchMult = 0.25f;
		float MouseZoomMult = 0.5f;
		float ZoomToggleCloseValue = 2.5f;
		float ZoomToggleSelectorCloseValue = 4.0f;
		float ZoomToggleCombatFOV = 35.0f;
		float ZoomToggleFarValue = 10.0f;
		float ZoomToggleFarFOV = 55.0f;
		bool VanillaFarCamera = false;
		// MCM "Use Vanilla Camera in Combat": disables this mod's combat camera and uses the game's default one
		bool VanillaCombatCamera = false;
		float ZoomToggleCloseFOV = 40.0f;
		float ZoomToggleCloseHorizontalOffset = 0.50f;
		float ZoomToggleCloseVerticalOffset = 0.74f;
		float ZoomToggleRunningFOVIncrease = 5.0f;
		float ZoomToggleCrouchVerticalOffset = -0.20f;
		int ZoomToggleKeyboardKey = 106;
		int ZoomToggleHorizontalFlipKey = 111;

		float ZoomToggleRunningZoomIncrease = -0.1f;
		float ZoomToggleRunningHorizontalOffsetIncrease = 0.0f;
		float ZoomToggleRunningVerticalOffsetIncrease = 0.0f;

		float ZoomToggleCastMinZoom = 5.0f;

		float ZoomToggleCombatActionFOV = 50.0f;
		float ZoomToggleCombatActionZoom = 5.0f;

		bool ZoomToggleFTBActive = false;
		bool IsControllingBeastForm = false;
		bool IsControllingElementalForm = false;
		bool HasUnstableBoundsStatus = false;
		float BeastZoomPushOverride = -1.0f;
		float BeastVerticalPushOverride = -1.0f;

		bool PlanarAllyActive = false;
		float PlanarAllyZoomPush = 0.0f;
		float PlanarAllyVerticalPush = 0.0f;
		float PlanarAllyHorizontalPush = 0.0f;
		bool PlanarAllyElevateOnFly = false;

		bool PersistedCloseViewMode = false;

		struct ClientCameraSnapshot
		{
			bool valid = false;
			int userId = -1;
			bool selectMode = false;
			bool inputControllerActive = true;
			bool enemyTurnActive = false;
		};
		std::array<ClientCameraSnapshot, 2> ClientCameraSnapshots;

		SharedLock Lock;

		void Load() noexcept;
		bool changed = false;

		void WatchForChanges();
		static void SaveCameraState(bool closeViewMode, bool combatFocusTrackingEnabled, bool combatActionPhase) noexcept;
	};
}
