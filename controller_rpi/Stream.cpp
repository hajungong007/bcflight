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

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <iwlib.h>
#include <wiringPi.h>
#include <gammaengine/Debug.h>
#include <gammaengine/Time.h>
#include <gammaengine/Image.h>
#include <links/RawWifi.h>
#include <links/Socket.h>
#include "Stream.h"
#include "Controller.h"
#include "RendererHUDClassic.h"
#include "RendererHUDNeo.h"

#define UDPLITE_SEND_CSCOV   10 /* sender partial coverage (as sent)      */
#define UDPLITE_RECV_CSCOV   11 /* receiver partial coverage (threshold ) */

/*
std::string interface_address( const std::string& itf )
{
	int fd;
	struct ifreq ifr;

	fd = socket( AF_INET, SOCK_DGRAM, 0 );
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy( ifr.ifr_name, itf.c_str(), IFNAMSIZ - 1 );
	ioctl( fd, SIOCGIFADDR, &ifr );
	close( fd );

	return std::string( inet_ntoa( ( (struct sockaddr_in *)&ifr.ifr_addr )->sin_addr ) );
}
*/

Stream::Stream( Controller* controller, Font* font, Link* link, uint32_t width, uint32_t height, bool stereo )
	: Thread()
	, mInstance( nullptr )
	, mWindow( nullptr )
	, mController( controller )
	, mFont( font )
	, mRendererHUD( nullptr )
	, mRenderHUD( true )
	, mStereo( stereo )
	, mWidth( width )
	, mHeight( height )
	, mLink( link )
	, mDecodeThread( nullptr )
	, mDecodeInput( nullptr )
	, mEGLVideoImage( 0 )
	, mVideoTexture( 0 )
{
	mFPS = 0;
	mFrameCounter = 0;
	mDecodeLen = 0;

	mIwSocket = iw_sockets_open();
	memset( &mIwStats, 0, sizeof( mIwStats ) );

	mFPSTimer = Timer();
	mSecondTimer = Timer();
	mSecondTimer.Start();
	Start();

	mSignalThread = new HookThread< Stream >( "signal-quality", this, &Stream::SignalThreadRun );
	mSignalThread->Start();
}


Stream::~Stream()
{
	vc_dispmanx_resource_delete( mScreenshot.resource );
	vc_dispmanx_display_close( mDisplay );
}

void Stream::setStereo( bool en )
{
	while ( not this->running() or not mRendererHUD ) {
		usleep( 1000 );
	}
	mStereo = en;
	mRendererHUD->setStereo( en );
}


