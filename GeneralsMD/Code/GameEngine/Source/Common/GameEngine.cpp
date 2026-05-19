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

// GameEngine.cpp /////////////////////////////////////////////////////////////////////////////////
// Implementation of the Game Engine singleton
// Author: Michael S. Booth, April 2001

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ActionManager.h"
#include "Common/AudioAffect.h"
#include "Common/BuildAssistant.h"
#include "Common/CRCDebug.h"
#include "Common/FramePacer.h"
#include "Common/Radar.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "Common/PlayerList.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/MessageStream.h"
#include "Common/ThingFactory.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/LocalFileSystem.h"
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/RandomValue.h"
#include "Common/NameKeyGenerator.h"
#include "Common/ModuleFactory.h"
#include "Common/Debug.h"
#include "Common/GameState.h"
#include "Common/GameStateMap.h"
#include "Common/Science.h"
#include "Common/FunctionLexicon.h"
#include "Common/CommandLine.h"
#include "Common/DamageFX.h"
#include "Common/MultiplayerSettings.h"
#include "Common/Recorder.h"
#include "Common/SpecialPower.h"
#include "Common/TerrainTypes.h"
#include "Common/Upgrade.h"
#include "Common/OptionPreferences.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"
#include "Common/GameLOD.h"
#include "Common/Registry.h"
#include "Common/GameCommon.h"	// FOR THE ALLOW_DEBUG_CHEATS_IN_RELEASE #define

#include "GameLogic/Armor.h"
#include "GameLogic/AI.h"
#include "GameLogic/CaveSystem.h"
#include "GameLogic/CrateSystem.h"
#include "GameLogic/Damage.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/SidesList.h"

#include "GameClient/ClientInstance.h"
#include "GameClient/FXList.h"
#include "GameClient/GameClient.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Shell.h"
#include "GameClient/GameText.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/Water.h"
#include "GameClient/TerrainRoads.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/MapUtil.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Drawable.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/View.h"

#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/WOLBrowser/WebBrowser.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/GameSpy/GameResultsThread.h"

#include "Common/version.h"

#if RTS_ZEROHOUR
#include "../../../../../Core/GameEngine/Source/Common/System/AirGeneralMapAutoPatch.inl"
#endif

#include "../NextGenMP_defines.h"

// GENERALS ONLINE
#include "../OnlineServices_Init.h"
#include "GameNetwork/GameSpyOverlay.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <string>
#include <vector>
#include "ww3d.h"

static bool g_bTearDownGeneralsOnlineRequested = false;
void TearDownGeneralsOnline()
{
	g_bTearDownGeneralsOnlineRequested = true;

	if (NGMP_OnlineServicesManager::GetInstance() == nullptr)
		return;

	EGOTearDownReason teardownReason = NGMP_OnlineServicesManager::GetInstance()->GetTeardownReason();

	if (teardownReason != EGOTearDownReason::USER_REQUESTED_SILENT)
	{
		UnicodeString title, body;

		if (teardownReason == EGOTearDownReason::USER_LOGOUT)
		{
			title = L"Logged Out";
			body = L"You are now logged out of GeneralsOnline.";
		}
		else if (teardownReason == EGOTearDownReason::LOST_CONNECTION)
		{
			title = TheGameText->fetch("GUI:GSErrorTitle");
			body = L"Your connection to the Generals Online servers was lost.";
		}
		else
		{
			title = TheGameText->fetch("GUI:GSErrorTitle");
			body = L"An unknown error occurred.";
		}

		NGMP_OnlineServicesManager::GetInstance()->ResetPendingFullTeardownReason();

		GameSpyCloseAllOverlays();
		GSMessageBoxOk(title, body);
	}
}


//-------------------------------------------------------------------------------------------------

#ifdef DEBUG_CRC
class DeepCRCSanityCheck : public SubsystemInterface
{
public:
	DeepCRCSanityCheck() {}
	virtual ~DeepCRCSanityCheck() {}

	virtual void init() {}
	virtual void reset();
	virtual void update() {}

protected:
};

DeepCRCSanityCheck* TheDeepCRCSanityCheck = nullptr;

void DeepCRCSanityCheck::reset()
{
	static Int timesThrough = 0;
	static UnsignedInt lastCRC = 0;

	AsciiString fname;
	fname.format("%sCRCAfter%dMaps.dat", TheGlobalData->getPath_UserData().str(), timesThrough);
	UnsignedInt thisCRC = TheGameLogic->getCRC(CRC_RECALC, fname);

	DEBUG_LOG(("DeepCRCSanityCheck: CRC is %X", thisCRC));
	DEBUG_ASSERTCRASH(timesThrough == 0 || thisCRC == lastCRC,
		("CRC after reset did not match beginning CRC!\nNetwork games won't work after this.\nOld: 0x%8.8X, New: 0x%8.8X",
			lastCRC, thisCRC));
	lastCRC = thisCRC;

	timesThrough++;
}
#endif // DEBUG_CRC

//-------------------------------------------------------------------------------------------------
/// The GameEngine singleton instance
GameEngine* TheGameEngine = nullptr;

//-------------------------------------------------------------------------------------------------
SubsystemInterfaceList* TheSubsystemList = nullptr;

//-------------------------------------------------------------------------------------------------
template<class SUBSYSTEM>
void initSubsystem(
	SUBSYSTEM*& sysref,
	AsciiString name,
	SUBSYSTEM* sys,
	Xfer* pXfer,
	const char* path1 = nullptr,
	const char* path2 = nullptr)
{
	sysref = sys;
	TheSubsystemList->initSubsystem(sys, path1, path2, pXfer, name);
}

//-------------------------------------------------------------------------------------------------
extern HINSTANCE ApplicationHInstance;  ///< our application instance
extern CComModule _Module;

//-------------------------------------------------------------------------------------------------
static void updateTGAtoDDS();

//-------------------------------------------------------------------------------------------------
static void updateWindowTitle()
{
	// TheSuperHackers @tweak Now prints product and version information in the Window title.

	DEBUG_ASSERTCRASH(TheVersion != nullptr, ("TheVersion is null"));
	DEBUG_ASSERTCRASH(TheGameText != nullptr, ("TheGameText is null"));

	UnicodeString title;

	if (rts::ClientInstance::getInstanceId() > 1u)
	{
		UnicodeString str;
		str.format(L"Instance:%.2u", rts::ClientInstance::getInstanceId());
		title.concat(str);
	}

	UnicodeString productString = TheVersion->getUnicodeProductString();

	if (!productString.isEmpty())
	{
		if (!title.isEmpty())
			title.concat(L" ");
		title.concat(productString);
	}

#if RTS_GENERALS
	const WideChar* defaultGameTitle = L"Command and Conquer Generals";
#elif RTS_ZEROHOUR
	const WideChar* defaultGameTitle = L"Command and Conquer Generals Zero Hour";
#endif
	UnicodeString gameTitle = TheGameText->FETCH_OR_SUBSTITUTE("GUI:Command&ConquerGenerals", defaultGameTitle);

	if (!gameTitle.isEmpty())
	{
		UnicodeString gameTitleFinal;
		UnicodeString gameVersion = TheVersion->getUnicodeVersion();

		if (productString.isEmpty())
		{
			gameTitleFinal = gameTitle;
		}
		else
		{
			UnicodeString gameTitleFormat = TheGameText->FETCH_OR_SUBSTITUTE("Version:GameTitle", L"for %ls");
			gameTitleFinal.format(gameTitleFormat.str(), gameTitle.str());
		}

		if (!title.isEmpty())
			title.concat(L" ");
		title.concat(gameTitleFinal.str());
		title.concat(L" ");
		title.concat(gameVersion.str());
	}

	if (!title.isEmpty())
	{
		AsciiString titleA;
		titleA.translate(title);	//get ASCII version for Win 9x

		extern HWND ApplicationHWnd;  ///< our application window handle
		if (ApplicationHWnd) {
			//Set it twice because Win 9x does not support SetWindowTextW.
			::SetWindowText(ApplicationHWnd, titleA.str());
			::SetWindowTextW(ApplicationHWnd, title.str());
		}
	}
}

//-------------------------------------------------------------------------------------------------
GameEngine::GameEngine()
{
	// initialize to non garbage values
	m_logicTimeAccumulator = 0.0f;
	m_quitting = FALSE;
	m_isActive = FALSE;

	_Module.Init(nullptr, ApplicationHInstance, nullptr);
}

//-------------------------------------------------------------------------------------------------
GameEngine::~GameEngine()
{
	//extern std::vector<std::string>	preloadTextureNamesGlobalHack;
	//preloadTextureNamesGlobalHack.clear();

	delete TheMapCache;
	TheMapCache = nullptr;

	//	delete TheShell;
	//	TheShell = nullptr;

	TheGameResultsQueue->endThreads();

	// TheSuperHackers @fix helmutbuhler 03/06/2025
	// Reset all subsystems before deletion to prevent crashing due to cross dependencies.
	reset();

	TheSubsystemList->shutdownAll();
	delete TheSubsystemList;
	TheSubsystemList = nullptr;

	delete TheNetwork;
	TheNetwork = nullptr;

	delete TheCommandList;
	TheCommandList = nullptr;

	delete TheNameKeyGenerator;
	TheNameKeyGenerator = nullptr;

	delete TheFileSystem;
	TheFileSystem = nullptr;

	delete TheGameLODManager;
	TheGameLODManager = nullptr;
	// GENERALS ONLINE
	NGMP_OnlineServicesManager::DestroyInstance();
	Drawable::killStaticImages();

	_Module.Term();

#ifdef PERF_TIMERS
	PerfGather::termPerfDump();
#endif
	// Kill sentry
	NGMP_OnlineServicesManager::ShutdownSentry();}

//-------------------------------------------------------------------------------------------------

Bool GameEngine::isTimeFrozen()
{
	// TheSuperHackers @fix The time can no longer be frozen in Network games. It would disconnect the player.
	if (TheNetwork != nullptr)
		return false;

	if (TheTacticalView != nullptr)
	{
		if (TheTacticalView->isTimeFrozen() && !TheTacticalView->isCameraMovementFinished())
			return true;
	}

	if (TheScriptEngine != nullptr)
	{
		if (TheScriptEngine->isTimeFrozenDebug() || TheScriptEngine->isTimeFrozenScript())
			return true;
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
Bool GameEngine::isGameHalted()
{
	if (TheNetwork != nullptr)
	{
		if (TheNetwork->isStalling())
			return true;
	}
	else
	{
		if (TheGameLogic != nullptr && TheGameLogic->isGamePaused())
			return true;
	}

	return false;
}

/** -----------------------------------------------------------------------------------------------
 * Initialize the game engine by initializing the GameLogic and GameClient.
 */
void GameEngine::init()
{
	try {
		//create an INI object to use for loading stuff
		INI ini;

#ifdef DEBUG_LOGGING
		if (TheVersion)
		{
			DEBUG_LOG(("================================================================================"));
			DEBUG_LOG(("Generals version %s", TheVersion->getAsciiVersion().str()));
			DEBUG_LOG(("Build date: %s", TheVersion->getAsciiBuildTime().str()));
			DEBUG_LOG(("Build location: %s", TheVersion->getAsciiBuildLocation().str()));
			DEBUG_LOG(("Build user: %s", TheVersion->getAsciiBuildUser().str()));
			DEBUG_LOG(("Build git revision: %s", TheVersion->getAsciiGitCommitCount().str()));
			DEBUG_LOG(("Build git version: %s", TheVersion->getAsciiGitTagOrHash().str()));
			DEBUG_LOG(("Build git commit time: %s", TheVersion->getAsciiGitCommitTime().str()));
			DEBUG_LOG(("Build git commit author: %s", Version::getGitCommitAuthorName()));
			DEBUG_LOG(("================================================================================"));
		}
#endif

#if defined(PERF_TIMERS) || defined(DUMP_PERF_STATS)
		DEBUG_LOG(("Calculating CPU frequency for performance timers."));
		InitPrecisionTimer();
#endif
#ifdef PERF_TIMERS
		PerfGather::initPerfDump("AAAPerfStats", PerfGather::PERF_NETTIME);
#endif




#ifdef DUMP_PERF_STATS////////////////////////////////////////////////////////////
		__int64 startTime64;//////////////////////////////////////////////////////////////
		__int64 endTime64, freq64;///////////////////////////////////////////////////////////
		GetPrecisionTimerTicksPerSec(&freq64);///////////////////////////////////////////////
		GetPrecisionTimer(&startTime64);////////////////////////////////////////////////////
		char Buf[256];//////////////////////////////////////////////////////////////////////
#endif//////////////////////////////////////////////////////////////////////////////


		TheSubsystemList = MSGNEW("GameEngineSubsystem") SubsystemInterfaceList;

		TheSubsystemList->addSubsystem(this);

		// initialize the random number system
		InitRandom();

		// Create the low-level file system interface
		TheFileSystem = createFileSystem();

		// not part of the subsystem list, because it should normally never be reset!
		TheNameKeyGenerator = MSGNEW("GameEngineSubsystem") NameKeyGenerator;
		TheNameKeyGenerator->init();


#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
		GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
		sprintf(Buf, "----------------------------------------------------------------------------After TheNameKeyGenerator  = %f seconds", ((double)(endTime64 - startTime64) / (double)(freq64)));
		startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
		DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
#endif/////////////////////////////////////////////////////////////////////////////////////////////


		// not part of the subsystem list, because it should normally never be reset!
		TheCommandList = MSGNEW("GameEngineSubsystem") CommandList;
		TheCommandList->init();

#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
		GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
		sprintf(Buf, "----------------------------------------------------------------------------After TheCommandList  = %f seconds", ((double)(endTime64 - startTime64) / (double)(freq64)));
		startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
		DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
#endif/////////////////////////////////////////////////////////////////////////////////////////////


		XferCRC xferCRC;
		xferCRC.open("lightCRC");


		initSubsystem(TheLocalFileSystem, "TheLocalFileSystem", createLocalFileSystem(), nullptr);

#if RTS_ZEROHOUR
		rts::airgeneral_patch::patchMapsZhBigIfNeeded();
#endif


#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
		GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
		sprintf(Buf, "----------------------------------------------------------------------------After TheLocalFileSystem  = %f seconds", ((double)(endTime64 - startTime64) / (double)(freq64)));
		startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
		DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheArchiveFileSystem, "TheArchiveFileSystem", createArchiveFileSystem(), nullptr); // this MUST come after TheLocalFileSystem creation

#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
		GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
		sprintf(Buf, "----------------------------------------------------------------------------After TheArchiveFileSystem  = %f seconds", ((double)(endTime64 - startTime64) / (double)(freq64)));
		startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
		DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
#endif/////////////////////////////////////////////////////////////////////////////////////////////


		DEBUG_ASSERTCRASH(TheWritableGlobalData, ("TheWritableGlobalData expected to be created"));
		initSubsystem(TheWritableGlobalData, "TheWritableGlobalData", TheWritableGlobalData, &xferCRC, "Data\\INI\\Default\\GameData", "Data\\INI\\GameData");
		TheWritableGlobalData->parseCustomDefinition();

		// Init sentry ASAP to catch early crashes
		NGMP_OnlineServicesManager::InitSentry();

#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
		GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
		sprintf(Buf, "----------------------------------------------------------------------------After  TheWritableGlobalData = %f seconds", ((double)(endTime64 - startTime64) / (double)(freq64)));
		startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
		DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
#endif/////////////////////////////////////////////////////////////////////////////////////////////



#if defined(RTS_DEBUG)
		// If we're in Debug, load the Debug settings as well.
		ini.loadFileDirectory("Data\\INI\\GameDataDebug", INI_LOAD_OVERWRITE, nullptr);
#endif

		// special-case: parse command-line parameters after loading global data
		CommandLine::parseCommandLineForEngineInit();

		TheArchiveFileSystem->loadMods();

		// doesn't require resets so just create a single instance here.
		TheGameLODManager = MSGNEW("GameEngineSubsystem") GameLODManager;
		TheGameLODManager->init();

		// after parsing the command line, we may want to perform dds stuff. Do that here.
		if (TheGlobalData->m_shouldUpdateTGAToDDS) {
			// update any out of date targas here.
			updateTGAtoDDS();
		}

		// read the water settings from INI (must do prior to initing GameClient, apparently)
		ini.loadFileDirectory("Data\\INI\\Default\\Water", INI_LOAD_OVERWRITE, &xferCRC);
		ini.loadFileDirectory("Data\\INI\\Water", INI_LOAD_OVERWRITE, &xferCRC);
		ini.loadFileDirectory("Data\\INI\\Default\\Weather", INI_LOAD_OVERWRITE, &xferCRC);
		ini.loadFileDirectory("Data\\INI\\Weather", INI_LOAD_OVERWRITE, &xferCRC);



#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
		GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
		sprintf(Buf, "----------------------------------------------------------------------------After water INI's = %f seconds", ((double)(endTime64 - startTime64) / (double)(freq64)));
		startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
		DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
#endif/////////////////////////////////////////////////////////////////////////////////////////////


#ifdef DEBUG_CRC
		initSubsystem(TheDeepCRCSanityCheck, "TheDeepCRCSanityCheck", MSGNEW("GameEngineSubystem") DeepCRCSanityCheck, nullptr);
#endif // DEBUG_CRC
		initSubsystem(TheGameText, "TheGameText", CreateGameTextInterface(), nullptr);
		updateWindowTitle();

#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
		GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
		sprintf(Buf, "----------------------------------------------------------------------------After TheGameText = %f seconds", ((double)(endTime64 - startTime64) / (double)(freq64)));
		startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
		DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
#endif/////////////////////////////////////////////////////////////////////////////////////////////


#if RETAIL_COMPATIBLE_CRC
		if (xferCRC.getCRC() == 0xA1E7F8E6)
			TheNameKeyGenerator->verifyNameKeyID(1);
#endif

		initSubsystem(TheScienceStore,"TheScienceStore", MSGNEW("GameEngineSubsystem") ScienceStore(), &xferCRC, "Data\\INI\\Default\\Science", "Data\\INI\\Science");
		initSubsystem(TheMultiplayerSettings,"TheMultiplayerSettings", MSGNEW("GameEngineSubsystem") MultiplayerSettings(), &xferCRC, "Data\\INI\\Default\\Multiplayer", "Data\\INI\\Multiplayer");
		initSubsystem(TheTerrainTypes,"TheTerrainTypes", MSGNEW("GameEngineSubsystem") TerrainTypeCollection(), &xferCRC, "Data\\INI\\Default\\Terrain", "Data\\INI\\Terrain");
		initSubsystem(TheTerrainRoads,"TheTerrainRoads", MSGNEW("GameEngineSubsystem") TerrainRoadCollection(), &xferCRC, "Data\\INI\\Default\\Roads", "Data\\INI\\Roads");
		initSubsystem(TheGlobalLanguageData,"TheGlobalLanguageData",MSGNEW("GameEngineSubsystem") GlobalLanguage, nullptr); // must be before the game text
		TheGlobalLanguageData->parseCustomDefinition();
	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheGlobalLanguageData = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////
		initSubsystem(TheAudio,"TheAudio", TheGlobalData->m_headless ? NEW AudioManagerDummy : createAudioManager(), nullptr);
		if (!TheAudio->isMusicAlreadyLoaded())
			setQuitting(TRUE);

#if RTS_ZEROHOUR && RETAIL_COMPATIBLE_CRC
		TheNameKeyGenerator->syncNameKeyID();
#endif

	#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
	GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
	sprintf(Buf,"----------------------------------------------------------------------------After TheAudio = %f seconds",((double)(endTime64-startTime64)/(double)(freq64)));
  startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
	DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
	#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheFunctionLexicon,"TheFunctionLexicon", createFunctionLexicon(), nullptr);
		initSubsystem(TheModuleFactory,"TheModuleFactory", createModuleFactory(), nullptr);
		initSubsystem(TheMessageStream,"TheMessageStream", createMessageStream(), nullptr);
		initSubsystem(TheSidesList,"TheSidesList", MSGNEW("GameEngineSubsystem") SidesList(), nullptr);
		initSubsystem(TheCaveSystem,"TheCaveSystem", MSGNEW("GameEngineSubsystem") CaveSystem(), nullptr);
		initSubsystem(TheRankInfoStore,"TheRankInfoStore", MSGNEW("GameEngineSubsystem") RankInfoStore(), &xferCRC, nullptr, "Data\\INI\\Rank");
		initSubsystem(ThePlayerTemplateStore,"ThePlayerTemplateStore", MSGNEW("GameEngineSubsystem") PlayerTemplateStore(), &xferCRC, "Data\\INI\\Default\\PlayerTemplate", "Data\\INI\\PlayerTemplate");
		initSubsystem(TheParticleSystemManager,"TheParticleSystemManager", createParticleSystemManager(TheGlobalData->m_headless), nullptr);

#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
		GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
		sprintf(Buf, "----------------------------------------------------------------------------After TheParticleSystemManager = %f seconds", ((double)(endTime64 - startTime64) / (double)(freq64)));
		startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
		DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheFXListStore, "TheFXListStore", MSGNEW("GameEngineSubsystem") FXListStore(), &xferCRC, "Data\\INI\\Default\\FXList", "Data\\INI\\FXList");
		initSubsystem(TheWeaponStore, "TheWeaponStore", MSGNEW("GameEngineSubsystem") WeaponStore(), &xferCRC, nullptr, "Data\\INI\\Weapon");
		initSubsystem(TheObjectCreationListStore, "TheObjectCreationListStore", MSGNEW("GameEngineSubsystem") ObjectCreationListStore(), &xferCRC, "Data\\INI\\Default\\ObjectCreationList", "Data\\INI\\ObjectCreationList");
		initSubsystem(TheLocomotorStore, "TheLocomotorStore", MSGNEW("GameEngineSubsystem") LocomotorStore(), &xferCRC, nullptr, "Data\\INI\\Locomotor");
		initSubsystem(TheSpecialPowerStore, "TheSpecialPowerStore", MSGNEW("GameEngineSubsystem") SpecialPowerStore(), &xferCRC, "Data\\INI\\Default\\SpecialPower", "Data\\INI\\SpecialPower");
		initSubsystem(TheDamageFXStore, "TheDamageFXStore", MSGNEW("GameEngineSubsystem") DamageFXStore(), &xferCRC, nullptr, "Data\\INI\\DamageFX");
		initSubsystem(TheArmorStore, "TheArmorStore", MSGNEW("GameEngineSubsystem") ArmorStore(), &xferCRC, nullptr, "Data\\INI\\Armor");
		initSubsystem(TheBuildAssistant, "TheBuildAssistant", MSGNEW("GameEngineSubsystem") BuildAssistant, nullptr);


#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
		GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
		sprintf(Buf, "----------------------------------------------------------------------------After TheBuildAssistant = %f seconds", ((double)(endTime64 - startTime64) / (double)(freq64)));
		startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
		DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
#endif/////////////////////////////////////////////////////////////////////////////////////////////



		initSubsystem(TheThingFactory, "TheThingFactory", createThingFactory(), &xferCRC, "Data\\INI\\Default\\Object", "Data\\INI\\Object");

#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
		GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
		sprintf(Buf, "----------------------------------------------------------------------------After TheThingFactory = %f seconds", ((double)(endTime64 - startTime64) / (double)(freq64)));
		startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
		DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
#endif/////////////////////////////////////////////////////////////////////////////////////////////


#if RETAIL_COMPATIBLE_CRC
		if (xferCRC.getCRC() == 0x6209AF6E)
			TheNameKeyGenerator->verifyNameKeyID(2265);
#endif

		initSubsystem(TheUpgradeCenter,"TheUpgradeCenter", MSGNEW("GameEngineSubsystem") UpgradeCenter, &xferCRC, "Data\\INI\\Default\\Upgrade", "Data\\INI\\Upgrade");
		initSubsystem(TheGameClient,"TheGameClient", createGameClient(), nullptr);


#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
		GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
		sprintf(Buf, "----------------------------------------------------------------------------After TheGameClient = %f seconds", ((double)(endTime64 - startTime64) / (double)(freq64)));
		startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
		DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
#endif/////////////////////////////////////////////////////////////////////////////////////////////


		initSubsystem(TheAI, "TheAI", MSGNEW("GameEngineSubsystem") AI(), &xferCRC, "Data\\INI\\Default\\AIData", "Data\\INI\\AIData");
		initSubsystem(TheGameLogic, "TheGameLogic", createGameLogic(), nullptr);
		initSubsystem(TheTeamFactory, "TheTeamFactory", MSGNEW("GameEngineSubsystem") TeamFactory(), nullptr);
		initSubsystem(TheCrateSystem, "TheCrateSystem", MSGNEW("GameEngineSubsystem") CrateSystem(), &xferCRC, "Data\\INI\\Default\\Crate", "Data\\INI\\Crate");
		initSubsystem(ThePlayerList, "ThePlayerList", MSGNEW("GameEngineSubsystem") PlayerList(), nullptr);
		initSubsystem(TheRecorder, "TheRecorder", createRecorder(), nullptr);
		initSubsystem(TheRadar, "TheRadar", TheGlobalData->m_headless ? NEW RadarDummy : createRadar(), nullptr);
		initSubsystem(TheVictoryConditions, "TheVictoryConditions", createVictoryConditions(), nullptr);



#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
		GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
		sprintf(Buf, "----------------------------------------------------------------------------After TheVictoryConditions = %f seconds", ((double)(endTime64 - startTime64) / (double)(freq64)));
		startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
		DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
#endif/////////////////////////////////////////////////////////////////////////////////////////////


		AsciiString fname;
		fname.format("Data\\%s\\CommandMap", GetRegistryLanguage().str());
		initSubsystem(TheMetaMap, "TheMetaMap", MSGNEW("GameEngineSubsystem") MetaMap(), nullptr, fname.str(), "Data\\INI\\CommandMap");

		TheMetaMap->generateMetaMap();

#if defined(RTS_DEBUG)
		ini.loadFileDirectory("Data\\INI\\CommandMapDebug", INI_LOAD_MULTIFILE, nullptr);
#endif

#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		ini.loadFileDirectory("Data\\INI\\CommandMapDemo", INI_LOAD_MULTIFILE, nullptr);
#endif


		initSubsystem(TheActionManager, "TheActionManager", MSGNEW("GameEngineSubsystem") ActionManager(), nullptr);
		//initSubsystem((CComObject<WebBrowser> *)TheWebBrowser,"(CComObject<WebBrowser> *)TheWebBrowser", (CComObject<WebBrowser> *)createWebBrowser(), nullptr);
		initSubsystem(TheGameStateMap, "TheGameStateMap", MSGNEW("GameEngineSubsystem") GameStateMap, nullptr);
		initSubsystem(TheGameState, "TheGameState", MSGNEW("GameEngineSubsystem") GameState, nullptr);

		// Create the interface for sending game results
		initSubsystem(TheGameResultsQueue, "TheGameResultsQueue", GameResultsInterface::createNewGameResultsInterface(), nullptr);


#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
		GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
		sprintf(Buf, "----------------------------------------------------------------------------After TheGameResultsQueue = %f seconds", ((double)(endTime64 - startTime64) / (double)(freq64)));
		startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
		DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
#endif/////////////////////////////////////////////////////////////////////////////////////////////


		xferCRC.close();
		TheWritableGlobalData->m_iniCRC = xferCRC.getCRC();
		DEBUG_LOG(("INI CRC is 0x%8.8X", TheGlobalData->m_iniCRC));

		TheSubsystemList->postProcessLoadAll();

		TheFramePacer->setFramesPerSecondLimit(TheGlobalData->m_framesPerSecondLimit);

		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_musicOn, AudioAffect_Music);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_soundsOn, AudioAffect_Sound);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_sounds3DOn, AudioAffect_Sound3D);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_speechOn, AudioAffect_Speech);

		// We're not in a network game yet, so set the network singleton to nullptr.
		TheNetwork = nullptr;

		//Create a default ini file for options if it doesn't already exist.
		//OptionPreferences prefs( TRUE );

		// If we turn m_quitting to FALSE here, then we throw away any requests to quit that
		// took place during loading. :-\ - jkmcd
		// If this really needs to take place, please make sure that pressing cancel on the audio
		// load music dialog will still cause the game to quit.
		// m_quitting = FALSE;

		// initialize the MapCache
		TheMapCache = MSGNEW("GameEngineSubsystem") MapCache;
		TheMapCache->updateCache();


