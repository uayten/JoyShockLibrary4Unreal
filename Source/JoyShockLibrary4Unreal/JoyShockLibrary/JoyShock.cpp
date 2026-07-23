#pragma once

#include "JoyShock.h"
#include "JoyShockLibrary.h"
#include "GamepadMotion.hpp"
#include <bitset>
#include "hidapi.h"
#include <chrono>
#include <thread>
#include <unordered_map>
#include <atomic>
#include "tools.h"
#include <cstring>

#if PLATFORM_WINDOWS
// The Switch 2 Pro Controller takes its commands over the WinUSB (bulk) interface MI_01, not over HID.
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <setupapi.h>
#include <winusb.h>
#include <cfgmgr32.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#ifdef __GNUC__
#define _wcsdup wcsdup
#endif

//// https://docs.microsoft.com/en-us/openspecs/office_protocols/ms-abs/06966aa2-70da-4bf9-8448-3355f277cd77
uint32_t JoyShock::crc_32(unsigned char* buf, int length) {
	uint32_t result = 0xFFFFFFFF;
	int index = 0;
	while (index < length) {
		result = crc_table[(result & 0xFF) ^ buf[index]] ^ (result >> 8);
		index++;
	}
	return result ^ 0xFFFFFFFF;
}

void JoyShock::enable_gyro_ds4_bt(unsigned char *buf, int bufLength)
{
	// gyro is enabled by getting feature report 0x05 on BT controllers.
	// in addition, this request is also responsible for getting current calibration info.
	buf[0] = 0x05; // controller calibration request for BT

	hid_get_feature_report(handle, buf, 41);
	//hid_write(handle, buf, 38);
	//hid_read_timeout(handle, buf, bufLength, 100);
}

// Is this device attached over Bluetooth rather than USB?
//
// The product id cannot answer this: a DS4 v2 and a DualSense report the same product id over both
// transports (0x09CC / 0x0CE6), so the old "is_usb = product_id != DS4_BT" guess marked every Bluetooth
// DS4 v2 as USB. That is not a cosmetic mistake -- the two transports have different report ids and
// payload offsets, so a misidentified controller is parsed with the wrong layout and reports no input at
// all (the DS4's BT reports are 0x11, and the USB parser only accepts 0x01, so every packet is discarded).
//
// Windows puts the Bluetooth HID service class GUID in the device path of every Bluetooth HID device, and
// spells the ids as "_VID&0002xxxx_PID&xxxx" instead of USB's "VID_xxxx&PID_xxxx", so the path is a
// reliable and cheap answer. Non-Windows platforms fall through to the report-id probe below, which is
// where this used to be decided.
static bool IsBluetoothHidPath(const FString& InPath)
{
	// Bluetooth HID (HIDP) service class UUID -- present in the path of every Windows Bluetooth HID device.
	return InPath.Contains(TEXT("{00001124-0000-1000-8000-00805f9b34fb}"))
		|| InPath.Contains(TEXT("_VID&"));
}

void JoyShock::init(struct hid_device_info *dev, hid_device* inHandle, int uniqueHandle, const FString &inPath) {
	this->path = inPath;

	const bool bIsBluetoothPath = IsBluetoothHidPath(inPath);

	if (dev->product_id == JOYCON_CHARGING_GRIP) {

		if (dev->interface_number == 0 || dev->interface_number == -1) {
			this->name = TEXT("Joy-Con (R)");
			this->left_right = 2;// right joycon
			this->is_usb = true;
		}
		else if (dev->interface_number == 1) {
			this->name = TEXT("Joy-Con (L)");
			this->left_right = 1;// left joycon
			this->is_usb = true;
		}
	}

	if (dev->product_id == JOYCON_L_BT) {
		this->name = TEXT("Joy-Con (L)");
		this->left_right = 1;// left joycon
	}
	else if (dev->product_id == JOYCON_R_BT) {
		this->name = TEXT("Joy-Con (R)");
		this->left_right = 2;// right joycon
	}
	else if (dev->product_id == PRO_CONTROLLER) {
		this->name = TEXT("Pro Controller");
		this->left_right = 3;// left joycon
	}
	else if (dev->product_id == PRO_CONTROLLER_2) {
		// Switch 2 Pro Controller, enumerated here over USB.
		this->name = TEXT("Pro Controller 2");
		this->left_right = 3;// treated like a Pro Controller (both halves)
		this->is_usb = true;
		this->is_switch2_pro = true;
	}

	if (dev->product_id == DS4_BT ||
		dev->product_id == DS4_USB ||
		dev->product_id == DS4_USB_DONGLE ||
		dev->product_id == DS4_USB_V2) {
		this->name = TEXT("DualShock 4");
		this->left_right = 3; // left and right?
		this->controller_type = ControllerType::s_ds4;
		this->is_usb = (dev->product_id != DS4_BT) && !bIsBluetoothPath;
	}
	
	if (dev->product_id == BROOK_DS4_USB) {
		this->name = TEXT("DualShock 4");
		this->left_right = 3; // left and right?
		this->controller_type = ControllerType::s_ds4;
		this->is_usb = true; // this controller is wired
	}

	if (dev->product_id == DS_USB ||
		dev->product_id == DS_USB_V2) {
		this->name = TEXT("DualSense");
		this->left_right = 3; // left and right?
		this->controller_type = ControllerType::s_ds;
		this->is_usb = !bIsBluetoothPath;
	}

	this->serial = _wcsdup(dev->serial_number);
	this->intHandle = uniqueHandle;

	//UE_LOG(LogJoyShockLibrary, Log, TEXT("Found device %c: %ls %s\n"), L_OR_R(this->left_right), this->serial, dev->path);
	this->handle = inHandle;

	// Ask a PlayStation controller for its calibration, which is also what switches it into the full report
	// mode that carries the IMU and touchpad, then confirm the transport from the report id it answers with.
	//
	// This probe can only ever move a device to Bluetooth, never back to USB: it is a fallback for platforms
	// where the path tells us nothing, and it is inherently racy (the controller does not necessarily emit
	// its first full report within the timeout). Letting it decide "USB" on a timeout is what left Bluetooth
	// controllers parsed with the USB layout. Note also that enable_gyro_ds4_bt overwrites the buffer with
	// the feature report, so the buffer has to be cleared and the read's return value checked -- otherwise a
	// timed-out read leaves the feature report's own id sitting in buf[0].
	if (this->controller_type == ControllerType::s_ds4 || this->controller_type == ControllerType::s_ds) {
		unsigned char buf[64];
		memset(buf, 0, 64);

		// The DS's protocol is literally so similar to the DS4 that we can reuse the same reports to get the
		// same results. Meet the new boss - the same as the old boss.
		enable_gyro_ds4_bt(buf, 64);

		memset(buf, 0, 64);
		const int res = hid_read_timeout(handle, buf, 64, 100);

		const unsigned char bluetoothReportId = (this->controller_type == ControllerType::s_ds4) ? 0x11 : 0x31;
		if (res > 0 && buf[0] == bluetoothReportId) {
			this->is_usb = false;
		}
	}

	UE_LOG(LogJoyShockLibrary, Log, TEXT("\t%s detected on %s"), *this->name, this->is_usb ? TEXT("USB") : TEXT("Bluetooth"));
}

JoyShock::JoyShock(struct hid_device_info* dev, hid_device* inHandle, int uniqueHandle, const FString& inPath) {
	init(dev, inHandle, uniqueHandle, inPath);

	// initialise continuous calibration windows
	reset_continuous_calibration();
}

JoyShock::~JoyShock() {
	if (handle != nullptr) {
		hid_close(handle);
	}

#if PLATFORM_WINDOWS
	// Force the same close path used by the polling thread.
	sw2_last_command_time = {};
	release_sw2_command_interface_if_idle();
#endif
}

void JoyShock::push_cumulative_gyro(float gyroX, float gyroY, float gyroZ) {
	modifying_lock.Lock();
	if (num_cumulative_gyro_samples == 0) {
		cumulative_gyro_x = 0.f;
		cumulative_gyro_y = 0.f;
		cumulative_gyro_z = 0.f;
	}
	cumulative_gyro_x += gyroX;
	cumulative_gyro_y += gyroY;
	cumulative_gyro_z += gyroZ;
	num_cumulative_gyro_samples++;
	modifying_lock.Unlock();
}

void JoyShock::get_and_flush_cumulative_gyro(float& gyroX, float& gyroY, float& gyroZ) {
	modifying_lock.Lock();
	if (num_cumulative_gyro_samples == 0) {
		gyroX = cumulative_gyro_x;
		gyroX = cumulative_gyro_y;
		gyroX = cumulative_gyro_z;
	}
	else {
		gyroX = cumulative_gyro_x / num_cumulative_gyro_samples;
		gyroY = cumulative_gyro_y / num_cumulative_gyro_samples;
		gyroZ = cumulative_gyro_z / num_cumulative_gyro_samples;
		num_cumulative_gyro_samples = 0;
		// so that we don't return zeroes before we receive a new sample, store this for next time:
		cumulative_gyro_x = gyroX;
		cumulative_gyro_y = gyroY;
		cumulative_gyro_z = gyroZ;
	}
	float gravX, gravY, gravZ;
	motion.GetGravity(gravX, gravY, gravZ);
	modifying_lock.Unlock();
	switch (gyroSpace)
	{
	default:
	case 0:
		break;
	case 1:
		GamepadMotion::CalculateWorldSpaceGyro(gyroX, gyroY, gyroX, gyroY, gyroZ, gravX, gravY, gravZ);
		gyroZ = 0.f;
		break;
	case 2:
		GamepadMotion::CalculatePlayerSpaceGyro(gyroX, gyroY, gyroX, gyroY, gyroZ, gravX, gravY, gravZ);
		gyroZ = 0.f;
		break;
	}
}

void JoyShock::reset_continuous_calibration() {
	modifying_lock.Lock();
	motion.ResetContinuousCalibration();
	modifying_lock.Unlock();
}

void JoyShock::push_sensor_samples(float gyroX, float gyroY, float gyroZ, float accelX, float accelY, float accelZ, float deltaTime) {
	motion.ProcessMotion(gyroX, gyroY, gyroZ, accelX, accelY, accelZ, deltaTime);
}

void JoyShock::get_calibrated_gyro(float& gyroX, float& gyroY, float& gyroZ)
{
	motion.GetCalibratedGyro(gyroX, gyroY, gyroZ);
}

FMotionState JoyShock::get_motion_state()
{
	FMotionState motionState = FMotionState();
	modifying_lock.Lock();
	motion.GetProcessedAcceleration(motionState.accelX, motionState.accelY, motionState.accelZ);
	motion.GetOrientation(motionState.quatW, motionState.quatX, motionState.quatY, motionState.quatZ);
	motion.GetRawOrientation(motionState.rawQuatW, motionState.rawQuatX, motionState.rawQuatY, motionState.rawQuatZ);
	motion.GetGravity(motionState.gravX, motionState.gravY, motionState.gravZ);
	modifying_lock.Unlock();
	return motionState;
}

