#include "Switch2Bluetooth.h"

// JSL_SWITCH2_BLUETOOTH is set by the module rules: it is 0 when no Windows SDK on the build machine
// carries the C++/WinRT headers this transport is written against, which compiles Bluetooth out rather
// than failing the build for everyone who only ever uses these controllers on a cable.
#ifndef JSL_SWITCH2_BLUETOOTH
#define JSL_SWITCH2_BLUETOOTH 0
#endif

#if PLATFORM_WINDOWS && JSL_SWITCH2_BLUETOOTH

#include "HAL/CriticalSection.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"

#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogSwitch2Bluetooth, Log, All);

namespace
{
	using namespace winrt::Windows::Devices::Bluetooth;
	using namespace winrt::Windows::Devices::Bluetooth::Advertisement;
	using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
	using namespace winrt::Windows::Storage::Streams;

	// The controllers' vendor GATT service and its characteristics. Every Switch 2 controller exposes the
	// same set; only the vibration characteristic differs between the Pro Controller and each Joy-Con,
	// because a Joy-Con pair is two peripherals driving one actuator each.
	const winrt::guid ServiceUuid          { 0xab7de9be, 0x89fe, 0x49ad, { 0x82, 0x8f, 0x11, 0x8f, 0x09, 0xdf, 0x7f, 0xd0 } };
	const winrt::guid InputReportUuid      { 0xab7de9be, 0x89fe, 0x49ad, { 0x82, 0x8f, 0x11, 0x8f, 0x09, 0xdf, 0x7f, 0xd2 } };
	const winrt::guid CommandWriteUuid     { 0x649d4ac9, 0x8eb7, 0x4e6c, { 0xaf, 0x44, 0x1e, 0xa5, 0x4f, 0xe5, 0xf0, 0x05 } };
	const winrt::guid CommandResponseUuid  { 0xc765a961, 0xd9d8, 0x4d36, { 0xa2, 0x0a, 0x53, 0x15, 0xb1, 0x11, 0x83, 0x6a } };
	const winrt::guid VibrationProUuid     { 0xcc483f51, 0x9258, 0x427d, { 0xa9, 0x39, 0x63, 0x0c, 0x31, 0xf7, 0x2b, 0x05 } };
	const winrt::guid VibrationJoyConLUuid { 0x289326cb, 0xa471, 0x485d, { 0xa8, 0xf4, 0x24, 0x0c, 0x14, 0xf1, 0x82, 0x41 } };
	const winrt::guid VibrationJoyConRUuid { 0xfa19b0fb, 0xcd1f, 0x46a7, { 0x84, 0xa1, 0xbb, 0xb0, 0x9e, 0x00, 0xc1, 0x49 } };

	// Switch 2 product ids as they appear in the advertisement, which is the only place they can be read
	// before a connection exists.
	constexpr uint16 Switch2ProControllerId = 0x2069;
	constexpr uint16 Switch2JoyConLeftId    = 0x2067;
	constexpr uint16 Switch2JoyConRightId   = 0x2066;

	bool IsSwitch2ProductId(uint16 ProductId)
	{
		return ProductId == Switch2ProControllerId
			|| ProductId == Switch2JoyConLeftId
			|| ProductId == Switch2JoyConRightId;
	}

	// C++/WinRT's blocking waits are only legal on a multi-threaded apartment, and every call in this file
	// runs on a plugin worker thread that has no message pump to service an STA with. Initialising is
	// per-thread and idempotent; a thread the engine already put in an apartment keeps the one it has.
	void EnsureApartment()
	{
		static thread_local bool bInitialised = false;
		if (bInitialised)
		{
			return;
		}
		bInitialised = true;
		try
		{
			winrt::init_apartment(winrt::apartment_type::multi_threaded);
		}
		catch (const winrt::hresult_error&)
		{
			// RPC_E_CHANGED_MODE: the thread is already in an apartment. Blocking waits still work when
			// that apartment is the MTA, and this is not something to fail a connection over.
		}
	}

	TArray<uint8> BufferToArray(const IBuffer& Buffer)
	{
		TArray<uint8> Result;
		if (Buffer == nullptr || Buffer.Length() == 0)
		{
			return Result;
		}
		Result.SetNumUninitialized(static_cast<int32>(Buffer.Length()));
		DataReader Reader = DataReader::FromBuffer(Buffer);
		Reader.ReadBytes(winrt::array_view<uint8_t>(Result.GetData(), Result.GetData() + Result.Num()));
		return Result;
	}

