// JoyShockLibrary.cpp : Defines the exported functions for the DLL application.
//

#include "JoyShockLibrary.h"
#include <bitset>
#include "hidapi.h"
#include <chrono>
#include <thread>
#include <shared_mutex>
#include <unordered_map>
#include <atomic>
#include "GamepadMotion.hpp"
#include "JoyShock.h"
#include "InputHelpers.h"
#include "JoyShockLibrary4Unreal.h"
#include "JoyShockInterface.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"

DEFINE_LOG_CATEGORY(LogJoyShockLibrary)

TMap<int32, JoyShock*> _joyshocks;

TMap<FString, JoyShock*> _byPath;

FCriticalSection _pathHandleLock;
TMap<FString, int32> _pathHandle;

static int32 GetUniqueHandle(const FString &path)
{
	_pathHandleLock.Lock();
	int32* iter = _pathHandle.Find(path);

	if (iter != nullptr)
	{
		_pathHandleLock.Unlock();
		return *iter;
	}

	// Assign the lowest handle not currently in use, so a controller that reconnects (or is re-paired,
	// getting a new device path) reuses a freed slot -- e.g. players 0 and 1 -- instead of ever-increasing
	// ids (2, 3, ...). Handles are freed when a device is removed (see the poll-thread disconnect cleanup).
	// This handle is also what the input-device mapper uses as the player
	// index, so keeping it dense keeps player numbers stable across reconnects.
	int32 handle = 0;
	for (bool bInUse = true; bInUse; )
	{
		bInUse = false;
		for (const TTuple<FString, int32>& pair : _pathHandle)
		{
			if (pair.Value == handle)
			{
				bInUse = true;
				handle++;
				break;
			}
		}
	}

	_pathHandle.Emplace(path, handle);
	_pathHandleLock.Unlock();

	return handle;
}

// https://stackoverflow.com/questions/25144887/map-unordered-map-prefer-find-and-then-at-or-try-at-catch-out-of-range
// not thread-safe -- because you probably want to do something with the object you get out of it, I've left locking to the caller
static JoyShock* GetJoyShockFromHandle(int handle) {
	JoyShock** iter = _joyshocks.Find(handle);

	if (iter != nullptr)
	{
		return *iter;
	}
	return nullptr;
}

// Nintendo's four LEDs use distinct patterns for players 1-8. They are deliberately not the
// one-hot value 1 << (player-1): after player 4 the console identifies players with combinations.
static unsigned char PlayerNumberToSwitchLedMask(int32 PlayerNumber)
{
	static constexpr unsigned char PlayerMasks[] =
	{
		0x01, // P1: *...
		0x03, // P2: **..
		0x07, // P3: ***.
		0x0F, // P4: ****
		0x09, // P5: *..*
		0x05, // P6: *.*.
		0x0D, // P7: *.**
		0x06, // P8: .**.
	};
	return PlayerNumber >= 1 && PlayerNumber <= UE_ARRAY_COUNT(PlayerMasks)
		? PlayerMasks[PlayerNumber - 1]
		: 0;
}

static unsigned char PlayerNumberToDualSenseLedMask(int32 PlayerNumber)
{
	static constexpr unsigned char PlayerMasks[] =
	{
		DS5_PLAYER_1,
		DS5_PLAYER_2,
		DS5_PLAYER_3,
		DS5_PLAYER_4,
		DS5_PLAYER_5,
	};
	return PlayerNumber >= 1 && PlayerNumber <= UE_ARRAY_COUNT(PlayerMasks)
		? PlayerMasks[PlayerNumber - 1]
		: 0;
}