FIMUState JoyShock::get_transformed_imu_state(FIMUState& InIMUState)
{
	float gyroX, gyroY, gyroZ, gravX, gravY, gravZ;
	modifying_lock.Lock();
	motion.GetGravity(gravX, gravY, gravZ);
	gyroX = InIMUState.gyroX;
	gyroY = InIMUState.gyroY;
	gyroZ = InIMUState.gyroZ;
	modifying_lock.Unlock();
	switch (gyroSpace)
	{
	default:
	case 0:
		break;
	case 1:
		GamepadMotion::CalculateWorldSpaceGyro(gyroX, gyroY, gyroX, gyroY, gyroZ, gravX, gravY, gravZ);
		gyroZ = 0.f;
		break;
	case 2:
		GamepadMotion::CalculatePlayerSpaceGyro(gyroX, gyroY, gyroX, gyroY, gyroZ, gravX, gravY, gravZ);
		gyroZ = 0.f;
		break;
	}
	FIMUState transformedState = FIMUState();
	transformedState.accelX = InIMUState.accelX;
	transformedState.accelY = InIMUState.accelY;
	transformedState.accelZ = InIMUState.accelZ;
	transformedState.gyroX = gyroX;
	transformedState.gyroY = gyroY;
	transformedState.gyroZ = gyroZ;
	return transformedState;
}

bool JoyShock::hid_exchange(hid_device *InHandle, unsigned char *buf, int len) {
	if (!InHandle) return false;

	int res;

	res = hid_write(InHandle, buf, len);

	res = hid_read_timeout(InHandle, buf, 0x40, 1000);
	if (res == 0)
	{
		return false;
	}
	return true;
}


bool JoyShock::send_command(int command, uint8_t *data, int len) {
	unsigned char buf[0x40];
	memset(buf, 0, 0x40);

	if (is_usb) {
		buf[0x00] = 0x80;
		buf[0x01] = 0x92;
		buf[0x03] = 0x31;
	}

	buf[is_usb ? 0x8 : 0x0] = command;
	if (data != nullptr && len != 0) {
		memcpy(buf + (is_usb ? 0x9 : 0x1), data, len);
	}

	if (!hid_exchange(this->handle, buf, len + (is_usb ? 0x9 : 0x1)))
	{
		return false;
	}

	if (data) {
		memcpy(data, buf, 0x40);
	}
	return true;
}

bool JoyShock::send_subcommand(int command, int subcommand, uint8_t *data, int len) {
	unsigned char buf[0x40];
	memset(buf, 0, 0x40);

	uint8_t rumble_base[9] = { std::uint8_t((++global_count) & 0xF), 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40 };
	memcpy(buf, rumble_base, 9);

	if (global_count > 0xF) {
		global_count = 0x0;
	}

	// set neutral rumble base only if the command is vibrate (0x01)
	// if set when other commands are set, might cause the command to be misread and not executed
	//if (subcommand == 0x01) {
	//	uint8_t rumble_base[9] = { (++global_count) & 0xF, 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40 };
	//	memcpy(buf + 10, rumble_base, 9);
	//}

	buf[9] = subcommand;
	if (data && len != 0) {
		memcpy(buf + 10, data, len);
	}

	if (!send_command(command, buf, 10 + len))
	{
		return false;
	}

	if (data) {
		memcpy(data, buf, 0x40); //TODO
	}
	return true;
}

void JoyShock::rumble(int frequency, int intensity) {

	unsigned char buf[0x400];
	memset(buf, 0, 0x40);

	// intensity: (0, 8)
	// frequency: (0, 255)

	//	 X	AA	BB	 Y	CC	DD
	//[0 1 x40 x40 0 1 x40 x40] is neutral.


	//for (int j = 0; j <= 8; j++) {
	//	buf[1 + intensity] = 0x1;//(i + j) & 0xFF;
	//}

	buf[1 + 0 + intensity] = 0x1;
	buf[1 + 4 + intensity] = 0x1;

	// Set frequency to increase
	if (this->left_right == 1) {
		buf[1 + 0] = frequency;// (0, 255)
	}
	else {
		buf[1 + 4] = frequency;// (0, 255)
	}

	// set non-blocking:
	hid_set_nonblocking(this->handle, 1);

	send_command(0x10, (uint8_t*)buf, 0x9);
}