	IBuffer ArrayToBuffer(const uint8* Data, int32 Length)
	{
		DataWriter Writer;
		Writer.WriteBytes(winrt::array_view<const uint8_t>(Data, Data + Length));
		return Writer.DetachBuffer();
	}
}

// One live GATT connection. Input reports and command responses arrive on WinRT's own threads, so both
// are handed to the polling thread through a queue guarded by a lock rather than being acted on in place.
class FSwitch2BleConnection
{
public:
	uint64 Address = 0;
	uint16 ProductId = 0;

	BluetoothLEDevice Device{ nullptr };
	// The session is what actually holds the radio link up. Without one WinRT is free to drop the
	// connection the moment nothing is being read, and the controller goes quiet mid-game.
	GattSession Session{ nullptr };
	GattCharacteristic InputCharacteristic{ nullptr };
	GattCharacteristic CommandCharacteristic{ nullptr };
	GattCharacteristic CommandResponseCharacteristic{ nullptr };
	GattCharacteristic VibrationCharacteristic{ nullptr };

	winrt::event_token InputToken{};
	winrt::event_token ResponseToken{};
	winrt::event_token ConnectionStatusToken{};

	std::atomic<bool> bConnected{ true };

	// Input reports. The queue is bounded: a game that stops polling must not grow it without limit, and
	// an old report is worth nothing next to a new one, so the oldest is dropped.
	static constexpr int32 MaxQueuedInputReports = 32;
	FCriticalSection InputLock;
	TArray<TArray<uint8>> InputQueue;
	FEvent* InputEvent = nullptr;

	// The most recent command response, and the event the sender waits on.
	FCriticalSection ResponseLock;
	TArray<uint8> Response;
	FEvent* ResponseEvent = nullptr;

	// Commands are strictly serialised; see SendCommand.
	FCriticalSection CommandLock;

	FSwitch2BleConnection()
	{
		InputEvent = FPlatformProcess::GetSynchEventFromPool(false);
		ResponseEvent = FPlatformProcess::GetSynchEventFromPool(false);
	}

	~FSwitch2BleConnection()
	{
		if (InputEvent != nullptr)
		{
			FPlatformProcess::ReturnSynchEventToPool(InputEvent);
			InputEvent = nullptr;
		}
		if (ResponseEvent != nullptr)
		{
			FPlatformProcess::ReturnSynchEventToPool(ResponseEvent);
			ResponseEvent = nullptr;
		}
	}

	void MarkDisconnected()
	{
		bConnected = false;
		// Wake anyone blocked in a read or waiting on a command, so a dropped link surfaces immediately
		// instead of at the end of their timeout.
		if (InputEvent != nullptr)
		{
			InputEvent->Trigger();
		}
		if (ResponseEvent != nullptr)
		{
			ResponseEvent->Trigger();
		}
	}
};

namespace
{
	// Scanning state. The watcher raises its callback on a WinRT thread, so discovered controllers are
	// queued here and picked up by whichever plugin thread next drains them.
	FCriticalSection GScanLock;
	BluetoothLEAdvertisementWatcher GWatcher{ nullptr };
	TArray<FSwitch2BleAdvertisement> GDiscovered;
	// When each address may be offered again, so a controller advertising several times a second is not
	// offered over and over. A controller that has been taken is suppressed until it is disconnected
	// (Forever); one whose connection attempt failed is suppressed briefly, so a controller that cannot be
	// connected to right now is retried rather than hammered.
	constexpr double SuppressForever = TNumericLimits<double>::Max();
	constexpr double SuppressAfterFailureSeconds = 3.0;
	TMap<uint64, double> GSuppressedUntil;
	TFunction<void()> GOnDiscovered;

	FCriticalSection GConnectionLock;
	TArray<FSwitch2BleConnection*> GConnections;
}