#ifdef DUMP_PERF_STATS///////////////////////////////////////////////////////////////////////////
		GetPrecisionTimer(&endTime64);//////////////////////////////////////////////////////////////////
		sprintf(Buf, "----------------------------------------------------------------------------After TheMapCache->updateCache = %f seconds", ((double)(endTime64 - startTime64) / (double)(freq64)));
		startTime64 = endTime64;//Reset the clock ////////////////////////////////////////////////////////
		DEBUG_LOG(("%s", Buf));////////////////////////////////////////////////////////////////////////////
#endif/////////////////////////////////////////////////////////////////////////////////////////////


		if (TheGlobalData->m_buildMapCache)
		{
			// just quit, since the map cache has already updated
			//populateMapListbox(nullptr, true, true);
			m_quitting = TRUE;
		}

		// load the initial shell screen
		//TheShell->push( "Menus/MainMenu.wnd" );

		// This allows us to run a map from the command line
		if (TheGlobalData->m_initialFile.isEmpty() == FALSE)
		{
			AsciiString fname = TheGlobalData->m_initialFile;
			fname.toLower();

			if (fname.endsWithNoCase(".map"))
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
				TheWritableGlobalData->m_playIntro = FALSE;
				TheWritableGlobalData->m_pendingFile = TheGlobalData->m_initialFile;

				// shutdown the top, but do not pop it off the stack
	//			TheShell->hideShell();

				// send a message to the logic for a new game
				GameMessage* msg = TheMessageStream->appendMessage(GameMessage::MSG_NEW_GAME);
				msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
				msg->appendIntegerArgument(DIFFICULTY_NORMAL);
				msg->appendIntegerArgument(0);
				InitRandom(0);
			}
		}

		//
		if (TheMapCache && TheGlobalData->m_shellMapOn)
		{
			AsciiString lowerName = TheGlobalData->m_shellMapName;
			lowerName.toLower();

			MapCache::const_iterator it = TheMapCache->find(lowerName);
			if (it == TheMapCache->end())
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
			}
		}

		if (!TheGlobalData->m_playIntro && !TheGlobalData->m_benchmarkMode)
			TheWritableGlobalData->m_afterIntro = TRUE;

	}
	catch (ErrorCode ec)
	{
		if (ec == ERROR_INVALID_D3D)
		{
			RELEASE_CRASHLOCALIZED("ERROR:D3DFailurePrompt", "ERROR:D3DFailureMessage");
		}
	}
	catch (INIException e)
	{
		if (e.mFailureMessage)
			RELEASE_CRASH((e.mFailureMessage));
		else
			RELEASE_CRASH(("Uncaught Exception during initialization."));

	}
	catch (...)
	{
		RELEASE_CRASH(("Uncaught Exception during initialization."));
	}

	if (!TheGlobalData->m_playIntro && !TheGlobalData->m_benchmarkMode)
		TheWritableGlobalData->m_afterIntro = TRUE;

	resetSubsystems();

	HideControlBar();

	// NGMP_CHANGE: Init our settings
	NGMP_OnlineServicesManager::Settings.Initialize();
}

/** -----------------------------------------------------------------------------------------------
	* Reset all necessary parts of the game engine to be ready to accept new game data
	*/
void GameEngine::reset()
{

	WindowLayout* background = TheWindowManager->winCreateLayout("Menus/BlankWindow.wnd");
	DEBUG_ASSERTCRASH(background, ("We Couldn't Load Menus/BlankWindow.wnd"));
	background->hide(FALSE);
	background->bringForward();
	background->getFirstWindow()->winClearStatus(WIN_STATUS_IMAGE);
	Bool deleteNetwork = false;
	if (TheGameLogic->isInMultiplayerGame())
		deleteNetwork = true;

	resetSubsystems();

	if (deleteNetwork)
	{
		DEBUG_ASSERTCRASH(TheNetwork, ("Deleting null TheNetwork!"));
		delete TheNetwork;
		TheNetwork = nullptr;
	}
	if (background)
	{
		background->destroyWindows();
		deleteInstance(background);
		background = nullptr;
	}
}

/// -----------------------------------------------------------------------------------------------
void GameEngine::resetSubsystems()
{
	// TheSuperHackers @fix xezon 09/06/2025 Reset GameLogic first to purge all world objects early.
	// This avoids potentially catastrophic issues when objects and subsystems have cross dependencies.
	TheGameLogic->reset();

	TheSubsystemList->resetAll();
}

/// -----------------------------------------------------------------------------------------------
Bool GameEngine::canUpdateGameLogic()
{
	// Must be first.
	TheGameLogic->preUpdate();

	TheFramePacer->setTimeFrozen(isTimeFrozen());
	TheFramePacer->setGameHalted(isGameHalted());

	if (TheNetwork != nullptr)
	{
		return canUpdateNetworkGameLogic();
	}
	else
	{
		return canUpdateRegularGameLogic();
	}
}

/// -----------------------------------------------------------------------------------------------
Bool GameEngine::canUpdateNetworkGameLogic()
{
	DEBUG_ASSERTCRASH(TheNetwork != nullptr, ("TheNetwork is null"));

	if (TheNetwork->isFrameDataReady())
	{
		// Important: The Network is definitely no longer stalling.
		TheFramePacer->setGameHalted(false);

		return true;
	}

	return false;
}

/// -----------------------------------------------------------------------------------------------
Bool GameEngine::canUpdateRegularGameLogic()
{
	const Bool enabled = TheFramePacer->isLogicTimeScaleEnabled();
	const Int logicTimeScaleFps = TheFramePacer->getLogicTimeScaleFps();
	const Int maxRenderFps = TheFramePacer->getFramesPerSecondLimit();

#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	const Bool useFastMode = TheGlobalData->m_TiVOFastMode;
#else	//always allow this cheat key if we're in a replay game.
	const Bool useFastMode = TheGlobalData->m_TiVOFastMode && TheGameLogic->isInReplayGame();
#endif

	if (useFastMode || !enabled || logicTimeScaleFps >= maxRenderFps)
	{
		// Logic time scale is uncapped or larger equal Render FPS. Update straight away.
		return true;
	}
	else
	{
		// TheSuperHackers @tweak xezon 06/08/2025
		// The logic time step is now decoupled from the render update.
		const Real targetFrameTime = 1.0f / logicTimeScaleFps;
		m_logicTimeAccumulator += min(TheFramePacer->getUpdateTime(), targetFrameTime);

		if (m_logicTimeAccumulator >= targetFrameTime)
		{
			m_logicTimeAccumulator -= targetFrameTime;
			return true;
		}
	}

	return false;
}

#if defined(GENERALS_ONLINE_HIGH_FPS_RENDER)
extern NGMPGame* TheNGMPGame;
#endif

// ---------------------------------------------------------------------------
// Performance instrumentation (Release-active). Writes a separate
// perf_HHMMSS.txt alongside the exe containing per-subsystem timings.
// Logs spikes immediately and a rolling summary every ~5 seconds.
// ---------------------------------------------------------------------------
static FILE *g_perfLogFile = nullptr;
static bool  g_perfLogOpened = false;
static LARGE_INTEGER g_perfFreq = {};
static bool  g_perfFreqInit = false;
static char  g_perfLogPath[MAX_PATH] = {};
// Benchmark mode stores gameplay-rate samples in legacy/original 30 FPS units so
// the top-line MEDIAN FPS reflects effective gameplay speed rather than the
// decoupled render loop. Raw render and logic samples are logged separately.
static std::vector<double> g_benchmarkFpsSamples;
static std::vector<double> g_benchmarkRenderFpsSamples;
static std::vector<double> g_benchmarkLogicFpsSamples;
static bool  g_benchmarkWasSampling = false;
static DWORD g_benchmarkLastLogicSampleMs = 0u;
static UnsignedInt g_benchmarkLastLogicSampleFrame = 0u;
static constexpr DWORD kBenchmarkDurationMs = 30000u;
static constexpr DWORD kBenchmarkSampleIntervalMs = 100u;
static std::vector<double> g_summaryFpsSamples;
static std::vector<double> g_summaryRenderFpsSamples;
static std::vector<double> g_summaryLogicFpsSamples;
static bool  g_summarySessionInitialized = false;
static DWORD g_summarySessionStartMs = 0u;
static DWORD g_summaryLastSampleMs = 0u;
static UnsignedInt g_summaryLastLogicSampleFrame = 0u;
static UnsignedInt g_summaryFrameCounter = 0u;
static UnsignedInt g_summaryLastFrameSampleCount = 0u;
static DWORD g_summaryLastWriteMs = 0u;
static constexpr DWORD kSummaryLogWriteIntervalMs = 1000u;

// Rolling stats (accumulated between summary flushes).
static double g_perfAccumFrameMs   = 0.0;
static double g_perfAccumClientMs  = 0.0;
static double g_perfAccumLogicMs   = 0.0;
static double g_perfAccumRadarMs   = 0.0;
static double g_perfAccumAudioMs   = 0.0;
static double g_perfAccumNetMs     = 0.0;
static double g_perfAccumMsgMs     = 0.0;
static double g_perfMaxFrameMs     = 0.0;
static double g_perfMaxClientMs    = 0.0;
static double g_perfMaxLogicMs     = 0.0;
static double g_perfMaxRadarMs     = 0.0;
static double g_perfMaxAudioMs     = 0.0;
static double g_perfMaxNetMs       = 0.0;
static double g_perfMaxMsgMs       = 0.0;
static int    g_perfFrameCount     = 0;
static DWORD  g_perfLastSummaryMs  = 0;

static double g_perfGameplayAccumFrameMs  = 0.0;
static double g_perfGameplayAccumClientMs = 0.0;
static double g_perfGameplayAccumLogicMs  = 0.0;
static double g_perfGameplayAccumRadarMs  = 0.0;
static double g_perfGameplayAccumAudioMs  = 0.0;
static double g_perfGameplayAccumNetMs    = 0.0;
static double g_perfGameplayAccumMsgMs    = 0.0;
static double g_perfGameplayMaxFrameMs    = 0.0;
static double g_perfGameplayMaxClientMs   = 0.0;
static double g_perfGameplayMaxLogicMs    = 0.0;
static double g_perfGameplayMaxRadarMs    = 0.0;
static double g_perfGameplayMaxAudioMs    = 0.0;
static double g_perfGameplayMaxNetMs      = 0.0;
static double g_perfGameplayMaxMsgMs      = 0.0;
static int    g_perfGameplayFrameCount    = 0;

enum PerfFrameState
{
	PERF_FRAME_BOOT = 0,
	PERF_FRAME_SHELL,
	PERF_FRAME_LOADING,
	PERF_FRAME_PAUSED,
	PERF_FRAME_STALL,
	PERF_FRAME_GAMEPLAY,
	kPerfFrameStateCount
};

static int g_perfStateFrameCounts[kPerfFrameStateCount] = {};

enum PerfMetricKind
{
	PERF_METRIC_ACCUM = 0,
	PERF_METRIC_FRAME,
	PERF_METRIC_GAMEPLAY
};

static const double kPerfMsgStallThresholdMs = 100.0;
static const double kPerfSpikeThresholdMs = 33.0;
static const double kPerfSlowFrameThresholdMs = 25.0;

static inline bool PerfProfilingEnabled()
{
	return TheGlobalData != nullptr && TheGlobalData->m_enableRuntimeProfiling;
}

static inline bool PerfSummaryOnlyLoggingEnabled()
{
	return TheGlobalData != nullptr && !TheGlobalData->m_benchmarkMode && !PerfProfilingEnabled();
}

static bool PerfBuildUniqueLogPath(char *path, size_t pathSize)
{
	if (path == nullptr || pathSize == 0)
		return false;

	char exeDir[MAX_PATH] = {};
	::GetModuleFileNameA(nullptr, exeDir, MAX_PATH);
	char *slash = strrchr(exeDir, '\\');
	if (slash) *(slash + 1) = '\0'; else exeDir[0] = '\0';

	time_t now = time(nullptr);
	struct tm *t = localtime(&now);
	if (t == nullptr)
		return false;

	snprintf(path, pathSize, "%sperf_%02d%02d%02d.txt", exeDir,
		t->tm_hour, t->tm_min, t->tm_sec);

	FILE *existing = fopen(path, "rb");
	if (existing == nullptr)
		return true;
	fclose(existing);

	for (int i = 2; i <= 9; ++i)
	{
		snprintf(path, pathSize, "%sperf_%02d%02d%02d_%d.txt", exeDir,
			t->tm_hour, t->tm_min, t->tm_sec, i);
		existing = fopen(path, "rb");
		if (existing == nullptr)
			return true;
		fclose(existing);
	}

	return false;
}

