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
#include "GameFramework/InputDeviceSubsystem.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY(LogJoyShockLibrary)

TMap<int32, JoyShock*> _joyshocks;

TMap<FString, JoyShock*> _byPath;

FCriticalSection _pathHandleLock;

// Which device id each known controller identity currently holds. The identity is the controller's MAC
// where one can be read and its HID path otherwise -- see GetUniqueHandle. Guarded by _pathHandleLock.
TMap<FString, int32> _handleByIdentity;

// How many times in a row a device on this path has been opened, tracked, and then dropped without ever
// delivering an input report. Guarded by _pathHandleLock.
//
// This exists to bound the retry described at PhantomRescanBudget's use site, and is cleared the moment a
// device on the path delivers input, so a controller that works costs nothing here.
TMap<FString, int32> _phantomAttemptsByPath;

// A path that has produced this many consecutive phantoms is left alone until the platform reports a real
// device change. Small on purpose: the point is to cover the handful of passes it takes a controller that is
// mid-reconnect to start streaming, not to poll dead hardware indefinitely.
static constexpr int32 PhantomRescanBudget = 3;

// Reserves this controller's device id. `identity` must be the controller's MAC where one could be read and
// its HID path otherwise: keying on the MAC is what lets a controller that was switched off and back on --
// or unpaired and paired again, which is when Windows hands the same controller a brand new HID path -- come
// back as the device id it had before, instead of as a stranger. Whatever key is used here must be the one
// passed to ReleaseUniqueHandle; devices carry it in JoyShock::handle_identity for exactly that reason.
static int32 GetUniqueHandle(const FString &identity)
{
	_pathHandleLock.Lock();
	int32* iter = _handleByIdentity.Find(identity);

	if (iter != nullptr)
	{
		_pathHandleLock.Unlock();
		return *iter;
	}

	// Assign the lowest handle not currently in use, so a controller that reconnects reuses a freed slot --
	// e.g. players 0 and 1 -- instead of ever-increasing ids (2, 3, ...). Handles are freed when a device is
	// removed (see the poll-thread disconnect cleanup). This handle is also what the input-device mapper
	// uses as the player index, so keeping it dense keeps player numbers stable across reconnects.
	int32 handle = 0;
	for (bool bInUse = true; bInUse; )
	{
		bInUse = false;
		for (const TTuple<FString, int32>& pair : _handleByIdentity)
		{
			if (pair.Value == handle)
			{
				bInUse = true;
				handle++;
				break;
			}
		}
	}

	_handleByIdentity.Emplace(identity, handle);
	_pathHandleLock.Unlock();

	return handle;
}

// Re-asserts a reservation that GetUniqueHandle already granted. Needed because a device only becomes real
// at the end of enumeration, and the previous holder of its identity can finish disappearing -- releasing
// the very entry we were handed -- in between. Without this the id would look free to the next arrival.
static void ReserveUniqueHandle(const FString& identity, int32 handle)
{
	if (identity.IsEmpty())
	{
		return;
	}

	_pathHandleLock.Lock();
	_handleByIdentity.Emplace(identity, handle);
	_pathHandleLock.Unlock();
}

