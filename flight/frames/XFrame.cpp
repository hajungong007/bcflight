#include <unistd.h>
#include <Debug.h>
#include <Config.h>
#include <Controller.h>
#include "XFrame.h"
#include "IMU.h"
#include <motors/Generic.h>
#include <Servo.h>
#include <Board.h>

int XFrame::flight_register( Main* main )
{
	RegisterFrame( "XFrame", XFrame::Instanciate );
	return 0;
}


Frame* XFrame::Instanciate( Config* config )
{
	return new XFrame( config );
}


XFrame::XFrame( Config* config )
	: Frame()
	, mStabSpeeds{ 0.0f }
	, mPIDMultipliers{ Vector3f() }
	, mMaxSpeed( 1.0f )
	, mAirMode( false )
{
	mMotors.resize( 4 );

	float maxspeed = config->number( "frame.motors.max_speed" );
	if ( maxspeed > 0.0f and maxspeed <= 1.0f ) {
		mMaxSpeed = maxspeed;
	}

	// WARNING : minimum_us and maximum_us do not work for now

	int fl_pin = config->integer( "frame.motors.front_left.pin" );
	int fl_min = config->integer( "frame.motors.front_left.minimum_us" );
	int fl_max = config->integer( "frame.motors.front_left.maximum_us" );
	if ( fl_min == 0 ) { fl_min = 1060; }
	if ( fl_max == 0 ) { fl_max = 1860; }
	mMotors[0] = new Generic( new Servo( fl_pin, fl_min, fl_max ) );

	int fr_pin = config->integer( "frame.motors.front_right.pin" );
	int fr_min = config->integer( "frame.motors.front_right.minimum_us" );
	int fr_max = config->integer( "frame.motors.front_right.maximum_us" );
	if ( fr_min == 0 ) { fr_min = 1060; }
	if ( fr_max == 0 ) { fr_max = 1860; }
	mMotors[1] = new Generic( new Servo( fr_pin, fr_min, fr_max ) );

	int rl_pin = config->integer( "frame.motors.rear_left.pin" );
	int rl_min = config->integer( "frame.motors.rear_left.minimum_us" );
	int rl_max = config->integer( "frame.motors.rear_left.maximum_us" );
	if ( rl_min == 0 ) { rl_min = 1060; }
	if ( rl_max == 0 ) { rl_max = 1860; }
	mMotors[2] = new Generic( new Servo( rl_pin, rl_min, rl_max ) );

	int rr_pin = config->integer( "frame.motors.rear_right.pin" );
	int rr_min = config->integer( "frame.motors.rear_right.minimum_us" );
	int rr_max = config->integer( "frame.motors.rear_right.maximum_us" );
	if ( rr_min == 0 ) { rr_min = 1060; }
	if ( rr_max == 0 ) { rr_max = 1860; }
	mMotors[3] = new Generic( new Servo( rr_pin, rr_min, rr_max ) );


	mPIDMultipliers[0].x = config->number( "frame.motors.front_left.pid_vector.x" );
	mPIDMultipliers[0].y = config->number( "frame.motors.front_left.pid_vector.y" );
	mPIDMultipliers[0].z = config->number( "frame.motors.front_left.pid_vector.z" );
	if ( mPIDMultipliers[0].length() == 0.0f ) {
		gDebug() << "WARNING : PID multipliers for motor 0 seem to be not set !\n";
	}

	mPIDMultipliers[1].x = config->number( "frame.motors.front_right.pid_vector.x" );
	mPIDMultipliers[1].y = config->number( "frame.motors.front_right.pid_vector.y" );
	mPIDMultipliers[1].z = config->number( "frame.motors.front_right.pid_vector.z" );
	if ( mPIDMultipliers[1].length() == 0.0f ) {
		gDebug() << "WARNING : PID multipliers for motor 1 seem to be not set !\n";
	}

	mPIDMultipliers[2].x = config->number( "frame.motors.rear_left.pid_vector.x" );
	mPIDMultipliers[2].y = config->number( "frame.motors.rear_left.pid_vector.y" );
	mPIDMultipliers[2].z = config->number( "frame.motors.rear_left.pid_vector.z" );
	if ( mPIDMultipliers[2].length() == 0.0f ) {
		gDebug() << "WARNING : PID multipliers for motor 2 seem to be not set !\n";
	}

	mPIDMultipliers[3].x = config->number( "frame.motors.rear_right.pid_vector.x" );
	mPIDMultipliers[3].y = config->number( "frame.motors.rear_right.pid_vector.y" );
	mPIDMultipliers[3].z = config->number( "frame.motors.rear_right.pid_vector.z" );
	if ( mPIDMultipliers[3].length() == 0.0f ) {
		gDebug() << "WARNING : PID multipliers for motor 3 seem to be not set !\n";
	}

	mMotors[0]->Disarm();
	mMotors[1]->Disarm();
	mMotors[2]->Disarm();
	mMotors[3]->Disarm();
	Servo::HardwareSync();
}