void pollIndividualLoop(JoyShock *jc) {
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	if (!jc->handle) { return; }

	hid_set_nonblocking(jc->handle, 0);
	//hid_set_nonblocking(jc->handle, 1); // temporary, to see if it helps. this means we'll have a crazy spin

	int numTimeOuts = 0;
	int consecutiveReportsWithoutIMU = 0;
	// Joy-Con firmware can acknowledge the IMU enable during init and still never start the sensor
	// (observed on the first Joy-Con to wake the Bluetooth radio: acked 0x40, then streamed zeroed IMU
	// forever). These drive the in-stream repair: an off->on toggle of subcommand 0x40, split across two
	// report reads so the controller gets an input-report interval to process the disable.
	int imuRepairAttempts = 0;
	bool bImuRepairToggleOffSent = false;
	bool bLoggedMissingIMU = false;
	// Functions already reported as blocked, so each one fires a single event per failure episode.
	uint8_t reportedBlockedFunctions = 0;
	bool hasIMU = false;
	bool lockedThread = false;
	// Whether this device ever delivered a real input packet. Used to avoid re-scanning for a "phantom"
	// device: a controller that is powered off but still lingers in HID enumeration can be opened and
	// even respond to the init handshake, yet never produce input. Re-scanning after such a device drops
	// would just recreate it in an endless loop, so we only trigger a rescan for devices that actually worked.
	bool bReceivedInput = false;

	// Diagnostic: log the first report the Switch 2 streams (size + report id), so we can confirm whether
	// it started streaming after init.
	bool bLoggedFirstRead = false;

	// Rumble: this thread is the SOLE writer of rumble packets/commands (JSL4USetRumble only stores the
	// values). Switch 1 rumble decays shortly after a single 0x10 packet, and the Switch 2 only has a short
	// one-shot preset, so while active the state is re-sent/retriggered continuously; a change to (0,0)
	// sends one final stop.
	int rumbleRefreshCounter = 0;
	unsigned char lastSentSmallRumble = 0;
	unsigned char lastSentBigRumble = 0;
	auto lastSw2RumbleTime = std::chrono::steady_clock::now();
	auto lastSw2InitRetryTime = std::chrono::steady_clock::now();

	// DualShock 4 / DualSense: rumble, light colour and the player LED all ride in the SAME output report,
	// so they are tracked together and sent as one packet whenever any of them changes. Seeded from what
	// JslConnectDevices already sent when it brought this device online, so we don't re-send it immediately.
	unsigned char lastSentLedR = jc->led_r;
	unsigned char lastSentLedG = jc->led_g;
	unsigned char lastSentLedB = jc->led_b;
	int32 lastSentPlayerNumber = jc->player_number;
	bool bHomeLightCleared = false;

	float wakeupTimer = 0.0f;

	while (!jc->cancel_thread) {
		// get input:
		unsigned char buf[64];
		memset(buf, 0, 64);

		// 10 seconds of no signal means forget this controller
		int reuseCounter = jc->reuse_counter;
		int res = hid_read_timeout(jc->handle, buf, 64, 1000);

		if (res == -1)
		{
			// disconnected!
			UE_LOG(LogJoyShockLibrary, Log, TEXT("Controller %d disconnected\n"), jc->intHandle);

			JSL4UModule._connectedLock.lock();
			// UJoyShockLibrary::_connectedLock.lock();
			// UJoyShockLibrary::ConnectedLock.Lock();
			lockedThread = true;
			const bool gettingReused = jc->reuse_counter != reuseCounter;
			jc->delete_on_finish = true;
			if (gettingReused)
			{
				jc->remove_on_finish = false;
				jc->delete_on_finish = false;
				lockedThread = false;
				JSL4UModule._connectedLock.unlock();
				// UJoyShockLibrary::_connectedLock.unlock();
				// UJoyShockLibrary::ConnectedLock.Unlock();
			}
			break;
		}

		if (res == 0)
		{
			numTimeOuts++;
			if (numTimeOuts >= 10 && !jc->is_switch2_pro)
			{
				UE_LOG(LogJoyShockLibrary, Log, TEXT("Controller %d timed out\n"), jc->intHandle);

				// just make sure we get this thing deleted before someone else tries to start a new connection
				JSL4UModule._connectedLock.lock();
				// UJoyShockLibrary::ConnectedLock.Lock();
				lockedThread = true;
				const bool gettingReused = jc->reuse_counter != reuseCounter;
				jc->delete_on_finish = true;
				if (gettingReused)
				{
					jc->remove_on_finish = false;
					jc->delete_on_finish = false;
					lockedThread = false;
					JSL4UModule._connectedLock.unlock();
					// UJoyShockLibrary::ConnectedLock.Unlock();
				}
				break;
			}
			// Deliberately no re-init here to "wake up" a quiet controller: the full handshake is
			// destructive, not restorative. A silent device is still present (hid_read returns 0, not -1),
			// but re-running init_bt on it drops the Bluetooth link outright -- the very next read returns
			// -1 -- so a one-second hiccup was being turned into a disconnect/reconnect cycle. The Switch 2
			// already had to be excluded here for the same reason (its handshake resets the stream so it
			// never settles), and there is nothing Switch 1 specific about the problem.
			//
			// Doing nothing is strictly better: a hiccuping controller recovers for free on the next read,
			// and one that is genuinely gone still gets dropped by the timeout above and recreated by
			// enumeration -- with a fresh handle and a clean init, which is the path that actually works.
		}
		else
		{
			numTimeOuts = 0;

			if (jc->is_switch2_pro && !bLoggedFirstRead)
			{
				UE_LOG(LogJoyShockLibrary, Log, TEXT("Pro Controller 2 first report received: %d bytes, report id 0x%02X"), res, buf[0]);
				bLoggedFirstRead = true;
			}

			if (jc->is_switch2_pro && !jc->sw2_init_succeeded)
			{
				const auto now = std::chrono::steady_clock::now();
				if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSw2InitRetryTime).count() >= 2000)
				{
					lastSw2InitRetryTime = now;
					if (jc->init_switch2())
					{
						UE_LOG(LogJoyShockLibrary, Log,
							TEXT("SW2: acquired the released command interface and completed calibrated init."));
					}
				}
			}

			// Player indicators are assigned from Unreal's actual player slot, not from HID enumeration order.
			// The game thread stores the semantic one-based player number; this polling thread remains the sole
			// writer to both Nintendo command pipes, keeping LED changes serialised with rumble.
			if (jc->controller_type == ControllerType::n_switch)
			{
				jc->modifying_lock.Lock();
				const int32 wantedPlayerNumber = jc->player_number;
				jc->modifying_lock.Unlock();

				if (wantedPlayerNumber != lastSentPlayerNumber)
				{
					const unsigned char playerLightMask = PlayerNumberToSwitchLedMask(wantedPlayerNumber);
					bool bSent = false;
					if (jc->is_switch2_pro)
					{
						bSent = jc->set_sw2_player_lights(playerLightMask);
					}
					else
					{
						bSent = jc->set_switch_player_lights(playerLightMask);
					}
					jc->note_output_result(JoyShock::OutputFunctionPlayerIndicator, bSent);
					if (bSent)
					{
						UE_LOG(LogJoyShockLibrary, Verbose,
							TEXT("Player indicator device %d -> player %d, mask 0x%02X"),
							jc->intHandle, wantedPlayerNumber, playerLightMask);
						lastSentPlayerNumber = wantedPlayerNumber;
					}
				}
			}

			// The blue HOME light on a right Joy-Con / Pro Controller is a notification channel, not one
			// of Nintendo's four player indicators. Controllers can retain a pulse from firmware or a
			// previous host, so explicitly clear it once after the live input stream is established.
			if (!bHomeLightCleared && jc->controller_type == ControllerType::n_switch
				&& !jc->is_switch2_pro && (jc->left_right == 2 || jc->left_right == 3))
			{
				bHomeLightCleared = jc->clear_switch_home_light();
				jc->note_output_result(JoyShock::OutputFunctionHomeLight, bHomeLightCleared);
				if (bHomeLightCleared)
				{
					UE_LOG(LogJoyShockLibrary, Verbose,
						TEXT("Cleared HOME notification light on device %d"), jc->intHandle);
				}
			}

			// Switch 1 rumble (sole writer, see above): send immediately when the requested values change
			// (including a final stop packet on a change to 0,0), and while active re-send roughly every
			// 4 input reports (~60ms at 66Hz) -- the actuator fades out on its own otherwise, which made a
			// single packet feel like a short, inconsistent blip. The write happens outside modifying_lock
			// so a slow Bluetooth write can never stall the game thread's Jsl* calls.
			if (jc->controller_type == ControllerType::n_switch && !jc->is_switch2_pro)
			{
				jc->modifying_lock.Lock();
				const unsigned char wantedSmallRumble = jc->get_wanted_small_rumble();
				const unsigned char wantedBigRumble = jc->get_wanted_big_rumble();
				jc->modifying_lock.Unlock();

				const bool bRumbleActive = wantedSmallRumble != 0 || wantedBigRumble != 0;
				const bool bRumbleChanged = wantedSmallRumble != lastSentSmallRumble || wantedBigRumble != lastSentBigRumble;
				if (bRumbleChanged || (bRumbleActive && ++rumbleRefreshCounter >= 4))
				{
					rumbleRefreshCounter = 0;
					jc->set_switch_rumble(wantedSmallRumble, wantedBigRumble);
					lastSentSmallRumble = wantedSmallRumble;
					lastSentBigRumble = wantedBigRumble;
				}
			}

			// Switch 2 rumble: its vibration preset is a short one-shot, so to behave like the other
			// controllers (vibrate until (0,0)) it is retriggered continuously while the requested values
			// are non-zero. Time-based (not report-count) because the Switch 2 streams reports much faster
			// than the Switch 1's 66Hz.
			if (jc->is_switch2_pro)
			{
				jc->modifying_lock.Lock();
				const unsigned char wantedSmallRumble = jc->get_wanted_small_rumble();
				const unsigned char wantedBigRumble = jc->get_wanted_big_rumble();
				jc->modifying_lock.Unlock();

				const bool bRumbleActive = wantedSmallRumble != 0 || wantedBigRumble != 0;
				const bool bRumbleChanged = wantedSmallRumble != lastSentSmallRumble || wantedBigRumble != lastSentBigRumble;
				const auto now = std::chrono::steady_clock::now();
				if (bRumbleChanged
					|| (bRumbleActive && std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSw2RumbleTime).count() >= 70))
				{
					jc->set_sw2_rumble(wantedSmallRumble, wantedBigRumble);
					lastSentSmallRumble = wantedSmallRumble;
					lastSentBigRumble = wantedBigRumble;
					lastSw2RumbleTime = now;
				}

				// HID input is visible to both the editor and its multi-process Standalone child, but
				// WinUSB permits only one command-interface owner. Release an inactive lease so the process
				// that is actually playing can acquire it for calibrated init, LEDs and rumble.
				jc->release_sw2_command_interface_if_idle();
			}

			// DualShock 4 / DualSense output (sole writer, see above): unlike the Switch controllers these
			// actuators hold whatever they were last told until told otherwise, so there is nothing to
			// sustain -- one packet per change is enough. Rumble, colour and player LED share one report, so
			// a change to any of them sends a single coalesced write.
			//
			// This has to happen here rather than in the JSL4USet* calls: those run on the game thread, and
			// writing from there meant a blocking HID write while holding modifying_lock -- the very lock
			// this thread takes to parse every input packet.
			if (jc->controller_type == ControllerType::s_ds4 || jc->controller_type == ControllerType::s_ds)
			{
				jc->modifying_lock.Lock();
				const unsigned char wantedSmallRumble = jc->get_wanted_small_rumble();
				const unsigned char wantedBigRumble = jc->get_wanted_big_rumble();
				const unsigned char wantedLedR = jc->led_r;
				const unsigned char wantedLedG = jc->led_g;
				const unsigned char wantedLedB = jc->led_b;
				const int32 wantedPlayerNumber = jc->player_number;
				jc->modifying_lock.Unlock();

				if (wantedSmallRumble != lastSentSmallRumble || wantedBigRumble != lastSentBigRumble
					|| wantedLedR != lastSentLedR || wantedLedG != lastSentLedG || wantedLedB != lastSentLedB
					|| wantedPlayerNumber != lastSentPlayerNumber)
				{
					// Outside modifying_lock: a slow Bluetooth write must not stall the game thread's setters.
					if (jc->controller_type == ControllerType::s_ds4)
					{
						jc->set_ds4_rumble_light(wantedSmallRumble, wantedBigRumble, wantedLedR, wantedLedG, wantedLedB);
					}
					else
					{
						const unsigned char playerLightMask = PlayerNumberToDualSenseLedMask(wantedPlayerNumber);
						jc->set_ds5_rumble_light(wantedSmallRumble, wantedBigRumble, wantedLedR, wantedLedG, wantedLedB,
							playerLightMask);
						if (wantedPlayerNumber != lastSentPlayerNumber)
						{
							UE_LOG(LogJoyShockLibrary, Verbose,
								TEXT("Player indicator device %d -> player %d, DualSense mask 0x%02X"),
								jc->intHandle, wantedPlayerNumber, playerLightMask);
						}
					}

					lastSentSmallRumble = wantedSmallRumble;
					lastSentBigRumble = wantedBigRumble;
					lastSentLedR = wantedLedR;
					lastSentLedG = wantedLedG;
					lastSentLedB = wantedLedB;
					lastSentPlayerNumber = wantedPlayerNumber;
				}
			}

			// we want to be able to do these check-and-calls without fear of interruption by another thread. there could be many threads (as many as connected controllers),
			// and the callback could be time-consuming (up to the user), so we use a readers-writer-lock.
			if (handle_input(jc, buf, res, hasIMU)) { // but the user won't necessarily have a callback at all, so we'll skip the lock altogether in that case
				if (!bReceivedInput)
				{
					// First real input report: only now is this a controller rather than a device that
					// merely answered enumeration, so this is where it gets announced to the engine. See
					// JoyShock::has_delivered_input for why the connect can't be sent at creation time.
					bReceivedInput = true;
					jc->has_delivered_input.store(true);

					std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._callbackLock);
					JSL4UModule.GetOnConnected().ExecuteIfBound(jc->intHandle);
				}
				// accumulate gyro
				FIMUState imuState = jc->get_transformed_imu_state(jc->imu_state);
				jc->push_cumulative_gyro(imuState.gyroX, imuState.gyroY, imuState.gyroZ);
				if (JSL4UModule.GetOnPoll().IsBound() || JSL4UModule.GetOnPollTouch().IsBound())
				{
					std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._callbackLock);
					// FScopeLock Lock(&UJoyShockLibrary::CallbackLock);

					JSL4UModule.GetOnPoll().ExecuteIfBound(jc->intHandle, jc->simple_state, jc->last_simple_state, imuState, jc->get_transformed_imu_state(jc->last_imu_state), jc->delta_time);

					// touchpad will have its own callback so that it doesn't change the existing api
					if (jc->controller_type != ControllerType::n_switch) {
						JSL4UModule.GetOnPollTouch().ExecuteIfBound(jc->intHandle, jc->touch_state, jc->last_touch_state, jc->delta_time);
					}
				}
				// IMU is configured with a verified ack as part of the controller handshake, but Joy-Con
				// firmware can acknowledge the enable and still never start the sensor. When the stream
				// stays IMU-less, toggle subcommand 0x40 off and back on from here -- as bare writes only
				// (see write_subcommand for why that is the one subcommand form this thread may use).
				// Reading a reply here, or re-running the full init handshake, remains forbidden: the
				// former steals input reports, the latter drops the Bluetooth link outright.
				if (!hasIMU)
				{
					consecutiveReportsWithoutIMU++;
					const bool bCanRepairIMU = jc->controller_type == ControllerType::n_switch
						&& !jc->is_switch2_pro;
					if (bCanRepairIMU && imuRepairAttempts < 3)
					{
						if (bImuRepairToggleOffSent)
						{
							// Second half of the toggle, one report later, so the controller had a full
							// input-report interval to process the disable.
							const uint8_t imuOn = 0x01;
							jc->note_output_result(JoyShock::OutputFunctionMotionSensor,
								jc->write_subcommand(0x40, &imuOn, 1));
							bImuRepairToggleOffSent = false;
							imuRepairAttempts++;
						}
						else if (consecutiveReportsWithoutIMU >= 60 + 250 * imuRepairAttempts)
						{
							UE_LOG(LogJoyShockLibrary, Log,
								TEXT("Controller %d (%s): no IMU samples after %d reports; toggling the IMU enable in-stream (attempt %d/3)."),
								jc->intHandle, *jc->name, consecutiveReportsWithoutIMU, imuRepairAttempts + 1);
							const uint8_t imuOff = 0x00;
							jc->note_output_result(JoyShock::OutputFunctionMotionSensor,
								jc->write_subcommand(0x40, &imuOff, 1));
							bImuRepairToggleOffSent = true;
						}
					}
					if (!bLoggedMissingIMU && consecutiveReportsWithoutIMU >= 250)
					{
						UE_LOG(LogJoyShockLibrary, Warning,
							TEXT("Controller %d (%s) is delivering input without IMU samples "
								"(report 0x%02X, %d bytes); preserving the active input stream."),
							jc->intHandle, *jc->name, buf[0], res);
						bLoggedMissingIMU = true;
					}
				}
				else
				{
					if (bLoggedMissingIMU)
					{
						UE_LOG(LogJoyShockLibrary, Log,
							TEXT("Controller %d (%s) resumed IMU samples (report 0x%02X, %d bytes)."),
							jc->intHandle, *jc->name, buf[0], res);
						bLoggedMissingIMU = false;
					}
					consecutiveReportsWithoutIMU = 0;
					imuRepairAttempts = 0;
					bImuRepairToggleOffSent = false;
				}
				
				// A failed output write is ambiguous on its own: an unplugged controller fails its writes
				// too, but its next read returns -1 and is handled above as a disconnect. A write that
				// fails while input keeps arriving means something else owns the controller's output path
				// (Steam Input, most often). Report each function once, on the transition into failure; a
				// later successful write of that function re-arms it.
				{
					const uint8_t failedFunctions = jc->failed_output_functions.load();
					const uint8_t newlyFailed = failedFunctions & ~reportedBlockedFunctions;
					reportedBlockedFunctions = failedFunctions;
					for (uint8_t FunctionIndex = 0; newlyFailed != 0 && FunctionIndex < 4; FunctionIndex++)
					{
						if ((newlyFailed & (1 << FunctionIndex)) == 0)
						{
							continue;
						}
						const EJSL4UControllerFunction Function = static_cast<EJSL4UControllerFunction>(FunctionIndex);
						UE_LOG(LogJoyShockLibrary, Warning,
							TEXT("Controller %d (%s): %s output is being rejected while input still flows -- another application (Steam Input, typically) appears to be holding this controller."),
							jc->intHandle, *jc->name, *UEnum::GetValueAsString(Function));
						std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._callbackLock);
						JSL4UModule.GetOnFunctionBlocked().ExecuteIfBound(jc->intHandle, Function);
					}
				}

				// dualshock 4 bluetooth might need waking up
				if (jc->controller_type == ControllerType::s_ds4 && !jc->is_usb)
				{
					wakeupTimer += jc->delta_time;
					if (wakeupTimer > 30.0f)
					{
						jc->init_ds4_bt();
						wakeupTimer = 0.0f;
					}
				}
			}
		}
	}

	if (jc->cancel_thread)
	{
		UE_LOG(LogJoyShockLibrary, Log, TEXT("\tending cancelled thread\n"));
	}
	else
	{
		UE_LOG(LogJoyShockLibrary, Log, TEXT("\ttiming out thread\n"));
	}

	// remove
	if (jc->remove_on_finish)
	{
		UE_LOG(LogJoyShockLibrary, Log, TEXT("\t\tremoving jc\n"));
		if (!lockedThread)
		{
			JSL4UModule._connectedLock.lock();
			// UJoyShockLibrary::ConnectedLock.Lock();
		}
		_joyshocks.Remove(jc->intHandle);
		_byPath.Remove(jc->path);
		if (!lockedThread)
		{
			JSL4UModule._connectedLock.unlock();
			// UJoyShockLibrary::ConnectedLock.Unlock();
		}

		// Free this device's handle so it can be reused by a future connection (keeps player numbers dense).
		_pathHandleLock.Lock();
		_pathHandle.Remove(jc->path);
		_pathHandleLock.Unlock();
	}

	const int32 intHandle = jc->intHandle;
	// Capture the notify decision before deleting jc -- reading jc->* after delete is a use-after-free.
	// Only devices that were announced as connected (i.e. that delivered input) may report a disconnect;
	// otherwise a phantom that was opened but never worked would produce a disconnect for a controller the
	// engine was never told about.
	const bool bShouldNotifyDisconnect = (jc->remove_on_finish || jc->delete_on_finish) // Don't notify if reused
		&& bReceivedInput;

	// disconnect this device
	if (jc->delete_on_finish)
	{
		UE_LOG(LogJoyShockLibrary, Log, TEXT("\t\tdeleting jc\n"));
		delete jc;
	}

	if (lockedThread)
	{
		JSL4UModule._connectedLock.unlock();
		// UJoyShockLibrary::ConnectedLock.Unlock();
	}

	// notify that we disconnected this device, and say whether or not it was a timeout (if not a timeout, then an explicit disconnect)
	if (bShouldNotifyDisconnect)
	{
		{
			std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._callbackLock);
			// FScopeLock Lock(&UJoyShockLibrary::CallbackLock);
			JSL4UModule.GetOnDisconnected().ExecuteIfBound(intHandle, numTimeOuts >= 10);
		}

		// A controller may have reconnected on the same device path just before we finished cleaning up
		// (within the poll timeout window). Re-scan so it is picked up again -- cheap now that enumeration
		// only creates genuinely new devices and never touches existing ones. The device that has just gone
		// away is often still enumerable for a moment, so this pass tends to recreate it; that recreated
		// device is harmless because it never delivers input and so is never announced.
		JSL4UModule.RequestConnectDevices();
	}
}

