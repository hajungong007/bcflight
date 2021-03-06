/*
 * BCFlight
 * Copyright (C) 2016 Adrien Aubry (drich)
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
**/

#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <wiringPi.h>
#include <gammaengine/Time.h>
#include <gammaengine/Debug.h>
#include "ControllerPi.h"
#include "ui/Globals.h"

ControllerPi::ControllerPi( Link* link )
	: Controller( link )
	, mADC( nullptr )
{
	wiringPiSetupGpio();
// 	pullUpDnControl( 20, PUD_DOWN );
	pullUpDnControl( 21, PUD_DOWN );
	pullUpDnControl( 0, PUD_DOWN );
	pullUpDnControl( 5, PUD_DOWN );
	pullUpDnControl( 6, PUD_DOWN );
	pullUpDnControl( 13, PUD_DOWN );
	pullUpDnControl( 19, PUD_DOWN );
	pullUpDnControl( 26, PUD_DOWN );
// 	pinMode( 20, INPUT );
	pinMode( 21, INPUT );
	pinMode( 0, INPUT );
	pinMode( 5, INPUT );
	pinMode( 6, INPUT );
	pinMode( 13, INPUT );
	pinMode( 19, INPUT );
	pinMode( 26, INPUT );
}


ControllerPi::~ControllerPi()
{
}


int8_t ControllerPi::ReadSwitch( uint32_t id )
{
	static const uint32_t map[8] = { 20, 21, 0, 5, 6, 13, 19, 26 };

	if ( id >= 8 or id == 0 ) {
		return 0;
	}

	int8_t ret = !digitalRead( map[id] );
	return ret;
}


float ControllerPi::ReadThrust()
{
	return mJoysticks[0].Read();
}


float ControllerPi::ReadRoll()
{
	return -mJoysticks[3].Read();
}


float ControllerPi::ReadPitch()
{
	return mJoysticks[2].Read();
}


float ControllerPi::ReadYaw()
{
	return -mJoysticks[1].Read();
}


bool ControllerPi::run()
{
	if ( mADC == nullptr ) {
		mADC = new MCP320x();
		mJoysticks[0] = Joystick( mADC, 0, 0, true );
		mJoysticks[1] = Joystick( mADC, 1, 1 );
		mJoysticks[2] = Joystick( mADC, 2, 2 );
		mJoysticks[3] = Joystick( mADC, 3, 3 );
	}
	if ( mADC ) {
		uint16_t battery_voltage = mADC->Read( 7 );
		if ( battery_voltage != 0 ) {
			mLocalBatteryVoltage = (float)battery_voltage * 5.0f * 4.0f / 4096.0f;
		}
	}

	return Controller::run();
}


ControllerPi::Joystick::Joystick( MCP320x* adc, int id, int adc_channel, bool thrust_mode )
	: mADC( adc )
	, mId( id )
	, mADCChannel( adc_channel )
	, mCalibrated( false )
	, mThrustMode( thrust_mode )
	, mMin( 0 )
	, mCenter( 32767 )
	, mMax( 65535 )
{
	mMin = getGlobals()->setting( "Joystick:" + std::to_string( mId ) + ":min", 0 );
	mCenter = getGlobals()->setting( "Joystick:" + std::to_string( mId ) + ":cen", 32767 );
	mMax = getGlobals()->setting( "Joystick:" + std::to_string( mId ) + ":max", 65535 );
	if ( mMin != 0 and mCenter != 32767 and mMax != 65535 ) {
		mCalibrated = true;
	}
}


ControllerPi::Joystick::~Joystick()
{
}


void ControllerPi::Joystick::SetCalibratedValues( uint16_t min, uint16_t center, uint16_t max )
{
	mMin = min;
	mCenter = center;
	mMax = max;
	mCalibrated = true;
}


uint16_t ControllerPi::Joystick::ReadRaw()
{
	if ( mADC == nullptr ) {
		return 0;
	}
	mLastRaw = mADC->Read( mADCChannel );
	return mLastRaw;
}


float ControllerPi::Joystick::Read()
{
	uint16_t raw = ReadRaw();
	if ( raw <= 0 ) {
		return -10.0f;
	}

	if ( mThrustMode ) {
		float ret = (float)( raw - mMin ) / (float)( mMax - mMin );
		ret = std::max( 0.0f, std::min( 1.0f, ret ) );
		ret = 0.01f * std::round( ret * 100.0f );
		return ret;
	}

	float base = mMax - mCenter;
	if ( raw < mCenter ) {
		base = mCenter - mMin;
	}

	float ret = (float)( raw - mCenter ) / base;
	ret = std::max( -1.0f, std::min( 1.0f, ret ) );
	ret = 0.01f * std::round( ret * 100.0f );
	return ret;
}