XFrame::~XFrame()
{
}


void XFrame::Arm()
{
	fDebug0();
	memset( mStabSpeeds, 0, sizeof( mStabSpeeds ) );
	mMotors[0]->setSpeed( 0.0f, true );
	mMotors[1]->setSpeed( 0.0f, true );
	mMotors[2]->setSpeed( 0.0f, true );
	mMotors[3]->setSpeed( 0.0f, true );
}


void XFrame::Disarm()
{
	fDebug0();
	mAirMode = false;
	memset( mStabSpeeds, 0, sizeof( mStabSpeeds ) );
	mMotors[0]->Disarm();
	mMotors[1]->Disarm();
	mMotors[2]->Disarm();
	mMotors[3]->Disarm();
	Servo::HardwareSync();
}


void XFrame::WarmUp()
{
}


bool XFrame::Stabilize( const Vector3f& pid_output, const float& thrust )
{
	if ( thrust >= 0.4f ) {
		mAirMode = true;
	}

	if ( mAirMode or thrust > 0.05f ) {
		mStabSpeeds[0] = mPIDMultipliers[0] * pid_output + thrust; // Front L
		mStabSpeeds[1] = mPIDMultipliers[1] * pid_output + thrust; // Front R
		mStabSpeeds[2] = mPIDMultipliers[2] * pid_output + thrust; // Rear L
		mStabSpeeds[3] = mPIDMultipliers[3] * pid_output + thrust; // Rear R

		float motor_max = mMaxSpeed;
		float max = std::max( std::max( std::max( mStabSpeeds[0], mStabSpeeds[1] ), mStabSpeeds[2] ), mStabSpeeds[3] );
		if ( max > motor_max ) {
			float diff = max - motor_max;
			mStabSpeeds[0] -= diff;
			mStabSpeeds[1] -= diff;
			mStabSpeeds[2] -= diff;
			mStabSpeeds[3] -= diff;
		}


		if ( mAirMode ) {
			mStabSpeeds[0] = std::max( mStabSpeeds[0], 0.1f );
			mStabSpeeds[1] = std::max( mStabSpeeds[1], 0.1f );
			mStabSpeeds[2] = std::max( mStabSpeeds[2], 0.1f );
			mStabSpeeds[3] = std::max( mStabSpeeds[3], 0.1f );
		}

		mMotors[0]->setSpeed( mStabSpeeds[0] );
		mMotors[1]->setSpeed( mStabSpeeds[1] );
		mMotors[2]->setSpeed( mStabSpeeds[2] );
		mMotors[3]->setSpeed( mStabSpeeds[3] );
		Servo::HardwareSync();

		return true;
	}

	mMotors[0]->setSpeed( 0.0f );
	mMotors[1]->setSpeed( 0.0f );
	mMotors[2]->setSpeed( 0.0f );
	mMotors[3]->setSpeed( 0.0f );
	Servo::HardwareSync();
	return false;
}