void UJoyShockLibrary::JSL4URefreshControllers()
{
	// Hands the scan to the module's background worker rather than enumerating here. Enumeration opens and
	// initialises HID devices, which blocks -- doing it on the calling thread is what makes the legacy
	// JslConnectDevices node freeze the game when a Blueprint calls it.
	FJoyShockLibrary4UnrealModule::GetInstance().RequestConnectDevices();
}

int32 UJoyShockLibrary::JslConnectDevices()
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	// for writing to console:
	//freopen("CONOUT$", "w", stdout);

	// most of the joycon and pro controller stuff here is thanks to mfosse's vjoy feeder
	// Enumerate and print the HID devices on the system
	struct hid_device_info *devs, *cur_dev;

	JSL4UModule._connectedLock.lock();

	hid_init();

	devs = hid_enumerate(0x0, 0x0);
	cur_dev = devs;
	while (cur_dev) {
		bool isSupported = false;
		switch (cur_dev->vendor_id)
		{
		case JOYCON_VENDOR:
			isSupported = cur_dev->product_id == JOYCON_L_BT ||
				cur_dev->product_id == JOYCON_R_BT ||
				cur_dev->product_id == PRO_CONTROLLER ||
				cur_dev->product_id == PRO_CONTROLLER_2 ||
				cur_dev->product_id == JOYCON_CHARGING_GRIP;
			break;
		case DS_VENDOR:
			isSupported = cur_dev->product_id == DS4_USB ||
				cur_dev->product_id == DS4_USB_V2 ||
				cur_dev->product_id == DS4_USB_DONGLE ||
				cur_dev->product_id == DS4_BT ||
				cur_dev->product_id == DS_USB ||
				cur_dev->product_id == DS_USB_V2;
			break;
		case BROOK_DS4_VENDOR:
			isSupported = cur_dev->product_id == BROOK_DS4_USB;
			break;
		default:
			break;
		}
		if (!isSupported)
		{
			cur_dev = cur_dev->next;
			continue;
		}

		const FString path = cur_dev->path;

		// If we're already tracking this device, leave it entirely alone. Its own poll thread is the sole
		// authority on disconnection (hid_read returning -1), so we don't need to reconcile it here.
		// Re-opening / re-initialising an already-tracked device does blocking HID I/O while we hold
		// _connectedLock, and if that device has just been unplugged the I/O hangs -- which stalls every
		// other poll thread's disconnect cleanup (so the other controller never reports a disconnect) and
		// freezes the game thread's Jsl* getters during play. We only ever create genuinely new devices.
		if (_byPath.Contains(path))
		{
			cur_dev = cur_dev->next;
			continue;
		}

		UE_LOG(LogJoyShockLibrary, Log, TEXT("path: %s\n"), *FString(StringCast<TCHAR>(cur_dev->path).Get()));

		hid_device* handle = hid_open_path(cur_dev->path);
		if (handle != nullptr)
		{
			UE_LOG(LogJoyShockLibrary, Log, TEXT("\tcreating new JoyShock\n"));
			JoyShock* jc = new JoyShock(cur_dev, handle, GetUniqueHandle(path), path);
			_joyshocks.Emplace(jc->intHandle, jc);
			_byPath.Emplace(path, jc);
		}

		cur_dev = cur_dev->next;
	}
	hid_free_enumeration(devs);

	// init joyshocks:
	for (TTuple<int32, JoyShock*> pair : _joyshocks)
	{
		JoyShock* jc = pair.Value;
		
		if (jc->initialised)
		{
			continue;
		}

		if (jc->is_switch2_pro) {
			// Send the Switch 2 USB init sequence (captured from Steam) that makes it start streaming.
			jc->init_switch2();
		}
		else if (jc->controller_type == ControllerType::s_ds4) {
			if (!jc->is_usb) {
				jc->init_ds4_bt();
			}
			else {
				jc->init_ds4_usb();
			}
		} // dualsense
		else if (jc->controller_type == ControllerType::s_ds)
		{
			jc->initialised = true;
		} // charging grip
		// init_usb/init_bt don't set this themselves, and it has to be set on success: without it a Switch
		// controller is never considered initialised, so every JslConnectDevices call re-runs init (blocking
		// HID I/O while holding _connectedLock) on every already-connected one -- which freezes the game
		// thread's Jsl* getters during play.
		//
		// It equally has to NOT be set on failure. Marking a failed init as initialised is silent and
		// permanent: init is where vibration and the IMU get switched on, so the controller streams buttons
		// and looks connected while its rumble does nothing and its IMU reports zeroes forever. The `continue`
		// above is the retry -- leaving the flag false is what arms it for the next enumeration, and costs
		// nothing for a controller that initialised fine.
		else if (jc->is_usb) {
			//UE_LOG(LibraryLogJoyShock, Log, TEXT("USB\n"));
			jc->initialised = jc->init_usb();
		}
		else {
			//UE_LOG(LibraryLogJoyShock, Log, TEXT("BT\n"));
			jc->initialised = jc->init_bt();
		}
		// all get time now for polling
		jc->last_polled = std::chrono::steady_clock::now();
		jc->delta_time = 0.0;

		jc->deviceNumber = 0; // left
	}

	for (TTuple<int32, JoyShock*> pair : _joyshocks)
	{
		JoyShock *jc = pair.Value;

		// Only perform HID I/O on devices we're bringing online in this pass (thread == nullptr).
		// This runs on every WM_DEVICECHANGE (including disconnects), and writing to a controller that
		// already has a running poll thread can block on a device that has just been disconnected (e.g.
		// bluetooth turned off) while we hold _connectedLock -- which would stall the poll threads and,
		// during play, the game thread's Jsl* getters, freezing the editor.
		const bool bIsNewDevice = jc->thread == nullptr;

		// DS4 has no separate player indicator, but its RGB light bar and rumble start in the same report.
		// This is safe before its polling thread exists. All later output, including every numeric player
		// indicator, is written by that controller's polling thread.
		if (bIsNewDevice && jc->controller_type == ControllerType::s_ds4)
		{
			jc->set_ds4_rumble_light(0, 0, jc->led_r, jc->led_g, jc->led_b);
		}

		// threads for polling
		if (bIsNewDevice)
		{
			UE_LOG(LogJoyShockLibrary, Log, TEXT("\tstarting new thread\n"));
			jc->thread = new std::thread(pollIndividualLoop, jc);
		}
	}

	const int32 totalDevices = _joyshocks.Num();

	JSL4UModule._connectedLock.unlock();

	// Deliberately no connect notification here. Being enumerable is not the same as being a working
	// controller: a device that has just dropped off Bluetooth lingers in HID enumeration for a moment, and
	// can still be opened and answer the init handshake, so announcing at creation time produced a bogus
	// "input device connected" for a controller that had in fact just disconnected (and, because it was
	// gone again by the time the game thread looked it up, one reported as "Unknown Controller"). Each
	// device announces itself from its own poll thread once it delivers a real input report.

	return totalDevices;
}