bool Stream::run()
{
	if ( !mInstance and !mWindow ) {
		mInstance = Instance::Create( "flight::control", 1, false, "opengles20" );
		mWindow = mInstance->CreateWindow( "", -1, -1, Window::Fullscreen );

		mLayerDisplay = CreateNativeWindow( 2 );
		mWindow->SetNativeWindow( reinterpret_cast< EGLNativeWindowType >( &mLayerDisplay ) );

		mRendererHUD = new RendererHUDNeo( mInstance, mFont );

// 		mDecodeContext = video_configure( mWidth, mHeight, mStereo );
// 		video_start( mDecodeContext );
		float reduce = 0.25f;
		int y_offset = mStereo * 56 + ( reduce * mHeight * 0.1 );
		int width = mWidth / ( 1 + mStereo ) - ( reduce * mWidth * 0.1 );
		int height = mHeight - ( mStereo * 112 ) - ( reduce * mHeight * 0.2 );

		OMX_Init();
		mDecoder = new IL::VideoDecode( 0, IL::VideoDecode::CodingAVC, true );
		mDecoderRender1 = new IL::VideoRender( reduce * mWidth * 0.05, y_offset, width, height, true );
		mDecoderRender1->setMirror( false, true );
		if ( mStereo ) {
			mDecoderSplitter = new IL::VideoSplitter( true );
			mDecoderRender2 = new IL::VideoRender( mWidth / 2 + reduce * mWidth * 0.05, y_offset, width, height, true );
			mDecoderRender2->setMirror( false, true );
			mDecoder->SetupTunnel( 131, mDecoderSplitter, 250 );
			mDecoderSplitter->SetupTunnel( 251, mDecoderRender1, 90 );
			mDecoderSplitter->SetupTunnel( 252, mDecoderRender2, 90 );
			mDecoder->SetState( IL::Component::StateExecuting );
			mDecoderSplitter->SetState( IL::Component::StateExecuting );
			mDecoderRender1->SetState( IL::Component::StateExecuting );
			mDecoderRender2->SetState( IL::Component::StateExecuting );
		} else {
			mDecoder->SetupTunnel( 131, mDecoderRender1, 90 );
			mDecoder->SetState( IL::Component::StateExecuting );
			mDecoderRender1->SetState( IL::Component::StateExecuting );
		}

		mDecodeThread = new HookThread< Stream >( "decoder", this, &Stream::DecodeThreadRun );
		mDecodeThread->setPriority( 99 );
		mDecodeThread->Start();

		mFPSTimer.Start();

		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	}

	glClear( GL_COLOR_BUFFER_BIT );

	if ( mRenderHUD ) {
		VideoStats video_stats = {
			.fps = mFPS,
		};
		mRendererHUD->PreRender( &video_stats );
		mRendererHUD->Render( mWindow, mController, &video_stats, &mIwStats );
	} else {
		usleep( 1000 * 1000 / 30 );
	}

	if ( mSecondTimer.ellapsed() >= 1000 ) {
		/*
		vc_dispmanx_snapshot( mDisplay, mScreenshot.resource, DISPMANX_NO_ROTATE );
		vc_dispmanx_resource_read_data( mScreenshot.resource, &mScreenshot.rect, mScreenshot.image, 1920 / 2 * 3 );
		uint32_t r = 0;
		uint32_t g = 0;
		uint32_t b = 0;
		for ( uint32_t y = 0; y < 1080; y += 36 ) {
			for ( uint32_t x = 0; x < 1920 / 2; x += 32 ) {
				r += ((uint8_t*)mScreenshot.image)[ ( y * 1920 / 2 + x ) * 3 + 0 ];
				g += ((uint8_t*)mScreenshot.image)[ ( y * 1920 / 2 + x ) * 3 + 1 ];
				b += ((uint8_t*)mScreenshot.image)[ ( y * 1920 / 2 + x ) * 3 + 2 ];
			}
		}
		mRendererHUD->setNightMode( r / 900 < 32 and g / 900 < 32 and b / 900 < 32 );
		*/
		mSecondTimer.Stop();
		mSecondTimer.Start();
	}

	mWindow->SwapBuffers();
// 	printf( "%.2f\n", mWindow->fps() );
	return true;
}


bool Stream::SignalThreadRun()
{
	iwstats stats;
	wireless_config info;
	iwrange range;
	memset( &stats, 0, sizeof( stats ) );
	if ( iw_get_stats( mIwSocket, "wlan0", &stats, nullptr, 0 ) == 0 ) {
		mIwStats.qual = (int)stats.qual.qual * 100 / 70;
		union { int8_t s; uint8_t u; } conv = { .u = stats.qual.level };
		mIwStats.level = conv.s;
		mIwStats.noise = stats.qual.noise;
	}
	if ( iw_get_basic_config( mIwSocket, "wlan0", &info ) == 0 ) {
		if ( iw_get_range_info( mIwSocket, "wlan0", &range ) == 0 ) {
			mIwStats.channel = iw_freq_to_channel( info.freq, &range );
		}
	}
	if ( dynamic_cast< RawWifi* >( mLink ) != nullptr and mController != nullptr ) {
		mIwStats.source = 'R';
		mIwStats.qual = mController->link()->RxQuality();
		if ( (int)mController->droneRxQuality() < mIwStats.qual ) {
			mIwStats.source = 'T';
			mIwStats.qual = mController->droneRxQuality();
		}
		if ( mLink->RxQuality() < mIwStats.qual ) {
			mIwStats.source = 'V';
			mIwStats.qual = mLink->RxQuality();
		}
		mIwStats.level = dynamic_cast< RawWifi* >( mLink )->level();
// 		mIwStats.channel = dynamic_cast< RawWifi* >( mLink )->channel();
	}
/*
	if ( not mDecodeThread or not mDecodeThread->running() ) {
		mDecodeThread = new HookThread< Stream >( "decoder", this, &Stream::DecodeThreadRun );
		mDecodeThread->setPriority( 99 );
		mDecodeThread->Start();
	}
	*/
	usleep( 1000 * 250 );
	return true;
}