bool JoyShock::get_switch_controller_info() {
	bool result = false;

	memset(factory_stick_cal, 0, 0x12);
	memset(device_colours, 0, 0xC);
	memset(user_stick_cal, 0, 0x16);
	memset(sensor_model, 0, 0x6);
	memset(stick_model, 0, 0x12);
	memset(factory_sensor_cal, 0, 0x18);
	memset(user_sensor_cal, 0, 0x1A);
	memset(factory_sensor_cal_calm, 0, 0xC);
	memset(user_sensor_cal_calm, 0, 0xC);
	memset(sensor_cal, 0, sizeof(sensor_cal));
	memset(stick_cal_x_l, 0, sizeof(stick_cal_x_l));
	memset(stick_cal_y_l, 0, sizeof(stick_cal_y_l));
	memset(stick_cal_x_r, 0, sizeof(stick_cal_x_r));
	memset(stick_cal_y_r, 0, sizeof(stick_cal_y_r));


	// These reads used to be all-or-nothing: eight SPI transactions in a row, any one of which failed the
	// whole init -- and with it vibration, the IMU, the report mode and the player LED, since those are set
	// up by the caller before this point. At ~95% per read over a busy Bluetooth link that is 0.95^8, about
	// two connects in three succeeding, which matches the observed flakiness.
	//
	// Only the factory stick calibration is actually load-bearing; without it the sticks have no range.
	// Everything else degrades gracefully, so it warns and carries on: the sensor calibration falls back to
	// the uncalibrated coefficients computed below, the colours are cosmetic, and the two user-calibration
	// blocks are optional anyway (the code below only uses them if they carry their own 0xA1B2 magic, and a
	// failed read leaves them zeroed by the memsets above).
	//
	// The old reads of sensor_model (0x6080) and stick_model (0x6086, 0x6098) are gone entirely: nothing
	// ever read those buffers back, so they were three failure opportunities for data we discard.
	if (!get_spi_data(0x6020, 0x18, factory_sensor_cal))
	{
		UE_LOG(LogJoyShockLibrary, Warning, TEXT("Controller %d: couldn't read factory sensor calibration; continuing with an uncalibrated gyro/accelerometer.\n"), intHandle);
	}
	if (!get_spi_data(0x603D, 0x12, factory_stick_cal))
	{
		UE_LOG(LogJoyShockLibrary, Warning, TEXT("Controller %d: couldn't read factory stick calibration; failing init so it gets retried (the sticks would have no range).\n"), intHandle);
		return false;
	}
	if (!get_spi_data(0x6050, 0xC, device_colours))
	{
		UE_LOG(LogJoyShockLibrary, Warning, TEXT("Controller %d: couldn't read colours; continuing.\n"), intHandle);
	}
	if (!get_spi_data(0x8010, 0x16, user_stick_cal))
	{
		UE_LOG(LogJoyShockLibrary, Warning, TEXT("Controller %d: couldn't read user stick calibration; using the factory values.\n"), intHandle);
	}
	if (!get_spi_data(0x8026, 0x1A, user_sensor_cal))
	{
		UE_LOG(LogJoyShockLibrary, Warning, TEXT("Controller %d: couldn't read user sensor calibration; using the factory values.\n"), intHandle);
	}


	// get stick calibration data:

	// factory calibration:

	if (this->left_right == 1 || this->left_right == 3) {
		stick_cal_x_l[1] = (factory_stick_cal[4] << 8) & 0xF00 | factory_stick_cal[3];
		stick_cal_y_l[1] = (factory_stick_cal[5] << 4) | (factory_stick_cal[4] >> 4);
		stick_cal_x_l[0] = stick_cal_x_l[1] - ((factory_stick_cal[7] << 8) & 0xF00 | factory_stick_cal[6]);
		stick_cal_y_l[0] = stick_cal_y_l[1] - ((factory_stick_cal[8] << 4) | (factory_stick_cal[7] >> 4));
		stick_cal_x_l[2] = stick_cal_x_l[1] + ((factory_stick_cal[1] << 8) & 0xF00 | factory_stick_cal[0]);
		stick_cal_y_l[2] = stick_cal_y_l[1] + ((factory_stick_cal[2] << 4) | (factory_stick_cal[2] >> 4));

	}

	if (this->left_right == 2 || this->left_right == 3) {
		stick_cal_x_r[1] = (factory_stick_cal[10] << 8) & 0xF00 | factory_stick_cal[9];
		stick_cal_y_r[1] = (factory_stick_cal[11] << 4) | (factory_stick_cal[10] >> 4);
		stick_cal_x_r[0] = stick_cal_x_r[1] - ((factory_stick_cal[13] << 8) & 0xF00 | factory_stick_cal[12]);
		stick_cal_y_r[0] = stick_cal_y_r[1] - ((factory_stick_cal[14] << 4) | (factory_stick_cal[13] >> 4));
		stick_cal_x_r[2] = stick_cal_x_r[1] + ((factory_stick_cal[16] << 8) & 0xF00 | factory_stick_cal[15]);
		stick_cal_y_r[2] = stick_cal_y_r[1] + ((factory_stick_cal[17] << 4) | (factory_stick_cal[16] >> 4));

	}


	// if there is user calibration data:
	if ((user_stick_cal[0] | user_stick_cal[1] << 8) == 0xA1B2) {
		stick_cal_x_l[1] = (user_stick_cal[6] << 8) & 0xF00 | user_stick_cal[5];
		stick_cal_y_l[1] = (user_stick_cal[7] << 4) | (user_stick_cal[6] >> 4);
		stick_cal_x_l[0] = stick_cal_x_l[1] - ((user_stick_cal[9] << 8) & 0xF00 | user_stick_cal[8]);
		stick_cal_y_l[0] = stick_cal_y_l[1] - ((user_stick_cal[10] << 4) | (user_stick_cal[9] >> 4));
		stick_cal_x_l[2] = stick_cal_x_l[1] + ((user_stick_cal[3] << 8) & 0xF00 | user_stick_cal[2]);
		stick_cal_y_l[2] = stick_cal_y_l[1] + ((user_stick_cal[4] << 4) | (user_stick_cal[3] >> 4));
		//FormJoy::myform1->textBox_lstick_ucal->Text = String::Format(L"L Stick User:\r\nCenter X,Y: ({0:X3}, {1:X3})\r\nX: [{2:X3} - {4:X3}] Y: [{3:X3} - {5:X3}]",
		//stick_cal_x_l[1], stick_cal_y_l[1], stick_cal_x_l[0], stick_cal_y_l[0], stick_cal_x_l[2], stick_cal_y_l[2]);
	}
	else {
		//FormJoy::myform1->textBox_lstick_ucal->Text = L"L Stick User:\r\nNo calibration";
		//UE_LOG(LogJoyShockLibrary, Log, TEXT("no user Calibration data for left stick.\n"));
	}

	if ((user_stick_cal[0xB] | user_stick_cal[0xC] << 8) == 0xA1B2) {
		stick_cal_x_r[1] = (user_stick_cal[14] << 8) & 0xF00 | user_stick_cal[13];
		stick_cal_y_r[1] = (user_stick_cal[15] << 4) | (user_stick_cal[14] >> 4);
		stick_cal_x_r[0] = stick_cal_x_r[1] - ((user_stick_cal[17] << 8) & 0xF00 | user_stick_cal[16]);
		stick_cal_y_r[0] = stick_cal_y_r[1] - ((user_stick_cal[18] << 4) | (user_stick_cal[17] >> 4));
		stick_cal_x_r[2] = stick_cal_x_r[1] + ((user_stick_cal[20] << 8) & 0xF00 | user_stick_cal[19]);
		stick_cal_y_r[2] = stick_cal_y_r[1] + ((user_stick_cal[21] << 4) | (user_stick_cal[20] >> 4));
		//FormJoy::myform1->textBox_rstick_ucal->Text = String::Format(L"R Stick User:\r\nCenter X,Y: ({0:X3}, {1:X3})\r\nX: [{2:X3} - {4:X3}] Y: [{3:X3} - {5:X3}]",
		//stick_cal_x_r[1], stick_cal_y_r[1], stick_cal_x_r[0], stick_cal_y_r[0], stick_cal_x_r[2], stick_cal_y_r[2]);
	}
	else {
		//FormJoy::myform1->textBox_rstick_ucal->Text = L"R Stick User:\r\nNo calibration";
		//UE_LOG(LogJoyShockLibrary, Log, TEXT("no user Calibration data for right stick.\n"));
	}

	// get gyro / accelerometer calibration data:

	// factory calibration:

	// Acc cal origin position
	sensor_cal[0][0] = uint16_to_int16(factory_sensor_cal[0] | factory_sensor_cal[1] << 8);
	sensor_cal[0][1] = uint16_to_int16(factory_sensor_cal[2] | factory_sensor_cal[3] << 8);
	sensor_cal[0][2] = uint16_to_int16(factory_sensor_cal[4] | factory_sensor_cal[5] << 8);

	// Gyro cal origin position
	sensor_cal[1][0] = uint16_to_int16(factory_sensor_cal[0xC] | factory_sensor_cal[0xD] << 8);
	sensor_cal[1][1] = uint16_to_int16(factory_sensor_cal[0xE] | factory_sensor_cal[0xF] << 8);
	sensor_cal[1][2] = uint16_to_int16(factory_sensor_cal[0x10] | factory_sensor_cal[0x11] << 8);


	//hex_dump(user_sensor_cal, 0x14);

	// user calibration:
	if ((user_sensor_cal[0x0] | user_sensor_cal[0x1] << 8) == 0xA1B2) {
		//UE_LOG(LogJoyShockLibrary, Log, TEXT("User calibration available\n"));
		//if (true) {
		//FormJoy::myform1->textBox_6axis_ucal->Text = L"6-Axis User (XYZ):\r\nAcc:  ";
		//for (int i = 0; i < 0xC; i = i + 6) {
		//	FormJoy::myform1->textBox_6axis_ucal->Text += String::Format(L"{0:X4} {1:X4} {2:X4}\r\n      ",
		//		user_sensor_cal[i + 2] | user_sensor_cal[i + 3] << 8,
		//		user_sensor_cal[i + 4] | user_sensor_cal[i + 5] << 8,
		//		user_sensor_cal[i + 6] | user_sensor_cal[i + 7] << 8);
		//}
		// Acc cal origin position
		sensor_cal[0][0] = uint16_to_int16(user_sensor_cal[2] | user_sensor_cal[3] << 8);
		sensor_cal[0][1] = uint16_to_int16(user_sensor_cal[4] | user_sensor_cal[5] << 8);
		sensor_cal[0][2] = uint16_to_int16(user_sensor_cal[6] | user_sensor_cal[7] << 8);
		//FormJoy::myform1->textBox_6axis_ucal->Text += L"\r\nGyro: ";
		//for (int i = 0xC; i < 0x18; i = i + 6) {
		//	FormJoy::myform1->textBox_6axis_ucal->Text += String::Format(L"{0:X4} {1:X4} {2:X4}\r\n      ",
		//		user_sensor_cal[i + 2] | user_sensor_cal[i + 3] << 8,
		//		user_sensor_cal[i + 4] | user_sensor_cal[i + 5] << 8,
		//		user_sensor_cal[i + 6] | user_sensor_cal[i + 7] << 8);
		//}
		// Gyro cal origin position
		sensor_cal[1][0] = uint16_to_int16(user_sensor_cal[0xE] | user_sensor_cal[0xF] << 8);
		sensor_cal[1][1] = uint16_to_int16(user_sensor_cal[0x10] | user_sensor_cal[0x11] << 8);
		sensor_cal[1][2] = uint16_to_int16(user_sensor_cal[0x12] | user_sensor_cal[0x13] << 8);
	}
	else {
		//FormJoy::myform1->textBox_6axis_ucal->Text = L"\r\n\r\nUser:\r\nNo calibration";
	}

	// Internal scaling and unit conversions as per https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/imu_sensor_notes.md
	// Use SPI calibration and convert them to Gs
	acc_cal_coeff[0] = (float)(1.0 / (float)(16384 - uint16_to_int16(sensor_cal[0][0]))) * 4.0f;
	acc_cal_coeff[1] = (float)(1.0 / (float)(16384 - uint16_to_int16(sensor_cal[0][1]))) * 4.0f;
	acc_cal_coeff[2] = (float)(1.0 / (float)(16384 - uint16_to_int16(sensor_cal[0][2]))) * 4.0f;

	// Use SPI calibration and convert them to degrees per second
	gyro_cal_coeff[0] = (float)(936.0 / (float)(13371 - uint16_to_int16(sensor_cal[1][0])));
	gyro_cal_coeff[1] = (float)(936.0 / (float)(13371 - uint16_to_int16(sensor_cal[1][1])));
	gyro_cal_coeff[2] = (float)(936.0 / (float)(13371 - uint16_to_int16(sensor_cal[1][2])));

	// Device colours
	body_colour =
		(((int)device_colours[0]) << 16) +
		(((int)device_colours[1]) << 8) +
		(((int)device_colours[2]));
	button_colour =
		(((int)device_colours[3]) << 16) +
		(((int)device_colours[4]) << 8) +
		(((int)device_colours[5]));
	left_grip_colour =
		(((int)device_colours[6]) << 16) +
		(((int)device_colours[7]) << 8) +
		(((int)device_colours[8]));
	right_grip_colour =
		(((int)device_colours[9]) << 16) +
		(((int)device_colours[10]) << 8) +
		(((int)device_colours[11]));

	UE_LOG(LogJoyShockLibrary, Log, TEXT("Body: %#08x; Buttons: %#08x; Left Grip: %#08x; Right Grip: %#08x;\n"),
		body_colour,
		button_colour,
		left_grip_colour,
		right_grip_colour);

	//hex_dump(reinterpret_cast<unsigned char*>(sensor_cal[0]), 6);
	//hex_dump(reinterpret_cast<unsigned char*>(sensor_cal[1]), 6);

	return true;
}

void JoyShock::enable_IMU(unsigned char *buf, int bufLength) {
	memset(buf, 0, bufLength);

	// Enable IMU data
	UE_LOG(LogJoyShockLibrary, Log, TEXT("Enabling IMU data on controller %d (%s)...\n"), this->intHandle, *this->name);
	if (controller_type == ControllerType::s_ds4)
	{
		// These two branches used to be the wrong way round. It went unnoticed because is_usb was itself
		// inverted for every Bluetooth DS4 (see IsBluetoothHidPath), so the two mistakes cancelled out; now
		// that the transport is detected properly, re-running the init for the wrong transport here would
		// leave a Bluetooth controller silent.
		if (is_usb)
		{
			init_ds4_usb();
		}
		else
		{
			init_ds4_bt();
			enable_gyro_ds4_bt(buf, bufLength);
		}
	}
	else
	{
		buf[0] = 0x01; // Enabled
		send_subcommand(0x1, 0x40, buf, 1);
	}
}

bool JoyShock::init_usb() {
	unsigned char buf[0x400];
	memset(buf, 0, 0x400);

	// set blocking:
	// this insures we get the MAC Address
	hid_set_nonblocking(this->handle, 0);

	//Get MAC Left
	UE_LOG(LogJoyShockLibrary, Log, TEXT("Getting MAC...\n"));
	memset(buf, 0x00, 0x40);
	buf[0] = 0x80;
	buf[1] = 0x01;
	hid_exchange(this->handle, buf, 0x2);

	//if (buf[2] == 0x3) {
	//	UE_LOG(LogJoyShockLibrary, Log, TEXT("%s disconnected!\n", this->name.c_str()));
	//}
	//else {
	//	UE_LOG(LogJoyShockLibrary, Log, TEXT("Found %s, MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", this->name.c_str(), buf[9], buf[8], buf[7], buf[6], buf[5], buf[4]));
	//}

	// set non-blocking:
	//hid_set_nonblocking(jc->handle, 1);

	// Do handshaking
	UE_LOG(LogJoyShockLibrary, Log, TEXT("Doing handshake...\n"));
	memset(buf, 0x00, 0x40);
	buf[0] = 0x80;
	buf[1] = 0x02;
	hid_exchange(this->handle, buf, 0x2);

	// Switch baudrate to 3Mbit
	UE_LOG(LogJoyShockLibrary, Log, TEXT("Switching baudrate...\n"));
	memset(buf, 0x00, 0x40);
	buf[0] = 0x80;
	buf[1] = 0x03;
	hid_exchange(this->handle, buf, 0x2);

	//Do handshaking again at new baudrate so the firmware pulls pin 3 low?
	UE_LOG(LogJoyShockLibrary, Log, TEXT("Doing handshake...\n"));
	memset(buf, 0x00, 0x40);
	buf[0] = 0x80;
	buf[1] = 0x02;
	hid_exchange(this->handle, buf, 0x2);

	//Only talk HID from now on
	UE_LOG(LogJoyShockLibrary, Log, TEXT("Only talk HID...\n"));
	memset(buf, 0x00, 0x40);
	buf[0] = 0x80;
	buf[1] = 0x04;
	hid_exchange(this->handle, buf, 0x2);

	// Enable vibration
	UE_LOG(LogJoyShockLibrary, Log, TEXT("Enabling vibration...\n"));
	memset(buf, 0x00, 0x400);
	buf[0] = 0x01; // Enabled
	send_subcommand(0x1, 0x48, buf, 1);

	enable_IMU(buf, 0x400);

	UE_LOG(LogJoyShockLibrary, Log, TEXT("Getting calibration data...\n"));
	bool result = get_switch_controller_info();

	if (result)
	{
		UE_LOG(LogJoyShockLibrary, Log, TEXT("Successfully initialized %s!\n"), *this->name);
	}
	else
	{
		UE_LOG(LogJoyShockLibrary, Log, TEXT("Could not initialise %s! Will try again later.\n"), *this->name);
	}
	return result;
}

