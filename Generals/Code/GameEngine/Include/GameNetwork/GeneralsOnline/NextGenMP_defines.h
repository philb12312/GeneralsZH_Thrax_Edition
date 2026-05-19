#pragma once

#if !defined(GENERALS_ONLINE)
#define GENERALS_ONLINE
#endif

//#define USE_MAULLER_ONEDRIVE_FIX 1
//#define USE_STUBBJAX_TRANSPORT_CONTAIN_FIX 1

#define GENERALS_ONLINE_LOBBY_MAX_PASSWORD_LENGTH 16

#if defined(_DEBUG)
//#define ARTIFICIAL_DELAY_HTTP_REQUESTS 1
#endif

#if defined(_DEBUG)
//#define USE_DEBUG_ON_LIVE_SERVER 1
#endif

#if !defined(_DEBUG)
//#define USE_TEST_ENV 1
#endif

#define HTTP_UPLOAD_TIMEOUT 600000

#define GENERALS_ONLINE_GAMETYPE_GENERALS
//#define GENERALS_ONLINE_GAMETYPE_ZEROHOUR

#define VANILLA_INI_CRC 4272612339

#if defined(_DEBUG)
#define RTS_MULTI_INSTANCE 1
#endif

class AsciiString;
class UnicodeString;
void showNotificationBox(AsciiString nick, UnicodeString message, bool bPlaySound = true);

#define ALLOW_NON_PROFILED_LOGIN 1

#define GENERALS_ONLINE_ENABLE_MATCH_START_COUNTDOWN

#define GENERALS_ONLINE_VERSION 1
#define GENERALS_ONLINE_NET_VERSION 1
#define GENERALS_ONLINE_SERVICE_VERSION 1

#if !_DEBUG || defined(USE_DEBUG_ON_LIVE_SERVER)
#define GENERALS_ONLINE_ENCRYPT_CREDENTIALS 1
#endif

// annoying game assertions, we'll catch real things in the debugger (or sentry)
#if !defined(_DEBUG)
#define DISABLE_DEBUG_CRASHING 1
#endif

//#define GO_REVEAL_TEAMS 1

//#define GENERALS_ONLINE_RUN_FAST 1

#define GENERALS_ONLINE_DEFAULT_LOBBY_CAMERA_ZOOM 1000
#define GENERALS_ONLINE_MIN_LOBBY_CAMERA_ZOOM 210
#define GENERALS_ONLINE_MAX_LOBBY_CAMERA_ZOOM 1000

#define GENERALS_ONLINE_HIGH_FPS_SERVER 1

#if defined(GENERALS_ONLINE_HIGH_FPS_SERVER)
#define GENERALS_ONLINE_CLIENT_ID "gen_online_60hz"
#else
#define GENERALS_ONLINE_CLIENT_ID "gen_online_30hz"
#endif

#if defined(GENERALS_ONLINE_HIGH_FPS_SERVER)
	#define GENERALS_ONLINE_HIGH_FPS_LIMIT 60
	#define GENERALS_ONLINE_HIGH_FPS_FRAME_MULTIPLIER (GENERALS_ONLINE_HIGH_FPS_LIMIT/30)
	#define GENERALS_ONLINE_HIGH_FPS_RENDER 1 // This must be defined for high fps server
#else
	#define GENERALS_ONLINE_HIGH_FPS_LIMIT 30
	#define GENERALS_ONLINE_HIGH_FPS_FRAME_MULTIPLIER 1

	#define GENERALS_ONLINE_HIGH_FPS_RENDER 1 // This is optional on 30fps, but will boost/unlock the framerate, similar to gentool
#endif

#if defined(GENERALS_ONLINE_HIGH_FPS_SERVER)
#define GENERALS_ONLINE_DEFAULT_FRAME_GROUPING_CAP 32
#else
#define GENERALS_ONLINE_DEFAULT_FRAME_GROUPING_CAP 64
#endif

int GetFrameGroupingCap();
void SetFrameGroupingCap(int frameGroupingCap);

//#define GENERALS_ONLINE_ENABLE_CONTROVERSIAL_NON_RETAIL_CHANGES 1

#define GENERALS_ONLINE_USE_LARGER_DMAPOOL 1

#if !_DEBUG
#define GENERALS_ONLINE_USE_SENTRY 1
#endif

// NOTE: This is temporary until we work out why this causes mismatch when some players set it and others dont
#if !_DEBUG
#define GENERALS_ONLINE_DISABLE_QUICKSTART_FUNCTIONALITY 1
#endif

#define GENERALS_ONLINE_WIDESCREEN 1