namespace Switch2Ble
{

bool IsSupported()
{
	// Cached: this asks the OS for the radio, which blocks, and enumeration calls it on every pass. A
	// machine does not grow a Bluetooth adapter mid-session.
	static std::atomic<int8> CachedSupport{ -1 };
	const int8 Cached = CachedSupport.load();
	if (Cached >= 0)
	{
		return Cached != 0;
	}

	EnsureApartment();
	bool bSupported = false;
	try
	{
		BluetoothAdapter Adapter = BluetoothAdapter::GetDefaultAsync().get();
		bSupported = Adapter != nullptr && Adapter.IsLowEnergySupported();
	}
	catch (const winrt::hresult_error&)
	{
		bSupported = false;
	}

	if (!bSupported)
	{
		UE_LOG(LogSwitch2Bluetooth, Log,
			TEXT("No Bluetooth LE adapter available; Switch 2 controllers will only work over USB."));
	}
	CachedSupport.store(bSupported ? 1 : 0);
	return bSupported;
}

uint64 GetHostAddress()
{
	EnsureApartment();
	try
	{
		BluetoothAdapter Adapter = BluetoothAdapter::GetDefaultAsync().get();
		return Adapter != nullptr ? Adapter.BluetoothAddress() : 0;
	}
	catch (const winrt::hresult_error&)
	{
		return 0;
	}
}

bool StartScan()
{
	EnsureApartment();

	FScopeLock Lock(&GScanLock);
	if (GWatcher != nullptr)
	{
		return true;
	}

	try
	{
		GWatcher = BluetoothLEAdvertisementWatcher();
		// Active scanning asks each advertiser for its scan response. The manufacturer data this transport
		// identifies controllers by does not always fit in the initial advertisement.
		GWatcher.ScanningMode(BluetoothLEScanningMode::Active);

		GWatcher.Received([](const BluetoothLEAdvertisementWatcher&, const BluetoothLEAdvertisementReceivedEventArgs& Args)
		{
			// Nintendo packs vendor, product and the bonded host into its manufacturer data. A controller
			// with none of it is some other BLE device that happens to be in range.
			for (const BluetoothLEManufacturerData& Section : Args.Advertisement().ManufacturerData())
			{
				if (Section.CompanyId() != NINTENDO_BLE_MANUFACTURER_ID)
				{
					continue;
				}

				const TArray<uint8> Data = BufferToArray(Section.Data());
				if (Data.Num() < 7)
				{
					continue;
				}

				FSwitch2BleAdvertisement Found;
				Found.Address = Args.BluetoothAddress();
				Found.VendorId = static_cast<uint16>(Data[3] | (Data[4] << 8));
				Found.ProductId = static_cast<uint16>(Data[5] | (Data[6] << 8));
				if (Data.Num() >= 16)
				{
					uint64 Mac = 0;
					for (int32 Index = 0; Index < 6; Index++)
					{
						Mac |= static_cast<uint64>(Data[10 + Index]) << (Index * 8);
					}
					Found.ReconnectMac = Mac;
				}

				if (!IsSwitch2ProductId(Found.ProductId))
				{
					continue;
				}

				TFunction<void()> Notify;
				{
					FScopeLock ScanLock(&GScanLock);
					if (const double* SuppressedUntil = GSuppressedUntil.Find(Found.Address))
					{
						if (FPlatformTime::Seconds() < *SuppressedUntil)
						{
							continue;
						}
					}
					GSuppressedUntil.Add(Found.Address, SuppressForever);
					GDiscovered.Add(Found);
					Notify = GOnDiscovered;
				}

				UE_LOG(LogSwitch2Bluetooth, Log,
					TEXT("Switch 2 controller %012llX is advertising (product %04X, paired to %012llX)."),
					Found.Address, Found.ProductId, Found.ReconnectMac);

				// Outside the lock: this runs an enumeration pass, and holding the scan lock across it
				// would block every further advertisement for its duration.
				if (Notify)
				{
					Notify();
				}
				return;
			}
		});

		GWatcher.Stopped([](const BluetoothLEAdvertisementWatcher&, const BluetoothLEAdvertisementWatcherStoppedEventArgs&)
		{
			// The radio can abort a watcher on its own (adapter reset, radio switched off). Clearing the
			// handle here means the next StartScan builds a fresh one rather than reusing a dead watcher.
			FScopeLock ScanLock(&GScanLock);
			GWatcher = nullptr;
		});

		GWatcher.Start();
		UE_LOG(LogSwitch2Bluetooth, Log,
			TEXT("Scanning for Switch 2 controllers. Hold SYNC on a new one, or press a button on one already paired to this PC."));
		return true;
	}
	catch (const winrt::hresult_error& Error)
	{
		UE_LOG(LogSwitch2Bluetooth, Warning, TEXT("Could not start the Bluetooth scan: %s"),
			Error.message().c_str());
		GWatcher = nullptr;
		return false;
	}
}

void SetDiscoveryCallback(TFunction<void()> OnDiscovered)
{
	FScopeLock Lock(&GScanLock);
	GOnDiscovered = MoveTemp(OnDiscovered);
}

void StopScan()
{
	FScopeLock Lock(&GScanLock);
	if (GWatcher == nullptr)
	{
		return;
	}
	try
	{
		GWatcher.Stop();
	}
	catch (const winrt::hresult_error&)
	{
	}
	GWatcher = nullptr;
	GDiscovered.Reset();
	GSuppressedUntil.Reset();
}

void DrainDiscovered(TArray<FSwitch2BleAdvertisement>& OutDiscovered)
{
	FScopeLock Lock(&GScanLock);
	OutDiscovered.Append(GDiscovered);
	GDiscovered.Reset();
}

FSwitch2BleConnection* Connect(uint64 Address)
{
	EnsureApartment();

	// A controller that fails to connect must be offered again, or one bad attempt would take it out of
	// this session entirely -- but not immediately: it advertises many times a second, and retrying on
	// every one of those would be a tight loop of failing connections. One that succeeds stays suppressed
	// until it is disconnected, so a connected controller is not handed out a second time.
	bool bConnected = false;
	ON_SCOPE_EXIT
	{
		if (!bConnected)
		{
			FScopeLock ScanLock(&GScanLock);
			GSuppressedUntil.Add(Address, FPlatformTime::Seconds() + SuppressAfterFailureSeconds);
		}
	};

	TUniquePtr<FSwitch2BleConnection> Connection = MakeUnique<FSwitch2BleConnection>();
	Connection->Address = Address;

	try
	{
		Connection->Device = BluetoothLEDevice::FromBluetoothAddressAsync(Address).get();
		if (Connection->Device == nullptr)
		{
			UE_LOG(LogSwitch2Bluetooth, Warning, TEXT("No Bluetooth device at address %012llX."), Address);
			return nullptr;
		}

		// Uncached: the controller was very likely connected to something else since Windows last looked,
		// and a stale service list produces characteristics whose handles no longer resolve.
		GattDeviceServicesResult ServicesResult =
			Connection->Device.GetGattServicesForUuidAsync(ServiceUuid, BluetoothCacheMode::Uncached).get();
		if (ServicesResult.Status() != GattCommunicationStatus::Success || ServicesResult.Services().Size() == 0)
		{
			UE_LOG(LogSwitch2Bluetooth, Warning,
				TEXT("Device %012llX does not expose the Switch 2 service; it is not a Switch 2 controller."), Address);
			return nullptr;
		}

		GattDeviceService Service = ServicesResult.Services().GetAt(0);

		Connection->Session = GattSession::FromDeviceIdAsync(Connection->Device.BluetoothDeviceId()).get();
		if (Connection->Session != nullptr)
		{
			Connection->Session.MaintainConnection(true);
		}

		auto FindCharacteristic = [&Service](const winrt::guid& Uuid) -> GattCharacteristic
		{
			GattCharacteristicsResult Result = Service.GetCharacteristicsForUuidAsync(Uuid, BluetoothCacheMode::Uncached).get();
			if (Result.Status() != GattCommunicationStatus::Success || Result.Characteristics().Size() == 0)
			{
				return GattCharacteristic{ nullptr };
			}
			return Result.Characteristics().GetAt(0);
		};

		Connection->InputCharacteristic = FindCharacteristic(InputReportUuid);
		Connection->CommandCharacteristic = FindCharacteristic(CommandWriteUuid);
		Connection->CommandResponseCharacteristic = FindCharacteristic(CommandResponseUuid);

		if (Connection->InputCharacteristic == nullptr || Connection->CommandCharacteristic == nullptr
			|| Connection->CommandResponseCharacteristic == nullptr)
		{
			UE_LOG(LogSwitch2Bluetooth, Warning,
				TEXT("Device %012llX is missing one of the Switch 2 characteristics."), Address);
			return nullptr;
		}

		// The vibration characteristic is per controller shape. Try all three rather than deciding from the
		// advertised product id: a Joy-Con exposes only its own side, so whichever one is present is the
		// right one, and the Pro Controller only ever has the Pro characteristic.
		Connection->VibrationCharacteristic = FindCharacteristic(VibrationProUuid);
		if (Connection->VibrationCharacteristic == nullptr)
		{
			Connection->VibrationCharacteristic = FindCharacteristic(VibrationJoyConLUuid);
		}
		if (Connection->VibrationCharacteristic == nullptr)
		{
			Connection->VibrationCharacteristic = FindCharacteristic(VibrationJoyConRUuid);
		}

		FSwitch2BleConnection* Raw = Connection.Get();

		Connection->ConnectionStatusToken = Connection->Device.ConnectionStatusChanged(
			[Raw](const BluetoothLEDevice& Sender, const winrt::Windows::Foundation::IInspectable&)
			{
				if (Sender.ConnectionStatus() == BluetoothConnectionStatus::Disconnected)
				{
					Raw->MarkDisconnected();
				}
			});

		Connection->ResponseToken = Connection->CommandResponseCharacteristic.ValueChanged(
			[Raw](const GattCharacteristic&, const GattValueChangedEventArgs& Args)
			{
				FScopeLock ResponseLock(&Raw->ResponseLock);
				Raw->Response = BufferToArray(Args.CharacteristicValue());
				Raw->ResponseEvent->Trigger();
			});

		Connection->InputToken = Connection->InputCharacteristic.ValueChanged(
			[Raw](const GattCharacteristic&, const GattValueChangedEventArgs& Args)
			{
				TArray<uint8> Report = BufferToArray(Args.CharacteristicValue());
				if (Report.Num() == 0)
				{
					return;
				}

				FScopeLock InputLock(&Raw->InputLock);
				if (Raw->InputQueue.Num() >= FSwitch2BleConnection::MaxQueuedInputReports)
				{
					Raw->InputQueue.RemoveAt(0, 1, EAllowShrinking::No);
				}
				Raw->InputQueue.Add(MoveTemp(Report));
				Raw->InputEvent->Trigger();
			});

		// Subscribe to the response channel before input: the init commands that follow a connection are
		// answered on it, and a subscription that lands late loses the first acknowledgement.
		auto Subscribe = [](const GattCharacteristic& Characteristic) -> bool
		{
			return Characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
				GattClientCharacteristicConfigurationDescriptorValue::Notify).get() == GattCommunicationStatus::Success;
		};

		if (!Subscribe(Connection->CommandResponseCharacteristic))
		{
			UE_LOG(LogSwitch2Bluetooth, Warning,
				TEXT("Device %012llX refused the command-response subscription."), Address);
			return nullptr;
		}
		if (!Subscribe(Connection->InputCharacteristic))
		{
			UE_LOG(LogSwitch2Bluetooth, Warning,
				TEXT("Device %012llX refused the input subscription."), Address);
			return nullptr;
		}

		{
			FScopeLock ConnectionsLock(&GConnectionLock);
			GConnections.Add(Raw);
		}

		UE_LOG(LogSwitch2Bluetooth, Log, TEXT("Connected to Switch 2 controller %012llX over Bluetooth."), Address);
		bConnected = true;
		return Connection.Release();
	}
	catch (const winrt::hresult_error& Error)
	{
		UE_LOG(LogSwitch2Bluetooth, Warning, TEXT("Bluetooth connection to %012llX failed: %s"),
			Address, Error.message().c_str());
		return nullptr;
	}
}