// Per-subsystem accumulators for GameLogic & GameClient internals.
// Keys are short string tags (e.g. "script", "terrain", "sleepy", "ai", "partition").
// Simple fixed-size table keyed by tag pointer (same static literals used on each call).
struct PerfSubEntry
{
	const char *tag;
	double accum;
	double inclAccum;
	double maxMs;
	double inclMaxMs;
	double maxFrameMs;
	double inclMaxFrameMs;
	int maxFrameCalls;
	int inclMaxFrameCalls;
	int calls;
	int inclCalls;
	double frameMs;
	double inclFrameMs;
	int frameCalls;
	int inclFrameCalls;
	double gameplayAccum;
	double inclGameplayAccum;
	double gameplayMaxFrameMs;
	double inclGameplayMaxFrameMs;
	int gameplayCalls;
	int inclGameplayCalls;
	bool scopedExclusive;
};

struct PerfSubSnapshot
{
	const char *tag;
	double ms;
	int calls;
};

enum { kPerfNamedSnapshotCap = 10 };

struct PerfNamedSnapshot
{
	const char *category;
	const char *name;
	double ms;
	int calls;
};

struct PerfFrameSnapshot
{
	bool valid;
	unsigned int frame;
	PerfFrameState state;
	double frameMs;
	double clientMs;
	double logicMs;
	double radarMs;
	double audioMs;
	double netMs;
	double msgMs;
	double otherMs;
	double leafTrackedMs;
	double inclTrackedMs;
	double clientLeafMs;
	double logicLeafMs;
	int leafCount;
	int inclCount;
	int namedCount;
	PerfSubSnapshot leaf[12];
	PerfSubSnapshot incl[12];
	PerfNamedSnapshot named[kPerfNamedSnapshotCap];
};

struct PerfContextSummary
{
	bool valid;
	int samples;
	int gameplaySamples;
	int latestFramePacerFps;
	int latestActualFps;
	int latestGlobalFps;
	int latestFpsLimitEnabled;
	int latestGlobalUseFpsLimit;
	int latestViewWidth;
	int latestViewHeight;
	Real latestZoom;
	Real latestHeightAboveGround;
	Real latestPitch;
	Real latestFov;
	Coord3D latestPivot;
	double zoomAccum;
	double heightAccum;
	double fovAccum;
	double gameplayZoomAccum;
	double gameplayHeightAccum;
	Real zoomMin;
	Real zoomMax;
	Real heightMin;
	Real heightMax;
	Real gameplayZoomMin;
	Real gameplayZoomMax;
	Real gameplayHeightMin;
	Real gameplayHeightMax;
};

struct PerfRenderStatEntry
{
	const char *tag;
	int samples;
	int highFpsSamples;
	int latestHighFps;
	int latestProcessed;
	int latestRendered;
	int latestCulled;
	int latestSkipped;
	int latestBudget;
	int maxProcessed;
	int maxRendered;
	int maxCulled;
	int maxSkipped;
	int maxBudget;
	double accumProcessed;
	double accumRendered;
	double accumCulled;
	double accumSkipped;
	double accumBudget;
	int gameplaySamples;
	int gameplayHighFpsSamples;
	double gameplayProcessed;
	double gameplayRendered;
	double gameplayCulled;
	double gameplaySkipped;
	double gameplayBudget;
};

struct PerfNamedCostEntry
{
	const char *category;
	char name[64];
	double accum;
	double maxMs;
	double maxFrameMs;
	int maxFrameCalls;
	int calls;
	double frameMs;
	int frameCalls;
	double gameplayAccum;
	double gameplayMaxFrameMs;
	int gameplayCalls;
};

struct PerfEdgeEntry
{
	const char *parent;
	const char *child;
	double accum;
	double maxMs;
	double maxFrameMs;
	int maxFrameCalls;
	int calls;
	double frameMs;
	int frameCalls;
	double gameplayAccum;
	double gameplayMaxFrameMs;
	int gameplayMaxFrameCalls;
	int gameplayCalls;
};

struct PerfZoomBin
{
	const char *label;
	int samples;
	double zoomAccum;
	double heightAccum;
	double frameAccum;
	double clientAccum;
	double logicAccum;
	double radarAccum;
	double audioAccum;
	double netAccum;
	double msgAccum;
	double clientLeafAccum;
	double logicLeafAccum;
	double maxFrameMs;
	double maxClientMs;
	double maxLogicMs;
};

struct PerfScopeStackEntry
{
	const char *tag;
	LARGE_INTEGER begin;
	double childMs;
};

enum { kPerfSubCap = 256 };
enum { kPerfWorstSubCap = 12 };
enum { kPerfFrameSampleCap = 4096 };
enum { kPerfRenderStatCap = 32 };
enum { kPerfNamedCostCap = 4096 };
enum { kPerfEdgeCap = 4096 };
enum { kPerfLookupCacheSize = 512 };
enum { kPerfZoomBinCount = 7 };
enum { kPerfScopeStackCap = 96 };
static PerfSubEntry g_perfSubs[kPerfSubCap] = {};
static int g_perfSubCount = 0;
static double g_perfDroppedSubAccumMs = 0.0;
static double g_perfDroppedSubMaxMs = 0.0;
static double g_perfDroppedSubFrameMs = 0.0;
static int g_perfDroppedSubCalls = 0;
static int g_perfDroppedSubFrameCalls = 0;
static PerfScopeStackEntry g_perfScopeStack[kPerfScopeStackCap] = {};
static int g_perfScopeDepth = 0;
static int g_perfScopeOverflow = 0;
static int g_perfScopeMismatch = 0;

static double g_perfFrameSamples[kPerfFrameSampleCap] = {};
static double g_perfClientSamples[kPerfFrameSampleCap] = {};
static double g_perfLogicSamples[kPerfFrameSampleCap] = {};
static double g_perfMsgSamples[kPerfFrameSampleCap] = {};
static int g_perfFrameSampleCount = 0;
static int g_perfFrameSampleOverflow = 0;
static PerfFrameSnapshot g_perfWorstFrame = {};
static PerfFrameSnapshot g_perfGameplayWorstFrame = {};
static PerfContextSummary g_perfContext = {};
static PerfRenderStatEntry g_perfRenderStats[kPerfRenderStatCap] = {};
static int g_perfRenderStatCount = 0;
static PerfNamedCostEntry g_perfNamedCosts[kPerfNamedCostCap] = {};
static int g_perfNamedCostCount = 0;
static double g_perfDroppedNamedAccumMs = 0.0;
static double g_perfDroppedNamedFrameMs = 0.0;
static int g_perfDroppedNamedCalls = 0;
static int g_perfDroppedNamedFrameCalls = 0;
static PerfEdgeEntry g_perfEdges[kPerfEdgeCap] = {};
static int g_perfEdgeCount = 0;
static double g_perfDroppedEdgeAccumMs = 0.0;
static double g_perfDroppedEdgeFrameMs = 0.0;
static double g_perfDroppedEdgeMaxMs = 0.0;
static int g_perfDroppedEdgeCalls = 0;
static int g_perfDroppedEdgeFrameCalls = 0;
struct PerfSubLookupEntry
{
	const char *tag;
	int index;
};
struct PerfEdgeLookupEntry
{
	const char *parent;
	const char *child;
	int index;
};
struct PerfNamedLookupEntry
{
	const char *category;
	const char *name;
	int index;
};
static PerfSubLookupEntry g_perfSubLookupCache[kPerfLookupCacheSize] = {};
static PerfEdgeLookupEntry g_perfEdgeLookupCache[kPerfLookupCacheSize] = {};
static PerfNamedLookupEntry g_perfNamedLookupCache[kPerfLookupCacheSize] = {};
static PerfZoomBin g_perfZoomBins[kPerfZoomBinCount] =
{
	{ "<1.50" },
	{ "1.50-2.00" },
	{ "2.00-2.25" },
	{ "2.25-2.50" },
	{ "2.50-2.75" },
	{ "2.75-3.00" },
	{ ">=3.00" }
};

static inline unsigned int PerfPointerHash(const void *ptr)
{
	size_t value = reinterpret_cast<size_t>(ptr);
	return static_cast<unsigned int>((value >> 4) ^ (value >> 12));
}

static inline unsigned int PerfPointerPairHash(const void *a, const void *b)
{
	return PerfPointerHash(a) ^ (PerfPointerHash(b) * 16777619u);
}

static inline bool PerfTagEquals(const char *a, const char *b)
{
	if (a == b)
		return true;
	if (a == nullptr || b == nullptr)
		return false;
	while (*a != '\0' && *b != '\0')
	{
		if (*a != *b)
			return false;
		++a;
		++b;
	}
	return *a == *b;
}

static inline bool PerfTagStartsWith(const char *tag, const char *prefix)
{
	if (tag == nullptr || prefix == nullptr)
		return false;
	while (*prefix != '\0')
	{
		if (*tag == '\0' || *tag != *prefix)
			return false;
		++tag;
		++prefix;
	}
	return true;
}

static void PerfCopyName(char *dest, int destSize, const char *src)
{
	if (dest == nullptr || destSize <= 0)
		return;
	if (src == nullptr || src[0] == '\0')
		src = "?";
	strncpy(dest, src, destSize - 1);
	dest[destSize - 1] = '\0';
}

static const char *PerfFrameStateName(PerfFrameState state)
{
	switch (state)
	{
		case PERF_FRAME_BOOT:     return "BOOT";
		case PERF_FRAME_SHELL:    return "SHELL";
		case PERF_FRAME_LOADING:  return "LOADING";
		case PERF_FRAME_PAUSED:   return "PAUSED";
		case PERF_FRAME_STALL:    return "STALL";
		case PERF_FRAME_GAMEPLAY: return "GAMEPLAY";
		default:                  return "?";
	}
}

static bool PerfIsMessageStall(double frameMs, double clientMs, double logicMs, double radarMs, double audioMs, double netMs, double msgMs)

{
	if (msgMs < kPerfMsgStallThresholdMs)
		return false;
	double nonMsgMs = clientMs + logicMs + radarMs + audioMs + netMs;
	return msgMs >= nonMsgMs || msgMs >= frameMs * 0.5;
}

static PerfFrameState PerfClassifyFrameState(double frameMs, double clientMs, double logicMs, double radarMs, double audioMs, double netMs, double msgMs)
{
	if (TheGameLogic == nullptr || !TheGameLogic->isInGame())
		return PERF_FRAME_BOOT;
	if (TheGameLogic->isLoadingMap())
		return PERF_FRAME_LOADING;
	if (TheGameLogic->isInShellGame())
		return PERF_FRAME_SHELL;
	if (TheGameLogic->isGamePaused() || TheFramePacer->isGameHalted() || TheFramePacer->isTimeFrozen())
		return PERF_FRAME_PAUSED;
	if (PerfIsMessageStall(frameMs, clientMs, logicMs, radarMs, audioMs, netMs, msgMs))
		return PERF_FRAME_STALL;
	return PERF_FRAME_GAMEPLAY;
}

static bool PerfIsGameplayContextNow()
{
	if (TheGameLogic == nullptr || !TheGameLogic->isInGame())
		return false;
	if (TheGameLogic->isLoadingMap() || TheGameLogic->isInShellGame())
		return false;
	if (TheGameLogic->isGamePaused() || TheFramePacer->isGameHalted() || TheFramePacer->isTimeFrozen())
		return false;
	return true;
}

static void PerfContextAddRange(Real value, Real &minValue, Real &maxValue, int sampleCount)
{
	if (sampleCount == 1)
	{
		minValue = value;
		maxValue = value;
	}
	else
	{
		if (value < minValue) minValue = value;
		if (value > maxValue) maxValue = value;
	}
}

static void PerfAccumulateContext(bool gameplayFrame)
{
	if (TheTacticalView == nullptr)
		return;

	g_perfContext.valid = true;
	++g_perfContext.samples;
	g_perfContext.latestFramePacerFps = TheFramePacer ? TheFramePacer->getFramesPerSecondLimit() : 0;
	g_perfContext.latestActualFps = TheFramePacer ? TheFramePacer->getActualFramesPerSecondLimit() : 0;
	g_perfContext.latestGlobalFps = TheGlobalData ? TheGlobalData->m_framesPerSecondLimit : 0;
	g_perfContext.latestFpsLimitEnabled = TheFramePacer ? (TheFramePacer->isActualFramesPerSecondLimitEnabled() ? 1 : 0) : 0;
	g_perfContext.latestGlobalUseFpsLimit = TheGlobalData ? (TheGlobalData->m_useFpsLimit ? 1 : 0) : 0;
	g_perfContext.latestViewWidth = TheTacticalView->getWidth();
	g_perfContext.latestViewHeight = TheTacticalView->getHeight();
	g_perfContext.latestZoom = TheTacticalView->getZoom();
	g_perfContext.latestHeightAboveGround = TheTacticalView->getCurrentHeightAboveGround();
	g_perfContext.latestPitch = TheTacticalView->getPitch();
	g_perfContext.latestFov = TheTacticalView->getFieldOfView();
	TheTacticalView->getPosition(&g_perfContext.latestPivot);

	g_perfContext.zoomAccum += g_perfContext.latestZoom;
	g_perfContext.heightAccum += g_perfContext.latestHeightAboveGround;
	g_perfContext.fovAccum += g_perfContext.latestFov;
	PerfContextAddRange(g_perfContext.latestZoom, g_perfContext.zoomMin, g_perfContext.zoomMax, g_perfContext.samples);
	PerfContextAddRange(g_perfContext.latestHeightAboveGround, g_perfContext.heightMin, g_perfContext.heightMax, g_perfContext.samples);

	if (gameplayFrame)
	{
		++g_perfContext.gameplaySamples;
		g_perfContext.gameplayZoomAccum += g_perfContext.latestZoom;
		g_perfContext.gameplayHeightAccum += g_perfContext.latestHeightAboveGround;
		PerfContextAddRange(g_perfContext.latestZoom, g_perfContext.gameplayZoomMin, g_perfContext.gameplayZoomMax, g_perfContext.gameplaySamples);
		PerfContextAddRange(g_perfContext.latestHeightAboveGround, g_perfContext.gameplayHeightMin, g_perfContext.gameplayHeightMax, g_perfContext.gameplaySamples);
	}
}

static int PerfFindRenderStat(const char *tag)
{
	for (int i = 0; i < g_perfRenderStatCount; ++i)
	{
		if (g_perfRenderStats[i].tag == tag || PerfTagEquals(g_perfRenderStats[i].tag, tag))
			return i;
	}
	if (g_perfRenderStatCount >= kPerfRenderStatCap)
		return -1;

	const int index = g_perfRenderStatCount++;
	memset(&g_perfRenderStats[index], 0, sizeof(g_perfRenderStats[index]));
	g_perfRenderStats[index].tag = tag;
	return index;
}

static int PerfFindNamedCost(const char *category, const char *name)
{
	if (category == nullptr || name == nullptr)
		return -1;

	const unsigned int slot = PerfPointerPairHash(category, name) & (kPerfLookupCacheSize - 1);
	PerfNamedLookupEntry &cached = g_perfNamedLookupCache[slot];
	if (cached.category == category && cached.name == name && cached.index >= 0 && cached.index < g_perfNamedCostCount)
		return cached.index;

	for (int i = 0; i < g_perfNamedCostCount; ++i)
	{
		if (PerfTagEquals(g_perfNamedCosts[i].category, category) && strcmp(g_perfNamedCosts[i].name, name) == 0)
		{
			cached.category = category;
			cached.name = name;
			cached.index = i;
			return i;
		}
	}
	if (g_perfNamedCostCount >= kPerfNamedCostCap)
		return -1;

	const int index = g_perfNamedCostCount++;
	memset(&g_perfNamedCosts[index], 0, sizeof(g_perfNamedCosts[index]));
	g_perfNamedCosts[index].category = category;
	PerfCopyName(g_perfNamedCosts[index].name, sizeof(g_perfNamedCosts[index].name), name);
	cached.category = category;
	cached.name = name;
	cached.index = index;
	return index;
}

static int PerfFindEdge(const char *parent, const char *child)
{
	if (parent == nullptr || child == nullptr)
		return -1;

	const unsigned int slot = PerfPointerPairHash(parent, child) & (kPerfLookupCacheSize - 1);
	PerfEdgeLookupEntry &cached = g_perfEdgeLookupCache[slot];
	if (cached.parent == parent && cached.child == child && cached.index >= 0 && cached.index < g_perfEdgeCount)
		return cached.index;

	for (int i = 0; i < g_perfEdgeCount; ++i)
	{
		if (PerfTagEquals(g_perfEdges[i].parent, parent) && PerfTagEquals(g_perfEdges[i].child, child))
		{
			cached.parent = parent;
			cached.child = child;
			cached.index = i;
			return i;
		}
	}
	if (g_perfEdgeCount >= kPerfEdgeCap)
		return -1;

	const int index = g_perfEdgeCount++;
	memset(&g_perfEdges[index], 0, sizeof(g_perfEdges[index]));
	g_perfEdges[index].parent = parent;
	g_perfEdges[index].child = child;
	cached.parent = parent;
	cached.child = child;
	cached.index = index;
	return index;
}

static void PerfRecordScopeEdge(const char *parent, const char *child, double ms)
{
	if (parent == nullptr || child == nullptr || ms <= 0.0)
		return;
	if (PerfTagEquals(parent, child))
		return;

	const int index = PerfFindEdge(parent, child);
	if (index < 0)
	{
		g_perfDroppedEdgeAccumMs += ms;
		g_perfDroppedEdgeFrameMs += ms;
		if (ms > g_perfDroppedEdgeMaxMs)
			g_perfDroppedEdgeMaxMs = ms;
		++g_perfDroppedEdgeCalls;
		++g_perfDroppedEdgeFrameCalls;
		return;
	}

	PerfEdgeEntry &entry = g_perfEdges[index];
	entry.accum += ms;
	entry.frameMs += ms;
	if (ms > entry.maxMs)
		entry.maxMs = ms;
	++entry.calls;
	++entry.frameCalls;
}

extern "C" void PerfRecordRenderFeatureStats(const char *tag, int highFpsActive, int processed, int rendered,
	int culled, int skipped, int budget)
{
	if (!PerfProfilingEnabled() || tag == nullptr)
		return;

	const int index = PerfFindRenderStat(tag);
	if (index < 0)
		return;

	PerfRenderStatEntry &entry = g_perfRenderStats[index];
	++entry.samples;
	if (highFpsActive)
		++entry.highFpsSamples;
	entry.latestHighFps = highFpsActive;
	entry.latestProcessed = processed;
	entry.latestRendered = rendered;
	entry.latestCulled = culled;
	entry.latestSkipped = skipped;
	entry.latestBudget = budget;
	if (processed > entry.maxProcessed) entry.maxProcessed = processed;
	if (rendered > entry.maxRendered) entry.maxRendered = rendered;
	if (culled > entry.maxCulled) entry.maxCulled = culled;
	if (skipped > entry.maxSkipped) entry.maxSkipped = skipped;
	if (budget > entry.maxBudget) entry.maxBudget = budget;
	entry.accumProcessed += processed;
	entry.accumRendered += rendered;
	entry.accumCulled += culled;
	entry.accumSkipped += skipped;
	entry.accumBudget += budget;

	if (PerfIsGameplayContextNow())
	{
		++entry.gameplaySamples;
		if (highFpsActive)
			++entry.gameplayHighFpsSamples;
		entry.gameplayProcessed += processed;
		entry.gameplayRendered += rendered;
		entry.gameplayCulled += culled;
		entry.gameplaySkipped += skipped;
		entry.gameplayBudget += budget;
	}
}

extern "C" void PerfRecordNamedCost(const char *category, const char *name, double ms)
{
	if (!PerfProfilingEnabled() || category == nullptr || name == nullptr || ms <= 0.0)
		return;

	const int index = PerfFindNamedCost(category, name);
	if (index < 0)
	{
		g_perfDroppedNamedAccumMs += ms;
		g_perfDroppedNamedFrameMs += ms;
		++g_perfDroppedNamedCalls;
		++g_perfDroppedNamedFrameCalls;
		return;
	}

	PerfNamedCostEntry &entry = g_perfNamedCosts[index];
	entry.accum += ms;
	entry.frameMs += ms;
	if (ms > entry.maxMs)
		entry.maxMs = ms;
	++entry.calls;
	++entry.frameCalls;
}

static inline double PerfMetricEpsilon(PerfMetricKind metric)
{
	return metric == PERF_METRIC_FRAME ? 0.05 : 0.01;
}

static inline double PerfEntryValue(const PerfSubEntry &entry, PerfMetricKind metric)
{
	switch (metric)
	{
		case PERF_METRIC_FRAME:
			return entry.frameMs;
		case PERF_METRIC_GAMEPLAY:
			return entry.gameplayAccum;
		case PERF_METRIC_ACCUM:
		default:
			return entry.accum;
	}
}

