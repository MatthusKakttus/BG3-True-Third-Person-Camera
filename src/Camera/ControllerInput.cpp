#include "Camera/CameraController.h"

#include <Xinput.h>
#include <windows.h>

namespace
{
	constexpr bool kDeviceRoutingEnabled = true;

	// Read-only access to the game's own SDL2 game controller state.
	//
	// NOTE: SDL_GameControllerUpdate is deliberately NOT bound or called here. It used to be
	// called on every poll (once per frame from the camera hooks). The game already pumps SDL
	// events itself, and the extra calls interfered with its input handling while a level
	// transition was re-initialising input, leaving the game stuck on a black loading screen
	// when playing with a controller. We only read the state the game has already updated.
	struct SdlPad
	{
		typedef int(__cdecl* tNumJoysticks)();
		typedef int(__cdecl* tIsGameController)(int);
		typedef void*(__cdecl* tOpen)(int);
		typedef uint8_t(__cdecl* tGetButton)(void*, int);
		typedef int16_t(__cdecl* tGetAxis)(void*, int);
		typedef void(__cdecl* tClose)(void*);
		typedef int(__cdecl* tGetAttached)(void*);

		static inline tNumJoysticks NumJoysticks = nullptr;
		static inline tIsGameController IsGameController = nullptr;
		static inline tOpen Open = nullptr;
		static inline tClose Close = nullptr;
		static inline tGetButton GetButton = nullptr;
		static inline tGetAxis GetAxis = nullptr;
		static inline tGetAttached GetAttached = nullptr;
		static inline bool triedLoad = false;

		static inline constexpr int kMaxPads = 4;
		static inline void* handles[kMaxPads] = { nullptr, nullptr, nullptr, nullptr };

		static void EnsureLoaded()
		{
			if (triedLoad)
				return;
			triedLoad = true;
			HMODULE h = GetModuleHandleA("SDL2.dll");
			if (!h)
				h = LoadLibraryA("SDL2.dll");
			if (!h)
				return;
			NumJoysticks = reinterpret_cast<tNumJoysticks>(GetProcAddress(h, "SDL_NumJoysticks"));
			IsGameController = reinterpret_cast<tIsGameController>(GetProcAddress(h, "SDL_IsGameController"));
			Open = reinterpret_cast<tOpen>(GetProcAddress(h, "SDL_GameControllerOpen"));
			GetButton = reinterpret_cast<tGetButton>(GetProcAddress(h, "SDL_GameControllerGetButton"));
			GetAxis = reinterpret_cast<tGetAxis>(GetProcAddress(h, "SDL_GameControllerGetAxis"));
			Close = reinterpret_cast<tClose>(GetProcAddress(h, "SDL_GameControllerClose"));
			GetAttached = reinterpret_cast<tGetAttached>(GetProcAddress(h, "SDL_GameControllerGetAttached"));
		}

		static void* GetHandle(int padIndex)
		{
			EnsureLoaded();
			if (!Open || !NumJoysticks || !IsGameController || padIndex < 0 || padIndex >= kMaxPads)
				return nullptr;
			if (handles[padIndex]) {
				if (GetAttached && !GetAttached(handles[padIndex])) {
					// The pad was detached or re-enumerated (e.g. during a transition). Close our
					// handle instead of just dropping the pointer, so the reference is not leaked
					// and no stale handle is kept. A fresh one is opened below.
					if (Close) {
						Close(handles[padIndex]);
					}
					handles[padIndex] = nullptr;
				} else {
					return handles[padIndex];
				}
			}
			int found = -1;
			const int n = NumJoysticks();
			for (int i = 0; i < n && i < 32; ++i) {
				if (IsGameController(i)) {
					++found;
					if (found == padIndex) {
						void* c = Open(i);
						if (c) {
							handles[padIndex] = c;
						}
						return handles[padIndex];
					}
				}
			}
			return nullptr;
		}

