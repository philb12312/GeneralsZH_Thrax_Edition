/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: W3DRadar.cpp /////////////////////////////////////////////////////////////////////////////
// Author: Colin Day, January 2002
// Desc:   W3D radar implementation, this has the necessary device dependent drawing
//				 necessary for the radar
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/AudioEventRTS.h"
#include "Common/Debug.h"
#include "Common/GlobalData.h"
#include "Common/GameUtility.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"

#include "GameLogic/TerrainLogic.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"

#include "GameClient/Color.h"
#include "GameClient/Display.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameWindow.h"
#include "GameClient/Image.h"
#include "GameClient/Line2D.h"
#include "GameClient/TerrainVisual.h"
#include "GameClient/Water.h"
#include "W3DDevice/Common/W3DRadar.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "WW3D2/texture.h"
#include "WW3D2/textureloader.h"
#include "WW3D2/dx8caps.h"
#include "WWMath/vector2i.h"
#include "GameClient/ControlBar.h"


extern "C" int PerfBeginScopedLogicSub(const char *tag);
extern "C" void PerfEndScopedLogicSub(int token);
extern "C" void PerfRecordRenderFeatureStats(const char *tag, int highFpsActive, int processed, int rendered,
	int culled, int skipped, int budget);

namespace
{
	class RadarPerfScope
	{
	public:
		explicit RadarPerfScope(const char *tag) :
			m_token(PerfBeginScopedLogicSub(tag))
		{
		}

		~RadarPerfScope()
		{
			PerfEndScopedLogicSub(m_token);
		}

	private:
		int m_token;
	};

	#define RADAR_PERF_JOIN2(a, b) a##b
	#define RADAR_PERF_JOIN(a, b) RADAR_PERF_JOIN2(a, b)
	#define RADAR_PERF_SCOPE(tag) RadarPerfScope RADAR_PERF_JOIN(_radarPerfScope_, __LINE__)(tag)
}


// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
enum
{
	BASE_RADAR_CELL_RESOLUTION = 512,
	BASE_RADAR_UNIT_BLIP_SIZE = 8,
	BASE_RADAR_STRUCTURE_BLIP_SIZE = 10,
	BASE_RADAR_STRUCTURE_OUTLINE_THICKNESS = 2,
	BASE_RADAR_BEACON_MIN_EVENT_SIZE = 6,
	BASE_RADAR_GENERIC_MIN_EVENT_SIZE = 20,
	HERO_RETICLE_SIZE_SCALE = 2,
	OVERLAY_REFRESH_RATE = 6,	///< over updates once this many frames
	RADAR_UNIT_BLIP_SIZE = (BASE_RADAR_UNIT_BLIP_SIZE * RADAR_CELL_WIDTH + BASE_RADAR_CELL_RESOLUTION - 1) / BASE_RADAR_CELL_RESOLUTION,
	RADAR_STRUCTURE_BLIP_SIZE = (BASE_RADAR_STRUCTURE_BLIP_SIZE * RADAR_CELL_WIDTH + BASE_RADAR_CELL_RESOLUTION - 1) / BASE_RADAR_CELL_RESOLUTION,
	RADAR_STRUCTURE_OUTLINE_THICKNESS = (BASE_RADAR_STRUCTURE_OUTLINE_THICKNESS * RADAR_CELL_WIDTH + BASE_RADAR_CELL_RESOLUTION - 1) / BASE_RADAR_CELL_RESOLUTION
};

//-------------------------------------------------------------------------------------------------
/** Is the point legal, that is, inside the resolution of the radar cells */
//-------------------------------------------------------------------------------------------------
inline Bool legalRadarPoint( Int px, Int py )
{

	if( px < 0 || py < 0 || px >= RADAR_CELL_WIDTH || py >= RADAR_CELL_HEIGHT )
		return FALSE;

	return TRUE;

}

//-------------------------------------------------------------------------------------------------
/** Return true when the structure should display a resource X marker on the minimap */
//-------------------------------------------------------------------------------------------------
static Bool isRadarResourceStructure( const Object *obj )
{
	if( obj == nullptr )
		return FALSE;

	if( obj->isKindOf( KINDOF_SUPPLY_SOURCE ) )
		return TRUE;

	const ThingTemplate *thing = obj->getTemplate();
	return thing != nullptr && thing->getName() == "TechOilDerrick";
}

//-------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
static WW3DFormat findFormat(const WW3DFormat formats[])
{
	for( Int i = 0; formats[ i ] != WW3D_FORMAT_UNKNOWN; i++ )
	{

		if( DX8Wrapper::Get_Current_Caps()->Support_Texture_Format( formats[ i ] ) )
		{

			return formats[ i ];

		}

	}
	DEBUG_CRASH(("WW3DRadar: No appropriate texture format") );
	return WW3D_FORMAT_UNKNOWN;
}

//-------------------------------------------------------------------------------------------------
/** Find the texture format we're going to use for the radar.  The texture format must
	* be supported by the hardware.  The "more preferred" formats appear at the top of
	* the format tables in order from most preferred to least preferred */
//-------------------------------------------------------------------------------------------------
void W3DRadar::initializeTextureFormats()
{
	const WW3DFormat terrainFormats[] =
	{
		WW3D_FORMAT_R8G8B8,
		WW3D_FORMAT_X8R8G8B8,
		WW3D_FORMAT_R5G6B5,
		WW3D_FORMAT_X1R5G5B5,
		WW3D_FORMAT_UNKNOWN				// keep this one last
	};
	const WW3DFormat overlayFormats[] =
	{
		WW3D_FORMAT_A8R8G8B8,
		WW3D_FORMAT_A4R4G4B4,
		WW3D_FORMAT_UNKNOWN				// keep this one last
	};
	const WW3DFormat shroudFormats[] =
	{
		WW3D_FORMAT_A8R8G8B8,
		WW3D_FORMAT_A4R4G4B4,
		WW3D_FORMAT_UNKNOWN				// keep this one last
	};

	// find a format for the terrain texture
	m_terrainTextureFormat = findFormat(terrainFormats);

	// find a format for the overlay texture
	m_overlayTextureFormat = findFormat(overlayFormats);

	// find a format for the shroud texture
	m_shroudTextureFormat = findFormat(shroudFormats);

}

//-------------------------------------------------------------------------------------------------
/** Delete resources used specifically in this W3D radar implementation */
//-------------------------------------------------------------------------------------------------
void W3DRadar::deleteResources()
{

	//
	// delete terrain resources used
	//
	if( m_terrainTexture )
		m_terrainTexture->Release_Ref();
	m_terrainTexture = nullptr;
	if( m_terrainImage )
		deleteInstance(m_terrainImage);
	m_terrainImage = nullptr;

	//
	// delete overlay resources used
	//
	if( m_overlayTexture )
		m_overlayTexture->Release_Ref();
	m_overlayTexture = nullptr;
	if( m_overlayImage )
		deleteInstance(m_overlayImage);
	m_overlayImage = nullptr;

	//
	// delete shroud resources used
	//
	if( m_shroudTexture )
		m_shroudTexture->Release_Ref();
	m_shroudTexture = nullptr;
	if( m_shroudImage )
		deleteInstance(m_shroudImage);
	m_shroudImage = nullptr;

	DEBUG_ASSERTCRASH(m_shroudSurface == nullptr, ("W3DRadar::deleteResources: m_shroudSurface is expected null"));
	DEBUG_ASSERTCRASH(m_shroudSurfaceBits == nullptr, ("W3DRadar::deleteResources: m_shroudSurfaceBits is expected null"));
	if( m_shroudCellAlphaBuffer != nullptr )
		delete[] m_shroudCellAlphaBuffer;
	m_shroudCellAlphaBuffer = nullptr;
	m_shroudTextureDirty = FALSE;
	m_shroudDataWidth = 0;
	m_shroudDataHeight = 0;

}

//-------------------------------------------------------------------------------------------------
/** Reconstruct the view box given the current camera settings */
//-------------------------------------------------------------------------------------------------
void W3DRadar::reconstructViewBox()
{
	Coord3D world[ 4 ];
	ICoord2D radar[ 4 ];
	Int i;

	// get the 4 points of the view corners in the 3D world at the average Z height in the map
	//  1-------2
	//   \     /
	//    4---3
	TheTacticalView->getScreenCornerWorldPointsAtZ( &world[ 0 ],
																									&world[ 1 ],
																									&world[ 2 ],
																									&world[ 3 ],
																									getTerrainAverageZ() );

	// convert each of the 4 points in the world to radar cell positions
	for( i = 0; i < 4; i++ )
	{

		// first convert to radar cells
 		radar[ i ].x = world[ i ].x / (m_mapExtent.width() / RADAR_CELL_WIDTH);
 		radar[ i ].y = world[ i ].y / (m_mapExtent.height() / RADAR_CELL_HEIGHT);

		//
		// store these points in the view box array which contains a first position
		// of (0,0) and then offsets for each additional entry point
		//
		if( i == 0 )
		{

			m_viewBox[ i ].x = 0;
			m_viewBox[ i ].y = 0;

		}
		else
		{

			m_viewBox[ i ].x = radar[ i ].x - radar[ i - 1 ].x;
			m_viewBox[ i ].y = radar[ i ].y - radar[ i - 1 ].y;

		}

	}

	m_reconstructViewBox = FALSE;

}

//-------------------------------------------------------------------------------------------------
/** Convert radar position to actual pixel coord */
//-------------------------------------------------------------------------------------------------
void W3DRadar::radarToPixel( const ICoord2D *radar, ICoord2D *pixel,
														 Int radarUpperLeftX, Int radarUpperLeftY,
														 Int radarWidth, Int radarHeight )
{

	// sanity
	if( radar == nullptr || pixel == nullptr )
		return;

	pixel->x = (radar->x * radarWidth / RADAR_CELL_WIDTH) + radarUpperLeftX;
	// note the "inverted" y here to orient the way our world looks with +x=right and -y=down
	pixel->y = ((RADAR_CELL_HEIGHT - 1 - radar->y) * radarHeight / RADAR_CELL_HEIGHT) + radarUpperLeftY;

}