int32 UJoyShockLibrary::JslGetConnectedDeviceHandles(TArray<int32>& OutDeviceHandleArray)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	int i = 0;
	JSL4UModule._connectedLock.lock_shared();

	for (TTuple<signed int, JoyShock*> pair : _joyshocks)
	{
		// Skip devices that have not delivered input yet: they are enumerable, but not confirmed to be
		// working controllers, and the connect event for them has not been (and may never be) sent.
		if (pair.Value == nullptr || !pair.Value->has_delivered_input.load())
		{
			continue;
		}
		OutDeviceHandleArray.Add(pair.Key);
		i++;
	}
	JSL4UModule._connectedLock.unlock_shared();
	return i; // return num actually found
}

// --- Joy-Con pairing (Blueprint) ------------------------------------------------------------------------

static EJSL4UControllerType JSL4UControllerTypeFromLegacy(int32 LegacyType)
{
	switch (LegacyType)
	{
	case JS_TYPE_JOYCON_LEFT:    return EJSL4UControllerType::JoyConLeft;
	case JS_TYPE_JOYCON_RIGHT:   return EJSL4UControllerType::JoyConRight;
	case JS_TYPE_PRO_CONTROLLER: return EJSL4UControllerType::ProController;
	case JS_TYPE_PRO_CONTROLLER_2: return EJSL4UControllerType::ProController2;
	case JS_TYPE_DS4:            return EJSL4UControllerType::DualShock4;
	case JS_TYPE_DS:             return EJSL4UControllerType::DualSense;
	default:                     return EJSL4UControllerType::Undefined;
	}
}

// Reads everything the JoyShock itself owns. The caller must hold _connectedLock (shared is enough) and
// must have already null-checked jc. Kept separate from the locking so that callers which already hold
// the lock for a whole batch of devices don't have to re-acquire it per device.
static FJSLSettings JSL4UReadJslSettings(JoyShock* jc)
{
	FJSLSettings settings;

	settings.gyroSpace = jc->gyroSpace;
	settings.playerNumber = jc->player_number;
	settings.splitType = jc->left_right;
	settings.isConnected = true;
	settings.isCalibrating = jc->use_continuous_calibration;
	settings.autoCalibrationEnabled = jc->motion.GetCalibrationMode() != GamepadMotionHelpers::CalibrationMode::Manual;

	switch (jc->controller_type)
	{
	case ControllerType::s_ds4:
		settings.controllerType = JS_TYPE_DS4;
		break;
	case ControllerType::s_ds:
		settings.controllerType = JS_TYPE_DS;
		break;
	default:
	case ControllerType::n_switch:
		settings.controllerType = jc->is_switch2_pro ? JS_TYPE_PRO_CONTROLLER_2 : jc->left_right;
		settings.colour = jc->body_colour;
		break;
	}

	if (jc->controller_type != ControllerType::n_switch)
	{
		// get led colour
		settings.colour = (int)(jc->led_b) | ((int)(jc->led_g) << 8) | ((int)(jc->led_r) << 16);
	}

	return settings;
}

// Builds the Blueprint-facing struct from the raw JSL settings. Leaves the interface-owned fields
// (PlayerIndex / JoinedToDeviceId) at their defaults -- see JSL4UFillPlayerFields.
static FJSL4UControllerInfo JSL4UMakeControllerInfo(int32 DeviceId, const FJSLSettings& JslSettings)
{
	// The colour arrives packed as 0xRRGGBB (both the Switch body colour read over SPI and the DS4/DualSense
	// LED colour are assembled that way), so it has to go into FColor as (R, G, B) -- passing it reversed
	// swapped red and blue, turning a blue Joy-Con red on screen.
	const uint32 RGBColor = JslSettings.colour;
	const uint8 Red = (RGBColor >> 16) & 0xff;
	const uint8 Green = (RGBColor >> 8) & 0xff;
	const uint8 Blue = RGBColor & 0xff;

	FJSL4UControllerInfo Info;
	Info.DeviceId = DeviceId;
	Info.ControllerType = JSL4UControllerTypeFromLegacy(JslSettings.controllerType);
	Info.PlayerLedNumber = JslSettings.playerNumber;
	Info.Color = FColor(Red, Green, Blue);
	Info.bHasMotionSensors = Info.ControllerType != EJSL4UControllerType::Undefined;
	Info.bHasRumble = Info.ControllerType != EJSL4UControllerType::Undefined;
	Info.bHasRgbLight = Info.ControllerType == EJSL4UControllerType::DualShock4
		|| Info.ControllerType == EJSL4UControllerType::DualSense;
	Info.bHasTouchpad = Info.ControllerType == EJSL4UControllerType::DualShock4
		|| Info.ControllerType == EJSL4UControllerType::DualSense;
	Info.bHasPlayerIndicator = Info.ControllerType == EJSL4UControllerType::JoyConLeft
		|| Info.ControllerType == EJSL4UControllerType::JoyConRight
		|| Info.ControllerType == EJSL4UControllerType::ProController
		|| Info.ControllerType == EJSL4UControllerType::ProController2
		|| Info.ControllerType == EJSL4UControllerType::DualSense;
	Info.GyroSpace = JslSettings.gyroSpace;
	Info.SplitType = JslSettings.splitType;
	Info.bIsCalibrating = JslSettings.isCalibrating;
	Info.bAutoCalibrationEnabled = JslSettings.autoCalibrationEnabled;
	Info.bIsConnected = JslSettings.isConnected;
	return Info;
}