static inline double PerfEntryInclusiveValue(const PerfSubEntry &entry, PerfMetricKind metric)
{
	switch (metric)
	{
		case PERF_METRIC_FRAME:
			return entry.inclFrameMs;
		case PERF_METRIC_GAMEPLAY:
			return entry.inclGameplayAccum;
		case PERF_METRIC_ACCUM:
		default:
			return entry.inclAccum;
	}
}

static inline int PerfEntryInclusiveCalls(const PerfSubEntry &entry, PerfMetricKind metric)
{
	switch (metric)
	{
		case PERF_METRIC_FRAME:
			return entry.inclFrameCalls;
		case PERF_METRIC_GAMEPLAY:
			return entry.inclGameplayCalls;
		case PERF_METRIC_ACCUM:
		default:
			return entry.inclCalls;
	}
}

static bool PerfHasMeasuredTag(const char *tag, PerfMetricKind metric, double epsilon)
{
	if (tag == nullptr)
		return false;

	for (int i = 0; i < g_perfSubCount; ++i)
	{
		const PerfSubEntry &entry = g_perfSubs[i];
		if (PerfEntryValue(entry, metric) <= epsilon)
			continue;
		if (PerfTagEquals(entry.tag, tag))
			return true;
	}

	return false;
}

static bool PerfHasMeasuredPrefix(const char *prefix, const char *excludeTag, PerfMetricKind metric, double epsilon)
{
	if (prefix == nullptr)
		return false;

	for (int i = 0; i < g_perfSubCount; ++i)
	{
		const PerfSubEntry &entry = g_perfSubs[i];
		if (PerfEntryValue(entry, metric) <= epsilon)
			continue;
		if (excludeTag != nullptr && PerfTagEquals(entry.tag, excludeTag))
			continue;
		if (PerfTagStartsWith(entry.tag, prefix))
			return true;
	}

	return false;
}