void Disconnect(FSwitch2BleConnection* Connection)
{
	if (Connection == nullptr)
	{
		return;
	}

	{
		FScopeLock ConnectionsLock(&GConnectionLock);
		GConnections.Remove(Connection);
	}
	{
		// Offer this controller again the next time it advertises: it has just become available.
		FScopeLock ScanLock(&GScanLock);
		GSuppressedUntil.Remove(Connection->Address);
	}

	Connection->MarkDisconnected();

	try
	{
		// Handlers are removed before the objects they capture go away: a notification arriving during
		// teardown would otherwise run against a half-destroyed connection.
		if (Connection->InputCharacteristic != nullptr && Connection->InputToken)
		{
			Connection->InputCharacteristic.ValueChanged(Connection->InputToken);
		}
		if (Connection->CommandResponseCharacteristic != nullptr && Connection->ResponseToken)
		{
			Connection->CommandResponseCharacteristic.ValueChanged(Connection->ResponseToken);
		}
		if (Connection->Device != nullptr && Connection->ConnectionStatusToken)
		{
			Connection->Device.ConnectionStatusChanged(Connection->ConnectionStatusToken);
		}
		if (Connection->Session != nullptr)
		{
			Connection->Session.MaintainConnection(false);
			Connection->Session.Close();
		}
		if (Connection->Device != nullptr)
		{
			Connection->Device.Close();
		}
	}
	catch (const winrt::hresult_error&)
	{
	}

	delete Connection;
}