// Fills the fields the input-device interface owns. Must NOT be called while holding _connectedLock:
// the game thread takes the interface's lock and then calls Jsl* getters (which take _connectedLock),
// so acquiring them in the opposite order here could deadlock.
static void JSL4UFillPlayerFields(FJSL4UControllerInfo& Info, FJoyShockInterface* Interface)
{
	if (Interface != nullptr)
	{
		Interface->FillControllerInfo(Info);
	}
}

TArray<FJSL4UControllerInfo> UJoyShockLibrary::JSL4UGetConnectedControllers()
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	FJoyShockInterface* Interface = JSL4UModule.GetActiveInterface();

	TArray<FJSL4UControllerInfo> Result;
	{
		// One shared lock for every device, rather than one per device per getter.
		std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);

		Result.Reserve(_joyshocks.Num());
		for (const TTuple<int32, JoyShock*>& Pair : _joyshocks)
		{
			// Same filter as JslGetConnectedDeviceHandles: only controllers that have actually delivered
			// input, so a device still lingering in enumeration after a disconnect never shows up here.
			if (Pair.Value != nullptr && Pair.Value->has_delivered_input.load())
			{
				Result.Add(JSL4UMakeControllerInfo(Pair.Key, JSL4UReadJslSettings(Pair.Value)));
			}
		}
	}

	Result.Sort([](const FJSL4UControllerInfo& A, const FJSL4UControllerInfo& B) { return A.DeviceId < B.DeviceId; });

	for (FJSL4UControllerInfo& Info : Result)
	{
		JSL4UFillPlayerFields(Info, Interface);
	}
	return Result;
}

bool UJoyShockLibrary::JSL4UIsJoinable(EJSL4UControllerType ControllerType)
{
	return ControllerType == EJSL4UControllerType::JoyConLeft
		|| ControllerType == EJSL4UControllerType::JoyConRight;
}

bool UJoyShockLibrary::JSL4UJoinJoyCons(int32 DeviceIdA, int32 DeviceIdB)
{
	if (DeviceIdA == DeviceIdB)
	{
		return false;
	}

	const EJSL4UControllerType TypeA = JSL4UControllerTypeFromLegacy(JslGetControllerType(DeviceIdA));
	const EJSL4UControllerType TypeB = JSL4UControllerTypeFromLegacy(JslGetControllerType(DeviceIdB));

	if (!JSL4UIsJoinable(TypeA) || !JSL4UIsJoinable(TypeB))
	{
		UE_LOG(LogJoyShockLibrary, Warning, TEXT("JSL4UJoinJoyCons: both device ids must be Joy-Cons (got %d and %d)."), DeviceIdA, DeviceIdB);
		return false;
	}

	if (TypeA == TypeB)
	{
		UE_LOG(LogJoyShockLibrary, Warning, TEXT("JSL4UJoinJoyCons: expected one left and one right Joy-Con; both are the same side."));
		return false;
	}

	FJoyShockInterface* Interface = FJoyShockLibrary4UnrealModule::GetInstance().GetActiveInterface();
	return Interface != nullptr && Interface->JoinControllers(DeviceIdA, DeviceIdB);
}

void UJoyShockLibrary::JSL4UUnjoinJoyCon(int32 DeviceId)
{
	if (FJoyShockInterface* Interface = FJoyShockLibrary4UnrealModule::GetInstance().GetActiveInterface())
	{
		Interface->UnjoinController(DeviceId);
	}
}

void UJoyShockLibrary::JSL4UUnjoinAllJoyCons()
{
	if (FJoyShockInterface* Interface = FJoyShockLibrary4UnrealModule::GetInstance().GetActiveInterface())
	{
		Interface->UnjoinAllControllers();
	}
}

bool UJoyShockLibrary::JSL4USetJoyConGripMode(int32 DeviceId, EJSL4UJoyConGripMode GripMode)
{
	if (GripMode == EJSL4UJoyConGripMode::NotApplicable)
	{
		return false;
	}
	FJoyShockInterface* Interface = FJoyShockLibrary4UnrealModule::GetInstance().GetActiveInterface();
	return Interface != nullptr
		&& Interface->SetJoyConHorizontal(DeviceId, GripMode == EJSL4UJoyConGripMode::Horizontal);
}

int32 UJoyShockLibrary::JSL4UGetPlayerIndexOfController(int32 DeviceId)
{
	FJoyShockInterface* Interface = FJoyShockLibrary4UnrealModule::GetInstance().GetActiveInterface();
	return Interface != nullptr ? Interface->GetPlayerIndexForDevice(DeviceId) : INDEX_NONE;
}

bool UJoyShockLibrary::JSL4UAssignControllerToPlayerIndex(int32 DeviceId, int32 PlayerIndex)
{
	FJoyShockInterface* Interface = FJoyShockLibrary4UnrealModule::GetInstance().GetActiveInterface();
	if (Interface == nullptr)
	{
		UE_LOG(LogJoyShockLibrary, Warning,
			TEXT("JSL4UAssignControllerToPlayerIndex: no input interface yet, so device %d was not assigned."), DeviceId);
		return false;
	}

	if (!Interface->SetPlayerIndexForDevice(DeviceId, PlayerIndex))
	{
		// The only way that fails is the handle not being a connected controller, which is worth naming --
		// a stale or wrong device id looks identical to "the assignment didn't work" from Blueprint.
		UE_LOG(LogJoyShockLibrary, Warning,
			TEXT("JSL4UAssignControllerToPlayerIndex: %d is not a connected controller, so it was not assigned to player %d. ")
			TEXT("Device ids come from JSL4UGetConnectedControllers and are not array indices."),
			DeviceId, PlayerIndex);
		return false;
	}

	return true;
}

bool UJoyShockLibrary::JSL4UAssignControllerToPlayer(int32 DeviceId, APlayerController* PlayerController)
{
	if (PlayerController == nullptr)
	{
		UE_LOG(LogJoyShockLibrary, Warning,
			TEXT("JSL4UAssignControllerToPlayer: no player controller given, so device %d was not assigned. ")
			TEXT("Create Local Player returns null when it cannot make another player -- check the viewport's "
			TEXT("MaxSplitscreenPlayers if that is where this came from.")),
			DeviceId);
		return false;
	}

	// Convert through the same IPlatformInputDeviceMapper the slot assignment uses -- see the note on
	// JSL4UGetControllersOfPlayer about why the legacy controller id is the wrong number here.
	const FPlatformUserId User = PlayerController->GetPlatformUserId();
	const int32 UserIndex = IPlatformInputDeviceMapper::Get().GetUserIndexForPlatformUser(User);

	// A PlayerController only has a platform user once its ULocalPlayer has been attached, which does not
	// happen until after the controller has been spawned -- so this is reachable, and it used to be far
	// worse than a plain failure: passing the resulting -1 through to the index call means "hand the
	// controller back to automatic assignment", so the call quietly did the opposite of what was asked and
	// still reported success.
	if (!User.IsValid() || UserIndex < 0)
	{
		UE_LOG(LogJoyShockLibrary, Warning,
			TEXT("JSL4UAssignControllerToPlayer: %s has no platform user yet, so device %d was not assigned. ")
			TEXT("Assign after the player controller has been created and possesses its pawn -- from the pawn's "
			TEXT("Possessed By, for instance, rather than from a Begin Play.")),
			*PlayerController->GetName(), DeviceId);
		return false;
	}

	return JSL4UAssignControllerToPlayerIndex(DeviceId, UserIndex);
}

TArray<FJSL4UControllerInfo> UJoyShockLibrary::JSL4UGetControllersOfPlayerIndex(int32 PlayerIndex)
{
	// Filtering the full list keeps this on the single-pass path in JSL4UGetConnectedControllers rather
	// than adding a second way to read the same state.
	TArray<FJSL4UControllerInfo> Result = JSL4UGetConnectedControllers();
	Result.RemoveAll([PlayerIndex](const FJSL4UControllerInfo& Info)
	{
		return Info.PlayerIndex != PlayerIndex;
	});
	return Result;
}

TArray<FJSL4UControllerInfo> UJoyShockLibrary::JSL4UGetControllersOfPlayer(AController* Controller)
{
	// Accepting the base class lets the reference from a Pawn's Possessed event plug in without a
	// Blueprint-side cast; AI controllers simply own no physical input devices.
	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	if (PlayerController == nullptr)
	{
		return {};
	}

	// RefreshPlayerAssignments maps a device to GetPlatformUserForUserIndex(Slot), so convert back through
	// the same mapper. The player's legacy controller id is a different number and would silently pick the
	// wrong slot for anyone but player 0.
	const int32 UserIndex = IPlatformInputDeviceMapper::Get().GetUserIndexForPlatformUser(PlayerController->GetPlatformUserId());

	// A PlayerController with no Local Player yet (remote net client, or too early in startup) resolves to
	// index -1. Return empty rather than passing -1 through: unassigned devices also carry -1, so the
	// pass-through would hand back controllers that belong to nobody.
	if (UserIndex < 0)
	{
		return {};
	}

	return JSL4UGetControllersOfPlayerIndex(UserIndex);
}

// JslDisconnectAndDisposeAll used to live here. It was exposed to Blueprint, called by nothing, and
// unrecoverable: it unbound the module's connect/disconnect/poll delegates, which only the input
// interface's constructor ever binds, so calling it left every controller dead for the rest of the
// session with no way back short of restarting the editor. Removed rather than given a JSL4U name --
// there is no situation in which a game should be tearing the device layer down underneath itself.