#if PLATFORM_WINDOWS
// Walks up the device tree from a device node and returns the instance id of the composite USB parent
// (the node whose id has the controller's VID/PID but no interface suffix), or empty if not found.
static FString Sw2GetCompositeParentId(DEVINST InDevInst)
{
	DEVINST current = InDevInst;
	for (int depth = 0; depth < 4; depth++)
	{
		DEVINST parent = 0;
		if (CM_Get_Parent(&parent, current, 0) != CR_SUCCESS)
		{
			break;
		}
		WCHAR idBuffer[MAX_DEVICE_ID_LEN];
		if (CM_Get_Device_IDW(parent, idBuffer, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
		{
			break;
		}
		FString id(idBuffer);
		if (id.Contains(TEXT("VID_057E")) && id.Contains(TEXT("PID_2069")) && !id.Contains(TEXT("&MI_")))
		{
			return id;
		}
		current = parent;
	}
	return FString();
}

// Resolves the composite USB parent id for a device *interface path* (e.g. this JoyShock's HID path),
// so a controller's HID (MI_00) and WinUSB (MI_01) interfaces can be matched to the same physical unit.
static FString Sw2GetCompositeParentIdForInterfacePath(const FString& InterfacePath)
{
	FString result;
	HDEVINFO devInfo = SetupDiCreateDeviceInfoList(nullptr, nullptr);
	if (devInfo == INVALID_HANDLE_VALUE)
	{
		return result;
	}

	SP_DEVICE_INTERFACE_DATA ifData;
	ifData.cbSize = sizeof(ifData);
	if (SetupDiOpenDeviceInterfaceW(devInfo, *InterfacePath, 0, &ifData))
	{
		DWORD requiredSize = 0;
		SP_DEVINFO_DATA devData;
		devData.cbSize = sizeof(devData);
		devData.DevInst = 0;
		// Sizing call; fails with insufficient-buffer but still fills devData with the owning devnode.
		SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &requiredSize, &devData);
		if (devData.DevInst != 0)
		{
			result = Sw2GetCompositeParentId(devData.DevInst);
		}
	}
	SetupDiDestroyDeviceInfoList(devInfo);
	return result;
}
#endif

bool JoyShock::sw2_open_winusb() {
#if PLATFORM_WINDOWS
	// Device interface GUID exposed by the controller's own MS OS descriptor for its WinUSB interface
	// (registry: Enum\USB\VID_057E&PID_2069&MI_01\...\Device Parameters\DeviceInterfaceGUID). Firmware-
	// defined, so it is the same on every machine.
	static const GUID Sw2WinUsbGuid = { 0x6F13725E, 0xEF0E, 0x4FD3, { 0xAE, 0x5F, 0xB2, 0xDE, 0x98, 0x9E, 0xC8, 0x25 } };

	HDEVINFO devInfo = SetupDiGetClassDevsW(&Sw2WinUsbGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (devInfo == INVALID_HANDLE_VALUE)
	{
		UE_LOG(LogJoyShockLibrary, Warning, TEXT("SW2: SetupDiGetClassDevs failed (%u)\n"), GetLastError());
		return false;
	}

	// Composite USB parent of this JoyShock's HID interface: used to pick the WinUSB interface belonging
	// to the SAME physical controller when several Pro Controller 2s are connected.
	const FString MyCompositeId = Sw2GetCompositeParentIdForInterfacePath(this->path);

	bool bOpened = false;
	SP_DEVICE_INTERFACE_DATA ifData;
	ifData.cbSize = sizeof(ifData);
	for (DWORD index = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &Sw2WinUsbGuid, index, &ifData); index++)
	{
		DWORD requiredSize = 0;
		SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &requiredSize, nullptr);
		if (requiredSize == 0)
		{
			continue;
		}

		TArray<uint8> detailBuffer;
		detailBuffer.SetNumZeroed(requiredSize);
		PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuffer.GetData());
		detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
		SP_DEVINFO_DATA devData;
		devData.cbSize = sizeof(devData);
		devData.DevInst = 0;
		if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, requiredSize, nullptr, &devData))
		{
			continue;
		}

		FString devicePath(detail->DevicePath);
		if (!devicePath.Contains(TEXT("vid_057e")) || !devicePath.Contains(TEXT("pid_2069")))
		{
			continue;
		}

		// Match this WinUSB interface to the same physical controller as our HID interface (shared
		// composite parent). If the HID side couldn't be resolved, fall back to first-match.
		if (!MyCompositeId.IsEmpty() && devData.DevInst != 0)
		{
			const FString CandidateCompositeId = Sw2GetCompositeParentId(devData.DevInst);
			if (!CandidateCompositeId.Equals(MyCompositeId, ESearchCase::IgnoreCase))
			{
				continue;
			}
		}

		HANDLE fileHandle = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
		if (fileHandle == INVALID_HANDLE_VALUE)
		{
			const DWORD openError = GetLastError();
			if (openError == ERROR_ACCESS_DENIED)
			{
				++sw2_access_denied_count;
				if (sw2_access_denied_count >= 3 && !sw2_access_warning_logged)
				{
					UE_LOG(LogJoyShockLibrary, Warning,
						TEXT("SW2: the controller command interface remains owned by another process after multiple retries. ")
						TEXT("This can be another Unreal instance, Steam, or another controller tool."));
					sw2_access_warning_logged = true;
				}
			}
			else
			{
				UE_LOG(LogJoyShockLibrary, Warning, TEXT("SW2: CreateFile failed (%u) for %s\n"), openError, *devicePath);
			}
			continue;
		}

		WINUSB_INTERFACE_HANDLE usbHandle = nullptr;
		if (!WinUsb_Initialize(fileHandle, &usbHandle))
		{
			UE_LOG(LogJoyShockLibrary, Warning, TEXT("SW2: WinUsb_Initialize failed (%u)\n"), GetLastError());
			CloseHandle(fileHandle);
			continue;
		}

		// Find the bulk pipes (capture shows OUT=0x02, IN=0x82; query them to be safe).
		UCHAR outPipe = 0x02, inPipe = 0x82;
		USB_INTERFACE_DESCRIPTOR ifaceDesc;
		if (WinUsb_QueryInterfaceSettings(usbHandle, 0, &ifaceDesc))
		{
			for (UCHAR p = 0; p < ifaceDesc.bNumEndpoints; p++)
			{
				WINUSB_PIPE_INFORMATION pipeInfo;
				if (WinUsb_QueryPipe(usbHandle, 0, p, &pipeInfo) && pipeInfo.PipeType == UsbdPipeTypeBulk)
				{
					if (pipeInfo.PipeId & 0x80) { inPipe = pipeInfo.PipeId; }
					else { outPipe = pipeInfo.PipeId; }
				}
			}
		}

		// Bound every transfer on both pipes: these handles stay open for the device's lifetime and rumble
		// runs on the game thread, so an unbounded WritePipe/ReadPipe could otherwise hang the editor.
		ULONG timeoutMs = 200;
		WinUsb_SetPipePolicy(usbHandle, inPipe, PIPE_TRANSFER_TIMEOUT, sizeof(timeoutMs), &timeoutMs);
		WinUsb_SetPipePolicy(usbHandle, outPipe, PIPE_TRANSFER_TIMEOUT, sizeof(timeoutMs), &timeoutMs);

		// Keep the command interface open for the device's lifetime (closed in the destructor).
		sw2_winusb_file = fileHandle;
		sw2_winusb_handle = usbHandle;
		sw2_out_pipe = outPipe;
		sw2_in_pipe = inPipe;
		sw2_last_command_time = std::chrono::steady_clock::now();
		sw2_access_warning_logged = false;
		sw2_access_denied_count = 0;
		bOpened = true;
		break; // this JoyShock's own controller found (matched via composite parent above)
	}

	SetupDiDestroyDeviceInfoList(devInfo);
	return bOpened;
#else
	return false;
#endif
}

void JoyShock::release_sw2_command_interface_if_idle() {
#if PLATFORM_WINDOWS
	if (sw2_winusb_handle == nullptr)
	{
		return;
	}

	const auto now = std::chrono::steady_clock::now();
	if (sw2_last_command_time.time_since_epoch().count() != 0
		&& std::chrono::duration_cast<std::chrono::milliseconds>(now - sw2_last_command_time).count() < 1000)
	{
		return;
	}

	WinUsb_Free(static_cast<WINUSB_INTERFACE_HANDLE>(sw2_winusb_handle));
	sw2_winusb_handle = nullptr;
	if (sw2_winusb_file != nullptr)
	{
		CloseHandle(static_cast<HANDLE>(sw2_winusb_file));
		sw2_winusb_file = nullptr;
	}
#endif
}