bool IsConnected(const FSwitch2BleConnection* Connection)
{
	return Connection != nullptr && Connection->bConnected.load();
}

bool SendCommand(FSwitch2BleConnection* Connection, uint8 CommandId, uint8 SubcommandId,
	const uint8* Data, int32 DataLength, TArray<uint8>* OutResponse, int32 TimeoutMs)
{
	if (!IsConnected(Connection) || Connection->CommandCharacteristic == nullptr)
	{
		return false;
	}

	// Eight-byte header, then the payload: [command][0x91][transport][subcommand][0][length][0][0].
	// Transport 0x01 is Bluetooth -- the same command with USB's 0x00 makes the controller go looking for
	// a Bluetooth host instead of answering.
	TArray<uint8> Packet;
	Packet.SetNumZeroed(8 + FMath::Max(DataLength, 0));
	Packet[0] = CommandId;
	Packet[1] = 0x91;
	Packet[2] = 0x01;
	Packet[3] = SubcommandId;
	Packet[5] = static_cast<uint8>(DataLength);
	if (Data != nullptr && DataLength > 0)
	{
		FMemory::Memcpy(Packet.GetData() + 8, Data, DataLength);
	}

	// One command at a time: the response carries no request id, so a second command in flight would make
	// the two answers indistinguishable.
	FScopeLock CommandLock(&Connection->CommandLock);

	{
		FScopeLock ResponseLock(&Connection->ResponseLock);
		Connection->Response.Reset();
	}
	Connection->ResponseEvent->Reset();

	try
	{
		const GattCommunicationStatus Status = Connection->CommandCharacteristic.WriteValueAsync(
			ArrayToBuffer(Packet.GetData(), Packet.Num())).get();
		if (Status != GattCommunicationStatus::Success)
		{
			return false;
		}
	}
	catch (const winrt::hresult_error&)
	{
		return false;
	}

	if (!Connection->ResponseEvent->Wait(FMath::Max(TimeoutMs, 0)))
	{
		return false;
	}

	FScopeLock ResponseLock(&Connection->ResponseLock);
	// Status byte 0x01 is the controller's "accepted". Anything else -- including the empty response left
	// behind when the wait was woken by a disconnect -- is a failure.
	if (Connection->Response.Num() < 8 || Connection->Response[0] != CommandId || Connection->Response[1] != 0x01)
	{
		return false;
	}
	if (OutResponse != nullptr)
	{
		OutResponse->Append(Connection->Response.GetData() + 8, Connection->Response.Num() - 8);
	}
	return true;
}