bool UJoyShockLibrary::JSL4UIsControllerConnected(int32 DeviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);

	// Same test the JSL4U listings use, so this can never disagree with JSL4UGetConnectedControllers about
	// whether a controller is there. JslStillConnected below deliberately keeps its looser "is it in the
	// device map at all" answer, which is what existing callers of it expect.
	const JoyShock* jc = GetJoyShockFromHandle(DeviceId);
	return jc != nullptr && jc->has_delivered_input.load();
}

bool UJoyShockLibrary::JslStillConnected(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	return GetJoyShockFromHandle(deviceId) != nullptr;
}

// get buttons as bits in the following order, using North South East West to name face buttons to avoid ambiguity between Xbox and Nintendo layouts:
// 0x00001: up
// 0x00002: down
// 0x00004: left
// 0x00008: right
// 0x00010: plus
// 0x00020: minus
// 0x00040: left stick click
// 0x00080: right stick click
// 0x00100: L
// 0x00200: R
// ZL and ZR are reported as analogue inputs (GetLeftTrigger, GetRightTrigger), because DS4 and XBox controllers use analogue triggers, but we also have them as raw buttons
// 0x00400: ZL
// 0x00800: ZR
// 0x01000: S
// 0x02000: E
// 0x04000: W
// 0x08000: N
// 0x10000: home / PS
// 0x20000: capture / touchpad-click
// 0x40000: SL
// 0x80000: SR
// if you want the whole state, this is the best way to do it
FJoyShockState UJoyShockLibrary::JslGetSimpleState(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->simple_state;
	}
	return {};
}

FJSL4UJoyShockState UJoyShockLibrary::JSL4UGetSimpleState(int32 DeviceId)
{
	const FJoyShockState& LegacySimpleState = JslGetSimpleState(DeviceId);
	return {
		.Buttons = LegacySimpleState.buttons,
		.LeftTrigger = LegacySimpleState.lTrigger,
		.RightTrigger = LegacySimpleState.rTrigger,
		.LeftStick = FVector2D(LegacySimpleState.stickLX, LegacySimpleState.stickLY),
		.RightStick = FVector2D(LegacySimpleState.stickRX, LegacySimpleState.stickRY)
	};
}

FIMUState UJoyShockLibrary::JslGetIMUState(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->get_transformed_imu_state(jc->imu_state);
	}
	return {};
}

FJSL4UIMUState UJoyShockLibrary::JSL4UGetIMUState(int32 DeviceID)
{
	const FIMUState& LegacyIMUState = JslGetIMUState(DeviceID);
	return {
		.Acceleration = FVector(LegacyIMUState.accelZ, LegacyIMUState.accelX, -LegacyIMUState.accelY),
		.Gyro = FVector(-LegacyIMUState.gyroZ, LegacyIMUState.gyroX, -LegacyIMUState.gyroY) // Negate rotation around vertical axis to make up for different handedness 
	};
}

FJSL4UIMUState UJoyShockLibrary::JSL4UGetRawIMUState(int32 DeviceID)
{
	const FIMUState& LegacyIMUState = JslGetIMUState(DeviceID);
	return {
		.Acceleration = FVector(LegacyIMUState.accelX, LegacyIMUState.accelY, LegacyIMUState.accelZ),
		.Gyro = FVector(LegacyIMUState.gyroX, LegacyIMUState.gyroY, LegacyIMUState.gyroZ)
	};

	// Right-handed Y-UP (X-RIGHT/Z-BACK)
	// Raw X = JSL4U Y
	// Raw Y = JSL4U Z
	// Raw Z = JSL4U -X
}

FMotionState UJoyShockLibrary::JslGetMotionState(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->get_motion_state();
	}
	return {};
}

FJSL4UMotionState UJoyShockLibrary::JSL4UGetMotionState(int32 DeviceID)
{
	/* TEMP DEBUG
	FVector Origin = FVector(280.0, 0.0f, 0.0f);
	FWorldContext* WorldContext = GEngine->GetWorldContextFromGameViewport(GEngine->GameViewport);
	UWorld* World = WorldContext->World();
	FVector TempFlattened = GamepadMotionHelpers::Motion::Flattened;
	// FVector TempFlattened = FVector(GamepadMotionHelpers::Motion::FlattenedX, GamepadMotionHelpers::Motion::FlattenedY, GamepadMotionHelpers::Motion::FlattenedZ);
	UKismetSystemLibrary::DrawDebugArrow(World, Origin, Origin + TempFlattened * 200.0f, 2.0f, FColor::Red);
	TEMP DEBUG */
	
	FMotionState NativeMotionState = JslGetMotionState(DeviceID);
	FJSL4UMotionState UnrealMotionState;


	// Works with no gravity correction
	UnrealMotionState.Orientation = FQuat(NativeMotionState.rawQuatZ, -NativeMotionState.rawQuatX, -NativeMotionState.rawQuatY, NativeMotionState.rawQuatW);
	
	// Restore one of the two below once the gravity correction bug has been fixed in GamepadMotion.hpp
	// UnrealMotionState.Orientation = FQuat(-NativeMotionState.quatZ, NativeMotionState.quatX, -NativeMotionState.quatY, NativeMotionState.quatW);
	// UnrealMotionState.Orientation = FQuat(NativeMotionState.quatZ, -NativeMotionState.quatX, NativeMotionState.quatY, -NativeMotionState.quatW);

	UnrealMotionState.Acceleration = FVector(NativeMotionState.accelZ, NativeMotionState.accelX, -NativeMotionState.accelY);
	UnrealMotionState.Gravity = FVector(-NativeMotionState.gravZ, NativeMotionState.gravX, NativeMotionState.gravY);
	return UnrealMotionState;
}

FJSL4UMotionState UJoyShockLibrary::JSL4UGetRawMotionState(int32 DeviceID)
{
	FMotionState NativeMotionState = JslGetMotionState(DeviceID);
	FJSL4UMotionState UnrealMotionState;
	UnrealMotionState.Orientation = FQuat(NativeMotionState.quatX, NativeMotionState.quatY, -NativeMotionState.quatZ, NativeMotionState.quatW);

	UnrealMotionState.Acceleration = FVector(NativeMotionState.accelX, NativeMotionState.accelY, NativeMotionState.accelZ);
	UnrealMotionState.Gravity = FVector(NativeMotionState.gravX, NativeMotionState.gravY, NativeMotionState.gravZ);
	return UnrealMotionState;
}

FTouchState UJoyShockLibrary::JslGetTouchState(int32 deviceId, bool previous)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return previous ? jc->last_touch_state : jc->touch_state;
	}
	return {};
}

FJSL4UTouchState UJoyShockLibrary::JSL4UGetTouchState(int32 DeviceId, bool bPrevious)
{
	const FTouchState& LegacyTouchState = JslGetTouchState(DeviceId, bPrevious);

	return {
		.PrimaryTouch = {
			.TouchID = LegacyTouchState.t0Id,
			.bIsDown = LegacyTouchState.t0Down,
			.Location = FVector2D(LegacyTouchState.t0X, LegacyTouchState.t0Y)
		},
		.SecondaryTouch = {
			.TouchID = LegacyTouchState.t1Id,
			.bIsDown = LegacyTouchState.t1Down,
			.Location = FVector2D(LegacyTouchState.t1X, LegacyTouchState.t1Y)
		}
	};
}

FVector2D UJoyShockLibrary::JSL4UGetTouchpadSize(int32 DeviceId)
{
	int32 SizeX = 0;
	int32 SizeY = 0;
	// Returns zero for a controller with no touchpad, which is also what the legacy call leaves behind.
	JslGetTouchpadDimension(DeviceId, SizeX, SizeY);
	return FVector2D(SizeX, SizeY);
}

bool UJoyShockLibrary::JslGetTouchpadDimension(int32 deviceId, int32 &sizeX, int32 &sizeY)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	// I am assuming a single touchpad (or all touchpads are the same dimension)?
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr)
	{
		switch (jc->controller_type)
		{
		case ControllerType::s_ds4:
		case ControllerType::s_ds:
			sizeX = 1920;
			sizeY = 943;
			break;
		default:
			sizeX = 0;
			sizeY = 0;
			break;
		}
		return true;
	}
	return false;
}

int32 UJoyShockLibrary::JslGetButtons(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->simple_state.buttons;
	}
	return 0;
}

FVector2D UJoyShockLibrary::JSL4UGetLeftStick(int32 DeviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(DeviceId);
	if (jc != nullptr)
	{
		return FVector2D(jc->simple_state.stickLX, jc->simple_state.stickLY);
	}
	return FVector2D::ZeroVector;
}

// get thumbsticks
float UJoyShockLibrary::JslGetLeftX(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->simple_state.stickLX;
	}
	return 0.0f;
}
float UJoyShockLibrary::JslGetLeftY(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->simple_state.stickLY;
	}
	return 0.0f;
}

FVector2D UJoyShockLibrary::JSL4UGetRightStick(int32 DeviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(DeviceId);
	if (jc != nullptr) {
		return FVector2D(jc->simple_state.stickRX, jc->simple_state.stickRY);
	}
	return FVector2D::ZeroVector;
}

float UJoyShockLibrary::JslGetRightX(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->simple_state.stickRX;
	}
	return 0.0f;
}
float UJoyShockLibrary::JslGetRightY(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->simple_state.stickRY;
	}
	return 0.0f;
}