bool Stream::DecodeThreadRun()
{
// 	uint8_t frame[32768] = { 0 };
// 	uint8_t* frame = nullptr;
	int32_t frameSize = 0;

	if ( not mLink->isConnected() ) {
		mLink->Connect();
		if ( mLink->isConnected() ) {
			uint32_t uid = htonl( 0x12345678 );
			mLink->Write( &uid, sizeof(uid), 0 );
		} else {
			usleep( 1000 * 1000 );
		}
		return true;
	}
/*
	if ( mDecodeInput == nullptr ) {
		mDecodeInput = mDecodeContext->decinput->pBuffer;
	} else {
		mDecodeContext->decinput->pBuffer = (OMX_U8*)mDecodeInput;
	}

	if ( mDecodeContext->decinput->pBuffer != mDecodeInput ) {
		gDebug() << "GPU messed up decoder input buffer ! ( " << (void*)mDecodeContext->decinput->pBuffer << " != " << (void*)mDecodeInput << "\n";
	}
	if ( mDecodeLen == 0 ) {
		mDecodeLen = mDecodeContext->decinput->nAllocLen;
	}*/

	uint8_t buffer[65536] = { 0 };
	frameSize = mLink->Read( buffer, sizeof(buffer), 0 );
	int corrupted = 0;
	if ( dynamic_cast< RawWifi* >( mLink ) != nullptr ) {
		corrupted = dynamic_cast< RawWifi* >( mLink )->lastIsCorrupt();
	}
	if ( frameSize > 0 ) {
		mDecoder->fillInput( buffer, frameSize );
		mFrameCounter++;
		mBitrateCounter += frameSize;
		frameSize = 0;
	}

	if ( mFPSTimer.ellapsed() >= 1000 ) {
		mFPS = mFrameCounter;
		mFPSTimer.Stop();
		mFPSTimer.Start();
// 		gDebug() << "fps : " << ( mFPS * 1 ) << "    bitrate : " << ( mBitrateCounter * 8 / 1024 ) << "kbps ( " << ( mBitrateCounter / 1024 ) << " KBps )\n";
		mFrameCounter = 0;
		mBitrateCounter = 0;

		if ( dynamic_cast< Socket* >( mLink ) != nullptr ) {
			uint32_t uid = htonl( 0x12345678 );
			mLink->Write( &uid, sizeof(uid), 0 );
		}
	}

	return true;
}


EGL_DISPMANX_WINDOW_T Stream::CreateNativeWindow( int layer )
{
	EGL_DISPMANX_WINDOW_T nativewindow;
	DISPMANX_ELEMENT_HANDLE_T dispman_element;
	DISPMANX_UPDATE_HANDLE_T dispman_update;
	VC_RECT_T dst_rect;
	VC_RECT_T src_rect;

	uint32_t display_width;
	uint32_t display_height;

	graphics_get_display_size( 5, &display_width, &display_height );
	gDebug() << "display size: " << display_width << " x "<< display_height << "\n";

	dst_rect.x = 0;
	dst_rect.y = 0;
	dst_rect.width = display_width;
	dst_rect.height = display_height;
	
	display_width = 1280;
	display_height = 720;

	src_rect.x = 0;
	src_rect.y = 0;
	src_rect.width = display_width << 16;
	src_rect.height = display_height << 16;

	mDisplay = vc_dispmanx_display_open( 5 );
	dispman_update = vc_dispmanx_update_start( 0 );

	VC_DISPMANX_ALPHA_T alpha;
	alpha.flags = (DISPMANX_FLAGS_ALPHA_T)( DISPMANX_FLAGS_ALPHA_FROM_SOURCE );
// 	alpha.flags = (DISPMANX_FLAGS_ALPHA_T)( DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS | DISPMANX_FLAGS_ALPHA_PREMULT );
	alpha.opacity = 0;

	dispman_element = vc_dispmanx_element_add( dispman_update, mDisplay, layer, &dst_rect, 0, &src_rect, DISPMANX_PROTECTION_NONE, &alpha, 0, DISPMANX_NO_ROTATE );
	nativewindow.element = dispman_element;
	nativewindow.width = display_width;
	nativewindow.height = display_height;
	vc_dispmanx_update_submit_sync( dispman_update );

	mScreenshot.image = new uint8_t[ 1920 / 2 * 1080 * 3 ];
	mScreenshot.resource = vc_dispmanx_resource_create( VC_IMAGE_RGB888, 1920 / 2, 1080, &mScreenshot.vc_image_ptr );
	vc_dispmanx_rect_set( &mScreenshot.rect, 0, 0, 1920 / 2, 1080 );

	return nativewindow;
}

