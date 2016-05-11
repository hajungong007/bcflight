#include <unistd.h>
#include "Main.h"
#include "PowerThread.h"
#include <Sensor.h>
#include <Voltmeter.h>
#include <CurrentSensor.h>

PowerThread::PowerThread( Main* main )
	: Thread( "power" )
	, mSaveTicks( Board::GetTicks() )
	, mTicks( Board::GetTicks() )
	, mMain( main )
	, mVBat( 0.0f )
	, mCurrentTotal( 0.0f )
	, mCurrentDraw( 0.0f )
	, mBatteryLevel( 0.0f )
	, mBatteryCapacity( 2500.0f )
	, mVoltageSensor{ NONE, nullptr, 0, 0, 0 }
	, mCurrentSensor{ NONE, nullptr, 0, 0, 0 }
{
	mBatteryCapacity = main->config()->integer( "battery.capacity" );
	if ( mBatteryCapacity == 0.0f ) {
		mBatteryCapacity = 1800.0f;
	}

	{
		std::string sensorType = main->config()->string( "battery.voltage.sensor_type" );
		if ( sensorType == "Voltmeter" ) {
			mVoltageSensor.type = VOLTAGE;
			mVoltageSensor.sensor = Sensor::voltmeter( main->config()->string( "battery.voltage.device" ) );
		} else {
			gDebug() << "FATAL ERROR : Unsupported sensor type ( " << sensorType << " ) for battery voltage !\n";
		}
		mVoltageSensor.channel = main->config()->integer( "battery.voltage.channel" );
		mVoltageSensor.shift = main->config()->number( "battery.voltage.shift" );
		mVoltageSensor.multiplier = main->config()->number( "battery.voltage.multiplier" );
	}
	{
		std::string sensorType = main->config()->string( "battery.current.sensor_type" );
		if ( sensorType == "Voltmeter" ) {
			mCurrentSensor.type = VOLTAGE;
			mCurrentSensor.sensor = Sensor::voltmeter( main->config()->string( "battery.current.device" ) );
		} else if ( sensorType == "CurrentSensor" ) {
			mCurrentSensor.type = CURRENT;
			mCurrentSensor.sensor = Sensor::currentSensor( main->config()->string( "battery.current.device" ) );
		} else {
			gDebug() << "WARNING : Unsupported sensor type ( " << sensorType << " ) for battery current !\n";
		}
		mCurrentSensor.channel = main->config()->integer( "battery.current.channel" );
		mCurrentSensor.shift = main->config()->number( "battery.current.shift" );
		mCurrentSensor.multiplier = main->config()->number( "battery.current.multiplier" );
	}

	mLastVBat = std::atof( Board::LoadRegister( "VBat" ).c_str() );
	mCurrentTotal = std::atof( Board::LoadRegister( "CurrentTotal" ).c_str() );
}


PowerThread::~PowerThread()
{
}


float PowerThread::VBat() const
{
	return mVBat;
}


float PowerThread::CurrentTotal() const
{
	return mCurrentTotal;
}


float PowerThread::CurrentDraw() const
{
	return mCurrentDraw;
}


float PowerThread::BatteryLevel() const
{
	return mBatteryLevel;
}


void PowerThread::ResetFullBattery( uint32_t capacity_mah )
{
	mCapacityMutex.lock();
	mCurrentTotal = 0.0f;
	if ( capacity_mah != 0 ) {
// 		mBatteryCapacity = (float)capacity_mah;
	}
	mCapacityMutex.unlock();
}


bool PowerThread::run()
{
	float dt = (float)( Board::GetTicks() - mTicks ) / 1000000.0f;
	mTicks = Board::GetTicks();

	if ( std::abs( dt ) >= 1.0 ) {
		gDebug() << "Critical : dt too high !! ( " << dt << " )\n";
		return true;
	}

	float volt = 0.0f;
	float current = 0.0f;

	if ( mVoltageSensor.sensor ) {
		if ( mVoltageSensor.type == VOLTAGE ) {
			volt = dynamic_cast< Voltmeter* >( mVoltageSensor.sensor )->Read( mVoltageSensor.channel );
		}
		volt = ( volt + mVoltageSensor.shift ) * mVoltageSensor.multiplier;
	}
	if ( mCurrentSensor.sensor ) {
		if ( mCurrentSensor.type == VOLTAGE ) {
			current = dynamic_cast< Voltmeter* >( mCurrentSensor.sensor )->Read( mCurrentSensor.channel );
		} else if ( mCurrentSensor.type == CURRENT ) {
			current = dynamic_cast< CurrentSensor* >( mCurrentSensor.sensor )->Read( mCurrentSensor.channel );
		}
		current = ( current + mCurrentSensor.shift ) * mCurrentSensor.multiplier;
	}

	mVBat = volt;
	mCurrentDraw = current / 3600.0f;

	mCapacityMutex.lock();
	mCurrentTotal += current * dt / 3600.0f;
	mBatteryLevel = std::max( 0.0f, std::min( 1.0f, 1.0f - ( mCurrentTotal * 1000.0f ) / mBatteryCapacity ) );

	if ( Board::GetTicks() - mSaveTicks >= 5 * 1000 * 1000 ) {
		Board::SaveRegister( "VBat", std::to_string( mVBat ) );
		Board::SaveRegister( "CurrentTotal", std::to_string( mCurrentTotal ) );
		Board::SaveRegister( "BatteryCapacity", std::to_string( mBatteryCapacity ) );
		mSaveTicks = Board::GetTicks();
	}

	mCapacityMutex.unlock();

	usleep( 1000 * 50 );
	return true;
}