bool JoyShock::init_switch2() {
	// Nintendo Switch 2 Pro Controller init, replicating what Steam does (confirmed with a USBPcap capture):
	// commands go over the controller's WinUSB interface (MI_01) bulk OUT endpoint 0x02 -- NOT over HID
	// (the HID interface MI_00 is input-only; hid_write fails with -1 on it). Once initialised, the
	// controller streams its 0x05 input reports on the HID interface, which the poll thread parses.
	// Command format: [command_id] 0x91 0x00 [subcommand_id] 0x00 [data_len] 0x00 0x00 [data...].
	// (Over Bluetooth LE byte 2 is 0x01 instead; sending that over USB makes it search for a BT host.)
#if PLATFORM_WINDOWS
	UE_LOG(LogJoyShockLibrary, Log, TEXT("Running Switch 2 (WinUSB) init sequence...\n"));

	if (!sw2_open_winusb())
	{
		// The controller may still stream input if it was already initialised (e.g. by Steam or a previous
		// session), so don't treat this as fatal; rumble retries the open lazily (see set_sw2_rumble).
		if (!sw2_init_failure_logged)
		{
			UE_LOG(LogJoyShockLibrary, Log,
				TEXT("SW2: command interface unavailable; deferring calibrated init until the current owner releases it."));
			sw2_init_failure_logged = true;
		}
		sw2_init_succeeded = false;
		this->initialised = true;
		return false;
	}

	WINUSB_INTERFACE_HANDLE usbHandle = static_cast<WINUSB_INTERFACE_HANDLE>(sw2_winusb_handle);
	const UCHAR outPipe = sw2_out_pipe;
	const UCHAR inPipe = sw2_in_pipe;
	bool bSuccess = false;

		// --- Factory config over SPI (same reads Steam performs before configuring the controller) ---
		// SPI read command 0x02:0x01, data = [flags u32 = 0][address LE u32]; the response is a 16-byte
		// header (cmd echo, status, length 0x40, address echo) followed by 64 bytes of flash data.
		auto SpiRead = [usbHandle, outPipe, inPipe](uint32 address, unsigned char* outData) -> bool
		{
			unsigned char cmdBuf[16];
			memset(cmdBuf, 0, sizeof(cmdBuf));
			cmdBuf[0] = 0x02;
			cmdBuf[1] = 0x91;
			cmdBuf[3] = 0x01;
			cmdBuf[5] = 0x08;
			cmdBuf[12] = address & 0xFF;
			cmdBuf[13] = (address >> 8) & 0xFF;
			cmdBuf[14] = (address >> 16) & 0xFF;
			cmdBuf[15] = (address >> 24) & 0xFF;

			ULONG written = 0;
			if (!WinUsb_WritePipe(usbHandle, outPipe, cmdBuf, sizeof(cmdBuf), &written, nullptr))
			{
				return false;
			}

			unsigned char resp[96];
			ULONG total = 0;
			while (total < 80) // 16-byte header + 64 bytes of data (may arrive split across bulk packets)
			{
				ULONG got = 0;
				if (!WinUsb_ReadPipe(usbHandle, inPipe, resp + total, sizeof(resp) - total, &got, nullptr) || got == 0)
				{
					break;
				}
				total += got;
			}
			if (total < 80 || resp[0] != 0x02)
			{
				return false;
			}
			memcpy(outData, resp + 16, 64);
			return true;
		};

		// Two 12-bit values packed into 3 bytes (same packing as the stick axes in input reports).
		auto Unpack12 = [](const unsigned char* p, uint16_t& v0, uint16_t& v1)
		{
			v0 = p[0] | ((p[1] & 0x0F) << 8);
			v1 = (p[1] >> 4) | (p[2] << 4);
		};

		// Stick factory calibration layout (blocks 0x013080 = left, 0x0130C0 = right): 9 bytes at offset
		// 0x28 as packed 12-bit pairs: [centerX,centerY][rangeX_a,rangeY_a][rangeX_b,rangeY_b].
		auto ParseStickCal = [&Unpack12](const unsigned char* blk, uint16_t* calX, uint16_t* calY)
		{
			uint16_t cx, cy, rxa, rya, rxb, ryb;
			Unpack12(blk + 0x28, cx, cy);
			Unpack12(blk + 0x2B, rxa, rya);
			Unpack12(blk + 0x2E, rxb, ryb);
			calX[1] = cx; calX[2] = cx + rxa; calX[0] = cx - rxb;
			calY[1] = cy; calY[2] = cy + rya; calY[0] = cy - ryb;
		};

		unsigned char spi[64];
		// Block 0x013000: serial, ids, then body/buttons/left-grip/right-grip RGB colours at offset 0x19.
		if (SpiRead(0x013000, spi))
		{
			body_colour = (spi[0x19] << 16) | (spi[0x1A] << 8) | spi[0x1B];
			button_colour = (spi[0x1C] << 16) | (spi[0x1D] << 8) | spi[0x1E];
			left_grip_colour = (spi[0x1F] << 16) | (spi[0x20] << 8) | spi[0x21];
			right_grip_colour = (spi[0x22] << 16) | (spi[0x23] << 8) | spi[0x24];
			UE_LOG(LogJoyShockLibrary, Log, TEXT("SW2 colours: body %06x, buttons %06x\n"), body_colour, button_colour);
		}
		if (SpiRead(0x013080, spi))
		{
			ParseStickCal(spi, stick_cal_x_l, stick_cal_y_l);
			UE_LOG(LogJoyShockLibrary, Log, TEXT("SW2 left stick cal: centre (%d, %d), min (%d, %d), max (%d, %d)\n"),
				stick_cal_x_l[1], stick_cal_y_l[1], stick_cal_x_l[0], stick_cal_y_l[0], stick_cal_x_l[2], stick_cal_y_l[2]);
		}
		if (SpiRead(0x0130C0, spi))
		{
			ParseStickCal(spi, stick_cal_x_r, stick_cal_y_r);
			UE_LOG(LogJoyShockLibrary, Log, TEXT("SW2 right stick cal: centre (%d, %d), min (%d, %d), max (%d, %d)\n"),
				stick_cal_x_r[1], stick_cal_y_r[1], stick_cal_x_r[0], stick_cal_y_r[0], stick_cal_x_r[2], stick_cal_y_r[2]);
		}

		struct Sw2InitCmd { unsigned char cmd; unsigned char subcmd; int dataLen; unsigned char data[20]; };
		const Sw2InitCmd cmds[] = {
			{ 0x07, 0x01, 0,  {} },
			{ 0x0c, 0x02, 4,  { 0x27, 0x00, 0x00, 0x00 } },              // FEATSEL
			{ 0x11, 0x01, 0,  {} },
			{ 0x0a, 0x08, 20, { 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x35, 0x00, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
			{ 0x0c, 0x04, 4,  { 0x27, 0x00, 0x00, 0x00 } },
			{ 0x01, 0x0c, 0,  {} },
			{ 0x01, 0x01, 0,  {} },
			{ 0x08, 0x02, 4,  { 0x01, 0x00, 0x00, 0x00 } },
			{ 0x03, 0x0a, 4,  { 0x05, 0x00, 0x00, 0x00 } },              // input report mode 0x05
			{ 0x03, 0x0d, 8,  { 0x01, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } },
		};

		bSuccess = true;
		for (const Sw2InitCmd& c : cmds)
		{
			unsigned char cmdBuf[28];
			memset(cmdBuf, 0, sizeof(cmdBuf));
			cmdBuf[0] = c.cmd;
			cmdBuf[1] = 0x91;
			cmdBuf[2] = 0x00;
			cmdBuf[3] = c.subcmd;
			cmdBuf[4] = 0x00;
			cmdBuf[5] = static_cast<unsigned char>(c.dataLen);
			cmdBuf[6] = 0x00;
			cmdBuf[7] = 0x00;
			if (c.dataLen > 0)
			{
				memcpy(cmdBuf + 8, c.data, c.dataLen);
			}

			ULONG written = 0;
			if (!WinUsb_WritePipe(usbHandle, outPipe, cmdBuf, 8 + c.dataLen, &written, nullptr))
			{
				UE_LOG(LogJoyShockLibrary, Warning, TEXT("  SW2 cmd %02x:%02x -> WritePipe failed (%u)\n"), c.cmd, c.subcmd, GetLastError());
				bSuccess = false;
				continue;
			}

			// Drain the command response (best effort; also confirms the controller acknowledged).
			unsigned char resp[64];
			ULONG got = 0;
			if (WinUsb_ReadPipe(usbHandle, inPipe, resp, sizeof(resp), &got, nullptr) && got >= 2)
			{
				UE_LOG(LogJoyShockLibrary, Log, TEXT("  SW2 cmd %02x:%02x -> ack %02x %02x (%u bytes)\n"), c.cmd, c.subcmd, resp[0], resp[1], got);
			}
			else
			{
				UE_LOG(LogJoyShockLibrary, Log, TEXT("  SW2 cmd %02x:%02x -> sent (%u bytes), no ack\n"), c.cmd, c.subcmd, written);
			}
		}
		sw2_last_command_time = std::chrono::steady_clock::now();

	if (!bSuccess)
	{
		UE_LOG(LogJoyShockLibrary, Warning, TEXT("SW2: some init commands failed; the controller may not stream input.\n"));
	}
	sw2_init_succeeded = bSuccess;
	if (bSuccess)
	{
		sw2_init_failure_logged = false;
	}
	this->initialised = true;
	return bSuccess;
#else
	// Non-Windows: the WinUSB path doesn't apply; hidapi may be able to talk to the command interface
	// directly on other platforms (untested).
	sw2_init_succeeded = false;
	this->initialised = true;
	return false;
#endif
}

void JoyShock::set_sw2_rumble(int smallRumble, int bigRumble) {
#if PLATFORM_WINDOWS
	if (sw2_winusb_handle == nullptr)
	{
		// The command interface couldn't be opened at connect time (typically another application such as
		// Steam holding it exclusively). Retry lazily -- if that application has since been closed, rumble
		// self-heals. Throttled so failed attempts don't run the device enumeration on every rumble call.
		const auto now = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::seconds>(now - sw2_last_open_attempt).count() < 2)
		{
			return;
		}
		sw2_last_open_attempt = now;
		if (!sw2_open_winusb())
		{
			return;
		}
		UE_LOG(LogJoyShockLibrary, Log, TEXT("SW2: command interface acquired on retry; rumble available.\n"));
	}

	// The Switch 2's amplitude-accurate rumble stream uses a dedicated channel that hasn't been mapped on
	// USB yet, but its command channel supports playing/stopping a built-in vibration preset:
	// command 0x0A, subcommand 0x02, data = preset id (u32; 1 = rumble, 0 = stop). The preset is a short
	// one-shot, so every call with a non-zero value retriggers it; only redundant stops are skipped.
	const bool bWantOn = smallRumble > 0 || bigRumble > 0;
	if (!bWantOn && !sw2_rumble_on)
	{
		return;
	}
	sw2_rumble_on = bWantOn;

	unsigned char cmdBuf[12];
	memset(cmdBuf, 0, sizeof(cmdBuf));
	cmdBuf[0] = 0x0A; // vibration command
	cmdBuf[1] = 0x91;
	cmdBuf[2] = 0x00; // USB transport flag
	cmdBuf[3] = 0x02; // play preset
	cmdBuf[5] = 0x04; // data length
	cmdBuf[8] = bWantOn ? 0x01 : 0x00;

	ULONG written = 0;
	WinUsb_WritePipe(static_cast<WINUSB_INTERFACE_HANDLE>(sw2_winusb_handle), sw2_out_pipe, cmdBuf, sizeof(cmdBuf), &written, nullptr);

	// Drain the ack so responses don't accumulate in the pipe's buffer.
	unsigned char resp[64];
	ULONG got = 0;
	WinUsb_ReadPipe(static_cast<WINUSB_INTERFACE_HANDLE>(sw2_winusb_handle), sw2_in_pipe, resp, sizeof(resp), &got, nullptr);
	sw2_last_command_time = std::chrono::steady_clock::now();
#endif
}

bool JoyShock::set_sw2_player_lights(unsigned char playerLightMask) {
#if PLATFORM_WINDOWS
	if (sw2_winusb_handle == nullptr)
	{
		const auto now = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::seconds>(now - sw2_last_open_attempt).count() < 2)
		{
			return false;
		}
		sw2_last_open_attempt = now;
		if (!sw2_open_winusb())
		{
			return false;
		}
	}

	// Player LEDs use command 09:07. The mask is the same four-bit pattern used by Switch 1;
	// byte 8 is the first byte of this command's eight-byte payload.
	unsigned char cmdBuf[16];
	memset(cmdBuf, 0, sizeof(cmdBuf));
	cmdBuf[0] = 0x09;
	cmdBuf[1] = 0x91;
	cmdBuf[2] = 0x00;
	cmdBuf[3] = 0x07;
	cmdBuf[5] = 0x08;
	cmdBuf[8] = playerLightMask;

	ULONG written = 0;
	const bool bWritten = WinUsb_WritePipe(static_cast<WINUSB_INTERFACE_HANDLE>(sw2_winusb_handle), sw2_out_pipe,
		cmdBuf, sizeof(cmdBuf), &written, nullptr) && written == sizeof(cmdBuf);
	if (!bWritten)
	{
		return false;
	}

	// Drain the acknowledgement so the shared command pipe remains ready for rumble and later LED changes.
	unsigned char resp[64];
	ULONG got = 0;
	WinUsb_ReadPipe(static_cast<WINUSB_INTERFACE_HANDLE>(sw2_winusb_handle), sw2_in_pipe,
		resp, sizeof(resp), &got, nullptr);
	sw2_last_command_time = std::chrono::steady_clock::now();
	return true;
#else
	return false;
#endif
}