// Returns a device id to the pool. Every path that destroys a JoyShock has to come through here, including
// the ones that throw away a device before it was ever tracked -- a reservation that is never released is a
// device id that can never be handed out again, and that is what makes ids creep upward over a session of
// unplugging and replugging the same controller.
static void ReleaseUniqueHandle(const FString& identity)
{
	if (identity.IsEmpty())
	{
		return;
	}

	_pathHandleLock.Lock();
	_handleByIdentity.Remove(identity);
	_pathHandleLock.Unlock();
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

// Moves this controller onto a different HID path and re-runs whatever init that transport needs.
// Returns false and leaves the controller untouched if the new path cannot be opened, so a failed swap
// is never worse than not attempting one.
//
// Called only from the polling thread: it owns the handle, so the swap needs no synchronisation with a
// reader, and no other thread has to be stopped for it to happen.
static bool SwitchControllerTransport(JoyShock* jc, const FString& NewPath, bool bNewIsUsb)
{
	hid_device* NewHandle = hid_open_path(TCHAR_TO_UTF8(*NewPath));
	if (NewHandle == nullptr)
	{
		return false;
	}

	hid_close(jc->handle);
	jc->handle = NewHandle;
	hid_set_nonblocking(jc->handle, 0);
	jc->is_usb = bNewIsUsb;
	jc->path = NewPath;

	// The two transports speak different init sequences, and the wrong one leaves the controller silent
	// (see the note in enable_IMU about how that failure used to hide).
	if (jc->controller_type == ControllerType::s_ds4)
	{
		// USB needs no init at a swap: a DS4 streams its full report (IMU included) over USB by default,
		// and the light/rumble state init would set is re-sent by the polling loop anyway, because the
		// caller invalidates its last-sent tracking. Skipping it also keeps the swap's blocking-I/O
		// surface as small as possible -- this runs where a wedged exchange costs a controller, and used
		// to cost the whole editor. Bluetooth genuinely needs its init: the feature-report read inside is
		// what switches the controller back into the full 0x11 report mode.
		if (!bNewIsUsb)
		{
			jc->init_ds4_bt();
		}
	}
	else if (jc->controller_type == ControllerType::n_switch && !jc->is_switch2_pro)
	{
		bNewIsUsb ? jc->init_usb() : jc->init_bt();
	}
	// The DualSense needs no explicit init for either transport, matching JslConnectDevices.

	return true;
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

	// Rumble: this thread is the SOLE writer of rumble packets/commands (JSL4USetControllerRumble only stores the
	// values). Switch 1 rumble decays shortly after a single 0x10 packet, and the Switch 2 only has a short
	// one-shot preset, so while active the state is re-sent/retriggered continuously; a change to (0,0)
	// sends one final stop.
	int rumbleRefreshCounter = 0;
	unsigned char lastSentSmallRumble = 0;
	unsigned char lastSentBigRumble = 0;
	auto lastSw2RumbleTime = std::chrono::steady_clock::now();
	auto lastSw2InitRetryTime = std::chrono::steady_clock::now();
	// Which JSL4USetHomeLight call this thread has already acted on. Generation 0 is "no call yet", and the
	// counter only ever increases, so the first game-set value always sends. Tracking the call rather than
	// the value is what makes re-sending an unchanged intensity work -- see home_light_generation.
	uint32 lastSentHomeLightGeneration = 0;

	// DualShock 4 / DualSense: rumble, light colour and the player LED all ride in the SAME output report,
	// so they are tracked together and sent as one packet whenever any of them changes. Seeded from what
	// JslConnectDevices already sent when it brought this device online, so we don't re-send it immediately.
	unsigned char lastSentLedR = jc->led_r;
	unsigned char lastSentLedG = jc->led_g;
	unsigned char lastSentLedB = jc->led_b;
	int32 lastSentPlayerNumber = jc->player_number;
	bool bHomeLightCleared = false;

	while (!jc->cancel_thread) {
		// get input:
		unsigned char buf[64];
		memset(buf, 0, 64);

		// Enumeration found this same physical controller on a better transport (the cable). Adopt it here
		// rather than there, so the swap happens on the thread that owns the handle.
		{
			jc->modifying_lock.Lock();
			const FString PendingPath = jc->pending_transport_path;
			jc->pending_transport_path.Reset();
			jc->modifying_lock.Unlock();

			if (!PendingPath.IsEmpty())
			{
				const FString PreviousPath = jc->path;
				// Deliberately NOT under _connectedLock. The swap's init performs blocking HID I/O, and on
				// a freshly replugged device that I/O can block until the cable is pulled again -- holding
				// the exclusive lock through it froze every Jsl* getter on the game thread, i.e. the whole
				// editor, on the second plug of a session. The lock is not needed for handle safety any
				// more: enumeration does no I/O on a tracked device's handle (every family marks
				// initialised now), and this thread is the handle's only user. Worst case an init that
				// wedges costs this one controller until its read fails, which the fallback below already
				// handles -- degradation instead of a frozen editor.
				const bool bSwitched = SwitchControllerTransport(jc, PendingPath, true);
				if (bSwitched)
				{
					jc->modifying_lock.Lock();
					jc->fallback_path = PreviousPath;
					jc->modifying_lock.Unlock();
					UE_LOG(LogJoyShockLibrary, Log,
						TEXT("Controller %d (%s) switched to its USB connection; the Bluetooth path is kept as a fallback."),
						jc->intHandle, *jc->name);
					// Rumble/LED state is re-sent by the tracking below, since the values it last sent went
					// to a handle that is now closed.
					lastSentSmallRumble = 0;
					lastSentBigRumble = 0;
					lastSentPlayerNumber = -1;
					bHomeLightCleared = false;
					// Generation 0 means "nothing sent on this handle yet", so a game-owned light is
					// re-asserted once on the new transport and an unclaimed one falls to the clear below.
					lastSentHomeLightGeneration = 0;
					continue;
				}
				// Unmap the path we never managed to adopt: enumeration registered it as ours when queueing
				// the swap, and leaving it there would make every replug look "already tracked" and never
				// retry. This brief map-only mutation is what the lock is actually for.
				JSL4UModule._connectedLock.lock();
				_byPath.Remove(PendingPath);
				JSL4UModule._connectedLock.unlock();
				UE_LOG(LogJoyShockLibrary, Warning,
					TEXT("Controller %d (%s): could not open its USB connection; staying on Bluetooth."),
					jc->intHandle, *jc->name);
			}
		}

		// 10 seconds of no signal means forget this controller
		int reuseCounter = jc->reuse_counter;
		int res = hid_read_timeout(jc->handle, buf, 64, 1000);

		if (res == -1)
		{
			// The cable was pulled on a controller that is still paired over Bluetooth. That is not a
			// disconnect -- the controller never left -- so return to the radio instead of telling the game
			// its controller is gone, which would cost the player their pawn for the sake of unplugging.
			jc->modifying_lock.Lock();
			const FString FallbackPath = jc->fallback_path;
			jc->modifying_lock.Unlock();

			if (!FallbackPath.IsEmpty())
			{
				const FString AbandonedPath = jc->path;
				// Lock-free for the same reason as the swap onto the cable above: the init inside can
				// block, and only map mutations need the lock.
				const bool bSwitched = SwitchControllerTransport(jc, FallbackPath, false);
				if (bSwitched)
				{
					// Drop the cable's path so replugging is seen as a new transport rather than as
					// something already tracked.
					JSL4UModule._connectedLock.lock();
					_byPath.Remove(AbandonedPath);
					JSL4UModule._connectedLock.unlock();

					jc->modifying_lock.Lock();
					jc->fallback_path.Reset();
					jc->modifying_lock.Unlock();

					UE_LOG(LogJoyShockLibrary, Log,
						TEXT("Controller %d (%s) lost its USB connection and returned to Bluetooth."),
						jc->intHandle, *jc->name);
					lastSentSmallRumble = 0;
					lastSentBigRumble = 0;
					lastSentPlayerNumber = -1;
					bHomeLightCleared = false;
					lastSentHomeLightGeneration = 0;
					numTimeOuts = 0;
					continue;
				}
				// Both transports are gone: this really is a disconnect, so fall through.
				jc->modifying_lock.Lock();
				jc->fallback_path.Reset();
				jc->modifying_lock.Unlock();
			}

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
			// previous host, so clear it once the live input stream is established -- and then keep
			// re-asserting it.
			//
			// Clearing it only once was not enough: the firmware owns this light and turns it back on by
			// itself (a reconnect or a battery notification is enough), after which nothing here ever
			// switched it off again and it stayed lit for the rest of the session. It is cheap to repeat
			// next to a 60Hz input stream, and the write-only path means it cannot disturb that stream.
			if (jc->controller_type == ControllerType::n_switch
				&& !jc->is_switch2_pro && (jc->left_right == 2 || jc->left_right == 3))
			{
				if (jc->home_light_owned_by_game.load())
				{
					// A game has taken the light over (JSL4USetHomeLight). Its value is authoritative from
					// then on, and the keep-it-off upkeep below stops -- otherwise the two would fight and
					// the game's light would last at most one upkeep interval.
					//
					// Sent once per call, not once per change: the firmware turns this light back on behind
					// the plugin's back, so a cached intensity is not evidence of what the light is actually
					// doing, and "set it to what I already asked for" is a legitimate request. A failed
					// write leaves the generation unrecorded, which retries on the next report.
					const uint32 wantedGeneration = jc->home_light_generation.load();
					if (wantedGeneration != lastSentHomeLightGeneration)
					{
						const unsigned char wantedHomeLight = jc->wanted_home_light.load();
						const bool bSent = jc->set_switch_home_light(wantedHomeLight);
						jc->note_output_result(JoyShock::OutputFunctionHomeLight, bSent);
						if (bSent)
						{
							lastSentHomeLightGeneration = wantedGeneration;
							UE_LOG(LogJoyShockLibrary, Verbose,
								TEXT("HOME light on device %d set to intensity %d by the game"),
								jc->intHandle, wantedHomeLight);
						}
					}
				}
				else if (!bHomeLightCleared)
				{
					// Cleared exactly once per connection, never on a timer.
					//
					// This was briefly re-asserted every few seconds, because the firmware can switch the
					// light back on by itself. That cost far more than it bought: subcommand 0x38 goes only
					// to a right Joy-Con or Pro Controller, and those started dropping off Bluetooth after
					// a while -- the very hazard the IMU note above describes, that configuration
					// subcommands disturb an established Bluetooth stream. A notification light that may
					// come back on is a blemish; a controller that disconnects mid-game is a broken game.
					//
					// The flag is reset after a transport switch, so a swapped controller is cleared again.
					const bool bSent = jc->clear_switch_home_light();
					jc->note_output_result(JoyShock::OutputFunctionHomeLight, bSent);
					if (bSent)
					{
						bHomeLightCleared = true;
						UE_LOG(LogJoyShockLibrary, Verbose,
							TEXT("Cleared HOME notification light on device %d"), jc->intHandle);
					}
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

					// This path produces working controllers, so it owes nothing to the phantom budget.
					_pathHandleLock.Lock();
					_phantomAttemptsByPath.Remove(jc->path);
					_pathHandleLock.Unlock();

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

				// Deliberately no periodic "wake up" re-init for a Bluetooth DualShock 4.
				//
				// This used to call init_ds4_bt() every 30 seconds. That is the same mistake the timeout
				// branch and the HOME light upkeep above each had to unlearn: a configuration exchange
				// disturbs an established Bluetooth stream, and it does so most readily when the radio is
				// already busy -- which is exactly what several Joy-Cons on the same adapter make it. The
				// controller then goes quiet, ten one-second reads time out, and a working controller is
				// dropped as "disconnected" while it is sitting there streaming.
				//
				// It was also worse than a no-op even when it worked. init_ds4_bt writes a full output
				// report with rumble and colour zeroed, so every thirty seconds the light bar went black
				// and stayed black: the output tracking below still believed its own last-sent colour, so
				// nothing re-sent it until the game happened to change it.
				//
				// Nothing needs the repeat. The report mode init_ds4_bt selects is a property of the
				// connection, so it survives for as long as the connection does, and a connection that has
				// genuinely gone is handled by the read failing -- which is the path that reconnects
				// cleanly. A controller that is quiet is still present (hid_read returns 0, not -1) and
				// recovers for free on its next report.
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
		// A controller that was switched to its cable is mapped under both paths, so the idle one has to go
		// too -- otherwise it would keep a dead device "tracked" and block that path from ever reconnecting.
		if (!jc->fallback_path.IsEmpty())
		{
			_byPath.Remove(jc->fallback_path);
		}
		if (!lockedThread)
		{
			JSL4UModule._connectedLock.unlock();
			// UJoyShockLibrary::ConnectedLock.Unlock();
		}

		// Free this device's handle so it can be reused by a future connection (keeps player numbers dense).
		ReleaseUniqueHandle(jc->handle_identity);
	}

	const int32 intHandle = jc->intHandle;
	// Capture the notify decision before deleting jc -- reading jc->* after delete is a use-after-free.
	// Only devices that were announced as connected (i.e. that delivered input) may report a disconnect;
	// otherwise a phantom that was opened but never worked would produce a disconnect for a controller the
	// engine was never told about.
	const bool bShouldNotifyDisconnect = (jc->remove_on_finish || jc->delete_on_finish) // Don't notify if reused
		&& bReceivedInput;

	// Whether this pass should ask for another scan even though it has nothing to announce -- i.e. whether
	// this was a phantom, and the path has budget left.
	//
	// A phantom holds its path in _byPath for the full ten seconds it takes to time out, and enumeration
	// skips tracked paths, so for that whole window the real controller behind that path is invisible. Once
	// the phantom is gone the path is free again -- but the old code notified nothing and scanned nothing for
	// a device that never delivered input, so the plugin simply stopped looking. The controller then stayed
	// missing until something else happened to produce a WM_DEVICECHANGE, which is why it appeared to come
	// back only after unrelated poking around in the editor.
	//
	// Rescanning unconditionally is not the answer either: for hardware that lingers in enumeration while
	// switched off, that is an endless recreate/timeout cycle. Hence the budget -- enough passes to cover a
	// controller that is mid-reconnect, then quiet.
	bool bShouldRescanForPhantom = false;
	if (!bReceivedInput && (jc->remove_on_finish || jc->delete_on_finish))
	{
		_pathHandleLock.Lock();
		int32& Attempts = _phantomAttemptsByPath.FindOrAdd(jc->path);
		bShouldRescanForPhantom = ++Attempts <= PhantomRescanBudget;
		_pathHandleLock.Unlock();
	}

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
	else if (bShouldRescanForPhantom)
	{
		UE_LOG(LogJoyShockLibrary, Verbose,
			TEXT("Device %d never delivered input; re-scanning in case a working controller is behind that path."),
			intHandle);
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

// Reads the MAC address that identifies this physical controller, independently of how it is attached.
//
// Bluetooth hands it over as the HID serial number. USB usually leaves that blank, so it has to be asked
// for with a vendor feature report -- which is the case that matters here, because a USB cable plugged
// into an already-paired controller is exactly when a phantom second device appears.
//
// Returns an empty string when the address cannot be determined; callers must treat that as "cannot tell"
// and let the device through, never as a match. Wrongly merging two real controllers into one would be a
// far worse failure than the duplicate this exists to prevent.
static FString ReadControllerMacAddress(hid_device* InHandle, const hid_device_info* dev)
{
	if (dev->serial_number != nullptr && dev->serial_number[0] != L'\0')
	{
		FString Serial(dev->serial_number);
		Serial.ReplaceInline(TEXT(":"), TEXT(""));
		Serial.ReplaceInline(TEXT("-"), TEXT(""));
		return Serial.ToLower();
	}

	// Only the PlayStation controllers have a known report for this. A Nintendo controller without a
	// serial number simply goes unmatched, which is the safe direction.
	if (dev->vendor_id != DS_VENDOR)
	{
		return FString();
	}

	const bool bIsDualSense = dev->product_id == DS_USB || dev->product_id == DS_USB_V2;
	unsigned char featureBuf[64];
	memset(featureBuf, 0, sizeof(featureBuf));
	featureBuf[0] = bIsDualSense ? 0x09 : 0x12;
	const int res = hid_get_feature_report(InHandle, featureBuf, sizeof(featureBuf));
	if (res < 7)
	{
		return FString();
	}

	// Bytes 1-6 carry the controller's own address, least significant byte first.
	return FString::Printf(TEXT("%02x%02x%02x%02x%02x%02x"),
		featureBuf[6], featureBuf[5], featureBuf[4], featureBuf[3], featureBuf[2], featureBuf[1]);
}

int32 UJoyShockLibrary::JslConnectDevices()
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	// for writing to console:
	//freopen("CONOUT$", "w", stdout);

	// most of the joycon and pro controller stuff here is thanks to mfosse's vjoy feeder
	// Enumerate and print the HID devices on the system
	struct hid_device_info *devs, *cur_dev;

	// Discovery is split into three phases so that NO blocking HID I/O ever happens while holding
	// _connectedLock. The game thread's Jsl* getters take that lock shared, so any wedged I/O under it --
	// hid_get_feature_report has no timeout, and a freshly replugged device can leave one hanging until
	// the cable is pulled -- freezes the whole editor, which is exactly what repeated DS4 replugs did.
	//
	// Phase 1 snapshots the tracked paths/identities under the lock; phase 2 does every open, identity
	// read and constructor probe lock-free; phase 3 retakes the lock and merges, re-validating everything
	// against the live maps, because poll threads may have disconnected or transport-switched devices
	// while phase 2 was blocked on hardware.

	hid_init();

	// Phase 1: snapshot.
	TSet<FString> TrackedPaths;
	struct FTrackedIdentity { int32 DeviceId; bool bIsUsb; };
	TMap<FString, FTrackedIdentity> TrackedByMac;

	JSL4UModule._connectedLock.lock();
	for (const TTuple<FString, JoyShock*>& Pair : _byPath)
	{
		TrackedPaths.Add(Pair.Key);
	}
	for (const TTuple<int32, JoyShock*>& Pair : _joyshocks)
	{
		if (Pair.Value != nullptr && !Pair.Value->mac_address.IsEmpty())
		{
			TrackedByMac.Add(Pair.Value->mac_address, { Pair.Key, Pair.Value->is_usb });
		}
	}
	JSL4UModule._connectedLock.unlock();

	// Phase 2: all the blocking work, without the lock.
	//
	// One physical controller can appear as two HID devices: plugging a USB cable into a controller that
	// is already paired over Bluetooth does not end the Bluetooth link, it adds a second path. Both stream
	// input, so without the MAC comparison the game gains a phantom controller -- taking a player slot,
	// and in a game that spawns per controller, an extra character -- for what the player experiences as
	// simply putting their controller on charge. A cable for an already-tracked controller is instead
	// recorded as a transport upgrade and handed to that device's own polling thread.
	struct FTransportUpgrade { FString Mac; FString Path; };
	TArray<FTransportUpgrade> TransportUpgrades;
	TArray<JoyShock*> NewDevices;
	// MACs already claimed by a device created in THIS pass, so one controller reachable two ways cannot be
	// built twice before either path is tracked. See its use below.
	TSet<FString> NewMacsThisPass;

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
		// (Snapshot-based: a path that becomes tracked during this pass is caught again in phase 3.)
		if (TrackedPaths.Contains(path))
		{
			cur_dev = cur_dev->next;
			continue;
		}

		UE_LOG(LogJoyShockLibrary, Log, TEXT("path: %s\n"), *FString(StringCast<TCHAR>(cur_dev->path).Get()));

		hid_device* handle = hid_open_path(cur_dev->path);
		if (handle != nullptr)
		{
			const FString DeviceMac = ReadControllerMacAddress(handle, cur_dev);
			const FTrackedIdentity* Existing = DeviceMac.IsEmpty() ? nullptr : TrackedByMac.Find(DeviceMac);
			if (Existing != nullptr)
			{
				// The existing connection wins: nothing about the player's controller should change just
				// because they put it on charge. Only a cable for a Bluetooth-tracked controller upgrades.
				const bool bIsUsbPath = !IsBluetoothHidPath(path);
				if (bIsUsbPath && !Existing->bIsUsb)
				{
					TransportUpgrades.Add({ DeviceMac, path });
					UE_LOG(LogJoyShockLibrary, Log,
						TEXT("\tdevice at %s is device %d (MAC %s) on its cable; handing it over for a transport switch\n"),
						*path, Existing->DeviceId, *DeviceMac);
				}
				else
				{
					UE_LOG(LogJoyShockLibrary, Log,
						TEXT("\tdevice at %s is the same physical controller as device %d (MAC %s); keeping the existing connection\n"),
						*path, Existing->DeviceId, *DeviceMac);
				}
				// Either way this pass does not own the handle: the polling thread opens the path itself.
				hid_close(handle);
			}
			else if (!DeviceMac.IsEmpty() && NewMacsThisPass.Contains(DeviceMac))
			{
				// Two untracked paths for one physical controller in a single pass -- the first scan of a
				// session where a controller is both paired over Bluetooth and sitting on its cable. The
				// MAC check above only catches duplicates of an ALREADY tracked device, and since device ids
				// are keyed by MAC now, letting this build a second JoyShock would hand two live devices the
				// same id and lose one of them inside _joyshocks. The first path wins; the next enumeration
				// sees this one as a duplicate of a tracked device and routes it through the transport
				// upgrade above, which is where the Bluetooth/cable choice actually belongs.
				UE_LOG(LogJoyShockLibrary, Log,
					TEXT("\tdevice at %s is the same physical controller (MAC %s) as another path found in this scan; ignoring it for now\n"),
					*path, *DeviceMac);
				hid_close(handle);
			}
			else
			{
				// The constructor runs its transport probe (blocking feature-report I/O on PlayStation
				// controllers) -- another reason this phase must be lock-free.
				UE_LOG(LogJoyShockLibrary, Log, TEXT("\tcreating new JoyShock\n"));
				// Prefer the MAC as the id key so a controller that is switched off and back on, or
				// re-paired onto a fresh HID path, comes back as the device id it had before.
				const FString Identity = DeviceMac.IsEmpty() ? path : DeviceMac;
				JoyShock* jc = new JoyShock(cur_dev, handle, GetUniqueHandle(Identity), path);
				jc->mac_address = DeviceMac;
				jc->handle_identity = Identity;
				NewDevices.Add(jc);
				if (!DeviceMac.IsEmpty())
				{
					NewMacsThisPass.Add(DeviceMac);
				}
			}
		}

		cur_dev = cur_dev->next;
	}
	hid_free_enumeration(devs);

	// Phase 3: merge under the lock, re-validating every decision against the live maps.
	JSL4UModule._connectedLock.lock();

	for (const FTransportUpgrade& Upgrade : TransportUpgrades)
	{
		JoyShock* ExistingDevice = nullptr;
		for (const TTuple<int32, JoyShock*>& Pair : _joyshocks)
		{
			if (Pair.Value != nullptr && Pair.Value->mac_address == Upgrade.Mac)
			{
				ExistingDevice = Pair.Value;
				break;
			}
		}
		// Stale if the controller disconnected during phase 2, already moved onto a cable, or the path got
		// claimed meanwhile.
		if (ExistingDevice == nullptr || ExistingDevice->is_usb || _byPath.Contains(Upgrade.Path))
		{
			continue;
		}
		ExistingDevice->modifying_lock.Lock();
		ExistingDevice->pending_transport_path = Upgrade.Path;
		ExistingDevice->modifying_lock.Unlock();
		_byPath.Emplace(Upgrade.Path, ExistingDevice);
	}

	for (JoyShock* jc : NewDevices)
	{
		// The path may have been claimed while phase 2 ran (a transport switch, or a concurrent caller of
		// this function). The live device wins over our fresh handle.
		//
		// So may the device id. Ids are keyed by controller identity, so a device that finished
		// disappearing between phase 1 and phase 2 hands its id straight back to the controller behind it,
		// which is the whole point -- but if it has NOT finished, that id is still in use and this device
		// cannot have it. Better to drop this one and pick the controller up on the next scan, which the
		// departing device asks for anyway, than to overwrite a live entry in _joyshocks.
		if (_byPath.Contains(jc->path) || _joyshocks.Contains(jc->intHandle))
		{
			// Give back the device id phase 2 reserved for it. Dropping it here without releasing it is
			// what used to burn an id per lost race, permanently, so a session of reconnects walked the
			// numbers upward with nothing ever occupying the ones it skipped.
			//
			// Only when nothing live holds that id, though. Whoever won the race is very often the same
			// physical controller (a transport switch, or another scan that got there first), and since ids
			// are keyed by identity our reservation is then literally the winner's own -- releasing it would
			// leave a connected controller's id unclaimed and free to be handed to the next one along.
			// (Safe to take the handle lock under _connectedLock: nothing takes them the other way round.)
			const FString Identity = jc->handle_identity;
			const int32 DiscardedHandle = jc->intHandle;
			delete jc;
			if (!_joyshocks.Contains(DiscardedHandle))
			{
				ReleaseUniqueHandle(Identity);
			}
			continue;
		}
		// The previous holder of this identity may have released the reservation on its way out after phase 2
		// read it, so put it back before the device goes live -- otherwise the id reads as free and the next
		// controller to arrive is given the one this device is already using.
		ReserveUniqueHandle(jc->handle_identity, jc->intHandle);
		_joyshocks.Emplace(jc->intHandle, jc);
		_byPath.Emplace(jc->path, jc);
	}

	// Collect the devices this pass has to bring online, then release the lock before touching any of them.
	//
	// Init is blocking HID I/O -- and unbounded, not merely slow: hidapi's Windows hid_write waits on its
	// overlapped result with no timeout, so a controller that never completes the write (which is what a
	// freshly replugged one does) never returns. Under the lock that is an editor hang lasting until the
	// cable is pulled, which is precisely the USB replug freeze. Working from a snapshot is what makes
	// dropping the lock safe: every device in it has no polling thread yet, and only a polling thread ever
	// frees a device, so none of these pointers can go away while we use them.
	TArray<JoyShock*> DevicesToBringOnline;
	for (const TTuple<int32, JoyShock*>& Pair : _joyshocks)
	{
		if (Pair.Value != nullptr && Pair.Value->thread == nullptr)
		{
			DevicesToBringOnline.Add(Pair.Value);
		}
	}
	const int32 totalDevices = _joyshocks.Num();

	JSL4UModule._connectedLock.unlock();

	// init joyshocks:
	for (JoyShock* jc : DevicesToBringOnline)
	{
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
			// Mark it done, for the reason spelled out for the Switch controllers below: an already-
			// connected controller must never be re-initialised from here. The DS4 was the one family left
			// out of that, so every device change re-ran a blocking HID exchange on a controller whose
			// polling thread was using the very same handle -- while holding _connectedLock.
			//
			// That was survivable only while nothing else touched the handle. Once a controller could move
			// between transports, the polling thread would close that handle mid-exchange, the enumeration
			// thread's read would never return, and the lock it holds would freeze every Jsl* getter on the
			// game thread -- an editor hang lasting until the controller was physically unplugged.
			jc->initialised = true;
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

	// Same snapshot, still lock-free: every device here is one this pass is bringing online, so it has no
	// polling thread and nothing else can be writing to it.
	for (JoyShock* jc : DevicesToBringOnline)
	{
		// DS4 has no separate player indicator, but its RGB light bar and rumble start in the same report.
		// This is safe before its polling thread exists. All later output, including every numeric player
		// indicator, is written by that controller's polling thread.
		if (jc->controller_type == ControllerType::s_ds4)
		{
			jc->set_ds4_rumble_light(0, 0, jc->led_r, jc->led_g, jc->led_b);
		}

		// threads for polling. From here the device is owned by its polling thread, so this is the last
		// point at which this function may touch it.
		UE_LOG(LogJoyShockLibrary, Log, TEXT("\tstarting new thread\n"));
		jc->thread = new std::thread(pollIndividualLoop, jc);
	}

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
static FJSL4UControllerInfo JSL4UMakeControllerInfo(int32 DeviceId, const FJSLSettings& JslSettings,
	const JoyShock* jc)
{
	// The colour arrives packed as 0xRRGGBB (both the Switch body colour read over SPI and the DS4/DualSense
	// LED colour are assembled that way), so it has to go into FColor as (R, G, B) -- passing it reversed
	// swapped red and blue, turning a blue Joy-Con red on screen.
	const uint32 RGBColor = JslSettings.colour;
	const uint8 Red = (RGBColor >> 16) & 0xff;
	const uint8 Green = (RGBColor >> 8) & 0xff;
	const uint8 Blue = RGBColor & 0xff;

	FJSL4UControllerInfo Info;
	// Everything built here is, by definition, a controller this plugin drives. The flag exists for the one
	// node that also reports controllers it does not -- see FJSL4UControllerInfo::bIsJoyShockController.
	Info.bIsJoyShockController = true;
	Info.DeviceId = DeviceId;
	Info.ControllerType = JSL4UControllerTypeFromLegacy(JslSettings.controllerType);
	Info.PlayerLedNumber = JslSettings.playerNumber;

	if (jc != nullptr)
	{
		// The polling thread stores 0-4 (or BatteryLevelUnknown); the Blueprint enum reserves 0 for
		// Unknown, so a reported level shifts up by one. Anything unexpected reads as Unknown rather than
		// as a low battery -- a wrong "flat controller" warning is worse than no warning.
		const uint8 RawLevel = jc->battery_level.load();
		Info.BatteryLevel = RawLevel <= 4
			? static_cast<EJSL4UBatteryLevel>(RawLevel + 1)
			: EJSL4UBatteryLevel::Unknown;
		Info.bIsCharging = jc->battery_charging.load();
		Info.BatteryPercent = jc->battery_percent.load();
	}
	Info.Color = FColor(Red, Green, Blue);
	// Every family this plugin drives has both; Undefined means one it drives but could not identify, and
	// there is nothing to promise about a controller we cannot name. XInputController is excluded
	// explicitly even though nothing built here can be one: this builder is for controllers we drive, and
	// the day something routes a foreign device through it, "not Undefined" would quietly have an Xbox pad
	// claiming a gyroscope it does not have.
	const bool bIsDrivenFamily = Info.ControllerType != EJSL4UControllerType::Undefined
		&& Info.ControllerType != EJSL4UControllerType::XInputController;
	Info.bHasMotionSensors = bIsDrivenFamily;
	Info.bHasRumble = bIsDrivenFamily;
	Info.bHasRgbLight = Info.ControllerType == EJSL4UControllerType::DualShock4
		|| Info.ControllerType == EJSL4UControllerType::DualSense;
	Info.bHasTouchpad = Info.ControllerType == EJSL4UControllerType::DualShock4
		|| Info.ControllerType == EJSL4UControllerType::DualSense;
	Info.bHasPlayerIndicator = Info.ControllerType == EJSL4UControllerType::JoyConLeft
		|| Info.ControllerType == EJSL4UControllerType::JoyConRight
		|| Info.ControllerType == EJSL4UControllerType::ProController
		|| Info.ControllerType == EJSL4UControllerType::ProController2
		|| Info.ControllerType == EJSL4UControllerType::DualSense;
	// JSL stores the gyro space as a raw int. Anything outside the three JSL4U knows about reads as Local
	// Space rather than as a garbage enumerator, so a value the library grows later cannot make a Blueprint
	// switch fall through a branch that does not exist.
	Info.GyroSpace = JslSettings.gyroSpace >= 0 && JslSettings.gyroSpace <= static_cast<int32>(EJSL4UGyroSpace::PlayerSpace)
		? static_cast<EJSL4UGyroSpace>(JslSettings.gyroSpace)
		: EJSL4UGyroSpace::LocalSpace;
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
				Result.Add(JSL4UMakeControllerInfo(Pair.Key, JSL4UReadJslSettings(Pair.Value), Pair.Value));
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

FJSL4UControllerInfo UJoyShockLibrary::JSL4UDescribeUndrivenDevice(FPlatformUserId PlatformUser,
	FInputDeviceId InputDevice)
{
	// Fill in what Unreal knows about any input device and leave the rest at its defaults, which is what
	// bIsJoyShockController staying false tells a Blueprint to expect. Device Id is explicitly -1 rather
	// than the field's default of 0, so a controller this plugin cannot address can never be mistaken for
	// its device 0 -- and that -1 is the test for "can I call the controller-specific nodes on this".
	FJSL4UControllerInfo Info;
	Info.bIsJoyShockController = false;
	Info.DeviceId = INDEX_NONE;
	Info.InputDeviceId = InputDevice.GetId();

	// Connection Id is the one identity here no JSL4U call consumes, so unlike Device Id it can carry a
	// real value for a controller this plugin does not drive -- and it has to. Left at its default of 0,
	// every XInput pad shared one key: a second one collided with the first in any map keyed by it, and a
	// disconnect removed whichever entry sat on 0. The demo's controller mirror keys exactly this way.
	//
	// Negative, derived from Unreal's device id: the interface counts Connection Ids up from 1 (see
	// NextConnectionId in JoyShockInterface), so the two sources share a map without ever colliding.
	// Deriving beats counting because every caller then describes a pad identically -- a private counter
	// would make a disconnect match a value some other caller invented. The cost is that Unreal reuses a
	// device id once a pad leaves, so this is unique among connected controllers rather than unique
	// forever: good for keying a live map, not for storing on disk.
	Info.ConnectionId = -(static_cast<int64>(Info.InputDeviceId) + 1);

	Info.PlatformUserId = PlatformUser.GetInternalId();
	Info.PlayerIndex = IPlatformInputDeviceMapper::Get().GetUserIndexForPlatformUser(PlatformUser);
	Info.ControllerType = EJSL4UControllerType::XInputController;
	Info.bIsConnected = true;

	// The capability flags describe the HARDWARE, not what this plugin can drive -- "can I drive it" is
	// what Device Id -1 already says. An XInput pad has rumble motors and a game should offer the setting
	// for it; that rumble is reached through Unreal's own force feedback, which drives our controllers and
	// this one from the same authored effect. Reporting false here made a game hide its vibration options
	// from the one pad every player owns. Motion, touchpad, lights and the player indicator stay false
	// because the hardware genuinely lacks them.
	Info.bHasRumble = true;

	if (const UInputDeviceSubsystem* InputDevices = UInputDeviceSubsystem::Get())
	{
		Info.HardwareDeviceIdentifier =
			InputDevices->GetInputDeviceHardwareIdentifier(InputDevice).HardwareDeviceIdentifier;
	}

	return Info;
}

TArray<FJSL4UControllerInfo> UJoyShockLibrary::JSL4UGetAllConnectedControllers()
{
	// Ours first and already sorted by device id, so the JSL4U block of this list is byte-for-byte the
	// answer Get Connected Controllers gives. Anything a game did with that list keeps working when it
	// switches to this one.
	TArray<FJSL4UControllerInfo> Result = JSL4UGetConnectedControllers();

	IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();
	const FInputDeviceId DefaultDevice = DeviceMapper.GetDefaultInputDevice();

	TArray<FInputDeviceId> ConnectedDevices;
	DeviceMapper.GetAllConnectedInputDevices(ConnectedDevices);
	for (const FInputDeviceId& InputDevice : ConnectedDevices)
	{
		// The keyboard and mouse share one device that is connected from the first frame of every game.
		// Wait For Any Controller Changes leaves it out for that reason and this has to agree with it --
		// a roster that disagrees with the event that built it is worse than either alone.
		if (InputDevice == DefaultDevice)
		{
			continue;
		}

		// Ours are already in, described by the plugin itself. Matching on Unreal's device id rather than
		// on the hardware name means a controller family the plugin gains later needs no change here.
		const bool bAlreadyListed = Result.ContainsByPredicate(
			[&InputDevice](const FJSL4UControllerInfo& Listed)
			{
				return Listed.InputDeviceId == InputDevice.GetId();
			});
		if (bAlreadyListed)
		{
			continue;
		}

		Result.Add(JSL4UDescribeUndrivenDevice(DeviceMapper.GetUserForInputDevice(InputDevice), InputDevice));
	}

	return Result;
}

bool UJoyShockLibrary::JSL4UIsControllerTypeJoinable(EJSL4UControllerType ControllerType)
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

	if (!JSL4UIsControllerTypeJoinable(TypeA) || !JSL4UIsControllerTypeJoinable(TypeB))
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

bool UJoyShockLibrary::JSL4UGetJoyConPartner(int32 DeviceId, int32& PartnerDeviceId)
{
	FJoyShockInterface* Interface = FJoyShockLibrary4UnrealModule::GetInstance().GetActiveInterface();
	PartnerDeviceId = Interface != nullptr ? Interface->GetJoinPartner(DeviceId) : INDEX_NONE;
	return PartnerDeviceId != INDEX_NONE;
}

bool UJoyShockLibrary::JSL4UIsJoyConPrimary(int32 DeviceId)
{
	FJoyShockInterface* Interface = FJoyShockLibrary4UnrealModule::GetInstance().GetActiveInterface();
	// Without an interface there is no pairing at all, so every device stands alone -- and a standalone
	// device is its own primary. Returning true keeps a mirror visible rather than hiding everything.
	return Interface == nullptr || Interface->IsJoinPrimary(DeviceId);
}

bool UJoyShockLibrary::JSL4UGetJoyConPair(int32 DeviceId, FJSL4UControllerInfo& PrimaryController,
	FJSL4UControllerInfo& PartnerController)
{
	PrimaryController = JSL4UGetControllerInfo(DeviceId);
	PartnerController = FJSL4UControllerInfo();

	int32 PartnerDeviceId = INDEX_NONE;
	if (!JSL4UGetJoyConPartner(DeviceId, PartnerDeviceId))
	{
		// Standalone: the caller's controller leads a group of one. Partner stays unset rather than being
		// filled with a copy, so "is there a second half" is answerable from the struct alone.
		return false;
	}

	const FJSL4UControllerInfo OtherInfo = JSL4UGetControllerInfo(PartnerDeviceId);

	// Order the two by the plugin's own grouping rule instead of by which half was asked about. Both halves
	// therefore get identical answers, which is what lets a caller act on a pair without first working out
	// which of the two it is holding.
	if (JSL4UIsJoyConPrimary(DeviceId))
	{
		PartnerController = OtherInfo;
	}
	else
	{
		PartnerController = PrimaryController;
		PrimaryController = OtherInfo;
	}
	return true;
}

// The game viewport owning the world this Blueprint is running in, or null outside a game world.
static UGameViewportClient* JSL4UGetGameViewport(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine != nullptr
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World != nullptr ? World->GetGameViewport() : nullptr;
}

bool UJoyShockLibrary::JSL4USetMaxLocalPlayers(const UObject* WorldContextObject, int32 MaxLocalPlayers)
{
	if (MaxLocalPlayers < 1)
	{
		UE_LOG(LogJoyShockLibrary, Warning,
			TEXT("JSL4USetMaxLocalPlayers: %d is not a usable number of players; ignoring."), MaxLocalPlayers);
		return false;
	}

	UGameViewportClient* Viewport = JSL4UGetGameViewport(WorldContextObject);
	if (Viewport == nullptr)
	{
		UE_LOG(LogJoyShockLibrary, Warning,
			TEXT("JSL4USetMaxLocalPlayers: no game viewport in this context, so the limit was not changed."));
		return false;
	}

	// Unreal's own name for this is MaxSplitscreenPlayers, but it is the ceiling UGameInstance::CreateLocalPlayer
	// checks whatever the screen is doing -- see the note on the declaration.
	Viewport->MaxSplitscreenPlayers = MaxLocalPlayers;
	return true;
}

int32 UJoyShockLibrary::JSL4UGetMaxLocalPlayers(const UObject* WorldContextObject)
{
	const UGameViewportClient* Viewport = JSL4UGetGameViewport(WorldContextObject);
	return Viewport != nullptr ? Viewport->MaxSplitscreenPlayers : INDEX_NONE;
}

bool UJoyShockLibrary::JSL4UAssignControllerToPlayerIndex(const FJSL4UControllerInfo& Controller, int32 PlayerIndex)
{
	const int32 DeviceId = Controller.DeviceId;

	// A controller this plugin does not drive has no slot in the plugin's table, and does not need one: the
	// engine decides which player an input device feeds by which platform user it is mapped to, so moving
	// it means remapping it there. Doing that here rather than making the caller find out which kind it is
	// holding is the whole point of taking the description instead of a device id.
	if (DeviceId < 0)
	{
		if (Controller.InputDeviceId < 0)
		{
			UE_LOG(LogJoyShockLibrary, Warning,
				TEXT("JSL4UAssignControllerToPlayerIndex: this controller has neither a device id nor an Unreal ")
				TEXT("input device id, so there is nothing to assign. Pass a controller from Get All Connected ")
				TEXT("Controllers or from one of the Wait For ... Changes nodes."));
			return false;
		}

		IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();
		const FInputDeviceId InputDevice = FInputDeviceId::CreateFromInternalId(Controller.InputDeviceId);

		// -1 means "back to automatic" for our devices. Unreal spells the same idea as the unpaired user,
		// so the two halves of this function agree on what -1 does rather than one of them refusing it.
		const FPlatformUserId TargetUser = PlayerIndex < 0
			? DeviceMapper.GetUserForUnpairedInputDevices()
			: DeviceMapper.GetPlatformUserForUserIndex(PlayerIndex);

		if (!TargetUser.IsValid())
		{
			UE_LOG(LogJoyShockLibrary, Warning,
				TEXT("JSL4UAssignControllerToPlayerIndex: player %d has no platform user, so input device %d ")
				TEXT("was not assigned."), PlayerIndex, Controller.InputDeviceId);
			return false;
		}

		DeviceMapper.Internal_MapInputDeviceToUser(InputDevice, TargetUser, EInputDeviceConnectionState::Connected);
		UE_LOG(LogJoyShockLibrary, Verbose,
			TEXT("Input device %d is not one of ours; mapped it to platform user %d for player %d."),
			Controller.InputDeviceId, TargetUser.GetInternalId(), PlayerIndex);
		return true;
	}

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

bool UJoyShockLibrary::JSL4UAssignControllerToPlayer(const FJSL4UControllerInfo& Controller, APlayerController* PlayerController)
{
	const int32 DeviceId = Controller.DeviceId;
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
	// JSL4UGetControllersAssignedToPlayer about why the legacy controller id is the wrong number here.
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

	return JSL4UAssignControllerToPlayerIndex(Controller, UserIndex);
}

TArray<FJSL4UControllerInfo> UJoyShockLibrary::JSL4UGetControllersAssignedToPlayerIndex(int32 PlayerIndex)
{
	// Filtering a listing keeps this on an existing single-pass path rather than adding a second way to read
	// the same state. It filters the WIDE listing on purpose: a player can be assigned an XInput pad (see
	// JSL4UAssignControllerToPlayerIndex, which handles them through the device mapper), so answering from
	// the narrow one made assignment writable but not readable -- the pad went to the player and this node
	// then reported that player had no controller at all.
	TArray<FJSL4UControllerInfo> Result = JSL4UGetAllConnectedControllers();
	Result.RemoveAll([PlayerIndex](const FJSL4UControllerInfo& Info)
	{
		return Info.PlayerIndex != PlayerIndex;
	});
	return Result;
}

TArray<FJSL4UControllerInfo> UJoyShockLibrary::JSL4UGetControllersAssignedToPlayer(AController* Controller)
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

	return JSL4UGetControllersAssignedToPlayerIndex(UserIndex);
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

FJSL4UJoyShockState UJoyShockLibrary::JSL4UGetControllerState(int32 DeviceId)
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


	// Gravity-corrected orientation. This used to return rawQuat with a note blaming a bug in
	// GamepadMotion.hpp, alongside two commented-out attempts at the corrected quaternion -- each using a
	// DIFFERENT sign pattern from the one on the line below, and neither behaving correctly.
	//
	// That was the whole bug. Quaternion and RawQuaternion are the same quantity in the same coordinate
	// space: both start at identity and accumulate the same per-frame `*= rotation`, and the only
	// difference is the gravity correction applied to one of them (see Motion::Update). So whatever
	// conversion is right for one is right for the other, and swapping in a different sign pattern along
	// with the corrected quaternion made a working conversion look like broken correction maths. The
	// correction itself is upstream JibbSmart code, unmodified here apart from std:: -> FMath:: swaps.
	UnrealMotionState.Orientation = FQuat(NativeMotionState.quatZ, -NativeMotionState.quatX, -NativeMotionState.quatY, NativeMotionState.quatW);

	UnrealMotionState.Acceleration = FVector(NativeMotionState.accelZ, NativeMotionState.accelX, -NativeMotionState.accelY);
	UnrealMotionState.Gravity = FVector(-NativeMotionState.gravZ, NativeMotionState.gravX, NativeMotionState.gravY);
	return UnrealMotionState;
}

FJSL4UMotionState UJoyShockLibrary::JSL4UGetRawMotionState(int32 DeviceID)
{
	FMotionState NativeMotionState = JslGetMotionState(DeviceID);
	FJSL4UMotionState UnrealMotionState;

	// The uncorrected orientation -- which is what "raw" means here, the counterpart to the corrected one
	// JSL4UGetMotionState returns. This read quat (the CORRECTED quaternion) with a third sign pattern of
	// its own: the two getters were reading each other's quaternion. Axes are converted exactly as in
	// JSL4UGetMotionState, so both getters describe the same space and differ only in the correction --
	// matching how JSL4UGetRawIMUState relates to JSL4UGetIMUState.
	UnrealMotionState.Orientation = FQuat(NativeMotionState.rawQuatZ, -NativeMotionState.rawQuatX, -NativeMotionState.rawQuatY, NativeMotionState.rawQuatW);

	UnrealMotionState.Acceleration = FVector(NativeMotionState.accelZ, NativeMotionState.accelX, -NativeMotionState.accelY);
	UnrealMotionState.Gravity = FVector(-NativeMotionState.gravZ, NativeMotionState.gravX, NativeMotionState.gravY);
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

FVector UJoyShockLibrary::JSL4UGetAndClearAccumulatedGyro(int32 DeviceId)
{
	float GyroX, GyroY, GyroZ;
	JslGetAndFlushAccumulatedGyro(DeviceId, GyroY, GyroZ, GyroX);
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

void UJoyShockLibrary::JSL4USetGyroSpace(int32 DeviceId, EJSL4UGyroSpace GyroSpace)
{
	JslSetGyroSpace(DeviceId, static_cast<int32>(GyroSpace));
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

FVector2D UJoyShockLibrary::JSL4UGetTouchPosition(int32 DeviceId, bool bSecondTouch)
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
float UJoyShockLibrary::JSL4UGetStickResolutionStep(int32 DeviceId)
{
	return JslGetStickStep(DeviceId);
}

float UJoyShockLibrary::JSL4UGetTriggerResolutionStep(int32 DeviceId)
{
	return JslGetTriggerStep(DeviceId);
}

float UJoyShockLibrary::JSL4UGetPollInterval(int32 DeviceId)
{
	return JslGetPollRate(DeviceId);
}

float UJoyShockLibrary::JSL4UGetSecondsSinceLastReport(int32 DeviceId)
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

// The gyro axis convention JSL4U exposes everywhere else (see JSL4UGetIMUState / JSL4UGetAndClearAccumulatedGyro):
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
	// being held still. Manual leaves it entirely to JSL4UStartManualGyroCalibration / JSL4UStopManualGyroCalibration.
	JslSetAutomaticCalibration(DeviceId, Mode == EJSL4UGyroCalibrationMode::Automatic);
}

void UJoyShockLibrary::JSL4UStartManualGyroCalibration(int32 DeviceId)
{
	JslStartContinuousCalibration(DeviceId);
}

void UJoyShockLibrary::JSL4UStopManualGyroCalibration(int32 DeviceId)
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

FJSL4UControllerInfo UJoyShockLibrary::JSL4UGetControllerInfo(int32 DeviceId)
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
		Info = JSL4UMakeControllerInfo(DeviceId, JSL4UReadJslSettings(jc), jc);
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

// Shared by JSL4USetControllerRumble and the legacy JslSetRumble. Every controller family now works the same way:
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

void UJoyShockLibrary::JSL4USetControllerRumble(int32 DeviceId, float SmallRumble, float BigRumble)
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

void UJoyShockLibrary::JSL4USetHomeLight(int32 DeviceId, float Brightness)
{
	FJoyShockLibrary4UnrealModule& JSL4UModule = FJoyShockLibrary4UnrealModule::GetInstance();

	std::shared_lock<std::shared_timed_mutex> lock(JSL4UModule._connectedLock);

	JoyShock* jc = GetJoyShockFromHandle(DeviceId);
	// Only a Switch 1 right Joy-Con (left_right 2) or Pro Controller (3) has this light. The Switch 2 Pro
	// speaks a different command protocol and is excluded until its equivalent is mapped.
	if (jc == nullptr || jc->controller_type != ControllerType::n_switch || jc->is_switch2_pro
		|| (jc->left_right != 2 && jc->left_right != 3))
	{
		return;
	}

	// Store only; the polling thread stays the sole writer to the controller, exactly as with rumble and
	// the player indicator, so no blocking HID write happens on the game thread.
	const unsigned char Intensity =
		static_cast<unsigned char>(FMath::RoundToInt(FMath::Clamp(Brightness, 0.f, 1.f) * 15.f));
	jc->wanted_home_light.store(Intensity);
	jc->home_light_owned_by_game.store(true);
	// Published last, and after the intensity, so the polling thread cannot observe a new generation
	// alongside the previous value. Bumping it unconditionally is the point: this call must reach the
	// controller even when the intensity is the one the plugin already believes it set, because the firmware
	// can have turned the light on since.
	jc->home_light_generation.fetch_add(1);
}

void UJoyShockLibrary::JSL4USetPlayerIndicator(int32 DeviceId, int32 Number)
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
	JSL4USetPlayerIndicator(deviceId, number);
}