bool SendVibration(FSwitch2BleConnection* Connection, const uint8* Payload, int32 Length)
{
	if (!IsConnected(Connection) || Connection->VibrationCharacteristic == nullptr || Length <= 0)
	{
		return false;
	}

	try
	{
		// Write without response, and without waiting for the write either. Both matter: an acknowledged
		// write at rumble rate would queue up behind itself and starve the input notifications sharing the
		// link, and blocking on one would stall the caller -- which is the polling thread, the same thread
		// that reads input. A rumble packet that backs up in the radio stack would then stop the controller
		// responding. The next packet is due in 15ms, so a dropped one is not worth waiting for.
		//
		// The completion handler is not optional: an async operation whose failure nobody observes raises
		// its exception on a WinRT thread, where there is no catch to meet it.
		auto Operation = Connection->VibrationCharacteristic.WriteValueAsync(
			ArrayToBuffer(Payload, Length), GattWriteOption::WriteWithoutResponse);
		Operation.Completed([](const winrt::Windows::Foundation::IAsyncOperation<GattCommunicationStatus>&,
			winrt::Windows::Foundation::AsyncStatus) {});
		return true;
	}
	catch (const winrt::hresult_error&)
	{
		return false;
	}
}

int32 ReadInputReport(FSwitch2BleConnection* Connection, uint8* OutBuffer, int32 MaxLength, int32 TimeoutMs)
{
	if (Connection == nullptr)
	{
		return -1;
	}

	for (;;)
	{
		{
			FScopeLock InputLock(&Connection->InputLock);
			if (Connection->InputQueue.Num() > 0)
			{
				const TArray<uint8> Report = MoveTemp(Connection->InputQueue[0]);
				Connection->InputQueue.RemoveAt(0, 1, EAllowShrinking::No);
				const int32 Length = FMath::Min(Report.Num(), MaxLength);
				FMemory::Memcpy(OutBuffer, Report.GetData(), Length);
				return Length;
			}
		}

		// Checked after the queue, not before: reports already delivered are worth reading out even once
		// the link has dropped.
		if (!Connection->bConnected.load())
		{
			return -1;
		}

		if (!Connection->InputEvent->Wait(FMath::Max(TimeoutMs, 0)))
		{
			return 0;
		}
	}
}