bool JoyShock::set_switch_player_lights(unsigned char playerLightMask) {
	unsigned char buf[1] = { playerLightMask };
	return send_subcommand(0x01, 0x30, buf, 1);
}

bool JoyShock::clear_switch_home_light() {
	// Subcommand 0x38 controls the blue HOME notification light independently of subcommand 0x30's
	// four green player LEDs. This is Nintendo's five-byte "steady brightness 0" pattern; an all-zero
	// payload is not an off command and can make the ring light instead.
	unsigned char buf[5] = { 0x01, 0x00, 0x00, 0x11, 0x11 };
	return send_subcommand(0x01, 0x38, buf, sizeof(buf));
}

void JoyShock::set_switch_rumble(int smallRumble, int bigRumble) {
	// Switch 1 (Joy-Con / Pro Controller) HD rumble: output report 0x10, followed by a 4-bit packet
	// counter and 4 bytes of rumble data per side (left, right). Fixed frequencies (320 Hz high /
	// 160 Hz low -- the neutral encoding 00 01 40 40) with amplitudes encoded per dekuNukem's
	// Nintendo_Switch_Reverse_Engineering rumble tables:
	//   code = log2(amp * 17) * 16   for 0.12 < amp <= 0.23
	//   code = log2(amp * 8.7) * 32  for amp > 0.23
	//   HF amplitude byte = code * 2; LF amplitude u16 = 0x0040 + code/2, +0x8000 when code is odd.
	auto EncodeSide = [](float hfAmp, float lfAmp, unsigned char* out)
	{
		auto AmpToCode = [](float amp) -> int
		{
			if (amp <= 0.007f)
			{
				return 0;
			}
			float code;
			if (amp <= 0.23f)
			{
				code = log2f(amp * 17.0f) * 16.0f;
			}
			else
			{
				code = log2f(amp * 8.7f) * 32.0f;
			}
			return FMath::Clamp(FMath::RoundToInt(code), 0, 100);
		};

		const int hfCode = AmpToCode(hfAmp);
		const int lfCode = AmpToCode(lfAmp);
		const uint16_t lfEncoded = 0x0040 + (lfCode >> 1) + ((lfCode & 1) ? 0x8000 : 0);

		out[0] = 0x00;                                            // HF 320 Hz (low byte)
		out[1] = static_cast<unsigned char>(0x01 + hfCode * 2);   // HF 320 Hz (high byte) + HF amplitude
		out[2] = static_cast<unsigned char>(0x40 + (lfEncoded >> 8)); // LF 160 Hz + LF amplitude (high)
		out[3] = static_cast<unsigned char>(lfEncoded & 0xFF);    // LF amplitude (low)
	};

	const float hfAmp = FMath::Clamp(smallRumble, 0, 255) / 255.0f;
	const float lfAmp = FMath::Clamp(bigRumble, 0, 255) / 255.0f;

	unsigned char buf[10];
	buf[0] = 0x10;
	buf[1] = static_cast<unsigned char>((++global_count) & 0xF);
	if (global_count > 0xF)
	{
		global_count = 0x0;
	}
	EncodeSide(hfAmp, lfAmp, buf + 2); // left actuator
	EncodeSide(hfAmp, lfAmp, buf + 6); // right actuator

	// Plain hid_write: report 0x10 has no reply, and a blocking read here would fight the poll thread.
	hid_write(this->handle, buf, sizeof(buf));
}

bool JoyShock::init_bt() {
	bool result = true;
	unsigned char buf[0x40];
	memset(buf, 0, 0x40);
	UE_LOG(LogJoyShockLibrary, Log, TEXT("Initialising Bluetooth connection...\n"));

	// set blocking to ensure command is recieved:
	hid_set_nonblocking(this->handle, 0);

	// first, check if this is a USB connection
	buf[0] = 0x80;
	buf[1] = 0x01;
	hid_write(this->handle, buf, 2);
	// wait for up to 5 messages for a USB acknowledgement
	for (int idx = 0; idx < 5; idx++)
	{
		if (hid_read_timeout(this->handle, buf, 0x40, 200) && buf[0] == 0x81)
		{
			//UE_LOG(LogJoyShockLibrary, Log, TEXT("%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			//	buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8], buf[9], buf[10]);
			UE_LOG(LogJoyShockLibrary, Log, TEXT("Attempting USB connection\n"));

			// it's usb!
			is_usb = true;

			init_usb();
			return 1;

			break;
		}
		//UE_LOG(LogJoyShockLibrary, Log, TEXT("%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n"),
		//	buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8], buf[9], buf[10]);
		UE_LOG(LogJoyShockLibrary, Log, TEXT("Not a USB response...\n"));
	}
	memset(buf, 0, 0x40);
	//if (hid_exchange(this->handle, buf, 2))
	//{
	//	UE_LOG(LogJoyShockLibrary, Log, TEXT("%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n"),
	//		buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8], buf[9], buf[10]);
	//	UE_LOG(LogJoyShockLibrary, Log, TEXT("Attempting USB connection\n"));
	//	// it's usb!
	//	is_usb = true;
	//
	//	init_usb();
	//	return 1;
	//}
	buf[1] = 0x00;

	// Enable vibration
	UE_LOG(LogJoyShockLibrary, Log, TEXT("Enabling vibration...\n"));
	buf[0] = 0x01; // Enabled
	send_subcommand(0x1, 0x48, buf, 1);

	//UE_LOG(LogJoyShockLibrary, Log, TEXT("Set vibration\n"));

	// Enable IMU data
	enable_IMU(buf, 0x40);

	// Set input report mode (to push at 60hz)
	// x00	Active polling mode for IR camera data. Answers with more than 300 bytes ID 31 packet
	// x01	Active polling mode
	// x02	Active polling mode for IR camera data.Special IR mode or before configuring it ?
	// x21	Unknown.An input report with this ID has pairing or mcu data or serial flash data or device info
	// x23	MCU update input report ?
	// 30	NPad standard mode. Pushes current state @60Hz. Default in SDK if arg is not in the list
	// 31	NFC mode. Pushes large packets @60Hz
	UE_LOG(LogJoyShockLibrary, Log, TEXT("Set input report mode to 0x30...\n"));
	buf[0] = 0x30;
	send_subcommand(0x01, 0x03, buf, 1);

	// @CTCaer

	// get calibration data:
	UE_LOG(LogJoyShockLibrary, Log, TEXT("Getting calibration data...\n"));
	result = get_switch_controller_info();

	return result;
}

void JoyShock::init_ds4_bt() {
	UE_LOG(LogJoyShockLibrary, Log, TEXT("initialise, set colour\n"));
	unsigned char buf[78];
	memset(buf, 0, 78);

	//buf[0] = 0x11;
	//buf[1] = 0x80;
	//buf[3] = 0xff;

	// https://github.com/Ryochan7/DS4Windows/blob/jay/DS4Windows/DS4Library/DS4Device.cs
	buf[0] = 0x15;
	buf[1] = 0xC0 | 1;
	buf[2] = 0xA0;
	buf[3] = 0xf7;
	buf[4] = 0x04;

	//// https://github.com/chrippa/ds4drv/blob/master/ds4drv/device.py
	//buf[0] = 0xa2; // 0x80;
	////buf[1] = 0xff;
	//// trying to do colour stuff
	//// http://eleccelerator.com/wiki/index.php?title=DualShock_4
	//// this is only for bt
	//buf[1] = 0x11;
	//buf[2] = 0xc0;
	//buf[3] = 0x20;
	//buf[4] = 0xf3;
	//buf[5] = 0x04;
	//// rumble
	//buf[7] = 0xFF;
	//buf[8] = 0x00;
	//// colour
	//buf[9] = 0x00;
	//buf[10] = 0x00;
	//buf[11] = 0x00;
	//// flash time
	//buf[12] = 0xff;
	//buf[13] = 0x00;
	//// now we need a CRC-32 of previous bytes
	//uint32_t crc = crc_32(buf, 75);
	//buf[75] = (crc >> 24) & 0xFF;
	//buf[76] = (crc >> 16) & 0xFF;
	//buf[77] = (crc >> 8) & 0xFF;
	//buf[78] = crc & 0xFF;

	//// https://github.com/chrippa/ds4drv/blob/master/ds4drv/device.py
	//buf[0] = 0x80;
	//buf[1] = 0xff;
	//// trying to do colour stuff
	//// http://eleccelerator.com/wiki/index.php?title=DualShock_4
	//// this is only for bt
	//buf[2] = 0x11;
	//// rumble
	//buf[6] = 0xFF;
	//buf[7] = 0xFF;	
	//// colour
	//buf[8] = 0xFF; // 0x00;
	//buf[9] = 0x80; // 0x00;
	//buf[10] = 0x00;
	//// flash time
	//buf[11] = 0xff;
	//buf[12] = 0x00;
	//// now we need a CRC-32 of previous bytes
	//uint32_t crc = crc_32(buf, 75);
	//buf[75] = (crc >> 24) & 0xFF;
	//buf[76] = (crc >> 16) & 0xFF;
	//buf[77] = (crc >> 8) & 0xFF;
	//buf[78] = crc & 0xFF;

	// set blocking:
	// this insures we get the MAC Address
	hid_set_nonblocking(this->handle, 0);

	hid_write(handle, buf, 78);

	// initialise stuff
	memset(factory_stick_cal, 0, 0x12);
	memset(device_colours, 0, 0xC);
	memset(user_stick_cal, 0, 0x16);
	memset(sensor_model, 0, 0x6);
	memset(stick_model, 0, 0x12);
	memset(factory_sensor_cal, 0, 0x18);
	memset(user_sensor_cal, 0, 0x1A);
	memset(factory_sensor_cal_calm, 0, 0xC);
	memset(user_sensor_cal_calm, 0, 0xC);
	memset(sensor_cal, 0, sizeof(sensor_cal));
	memset(stick_cal_x_l, 0, sizeof(stick_cal_x_l));
	memset(stick_cal_y_l, 0, sizeof(stick_cal_y_l));
	memset(stick_cal_x_r, 0, sizeof(stick_cal_x_r));
	memset(stick_cal_y_r, 0, sizeof(stick_cal_y_r));
	stick_cal_x_l[0] =
		stick_cal_y_l[0] =
		stick_cal_x_r[0] =
		stick_cal_y_r[0] = 0;
	stick_cal_x_l[1] =
		stick_cal_y_l[1] =
		stick_cal_x_r[1] =
		stick_cal_y_r[1] = 127;
	stick_cal_x_l[2] =
		stick_cal_y_l[2] =
		stick_cal_x_r[2] =
		stick_cal_y_r[2] = 255;
	//// Acc cal origin position
	//sensor_cal[0][0] = 0;
	//sensor_cal[0][1] = 0;
	//sensor_cal[0][2] = 0;
	//
	//// Gyro cal origin position
	//sensor_cal[1][0] = 0;
	//sensor_cal[1][1] = 0;
	//sensor_cal[1][2] = 0;

	enable_gyro_ds4_bt(buf, 78);

	initialised = true;
}