		static int XInputMaskToButton(int mask)
		{
			switch (mask) {
			case 0x1000:
				return 0; // A
			case 0x2000:
				return 1; // B
			case 0x4000:
				return 2; // X
			case 0x8000:
				return 3; // Y
			case 0x0020:
				return 4; // BACK
			case 0x0010:
				return 6; // START
			case 0x0040:
				return 7; // LEFT_THUMB
			case 0x0080:
				return 8; // RIGHT_THUMB
			case 0x0100:
				return 9; // LEFT_SHOULDER
			case 0x0200:
				return 10; // RIGHT_SHOULDER
			case 0x0001:
				return 11; // DPAD_UP
			case 0x0002:
				return 12; // DPAD_DOWN
			case 0x0004:
				return 13; // DPAD_LEFT
			case 0x0008:
				return 14; // DPAD_RIGHT
			default:
				return -1;
			}
		}
	};

	int ResolvePadIndex(const CameraController* controller, int16_t playerId)
	{
		if (!kDeviceRoutingEnabled) {
			return 0;
		}
		return static_cast<int>(controller->GetPadIndexForPlayer(playerId));
	}
}

bool CameraController::IsControllerButtonPressed(int16_t playerId, int buttonMask) const
{
	const int padIndex = ResolvePadIndex(this, playerId);

	// Read-only SDL access, no SDL_GameControllerUpdate (see SdlPad).
	if (void* c = SdlPad::GetHandle(padIndex)) {
		const int btn = SdlPad::XInputMaskToButton(buttonMask);
		if (btn >= 0 && SdlPad::GetButton) {
			return SdlPad::GetButton(c, btn) != 0;
		}
		return false;
	}

	typedef DWORD(WINAPI * XInputGetState_t)(DWORD dwUserIndex, XINPUT_STATE * pState);
	static XInputGetState_t pXInputGetState = nullptr;
	static bool triedLoad = false;

	if (!pXInputGetState && !triedLoad) {
		triedLoad = true;
		HMODULE hXInput = LoadLibraryA("xinput1_4.dll");
		if (!hXInput)
			hXInput = LoadLibraryA("xinput9_1_0.dll");
		if (!hXInput)
			hXInput = LoadLibraryA("xinput1_3.dll");
		if (hXInput) {
			pXInputGetState = (XInputGetState_t)GetProcAddress(hXInput, "XInputGetState");
		}
	}

	if (pXInputGetState) {
		const DWORD userIndex = static_cast<DWORD>(padIndex) % 4;
		XINPUT_STATE state{};
		if (pXInputGetState(userIndex, &state) == ERROR_SUCCESS) {
			if (state.Gamepad.wButtons & static_cast<WORD>(buttonMask)) {
				return true;
			}
		}
	}

	return false;
}

bool CameraController::IsLeftStickActive(int16_t playerId) const
{
	const int padIndex = ResolvePadIndex(this, playerId);

	// Read-only SDL access, no SDL_GameControllerUpdate (see SdlPad).
	if (void* c = SdlPad::GetHandle(padIndex)) {
		if (SdlPad::GetAxis) {
			const float x = static_cast<float>(SdlPad::GetAxis(c, 0)); // SDL_CONTROLLER_AXIS_LEFTX
			const float y = static_cast<float>(SdlPad::GetAxis(c, 1)); // SDL_CONTROLLER_AXIS_LEFTY
			const float dz = static_cast<float>(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
			return (x * x + y * y) > dz * dz;
		}
		return false;
	}

	typedef DWORD(WINAPI * XInputGetState_t)(DWORD dwUserIndex, XINPUT_STATE * pState);
	static XInputGetState_t pXInputGetState = nullptr;
	static bool triedLoad = false;

	if (!pXInputGetState && !triedLoad) {
		triedLoad = true;
		HMODULE hXInput = LoadLibraryA("xinput1_4.dll");
		if (!hXInput)
			hXInput = LoadLibraryA("xinput9_1_0.dll");
		if (!hXInput)
			hXInput = LoadLibraryA("xinput1_3.dll");
		if (hXInput) {
			pXInputGetState = (XInputGetState_t)GetProcAddress(hXInput, "XInputGetState");
		}
	}

	if (pXInputGetState) {
		const DWORD userIndex = static_cast<DWORD>(padIndex) % 4;
		XINPUT_STATE state{};
		if (pXInputGetState(userIndex, &state) == ERROR_SUCCESS) {
			const float x = static_cast<float>(state.Gamepad.sThumbLX);
			const float y = static_cast<float>(state.Gamepad.sThumbLY);
			if ((x * x + y * y) > static_cast<float>(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) * static_cast<float>(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE)) {
				return true;
			}
		}
	}

	return false;
}