//-------------------------------------------------------------------------------------------------
/** Draw a hero icon at a position, given radar box upper left location and dimensions.  */
//-------------------------------------------------------------------------------------------------
void W3DRadar::drawHeroIcon( Int pixelX, Int pixelY, Int width, Int height, const Coord3D *pos )
{
	// get the hero icon image
	static const Image *image = (Image *)TheMappedImageCollection->findImageByName("HeroReticle");
	if (image != nullptr)
	{
		// convert world to radar coords
		ICoord2D ulRadar;
		ulRadar.x = pos->x / (m_mapExtent.width() / RADAR_CELL_WIDTH);
		ulRadar.y = pos->y / (m_mapExtent.height() / RADAR_CELL_HEIGHT);

		// convert radar to screen coords
		ICoord2D offsetScreen;
		radarToPixel( &ulRadar, &offsetScreen, pixelX, pixelY, width, height );

		// shift from an upper left to a center focus for the icon
		int iconWidth = image->getImageWidth() * HERO_RETICLE_SIZE_SCALE;
		int iconHeight = image->getImageHeight() * HERO_RETICLE_SIZE_SCALE;
		offsetScreen.x -= (iconWidth / 2) - 1;
		offsetScreen.y -= iconHeight / 2;

		// draw the icon
		TheDisplay->drawImage( image, offsetScreen.x , offsetScreen.y, offsetScreen.x + iconWidth, offsetScreen.y + iconHeight );
	}
}

//-------------------------------------------------------------------------------------------------
/** Draw a "box" into the texture passed in that represents the viewable area for
	* the tactical display into the game world */