// get triggers. Switch controllers don't have analogue triggers, but will report 0.0 or 1.0 so they can be used in the same way as others
float UJoyShockLibrary::JslGetLeftTrigger(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->simple_state.lTrigger;
	}
	return 0.0f;
}
float UJoyShockLibrary::JslGetRightTrigger(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->simple_state.rTrigger;
	}
	return 0.0f;
}

// get gyro
float UJoyShockLibrary::JslGetGyroX(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->get_transformed_imu_state(jc->imu_state).gyroX;
	}
	return 0.0f;
}
float UJoyShockLibrary::JslGetGyroY(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->get_transformed_imu_state(jc->imu_state).gyroY;
	}
	return 0.0f;
}
float UJoyShockLibrary::JslGetGyroZ(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->get_transformed_imu_state(jc->imu_state).gyroZ;
	}
	return 0.0f;
}

void UJoyShockLibrary::JslGetAndFlushAccumulatedGyro(int32 deviceId, float& gyroX, float& gyroY, float& gyroZ)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		jc->get_and_flush_cumulative_gyro(gyroX, gyroY, gyroZ);
		return;
	}
	gyroX = gyroY = gyroZ = 0.f;
}

FVector UJoyShockLibrary::JSL4UGetAndFlushAccumulatedGyro(int32 InDeviceId)
{
	float GyroX, GyroY, GyroZ;
	JslGetAndFlushAccumulatedGyro(InDeviceId, GyroY, GyroZ, GyroX);
	return FVector(-GyroX, GyroY, -GyroZ);
}

void UJoyShockLibrary::JslSetGyroSpace(int32 deviceId, int32 gyroSpace)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	if (gyroSpace < 0) {
		gyroSpace = 0;
	}
	if (gyroSpace > 2) {
		gyroSpace = 2;
	}
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		jc->modifying_lock.Lock();
		jc->gyroSpace = gyroSpace;
		jc->modifying_lock.Unlock();
	}
}

void UJoyShockLibrary::JSL4USetGyroSpace(int32 InDeviceID, EJSL4UGyroSpace InGyroSpace)
{
	JslSetGyroSpace(InDeviceID, static_cast<int32>(InGyroSpace));
}

// get accelerometer
float UJoyShockLibrary::JslGetAccelX(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->imu_state.accelX;
	}
	return 0.0f;
}
float UJoyShockLibrary::JslGetAccelY(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->imu_state.accelY;
	}
	return 0.0f;
}
float UJoyShockLibrary::JslGetAccelZ(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->imu_state.accelZ;
	}
	return 0.0f;
}

// get touchpad
int32 UJoyShockLibrary::JslGetTouchId(int32 deviceId, bool secondTouch)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		if (!secondTouch) {
			return jc->touch_state.t0Id;
		}
		else {
			return jc->touch_state.t1Id;
		}
	}
	return false;
}
bool UJoyShockLibrary::JslGetTouchDown(int32 deviceId, bool secondTouch)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		if (!secondTouch) {
			return jc->touch_state.t0Down;
		}
		else {
			return jc->touch_state.t1Down;
		}
	}
	return false;
}

FVector2D UJoyShockLibrary::JSL4UGetTouch(int32 DeviceId, bool bSecondTouch)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(DeviceId);
	if (jc != nullptr) {
		if (!bSecondTouch) {
			return FVector2D(jc->touch_state.t0X, jc->touch_state.t0Y);
		}
		else {
			return FVector2D(jc->touch_state.t1X, jc->touch_state.t1Y);
		}
	}
	return FVector2D::ZeroVector;
}

float UJoyShockLibrary::JslGetTouchX(int32 deviceId, bool secondTouch)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		if (!secondTouch) {
			return jc->touch_state.t0X;
		}
		else {
			return jc->touch_state.t1X;
		}
	}
	return 0.0f;
}
float UJoyShockLibrary::JslGetTouchY(int32 deviceId, bool secondTouch)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		if (!secondTouch) {
			return jc->touch_state.t0Y;
		}
		else {
			return jc->touch_state.t1Y;
		}
	}
	return 0.0f;
}

// analog parameters have different resolutions depending on device
float UJoyShockLibrary::JSL4UGetStickStep(int32 DeviceId)
{
	return JslGetStickStep(DeviceId);
}

float UJoyShockLibrary::JSL4UGetTriggerStep(int32 DeviceId)
{
	return JslGetTriggerStep(DeviceId);
}

float UJoyShockLibrary::JSL4UGetPollRate(int32 DeviceId)
{
	return JslGetPollRate(DeviceId);
}

float UJoyShockLibrary::JSL4UGetTimeSinceLastUpdate(int32 DeviceId)
{
	return JslGetTimeSinceLastUpdate(DeviceId);
}

float UJoyShockLibrary::JslGetStickStep(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		if (jc->controller_type != ControllerType::n_switch) {
			return 1.0f / 128.0;
		}
		else {
			if (jc->left_right == 2) // right joycon has no calibration for left stick
			{
				return 1.0f / (jc->stick_cal_x_r[2] - jc->stick_cal_x_r[1]);
			}
			else {
				return 1.0f / (jc->stick_cal_x_l[2] - jc->stick_cal_x_l[1]);
			}
		}
	}
	return 0.0f;
}
float UJoyShockLibrary::JslGetTriggerStep(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->controller_type != ControllerType::n_switch ? 1 / 256.0f : 1.0f;
	}
	return 1.0f;
}
float UJoyShockLibrary::JslGetPollRate(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->controller_type != ControllerType::n_switch ? 250.0f : 66.6667f;
	}
	return 0.0f;
}
float UJoyShockLibrary::JslGetTimeSinceLastUpdate(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		auto time_now = std::chrono::steady_clock::now();
		return (float)(std::chrono::duration_cast<std::chrono::microseconds>(time_now - jc->last_polled).count() / 1000000.0);
	}
	return 0.0f;
}

// calibration
// --- Gyro calibration (JSL4U) ---------------------------------------------------------------------------

// The gyro axis convention JSL4U exposes everywhere else (see JSL4UGetIMUState / JSL4UGetAndFlushAccumulatedGyro):
//   Unreal = (-jslZ, jslX, -jslY)
// The calibration offset lives in the same space as the raw gyro readings, so it is converted the same way
// -- otherwise an offset read back would not line up with the gyro values it is subtracted from.
static FVector JSL4UGyroToUnreal(float JslX, float JslY, float JslZ)
{
	return FVector(-JslZ, JslX, -JslY);
}

// Exact inverse of the above, for handing a value back to the library.
static void JSL4UGyroFromUnreal(const FVector& InVector, float& OutJslX, float& OutJslY, float& OutJslZ)
{
	OutJslX = InVector.Y;
	OutJslY = -InVector.Z;
	OutJslZ = -InVector.X;
}

void UJoyShockLibrary::JSL4USetGyroCalibrationMode(int32 DeviceId, EJSL4UGyroCalibrationMode Mode)
{
	// Automatic is the library's SensorFusion+Stillness pair: it decides for itself when the controller is
	// being held still. Manual leaves it entirely to JSL4UStartGyroCalibration / JSL4UStopGyroCalibration.
	JslSetAutomaticCalibration(DeviceId, Mode == EJSL4UGyroCalibrationMode::Automatic);
}

void UJoyShockLibrary::JSL4UStartGyroCalibration(int32 DeviceId)
{
	JslStartContinuousCalibration(DeviceId);
}

void UJoyShockLibrary::JSL4UStopGyroCalibration(int32 DeviceId)
{
	JslPauseContinuousCalibration(DeviceId);
}

void UJoyShockLibrary::JSL4UResetGyroCalibration(int32 DeviceId)
{
	JslResetContinuousCalibration(DeviceId);
}

FJSL4UGyroCalibrationStatus UJoyShockLibrary::JSL4UGetGyroCalibrationStatus(int32 DeviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);

	FJSL4UGyroCalibrationStatus Status;

	JoyShock* jc = GetJoyShockFromHandle(DeviceId);
	if (jc != nullptr)
	{
		const bool bAutomatic = jc->motion.GetCalibrationMode() != GamepadMotionHelpers::CalibrationMode::Manual;

		Status.Mode = bAutomatic ? EJSL4UGyroCalibrationMode::Automatic : EJSL4UGyroCalibrationMode::Manual;
		Status.Confidence = jc->motion.GetAutoCalibrationConfidence();
		Status.bIsSteady = jc->motion.GetAutoCalibrationIsSteady();
		// use_continuous_calibration is what Start/Stop toggle, so it is the "manual calibration in progress"
		// flag -- which is the one a calibration screen needs, and the one the legacy status struct omitted.
		Status.bIsCalibrating = jc->use_continuous_calibration;
	}

	return Status;
}

FVector UJoyShockLibrary::JSL4UGetGyroCalibrationOffset(int32 DeviceId)
{
	float JslX = 0.f, JslY = 0.f, JslZ = 0.f;
	JslGetCalibrationOffset(DeviceId, JslX, JslY, JslZ);
	return JSL4UGyroToUnreal(JslX, JslY, JslZ);
}

void UJoyShockLibrary::JSL4USetGyroCalibrationOffset(int32 DeviceId, FVector Offset)
{
	float JslX, JslY, JslZ;
	JSL4UGyroFromUnreal(Offset, JslX, JslY, JslZ);
	JslSetCalibrationOffset(DeviceId, JslX, JslY, JslZ);
}