// placeholder to get things working quickly. overdue for a refactor
void JoyShock::init_ds_usb() {
	// initialise stuff
	memset(factory_stick_cal, 0, 0x12);
	memset(device_colours, 0, 0xC);
	memset(user_stick_cal, 0, 0x16);
	memset(sensor_model, 0, 0x6);
	memset(stick_model, 0, 0x12);
	memset(factory_sensor_cal, 0, 0x18);
	memset(user_sensor_cal, 0, 0x1A);
	memset(factory_sensor_cal_calm, 0, 0xC);
	memset(user_sensor_cal_calm, 0, 0xC);
	memset(sensor_cal, 0, sizeof(sensor_cal));
	memset(stick_cal_x_l, 0, sizeof(stick_cal_x_l));
	memset(stick_cal_y_l, 0, sizeof(stick_cal_y_l));
	memset(stick_cal_x_r, 0, sizeof(stick_cal_x_r));
	memset(stick_cal_y_r, 0, sizeof(stick_cal_y_r));
	stick_cal_x_l[0] =
		stick_cal_y_l[0] =
		stick_cal_x_r[0] =
		stick_cal_y_r[0] = 0;
	stick_cal_x_l[1] =
		stick_cal_y_l[1] =
		stick_cal_x_r[1] =
		stick_cal_y_r[1] = 127;
	stick_cal_x_l[2] =
		stick_cal_y_l[2] =
		stick_cal_x_r[2] =
		stick_cal_y_r[2] = 255;

	initialised = true;
}

// this is mostly copied from init_usb() below, but modified to speak DS4
void JoyShock::init_ds4_usb() {
	unsigned char buf[31];
	memset(buf, 0, 31);

	// report id?
	buf[0] = 0x05;
	// I dunno what this is
	buf[1] = 0xf7;
	buf[2] = 0x04;
	//// http://eleccelerator.com/wiki/index.php?title=DualShock_4
	//// https://github.com/chrippa/ds4drv/blob/master/ds4drv/device.py
	//// rumble
	//buf[4] = 0x00;
	//buf[5] = 0x00;
	//// colour
	//buf[6] = 0x00;
	////buf[7] = 0xff;
	//buf[7] = 0x00;
	//buf[8] = 0x00;
	//// flash time
	//buf[9] = 0xff;
	//buf[10] = 0x00;
	// now we need a CRC-32 of previous bytes
	//uint32_t = crc_32(buf, 75);
	//buf[75] = 

	// set blocking:
	// this insures we get the MAC Address
	hid_set_nonblocking(this->handle, 0);

	hid_write(handle, buf, 31);

	// initialise stuff
	memset(factory_stick_cal, 0, 0x12);
	memset(device_colours, 0, 0xC);
	memset(user_stick_cal, 0, 0x16);
	memset(sensor_model, 0, 0x6);
	memset(stick_model, 0, 0x12);
	memset(factory_sensor_cal, 0, 0x18);
	memset(user_sensor_cal, 0, 0x1A);
	memset(factory_sensor_cal_calm, 0, 0xC);
	memset(user_sensor_cal_calm, 0, 0xC);
	memset(sensor_cal, 0, sizeof(sensor_cal));
	memset(stick_cal_x_l, 0, sizeof(stick_cal_x_l));
	memset(stick_cal_y_l, 0, sizeof(stick_cal_y_l));
	memset(stick_cal_x_r, 0, sizeof(stick_cal_x_r));
	memset(stick_cal_y_r, 0, sizeof(stick_cal_y_r));
	stick_cal_x_l[0] =
		stick_cal_y_l[0] =
		stick_cal_x_r[0] =
		stick_cal_y_r[0] = 0;
	stick_cal_x_l[1] =
		stick_cal_y_l[1] =
		stick_cal_x_r[1] =
		stick_cal_y_r[1] = 127;
	stick_cal_x_l[2] =
		stick_cal_y_l[2] =
		stick_cal_x_r[2] =
		stick_cal_y_r[2] = 255;
	//// Acc cal origin position
	//sensor_cal[0][0] = 0;
	//sensor_cal[0][1] = 0;
	//sensor_cal[0][2] = 0;
	//
	//// Gyro cal origin position
	//sensor_cal[1][0] = 0;
	//sensor_cal[1][1] = 0;
	//sensor_cal[1][2] = 0;

	initialised = true;
}

void JoyShock::deinit_ds4_bt() {
	// TODO. For now, init, which stops rumbling and disables light
	init_ds4_bt();

	initialised = false;
}

// TODO: implement this
void JoyShock::deinit_ds4_usb() {
	unsigned char buf[40];
	memset(buf, 0, 40);

	// report id?
	buf[0] = 0x05;
	// don't know what this is
	buf[1] = 0xff;
	// rumble
	buf[4] = 0x00;
	buf[5] = 0x00;
	// colour
	buf[6] = 0x00;
	buf[7] = 0x00;
	buf[8] = 0x00;
	// flash time
	buf[9] = 0x00;
	buf[10] = 0x00;
	// now we need a CRC-32 of previous bytes
	//uint32_t = crc_32(buf, 75);
	//buf[75] = 

	// set non-blocking
	hid_set_nonblocking(this->handle, 1);

	hid_write(handle, buf, 31);

	initialised = false;
}

void JoyShock::deinit_usb() {
	unsigned char buf[0x40];
	memset(buf, 0x00, 0x40);

	//Let the Joy-Con talk BT again    
	buf[0] = 0x80;
	buf[1] = 0x05;

	hid_set_nonblocking(this->handle, 1);
	hid_write(handle, buf, 0x2);

	initialised = false;
}

void JoyShock::set_ds5_rumble_light(unsigned char smallRumble, unsigned char bigRumble,
                       unsigned char colourR,
                       unsigned char colourG,
                       unsigned char colourB,
                       unsigned char playerlights) {
    if(!is_usb) {
        set_ds5_rumble_light_bt(smallRumble, bigRumble, colourR, colourG, colourB, playerlights);
    }
    else {
        set_ds5_rumble_light_usb(smallRumble, bigRumble, colourR, colourG, colourB, playerlights);
    }

}

void JoyShock::set_ds4_rumble_light(unsigned char smallRumble, unsigned char bigRumble,
	unsigned char colourR,
	unsigned char colourG,
	unsigned char colourB) {
	if (!is_usb) {
		set_ds4_rumble_light_bt(smallRumble, bigRumble, colourR, colourG, colourB);
	}
	else {
		set_ds4_rumble_light_usb(smallRumble, bigRumble, colourR, colourG, colourB);
	}
}

void JoyShock::set_ds4_rumble_light_usb(unsigned char smallRumble, unsigned char bigRumble,
	unsigned char colourR,
	unsigned char colourG,
	unsigned char colourB) {
	// todo: based on bluetoothness, switch report id to 0x11, offset everything by 2 -- basically use init stuff as basis
	unsigned char buf[40];
	memset(buf, 0, 40);

	// report id?
	buf[0] = 0x05;
	// don't know what this is
	buf[1] = 0xff;
	// rumble
	buf[4] = smallRumble;
	buf[5] = bigRumble;
	// colour
	buf[6] = colourR;
	buf[7] = colourG;
	buf[8] = colourB;
	// flash time
	buf[9] = 0xff;
	buf[10] = 0x00;
	// now we need a CRC-32 of previous bytes
	//uint32_t = crc_32(buf, 75);
	//buf[75] = 

	hid_write(handle, buf, 31);
}

void JoyShock::set_ds4_rumble_light_bt(unsigned char smallRumble, unsigned char bigRumble,
	unsigned char colourR,
	unsigned char colourG,
	unsigned char colourB) {
	unsigned char buf[79];
	memset(buf, 0, 79);

	// https://github.com/chrippa/ds4drv/blob/master/ds4drv/device.py
	//buf[0] = 0xa2; // 0x80;
	//buf[1] = 0xff;
	// trying to do colour stuff
	// http://eleccelerator.com/wiki/index.php?title=DualShock_4
	// this is only for bt

	buf[0] = 0xa2; // Output report header, needs to be included in crc32
	buf[1] = 0x11; // Output report 0x11
	buf[2] = 0xc0; // HID + CRC according to hid-sony
	buf[3] = 0x20; // ????
	buf[4] = 0x07; // Set blink + leds + motor
	buf[5] = 0x00;
	buf[6] = 0x00;
	// rumble
	buf[7] = smallRumble;
	buf[8] = bigRumble;
	// colour
	buf[9] = colourR;
	buf[10] = colourG;
	buf[11] = colourB;
	// flash time
	buf[12] = 0xff;
	buf[13] = 0x00;
	// now we need a CRC-32 of previous bytes

	/*
	// test
    buf[0] = 0xa2; // Output report header, needs to be included in crc32
    buf[1] = 0x11; // Output report 0x11
    buf[2] = 0xc0; // HID + CRC according to hid-sony
    buf[3] = 0x00; // ????
    buf[4] = 0x07; // Set blink + leds + motor
    buf[5] = 0x00;
    buf[6] = 0x00;
    buf[7] = 0xff;
    buf[8] = 0xff;
    buf[9] = 0xff;
    buf[10] = 0xff;
    buf[11] = 0xff;
    buf[12] = 0xff;
    buf[22] = 0x43;
    buf[23] = 0x43;
    buf[25] = 0x4d;
    buf[26] = 0x85;
*/

	uint32_t crc = crc_32(buf, 75);
	memcpy(&buf[75], &crc, 4);
	//buf[75] = (crc >> 24) & 0xFF;
	//buf[76] = (crc >> 16) & 0xFF;
	//buf[77] = (crc >> 8) & 0xFF;
	//buf[78] = crc & 0xFF;

	hid_write(handle, &buf[1], 78);
}