bool Bond(FSwitch2BleConnection* Connection, uint64 HostAddress)
{
	if (!IsConnected(Connection))
	{
		return false;
	}

	// Bonding is four commands: the host address the controller should remember, two halves of the link
	// key, and a commit. The keys are fixed values -- the controller stores whatever it is given and uses
	// it to recognise this host later, so they identify the pairing rather than secure it.
	uint8 AddressPayload[14] = { 0x00, 0x02 };
	for (int32 Index = 0; Index < 6; Index++)
	{
		const uint8 Byte = static_cast<uint8>((HostAddress >> (Index * 8)) & 0xFF);
		AddressPayload[2 + Index] = Byte;
		AddressPayload[8 + Index] = Byte;
	}

	static const uint8 LinkKeyFirst[17] = {
		0x00, 0xea, 0xbd, 0x47, 0x13, 0x89, 0x35, 0x42, 0xc6,
		0x79, 0xee, 0x07, 0xf2, 0x53, 0x2c, 0x6c, 0x31 };
	static const uint8 LinkKeySecond[17] = {
		0x00, 0x40, 0xb0, 0x8a, 0x5f, 0xcd, 0x1f, 0x9b, 0x41,
		0x12, 0x5c, 0xac, 0xc6, 0x3f, 0x38, 0xa0, 0x73 };
	static const uint8 Commit[1] = { 0x00 };

	if (!SendCommand(Connection, 0x15, 0x01, AddressPayload, sizeof(AddressPayload)))
	{
		return false;
	}
	if (!SendCommand(Connection, 0x15, 0x04, LinkKeyFirst, sizeof(LinkKeyFirst)))
	{
		return false;
	}
	if (!SendCommand(Connection, 0x15, 0x02, LinkKeySecond, sizeof(LinkKeySecond)))
	{
		return false;
	}
	return SendCommand(Connection, 0x15, 0x03, Commit, sizeof(Commit));
}

void Shutdown()
{
	StopScan();

	TArray<FSwitch2BleConnection*> Remaining;
	{
		FScopeLock ConnectionsLock(&GConnectionLock);
		Remaining = GConnections;
	}
	for (FSwitch2BleConnection* Connection : Remaining)
	{
		Disconnect(Connection);
	}
}

} // namespace Switch2Ble

#else // PLATFORM_WINDOWS && JSL_SWITCH2_BLUETOOTH

// Everywhere else: the plugin builds and the Switch 2 simply has no Bluetooth transport there.
namespace Switch2Ble
{
	bool IsSupported() { return false; }
	uint64 GetHostAddress() { return 0; }
	bool StartScan() { return false; }
	void StopScan() {}
	void SetDiscoveryCallback(TFunction<void()>) {}
	void DrainDiscovered(TArray<FSwitch2BleAdvertisement>&) {}
	FSwitch2BleConnection* Connect(uint64) { return nullptr; }
	void Disconnect(FSwitch2BleConnection*) {}
	bool IsConnected(const FSwitch2BleConnection*) { return false; }
	bool SendCommand(FSwitch2BleConnection*, uint8, uint8, const uint8*, int32, TArray<uint8>*, int32) { return false; }
	bool SendVibration(FSwitch2BleConnection*, const uint8*, int32) { return false; }
	int32 ReadInputReport(FSwitch2BleConnection*, uint8*, int32, int32) { return -1; }
	bool Bond(FSwitch2BleConnection*, uint64) { return false; }
	void Shutdown() {}
}

#endif // PLATFORM_WINDOWS && JSL_SWITCH2_BLUETOOTH