void UJoyShockLibrary::JslResetContinuousCalibration(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		jc->reset_continuous_calibration();
	}
}
void UJoyShockLibrary::JslStartContinuousCalibration(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		jc->use_continuous_calibration = true;
		jc->cue_motion_reset = true;
	}
}
void UJoyShockLibrary::JslPauseContinuousCalibration(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		jc->use_continuous_calibration = false;
	}
}
void UJoyShockLibrary::JslSetAutomaticCalibration(int32 deviceId, bool enabled)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();	
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		jc->modifying_lock.Lock();
		jc->motion.SetCalibrationMode(enabled ? GamepadMotionHelpers::CalibrationMode::SensorFusion | GamepadMotionHelpers::CalibrationMode::Stillness : GamepadMotionHelpers::CalibrationMode::Manual);
		jc->modifying_lock.Unlock();
	}
}
void UJoyShockLibrary::JslGetCalibrationOffset(int32 deviceId, float& xOffset, float& yOffset, float& zOffset)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		// not technically modifying, but also not a simple getter
		jc->modifying_lock.Lock();
		jc->motion.GetCalibrationOffset(xOffset, yOffset, zOffset);
		jc->modifying_lock.Unlock();
	}
}

void UJoyShockLibrary::JslSetCalibrationOffset(int32 deviceId, float xOffset, float yOffset, float zOffset)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		jc->modifying_lock.Lock();
		jc->motion.SetCalibrationOffset(xOffset, yOffset, zOffset, 1);
		jc->modifying_lock.Unlock();
	}
}
FJSLAutoCalibration UJoyShockLibrary::JslGetAutoCalibrationStatus(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		FJSLAutoCalibration calibration;

		calibration.autoCalibrationEnabled = jc->motion.GetCalibrationMode() != GamepadMotionHelpers::CalibrationMode::Manual;
		calibration.confidence = jc->motion.GetAutoCalibrationConfidence();
		calibration.isSteady = jc->motion.GetAutoCalibrationIsSteady();

		return calibration;
	}

	return {};
}

FJSL4UControllerInfo UJoyShockLibrary::JSL4UGetControllerInfoAndSettings(int32 DeviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	FJoyShockInterface* Interface = JSL4UModule.GetActiveInterface();

	FJSL4UControllerInfo Info;
	{
		std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);

		JoyShock* jc = GetJoyShockFromHandle(DeviceId);
		if (jc == nullptr)
		{
			return {}; // bIsConnected stays false
		}
		Info = JSL4UMakeControllerInfo(DeviceId, JSL4UReadJslSettings(jc));
	}

	JSL4UFillPlayerFields(Info, Interface);
	return Info;
}

// super-getter for reading a whole lot of state at once
FJSLSettings UJoyShockLibrary::JslGetControllerInfoAndSettings(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return JSL4UReadJslSettings(jc);
	}
	return {};
}

// what split type of controller is this?
int32 UJoyShockLibrary::JslGetControllerType(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		switch (jc->controller_type)
		{
		case ControllerType::s_ds4:
			return JS_TYPE_DS4;
		case ControllerType::s_ds:
			return JS_TYPE_DS;
		default:
		case ControllerType::n_switch:
			return jc->is_switch2_pro ? JS_TYPE_PRO_CONTROLLER_2 : jc->left_right;
		}
	}
	return 0;
}

// what split type of controller is this?
int32 UJoyShockLibrary::JslGetControllerSplitType(int32 deviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	JoyShock* jc = GetJoyShockFromHandle(deviceId);
	if (jc != nullptr) {
		return jc->left_right;
	}
	return 0;
}

// what colour is the controller (not all controllers support this; those that don't will report white)
FColor UJoyShockLibrary::JslGetControllerColor(int32 InDeviceId)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();
	
	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);
	
	// this just reports body colour. Switch controllers also give buttons colour, and in Pro's case, left and right grips
	JoyShock* jc = GetJoyShockFromHandle(InDeviceId);
	if (jc != nullptr) {
		// body_colour is packed 0xRRGGBB, so it goes into FColor as (R, G, B) -- see the note in
		// JSL4UMakeControllerInfo; this was reversed and reported a blue controller as red.
		uint32 RGBColor = jc->body_colour;
		uint8 Red = (RGBColor >> 16) & 0xff;
		uint8 Green = (RGBColor >> 8) & 0xff;
		uint8 Blue = RGBColor & 0xff;
		return FColor(Red, Green, Blue);
	}
	return FColor::White;
}

void UJoyShockLibrary::JSL4USetLightColor(int32 DeviceId, FColor Color)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);

	JoyShock* jc = GetJoyShockFromHandle(DeviceId);
	if (jc != nullptr && (jc->controller_type == ControllerType::s_ds4 || jc->controller_type == ControllerType::s_ds))
	{
		// Store only -- the polling thread is the sole writer of the output report (see pollIndividualLoop).
		jc->modifying_lock.Lock();
		jc->led_r = Color.R;
		jc->led_g = Color.G;
		jc->led_b = Color.B;
		jc->modifying_lock.Unlock();
	}
}

// set controller light colour (not all controllers have a light whose colour can be set, but that just means nothing will be done when this is called -- no harm)
void UJoyShockLibrary::JslSetLightColor(int32 InDeviceId, FColor InColor)
{
	JSL4USetLightColor(InDeviceId, InColor);
}

// Shared by JSL4USetRumble and the legacy JslSetRumble. Every controller family now works the same way:
// store the requested intensities and let that device's polling thread do the writing. What the poll thread
// then does with them differs (Switch 1 re-sends to fight the actuator's decay, Switch 2 retriggers its
// one-shot preset, DualShock 4 / DualSense send once per change), but no HID write happens on this thread.
static void SetRumbleRaw(int32 DeviceId, int32 SmallRumble, int32 BigRumble)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);

	JoyShock* jc = GetJoyShockFromHandle(DeviceId);
	// Diagnostic (Verbose so per-frame rumble pulses don't spam the log; enable with
	// `Log LogJoyShockLibrary Verbose` to see why a controller isn't vibrating -- wrong id, disconnected...).
	UE_LOG(LogJoyShockLibrary, Verbose, TEXT("SetRumble(device %d, small %d, big %d) -> %s"),
		DeviceId, SmallRumble, BigRumble, jc != nullptr ? *jc->name : TEXT("NO DEVICE WITH THIS ID"));

	if (jc == nullptr)
	{
		return;
	}

	jc->modifying_lock.Lock();
	jc->small_rumble = static_cast<unsigned char>(FMath::Clamp(SmallRumble, 0, 255));
	jc->big_rumble = static_cast<unsigned char>(FMath::Clamp(BigRumble, 0, 255));
	jc->modifying_lock.Unlock();
}

void UJoyShockLibrary::SetForceFeedbackRumble(int32 DeviceId, int32 SmallRumble, int32 BigRumble)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);

	// Deliberately unlogged: Unreal pushes force feedback values every frame, so logging here would bury
	// everything else in the log at Verbose.
	JoyShock* jc = GetJoyShockFromHandle(DeviceId);
	if (jc == nullptr)
	{
		return;
	}

	jc->modifying_lock.Lock();
	jc->ff_small_rumble = static_cast<unsigned char>(FMath::Clamp(SmallRumble, 0, 255));
	jc->ff_big_rumble = static_cast<unsigned char>(FMath::Clamp(BigRumble, 0, 255));
	jc->modifying_lock.Unlock();
}

void UJoyShockLibrary::JSL4USetRumble(int32 DeviceId, float SmallRumble, float BigRumble)
{
	// Normalised 0..1 to match Unreal's force-feedback convention, rather than the raw 0-255 the HID reports
	// carry.
	SetRumbleRaw(DeviceId,
		FMath::RoundToInt(FMath::Clamp(SmallRumble, 0.0f, 1.0f) * 255.0f),
		FMath::RoundToInt(FMath::Clamp(BigRumble, 0.0f, 1.0f) * 255.0f));
}

// set controller rumble
void UJoyShockLibrary::JslSetRumble(int32 deviceId, int32 smallRumble, int32 bigRumble)
{
	SetRumbleRaw(deviceId, smallRumble, bigRumble);
}

void UJoyShockLibrary::JSL4USetPlayerNumber(int32 DeviceId, int32 Number)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);

	JoyShock* jc = GetJoyShockFromHandle(DeviceId);
	if (jc != nullptr && (jc->controller_type == ControllerType::n_switch
		|| jc->controller_type == ControllerType::s_ds))
	{
		// Store the semantic one-based number, never a hardware-specific mask. The polling thread converts
		// it for Switch 1, Switch 2 or DualSense and remains the sole writer to the controller. Apart from
		// avoiding a blocking HID call on the game thread, this prevents the old Switch-1 subcommand from
		// accidentally being sent to a Switch 2 Pro Controller.
		jc->modifying_lock.Lock();
		jc->player_number = Number;
		jc->modifying_lock.Unlock();
	}
}

void UJoyShockLibrary::JSL4UGetSwitchPlayerLedPattern(int32 PlayerNumber,
	bool& bLed1, bool& bLed2, bool& bLed3, bool& bLed4)
{
	const unsigned char Mask = PlayerNumberToSwitchLedMask(PlayerNumber);
	bLed1 = (Mask & 0x01) != 0;
	bLed2 = (Mask & 0x02) != 0;
	bLed3 = (Mask & 0x04) != 0;
	bLed4 = (Mask & 0x08) != 0;
}

// set controller player number indicator (not all controllers have a number indicator which can be set, but that just means nothing will be done when this is called -- no harm)
void UJoyShockLibrary::JslSetPlayerNumber(int32 deviceId, int32 number)
{
	JSL4USetPlayerNumber(deviceId, number);
}