static bool PerfHasDetailedChildrenForTag(const char *tag, PerfMetricKind metric)
{
	const double epsilon = PerfMetricEpsilon(metric);

	if (PerfTagEquals(tag, "Cl_DispDraw"))
	{
		return PerfHasMeasuredPrefix("Cl_Dr_", nullptr, metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_Dr_Views"))
	{
		return PerfHasMeasuredPrefix("Cl_Cu_", nullptr, metric, epsilon)
			|| PerfHasMeasuredPrefix("Cl_Fl_", nullptr, metric, epsilon)
			|| PerfHasMeasuredPrefix("Cl_Vw_", nullptr, metric, epsilon)
			|| PerfHasMeasuredPrefix("Cl_Sh_", nullptr, metric, epsilon)
			|| PerfHasMeasuredPrefix("Cl_Oc_", nullptr, metric, epsilon)
			|| PerfHasMeasuredPrefix("Cl_DX_", nullptr, metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_Dr_UpdVws"))
	{
		return PerfHasMeasuredPrefix("Cl_Vw_", nullptr, metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_Dr_End"))
	{
		return PerfHasMeasuredPrefix("Cl_End_", nullptr, metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_Dr_UI"))
	{
		return PerfHasMeasuredPrefix("Cl_UI_", nullptr, metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_End_DX8"))
	{
		return PerfHasMeasuredTag("Cl_End_EndScene", metric, epsilon)
			|| PerfHasMeasuredTag("Cl_End_Web", metric, epsilon)
			|| PerfHasMeasuredTag("Cl_End_Present", metric, epsilon)
			|| PerfHasMeasuredTag("Cl_End_Unbind", metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_Cu_Render"))
	{
		return PerfHasMeasuredPrefix("Cl_Cu_", "Cl_Cu_Render", metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_Cu_ObjRender"))
	{
		return PerfHasMeasuredPrefix("Cl_Obj_", nullptr, metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_Obj_Total"))
	{
		return PerfHasMeasuredPrefix("Cl_Obj_", "Cl_Obj_Total", metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_Cu_Terrain"))
	{
		return PerfHasMeasuredPrefix("Cl_Ter_", nullptr, metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_Fl_Mesh"))
	{
		return PerfHasMeasuredPrefix("Cl_Mesh_", nullptr, metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_Mesh_Rigid"))
	{
		return PerfHasMeasuredPrefix("Cl_Mesh_Rigid_", "Cl_Mesh_Rigid", metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_Mesh_Rigid_Draw"))
	{
		return PerfHasMeasuredPrefix("Cl_DX_", nullptr, metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_DX_Apply"))
	{
		return PerfHasMeasuredPrefix("Cl_DX_RS_", nullptr, metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_Fl_ShVol"))
	{
		return PerfHasMeasuredPrefix("Cl_Sh_", nullptr, metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_Fl_ShDecal"))
	{
		return PerfHasMeasuredPrefix("Cl_SD_", nullptr, metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_Fl_Parts"))
	{
		return PerfHasMeasuredPrefix("Cl_PT_", nullptr, metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_PT_Post"))
	{
		return PerfHasMeasuredTag("Cl_PT_Snow", metric, epsilon)
			|| PerfHasMeasuredTag("Cl_PT_Smudge", metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_PT_Smudge"))
	{
		return PerfHasMeasuredPrefix("Cl_SM_", nullptr, metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_Fl_Occl"))
	{
		return PerfHasMeasuredPrefix("Cl_Oc_", nullptr, metric, epsilon);
	}
	if (PerfTagEquals(tag, "Cl_UI_Win"))
	{
		return PerfHasMeasuredPrefix("Cl_UI_Win_", "Cl_UI_Win", metric, epsilon);
	}
	if (PerfTagEquals(tag, "Lg_AI"))
	{
		return PerfHasMeasuredPrefix("Lg_AI_", "Lg_AI", metric, epsilon)
			|| PerfHasMeasuredPrefix("Lg_ASP_", nullptr, metric, epsilon)
			|| PerfHasMeasuredPrefix("Lg_AIP_", nullptr, metric, epsilon);
	}
	if (PerfTagEquals(tag, "Lg_AI_Plr"))
	{
		return PerfHasMeasuredPrefix("Lg_ASP_", nullptr, metric, epsilon)
			|| PerfHasMeasuredPrefix("Lg_AIP_", nullptr, metric, epsilon);
	}
	if (PerfTagEquals(tag, "Lg_ASP_Base"))
	{
		return PerfHasMeasuredPrefix("Lg_AIP_", nullptr, metric, epsilon);
	}

	return false;
}

static bool PerfEntryIsLeafForMetric(const PerfSubEntry &entry, PerfMetricKind metric)
{
	if (PerfEntryValue(entry, metric) <= PerfMetricEpsilon(metric))
		return false;
	if (entry.scopedExclusive)
		return true;
	return !PerfHasDetailedChildrenForTag(entry.tag, metric);
}

static bool PerfTagHasPhasePrefix(const char *tag, const char *phasePrefix)
{
	return tag != nullptr && phasePrefix != nullptr && PerfTagStartsWith(tag, phasePrefix);
}

static double PerfSumPhaseLeafMs(const char *phasePrefix, PerfMetricKind metric)
{
	double total = 0.0;
	for (int i = 0; i < g_perfSubCount; ++i)
	{
		const PerfSubEntry &entry = g_perfSubs[i];
		if (!PerfTagHasPhasePrefix(entry.tag, phasePrefix))
			continue;
		if (!PerfEntryIsLeafForMetric(entry, metric))
			continue;
		total += PerfEntryValue(entry, metric);
	}
	return total;
}

static double PerfSumPrefixMs(const char *prefix, PerfMetricKind metric, bool leafOnly)
{
	double total = 0.0;
	for (int i = 0; i < g_perfSubCount; ++i)
	{
		const PerfSubEntry &entry = g_perfSubs[i];
		if (!PerfTagStartsWith(entry.tag, prefix))
			continue;
		if (PerfEntryValue(entry, metric) <= PerfMetricEpsilon(metric))
			continue;
		if (leafOnly && !PerfEntryIsLeafForMetric(entry, metric))
			continue;
		total += PerfEntryValue(entry, metric);
	}
	return total;
}

static int PerfZoomBinIndex(Real zoom)
{
	if (zoom < 1.50f) return 0;
	if (zoom < 2.00f) return 1;
	if (zoom < 2.25f) return 2;
	if (zoom < 2.50f) return 3;
	if (zoom < 2.75f) return 4;
	if (zoom < 3.00f) return 5;
	return 6;
}

static void PerfAccumulateZoomBin(double frameMs, double clientMs, double logicMs, double radarMs,
	double audioMs, double netMs, double msgMs)
{
	if (!g_perfContext.valid || g_perfContext.samples <= 0)
		return;

	const int index = PerfZoomBinIndex(g_perfContext.latestZoom);
	PerfZoomBin &bin = g_perfZoomBins[index];
	++bin.samples;
	bin.zoomAccum += g_perfContext.latestZoom;
	bin.heightAccum += g_perfContext.latestHeightAboveGround;
	bin.frameAccum += frameMs;
	bin.clientAccum += clientMs;
	bin.logicAccum += logicMs;
	bin.radarAccum += radarMs;
	bin.audioAccum += audioMs;
	bin.netAccum += netMs;
	bin.msgAccum += msgMs;
	bin.clientLeafAccum += PerfSumPhaseLeafMs("Cl_", PERF_METRIC_FRAME);
	bin.logicLeafAccum += PerfSumPhaseLeafMs("Lg_", PERF_METRIC_FRAME);
	if (frameMs > bin.maxFrameMs) bin.maxFrameMs = frameMs;
	if (clientMs > bin.maxClientMs) bin.maxClientMs = clientMs;
	if (logicMs > bin.maxLogicMs) bin.maxLogicMs = logicMs;
}

static void PerfSortIndicesByMetric(int *order, int count, PerfMetricKind metric, bool inclusive)
{
	for (int i = 1; i < count; ++i)
	{
		int key = order[i];
		double keyVal = inclusive ? PerfEntryInclusiveValue(g_perfSubs[key], metric) : PerfEntryValue(g_perfSubs[key], metric);
		int j = i - 1;
		while (j >= 0 && (inclusive ? PerfEntryInclusiveValue(g_perfSubs[order[j]], metric) : PerfEntryValue(g_perfSubs[order[j]], metric)) < keyVal)
		{
			order[j + 1] = order[j];
			--j;
		}
		order[j + 1] = key;
	}
}

static int PerfBuildRankedOrder(int *order, PerfMetricKind metric, bool leafOnly)
{
	const double epsilon = PerfMetricEpsilon(metric);
	int count = 0;

	for (int i = 0; i < g_perfSubCount; ++i)
	{
		const PerfSubEntry &entry = g_perfSubs[i];
		const double value = leafOnly ? PerfEntryValue(entry, metric) : PerfEntryInclusiveValue(entry, metric);
		if (value <= epsilon)
			continue;
		if (leafOnly && !PerfEntryIsLeafForMetric(entry, metric))
			continue;
		order[count++] = i;
	}

	PerfSortIndicesByMetric(order, count, metric, !leafOnly);
	return count;
}

static void PerfStoreFrameSamples(double frameMs, double clientMs, double logicMs, double msgMs)
{
	if (g_perfFrameSampleCount < kPerfFrameSampleCap)
	{
		g_perfFrameSamples[g_perfFrameSampleCount] = frameMs;
		g_perfClientSamples[g_perfFrameSampleCount] = clientMs;
		g_perfLogicSamples[g_perfFrameSampleCount] = logicMs;
		g_perfMsgSamples[g_perfFrameSampleCount] = msgMs;
		++g_perfFrameSampleCount;
	}
	else
	{
		++g_perfFrameSampleOverflow;
	}
}

static double PerfPercentile(const double *samples, int count, double percentile)
{
	if (samples == nullptr || count <= 0)
		return 0.0;
	if (count > kPerfFrameSampleCap)
		count = kPerfFrameSampleCap;

	double sorted[kPerfFrameSampleCap];
	for (int i = 0; i < count; ++i)
	{
		double value = samples[i];
		int j = i - 1;
		while (j >= 0 && sorted[j] > value)
		{
			sorted[j + 1] = sorted[j];
			--j;
		}
		sorted[j + 1] = value;
	}

	int index = (int)(percentile * (double)(count - 1) + 0.5);
	if (index < 0)
		index = 0;
	if (index >= count)
		index = count - 1;
	return sorted[index];
}

static double PerfTopLevelOtherMs(double frameMs, double clientMs, double logicMs, double radarMs, double audioMs, double netMs, double msgMs)
{
	double otherMs = frameMs - (clientMs + logicMs + radarMs + audioMs + netMs + msgMs);
	return otherMs > 0.0 ? otherMs : 0.0;
}

static const char *PerfDominantPhaseName(double clientMs, double logicMs, double radarMs, double audioMs, double netMs, double msgMs, double otherMs)
{
	const char *name = "client";
	double value = clientMs;
	if (logicMs > value) { name = "logic"; value = logicMs; }
	if (radarMs > value) { name = "radar"; value = radarMs; }
	if (audioMs > value) { name = "audio"; value = audioMs; }
	if (netMs > value) { name = "net"; value = netMs; }
	if (msgMs > value) { name = "msg"; value = msgMs; }
	if (otherMs > value) { name = "other"; }
	return name;
}

static double PerfCaptureSubSnapshot(PerfSubSnapshot *snapshot, int &snapshotCount, PerfMetricKind metric, bool leafOnly)
{
	int order[kPerfSubCap];
	int count = PerfBuildRankedOrder(order, metric, leafOnly);
	int topCount = count < kPerfWorstSubCap ? count : kPerfWorstSubCap;
	double trackedMs = 0.0;

	for (int i = 0; i < count; ++i)
	{
		const PerfSubEntry &entry = g_perfSubs[order[i]];
		const double value = leafOnly ? PerfEntryValue(entry, metric) : PerfEntryInclusiveValue(entry, metric);
		trackedMs += value;
		if (i < topCount)
		{
			snapshot[i].tag = entry.tag;
			snapshot[i].ms = value;
			snapshot[i].calls = leafOnly ?
				(metric == PERF_METRIC_FRAME ? entry.frameCalls : (metric == PERF_METRIC_GAMEPLAY ? entry.gameplayCalls : entry.calls)) :
				PerfEntryInclusiveCalls(entry, metric);
		}
	}

	snapshotCount = topCount;
	return trackedMs;
}

static void PerfCaptureNamedSnapshot(PerfNamedSnapshot *snapshot, int &snapshotCount, PerfMetricKind metric);

static void PerfCaptureFrameSnapshot(PerfFrameSnapshot &snapshot, unsigned int frame, PerfFrameState state,
	double frameMs, double clientMs, double logicMs, double radarMs, double audioMs, double netMs, double msgMs)
{
	memset(&snapshot, 0, sizeof(snapshot));
	snapshot.valid = true;
	snapshot.frame = frame;
	snapshot.state = state;
	snapshot.frameMs = frameMs;
	snapshot.clientMs = clientMs;
	snapshot.logicMs = logicMs;
	snapshot.radarMs = radarMs;
	snapshot.audioMs = audioMs;
	snapshot.netMs = netMs;
	snapshot.msgMs = msgMs;
	snapshot.otherMs = PerfTopLevelOtherMs(frameMs, clientMs, logicMs, radarMs, audioMs, netMs, msgMs);
	snapshot.leafTrackedMs = PerfCaptureSubSnapshot(snapshot.leaf, snapshot.leafCount, PERF_METRIC_FRAME, true);
	snapshot.inclTrackedMs = PerfCaptureSubSnapshot(snapshot.incl, snapshot.inclCount, PERF_METRIC_FRAME, false);
	snapshot.clientLeafMs = PerfSumPhaseLeafMs("Cl_", PERF_METRIC_FRAME);
	snapshot.logicLeafMs = PerfSumPhaseLeafMs("Lg_", PERF_METRIC_FRAME);
	PerfCaptureNamedSnapshot(snapshot.named, snapshot.namedCount, PERF_METRIC_FRAME);
}

static void PerfMaybeCaptureWorstFrame(PerfFrameSnapshot &snapshot, unsigned int frame, PerfFrameState state,
	double frameMs, double clientMs, double logicMs, double radarMs, double audioMs, double netMs, double msgMs)
{
	if (!snapshot.valid || frameMs > snapshot.frameMs)
	{
		PerfCaptureFrameSnapshot(snapshot, frame, state, frameMs, clientMs, logicMs, radarMs, audioMs, netMs, msgMs);
	}
}

static void PerfWriteSubSnapshotLine(const char *label, const PerfSubSnapshot *snapshot, int count)
{
	if (g_perfLogFile == nullptr || label == nullptr || snapshot == nullptr || count <= 0)
		return;

	fprintf(g_perfLogFile, "         %s:", label);
	for (int i = 0; i < count; ++i)
	{
		fprintf(g_perfLogFile, "  %s=%.2f/%d", snapshot[i].tag ? snapshot[i].tag : "?", snapshot[i].ms, snapshot[i].calls);
	}
	fprintf(g_perfLogFile, "\n");
}

static double PerfNamedEntryValue(const PerfNamedCostEntry &entry, PerfMetricKind metric)
{
	switch (metric)
	{
		case PERF_METRIC_FRAME:
			return entry.frameMs;
		case PERF_METRIC_GAMEPLAY:
			return entry.gameplayAccum;
		case PERF_METRIC_ACCUM:
		default:
			return entry.accum;
	}
}

static void PerfSortNamedIndices(int *order, int count, PerfMetricKind metric)
{
	for (int i = 1; i < count; ++i)
	{
		int key = order[i];
		double keyVal = PerfNamedEntryValue(g_perfNamedCosts[key], metric);
		int j = i - 1;
		while (j >= 0 && PerfNamedEntryValue(g_perfNamedCosts[order[j]], metric) < keyVal)
		{
			order[j + 1] = order[j];
			--j;
		}
		order[j + 1] = key;
	}
}

static int PerfBuildNamedOrder(int *order, PerfMetricKind metric)
{
	const double epsilon = metric == PERF_METRIC_FRAME ? 0.01 : 0.05;
	int count = 0;
	for (int i = 0; i < g_perfNamedCostCount; ++i)
	{
		if (PerfNamedEntryValue(g_perfNamedCosts[i], metric) <= epsilon)
			continue;
		order[count++] = i;
	}
	PerfSortNamedIndices(order, count, metric);
	return count;
}

static void PerfCaptureNamedSnapshot(PerfNamedSnapshot *snapshot, int &snapshotCount, PerfMetricKind metric)
{
	if (snapshot == nullptr)
	{
		snapshotCount = 0;
		return;
	}

	int order[kPerfNamedCostCap];
	const int count = PerfBuildNamedOrder(order, metric);
	if (count <= 0)
	{
		snapshotCount = 0;
		return;
	}

	const int topCount = count < kPerfNamedSnapshotCap ? count : kPerfNamedSnapshotCap;
	for (int i = 0; i < topCount; ++i)
	{
		const PerfNamedCostEntry &entry = g_perfNamedCosts[order[i]];
		snapshot[i].category = entry.category;
		snapshot[i].name = entry.name;
		snapshot[i].ms = PerfNamedEntryValue(entry, metric);
		snapshot[i].calls = metric == PERF_METRIC_FRAME ? entry.frameCalls :
			(metric == PERF_METRIC_GAMEPLAY ? entry.gameplayCalls : entry.calls);
	}
	snapshotCount = topCount;
}

static double PerfEdgeEntryValue(const PerfEdgeEntry &entry, PerfMetricKind metric)
{
	switch (metric)
	{
		case PERF_METRIC_FRAME:
			return entry.frameMs;
		case PERF_METRIC_GAMEPLAY:
			return entry.gameplayAccum;
		case PERF_METRIC_ACCUM:
		default:
			return entry.accum;
	}
}

static void PerfSortEdgeIndices(int *order, int count, PerfMetricKind metric)
{
	for (int i = 1; i < count; ++i)
	{
		int key = order[i];
		double keyVal = PerfEdgeEntryValue(g_perfEdges[key], metric);
		int j = i - 1;
		while (j >= 0 && PerfEdgeEntryValue(g_perfEdges[order[j]], metric) < keyVal)
		{
			order[j + 1] = order[j];
			--j;
		}
		order[j + 1] = key;
	}
}

static int PerfBuildEdgeOrder(int *order, PerfMetricKind metric)
{
	const double epsilon = metric == PERF_METRIC_FRAME ? 0.01 : 0.05;
	int count = 0;
	for (int i = 0; i < g_perfEdgeCount; ++i)
	{
		if (PerfEdgeEntryValue(g_perfEdges[i], metric) <= epsilon)
			continue;
		order[count++] = i;
	}
	PerfSortEdgeIndices(order, count, metric);
	return count;
}

static void PerfWriteNamedSnapshotItems(const char *label, const PerfNamedSnapshot *snapshot, int count)
{
	if (g_perfLogFile == nullptr || label == nullptr || snapshot == nullptr || count <= 0)
		return;

	fprintf(g_perfLogFile, "         %s:", label);
	for (int i = 0; i < count; ++i)
	{
		fprintf(g_perfLogFile, "  %s:%s=%.2f/%d",
			snapshot[i].category ? snapshot[i].category : "?",
			snapshot[i].name ? snapshot[i].name : "?",
			snapshot[i].ms,
			snapshot[i].calls);
	}
	fprintf(g_perfLogFile, "\n");
}

static void PerfWriteFrameSnapshot(const char *label, const PerfFrameSnapshot &snapshot)
{
	if (g_perfLogFile == nullptr || label == nullptr || !snapshot.valid)
		return;

	fprintf(g_perfLogFile,
		"         %s frame=%06u state=%s total=%.1fms  phase=%s  client=%.1f  logic=%.1f  radar=%.1f  audio=%.1f  net=%.1f  msg=%.1f  other=%.1f  leafTracked=%.1f  inclTracked=%.1f  clientLeaf=%.1f (%+.1f)  logicLeaf=%.1f (%+.1f)\n",
		label,
		snapshot.frame,
		PerfFrameStateName(snapshot.state),
		snapshot.frameMs,
		PerfDominantPhaseName(snapshot.clientMs, snapshot.logicMs, snapshot.radarMs, snapshot.audioMs, snapshot.netMs, snapshot.msgMs, snapshot.otherMs),
		snapshot.clientMs,
		snapshot.logicMs,
		snapshot.radarMs,
		snapshot.audioMs,
		snapshot.netMs,
		snapshot.msgMs,
		snapshot.otherMs,
		snapshot.leafTrackedMs,
		snapshot.inclTrackedMs,
		snapshot.clientLeafMs,
		snapshot.clientLeafMs - snapshot.clientMs,
		snapshot.logicLeafMs,
		snapshot.logicLeafMs - snapshot.logicMs);

	char topLabel[64];
	snprintf(topLabel, sizeof(topLabel), "%s-TOP", label);
	PerfWriteSubSnapshotLine(topLabel, snapshot.leaf, snapshot.leafCount);
	snprintf(topLabel, sizeof(topLabel), "%s-NAMED", label);
	PerfWriteNamedSnapshotItems(topLabel, snapshot.named, snapshot.namedCount);
	if (snapshot.inclCount > snapshot.leafCount)
	{
		snprintf(topLabel, sizeof(topLabel), "%s-TOP-INCL", label);
		PerfWriteSubSnapshotLine(topLabel, snapshot.incl, snapshot.inclCount);
	}
}

static void PerfWriteCoverageLine(const char *label, PerfMetricKind metric, double frameCount,
	double frameMs, double clientMs, double logicMs, double radarMs, double audioMs, double netMs, double msgMs)
{
	if (g_perfLogFile == nullptr || label == nullptr || frameCount <= 0.0)
		return;

	const double clientLeafMs = PerfSumPhaseLeafMs("Cl_", metric);
	const double logicLeafMs = PerfSumPhaseLeafMs("Lg_", metric);
	fprintf(g_perfLogFile,
		"         %s frameAvg=%.2f  clientLeafAvg=%.2f (%+.2f vs client)  logicLeafAvg=%.2f (%+.2f vs logic)  topLevelOtherAvg=%.2f\n",
		label,
		frameMs / frameCount,
		clientLeafMs / frameCount,
		(clientLeafMs - clientMs) / frameCount,
		logicLeafMs / frameCount,
		(logicLeafMs - logicMs) / frameCount,
		PerfTopLevelOtherMs(frameMs, clientMs, logicMs, radarMs, audioMs, netMs, msgMs) / frameCount);
}

static void PerfWriteRollupLine(const char *label, PerfMetricKind metric, double frameCount)
{
	if (g_perfLogFile == nullptr || label == nullptr || frameCount <= 0.0)
		return;

	struct RollupDef
	{
		const char *label;
		const char *prefix;
	};
	static const RollupDef defs[] =
	{
		{ "Scene", "Cl_Cu_" },
		{ "Obj", "Cl_Obj_" },
		{ "Flush", "Cl_Fl_" },
		{ "Mesh", "Cl_Mesh_" },
		{ "DX", "Cl_DX_" },
		{ "UI", "Cl_UI_" },
		{ "RadarDraw", "Cl_RD_" },
		{ "Shadow", "Cl_Sh_" },
		{ "Terrain", "Cl_Ter_" },
		{ "Particle", "Cl_PT_" },
		{ "Occl", "Cl_Oc_" },
		{ "AI", "Lg_AI_" },
		{ "Path", "Lg_AI_Path" },
		{ "Script", "Lg_Script" },
		{ "Sleepy", "Lg_Sleep" }
	};

	fprintf(g_perfLogFile, "         %s (leaf avg/frame by prefix; groups are diagnostic and can overlap hierarchically):", label);
	for (int i = 0; i < (int)(sizeof(defs) / sizeof(defs[0])); ++i)
	{
		const double total = PerfSumPrefixMs(defs[i].prefix, metric, true);
		if (total <= PerfMetricEpsilon(metric))
			continue;
		fprintf(g_perfLogFile, "  %s=%.2f", defs[i].label, total / frameCount);
	}
	fprintf(g_perfLogFile, "\n");
}

static void PerfWriteZoomBins()
{
	if (g_perfLogFile == nullptr)
		return;

	int totalSamples = 0;
	for (int i = 0; i < kPerfZoomBinCount; ++i)
		totalSamples += g_perfZoomBins[i].samples;
	if (totalSamples <= 0)
		return;

	fprintf(g_perfLogFile, "         ZOOM-BINS (clean gameplay frames by TacticalView zoom):\n");
	for (int i = 0; i < kPerfZoomBinCount; ++i)
	{
		const PerfZoomBin &bin = g_perfZoomBins[i];
		if (bin.samples <= 0)
			continue;
		const double cnt = (double)bin.samples;
		fprintf(g_perfLogFile,
			"           zoom=%s samples=%d avgZoom=%.3f avgHeight=%.1f  frame=%.2f max=%.1f  client=%.2f max=%.1f leaf=%.2f  logic=%.2f max=%.1f leaf=%.2f  radar=%.2f audio=%.2f msg=%.2f\n",
			bin.label ? bin.label : "?",
			bin.samples,
			bin.zoomAccum / cnt,
			bin.heightAccum / cnt,
			bin.frameAccum / cnt,
			bin.maxFrameMs,
			bin.clientAccum / cnt,
			bin.maxClientMs,
			bin.clientLeafAccum / cnt,
			bin.logicAccum / cnt,
			bin.maxLogicMs,
			bin.logicLeafAccum / cnt,
			bin.radarAccum / cnt,
			bin.audioAccum / cnt,
			bin.msgAccum / cnt);
	}
}

static void PerfWriteNamedBreakdown(const char *label, PerfMetricKind metric, double frameCount)
{
	if (g_perfLogFile == nullptr || label == nullptr || frameCount <= 0.0)
		return;

	int order[kPerfNamedCostCap];
	const int count = PerfBuildNamedOrder(order, metric);
	if (count <= 0)
		return;

	const int reportCount = count < 28 ? count : 28;
	fprintf(g_perfLogFile, "         %s (avg/frame | total | maxFrame | maxCall | calls/frame):\n", label);
	for (int k = 0; k < reportCount; ++k)
	{
		const PerfNamedCostEntry &e = g_perfNamedCosts[order[k]];
		const double total = PerfNamedEntryValue(e, metric);
		const int calls = metric == PERF_METRIC_GAMEPLAY ? e.gameplayCalls : e.calls;
		const double maxFrame = metric == PERF_METRIC_GAMEPLAY ? e.gameplayMaxFrameMs : e.maxFrameMs;
		fprintf(g_perfLogFile,
			"           %-8s %-56s avg=%.3f  total=%.1f  maxFrame=%.2f/%d  maxCall=%.2f  calls=%d (%.1f/frm)\n",
			e.category ? e.category : "?",
			e.name[0] ? e.name : "?",
			total / frameCount,
			total,
			maxFrame,
			e.maxFrameCalls,
			e.maxMs,
			calls,
			(double)calls / frameCount);
	}
}

static void PerfWriteEdgeBreakdown(const char *label, PerfMetricKind metric, double frameCount)
{
	if (g_perfLogFile == nullptr || label == nullptr || frameCount <= 0.0)
		return;

	int order[kPerfEdgeCap];
	const int count = PerfBuildEdgeOrder(order, metric);
	if (count <= 0)
		return;

	const int reportCount = count < 40 ? count : 40;
	fprintf(g_perfLogFile, "         %s (parent -> child avg/frame | total | maxFrame | maxCall | calls/frame):\n", label);
	for (int k = 0; k < reportCount; ++k)
	{
		const PerfEdgeEntry &e = g_perfEdges[order[k]];
		const double total = PerfEdgeEntryValue(e, metric);
		const int calls = metric == PERF_METRIC_GAMEPLAY ? e.gameplayCalls : e.calls;
		const double maxFrame = metric == PERF_METRIC_GAMEPLAY ? e.gameplayMaxFrameMs : e.maxFrameMs;
		const int maxFrameCalls = metric == PERF_METRIC_GAMEPLAY ? e.gameplayMaxFrameCalls : e.maxFrameCalls;
		fprintf(g_perfLogFile,
			"           %-18s -> %-18s avg=%.3f  total=%.1f  maxFrame=%.2f/%d  maxCall=%.2f  calls=%d (%.1f/frm)\n",
			e.parent ? e.parent : "?",
			e.child ? e.child : "?",
			total / frameCount,
			total,
			maxFrame,
			maxFrameCalls,
			e.maxMs,
			calls,
			(double)calls / frameCount);
	}
}

static void PerfWriteFrameEvent(const char *label, unsigned int frame, PerfFrameState state,
	double frameMs, double clientMs, double logicMs, double radarMs, double audioMs, double netMs, double msgMs)
{
	if (g_perfLogFile == nullptr || label == nullptr)
		return;

	PerfFrameSnapshot snapshot;
	PerfCaptureFrameSnapshot(snapshot, frame, state, frameMs, clientMs, logicMs, radarMs, audioMs, netMs, msgMs);
	fprintf(g_perfLogFile,
		"[%06u] %s state=%s  frame=%.1fms  phase=%s  client=%.1f  logic=%.1f  radar=%.1f  audio=%.1f  net=%.1f  msg=%.1f  other=%.1f  leafTracked=%.1f  clientLeaf=%.1f (%+.1f)  logicLeaf=%.1f (%+.1f)\n",
		frame,
		label,
		PerfFrameStateName(state),
		frameMs,
		PerfDominantPhaseName(clientMs, logicMs, radarMs, audioMs, netMs, msgMs, snapshot.otherMs),
		clientMs,
		logicMs,
		radarMs,
		audioMs,
		netMs,
		msgMs,
		snapshot.otherMs,
		snapshot.leafTrackedMs,
		snapshot.clientLeafMs,
		snapshot.clientLeafMs - clientMs,
		snapshot.logicLeafMs,
		snapshot.logicLeafMs - logicMs);
	PerfWriteSubSnapshotLine(label && label[0] == 'S' && label[1] == 'P' ? "SPIKE-TOP" : "SLOW-TOP", snapshot.leaf, snapshot.leafCount);
	PerfWriteNamedSnapshotItems(label && label[0] == 'S' && label[1] == 'P' ? "SPIKE-NAMED" : "SLOW-NAMED", snapshot.named, snapshot.namedCount);
	if (snapshot.inclCount > snapshot.leafCount)
	{
		PerfWriteSubSnapshotLine(label && label[0] == 'S' && label[1] == 'P' ? "SPIKE-TOP-INCL" : "SLOW-TOP-INCL", snapshot.incl, snapshot.inclCount);
	}
	if (g_perfDroppedSubFrameCalls > 0)
	{
		fprintf(g_perfLogFile,
			"         %s-DROPPED subCalls=%d subMs=%.2f cap=%d\n",
			label,
			g_perfDroppedSubFrameCalls,
			g_perfDroppedSubFrameMs,
			kPerfSubCap);
	}
	if (g_perfDroppedNamedFrameCalls > 0)
	{
		fprintf(g_perfLogFile,
			"         %s-NAMED-DROPPED calls=%d ms=%.2f cap=%d\n",
			label,
			g_perfDroppedNamedFrameCalls,
			g_perfDroppedNamedFrameMs,
			kPerfNamedCostCap);
	}
	if (g_perfDroppedEdgeFrameCalls > 0)
	{
		fprintf(g_perfLogFile,
			"         %s-EDGE-DROPPED calls=%d ms=%.2f cap=%d\n",
			label,
			g_perfDroppedEdgeFrameCalls,
			g_perfDroppedEdgeFrameMs,
			kPerfEdgeCap);
	}
	if (g_perfScopeOverflow > 0 || g_perfScopeMismatch > 0)
	{
		fprintf(g_perfLogFile,
			"         %s-SCOPE-WARN overflow=%d mismatch=%d\n",
			label,
			g_perfScopeOverflow,
			g_perfScopeMismatch);
	}
}

static void PerfFinalizeFrameSubs()
{
	for (int i = 0; i < g_perfSubCount; ++i)
	{
		PerfSubEntry &entry = g_perfSubs[i];
		if (entry.frameMs > entry.maxFrameMs)
		{
			entry.maxFrameMs = entry.frameMs;
			entry.maxFrameCalls = entry.frameCalls;
		}
		if (entry.inclFrameMs > entry.inclMaxFrameMs)
		{
			entry.inclMaxFrameMs = entry.inclFrameMs;
			entry.inclMaxFrameCalls = entry.inclFrameCalls;
		}
	}
	for (int i = 0; i < g_perfNamedCostCount; ++i)
	{
		PerfNamedCostEntry &entry = g_perfNamedCosts[i];
		if (entry.frameMs > entry.maxFrameMs)
		{
			entry.maxFrameMs = entry.frameMs;
			entry.maxFrameCalls = entry.frameCalls;
		}
	}
	for (int i = 0; i < g_perfEdgeCount; ++i)
	{
		PerfEdgeEntry &entry = g_perfEdges[i];
		if (entry.frameMs > entry.maxFrameMs)
		{
			entry.maxFrameMs = entry.frameMs;
			entry.maxFrameCalls = entry.frameCalls;
		}
	}
}

static void PerfRecordCleanGameplayFrameSubs()
{
	for (int i = 0; i < g_perfSubCount; ++i)
	{
		PerfSubEntry &entry = g_perfSubs[i];
		if (entry.frameMs <= 0.0 && entry.frameCalls == 0 && entry.inclFrameMs <= 0.0 && entry.inclFrameCalls == 0)
			continue;
		entry.gameplayAccum += entry.frameMs;
		entry.inclGameplayAccum += entry.inclFrameMs;
		if (entry.frameMs > entry.gameplayMaxFrameMs)
			entry.gameplayMaxFrameMs = entry.frameMs;
		if (entry.inclFrameMs > entry.inclGameplayMaxFrameMs)
			entry.inclGameplayMaxFrameMs = entry.inclFrameMs;
		entry.gameplayCalls += entry.frameCalls;
		entry.inclGameplayCalls += entry.inclFrameCalls;
	}
	for (int i = 0; i < g_perfNamedCostCount; ++i)
	{
		PerfNamedCostEntry &entry = g_perfNamedCosts[i];
		if (entry.frameMs <= 0.0 && entry.frameCalls == 0)
			continue;
		entry.gameplayAccum += entry.frameMs;
		if (entry.frameMs > entry.gameplayMaxFrameMs)
			entry.gameplayMaxFrameMs = entry.frameMs;
		entry.gameplayCalls += entry.frameCalls;
	}
	for (int i = 0; i < g_perfEdgeCount; ++i)
	{
		PerfEdgeEntry &entry = g_perfEdges[i];
		if (entry.frameMs <= 0.0 && entry.frameCalls == 0)
			continue;
		entry.gameplayAccum += entry.frameMs;
		if (entry.frameMs > entry.gameplayMaxFrameMs)
		{
			entry.gameplayMaxFrameMs = entry.frameMs;
			entry.gameplayMaxFrameCalls = entry.frameCalls;
		}
		entry.gameplayCalls += entry.frameCalls;
	}
}

static void PerfResetPerFrameSubs()
{
	for (int i = 0; i < g_perfSubCount; ++i)
	{
		g_perfSubs[i].frameMs = 0.0;
		g_perfSubs[i].inclFrameMs = 0.0;
		g_perfSubs[i].frameCalls = 0;
		g_perfSubs[i].inclFrameCalls = 0;
	}
	g_perfScopeDepth = 0;
	g_perfDroppedSubFrameMs = 0.0;
	g_perfDroppedSubFrameCalls = 0;
	for (int i = 0; i < g_perfNamedCostCount; ++i)
	{
		g_perfNamedCosts[i].frameMs = 0.0;
		g_perfNamedCosts[i].frameCalls = 0;
	}
	g_perfDroppedNamedFrameMs = 0.0;
	g_perfDroppedNamedFrameCalls = 0;
	for (int i = 0; i < g_perfEdgeCount; ++i)
	{
		g_perfEdges[i].frameMs = 0.0;
		g_perfEdges[i].frameCalls = 0;
	}
	g_perfDroppedEdgeFrameMs = 0.0;
	g_perfDroppedEdgeFrameCalls = 0;
}

static void PerfResetSummaryWindow()
{
	g_perfAccumFrameMs = g_perfAccumClientMs = g_perfAccumLogicMs = 0.0;
	g_perfAccumRadarMs = g_perfAccumAudioMs = g_perfAccumNetMs = g_perfAccumMsgMs = 0.0;
	g_perfMaxFrameMs = g_perfMaxClientMs = g_perfMaxLogicMs = 0.0;
	g_perfMaxRadarMs = g_perfMaxAudioMs = g_perfMaxNetMs = g_perfMaxMsgMs = 0.0;
	g_perfGameplayAccumFrameMs = g_perfGameplayAccumClientMs = g_perfGameplayAccumLogicMs = 0.0;
	g_perfGameplayAccumRadarMs = g_perfGameplayAccumAudioMs = g_perfGameplayAccumNetMs = g_perfGameplayAccumMsgMs = 0.0;
	g_perfGameplayMaxFrameMs = g_perfGameplayMaxClientMs = g_perfGameplayMaxLogicMs = 0.0;
	g_perfGameplayMaxRadarMs = g_perfGameplayMaxAudioMs = g_perfGameplayMaxNetMs = g_perfGameplayMaxMsgMs = 0.0;
	g_perfFrameCount = 0;
	g_perfGameplayFrameCount = 0;
	memset(g_perfStateFrameCounts, 0, sizeof(g_perfStateFrameCounts));
	g_perfFrameSampleCount = 0;
	g_perfFrameSampleOverflow = 0;
	memset(&g_perfWorstFrame, 0, sizeof(g_perfWorstFrame));
	memset(&g_perfGameplayWorstFrame, 0, sizeof(g_perfGameplayWorstFrame));
	memset(&g_perfContext, 0, sizeof(g_perfContext));
	g_perfDroppedSubAccumMs = 0.0;
	g_perfDroppedSubMaxMs = 0.0;
	g_perfDroppedSubFrameMs = 0.0;
	g_perfDroppedSubCalls = 0;
	g_perfDroppedSubFrameCalls = 0;
	g_perfDroppedNamedAccumMs = 0.0;
	g_perfDroppedNamedFrameMs = 0.0;
	g_perfDroppedNamedCalls = 0;
	g_perfDroppedNamedFrameCalls = 0;
	g_perfDroppedEdgeAccumMs = 0.0;
	g_perfDroppedEdgeFrameMs = 0.0;
	g_perfDroppedEdgeMaxMs = 0.0;
	g_perfDroppedEdgeCalls = 0;
	g_perfDroppedEdgeFrameCalls = 0;
	g_perfScopeDepth = 0;
	g_perfScopeOverflow = 0;
	g_perfScopeMismatch = 0;
	for (int i = 0; i < g_perfSubCount; ++i)
	{
		const char *tag = g_perfSubs[i].tag;
		const bool scopedExclusive = g_perfSubs[i].scopedExclusive;
		memset(&g_perfSubs[i], 0, sizeof(g_perfSubs[i]));
		g_perfSubs[i].tag = tag;
		g_perfSubs[i].scopedExclusive = scopedExclusive;
	}
	for (int i = 0; i < g_perfRenderStatCount; ++i)
	{
		const char *tag = g_perfRenderStats[i].tag;
		memset(&g_perfRenderStats[i], 0, sizeof(g_perfRenderStats[i]));
		g_perfRenderStats[i].tag = tag;
	}
	memset(g_perfNamedCosts, 0, sizeof(g_perfNamedCosts));
	g_perfNamedCostCount = 0;
	memset(g_perfNamedLookupCache, 0, sizeof(g_perfNamedLookupCache));
	memset(g_perfEdges, 0, sizeof(g_perfEdges));
	g_perfEdgeCount = 0;
	memset(g_perfEdgeLookupCache, 0, sizeof(g_perfEdgeLookupCache));
	for (int i = 0; i < kPerfZoomBinCount; ++i)
	{
		const char *label = g_perfZoomBins[i].label;
		memset(&g_perfZoomBins[i], 0, sizeof(g_perfZoomBins[i]));
		g_perfZoomBins[i].label = label;
	}
}

static int PerfCountActiveFrameStates()
{
	int count = 0;
	for (int i = 0; i < kPerfFrameStateCount; ++i)
	{
		if (g_perfStateFrameCounts[i] > 0)
			++count;
	}
	return count;
}

static PerfFrameState PerfFindDominantFrameState()
{
	int bestState = 0;
	for (int i = 1; i < kPerfFrameStateCount; ++i)
	{
		if (g_perfStateFrameCounts[i] > g_perfStateFrameCounts[bestState])
			bestState = i;
	}
	return static_cast<PerfFrameState>(bestState);
}

static inline void PerfRecordSubValues(const char *tag, double exclusiveMs, double inclusiveMs, bool scopedExclusive)
{
	if (tag == nullptr || (exclusiveMs <= 0.0 && inclusiveMs <= 0.0))
		return;
	if (exclusiveMs < 0.0)
		exclusiveMs = 0.0;
	if (inclusiveMs < exclusiveMs)
		inclusiveMs = exclusiveMs;

	const unsigned int slot = PerfPointerHash(tag) & (kPerfLookupCacheSize - 1);
	PerfSubLookupEntry &cached = g_perfSubLookupCache[slot];
	if (cached.tag == tag && cached.index >= 0 && cached.index < g_perfSubCount)
	{
		PerfSubEntry &entry = g_perfSubs[cached.index];
		entry.accum += exclusiveMs;
		entry.inclAccum += inclusiveMs;
		entry.frameMs += exclusiveMs;
		entry.inclFrameMs += inclusiveMs;
		if (exclusiveMs > entry.maxMs) entry.maxMs = exclusiveMs;
		if (inclusiveMs > entry.inclMaxMs) entry.inclMaxMs = inclusiveMs;
		entry.calls++;
		entry.inclCalls++;
		entry.frameCalls++;
		entry.inclFrameCalls++;
		if (scopedExclusive)
			entry.scopedExclusive = true;
		return;
	}

	for (int i = 0; i < g_perfSubCount; ++i)
	{
		if (g_perfSubs[i].tag == tag || PerfTagEquals(g_perfSubs[i].tag, tag))
		{
			PerfSubEntry &entry = g_perfSubs[i];
			entry.accum += exclusiveMs;
			entry.inclAccum += inclusiveMs;
			entry.frameMs += exclusiveMs;
			entry.inclFrameMs += inclusiveMs;
			if (exclusiveMs > entry.maxMs) entry.maxMs = exclusiveMs;
			if (inclusiveMs > entry.inclMaxMs) entry.inclMaxMs = inclusiveMs;
			entry.calls++;
			entry.inclCalls++;
			entry.frameCalls++;
			entry.inclFrameCalls++;
			if (scopedExclusive)
				entry.scopedExclusive = true;
			cached.tag = tag;
			cached.index = i;
			return;
		}
	}
	if (g_perfSubCount < kPerfSubCap)
	{
		const int index = g_perfSubCount;
		PerfSubEntry &entry = g_perfSubs[index];
		memset(&entry, 0, sizeof(entry));
		entry.tag = tag;
		entry.accum = exclusiveMs;
		entry.inclAccum = inclusiveMs;
		entry.frameMs = exclusiveMs;
		entry.inclFrameMs = inclusiveMs;
		entry.maxMs = exclusiveMs;
		entry.inclMaxMs = inclusiveMs;
		entry.calls = 1;
		entry.inclCalls = 1;
		entry.frameCalls = 1;
		entry.inclFrameCalls = 1;
		entry.scopedExclusive = scopedExclusive;
		g_perfSubCount++;
		cached.tag = tag;
		cached.index = index;
	}
	else
	{
		g_perfDroppedSubAccumMs += inclusiveMs;
		g_perfDroppedSubFrameMs += inclusiveMs;
		if (inclusiveMs > g_perfDroppedSubMaxMs)
			g_perfDroppedSubMaxMs = inclusiveMs;
		++g_perfDroppedSubCalls;
		++g_perfDroppedSubFrameCalls;
	}
}

static inline void PerfRecordSub(const char *tag, double ms)
{
	PerfRecordSubValues(tag, ms, ms, false);
}

// C-linkage helpers called from other TUs (GameLogic.cpp).
extern "C" void PerfRecordLogicSub(const char *tag, double ms)
{
	if (!PerfProfilingEnabled())
		return;

	PerfRecordSub(tag, ms);
	if (g_perfScopeDepth > 0 && ms > 0.0)
	{
		PerfRecordScopeEdge(g_perfScopeStack[g_perfScopeDepth - 1].tag, tag, ms);
		g_perfScopeStack[g_perfScopeDepth - 1].childMs += ms;
	}
}

extern "C" int PerfBeginScopedLogicSub(const char *tag)
{
	if (!PerfProfilingEnabled() || tag == nullptr)
		return -1;
	if (!g_perfFreqInit || g_perfFreq.QuadPart == 0)
		QueryPerformanceFrequency(&g_perfFreq);
	g_perfFreqInit = true;
	if (g_perfFreq.QuadPart == 0)
		return -1;
	if (g_perfScopeDepth >= kPerfScopeStackCap)
	{
		++g_perfScopeOverflow;
		return -1;
	}

	const int token = g_perfScopeDepth++;
	g_perfScopeStack[token].tag = tag;
	g_perfScopeStack[token].childMs = 0.0;
	QueryPerformanceCounter(&g_perfScopeStack[token].begin);
	return token;
}

extern "C" void PerfEndScopedLogicSub(int token)
{
	if (token < 0)
		return;
	if (token != g_perfScopeDepth - 1)
	{
		++g_perfScopeMismatch;
		return;
	}

	LARGE_INTEGER end;
	QueryPerformanceCounter(&end);
	PerfScopeStackEntry entry = g_perfScopeStack[token];
	--g_perfScopeDepth;

	const double inclusiveMs = (double)(end.QuadPart - entry.begin.QuadPart) * 1000.0 / (double)g_perfFreq.QuadPart;
	double exclusiveMs = inclusiveMs - entry.childMs;
	if (exclusiveMs < 0.0 && exclusiveMs > -0.02)
		exclusiveMs = 0.0;
	if (exclusiveMs < 0.0)
		exclusiveMs = 0.0;

	PerfRecordSubValues(entry.tag, exclusiveMs, inclusiveMs, true);
	if (g_perfScopeDepth > 0)
	{
		PerfRecordScopeEdge(g_perfScopeStack[g_perfScopeDepth - 1].tag, entry.tag, inclusiveMs);
		g_perfScopeStack[g_perfScopeDepth - 1].childMs += inclusiveMs;
	}
}

extern "C" double PerfGetFreqQuadPartAsMsMultiplier()
{
	if (!PerfProfilingEnabled())
		return 0.0;
	if (!g_perfFreqInit || g_perfFreq.QuadPart == 0) return 0.0;
	return 1000.0 / (double)g_perfFreq.QuadPart;
}

static double PerfMedianFromSortedValues(const std::vector<double>& values, size_t begin, size_t end)
{
	if (begin >= end || end > values.size())
		return 0.0;

	const size_t count = end - begin;
	const size_t mid = begin + (count / 2);
	if ((count & 1u) != 0u)
	{
		return values[mid];
	}

	return (values[mid - 1] + values[mid]) * 0.5;
}

static bool PerfComputeBenchmarkMetricsFromSamples(const std::vector<double>& sourceSamples, double& medianFps, double& low5MedianFps, int& sampleCount)
{
	std::vector<double> sortedSamples;
	sortedSamples.reserve(sourceSamples.size());
	for (double fps : sourceSamples)
	{
		if (std::isfinite(fps) && fps > 0.0)
		{
			sortedSamples.push_back(fps);
		}
	}

	if (sortedSamples.empty())
	{
		return false;
	}

	std::sort(sortedSamples.begin(), sortedSamples.end());
	medianFps = PerfMedianFromSortedValues(sortedSamples, 0, sortedSamples.size());
	const size_t lowCount = std::max<size_t>(1u, (sortedSamples.size() + 19u) / 20u);
	low5MedianFps = PerfMedianFromSortedValues(sortedSamples, 0, lowCount);
	sampleCount = static_cast<int>(sortedSamples.size());
	return true;
}

static bool PerfComputeBenchmarkMetrics(double& medianFps, double& low5MedianFps, int& sampleCount)
{
	return PerfComputeBenchmarkMetricsFromSamples(g_benchmarkFpsSamples, medianFps, low5MedianFps, sampleCount);
}

static bool PerfReadWholeFile(const char* path, std::string& content)
{
	if (path == nullptr || *path == '\0')
		return false;

	FILE* file = fopen(path, "rb");
	if (file == nullptr)
		return false;

	if (fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		return false;
	}

	const long fileSize = ftell(file);
	if (fileSize < 0)
	{
		fclose(file);
		return false;
	}

	if (fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return false;
	}

	content.resize(static_cast<size_t>(fileSize));
	if (fileSize > 0)
	{
		const size_t bytesRead = fread(&content[0], 1, content.size(), file);
		if (bytesRead != content.size())
		{
			fclose(file);
			return false;
		}
	}

	fclose(file);
	return true;
}

static bool PerfWriteWholeFile(const char* path, const std::string& content)
{
	if (path == nullptr || *path == '\0')
		return false;

	FILE* file = fopen(path, "wb");
	if (file == nullptr)
		return false;

	if (!content.empty())
	{
		const size_t bytesWritten = fwrite(content.data(), 1, content.size(), file);
		if (bytesWritten != content.size())
		{
			fclose(file);
			return false;
		}
	}

	fclose(file);
	return true;
}

static void PerfPrependBenchmarkHeader(
	double medianFps,
	double low5MedianFps,
	int sampleCount,
	double renderMedianFps,
	double renderLow5MedianFps,
	int renderSampleCount,
	double logicMedianFps,
	double logicLow5MedianFps,
	int logicSampleCount,
	double measuredSeconds)
{
	if (g_perfLogPath[0] == '\0' && !PerfBuildUniqueLogPath(g_perfLogPath, sizeof(g_perfLogPath)))
		return;

	if (g_perfLogFile != nullptr)
	{
		fflush(g_perfLogFile);
		fclose(g_perfLogFile);
		g_perfLogFile = nullptr;
	}

	std::string originalContent;
	PerfReadWholeFile(g_perfLogPath, originalContent);

	std::string rewrittenContent;
	char line[256];

	rewrittenContent += "BENCHMARK FPS SOURCE: gameplay-rate fps (logic progress)\n";
#if defined(GENERALS_ONLINE_HIGH_FPS_SERVER)
	rewrittenContent += "BENCHMARK FPS SCALE: legacy/original 30 FPS gameplay units\n";
#endif

	if (sampleCount > 0)
	{
		snprintf(line, sizeof(line),
			"MEDIAN FPS: %.2f\n"
			"LOW 5%% FPS MEDIAN: %.2f\n",
			medianFps,
			low5MedianFps);
		rewrittenContent += line;
	}

	if (renderSampleCount > 0)
	{
		snprintf(line, sizeof(line),
			"MEDIAN RENDER FPS: %.2f\n"
			"LOW 5%% RENDER FPS MEDIAN: %.2f\n",
			renderMedianFps,
			renderLow5MedianFps);
		rewrittenContent += line;
	}

	if (logicSampleCount > 0)
	{
		snprintf(line, sizeof(line),
			"MEDIAN LOGIC FPS: %.2f\n"
			"LOW 5%% LOGIC FPS MEDIAN: %.2f\n",
			logicMedianFps,
			logicLow5MedianFps);
		rewrittenContent += line;
	}

	snprintf(line, sizeof(line),
		"BENCHMARK SAMPLES: %d\n"
		"BENCHMARK RENDER SAMPLES: %d\n"
		"BENCHMARK LOGIC SAMPLES: %d\n"
		"BENCHMARK DURATION: %.2fs\n"
		"BENCHMARK SAVE: %s\n\n",
		sampleCount,
		renderSampleCount,
		logicSampleCount,
		measuredSeconds,
		TheGlobalData != nullptr && TheGlobalData->m_benchmarkSaveFile.isEmpty() == FALSE ? TheGlobalData->m_benchmarkSaveFile.str() : "<unknown>");
	rewrittenContent += line;

	if (!originalContent.empty())
	{
		rewrittenContent += originalContent;
	}
	PerfWriteWholeFile(g_perfLogPath, rewrittenContent);
}

static void PerfWriteSummaryOnlySessionLog(
	double medianFps,
	double low5MedianFps,
	int sampleCount,
	double renderMedianFps,
	double renderLow5MedianFps,
	int renderSampleCount,
	double logicMedianFps,
	double logicLow5MedianFps,
	int logicSampleCount,
	double measuredSeconds,
	bool finalized)
{
	if (g_perfLogPath[0] == '\0' && !PerfBuildUniqueLogPath(g_perfLogPath, sizeof(g_perfLogPath)))
		return;

	std::string content;
	char line[256];

	content += "FPS SOURCE: gameplay-rate fps (logic progress)\n";
#if defined(GENERALS_ONLINE_HIGH_FPS_SERVER)
	content += "FPS SCALE: legacy/original 30 FPS gameplay units\n";
#endif

	const bool hasAnySamples = sampleCount > 0 || renderSampleCount > 0 || logicSampleCount > 0;
	if (!hasAnySamples && !finalized)
	{
		content += "DETAIL PROFILING: OFF\n";
		content += "STATUS: waiting for first valid sample...\n";
		PerfWriteWholeFile(g_perfLogPath, content);
		return;
	}

	if (sampleCount > 0)
	{
		snprintf(line, sizeof(line),
			"MEDIAN FPS: %.2f\n"
			"LOW 5%% FPS MEDIAN: %.2f\n",
			medianFps,
			low5MedianFps);
		content += line;
	}

	if (renderSampleCount > 0)
	{
		snprintf(line, sizeof(line),
			"MEDIAN RENDER FPS: %.2f\n"
			"LOW 5%% RENDER FPS MEDIAN: %.2f\n",
			renderMedianFps,
			renderLow5MedianFps);
		content += line;
	}

	if (logicSampleCount > 0)
	{
		snprintf(line, sizeof(line),
			"MEDIAN LOGIC FPS: %.2f\n"
			"LOW 5%% LOGIC FPS MEDIAN: %.2f\n",
			logicMedianFps,
			logicLow5MedianFps);
		content += line;
	}

	snprintf(line, sizeof(line),
		"SESSION SAMPLES: %d\n"
		"SESSION RENDER SAMPLES: %d\n"
		"SESSION LOGIC SAMPLES: %d\n"
		"SESSION DURATION: %.2fs\n"
		"DETAIL PROFILING: OFF\n",
		sampleCount,
		renderSampleCount,
		logicSampleCount,
		measuredSeconds);
	content += line;
	if (finalized)
	{
		content += "SESSION STATUS: final summary.\n";
	}

	PerfWriteWholeFile(g_perfLogPath, content);
}

static void PerfRefreshSummaryOnlySessionLog(DWORD nowMs, bool forceWrite, bool finalized)
{
	if (!g_summarySessionInitialized)
		return;
	if (!forceWrite && g_summaryLastWriteMs > 0u && nowMs - g_summaryLastWriteMs < kSummaryLogWriteIntervalMs)
		return;

	double medianFps = 0.0;
	double low5MedianFps = 0.0;
	int sampleCount = 0;
	double renderMedianFps = 0.0;
	double renderLow5MedianFps = 0.0;
	int renderSampleCount = 0;
	double logicMedianFps = 0.0;
	double logicLow5MedianFps = 0.0;
	int logicSampleCount = 0;
	const double measuredSeconds = g_summarySessionStartMs > 0u
		? (double)(nowMs - g_summarySessionStartMs) / 1000.0
		: 0.0;

	PerfComputeBenchmarkMetricsFromSamples(g_summaryFpsSamples, medianFps, low5MedianFps, sampleCount);
	PerfComputeBenchmarkMetricsFromSamples(g_summaryRenderFpsSamples, renderMedianFps, renderLow5MedianFps, renderSampleCount);
	PerfComputeBenchmarkMetricsFromSamples(g_summaryLogicFpsSamples, logicMedianFps, logicLow5MedianFps, logicSampleCount);
	PerfWriteSummaryOnlySessionLog(
		medianFps,
		low5MedianFps,
		sampleCount,
		renderMedianFps,
		renderLow5MedianFps,
		renderSampleCount,
		logicMedianFps,
		logicLow5MedianFps,
		logicSampleCount,
		measuredSeconds,
		finalized);
	g_summaryLastWriteMs = nowMs;
}

static void PerfSummaryLogInit()
{
	if (!PerfSummaryOnlyLoggingEnabled() || g_perfLogOpened)
		return;
	if (!PerfBuildUniqueLogPath(g_perfLogPath, sizeof(g_perfLogPath)))
		return;

	g_perfLogOpened = true;
	PerfWriteSummaryOnlySessionLog(0.0, 0.0, 0, 0.0, 0.0, 0, 0.0, 0.0, 0, 0.0, false);
}

static void PerfRecordSummaryOnlySample(DWORD nowMs)
{
	if (!PerfSummaryOnlyLoggingEnabled())
		return;

	PerfSummaryLogInit();
	++g_summaryFrameCounter;
	if (!g_summarySessionInitialized)
	{
		g_summaryFpsSamples.clear();
		g_summaryRenderFpsSamples.clear();
		g_summaryLogicFpsSamples.clear();
		g_summarySessionInitialized = true;
		g_summarySessionStartMs = nowMs;
		g_summaryLastSampleMs = 0u;
		g_summaryLastLogicSampleFrame = 0u;
		g_summaryFrameCounter = 1u;
		g_summaryLastFrameSampleCount = 0u;
		g_summaryLastWriteMs = 0u;
	}

	if (TheGameLogic == nullptr)
		return;

	const UnsignedInt currentLogicFrame = TheGameLogic->getFrame();
	if (g_summaryLastSampleMs == 0u)
	{
		g_summaryLastSampleMs = nowMs;
		g_summaryLastLogicSampleFrame = currentLogicFrame;
		g_summaryLastFrameSampleCount = g_summaryFrameCounter;
		return;
	}

	if (nowMs - g_summaryLastSampleMs < kBenchmarkSampleIntervalMs)
		return;

	const DWORD sampleDeltaMs = nowMs - g_summaryLastSampleMs;
	const UnsignedInt sampleDeltaFrames = currentLogicFrame - g_summaryLastLogicSampleFrame;
	const UnsignedInt sampleDeltaRenderFrames = g_summaryFrameCounter - g_summaryLastFrameSampleCount;
	const double renderFps = sampleDeltaMs > 0u
		? (double)sampleDeltaRenderFrames * 1000.0 / (double)sampleDeltaMs
		: 0.0;
	const double logicFps = sampleDeltaMs > 0u
		? (double)sampleDeltaFrames * 1000.0 / (double)sampleDeltaMs
		: 0.0;

	if (std::isfinite(logicFps) && logicFps > 0.0)
	{
		g_summaryLogicFpsSamples.push_back(logicFps);

		double gameplayFps = logicFps;
#if defined(GENERALS_ONLINE_HIGH_FPS_SERVER)
		gameplayFps /= (double)GENERALS_ONLINE_HIGH_FPS_FRAME_MULTIPLIER;
#endif
		if (std::isfinite(gameplayFps) && gameplayFps > 0.0)
		{
			g_summaryFpsSamples.push_back(gameplayFps);
		}
	}

	if (std::isfinite(renderFps) && renderFps > 0.0)
	{
		g_summaryRenderFpsSamples.push_back(renderFps);
	}

	g_summaryLastSampleMs = nowMs;
	g_summaryLastLogicSampleFrame = currentLogicFrame;
	g_summaryLastFrameSampleCount = g_summaryFrameCounter;
	PerfRefreshSummaryOnlySessionLog(nowMs, false, false);
}

static void PerfFinalizeSummaryOnlySession()
{
	if (!g_summarySessionInitialized)
		return;
	const DWORD nowMs = timeGetTime();
	PerfRefreshSummaryOnlySessionLog(nowMs, true, true);
}

static void PerfFinalizeBenchmarkRun()
{
	if (TheGlobalData == nullptr || !TheGlobalData->m_benchmarkSamplingActive)
		return;

	const DWORD nowMs = timeGetTime();
	const double measuredSeconds = TheGlobalData->m_benchmarkSamplingStartMs > 0
		? (double)(nowMs - TheGlobalData->m_benchmarkSamplingStartMs) / 1000.0
		: 0.0;

	TheWritableGlobalData->m_benchmarkSamplingActive = FALSE;
	TheWritableGlobalData->m_benchmarkWaitingForSettle = FALSE;
	TheWritableGlobalData->m_benchmarkStartupPending = FALSE;
	TheWritableGlobalData->m_benchmarkSettleStartMs = 0;

	double medianFps = 0.0;
	double low5MedianFps = 0.0;
	int sampleCount = 0;
	double renderMedianFps = 0.0;
	double renderLow5MedianFps = 0.0;
	int renderSampleCount = 0;
	double logicMedianFps = 0.0;
	double logicLow5MedianFps = 0.0;
	int logicSampleCount = 0;

	const bool hasBenchmarkMetrics = PerfComputeBenchmarkMetrics(medianFps, low5MedianFps, sampleCount);
	const bool hasRenderMetrics = PerfComputeBenchmarkMetricsFromSamples(g_benchmarkRenderFpsSamples, renderMedianFps, renderLow5MedianFps, renderSampleCount);
	const bool hasLogicMetrics = PerfComputeBenchmarkMetricsFromSamples(g_benchmarkLogicFpsSamples, logicMedianFps, logicLow5MedianFps, logicSampleCount);
	if (hasBenchmarkMetrics || hasRenderMetrics || hasLogicMetrics)
	{
		PerfPrependBenchmarkHeader(
			medianFps,
			low5MedianFps,
			sampleCount,
			renderMedianFps,
			renderLow5MedianFps,
			renderSampleCount,
			logicMedianFps,
			logicLow5MedianFps,
			logicSampleCount,
			measuredSeconds);
	}
	else
	{
		DEBUG_LOG(("Benchmark mode did not capture any valid FPS samples."));
	}

	TheGameEngine->setQuitting(TRUE);
}

static void PerfLogInit()
{
	if (!PerfProfilingEnabled())
		return;

	if (g_perfLogOpened) return;
	g_perfLogOpened = true;

	QueryPerformanceFrequency(&g_perfFreq);
	g_perfFreqInit = true;

	time_t now = time(nullptr);
	struct tm *t = localtime(&now);
	if (t == nullptr || !PerfBuildUniqueLogPath(g_perfLogPath, sizeof(g_perfLogPath)))
		return;

	g_perfLogFile = fopen(g_perfLogPath, "w");
	if (g_perfLogFile)
	{
		char startBuf[64];
		strftime(startBuf, sizeof(startBuf), "%Y-%m-%d %H:%M:%S", t);
		fprintf(g_perfLogFile, "=== Performance Log - %s ===\n", startBuf);
		fprintf(g_perfLogFile, "=== Build: Zero Hour (Release) ===\n");
		fprintf(g_perfLogFile, "Per-subsystem frame times in milliseconds.\n");
		fprintf(g_perfLogFile, "Slow frames (>25ms) and spikes (>33ms) are logged individually with top leaf buckets.\n");
		fprintf(g_perfLogFile, "Scoped timers report exclusive self time in TOP/SUB-BREAKDOWN and inclusive parent time in *-INCL.\n");
		fprintf(g_perfLogFile, "clientLeaf/logicLeaf show exclusive leaf coverage against top-level phase time; large deltas mean instrumentation still needs tighter children.\n");
		fprintf(g_perfLogFile, "NAMED lines aggregate per-object render cost by template/model so many-small-unit costs are visible.\n");
		fprintf(g_perfLogFile, "SUMMARY state marks BOOT/SHELL/LOADING/PAUSED/STALL/GAMEPLAY windows; mixed windows also emit clean GAMEPLAY-only summaries.\n");
		fprintf(g_perfLogFile, "ZOOM-BINS groups clean gameplay costs by TacticalView zoom so zoom-dependent work is visible.\n");
		fprintf(g_perfLogFile, "Rolling averages, p50/p95/p99, and worst-frame attribution are logged every ~5 seconds.\n\n");
		fprintf(g_perfLogFile, "Notable A* pathfinds are logged as A_STAR lines when capped, failed, slow, or large.\n\n");
		fflush(g_perfLogFile);
	}
	g_perfLastSummaryMs = timeGetTime();
}

extern "C" void PerfLogPathfindEvent(const char *kind, unsigned int frame, int playerIndex, int objectID,
	const char *templateName, double fromX, double fromY, double toX, double toY,
	int cells, int pathNodes, int capped, int partial, int success, double ms)
{
	if (!PerfProfilingEnabled())
		return;

	PerfLogInit();
	if (g_perfLogFile == nullptr)
		return;

	fprintf(g_perfLogFile,
		"[%06u] A_STAR kind=%s result=%s%s%s player=%d obj=%d tmpl='%s' from=(%.0f,%.0f) to=(%.0f,%.0f) cells=%d nodes=%d ms=%.2f\n",
		frame,
		kind ? kind : "?",
		success ? "ok" : "fail",
		capped ? "+cap" : "",
		partial ? "+partial" : "",
		playerIndex,
		objectID,
		templateName ? templateName : "?",
		fromX, fromY, toX, toY,
		cells, pathNodes, ms);
	if (capped || !success || ms >= 10.0)
		fflush(g_perfLogFile);
}

static inline double PerfTicksToMs(LONGLONG delta)
{
	if (!g_perfFreqInit || g_perfFreq.QuadPart == 0) return 0.0;
	return (double)delta * 1000.0 / (double)g_perfFreq.QuadPart;
}

/// -----------------------------------------------------------------------------------------------
DECLARE_PERF_TIMER(GameEngine_update)

/** -----------------------------------------------------------------------------------------------
 * Update the game engine by updating the GameClient and GameLogic singletons.
 */
void GameEngine::update()
{
	USE_PERF_TIMER(GameEngine_update)
	{
		const bool perfProfilingEnabled = PerfProfilingEnabled();
		const bool perfSummaryOnlyEnabled = PerfSummaryOnlyLoggingEnabled();
		if (perfProfilingEnabled)
		{
			PerfLogInit();
		}
		else if (perfSummaryOnlyEnabled)
		{
			PerfSummaryLogInit();
		}

		LARGE_INTEGER qpcFrameStart = {}, qpcT0 = {}, qpcT1 = {};
		if (perfProfilingEnabled)
		{
			QueryPerformanceCounter(&qpcFrameStart);
		}
		double tRadarMs  = 0.0;
		double tAudioMs  = 0.0;
		double tClientMs = 0.0;
		double tMsgMs    = 0.0;
		double tNetMs    = 0.0;
		double tLogicMs  = 0.0;

#define PERF_TIME_ACCUM_BLOCK(accum, code) \
		do { \
			if (perfProfilingEnabled) { \
				QueryPerformanceCounter(&qpcT0); \
				code \
				QueryPerformanceCounter(&qpcT1); \
				accum += PerfTicksToMs(qpcT1.QuadPart - qpcT0.QuadPart); \
			} else { \
				code \
			} \
		} while (false)

#define PERF_TIME_BLOCK(accum, code) \
		do { \
			if (perfProfilingEnabled) { \
				QueryPerformanceCounter(&qpcT0); \
				code \
				QueryPerformanceCounter(&qpcT1); \
				accum = PerfTicksToMs(qpcT1.QuadPart - qpcT0.QuadPart); \
			} else { \
				code \
			} \
		} while (false)

		{
			// VERIFY CRC needs to be in this code block.  Please to not pull TheGameLogic->update() inside this block.
			VERIFY_CRC

#if defined(GENERALS_ONLINE_HIGH_FPS_RENDER)
			// TheSuperHackers @mod Apply the configured FPS value everywhere, including menus and shellmap.
			// Preserve the prior menu/shellmap limiter behavior; only suppress redundant setter calls so
			// DebugLogFile does not get flooded every frame with the same "set max fps" message.
			const int fpsLimit = NGMP_OnlineServicesManager::Settings.Graphics_GetFPSValue();
			if (TheFramePacer->getFramesPerSecondLimit() != fpsLimit)
			{
				TheFramePacer->setFramesPerSecondLimit(fpsLimit);
			}
			if (!TheGlobalData->m_useFpsLimit)
			{
				TheWritableGlobalData->m_useFpsLimit = true;
			}
#endif
			
			PERF_TIME_BLOCK(tRadarMs, TheRadar->UPDATE(););

			/// @todo Move audio init, update, etc, into GameClient update

			PERF_TIME_BLOCK(tAudioMs, TheAudio->UPDATE(););

			PERF_TIME_BLOCK(tClientMs, TheGameClient->UPDATE(););

			PERF_TIME_BLOCK(tMsgMs, TheMessageStream->propagateMessages(););

			if (g_bTearDownGeneralsOnlineRequested) // delayed tear down
			{
				g_bTearDownGeneralsOnlineRequested = false;

				NGMP_OnlineServicesManager::DestroyInstance();

			}
			

			if (TheNetwork != nullptr)
			{
				PERF_TIME_ACCUM_BLOCK(tNetMs, TheNetwork->UPDATE(););
			}

			if (NGMP_OnlineServicesManager::GetInstance() != nullptr)
			{
				PERF_TIME_ACCUM_BLOCK(tNetMs, NGMP_OnlineServicesManager::GetInstance()->Tick(););
			}
		}

		const Bool canUpdate = canUpdateGameLogic();
		const Bool canUpdateLogic = canUpdate && !TheFramePacer->isGameHalted() && !TheFramePacer->isTimeFrozen();
		const Bool canUpdateScript = canUpdate && !TheFramePacer->isGameHalted();

		if (canUpdateLogic)
		{
			PERF_TIME_BLOCK(tLogicMs, TheGameClient->step(); TheGameLogic->UPDATE(););
		}
		else if (canUpdateScript)
		{
			// TheSuperHackers @info Still update the Script Engine to allow
			// for scripted camera movements while the time is frozen.
			PERF_TIME_BLOCK(tLogicMs, TheScriptEngine->UPDATE(););
		}

	#undef PERF_TIME_ACCUM_BLOCK
	#undef PERF_TIME_BLOCK

		DWORD nowMs = timeGetTime();
		if (perfProfilingEnabled)
		{

			// -------- Per-frame perf logging --------
			LARGE_INTEGER qpcFrameEnd;
			QueryPerformanceCounter(&qpcFrameEnd);
			double tFrameMs = PerfTicksToMs(qpcFrameEnd.QuadPart - qpcFrameStart.QuadPart);
			const PerfFrameState frameState = PerfClassifyFrameState(tFrameMs, tClientMs, tLogicMs, tRadarMs, tAudioMs, tNetMs, tMsgMs);
			const bool isCleanGameplayFrame = frameState == PERF_FRAME_GAMEPLAY;
			PerfAccumulateContext(isCleanGameplayFrame);

			g_perfAccumFrameMs  += tFrameMs;
			g_perfAccumClientMs += tClientMs;
			g_perfAccumLogicMs  += tLogicMs;
			g_perfAccumRadarMs  += tRadarMs;
			g_perfAccumAudioMs  += tAudioMs;
			g_perfAccumNetMs    += tNetMs;
			g_perfAccumMsgMs    += tMsgMs;
			if (tFrameMs  > g_perfMaxFrameMs)  g_perfMaxFrameMs  = tFrameMs;
			if (tClientMs > g_perfMaxClientMs) g_perfMaxClientMs = tClientMs;
			if (tLogicMs  > g_perfMaxLogicMs)  g_perfMaxLogicMs  = tLogicMs;
			if (tRadarMs  > g_perfMaxRadarMs)  g_perfMaxRadarMs  = tRadarMs;
			if (tAudioMs  > g_perfMaxAudioMs)  g_perfMaxAudioMs  = tAudioMs;
			if (tNetMs    > g_perfMaxNetMs)    g_perfMaxNetMs    = tNetMs;
			if (tMsgMs    > g_perfMaxMsgMs)    g_perfMaxMsgMs    = tMsgMs;
			g_perfStateFrameCounts[frameState]++;
			PerfStoreFrameSamples(tFrameMs, tClientMs, tLogicMs, tMsgMs);
			if (isCleanGameplayFrame)
			{
				g_perfGameplayAccumFrameMs  += tFrameMs;
				g_perfGameplayAccumClientMs += tClientMs;
				g_perfGameplayAccumLogicMs  += tLogicMs;
				g_perfGameplayAccumRadarMs  += tRadarMs;
				g_perfGameplayAccumAudioMs  += tAudioMs;
				g_perfGameplayAccumNetMs    += tNetMs;
				g_perfGameplayAccumMsgMs    += tMsgMs;
				if (tFrameMs  > g_perfGameplayMaxFrameMs)  g_perfGameplayMaxFrameMs  = tFrameMs;
				if (tClientMs > g_perfGameplayMaxClientMs) g_perfGameplayMaxClientMs = tClientMs;
				if (tLogicMs  > g_perfGameplayMaxLogicMs)  g_perfGameplayMaxLogicMs  = tLogicMs;
				if (tRadarMs  > g_perfGameplayMaxRadarMs)  g_perfGameplayMaxRadarMs  = tRadarMs;
				if (tAudioMs  > g_perfGameplayMaxAudioMs)  g_perfGameplayMaxAudioMs  = tAudioMs;
				if (tNetMs    > g_perfGameplayMaxNetMs)    g_perfGameplayMaxNetMs    = tNetMs;
				if (tMsgMs    > g_perfGameplayMaxMsgMs)    g_perfGameplayMaxMsgMs    = tMsgMs;
				++g_perfGameplayFrameCount;
				PerfRecordCleanGameplayFrameSubs();
				PerfAccumulateZoomBin(tFrameMs, tClientMs, tLogicMs, tRadarMs, tAudioMs, tNetMs, tMsgMs);
			}
			PerfFinalizeFrameSubs();
			PerfMaybeCaptureWorstFrame(g_perfWorstFrame, TheGameLogic ? (unsigned int)TheGameLogic->getFrame() : 0,
				frameState, tFrameMs, tClientMs, tLogicMs, tRadarMs, tAudioMs, tNetMs, tMsgMs);
			if (isCleanGameplayFrame)
			{
				PerfMaybeCaptureWorstFrame(g_perfGameplayWorstFrame, TheGameLogic ? (unsigned int)TheGameLogic->getFrame() : 0,
					frameState, tFrameMs, tClientMs, tLogicMs, tRadarMs, tAudioMs, tNetMs, tMsgMs);
			}
			++g_perfFrameCount;

			// Immediate log on hitches. 25-33 ms frames are not hard spikes, but they are
			// visible enough that summary-only attribution makes them hard to chase.
			if (g_perfLogFile && tFrameMs > kPerfSlowFrameThresholdMs)
			{
				unsigned int frame = TheGameLogic ? (unsigned int)TheGameLogic->getFrame() : 0;
				PerfWriteFrameEvent(tFrameMs > kPerfSpikeThresholdMs ? "SPIKE" : "SLOW",
					frame, frameState, tFrameMs, tClientMs, tLogicMs, tRadarMs, tAudioMs, tNetMs, tMsgMs);
			}

			// Reset per-frame sub counters for every frame (spike or not) so the next
			// frame's SPIKE-TOP attribution only reflects that single frame.
			PerfResetPerFrameSubs();

			// Summary every ~5 seconds of wall-clock time.
			if (g_perfLogFile && g_perfFrameCount > 0 && nowMs - g_perfLastSummaryMs >= 5000)
			{
			double cnt = (double)g_perfFrameCount;
			double avgFrame = g_perfAccumFrameMs / cnt;
			unsigned int frame = TheGameLogic ? (unsigned int)TheGameLogic->getFrame() : 0;
			const PerfFrameState dominantState = PerfFindDominantFrameState();
			const bool mixedStateWindow = PerfCountActiveFrameStates() > 1;
			const char *windowStateName = mixedStateWindow ? "MIXED" : PerfFrameStateName(dominantState);
			const bool emitGameplaySummary = g_perfGameplayFrameCount > 0 && (mixedStateWindow || dominantState != PERF_FRAME_GAMEPLAY);
			fprintf(g_perfLogFile,
				"[%06u] SUMMARY state=%s  frames=%d  avg=%.2fms (%.0f FPS)  max=%.1fms  |  avgClient=%.2f (max %.1f)  avgLogic=%.2f (max %.1f)  avgRadar=%.2f (max %.1f)  avgAudio=%.2f (max %.1f)  avgNet=%.2f (max %.1f)  avgMsg=%.2f (max %.1f)\n",
				frame, windowStateName, g_perfFrameCount, avgFrame,
				avgFrame > 0.001 ? 1000.0 / avgFrame : 0.0,
				g_perfMaxFrameMs,
				g_perfAccumClientMs / cnt, g_perfMaxClientMs,
				g_perfAccumLogicMs  / cnt, g_perfMaxLogicMs,
				g_perfAccumRadarMs  / cnt, g_perfMaxRadarMs,
				g_perfAccumAudioMs  / cnt, g_perfMaxAudioMs,
				g_perfAccumNetMs    / cnt, g_perfMaxNetMs,
				g_perfAccumMsgMs    / cnt, g_perfMaxMsgMs);
			if (g_perfFrameSampleCount > 0)
			{
				fprintf(g_perfLogFile,
					"         PERCENTILES frame p50=%.2f p95=%.2f p99=%.2f  |  client p95=%.2f  logic p95=%.2f  msg p95=%.2f  samples=%d%s\n",
					PerfPercentile(g_perfFrameSamples, g_perfFrameSampleCount, 0.50),
					PerfPercentile(g_perfFrameSamples, g_perfFrameSampleCount, 0.95),
					PerfPercentile(g_perfFrameSamples, g_perfFrameSampleCount, 0.99),
					PerfPercentile(g_perfClientSamples, g_perfFrameSampleCount, 0.95),
					PerfPercentile(g_perfLogicSamples, g_perfFrameSampleCount, 0.95),
					PerfPercentile(g_perfMsgSamples, g_perfFrameSampleCount, 0.95),
					g_perfFrameSampleCount,
					g_perfFrameSampleOverflow > 0 ? " (sample cap reached)" : "");
			}
			PerfWriteCoverageLine("COVERAGE", PERF_METRIC_ACCUM, cnt,
				g_perfAccumFrameMs, g_perfAccumClientMs, g_perfAccumLogicMs, g_perfAccumRadarMs,
				g_perfAccumAudioMs, g_perfAccumNetMs, g_perfAccumMsgMs);
			PerfWriteRollupLine("ROLLUP", PERF_METRIC_ACCUM, cnt);
			if (g_perfContext.valid && g_perfContext.samples > 0)
			{
				fprintf(g_perfLogFile,
					"         CONTEXT fpsTarget=%d actualLimit=%d fpsLimitEnabled=%d globalFps=%d globalUseLimit=%d view=%dx%d  zoom avg=%.3f min=%.3f max=%.3f last=%.3f  height avg=%.1f min=%.1f max=%.1f last=%.1f  pitch=%.2f fov=%.2f pivot=(%.1f,%.1f,%.1f)\n",
					g_perfContext.latestFramePacerFps,
					g_perfContext.latestActualFps,
					g_perfContext.latestFpsLimitEnabled,
					g_perfContext.latestGlobalFps,
					g_perfContext.latestGlobalUseFpsLimit,
					g_perfContext.latestViewWidth,
					g_perfContext.latestViewHeight,
					g_perfContext.zoomAccum / (double)g_perfContext.samples,
					g_perfContext.zoomMin,
					g_perfContext.zoomMax,
					g_perfContext.latestZoom,
					g_perfContext.heightAccum / (double)g_perfContext.samples,
					g_perfContext.heightMin,
					g_perfContext.heightMax,
					g_perfContext.latestHeightAboveGround,
					RAD_TO_DEGF(g_perfContext.latestPitch),
					RAD_TO_DEGF(g_perfContext.latestFov),
					g_perfContext.latestPivot.x,
					g_perfContext.latestPivot.y,
					g_perfContext.latestPivot.z);
				if (emitGameplaySummary && g_perfContext.gameplaySamples > 0)
				{
					fprintf(g_perfLogFile,
						"         GAMEPLAY-CONTEXT samples=%d  zoom avg=%.3f min=%.3f max=%.3f  height avg=%.1f min=%.1f max=%.1f\n",
						g_perfContext.gameplaySamples,
						g_perfContext.gameplayZoomAccum / (double)g_perfContext.gameplaySamples,
						g_perfContext.gameplayZoomMin,
						g_perfContext.gameplayZoomMax,
						g_perfContext.gameplayHeightAccum / (double)g_perfContext.gameplaySamples,
						g_perfContext.gameplayHeightMin,
						g_perfContext.gameplayHeightMax);
				}
			}
			PerfWriteFrameSnapshot("WORST", g_perfWorstFrame);
			if (mixedStateWindow || dominantState != PERF_FRAME_GAMEPLAY)
			{
				fprintf(g_perfLogFile,
					"         WINDOW-STATE boot=%d  shell=%d  loading=%d  paused=%d  stall=%d  cleanGameplay=%d\n",
					g_perfStateFrameCounts[PERF_FRAME_BOOT],
					g_perfStateFrameCounts[PERF_FRAME_SHELL],
					g_perfStateFrameCounts[PERF_FRAME_LOADING],
					g_perfStateFrameCounts[PERF_FRAME_PAUSED],
					g_perfStateFrameCounts[PERF_FRAME_STALL],
					g_perfGameplayFrameCount);
			}
			if (emitGameplaySummary)
			{
				double gameplayCnt = (double)g_perfGameplayFrameCount;
				double gameplayAvgFrame = g_perfGameplayAccumFrameMs / gameplayCnt;
				fprintf(g_perfLogFile,
					"         GAMEPLAY-SUMMARY cleanFrames=%d  avg=%.2fms (%.0f FPS)  max=%.1fms  |  avgClient=%.2f (max %.1f)  avgLogic=%.2f (max %.1f)  avgRadar=%.2f (max %.1f)  avgAudio=%.2f (max %.1f)  avgNet=%.2f (max %.1f)  avgMsg=%.2f (max %.1f)\n",
					g_perfGameplayFrameCount,
					gameplayAvgFrame,
					gameplayAvgFrame > 0.001 ? 1000.0 / gameplayAvgFrame : 0.0,
					g_perfGameplayMaxFrameMs,
					g_perfGameplayAccumClientMs / gameplayCnt, g_perfGameplayMaxClientMs,
					g_perfGameplayAccumLogicMs  / gameplayCnt, g_perfGameplayMaxLogicMs,
					g_perfGameplayAccumRadarMs  / gameplayCnt, g_perfGameplayMaxRadarMs,
					g_perfGameplayAccumAudioMs  / gameplayCnt, g_perfGameplayMaxAudioMs,
					g_perfGameplayAccumNetMs    / gameplayCnt, g_perfGameplayMaxNetMs,
					g_perfGameplayAccumMsgMs    / gameplayCnt, g_perfGameplayMaxMsgMs);
				PerfWriteFrameSnapshot("GAMEPLAY-WORST", g_perfGameplayWorstFrame);
			}
			if (g_perfGameplayFrameCount > 0)
			{
				const double gameplayCnt = (double)g_perfGameplayFrameCount;
				PerfWriteCoverageLine("GAMEPLAY-COVERAGE", PERF_METRIC_GAMEPLAY, gameplayCnt,
					g_perfGameplayAccumFrameMs, g_perfGameplayAccumClientMs, g_perfGameplayAccumLogicMs, g_perfGameplayAccumRadarMs,
					g_perfGameplayAccumAudioMs, g_perfGameplayAccumNetMs, g_perfGameplayAccumMsgMs);
				PerfWriteRollupLine("GAMEPLAY-ROLLUP", PERF_METRIC_GAMEPLAY, gameplayCnt);
				PerfWriteZoomBins();
			}
			// Per-subsystem breakdown (sorted by total time desc).
			if (g_perfSubCount > 0)
			{
				int leafOrder[kPerfSubCap];
				int inclOrder[kPerfSubCap];
				int leafCount = PerfBuildRankedOrder(leafOrder, PERF_METRIC_ACCUM, true);
				int inclCount = PerfBuildRankedOrder(inclOrder, PERF_METRIC_ACCUM, false);
				int *reportOrder = leafCount > 0 ? leafOrder : inclOrder;
				int reportCount = leafCount > 0 ? leafCount : inclCount;
				if (emitGameplaySummary)
					fprintf(g_perfLogFile, "         SUB-BREAKDOWN (all frames: avg/frame | total | maxFrame | maxCall | calls/frame):\n");
				else
					fprintf(g_perfLogFile, "         SUB-BREAKDOWN (ms, avg/frame | total | maxFrame | maxCall | calls/frame):\n");
				for (int k = 0; k < reportCount; ++k)
				{
					const PerfSubEntry &e = g_perfSubs[reportOrder[k]];
					fprintf(g_perfLogFile,
						"           %-14s  avg=%.3f  total=%.1f  maxFrame=%.2f/%d  maxCall=%.2f  calls=%d (%.1f/frm)\n",
						e.tag ? e.tag : "?", e.accum / cnt, e.accum, e.maxFrameMs, e.maxFrameCalls, e.maxMs, e.calls,
						(double)e.calls / cnt);
				}
				if (leafCount > 0 && inclCount > leafCount)
				{
					if (emitGameplaySummary)
						fprintf(g_perfLogFile, "         SUB-BREAKDOWN-INCL (all frames, parent+child overlap):\n");
					else
						fprintf(g_perfLogFile, "         SUB-BREAKDOWN-INCL (parent+child overlap):\n");
					for (int k = 0; k < inclCount; ++k)
					{
						const PerfSubEntry &e = g_perfSubs[inclOrder[k]];
						fprintf(g_perfLogFile,
							"           %-14s  avg=%.3f  total=%.1f  maxFrame=%.2f/%d  maxCall=%.2f  calls=%d (%.1f/frm)\n",
							e.tag ? e.tag : "?", e.inclAccum / cnt, e.inclAccum, e.inclMaxFrameMs, e.inclMaxFrameCalls, e.inclMaxMs, e.inclCalls,
							(double)e.inclCalls / cnt);
					}
				}
				if (emitGameplaySummary)
				{
					double gameplayCnt = (double)g_perfGameplayFrameCount;
					int gameplayLeafOrder[kPerfSubCap];
					int gameplayInclOrder[kPerfSubCap];
					int gameplayLeafCount = PerfBuildRankedOrder(gameplayLeafOrder, PERF_METRIC_GAMEPLAY, true);
					int gameplayInclCount = PerfBuildRankedOrder(gameplayInclOrder, PERF_METRIC_GAMEPLAY, false);
					int *gameplayOrder = gameplayLeafCount > 0 ? gameplayLeafOrder : gameplayInclOrder;
					int gameplayReportCount = gameplayLeafCount > 0 ? gameplayLeafCount : gameplayInclCount;
					if (gameplayReportCount > 0)
					{
						fprintf(g_perfLogFile, "         GAMEPLAY-BREAKDOWN (clean gameplay frames: avg/frame | total | max/frame | calls/frame):\n");
						for (int k = 0; k < gameplayReportCount; ++k)
						{
							const PerfSubEntry &e = g_perfSubs[gameplayOrder[k]];
							fprintf(g_perfLogFile,
								"           %-14s  avg=%.3f  total=%.1f  maxFrame=%.2f  calls=%d (%.1f/frm)\n",
								e.tag ? e.tag : "?", e.gameplayAccum / gameplayCnt, e.gameplayAccum, e.gameplayMaxFrameMs, e.gameplayCalls,
								(double)e.gameplayCalls / gameplayCnt);
						}
					}
					if (gameplayLeafCount > 0 && gameplayInclCount > gameplayLeafCount)
					{
						fprintf(g_perfLogFile, "         GAMEPLAY-BREAKDOWN-INCL (clean gameplay frames, parent+child overlap):\n");
						for (int k = 0; k < gameplayInclCount; ++k)
						{
							const PerfSubEntry &e = g_perfSubs[gameplayInclOrder[k]];
							fprintf(g_perfLogFile,
								"           %-14s  avg=%.3f  total=%.1f  maxFrame=%.2f  calls=%d (%.1f/frm)\n",
								e.tag ? e.tag : "?", e.inclGameplayAccum / gameplayCnt, e.inclGameplayAccum, e.inclGameplayMaxFrameMs, e.inclGameplayCalls,
								(double)e.inclGameplayCalls / gameplayCnt);
						}
					}
				}
			}
			if (g_perfNamedCostCount > 0)
			{
				PerfWriteNamedBreakdown("NAMED-BREAKDOWN", PERF_METRIC_ACCUM, cnt);
				if (g_perfGameplayFrameCount > 0)
				{
					PerfWriteNamedBreakdown("GAMEPLAY-NAMED-BREAKDOWN", PERF_METRIC_GAMEPLAY, (double)g_perfGameplayFrameCount);
				}
			}
			if (g_perfEdgeCount > 0)
			{
				PerfWriteEdgeBreakdown("EDGE-BREAKDOWN", PERF_METRIC_ACCUM, cnt);
				if (g_perfGameplayFrameCount > 0)
				{
					PerfWriteEdgeBreakdown("GAMEPLAY-EDGE-BREAKDOWN", PERF_METRIC_GAMEPLAY, (double)g_perfGameplayFrameCount);
				}
			}
			if (g_perfRenderStatCount > 0)
			{
				fprintf(g_perfLogFile, "         RENDER-STATS (avg/sample | max | last):\n");
				for (int k = 0; k < g_perfRenderStatCount; ++k)
				{
					const PerfRenderStatEntry &e = g_perfRenderStats[k];
					if (e.samples <= 0)
						continue;
					const double statCnt = (double)e.samples;
					fprintf(g_perfLogFile,
						"           %-12s samples=%d highFps=%d  processed=%.1f|%d|%d  rendered=%.1f|%d|%d  culled=%.1f|%d|%d  skipped=%.1f|%d|%d  limit=%.1f|%d|%d\n",
						e.tag ? e.tag : "?",
						e.samples,
						e.highFpsSamples,
						e.accumProcessed / statCnt, e.maxProcessed, e.latestProcessed,
						e.accumRendered / statCnt, e.maxRendered, e.latestRendered,
						e.accumCulled / statCnt, e.maxCulled, e.latestCulled,
						e.accumSkipped / statCnt, e.maxSkipped, e.latestSkipped,
						e.accumBudget / statCnt, e.maxBudget, e.latestBudget);
					if (emitGameplaySummary && e.gameplaySamples > 0)
					{
						const double gameplayStatCnt = (double)e.gameplaySamples;
						fprintf(g_perfLogFile,
							"           %-12s gameplaySamples=%d highFps=%d  processed=%.1f  rendered=%.1f  culled=%.1f  skipped=%.1f  limit=%.1f\n",
							e.tag ? e.tag : "?",
							e.gameplaySamples,
							e.gameplayHighFpsSamples,
							e.gameplayProcessed / gameplayStatCnt,
							e.gameplayRendered / gameplayStatCnt,
							e.gameplayCulled / gameplayStatCnt,
							e.gameplaySkipped / gameplayStatCnt,
							e.gameplayBudget / gameplayStatCnt);
					}
				}
			}
			if (g_perfDroppedSubCalls > 0)
			{
				fprintf(g_perfLogFile,
					"         SUB-DROPPED cap=%d  calls=%d  total=%.1f  maxCall=%.2f\n",
					kPerfSubCap,
					g_perfDroppedSubCalls,
					g_perfDroppedSubAccumMs,
					g_perfDroppedSubMaxMs);
			}
			if (g_perfDroppedNamedCalls > 0)
			{
				fprintf(g_perfLogFile,
					"         NAMED-DROPPED cap=%d  calls=%d  total=%.1f\n",
					kPerfNamedCostCap,
					g_perfDroppedNamedCalls,
					g_perfDroppedNamedAccumMs);
			}
			if (g_perfDroppedEdgeCalls > 0)
			{
				fprintf(g_perfLogFile,
					"         EDGE-DROPPED cap=%d  calls=%d  total=%.1f  maxCall=%.2f\n",
					kPerfEdgeCap,
					g_perfDroppedEdgeCalls,
					g_perfDroppedEdgeAccumMs,
					g_perfDroppedEdgeMaxMs);
			}
			if (g_perfScopeOverflow > 0 || g_perfScopeMismatch > 0)
			{
				fprintf(g_perfLogFile,
					"         SCOPE-WARN overflow=%d  mismatch=%d\n",
					g_perfScopeOverflow,
					g_perfScopeMismatch);
			}
			fflush(g_perfLogFile);
			PerfResetSummaryWindow();
			g_perfLastSummaryMs = nowMs;
			}
		}

		const bool benchmarkSamplingActive = TheGlobalData != nullptr && TheGlobalData->m_benchmarkSamplingActive;
		if (benchmarkSamplingActive && !g_benchmarkWasSampling)
		{
			g_benchmarkFpsSamples.clear();
			g_benchmarkRenderFpsSamples.clear();
			g_benchmarkLogicFpsSamples.clear();
			g_benchmarkLastLogicSampleMs = 0u;
			g_benchmarkLastLogicSampleFrame = 0u;
		}
		if (benchmarkSamplingActive)
		{
			if (TheGameLogic != nullptr)
			{
				const UnsignedInt currentLogicFrame = TheGameLogic->getFrame();
				if (g_benchmarkLastLogicSampleMs == 0u)
				{
					g_benchmarkLastLogicSampleMs = nowMs;
					g_benchmarkLastLogicSampleFrame = currentLogicFrame;
				}
				else if (nowMs - g_benchmarkLastLogicSampleMs >= kBenchmarkSampleIntervalMs)
				{
					const DWORD sampleDeltaMs = nowMs - g_benchmarkLastLogicSampleMs;
					const UnsignedInt sampleDeltaFrames = currentLogicFrame - g_benchmarkLastLogicSampleFrame;
					const double renderFps = TheDisplay != nullptr ? TheDisplay->getAverageFPS() : 0.0;
					const double logicFps = sampleDeltaMs > 0u
						? (double)sampleDeltaFrames * 1000.0 / (double)sampleDeltaMs
						: 0.0;

					if (std::isfinite(logicFps) && logicFps > 0.0)
					{
						g_benchmarkLogicFpsSamples.push_back(logicFps);

						double benchmarkFps = logicFps;
#if defined(GENERALS_ONLINE_HIGH_FPS_SERVER)
						benchmarkFps /= (double)GENERALS_ONLINE_HIGH_FPS_FRAME_MULTIPLIER;
#endif
						if (std::isfinite(benchmarkFps) && benchmarkFps > 0.0)
						{
							g_benchmarkFpsSamples.push_back(benchmarkFps);
						}
					}

					if (std::isfinite(renderFps) && renderFps > 0.0)
					{
						g_benchmarkRenderFpsSamples.push_back(renderFps);
					}

					g_benchmarkLastLogicSampleMs = nowMs;
					g_benchmarkLastLogicSampleFrame = currentLogicFrame;
				}
			}

			if (TheGlobalData->m_benchmarkSamplingStartMs > 0
				&& nowMs - TheGlobalData->m_benchmarkSamplingStartMs >= kBenchmarkDurationMs)
			{
				PerfFinalizeBenchmarkRun();
			}
		}
		else if (perfSummaryOnlyEnabled)
		{
			PerfRecordSummaryOnlySample(nowMs);
		}
		g_benchmarkWasSampling = TheGlobalData != nullptr && TheGlobalData->m_benchmarkSamplingActive;
	}
}

// Horrible reference, but we really, really need to know if we are windowed.
extern bool DX8Wrapper_IsWindowed;
extern HWND ApplicationHWnd;

/** -----------------------------------------------------------------------------------------------
 * The "main loop" of the game engine. It will not return until the game exits.
 */
void GameEngine::execute()
{
#if defined(RTS_DEBUG)
	DWORD startTime = timeGetTime() / 1000;
#endif

	// pretty basic for now
	while (!m_quitting)
	{

		//if (TheGlobalData->m_vTune)
		{
#ifdef PERF_TIMERS
			PerfGather::resetAll();
#endif
		}

		{

#if defined(RTS_DEBUG)
			{
				// enter only if in benchmark mode
				if (TheGlobalData->m_benchmarkTimer > 0)
				{
					DWORD currentTime = timeGetTime() / 1000;
					if (TheGlobalData->m_benchmarkTimer < currentTime - startTime)
					{
						if (TheGameLogic->isInGame())
						{
							if (TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
							{
								TheRecorder->stopRecording();
							}
							TheGameLogic->clearGameData();
						}
						TheGameEngine->setQuitting(TRUE);
					}
				}
			}
#endif

			{
				try
				{
					// compute a frame
					update();
				}
				catch (INIException e)
				{
					// Release CRASH doesn't return, so don't worry about executing additional code.
					if (e.mFailureMessage)
						RELEASE_CRASH((e.mFailureMessage));
					else
						RELEASE_CRASH(("Uncaught Exception in GameEngine::update"));
				}
#if !defined(GENERALS_ONLINE_USE_SENTRY)
				catch (...)
				{
					// try to save info off
					try
					{
						if (TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_RECORD && TheRecorder->isMultiplayer())
							TheRecorder->cleanUpReplayFile();
					}
					catch (...)
					{
					}
					RELEASE_CRASH(("Uncaught Exception in GameEngine::update"));
				}
#endif
			}

			TheFramePacer->update();
		}

#ifdef PERF_TIMERS
		if (!m_quitting && TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && !TheGameLogic->isGamePaused())
		{
			PerfGather::dumpAll(TheGameLogic->getFrame());
			PerfGather::displayGraph(TheGameLogic->getFrame());
			PerfGather::resetAll();
		}
#endif

	}

	PerfFinalizeSummaryOnlySession();
}

/** -----------------------------------------------------------------------------------------------
	* Factory for the message stream
	*/
MessageStream *GameEngine::createMessageStream()
{
	// if you change this update the tools that use the engine systems
	// like GUIEdit, it creates a message stream to run in "test" mode
	return MSGNEW("GameEngineSubsystem") MessageStream;
}

//-------------------------------------------------------------------------------------------------
FileSystem *GameEngine::createFileSystem()
{
	return MSGNEW("GameEngineSubsystem") FileSystem;
}

//-------------------------------------------------------------------------------------------------
Bool GameEngine::isMultiplayerSession()
{
	return TheRecorder->isMultiplayer();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
#define CONVERT_EXEC1	"..\\Build\\nvdxt -list buildDDS.txt -dxt5 -full -outdir Art\\Textures > buildDDS.out"

void updateTGAtoDDS()
{
	// Here's the scoop. We're going to traverse through all of the files in the Art\Textures folder
	// and determine if there are any .tga files that are newer than associated .dds files. If there
	// are, then we will re-run the compression tool on them.

	File* fp = TheLocalFileSystem->openFile("buildDDS.txt", File::WRITE | File::CREATE | File::TRUNCATE | File::TEXT);
	if (!fp) {
		return;
	}

	FilenameList files;
	TheLocalFileSystem->getFileListInDirectory("Art\\Textures\\", "", "*.tga", files, TRUE);
	FilenameList::iterator it;
	for (it = files.begin(); it != files.end(); ++it) {
		AsciiString filenameTGA = *it;
		AsciiString filenameDDS = *it;
		FileInfo infoTGA;
		TheLocalFileSystem->getFileInfo(filenameTGA, &infoTGA);

		// skip the water textures, since they need to be NOT compressed
		filenameTGA.toLower();
		if (strstr(filenameTGA.str(), "caust"))
		{
			continue;
		}
		// and the recolored stuff.
		if (strstr(filenameTGA.str(), "zhca"))
		{
			continue;
		}

		// replace tga with dds
		filenameDDS.truncateBy(3); // tga
		filenameDDS.concat("dds");

		Bool needsToBeUpdated = FALSE;
		FileInfo infoDDS;
		if (TheFileSystem->doesFileExist(filenameDDS.str())) {
			TheFileSystem->getFileInfo(filenameDDS, &infoDDS);
			if (infoTGA.timestampHigh > infoDDS.timestampHigh ||
				(infoTGA.timestampHigh == infoDDS.timestampHigh &&
					infoTGA.timestampLow > infoDDS.timestampLow)) {
				needsToBeUpdated = TRUE;
			}
		}
		else {
			needsToBeUpdated = TRUE;
		}

		if (!needsToBeUpdated) {
			continue;
		}

		filenameTGA.concat("\n");
		fp->write(filenameTGA.str(), filenameTGA.getLength());
	}

	fp->close();

	system(CONVERT_EXEC1);
}

//-------------------------------------------------------------------------------------------------
// System things

// If we're using the Wide character version of MessageBox, then there's no additional
// processing necessary. Please note that this is a sleazy way to get this information,
// but pending a better one, this'll have to do.
extern const Bool TheSystemIsUnicode = (((void*)(::MessageBox)) == ((void*)(::MessageBoxW)));