void JoyShock::set_ds5_rumble_light_usb(unsigned char smallRumble, unsigned char bigRumble,
                             unsigned char colourR,
                             unsigned char colourG,
                             unsigned char colourB,
                             unsigned char playerlights) { // DS5 actually has player lights.
    unsigned char buf[79];
    memset(buf, 0, 79);

    // https://github.com/Ryochan7/DS4Windows/blob/jay/DS4Windows/DS4Library/InputDevices/DualSenseDevice.cs
    // DS4Windows to the rescue.
    // Also thanks to Neilk1 for sharing his doc on the DS5 protocol.

    // Header & Report Information
    buf[0] = 0xa2; // Output report header, needs to be included in crc32
    buf[1] = 0x02; // DualSense output report is 0x02 for USB
    //buf[1] = 0x02; // DATA (0x02)


    buf[2] = 0x03;

    buf[3] = 0x54; // Toggle LED Strips, player lights, motor effect. Ignore Mic LED

    // Rumble emulation bytes.
    buf[4] = smallRumble;
    buf[5] = bigRumble;

    // 7-10 are mostly just audio settings.

    // Mute Button state. 0x00 = off, 0x01 = solid, 0x02 = pulsating.
    buf[10] = 0x00;

    // Skip to about 41, since we are ignoring trigger effect data.
    // Enable LED brightness
    buf[40] = 0x02; // ???
    buf[41] = 0x02;
    buf[44] = 0x02;

    // Controls the player lights, which the DS5 has.
    // Last two bits are unused - unset them to avoid issues.
    buf[45] = playerlights;
    buf[45] &= ~(1 << 7);
    buf[45] &= ~(1 << 8);

    // colour
    buf[46] = colourR;
    buf[47] = colourG;
    buf[48] = colourB;

    // USB does not send CRC32

    //uint32_t crc = crc_32(buf, 74);
    //memcpy(&buf[74], &crc, 4);
    //buf[75] = (crc >> 24) & 0xFF;
    //buf[76] = (crc >> 16) & 0xFF;
    //buf[77] = (crc >> 8) & 0xFF;
    //buf[78] = crc & 0xFF;

    hid_write(handle, &buf[1], 74);
}

// Calling the Dualsense anything but the DS5 is confusing, since DS also = DualShock, and the DualSense is the PS5 Controller anyway
void JoyShock::set_ds5_rumble_light_bt(unsigned char smallRumble, unsigned char bigRumble,
                             unsigned char colourR,
                             unsigned char colourG,
                             unsigned char colourB,
                             unsigned char playerlights) { // DS5 actually has player lights.
    unsigned char buf[79];
    memset(buf, 0, 79);

    // https://github.com/Ryochan7/DS4Windows/blob/jay/DS4Windows/DS4Library/InputDevices/DualSenseDevice.cs
    // DS4Windows to the rescue.
    // Also thanks to Neilk1 for sharing his doc on the DS5 protocol.

    // Header & Report Information
    buf[0] = 0xa2; // Output report header, needs to be included in crc32
    buf[1] = 0x31; // DualSense output report is 0x31
    buf[2] = 0x02; // DATA (0x02)

    // Comment stolen from DS4Windows:
    // 0x01 Set the main motors (also requires flag 0x02)
    // 0x02 Set the main motors (also requires flag 0x01)
    // 0x04 Set the right trigger motor
    // 0x08 Set the left trigger motor
    // 0x10 Enable modification of audio volume
    // 0x20 Enable internal speaker (even while headset is connected)
    // 0x40 Enable modification of microphone volume
    // 0x80 Enable internal mic (even while headset is connected)
    buf[3] = 0x03;

    // Comment stolen from DS4Windows:
    // 0x01 Toggling microphone LED, 0x02 Toggling Audio/Mic Mute
    // 0x04 Toggling LED strips on the sides of the Touchpad, 0x08 Turn off all LED lights
    // 0x10 Toggle player LED lights below Touchpad, 0x20 ???
    // 0x40 Adjust overall motor/effect power, 0x80 ???
    buf[4] = 0x54; // Toggle LED Strips, player lights, motor effect. Ignore Mic LED

    // Rumble emulation bytes.
    buf[5] = smallRumble;
    buf[6] = bigRumble;

    // 7-10 are mostly just audio settings.

    // Mute Button state. 0x00 = off, 0x01 = solid, 0x02 = pulsating.
    buf[11] = 0x00;

    // Skip to about 41, since we are ignoring trigger effect data.
    // Enable LED brightness
    buf[41] = 0x02; // ???
    buf[44] = 0x02;
    buf[45] = 0x02;

    // Last two bits are unused - unset them to avoid issues.
    buf[46] = playerlights;
    buf[46] &= ~(1 << 7);
    buf[46] &= ~(1 << 8);

    // colour
    buf[47] = colourR;
    buf[48] = colourG;
    buf[49] = colourB;

    uint32_t crc = crc_32(buf, 75);
    memcpy(&buf[75], &crc, 4);
    //buf[75] = (crc >> 24) & 0xFF;
    //buf[76] = (crc >> 16) & 0xFF;
    //buf[77] = (crc >> 8) & 0xFF;
    //buf[78] = crc & 0xFF;

    hid_write(handle, &buf[1], 78);
}

//// mfosse credits Hypersect (Ryan Juckett), but I've removed deadzones so the consuming application can deal with them
//// http://blog.hypersect.com/interpreting-analog-sticks/
void JoyShock::CalcAnalogStick2
(
	float &pOutX,       // out: resulting stick X value
	float &pOutY,       // out: resulting stick Y value
	uint16_t x,              // in: initial stick X value
	uint16_t y,              // in: initial stick Y value
	uint16_t x_calc[3],      // calc -X, CenterX, +X
	uint16_t y_calc[3]       // calc -Y, CenterY, +Y
)
{

	float x_f, y_f;

	// convert to float based on calibration and valid ranges per +/-axis
	x = FMath::Clamp(x, x_calc[0], x_calc[2]);
	y = FMath::Clamp(y, y_calc[0], y_calc[2]);
	if (x >= x_calc[1]) {
		x_f = (float)(x - x_calc[1]) / (float)(x_calc[2] - x_calc[1]);
	}
	else {
		x_f = -((float)(x - x_calc[1]) / (float)(x_calc[0] - x_calc[1]));
	}
	if (y >= y_calc[1]) {
		y_f = (float)(y - y_calc[1]) / (float)(y_calc[2] - y_calc[1]);
	}
	else {
		y_f = -((float)(y - y_calc[1]) / (float)(y_calc[0] - y_calc[1]));
	}

	pOutX = x_f;
	pOutY = y_f;
}

// SPI (@CTCaer):
bool JoyShock::get_spi_data(uint32_t offset, const uint8_t read_len, uint8_t *test_buf) {
	int res;
	uint8_t buf[0x100];
	// Bounded retries: once the controller is in report mode 0x30 it streams input at 60Hz, so
	// hid_read_timeout almost always returns *something* -- usually an ordinary input report rather than
	// our SPI reply, and most of these attempts are spent skipping past those. If the SPI response itself
	// is lost (flaky Bluetooth), an unbounded loop here spins forever -- while the enumeration thread holds
	// the exclusive connected lock, freezing connects AND (via the shared lock) the game thread.
	int timeouts = 0;
	for (int attempt = 0; attempt < 32; attempt++) {
		memset(buf, 0, sizeof(buf));
		auto hdr = (brcm_hdr *)buf;
		auto pkt = (brcm_cmd_01 *)(hdr + 1);
		hdr->cmd = 1;
		hdr->rumble[0] = timing_byte;

		buf[1] = timing_byte;

		timing_byte++;
		if (timing_byte > 0xF) {
			timing_byte = 0x0;
		}
		pkt->subcmd = 0x10;
		pkt->offset = offset;
		pkt->size = read_len;

		for (int i = 11; i < 22; ++i) {
			buf[i] = buf[i + 3];
		}

		res = hid_write(handle, buf, sizeof(*hdr) + sizeof(*pkt));

		res = hid_read_timeout(handle, buf, sizeof(buf), 1000);
		if (res < 0)
		{
			// The device is gone -- retrying can't bring it back.
			return false;
		}
		if (res == 0)
		{
			// Nothing came back this time. On a busy Bluetooth link that is transient, but it used to be
			// treated as fatal, which threw away the 32 retries below and failed the read on a single
			// hiccup. Retry -- with its own small bound, so a genuinely silent controller can't hold the
			// connected lock for 32 seconds.
			if (++timeouts >= 3)
			{
				UE_LOG(LogJoyShockLibrary, Warning, TEXT("SPI read at 0x%04X timed out %d times; giving up.\n"), offset, timeouts);
				return false;
			}
			continue;
		}

		if ((*(uint16_t*)&buf[0xD] == 0x1090) && (*(uint32_t*)&buf[0xF] == offset)) {
			if (res >= 0x14 + read_len) {
				for (int i = 0; i < read_len; i++) {
					test_buf[i] = buf[0x14 + i];
				}
			}
			return true;
		}
	}
	return false;
}

int32 JoyShock::write_spi_data(uint32_t offset, const uint8_t write_len, uint8_t* test_buf) {
	int res;
	uint8_t buf[0x100];
	int error_writing = 0;
	while (1) {
		memset(buf, 0, sizeof(buf));
		auto hdr = (brcm_hdr *)buf;
		auto pkt = (brcm_cmd_01 *)(hdr + 1);
		hdr->cmd = 1;
		hdr->rumble[0] = timing_byte;
		timing_byte++;
		if (timing_byte > 0xF) {
			timing_byte = 0x0;
		}
		pkt->subcmd = 0x11;
		pkt->offset = offset;
		pkt->size = write_len;
		for (int i = 0; i < write_len; i++) {
			buf[0x10 + i] = test_buf[i];
		}
		res = hid_write(handle, buf, sizeof(*hdr) + sizeof(*pkt) + write_len);

		// Bounded read: a blocking hid_read here can hang forever if the device stops responding.
		res = hid_read_timeout(handle, buf, sizeof(buf), 1000);
		if (res <= 0) {
			return 1;
		}

		if (*(uint16_t*)&buf[0xD] == 0x1180)
			break;

		error_writing++;
		if (error_writing == 125) {
			return 1;
		}
	}

	return 0;

}