//-------------------------------------------------------------------------------------------------
void W3DRadar::drawViewBox( Int pixelX, Int pixelY, Int width, Int height )
{
	RADAR_PERF_SCOPE("Cl_RD_ViewBox");

	ICoord2D ulScreen;
	ICoord2D ulRadar;
	Coord3D ulWorld;
	ICoord2D ulStart = { 0, 0 };
	ICoord2D start, end;
	ICoord2D clipStart, clipEnd;
	Real lineWidth = 1.0f;
	Color topColor = GameMakeColor( 225, 225, 0, 255 );
	Color bottomColor = GameMakeColor( 158, 158, 0, 255 );

	//
	// setup the clipping region ... note that this clipping region is not over just the
	// radar image area ... it's in the WHOLE window available for the radar
	//
	IRegion2D clipRegion;
	ICoord2D radarWindowSize, radarWindowScreenPos;
	m_radarWindow->winGetSize( &radarWindowSize.x, &radarWindowSize.y );
	m_radarWindow->winGetScreenPosition( &radarWindowScreenPos.x, &radarWindowScreenPos.y );
	clipRegion.lo.x = radarWindowScreenPos.x;
	clipRegion.lo.y = radarWindowScreenPos.y;
	clipRegion.hi.x = radarWindowScreenPos.x + radarWindowSize.x;
	clipRegion.hi.y = radarWindowScreenPos.y + radarWindowSize.y;

	// convert top left of screen into world position
	TheTacticalView->getOrigin( &ulScreen.x, &ulScreen.y );
	TheTacticalView->screenToWorldAtZ( &ulScreen, &ulWorld, getTerrainAverageZ() );

	// convert world to radar coords
 	ulRadar.x = ulWorld.x / (m_mapExtent.width() / RADAR_CELL_WIDTH);
 	ulRadar.y = ulWorld.y / (m_mapExtent.height() / RADAR_CELL_HEIGHT);

	//
	// convert radar point to actual pixel coords on the screen, shifted
	// into position on the radar for where the radar is drawn and the size of the
	// area that the radar is drawn in
	//
	radarToPixel( &ulRadar, &ulStart, pixelX, pixelY, width, height );

	//
	// using our view box offset array, convert each of those radar cell offset points
	// into screen pixels and draw the box.  The view box array is setup with the
	// first index containing (0,0) (the point we just converted in theory), with cell
	// offsets to each of the other corners in the following order
	// (upper left, upper right, lower right, lower left)
	//
	ICoord2D radar;

	// top line
	start = ulStart;
	radar.x = ulRadar.x + m_viewBox[ 1 ].x;
	radar.y = ulRadar.y + m_viewBox[ 1 ].y;
	radarToPixel( &radar, &end, pixelX, pixelY, width, height );
	if( ClipLine2D( &start, &end, &clipStart, &clipEnd, &clipRegion ) )
		TheDisplay->drawLine( clipStart.x, clipStart.y, clipEnd.x, clipEnd.y,
													lineWidth, topColor );

  // right line
	start = end;
	radar.x += m_viewBox[ 2 ].x;
	radar.y += m_viewBox[ 2 ].y;
	radarToPixel( &radar, &end, pixelX, pixelY, width, height );
	if( ClipLine2D( &start, &end, &clipStart, &clipEnd, &clipRegion ) )
		TheDisplay->drawLine( clipStart.x, clipStart.y, clipEnd.x, clipEnd.y,
													lineWidth, topColor, bottomColor );

  // bottom line
	start = end;
	radar.x += m_viewBox[ 3 ].x;
	radar.y += m_viewBox[ 3 ].y;
	radarToPixel( &radar, &end, pixelX, pixelY, width, height );
	if( ClipLine2D( &start, &end, &clipStart, &clipEnd, &clipRegion ) )
		TheDisplay->drawLine( clipStart.x, clipStart.y, clipEnd.x, clipEnd.y,
													lineWidth, bottomColor );

  // left line
	start = end;
	end = ulStart;
	if( ClipLine2D( &start, &end, &clipStart, &clipEnd, &clipRegion ) )
		TheDisplay->drawLine( clipStart.x, clipStart.y, clipEnd.x, clipEnd.y,
													lineWidth, bottomColor, topColor );

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DRadar::drawSingleBeaconEvent( Int pixelX, Int pixelY, Int width, Int height, Int index )
{
	RadarEvent *event = &(m_event[index]);
	ICoord2D tri[ 3 ];
	ICoord2D start, end;
	Real angle, addAngle;
	Color startColor, endColor;
	Real lineWidth = 1.0f;
	UnsignedInt currentFrame = TheGameLogic->getFrame();
	UnsignedInt frameDiff;							// frames the event has been alive for
	const Real radarResolutionScale = (Real)RADAR_CELL_WIDTH / (Real)BASE_RADAR_CELL_RESOLUTION;
	Real maxEventSize = (width / 10.0f) * radarResolutionScale;   // max size of the event marker in radar-space units
	Int minEventSize = REAL_TO_INT( (Real)BASE_RADAR_BEACON_MIN_EVENT_SIZE * radarResolutionScale + 0.5f );	 // min size of the event marker
	Int eventSize;									 // current size of a marker to draw
	const Real TIME_FROM_FULL_SIZE_TO_SMALL_SIZE = LOGICFRAMES_PER_SECOND * 1.5;
	Real totalAnglesToSpin = 2.0f * PI;  ///< spin around this many angles going from big to small
	UnsignedByte r, g, b, a;

	// setup screen clipping region
	IRegion2D clipRegion;
	clipRegion.lo.x = pixelX;
	clipRegion.lo.y = pixelY;
	clipRegion.hi.x = pixelX + width;
	clipRegion.hi.y = pixelY + height;

	// get the difference in frame from the current frame to the frame we were created on
	frameDiff = currentFrame - event->createFrame;

	// compute the size of the event marker, it is largest when it starts and smallest at the end
	eventSize = REAL_TO_INT( maxEventSize * ( 1.0f - frameDiff / TIME_FROM_FULL_SIZE_TO_SMALL_SIZE) );

	// we never let the event size get too small
	if( eventSize < minEventSize )
		eventSize = minEventSize;

	// compute how much "angle" we will add to each point to make it rotate as it's getting small
	addAngle = -totalAnglesToSpin * (frameDiff / TIME_FROM_FULL_SIZE_TO_SMALL_SIZE);

	// create a triangle around the event
	angle = 0.0f - addAngle;
	tri[ 0 ].x = REAL_TO_INT( (DOUBLE_TO_REAL( Cos( angle ) ) * eventSize) + event->radarLoc.x );
	tri[ 0 ].y = REAL_TO_INT( (DOUBLE_TO_REAL( Sin( angle ) ) * eventSize) + event->radarLoc.y );

	angle = 2.0f * PI / 3.0f - addAngle;
	tri[ 1 ].x = REAL_TO_INT( (DOUBLE_TO_REAL( Cos( angle ) ) * eventSize) + event->radarLoc.x );
	tri[ 1 ].y = REAL_TO_INT( (DOUBLE_TO_REAL( Sin( angle ) ) * eventSize) + event->radarLoc.y );

	angle = -2.0f * PI / 3.0f - addAngle;
	tri[ 2 ].x = REAL_TO_INT( (DOUBLE_TO_REAL( Cos( angle ) ) * eventSize) + event->radarLoc.x );
	tri[ 2 ].y = REAL_TO_INT( (DOUBLE_TO_REAL( Sin( angle ) ) * eventSize) + event->radarLoc.y );

	// translate radar coords to screen coords
	radarToPixel( &tri[ 0 ], &tri[ 0 ], pixelX, pixelY, width, height );
	radarToPixel( &tri[ 1 ], &tri[ 1 ], pixelX, pixelY, width, height );
	radarToPixel( &tri[ 2 ], &tri[ 2 ], pixelX, pixelY, width, height );

	//
	// make the colors we're going to use, when we're at our smallest size we will start to
	// fade the alpha away to transparent so that at our lifetime frame we are completely gone
	//

	// color 1 ------------------
	r = event->color1.red;
	g = event->color1.green;
	b = event->color1.blue;
	a = event->color1.alpha;
	if( currentFrame > event->fadeFrame )
	{

		a = REAL_TO_UNSIGNEDBYTE( (Real)a * (1.0f - (Real)(currentFrame - event->fadeFrame) /
																								(Real)(event->dieFrame - event->fadeFrame) ) );

	}
	startColor = GameMakeColor( r, g, b, a );

	// color 2 ------------------
	r = event->color2.red;
	g = event->color2.green;
	b = event->color2.blue;
	a = event->color2.alpha;
	if( currentFrame > event->fadeFrame )
	{

		a = REAL_TO_UNSIGNEDBYTE( (Real)a * (1.0f - (Real)(currentFrame - event->fadeFrame) /
																								(Real)(event->dieFrame - event->fadeFrame) ) );

	}
	endColor = GameMakeColor( r, g, b, a );

	// draw the lines
	if( ClipLine2D( &tri[ 0 ], &tri[ 1 ], &start, &end, &clipRegion ) )
		TheDisplay->drawLine( start.x, start.y, end.x, end.y, lineWidth, startColor, endColor );
	if( ClipLine2D( &tri[ 1 ], &tri[ 2 ], &start, &end, &clipRegion ) )
		TheDisplay->drawLine( start.x, start.y, end.x, end.y, lineWidth, startColor, endColor );
	if( ClipLine2D( &tri[ 2 ], &tri[ 0 ], &start, &end, &clipRegion ) )
		TheDisplay->drawLine( start.x, start.y, end.x, end.y, lineWidth, startColor, endColor );
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DRadar::drawSingleGenericEvent( Int pixelX, Int pixelY, Int width, Int height, Int index )
{
	RadarEvent *event = &(m_event[index]);
	ICoord2D tri[ 3 ];
	ICoord2D start, end;
	Real angle, addAngle;
	Color startColor, endColor;
	Real lineWidth = 6.0f;
	UnsignedInt currentFrame = TheGameLogic->getFrame();
	UnsignedInt frameDiff;							// frames the event has been alive for
	const Real radarResolutionScale = (Real)RADAR_CELL_WIDTH / (Real)BASE_RADAR_CELL_RESOLUTION;
	Real maxEventSize = (width / 2.0f) * radarResolutionScale;   // max size of the event marker in radar-space units
	Int minEventSize = REAL_TO_INT( (Real)BASE_RADAR_GENERIC_MIN_EVENT_SIZE * radarResolutionScale + 0.5f );    // min size of the event marker (big enough to spot and distinguish overlapping bot pings)
	Int eventSize;									 // current size of a marker to draw
	const Real TIME_FROM_FULL_SIZE_TO_SMALL_SIZE = LOGICFRAMES_PER_SECOND * 1.5;
	Real totalAnglesToSpin = 6.0f * PI;  ///< spin three full turns while shrinking for a snappier feel
	UnsignedByte r, g, b, a;

	// setup screen clipping region
	IRegion2D clipRegion;
	clipRegion.lo.x = pixelX;
	clipRegion.lo.y = pixelY;
	clipRegion.hi.x = pixelX + width;
	clipRegion.hi.y = pixelY + height;

	// get the difference in frame from the current frame to the frame we were created on
	frameDiff = currentFrame - event->createFrame;

	// compute the size of the event marker, it is largest when it starts and smallest at the end
	eventSize = REAL_TO_INT( maxEventSize * ( 1.0f - frameDiff / TIME_FROM_FULL_SIZE_TO_SMALL_SIZE) );

	// we never let the event size get too small
	if( eventSize < minEventSize )
		eventSize = minEventSize;

	// compute how much "angle" we will add to each point to make it rotate as it's getting small
	addAngle = totalAnglesToSpin * (frameDiff / TIME_FROM_FULL_SIZE_TO_SMALL_SIZE);

	// create a triangle around the event
	angle = 0.0f - addAngle;
	tri[ 0 ].x = REAL_TO_INT( (DOUBLE_TO_REAL( Cos( angle ) ) * eventSize) + event->radarLoc.x );
	tri[ 0 ].y = REAL_TO_INT( (DOUBLE_TO_REAL( Sin( angle ) ) * eventSize) + event->radarLoc.y );

	angle = 2.0f * PI / 3.0f - addAngle;
	tri[ 1 ].x = REAL_TO_INT( (DOUBLE_TO_REAL( Cos( angle ) ) * eventSize) + event->radarLoc.x );
	tri[ 1 ].y = REAL_TO_INT( (DOUBLE_TO_REAL( Sin( angle ) ) * eventSize) + event->radarLoc.y );

	angle = -2.0f * PI / 3.0f - addAngle;
	tri[ 2 ].x = REAL_TO_INT( (DOUBLE_TO_REAL( Cos( angle ) ) * eventSize) + event->radarLoc.x );
	tri[ 2 ].y = REAL_TO_INT( (DOUBLE_TO_REAL( Sin( angle ) ) * eventSize) + event->radarLoc.y );

	// translate radar coords to screen coords
	radarToPixel( &tri[ 0 ], &tri[ 0 ], pixelX, pixelY, width, height );
	radarToPixel( &tri[ 1 ], &tri[ 1 ], pixelX, pixelY, width, height );
	radarToPixel( &tri[ 2 ], &tri[ 2 ], pixelX, pixelY, width, height );

	//
	// make the colors we're going to use, when we're at our smallest size we will start to
	// fade the alpha away to transparent so that at our lifetime frame we are completely gone
	//

	// color 1 ------------------
	r = event->color1.red;
	g = event->color1.green;
	b = event->color1.blue;
	a = event->color1.alpha;
	if( currentFrame > event->fadeFrame )
	{

		a = REAL_TO_UNSIGNEDBYTE( (Real)a * (1.0f - (Real)(currentFrame - event->fadeFrame) /
																								(Real)(event->dieFrame - event->fadeFrame) ) );

	}
	startColor = GameMakeColor( r, g, b, a );

	// color 2 ------------------
	r = event->color2.red;
	g = event->color2.green;
	b = event->color2.blue;
	a = event->color2.alpha;
	if( currentFrame > event->fadeFrame )
	{

		a = REAL_TO_UNSIGNEDBYTE( (Real)a * (1.0f - (Real)(currentFrame - event->fadeFrame) /
																								(Real)(event->dieFrame - event->fadeFrame) ) );

	}
	endColor = GameMakeColor( r, g, b, a );

	// draw the lines
	if( ClipLine2D( &tri[ 0 ], &tri[ 1 ], &start, &end, &clipRegion ) )
		TheDisplay->drawLine( start.x, start.y, end.x, end.y, lineWidth, startColor, endColor );
	if( ClipLine2D( &tri[ 1 ], &tri[ 2 ], &start, &end, &clipRegion ) )
		TheDisplay->drawLine( start.x, start.y, end.x, end.y, lineWidth, startColor, endColor );
	if( ClipLine2D( &tri[ 2 ], &tri[ 0 ], &start, &end, &clipRegion ) )
		TheDisplay->drawLine( start.x, start.y, end.x, end.y, lineWidth, startColor, endColor );
}

//-------------------------------------------------------------------------------------------------
/** Draw all the radar events */
//-------------------------------------------------------------------------------------------------
void W3DRadar::drawEvents( Int pixelX, Int pixelY, Int width, Int height )
{
	RADAR_PERF_SCOPE("Cl_RD_Events");

	Int i;
	Int activeEvents = 0;
	Int fakeEvents = 0;

	for( i = 0;  i < MAX_RADAR_EVENTS; i++ )
	{

		// only 'active' events actually have something to draw
		if( m_event[ i ].active == TRUE && m_event[ i ].type != RADAR_EVENT_FAKE )
		{
			++activeEvents;

			// if we haven't played the sound for this event, do it now that we can see it
			if( m_event[ i ].soundPlayed == FALSE && m_event[i].type != RADAR_EVENT_BEACON_PULSE )
			{
				static AudioEventRTS eventSound("RadarEvent");
				TheAudio->addAudioEvent( &eventSound );

			}

			m_event[ i ].soundPlayed = TRUE;

			if ( m_event[ i ].type == RADAR_EVENT_BEACON_PULSE )
				drawSingleBeaconEvent( pixelX, pixelY, width, height, i );
			else
				drawSingleGenericEvent( pixelX, pixelY, width, height, i );

		}
		else if( m_event[ i ].active == TRUE )
		{
			++fakeEvents;
		}

	}

	PerfRecordRenderFeatureStats("RadarEvents", 0, MAX_RADAR_EVENTS, activeEvents, 0, fakeEvents, 0);
}


//-------------------------------------------------------------------------------------------------
/** Draw all the radar icons */
//-------------------------------------------------------------------------------------------------
void W3DRadar::drawIcons( Int pixelX, Int pixelY, Int width, Int height )
{
	RADAR_PERF_SCOPE("Cl_RD_Icons");

	Player *player = rts::getObservedOrLocalPlayer();
	Int heroes = 0;
	Int rendered = 0;
	Int hidden = 0;
	for (RadarObject *heroObj = m_localObjectList; heroObj; heroObj = heroObj->friend_getNext())
	{
		const Object *obj = heroObj->friend_getObject();

		if (!obj->isHero())
			continue;

		++heroes;
		if (!canRenderObject(heroObj, player))
		{
			++hidden;
			continue;
		}

		++rendered;
		drawHeroIcon(pixelX, pixelY, width, height, obj->getPosition());
	}

	PerfRecordRenderFeatureStats("RadarIcons", 0, heroes, rendered, hidden, 0, 0);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DRadar::updateObjectTexture(TextureClass *texture)
{
	RADAR_PERF_SCOPE("Cl_RD_OverlayUpd");

	{
		RADAR_PERF_SCOPE("Cl_RD_OverlayClear");
		// reset the overlay texture
		SurfaceClass *surface = texture->Get_Surface_Level();
		surface->Clear();
		REF_PTR_RELEASE(surface);
	}

	// rebuild the object overlay with units only; structures are drawn directly to screen later
	{
		RADAR_PERF_SCOPE("Cl_RD_ObjTexWorld");
		renderObjectList( m_objectList, texture );
	}
	{
		RADAR_PERF_SCOPE("Cl_RD_ObjTexLocal");
		renderObjectList( m_localObjectList, texture );
	}
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool W3DRadar::canRenderObject( const RadarObject *rObj, const Player *localPlayer )
{
	if (rObj->isTemporarilyHidden())
	{
		return false;
	}

	const Int playerIndex = localPlayer->getPlayerIndex();
	const Object *obj = rObj->friend_getObject();

	//
	// check for shrouded status
	// if object is fogged or shrouded, don't render it
	//
	if (obj->getShroudedStatus(playerIndex) > OBJECTSHROUD_PARTIAL_CLEAR)
	{
		return false;
	}

	//
	// objects with a local only unit priority will only appear on the radar if they
	// are controlled by the local player, or if the local player is an observer (cause
	// they are godlike and can see everything)
	//
	if (obj->getRadarPriority() == RADAR_PRIORITY_LOCAL_UNIT_ONLY &&
		obj->getControllingPlayer() != localPlayer &&
		localPlayer->isPlayerActive() )
	{
		return false;
	}

	//
	// ML-- What the heck is this? local-only and neutral-observer-viewed units are stealthy?? Since when?
	// Now it twinkles for any stealthed object, whether locally controlled or neutral-observer-viewed
	//
	if (TheControlBar->getCurrentlyViewedPlayerRelationship(obj->getTeam()) == ENEMIES &&
		obj->testStatus( OBJECT_STATUS_STEALTHED ) &&
		!obj->testStatus( OBJECT_STATUS_DETECTED ) &&
		!obj->testStatus( OBJECT_STATUS_DISGUISED ) )
	{
		return false;
	}

	return true;
}

//-------------------------------------------------------------------------------------------------
/** Render an object list into the texture passed in */
//-------------------------------------------------------------------------------------------------
void W3DRadar::renderObjectList( const RadarObject *listHead, TextureClass *texture )
{
	RADAR_PERF_SCOPE("Cl_RD_ObjTexList");

	// sanity
	if( listHead == nullptr || texture == nullptr )
		return;

	// get surface for texture to render into
	SurfaceClass *surface = texture->Get_Surface_Level();

	// loop through all objects and draw
	ICoord2D radarPoint;

	Player *player = rts::getObservedOrLocalPlayer();

	SurfaceClass::SurfaceDescription surfaceDesc;
	surface->Get_Description(surfaceDesc);
	int pitch;
	void *pBits = surface->Lock(&pitch);
	const unsigned int bytesPerPixel = Get_Bytes_Per_Pixel(surfaceDesc.Format);
	Int processed = 0;
	Int rendered = 0;
	Int hidden = 0;
	Int structures = 0;

	for( const RadarObject *rObj = listHead; rObj; rObj = rObj->friend_getNext() )
	{
		++processed;
		if (!canRenderObject(rObj, player))
		{
			++hidden;
			continue;
		}

		// get object position
		const Object *obj = rObj->friend_getObject();
		if( obj->isKindOf( KINDOF_STRUCTURE ) )
		{
			++structures;
			continue;
		}

		const Coord3D *pos = obj->getPosition();

		// compute object position as a radar blip
		radarPoint.x = pos->x / (m_mapExtent.width() / RADAR_CELL_WIDTH);
		radarPoint.y = pos->y / (m_mapExtent.height() / RADAR_CELL_HEIGHT);

		// get the color we're going to draw in
		Color argbColor = rObj->getColor();

		// adjust the alpha for stealth units so they "fade/blink" on the radar for the controller
		// if( obj->getRadarPriority() == RADAR_PRIORITY_LOCAL_UNIT_ONLY )
		// ML-- What the heck is this? local-only and neutral-observer-viewed units are stealthy?? Since when?
		// Now it twinkles for any stealthed object, whether locally controlled or neutral-observer-viewed
		if( obj->testStatus( OBJECT_STATUS_STEALTHED ) )
		{
			UnsignedByte r, g, b, a;
			GameGetColorComponents( argbColor, &r, &g, &b, &a );

			const UnsignedInt framesForTransition = LOGICFRAMES_PER_SECOND;
			const UnsignedByte minAlpha = 32;

			Real alphaScale = INT_TO_REAL(TheGameLogic->getFrame() % framesForTransition) / (framesForTransition / 2.0f);
			if( alphaScale > 0.0f )
				a = REAL_TO_UNSIGNEDBYTE( ((alphaScale - 1.0f) * (255.0f - minAlpha)) + minAlpha );
			else
				a = REAL_TO_UNSIGNEDBYTE( (alphaScale * (255.0f - minAlpha)) + minAlpha );
			argbColor = GameMakeColor( r, g, b, a );

		}

		const unsigned int pixelColor = ARGB_Color_To_WW3D_Color(surfaceDesc.Format, argbColor);

		// draw the unit blip into the high-resolution overlay texture
		{
			const int blipSize = RADAR_UNIT_BLIP_SIZE;
			int startX = radarPoint.x - blipSize / 2;
			int startY = radarPoint.y - blipSize / 2;
			for (int by = 0; by < blipSize; by++)
			{
				for (int bx = 0; bx < blipSize; bx++)
				{
					if (legalRadarPoint(startX + bx, startY + by))
						surface->Draw_Pixel( startX + bx,
							startY + by,
							pixelColor,
							bytesPerPixel,
							pBits,
							pitch );
				}
			}
		}
		++rendered;

	}

	surface->Unlock();
	REF_PTR_RELEASE(surface);
	PerfRecordRenderFeatureStats("RadarObjTex", 0, processed, rendered, hidden, structures, 0);

}

//-------------------------------------------------------------------------------------------------
/** Draw structure markers directly in screen space so outlines survive texture scaling */
//-------------------------------------------------------------------------------------------------
void W3DRadar::drawStructureList( const RadarObject *listHead, Int pixelX, Int pixelY, Int width, Int height,
	Bool drawResourceStructures )
{
	RADAR_PERF_SCOPE(drawResourceStructures ? "Cl_RD_StructResList" : "Cl_RD_StructList");

	if( listHead == nullptr )
		return;

	Player *player = rts::getObservedOrLocalPlayer();
	Int processed = 0;
	Int rendered = 0;
	Int hidden = 0;
	Int skipped = 0;
	Int markerWidth = (( RADAR_STRUCTURE_BLIP_SIZE * width ) + RADAR_CELL_WIDTH - 1) / RADAR_CELL_WIDTH;
	Int markerHeight = (( RADAR_STRUCTURE_BLIP_SIZE * height ) + RADAR_CELL_HEIGHT - 1) / RADAR_CELL_HEIGHT;
	Int outlineWidth = (( RADAR_STRUCTURE_OUTLINE_THICKNESS * width ) + RADAR_CELL_WIDTH - 1) / RADAR_CELL_WIDTH;
	Int outlineHeight = (( RADAR_STRUCTURE_OUTLINE_THICKNESS * height ) + RADAR_CELL_HEIGHT - 1) / RADAR_CELL_HEIGHT;
	if( markerWidth < 4 )
		markerWidth = 4;
	if( markerHeight < 4 )
		markerHeight = 4;
	if( outlineWidth < 1 )
		outlineWidth = 1;
	if( outlineHeight < 1 )
		outlineHeight = 1;

	for( const RadarObject *rObj = listHead; rObj; rObj = rObj->friend_getNext() )
	{
		++processed;
		if( !canRenderObject( rObj, player ) )
		{
			++hidden;
			continue;
		}

		const Object *obj = rObj->friend_getObject();
		if( !obj->isKindOf( KINDOF_STRUCTURE ) )
		{
			++skipped;
			continue;
		}
		const Bool isResourceStructure = isRadarResourceStructure( obj );
		if( isResourceStructure != drawResourceStructures )
		{
			++skipped;
			continue;
		}

		const Coord3D *pos = obj->getPosition();
		ICoord2D radarPoint;
		ICoord2D screenPoint;
		radarPoint.x = pos->x / (m_mapExtent.width() / RADAR_CELL_WIDTH);
		radarPoint.y = pos->y / (m_mapExtent.height() / RADAR_CELL_HEIGHT);
		radarToPixel( &radarPoint, &screenPoint, pixelX, pixelY, width, height );

		Color fillColor = rObj->getColor();
		if( obj->testStatus( OBJECT_STATUS_STEALTHED ) )
		{
			UnsignedByte r, g, b, a;
			GameGetColorComponents( fillColor, &r, &g, &b, &a );
			const UnsignedInt framesForTransition = LOGICFRAMES_PER_SECOND;
			const UnsignedByte minAlpha = 32;
			Real alphaScale = INT_TO_REAL(TheGameLogic->getFrame() % framesForTransition) / (framesForTransition / 2.0f);
			if( alphaScale > 0.0f )
				a = REAL_TO_UNSIGNEDBYTE( ((alphaScale - 1.0f) * (255.0f - minAlpha)) + minAlpha );
			else
				a = REAL_TO_UNSIGNEDBYTE( (alphaScale * (255.0f - minAlpha)) + minAlpha );
			fillColor = GameMakeColor( r, g, b, a );
		}

		Color markerFillColor = fillColor;

		Int drawMarkerWidth = markerWidth;
		Int drawMarkerHeight = markerHeight;
		Int drawOutlineWidth = outlineWidth;
		Int drawOutlineHeight = outlineHeight;
		if( isResourceStructure )
		{
			if( drawMarkerWidth < 7 )
				drawMarkerWidth = 7;
			if( drawMarkerHeight < 7 )
				drawMarkerHeight = 7;
		}

		const Int outerLeft = screenPoint.x - (drawMarkerWidth / 2);
		const Int outerTop = screenPoint.y - (drawMarkerHeight / 2);
		if( isResourceStructure )
		{
			UnsignedByte r, g, b, a;
			GameGetColorComponents( fillColor, &r, &g, &b, &a );
			TheDisplay->drawFillRect( outerLeft, outerTop, drawMarkerWidth, drawMarkerHeight, GameMakeColor( 0, 0, 0, a ) );
			++rendered;
			continue;
		}

		TheDisplay->drawFillRect( outerLeft, outerTop, drawMarkerWidth, drawMarkerHeight, GameMakeColor( 0, 0, 0, 255 ) );

		Int innerWidth = drawMarkerWidth - drawOutlineWidth * 2;
		Int innerHeight = drawMarkerHeight - drawOutlineHeight * 2;
		if( innerWidth < 1 )
			innerWidth = 1;
		if( innerHeight < 1 )
			innerHeight = 1;

		const Int innerLeft = screenPoint.x - (innerWidth / 2);
		const Int innerTop = screenPoint.y - (innerHeight / 2);
		TheDisplay->drawFillRect( innerLeft, innerTop, innerWidth, innerHeight, markerFillColor );
		++rendered;
	}

	PerfRecordRenderFeatureStats(drawResourceStructures ? "RadarStructR" : "RadarStruct", 0,
		processed, rendered, hidden, skipped, 0);
}

//-------------------------------------------------------------------------------------------------
/** Draw all structures on the minimap in screen space */
//-------------------------------------------------------------------------------------------------
void W3DRadar::drawStructureMarkers( Int pixelX, Int pixelY, Int width, Int height, Bool drawResourceStructures )
{
	RADAR_PERF_SCOPE(drawResourceStructures ? "Cl_RD_StructRes" : "Cl_RD_Struct");
	{
		RADAR_PERF_SCOPE(drawResourceStructures ? "Cl_RD_StructResWorld" : "Cl_RD_StructWorld");
		drawStructureList( m_objectList, pixelX, pixelY, width, height, drawResourceStructures );
	}
	{
		RADAR_PERF_SCOPE(drawResourceStructures ? "Cl_RD_StructResLocal" : "Cl_RD_StructLocal");
		drawStructureList( m_localObjectList, pixelX, pixelY, width, height, drawResourceStructures );
	}
}


//-------------------------------------------------------------------------------------------------
/** Create or recreate the minimap shroud texture using logical shroud cell resolution */
//-------------------------------------------------------------------------------------------------
void W3DRadar::initializeShroudTexture( Int dataWidth, Int dataHeight )
{
	if( dataWidth < 1 )
		dataWidth = 1;
	if( dataHeight < 1 )
		dataHeight = 1;

	if( m_shroudTexture != nullptr )
		m_shroudTexture->Release_Ref();
	m_shroudTexture = nullptr;
	if( m_shroudImage != nullptr )
		deleteInstance( m_shroudImage );
	m_shroudImage = nullptr;
	if( m_shroudCellAlphaBuffer != nullptr )
		delete[] m_shroudCellAlphaBuffer;
	m_shroudCellAlphaBuffer = nullptr;

	m_shroudDataWidth = dataWidth;
	m_shroudDataHeight = dataHeight;
	m_shroudCellAlphaBuffer = new UnsignedByte[ m_shroudDataWidth * m_shroudDataHeight ];
	memset( m_shroudCellAlphaBuffer, 0, m_shroudDataWidth * m_shroudDataHeight );
	m_shroudTextureDirty = FALSE;

	unsigned int textureWidth = (unsigned int)m_textureWidth;
	unsigned int textureHeight = (unsigned int)m_textureHeight;
	unsigned int depth = 1;
	TextureLoader::Validate_Texture_Size( textureWidth, textureHeight, depth );

	m_shroudTexture = MSGNEW("TextureClass") TextureClass( textureWidth, textureHeight,
		m_shroudTextureFormat, MIP_LEVELS_1 );
	DEBUG_ASSERTCRASH( m_shroudTexture, ("W3DRadar: Unable to allocate shroud texture") );
	m_shroudTexture->Get_Filter().Set_Min_Filter( TextureFilterClass::FILTER_TYPE_DEFAULT );
	m_shroudTexture->Get_Filter().Set_Mag_Filter( TextureFilterClass::FILTER_TYPE_DEFAULT );
	m_shroudTexture->Get_Filter().Set_U_Addr_Mode( TextureFilterClass::TEXTURE_ADDRESS_CLAMP );
	m_shroudTexture->Get_Filter().Set_V_Addr_Mode( TextureFilterClass::TEXTURE_ADDRESS_CLAMP );

	ICoord2D size;
	Region2D uv;
	m_shroudImage = newInstance(Image);
	uv.lo.x = 0.0f;
	uv.lo.y = 1.0f;
	uv.hi.x = 1.0f;
	uv.hi.y = 0.0f;
	m_shroudImage->setStatus( IMAGE_STATUS_RAW_TEXTURE );
	m_shroudImage->setRawTextureData( m_shroudTexture );
	m_shroudImage->setUV( &uv );
	m_shroudImage->setTextureWidth( textureWidth );
	m_shroudImage->setTextureHeight( textureHeight );
	size.x = textureWidth;
	size.y = textureHeight;
	m_shroudImage->setImageSize( &size );
}

//-------------------------------------------------------------------------------------------------
/** Rebuild the minimap shroud texture from logical shroud cells using smooth interpolation */
//-------------------------------------------------------------------------------------------------
void W3DRadar::rebuildShroudTexture()
{
	RADAR_PERF_SCOPE("Cl_RD_ShroudRebuild");

	if( m_shroudTexture == nullptr || m_shroudCellAlphaBuffer == nullptr )
		return;

	SurfaceClass *surface = m_shroudTexture->Get_Surface_Level();
	if( surface == nullptr )
		return;

	SurfaceClass::SurfaceDescription surfaceDesc;
	surface->Get_Description( surfaceDesc );
	int pitch = 0;
	void *surfaceBits = surface->Lock( &pitch );
	const UnsignedInt bytesPerPixel = Get_Bytes_Per_Pixel( surfaceDesc.Format );
	const Int surfaceWidth = surfaceDesc.Width;
	const Int surfaceHeight = surfaceDesc.Height;
	const Int surfacePixelCount = surfaceWidth * surfaceHeight;
	UnsignedByte *interpolatedAlpha = new UnsignedByte[ surfacePixelCount ];
	UnsignedByte *blurredAlpha = new UnsignedByte[ surfacePixelCount ];
	unsigned int alphaToColor[ 256 ];
	for( Int alpha = 0; alpha < 256; ++alpha )
		alphaToColor[ alpha ] = ARGB_Color_To_WW3D_Color( surfaceDesc.Format, GameMakeColor( 0, 0, 0, alpha ) );
	static const Int blurWeights[ 5 ] = { 1, 4, 6, 4, 1 };
	static const Int blurWeightTotal = 16;

	const Real maxCellX = m_shroudDataWidth > 1 ? (Real)(m_shroudDataWidth - 1) : 0.0f;
	const Real maxCellY = m_shroudDataHeight > 1 ? (Real)(m_shroudDataHeight - 1) : 0.0f;
	const Real invWidth = surfaceWidth > 1 ? 1.0f / (Real)(surfaceWidth - 1) : 0.0f;
	const Real invHeight = surfaceHeight > 1 ? 1.0f / (Real)(surfaceHeight - 1) : 0.0f;
	Int *x0Table = new Int[ surfaceWidth ];
	Int *x1Table = new Int[ surfaceWidth ];
	Real *txTable = new Real[ surfaceWidth ];
	for( Int x = 0; x < surfaceWidth; ++x )
	{
		Real fx = maxCellX * (x * invWidth);
		Int x0 = REAL_TO_INT( fx );
		if( x0 < 0 )
			x0 = 0;
		if( x0 >= m_shroudDataWidth )
			x0 = m_shroudDataWidth - 1;
		x0Table[ x ] = x0;
		x1Table[ x ] = x0 < m_shroudDataWidth - 1 ? x0 + 1 : x0;
		Real tx = fx - x0;
		txTable[ x ] = tx * tx * (3.0f - 2.0f * tx);
	}

	for( Int y = 0; y < surfaceHeight; ++y )
	{
		Real fy = maxCellY * (y * invHeight);
		Int y0 = REAL_TO_INT( fy );
		if( y0 < 0 )
			y0 = 0;
		if( y0 >= m_shroudDataHeight )
			y0 = m_shroudDataHeight - 1;
		Int y1 = y0 < m_shroudDataHeight - 1 ? y0 + 1 : y0;
		Real ty = fy - y0;
		ty = ty * ty * (3.0f - 2.0f * ty);

		for( Int x = 0; x < surfaceWidth; ++x )
		{
			const Int x0 = x0Table[ x ];
			const Int x1 = x1Table[ x ];
			const Real tx = txTable[ x ];

			const Real alpha00 = (Real)m_shroudCellAlphaBuffer[ y0 * m_shroudDataWidth + x0 ];
			const Real alpha10 = (Real)m_shroudCellAlphaBuffer[ y0 * m_shroudDataWidth + x1 ];
			const Real alpha01 = (Real)m_shroudCellAlphaBuffer[ y1 * m_shroudDataWidth + x0 ];
			const Real alpha11 = (Real)m_shroudCellAlphaBuffer[ y1 * m_shroudDataWidth + x1 ];
			const Real alphaTop = alpha00 + ((alpha10 - alpha00) * tx);
			const Real alphaBottom = alpha01 + ((alpha11 - alpha01) * tx);
			Int alpha = REAL_TO_INT( alphaTop + ((alphaBottom - alphaTop) * ty) + 0.5f );
			if( alpha < 0 )
				alpha = 0;
			else if( alpha > 255 )
				alpha = 255;

			interpolatedAlpha[ y * surfaceWidth + x ] = (UnsignedByte)alpha;
		}
	}

	for( Int y = 0; y < surfaceHeight; ++y )
	{
		const Int rowBase = y * surfaceWidth;
		if( surfaceWidth >= 5 )
		{
			const UnsignedByte *src = interpolatedAlpha + rowBase;
			UnsignedByte *dst = blurredAlpha + rowBase;
			dst[ 0 ] = (UnsignedByte)((src[ 0 ] * 11 + src[ 1 ] * 4 + src[ 2 ] + (blurWeightTotal / 2)) / blurWeightTotal);
			dst[ 1 ] = (UnsignedByte)((src[ 0 ] * 5 + src[ 1 ] * 6 + src[ 2 ] * 4 + src[ 3 ] + (blurWeightTotal / 2)) / blurWeightTotal);
			for( Int x = 2; x < surfaceWidth - 2; ++x )
			{
				dst[ x ] = (UnsignedByte)((src[ x - 2 ] + src[ x - 1 ] * 4 + src[ x ] * 6 + src[ x + 1 ] * 4 + src[ x + 2 ] + (blurWeightTotal / 2)) / blurWeightTotal);
			}
			dst[ surfaceWidth - 2 ] = (UnsignedByte)((src[ surfaceWidth - 4 ] + src[ surfaceWidth - 3 ] * 4 + src[ surfaceWidth - 2 ] * 6 + src[ surfaceWidth - 1 ] * 5 + (blurWeightTotal / 2)) / blurWeightTotal);
			dst[ surfaceWidth - 1 ] = (UnsignedByte)((src[ surfaceWidth - 3 ] + src[ surfaceWidth - 2 ] * 4 + src[ surfaceWidth - 1 ] * 11 + (blurWeightTotal / 2)) / blurWeightTotal);
		}
		else
		{
			for( Int x = 0; x < surfaceWidth; ++x )
			{
				Int accum = 0;
				for( Int tap = -2; tap <= 2; ++tap )
				{
					Int sampleX = x + tap;
					if( sampleX < 0 )
						sampleX = 0;
					else if( sampleX >= surfaceWidth )
						sampleX = surfaceWidth - 1;

					accum += interpolatedAlpha[ rowBase + sampleX ] * blurWeights[ tap + 2 ];
				}

				blurredAlpha[ rowBase + x ] = (UnsignedByte)((accum + (blurWeightTotal / 2)) / blurWeightTotal);
			}
		}
	}

	if( surfaceHeight >= 5 )
	{
		for( Int x = 0; x < surfaceWidth; ++x )
		{
			Int alpha = (blurredAlpha[ x ] * 11 + blurredAlpha[ surfaceWidth + x ] * 4 + blurredAlpha[ surfaceWidth * 2 + x ] + (blurWeightTotal / 2)) / blurWeightTotal;
			surface->Draw_Pixel( x, 0, alphaToColor[ alpha ], bytesPerPixel, surfaceBits, pitch );
			alpha = (blurredAlpha[ x ] * 5 + blurredAlpha[ surfaceWidth + x ] * 6 + blurredAlpha[ surfaceWidth * 2 + x ] * 4 + blurredAlpha[ surfaceWidth * 3 + x ] + (blurWeightTotal / 2)) / blurWeightTotal;
			surface->Draw_Pixel( x, 1, alphaToColor[ alpha ], bytesPerPixel, surfaceBits, pitch );
		}
		for( Int y = 2; y < surfaceHeight - 2; ++y )
		{
			const Int rowBase = y * surfaceWidth;
			for( Int x = 0; x < surfaceWidth; ++x )
			{
				const Int alpha = (blurredAlpha[ rowBase - surfaceWidth * 2 + x ] + blurredAlpha[ rowBase - surfaceWidth + x ] * 4 + blurredAlpha[ rowBase + x ] * 6 + blurredAlpha[ rowBase + surfaceWidth + x ] * 4 + blurredAlpha[ rowBase + surfaceWidth * 2 + x ] + (blurWeightTotal / 2)) / blurWeightTotal;
				surface->Draw_Pixel( x, y, alphaToColor[ alpha ], bytesPerPixel, surfaceBits, pitch );
			}
		}
		for( Int x = 0; x < surfaceWidth; ++x )
		{
			Int alpha = (blurredAlpha[ (surfaceHeight - 4) * surfaceWidth + x ] + blurredAlpha[ (surfaceHeight - 3) * surfaceWidth + x ] * 4 + blurredAlpha[ (surfaceHeight - 2) * surfaceWidth + x ] * 6 + blurredAlpha[ (surfaceHeight - 1) * surfaceWidth + x ] * 5 + (blurWeightTotal / 2)) / blurWeightTotal;
			surface->Draw_Pixel( x, surfaceHeight - 2, alphaToColor[ alpha ], bytesPerPixel, surfaceBits, pitch );
			alpha = (blurredAlpha[ (surfaceHeight - 3) * surfaceWidth + x ] + blurredAlpha[ (surfaceHeight - 2) * surfaceWidth + x ] * 4 + blurredAlpha[ (surfaceHeight - 1) * surfaceWidth + x ] * 11 + (blurWeightTotal / 2)) / blurWeightTotal;
			surface->Draw_Pixel( x, surfaceHeight - 1, alphaToColor[ alpha ], bytesPerPixel, surfaceBits, pitch );
		}
	}
	else
	{
		for( Int y = 0; y < surfaceHeight; ++y )
		{
			for( Int x = 0; x < surfaceWidth; ++x )
			{
				Int accum = 0;
				for( Int tap = -2; tap <= 2; ++tap )
				{
					Int sampleY = y + tap;
					if( sampleY < 0 )
						sampleY = 0;
					else if( sampleY >= surfaceHeight )
						sampleY = surfaceHeight - 1;

					accum += blurredAlpha[ sampleY * surfaceWidth + x ] * blurWeights[ tap + 2 ];
				}

				const Int alpha = (accum + (blurWeightTotal / 2)) / blurWeightTotal;
				surface->Draw_Pixel( x, y, alphaToColor[ alpha ], bytesPerPixel, surfaceBits, pitch );
			}
		}
	}

	delete[] txTable;
	delete[] x1Table;
	delete[] x0Table;
	delete[] blurredAlpha;
	delete[] interpolatedAlpha;

	surface->Unlock();
	REF_PTR_RELEASE( surface );
	m_shroudTextureDirty = FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Shade the color passed in using the height parameter to lighten and darken it.  Colors
	* will be interpolated using the value "height" across the range from loZ to hiZ.  The
	* midZ is the "middle" point, height values above it will be lightened, while
	* lower ones are darkened. */
//-------------------------------------------------------------------------------------------------
void W3DRadar::interpolateColorForHeight( RGBColor *color,
																					Real height,
																					Real hiZ,
																					Real midZ,
																					Real loZ )
{
	const Real howBright = 0.95f;  // bigger is brighter (0.0 to 1.0)
	const Real howDark   = 0.60f;  // bigger is darker (0.0 to 1.0)

	// sanity on map height (flat maps bomb)
	if (hiZ == midZ)
		hiZ = midZ+0.1f;
	if (midZ == loZ)
		loZ = midZ-0.1f;
	if (hiZ == loZ)
		hiZ = loZ+0.2f;

	Real t;
	RGBColor colorTarget;

	// if "over" the middle height, interpolate lighter
	if( height >= midZ )
	{

		// how far are we from the middleZ towards the hi Z
		t = (height - midZ) / (hiZ - midZ);

		// compute what our "lightest" color possible we want to use is
		colorTarget.red = color->red + (1.0f - color->red) * howBright;
		colorTarget.green = color->green + (1.0f - color->green) * howBright;
		colorTarget.blue = color->blue + (1.0f - color->blue) * howBright;

	}
	else  // interpolate darker
	{

		// how far are we from the middleZ towards the low Z
		t = (midZ - height) / (midZ - loZ);

		// compute what the "darkest" color possible we want to use is
		colorTarget.red = color->red + (0.0f - color->red) * howDark;
		colorTarget.green = color->green + (0.0f - color->green) * howDark;
		colorTarget.blue = color->blue + (0.0f - color->blue) * howDark;

	}

	// interpolate toward the target color
	color->red = color->red + (colorTarget.red - color->red) * t;
	color->green = color->green + (colorTarget.green - color->green) * t;
	color->blue = color->blue + (colorTarget.blue - color->blue) * t;

	// keep the color real
	if( color->red < 0.0f )
		color->red = 0.0f;
	if( color->red > 1.0f )
		color->red = 1.0f;
	if( color->green < 0.0f )
		color->green = 0.0f;
	if( color->green > 1.0f )
		color->green = 1.0f;
	if( color->blue < 0.0f )
		color->blue = 0.0f;
	if( color->blue > 1.0f )
		color->blue = 1.0f;

}

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC METHODS /////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DRadar::W3DRadar()
{

	m_terrainTextureFormat = WW3D_FORMAT_UNKNOWN;
	m_terrainImage = nullptr;
	m_terrainTexture = nullptr;

	m_overlayTextureFormat = WW3D_FORMAT_UNKNOWN;
	m_overlayImage = nullptr;
	m_overlayTexture = nullptr;

	m_shroudTextureFormat = WW3D_FORMAT_UNKNOWN;
	m_shroudImage = nullptr;
	m_shroudTexture = nullptr;
	m_shroudDataWidth = 1;
	m_shroudDataHeight = 1;
	m_shroudCellAlphaBuffer = nullptr;
	m_shroudTextureDirty = FALSE;
	m_shroudSurface = nullptr;
	m_shroudSurfaceBits = nullptr;
	m_shroudSurfacePitch = 0;
	m_shroudSurfaceFormat = WW3D_FORMAT_UNKNOWN;
	m_shroudSurfacePixelSize = 0;

	m_textureWidth = RADAR_CELL_WIDTH;
	m_textureHeight = RADAR_CELL_HEIGHT;

	m_reconstructViewBox = TRUE;

	for( Int i = 0; i < 4; i++ )
	{

		m_viewBox[ i ].x = 0;
		m_viewBox[ i ].y = 0;

	}

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DRadar::~W3DRadar()
{

	// delete resources used for the W3D radar
	deleteResources();

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DRadar::xfer( Xfer *xfer )
{
	Radar::xfer(xfer);
}

//-------------------------------------------------------------------------------------------------
/** Radar initialization */
//-------------------------------------------------------------------------------------------------
void W3DRadar::init()
{
	ICoord2D size;
	Region2D uv;

	// extending functionality
	Radar::init();

	// gather specific texture format information
	initializeTextureFormats();

	// allocate our terrain texture
	// poolify
	m_terrainTexture = MSGNEW("TextureClass") TextureClass( m_textureWidth, m_textureHeight,
																			 m_terrainTextureFormat, MIP_LEVELS_1 );
	DEBUG_ASSERTCRASH( m_terrainTexture, ("W3DRadar: Unable to allocate terrain texture") );

	// allocate our overlay texture
	m_overlayTexture = MSGNEW("TextureClass") TextureClass( m_textureWidth, m_textureHeight,
																			 m_overlayTextureFormat, MIP_LEVELS_1 );
	DEBUG_ASSERTCRASH( m_overlayTexture, ("W3DRadar: Unable to allocate overlay texture") );
	m_overlayTexture->Get_Filter().Set_Min_Filter( TextureFilterClass::FILTER_TYPE_NONE );
	m_overlayTexture->Get_Filter().Set_Mag_Filter( TextureFilterClass::FILTER_TYPE_NONE );

	// allocate a placeholder shroud texture until the map tells us how many shroud cells exist
	initializeShroudTexture( 1, 1 );

	//
	// create images used for rendering and set them up with the textures
	//

	//
	// the terrain image, note the UV coords change it from (0,0) in the upper left
	// to (0,0) in the lower left cause that's how we are initially oriented in the
	// world (positive X to the right and positive Y up)
	//
	m_terrainImage = newInstance(Image);
	uv.lo.x = 0.0f;
	uv.lo.y = 1.0f;
	uv.hi.x = 1.0f;
	uv.hi.y = 0.0f;
	m_terrainImage->setStatus( IMAGE_STATUS_RAW_TEXTURE );
	m_terrainImage->setRawTextureData( m_terrainTexture );
	m_terrainImage->setUV( &uv );
	m_terrainImage->setTextureWidth( m_textureWidth );
	m_terrainImage->setTextureHeight( m_textureHeight );
	size.x = m_textureWidth;
	size.y = m_textureHeight;
	m_terrainImage->setImageSize( &size );

	// the overlay image
	m_overlayImage = newInstance(Image);
	uv.lo.x = 0.0f;
	uv.lo.y = 1.0f;
	uv.hi.x = 1.0f;
	uv.hi.y = 0.0f;
	m_overlayImage->setStatus( IMAGE_STATUS_RAW_TEXTURE );
	m_overlayImage->setRawTextureData( m_overlayTexture );
	m_overlayImage->setUV( &uv );
	m_overlayImage->setTextureWidth( m_textureWidth );
	m_overlayImage->setTextureHeight( m_textureHeight );
	size.x = m_textureWidth;
	size.y = m_textureHeight;
	m_overlayImage->setImageSize( &size );

}

//-------------------------------------------------------------------------------------------------
/** Reset the radar to the initial empty state ready for new data */
//-------------------------------------------------------------------------------------------------
void W3DRadar::reset()
{

	// extending functionality, call base class
	Radar::reset();

	// clear our texture data, but do not delete the resources
	SurfaceClass *surface;

	surface = m_terrainTexture->Get_Surface_Level();
	if( surface )
	{
		surface->Clear();
		REF_PTR_RELEASE(surface);
	}

	surface = m_overlayTexture->Get_Surface_Level();
	if( surface )
	{
		surface->Clear();
		REF_PTR_RELEASE(surface);
	}

	// don't call Clear(); that wips to transparent. do this instead.
	//gs Dude, it's called CLEARshroud.  It needs to clear the shroud.
	clearShroud();

}

//-------------------------------------------------------------------------------------------------
/** Update */
//-------------------------------------------------------------------------------------------------
void W3DRadar::update()
{

	// extend base class
	Radar::update();

}

//-------------------------------------------------------------------------------------------------
/** Reset the radar for the new map data being given to it */
//-------------------------------------------------------------------------------------------------
void W3DRadar::newMap( TerrainLogic *terrain )
{

	//
	// extending functionality, call the base class ... this will cause a reset of the
	// system which will clear out our textures but not free them
	//
	Radar::newMap( terrain );

	// sanity
	if( terrain == nullptr )
		return;

	W3DShroud *shroud = TheTerrainRenderObject ? TheTerrainRenderObject->getShroud() : nullptr;
	if( shroud != nullptr )
	{
		initializeShroudTexture( shroud->getNumShroudCellsX(), shroud->getNumShroudCellsY() );
		clearShroud();
	}

	// build terrain texture
	buildTerrainTexture( terrain );

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DRadar::buildTerrainTexture( TerrainLogic *terrain )
{
	SurfaceClass *surface;
	RGBColor waterColor;

	// we will want to reconstruct our new view box now
	m_reconstructViewBox = TRUE;

	// setup our water color
	waterColor.red = TheWaterTransparency->m_radarColor.red;
	waterColor.green = TheWaterTransparency->m_radarColor.green;
	waterColor.blue = TheWaterTransparency->m_radarColor.blue;

	// get the terrain surface to draw in
	surface = m_terrainTexture->Get_Surface_Level();
	DEBUG_ASSERTCRASH( surface, ("W3DRadar: Can't get surface for terrain texture") );

	// build the terrain
	RGBColor sampleColor;
	RGBColor color;
	Int i, j, samples;
	Int x, y;
	ICoord2D radarPoint;
	Coord3D worldPoint;
	Bridge *bridge;

	SurfaceClass::SurfaceDescription surfaceDesc;
	surface->Get_Description(surfaceDesc);
	int pitch;
	void *pBits = surface->Lock(&pitch);
	const unsigned int bytesPerPixel = Get_Bytes_Per_Pixel(surfaceDesc.Format);

	for( y = 0; y < m_textureHeight; y++ )
	{

		for( x = 0; x < m_textureWidth; x++ )
		{

			// what point are we inspecting
			radarPoint.x = x;
			radarPoint.y = y;
			radarToWorld2D( &radarPoint, &worldPoint );

			// check to see if this point is part of a working bridge
			Bool workingBridge = FALSE;
			bridge = TheTerrainLogic->findBridgeAt( &worldPoint );
			if( bridge != nullptr )
			{
				Object *obj = TheGameLogic->findObjectByID( bridge->peekBridgeInfo()->bridgeObjectID );

				if( obj )
				{
					BodyModuleInterface *body = obj->getBodyModule();

					if( body->getDamageState() != BODY_RUBBLE )
						workingBridge = TRUE;

				}

			}

			// create a color based on the Z height of the map
			Real waterZ;
			if( workingBridge == FALSE && terrain->isUnderwater( worldPoint.x, worldPoint.y, &waterZ ) )
			{
				const Int waterSamplesAway = 1;		// how many "tiles" from the center tile we will sample away
																					// to average a color for the tile color

				sampleColor.red = sampleColor.green = sampleColor.blue = 0.0f;
				samples = 0;

				for( j = y - waterSamplesAway; j <= y + waterSamplesAway; j++ )
				{

					if( j >= 0 && j < m_textureHeight )
					{

						for( i = x - waterSamplesAway; i <= x + waterSamplesAway; i++ )
						{

							if( i >= 0 && i < m_textureWidth )
							{

								// the the world point we are concerned with
								radarPoint.x = i;
								radarPoint.y = j;
								radarToWorld2D( &radarPoint, &worldPoint );

								// get color for this Z and add to our sample color
								Real underwaterZ;
								if( terrain->isUnderwater( worldPoint.x, worldPoint.y, nullptr, &underwaterZ ) )
								{
									// this is our "color" for water
									color = waterColor;

									// interpolate the water color for height in the water table
									interpolateColorForHeight( &color, underwaterZ, waterZ,
																						 waterZ,
																						 m_mapExtent.lo.z );

									// add color to our samples
									sampleColor.red += color.red;
									sampleColor.green += color.green;
									sampleColor.blue += color.blue;
									samples++;

								}

							}

						}

					}

				}

				// prevent divide by zeros
				if( samples == 0 )
					samples = 1;

				// set the color to an average of the colors read
				color.red = sampleColor.red / (Real)samples;
				color.green = sampleColor.green / (Real)samples;
				color.blue = sampleColor.blue / (Real)samples;

			}
			else  // regular terrain ...
			{
				const Int samplesAway = 1;  // how many "tiles" from the center tile we will sample away
																		// to average a color for the tile color

				sampleColor.red = sampleColor.green = sampleColor.blue = 0.0f;
				samples = 0;

				for( j = y - samplesAway; j <= y + samplesAway; j++ )
				{

					if( j >= 0 && j < m_textureHeight )
					{

						for( i = x - samplesAway; i <= x + samplesAway; i++ )
						{

							if( i >= 0 && i < m_textureWidth )
							{

								// the the world point we are concerned with
								radarPoint.x = i;
								radarPoint.y = j;
								radarToWorld( &radarPoint, &worldPoint );

								// get the color we're going to use here
								if( workingBridge )
								{
									AsciiString bridgeTName = bridge->getBridgeTemplateName();
									TerrainRoadType *bridgeTemplate = TheTerrainRoads->findBridge( bridgeTName );

									// sanity
									DEBUG_ASSERTCRASH( bridgeTemplate, ("W3DRadar::buildTerrainTexture - Can't find bridge template for '%s'", bridgeTName.str()) );

									// use bridge color
									if ( bridgeTemplate )
										color = bridgeTemplate->getRadarColor();
									else
										color.setFromInt(0xffffffff);
									//
									// we won't use the height of the terrain at this sample point, we will
									// instead use the height for the entire bridge
									//
									Real bridgeHeight = (bridge->peekBridgeInfo()->fromLeft.z +
																			 bridge->peekBridgeInfo()->fromRight.z +
																			 bridge->peekBridgeInfo()->toLeft.z +
																			 bridge->peekBridgeInfo()->toRight.z) / 4.0f;

									// interpolate the color, but use the bridge height, not the terrain height
									interpolateColorForHeight( &color, bridgeHeight,
																						 getTerrainAverageZ(),
																						 m_mapExtent.hi.z, m_mapExtent.lo.z );

								}
								else
								{

									// get the color at this point
									TheTerrainVisual->getTerrainColorAt( worldPoint.x, worldPoint.y, &color );

									// interpolate the color for height
									interpolateColorForHeight( &color, worldPoint.z, getTerrainAverageZ(),
																						 m_mapExtent.hi.z, m_mapExtent.lo.z );

								}

								// add color to our samples
								sampleColor.red += color.red;
								sampleColor.green += color.green;
								sampleColor.blue += color.blue;
								samples++;

							}

						}

					}

				}

				// prevent divide by zeros
				if( samples == 0 )
					samples = 1;

				// set the color to an average of the colors read
				color.red = sampleColor.red / (Real)samples;
				color.green = sampleColor.green / (Real)samples;
				color.blue = sampleColor.blue / (Real)samples;

			}

			// draw the pixel for the terrain at this point, note that because of the orientation
			// of our world we draw it with positive y in the "up" direction
			const Color argbColor = GameMakeColor( color.red * 255, color.green * 255, color.blue * 255, 255 );
			const unsigned int pixelColor = ARGB_Color_To_WW3D_Color(surfaceDesc.Format, argbColor);
			surface->Draw_Pixel( x, y, pixelColor, bytesPerPixel, pBits, pitch );

		}

	}

	// all done with the surface
	surface->Unlock();
	REF_PTR_RELEASE(surface);

}

// ------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DRadar::clearShroud()
{
#if ENABLE_CONFIGURABLE_SHROUD
	if (!TheGlobalData->m_shroudOn)
		return;
#endif

	if( m_shroudCellAlphaBuffer != nullptr )
		memset( m_shroudCellAlphaBuffer, 0, m_shroudDataWidth * m_shroudDataHeight );

	SurfaceClass *surface = m_shroudTexture->Get_Surface_Level();
	if( surface == nullptr )
		return;

	// fill to clear, shroud will make black.  Don't want to make something black that logic can't clear

	int pitch;
	void *pBits = surface->Lock(&pitch);
	SurfaceClass::SurfaceDescription surfaceDesc;
	surface->Get_Description(surfaceDesc);
	const Int surfaceWidth = surfaceDesc.Width;
	const Int surfaceHeight = surfaceDesc.Height;
	const unsigned int bytesPerPixel = surface->Get_Bytes_Per_Pixel();
	const Color color = GameMakeColor( 0, 0, 0, 0 );

	for( Int y = 0; y < surfaceHeight; y++ )
	{
		surface->Draw_H_Line(y, 0, surfaceWidth - 1, color, bytesPerPixel, pBits, pitch);
	}

	surface->Unlock();
	REF_PTR_RELEASE(surface);
	m_shroudTextureDirty = FALSE;
}

// ------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DRadar::setShroudLevel(Int shroudX, Int shroudY, CellShroudStatus setting)
{
#if ENABLE_CONFIGURABLE_SHROUD
	if (!TheGlobalData->m_shroudOn)
		return;
#endif

	W3DShroud *shroud = TheTerrainRenderObject ? TheTerrainRenderObject->getShroud() : nullptr;
	if( shroud != nullptr &&
		( m_shroudDataWidth != shroud->getNumShroudCellsX() || m_shroudDataHeight != shroud->getNumShroudCellsY() ) )
	{
		initializeShroudTexture( shroud->getNumShroudCellsX(), shroud->getNumShroudCellsY() );
		clearShroud();
	}

	if( shroudX < 0 || shroudY < 0 || shroudX >= m_shroudDataWidth || shroudY >= m_shroudDataHeight )
		return;

	//Logic is saying shroud.  We can add alpha levels here in client if needed.
	// W3DShroud is a 0-255 alpha byte.  Logic shroud is a double reference count.
	Int alpha;
	if( setting == CELLSHROUD_SHROUDED )
		alpha = 255;
	else if( setting == CELLSHROUD_FOGGED )
		alpha = 80;///< lighter minimap fog plus filtered scaling reads more like soft fog
	else
		alpha = 0;

	m_shroudCellAlphaBuffer[ shroudY * m_shroudDataWidth + shroudX ] = (UnsignedByte)alpha;
	m_shroudTextureDirty = TRUE;
}

void W3DRadar::beginSetShroudLevel()
{
	DEBUG_ASSERTCRASH( m_shroudSurface == nullptr, ("W3DRadar::beginSetShroudLevel: m_shroudSurface is expected null") );
	m_shroudSurface = m_shroudTexture->Get_Surface_Level();
	DEBUG_ASSERTCRASH( m_shroudSurface != nullptr, ("W3DRadar::beginSetShroudLevel: Can't get surface for Shroud texture") );

	SurfaceClass::SurfaceDescription surfaceDesc;
	m_shroudSurface->Get_Description(surfaceDesc);
	m_shroudSurfaceBits = m_shroudSurface->Lock(&m_shroudSurfacePitch);
	m_shroudSurfaceFormat = surfaceDesc.Format;
	m_shroudSurfacePixelSize = Get_Bytes_Per_Pixel(surfaceDesc.Format);
}

void W3DRadar::endSetShroudLevel()
{
	DEBUG_ASSERTCRASH( m_shroudSurface != nullptr, ("W3DRadar::endSetShroudLevel: m_shroudSurface is not expected null") );
	if (m_shroudSurfaceBits != nullptr)
	{
		m_shroudSurface->Unlock();
		m_shroudSurfaceBits = nullptr;
		m_shroudSurfacePitch = 0;
		m_shroudSurfaceFormat = WW3D_FORMAT_UNKNOWN;
		m_shroudSurfacePixelSize = 0;
	}
	REF_PTR_RELEASE(m_shroudSurface);
}

//-------------------------------------------------------------------------------------------------
/** Actually draw the radar at the screen coordinates provided
	* NOTE about how drawing works: The radar images are computed at samples across the
	* map and are built into a "square" texture area.  At the time of drawing and computing
	* radar<->world coords we consider the "ratio" of width to height of the map dimensions
	* so that when we draw we preserve the aspect ratio of the map and don't squish it in
	* any direction that would cause the map to be distorted.  Extra blank space is drawn
	* around the radar images to keep the whole radar area covered when the map displayed
	* is "long" or "tall" */
//-------------------------------------------------------------------------------------------------
void W3DRadar::draw( Int pixelX, Int pixelY, Int width, Int height )
{
	RADAR_PERF_SCOPE("Cl_RD_Draw");

	// if the local player does not have a radar then we can't draw anything
	if( !rts::localPlayerHasRadar() )
		return;

	//
	// given a upper left corner at pixelX|Y and a width and height to draw into, figure out
	// where we should start and end the image so that the final drawn image has the
	// same ratio as the map and isn't stretched or distorted
	//
	ICoord2D ul, lr;
	findDrawPositions( pixelX, pixelY, width, height, &ul, &lr );

	Int scaledWidth = lr.x - ul.x;
	Int scaledHeight = lr.y - ul.y;

	// draw black border areas where we need map
	Color fillColor = GameMakeColor( 0, 0, 0, 255 );
	Color lineColor = GameMakeColor( 50, 50, 50, 255 );
	{
		RADAR_PERF_SCOPE("Cl_RD_Borders");
		if( m_mapExtent.width()/width >= m_mapExtent.height()/height )
		{

			// draw horizontal bars at top and bottom
			TheDisplay->drawFillRect( pixelX, pixelY, width, ul.y - pixelY - 1, fillColor );
			TheDisplay->drawFillRect( pixelX, lr.y + 1, width, pixelY + height - lr.y - 1, fillColor);
			TheDisplay->drawLine(pixelX, ul.y, pixelX + width, ul.y, 1, lineColor);
			TheDisplay->drawLine(pixelX, lr.y + 1, pixelX + width, lr.y + 1, 1, lineColor);

		}
		else
		{

			// draw vertical bars to the left and right
			TheDisplay->drawFillRect( pixelX, pixelY, ul.x - pixelX - 1, height, fillColor );
			TheDisplay->drawFillRect( lr.x + 1, pixelY, width - (lr.x - pixelX) - 1, height, fillColor );
			TheDisplay->drawLine(ul.x, pixelY, ul.x, pixelY + height, 1, lineColor);
			TheDisplay->drawLine(lr.x + 1, pixelY, lr.x + 1, pixelY + height, 1, lineColor);

		}

	}

	// draw the terrain texture
	{
		RADAR_PERF_SCOPE("Cl_RD_TerrainImg");
		TheDisplay->drawImage( m_terrainImage, ul.x, ul.y, lr.x, lr.y );
	}

	// draw non-resource structures first so units and resources can render above them
	drawStructureMarkers( ul.x, ul.y, scaledWidth, scaledHeight, FALSE );

	// refresh the overlay texture once every so many frames
	if( TheGameClient->getFrame() % OVERLAY_REFRESH_RATE == 0 )
	{
		updateObjectTexture(m_overlayTexture);
	}

	// draw the overlay image
	{
		RADAR_PERF_SCOPE("Cl_RD_OverlayImg");
		TheDisplay->drawImage( m_overlayImage, ul.x, ul.y, lr.x, lr.y );
	}

	// draw resources last among world markers so they stay above units and buildings
	drawStructureMarkers( ul.x, ul.y, scaledWidth, scaledHeight, TRUE );

	// draw the shroud image
#if ENABLE_CONFIGURABLE_SHROUD
	if( TheGlobalData->m_shroudOn )
#else
	if (true)
#endif
	{
		if( m_shroudTextureDirty )
			rebuildShroudTexture();
		{
			RADAR_PERF_SCOPE("Cl_RD_ShroudImg");
			TheDisplay->drawImage( m_shroudImage, ul.x, ul.y, lr.x, lr.y );
		}
	}

	// draw any icons
	drawIcons( ul.x, ul.y, scaledWidth, scaledHeight );

	// draw any radar events
	drawEvents( ul.x, ul.y, scaledWidth, scaledHeight );

	if( m_reconstructViewBox )
	{
		RADAR_PERF_SCOPE("Cl_RD_ViewRebuild");
		reconstructViewBox();
	}

	// draw the view region on top of the radar reconstructing if necessary
	drawViewBox( ul.x, ul.y, scaledWidth, scaledHeight );

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DRadar::refreshTerrain( TerrainLogic *terrain )
{

	// extend base class
	Radar::refreshTerrain( terrain );

	// rebuild the entire terrain texture
	buildTerrainTexture( terrain );

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DRadar::refreshObjects()
{
	if constexpr (OVERLAY_REFRESH_RATE > 1)
	{
		if (m_overlayTexture != nullptr)
		{
			updateObjectTexture(m_overlayTexture);
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void W3DRadar::notifyViewChanged()
{
	m_reconstructViewBox = TRUE;
}


///The following is an "archive" of an attempt to foil the mapshroud hack... saved for later, since it is too close to release to try it


/*
 *
	void W3DRadar::renderObjectList( const RadarObject *listHead, TextureClass *texture )
{

	// sanity
	if( listHead == nullptr || texture == nullptr )
		return;

	// get surface for texture to render into
	SurfaceClass *surface = texture->Get_Surface_Level();

	// loop through all objects and draw
	ICoord2D radarPoint;

	Player *player = rts::getObservedOrLocalPlayer();
	const Int playerIndex = player->getPlayerIndex();

	UnsignedByte minAlpha = 8;

	int pitch;
	void *pBits = surface->Lock(&pitch);
	const unsigned int bytesPerPixel = surface->Get_Bytes_Per_Pixel();

	for( const RadarObject *rObj = listHead; rObj; rObj = rObj->friend_getNext() )
	{
    UnsignedByte h = (UnsignedByte)(rObj->isTemporarilyHidden());
    if ( h )
			continue;

    UnsignedByte a = 0;

		// get object
		const Object *obj = rObj->friend_getObject();
		UnsignedByte r = 1;   // all decoys

		// get the color we're going to draw in
		UnsignedInt c = 0xfe000000;// this is a decoy
    c |= (UnsignedInt)( obj->testStatus( OBJECT_STATUS_STEALTHED ) );//so is this

		// check for shrouded status
		UnsignedByte k =  (UnsignedByte)(obj->getShroudedStatus(playerIndex) > OBJECTSHROUD_PARTIAL_CLEAR);
    if ( k || a)
			continue;	//object is fogged or shrouded, don't render it.

 		//
 		// objects with a local only unit priority will only appear on the radar if they
 		// are controlled by the local player, or if the local player is an observer (cause
		// they are godlike and can see everything)
 		//
 		if( obj->getRadarPriority() == RADAR_PRIORITY_LOCAL_UNIT_ONLY &&
				obj->getControllingPlayer() != player &&
				player->isPlayerActive() )
 			continue;

    UnsignedByte g = c|a;
    UnsignedByte b = h|a;
		// get object position
		const Coord3D *pos = obj->getPosition();

		// compute object position as a radar blip
		radarPoint.x = pos->x / (m_mapExtent.width() / RADAR_CELL_WIDTH);
		radarPoint.y = pos->y / (m_mapExtent.height() / RADAR_CELL_HEIGHT);


		const UnsignedInt framesForTransition = LOGICFRAMES_PER_SECOND;



		// adjust the alpha for stealth units so they "fade/blink" on the radar for the controller
		// if( obj->getRadarPriority() == RADAR_PRIORITY_LOCAL_UNIT_ONLY )
		// ML-- What the heck is this? local-only and neutral-observier-viewed units are stealthy?? Since when?
		// Now it twinkles for any stealthed object, whether locally controlled or neutral-observier-viewed
    c = rObj->getColor();

		if( g & r )
		{
		  Real alphaScale = INT_TO_REAL(TheGameLogic->getFrame() % framesForTransition) / (framesForTransition * 0.5f);
      minAlpha <<= 2; // decoy

 			if ( ( obj->isLocallyControlled() == (Bool)a ) // another decoy, comparing the return of this non-inline with a local
        && !obj->testStatus( OBJECT_STATUS_DISGUISED )
        && !obj->testStatus( OBJECT_STATUS_DETECTED )
        && ++a != 0 // The trick is that this increment does not occur unless all three above conditions are true
        && minAlpha == 32  // tricksy hobbit decoy
        && c != 0 )        // ditto
      {
        g = (UnsignedByte)(rObj->getColor());
        continue;
      }

      a |= k | b;
			GameGetColorComponentsWithCheatSpy( c, &r, &g, &b, &a );//this function does not touch the low order bit in 'a'


			if( alphaScale > 0.0f )
				a = REAL_TO_UNSIGNEDBYTE( ((alphaScale - 1.0f) * (255.0f - minAlpha)) + minAlpha );
			else
				a = REAL_TO_UNSIGNEDBYTE( (alphaScale * (255.0f - minAlpha)) + minAlpha );
			c = GameMakeColor( r, g, b, a );

		}




		// draw the blip, but make sure the points are legal
		if( legalRadarPoint( radarPoint.x, radarPoint.y ) )
			surface->Draw_Pixel( radarPoint.x, radarPoint.y, c, bytesPerPixel, pBits, pitch );

		radarPoint.x++;
		if( legalRadarPoint( radarPoint.x, radarPoint.y ) )
			surface->Draw_Pixel( radarPoint.x, radarPoint.y, c, bytesPerPixel, pBits, pitch );

		radarPoint.y++;
		if( legalRadarPoint( radarPoint.x, radarPoint.y ) )
			surface->Draw_Pixel( radarPoint.x, radarPoint.y, c, bytesPerPixel, pBits, pitch );

		radarPoint.x--;
		if( legalRadarPoint( radarPoint.x, radarPoint.y ) )
			surface->Draw_Pixel( radarPoint.x, radarPoint.y, c, bytesPerPixel, pBits, pitch );




	}

	surface->Unlock();
	REF_PTR_RELEASE(surface);

}


 *
 */
