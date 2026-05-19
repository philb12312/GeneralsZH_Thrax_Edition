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

// FILE: WinMain.cpp //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright (C) 2001 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:    MapCacheBuilder
//
// File name:  WinMain.cpp
//
// Created:    Matthew D. Campbell, October 2002
//
// Desc:       Application entry point for the standalone MapCache Builder
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "Lib/BaseType.h"
#include "Common/Debug.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Resource.h"

#include "Common/ThingFactory.h"
#include "Common/ThingSort.h"
#include "Common/ThingTemplate.h"
#include "Common/FileSystem.h"
#include "Win32Device/Common/Win32LocalFileSystem.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"
#include "Common/SubsystemInterface.h"
#include "GameClient/MapUtil.h"
#include "W3DDevice/Common/W3DModuleFactory.h"


#include "Common/FileSystem.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/LocalFileSystem.h"
#include "Common/Debug.h"
#include "Common/StackDump.h"
#include "Common/GameMemory.h"
#include "Common/MapObject.h"
#include "Common/MapReaderWriterInfo.h"
#include "Common/Science.h"
#include "Common/ThingFactory.h"
#include "Common/INI.h"
#include "Common/WellKnownKeys.h"
#include "Common/GameAudio.h"
#include "Common/SpecialPower.h"
#include "Common/TerrainTypes.h"
#include "Common/DamageFX.h"
#include "Common/Upgrade.h"
#include "Common/ModuleFactory.h"
#include "Common/PlayerTemplate.h"
#include "Common/MultiplayerSettings.h"

#include "GameLogic/Armor.h"
#include "GameLogic/CaveSystem.h"
#include "GameLogic/CrateSystem.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/ScriptActions.h"
#include "GameLogic/Scripts.h"
#include "GameClient/Anim2D.h"
#include "GameClient/GameText.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/Water.h"
#include "GameClient/TerrainRoads.h"
#include "GameClient/FXList.h"
#include "GameClient/VideoPlayer.h"
#include "GameLogic/Locomotor.h"

#include "W3DDevice/Common/W3DModuleFactory.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "MilesAudioDevice/MilesAudioManager.h"

#include <io.h>
#include "Win32Device/GameClient/Win32Mouse.h"
#include "Win32Device/Common/Win32LocalFileSystem.h"
#include "Win32Device/Common/Win32BIGFileSystem.h"
#include "trim.h"


// DEFINES ////////////////////////////////////////////////////////////////////

// PRIVATE TYPES //////////////////////////////////////////////////////////////


// PRIVATE DATA ///////////////////////////////////////////////////////////////

static SubsystemInterfaceList _TheSubsystemList;

template<class SUBSYSTEM>
void initSubsystem(SUBSYSTEM*& sysref, SUBSYSTEM* sys, const char* path1 = nullptr, const char* path2 = nullptr)
{
	sysref = sys;
	_TheSubsystemList.initSubsystem(sys, path1, path2, nullptr);
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC DATA ////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
HINSTANCE ApplicationHInstance = nullptr;  ///< our application instance

/// just to satisfy the game libraries we link to
HWND ApplicationHWnd = nullptr;

const char *gAppPrefix = "MC_";

// Where are the default string files?
const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";

// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static char *nextParam(char *newSource, const char *seps)
{
	static char *source = nullptr;
	if (newSource)
	{
		source = newSource;
	}
	if (!source)
	{
		return nullptr;
	}

	// find first separator
	char *first = source;//strpbrk(source, seps);
	if (first)
	{
		// go past initial spaces
		char *firstNonSpace = first;
		while (*firstNonSpace == ' ')
			++firstNonSpace;
		first = firstNonSpace;

		// go past separator
		char *firstSep = strpbrk(first, seps);
		char firstChar[2] = {0,0};
		if (firstSep == first)
		{
			firstChar[0] = *first;
			while (*first == firstChar[0]) first++;
		}

		// find end
		char *end;
		if (firstChar[0])
			end = strpbrk(first, firstChar);
		else
			end = strpbrk(first, seps);

		// trim string & save next start pos
		if (end)
		{
			source = end+1;
			*end = 0;

			if (!*source)
				source = nullptr;
		}
		else
		{
			source = nullptr;
		}

		if (first && !*first)
			first = nullptr;
	}

	return first;
}

static Bool getDumpMapArgs(const std::list<std::string> &argvSet, std::string *mapPath, std::string *outputPath)
{
	Bool expectMap = FALSE;
	Bool expectOut = FALSE;

	for (std::list<std::string>::const_iterator it = argvSet.begin(); it != argvSet.end(); ++it)
	{
		const std::string &arg = *it;

		if (expectMap)
		{
			*mapPath = arg;
			expectMap = FALSE;
			continue;
		}

		if (expectOut)
		{
			*outputPath = arg;
			expectOut = FALSE;
			continue;
		}

		if (arg == "--dump-map")
		{
			expectMap = TRUE;
			continue;
		}

		if (arg == "--dump-out")
		{
			expectOut = TRUE;
			continue;
		}

		if (arg.rfind("--dump-map=", 0) == 0)
		{
			*mapPath = arg.substr(sizeof("--dump-map=") - 1);
			continue;
		}

		if (arg.rfind("--dump-out=", 0) == 0)
		{
			*outputPath = arg.substr(sizeof("--dump-out=") - 1);
			continue;
		}
	}

	return !mapPath->empty();
}

static Bool getSingleArgValue(const std::list<std::string> &argvSet, const char *flagName, std::string *value)
{
	std::string flag = flagName;
	std::string flagWithEquals = flag;
	flagWithEquals += "=";
	Bool expectValue = FALSE;

	for (std::list<std::string>::const_iterator it = argvSet.begin(); it != argvSet.end(); ++it)
	{
		const std::string &arg = *it;
		if (expectValue)
		{
			*value = arg;
			return TRUE;
		}

		if (arg == flag)
		{
			expectValue = TRUE;
			continue;
		}

		if (arg.rfind(flagWithEquals, 0) == 0)
		{
			*value = arg.substr(flagWithEquals.size());
			return TRUE;
		}
	}

	return FALSE;
}

static AsciiString formatPlayerLabel(const AsciiString &playerName)
{
	if (playerName.isEmpty())
	{
		return AsciiString("(neutral)");
	}

	return playerName;
}

static AsciiString formatScriptLabel(Script *pScr)
{
	AsciiString fmt;
	if (pScr->isSubroutine())
	{
		fmt.concat("[S ");
	}
	else
	{
		fmt.concat("[ns ");
	}
	if (pScr->isActive())
	{
		fmt.concat("A ");
	}
	else
	{
		fmt.concat("na ");
	}
	if (pScr->isOneShot())
	{
		fmt.concat("D] [");
	}
	else
	{
		fmt.concat("nd] [");
	}
	if (pScr->isEasy())
	{
		fmt.concat("E ");
	}
	if (pScr->isNormal())
	{
		fmt.concat("N ");
	}
	if (pScr->isHard())
	{
		fmt.concat("H]");
	}
	else
	{
		fmt.concat("]");
	}
	fmt.concat(pScr->getName().str());
	return fmt;
}

static void writeScript(FILE *theLogFile, const char *str)
{
	while (*str)
	{
		if (*str != '\r')
		{
			fputc(*str, theLogFile);
		}
		++str;
	}
}

static void fprintUnit(FILE *theLogFile, Dict *teamDict, NameKeyType keyMinUnit, NameKeyType keyMaxUnit,
	NameKeyType keyUnitType)
{
	Bool exists = FALSE;
	Int minCount = teamDict->getInt(keyMinUnit, &exists);
	Int maxCount = teamDict->getInt(keyMaxUnit, &exists);
	AsciiString type = teamDict->getAsciiString(keyUnitType, &exists);
	if (type.isEmpty())
	{
		type = "<none>";
	}
	if (minCount || maxCount)
	{
		fprintf(theLogFile, " %d-%d %s", minCount, maxCount, type.str());
	}
}

static void dumpBuildLists(FILE *theLogFile)
{
	fprintf(theLogFile, "\nBuild Lists\n");
	for (Int sideIndex = 0; sideIndex < TheSidesList->getNumSides(); ++sideIndex)
	{
		SidesInfo *sideInfo = TheSidesList->getSideInfo(sideIndex);
		Dict *sideDict = sideInfo->getDict();
		AsciiString playerName = formatPlayerLabel(sideDict->getAsciiString(TheKey_playerName));
		AsciiString faction = sideDict->getAsciiString(TheKey_playerFaction);
		AsciiString allies = sideDict->getAsciiString(TheKey_playerAllies);
		AsciiString enemies = sideDict->getAsciiString(TheKey_playerEnemies);
		fprintf(theLogFile, "PLAYER %s faction '%s' allies '%s' enemies '%s'\n",
			playerName.str(), faction.str(), allies.str(), enemies.str());

		BuildListInfo *buildInfo = sideInfo->getBuildList();
		if (buildInfo == nullptr)
		{
			fprintf(theLogFile, "  <no build list entries>\n");
			continue;
		}

		for (; buildInfo; buildInfo = buildInfo->getNext())
		{
			AsciiString script = buildInfo->getScript();
			if (script.isEmpty())
			{
				script = "<none>";
			}

			fprintf(theLogFile,
				"  %s => %s @ (%0.0f,%0.0f) angle %0.0f initiallyBuilt=%s rebuilds=%u script='%s' health=%d whiner=%s unsellable=%s repairable=%s\n",
				buildInfo->getBuildingName().str(),
				buildInfo->getTemplateName().str(),
				buildInfo->getLocation()->x,
				buildInfo->getLocation()->y,
				buildInfo->getAngle() * 180.0f / PI,
				buildInfo->isInitiallyBuilt() ? "yes" : "no",
				buildInfo->getNumRebuilds(),
				script.str(),
				buildInfo->getHealth(),
				buildInfo->getWhiner() ? "yes" : "no",
				buildInfo->getUnsellable() ? "yes" : "no",
				buildInfo->getRepairable() ? "yes" : "no");
		}
	}
	fprintf(theLogFile, "End of Build Lists\n");
}

static void dumpTeams(FILE *theLogFile)
{
	fprintf(theLogFile, "\nTeams\n");
	for (Int sideIndex = 0; sideIndex < TheSidesList->getNumSides(); ++sideIndex)
	{
		Dict *sideDict = TheSidesList->getSideInfo(sideIndex)->getDict();
		AsciiString playerName = sideDict->getAsciiString(TheKey_playerName);
		AsciiString playerLabel = formatPlayerLabel(playerName);
		fprintf(theLogFile, "PLAYER %s\n", playerLabel.str());

		for (Int teamIndex = 0; teamIndex < TheSidesList->getNumTeams(); ++teamIndex)
		{
			TeamsInfo *teamInfo = TheSidesList->getTeamInfo(teamIndex);
			Dict *teamDict = teamInfo->getDict();
			if (teamDict->getAsciiString(TheKey_teamOwner) != playerName)
			{
				continue;
			}

			Bool exists = FALSE;
			AsciiString teamName = teamDict->getAsciiString(TheKey_teamName);
			AsciiString waypoint = teamDict->getAsciiString(TheKey_teamHome, &exists);
			Int priority = teamDict->getInt(TheKey_teamProductionPriority, &exists);
			AsciiString trigger = teamDict->getAsciiString(TheKey_teamProductionCondition, &exists);

			fprintf(theLogFile, "TEAM %s home '%s', priority %d, condition '%s',\n",
				teamName.str(), waypoint.str(), priority, trigger.str());
			fprintf(theLogFile, "  UNITS:");
			fprintUnit(theLogFile, teamDict, TheKey_teamUnitMinCount1, TheKey_teamUnitMaxCount1, TheKey_teamUnitType1);
			fprintUnit(theLogFile, teamDict, TheKey_teamUnitMinCount2, TheKey_teamUnitMaxCount2, TheKey_teamUnitType2);
			fprintUnit(theLogFile, teamDict, TheKey_teamUnitMinCount3, TheKey_teamUnitMaxCount3, TheKey_teamUnitType3);
			fprintUnit(theLogFile, teamDict, TheKey_teamUnitMinCount4, TheKey_teamUnitMaxCount4, TheKey_teamUnitType4);
			fprintUnit(theLogFile, teamDict, TheKey_teamUnitMinCount5, TheKey_teamUnitMaxCount5, TheKey_teamUnitType5);
			fprintUnit(theLogFile, teamDict, TheKey_teamUnitMinCount6, TheKey_teamUnitMaxCount6, TheKey_teamUnitType6);
			fprintUnit(theLogFile, teamDict, TheKey_teamUnitMinCount7, TheKey_teamUnitMaxCount7, TheKey_teamUnitType7);
			fprintf(theLogFile, "\n  SCRIPTS:");

			AsciiString script = teamDict->getAsciiString(TheKey_teamOnCreateScript, &exists);
			if (script.isEmpty()) script = "<none>";
			fprintf(theLogFile, " OnCreate='%s'", script.str());
			script = teamDict->getAsciiString(TheKey_teamOnIdleScript, &exists);
			if (script.isEmpty()) script = "<none>";
			fprintf(theLogFile, " OnIdle='%s'", script.str());
			script = teamDict->getAsciiString(TheKey_teamOnDestroyedScript, &exists);
			if (script.isEmpty()) script = "<none>";
			fprintf(theLogFile, " OnDestroyed='%s'", script.str());
			script = teamDict->getAsciiString(TheKey_teamEnemySightedScript, &exists);
			if (script.isEmpty()) script = "<none>";
			fprintf(theLogFile, " OnEnemySighted='%s'", script.str());
			script = teamDict->getAsciiString(TheKey_teamAllClearScript, &exists);
			if (script.isEmpty()) script = "<none>";
			fprintf(theLogFile, " OnAllClear='%s'\n", script.str());
		}
	}
	fprintf(theLogFile, "End of Teams\n");
}

static void dumpScripts(FILE *theLogFile)
{
	fprintf(theLogFile, "\nScripts\n");
	for (Int sideIndex = 0; sideIndex < TheSidesList->getNumSides(); ++sideIndex)
	{
		Dict *sideDict = TheSidesList->getSideInfo(sideIndex)->getDict();
		AsciiString playerLabel = formatPlayerLabel(sideDict->getAsciiString(TheKey_playerName));
		fprintf(theLogFile, "PLAYER %s\n", playerLabel.str());

		ScriptList *scriptList = TheSidesList->getSideInfo(sideIndex)->getScriptList();
		if (scriptList == nullptr)
		{
			fprintf(theLogFile, "  <no scripts>\n");
			continue;
		}

		for (ScriptGroup *group = scriptList->getScriptGroup(); group; group = group->getNext())
		{
			if (group->getName().isEmpty())
			{
				continue;
			}

			fprintf(theLogFile, "GROUP %s\n", group->getName().str());
			for (Script *script = group->getScript(); script; script = script->getNext())
			{
				if (script->getName().isEmpty())
				{
					continue;
				}

				AsciiString label = formatScriptLabel(script);
				AsciiString comment = script->getComment();
				AsciiString text = script->getUiText();
				fprintf(theLogFile, "%s:\n", label.str());
				if (comment.isNotEmpty())
				{
					fprintf(theLogFile, "//:%s:\n", comment.str());
				}
				writeScript(theLogFile, text.str());
				fprintf(theLogFile, "\n");
			}
		}

		for (Script *script = scriptList->getScript(); script; script = script->getNext())
		{
			if (script->getName().isEmpty())
			{
				continue;
			}

			AsciiString label = formatScriptLabel(script);
			AsciiString comment = script->getComment();
			AsciiString text = script->getUiText();
			fprintf(theLogFile, "%s:\n", label.str());
			if (comment.isNotEmpty())
			{
				fprintf(theLogFile, "//:%s:\n", comment.str());
			}
			writeScript(theLogFile, text.str());
			fprintf(theLogFile, "\n");
		}
	}
	fprintf(theLogFile, "End of Scripts\n");
}

static void dumpMapObjects(FILE *theLogFile)
{
	static const char *vetStrings[] = {"Green", "Regular", "Veteran", "Elite"};
	static const char *aggroStrings[] = {"Passive", "Normal", "Guard", "Hunt", "Aggressive", "Sleep"};
	AsciiString noOwner = "No Owner";

	fprintf(theLogFile, "\nBuildings\n");
	for (MapObject *mapObject = MapObject::getFirstMapObject(); mapObject; mapObject = mapObject->getNext())
	{
		const ThingTemplate *thingTemplate = mapObject->getThingTemplate();
		if (thingTemplate == nullptr || thingTemplate->getEditorSorting() != ES_STRUCTURE)
		{
			continue;
		}

		Dict *objectDict = mapObject->getProperties();
		TeamsInfo *teamInfo = TheSidesList->findTeamInfo(objectDict->getAsciiString(TheKey_originalOwner));
		Dict *teamDict = teamInfo ? teamInfo->getDict() : nullptr;
		AsciiString ownerName = teamDict ? teamDict->getAsciiString(TheKey_teamOwner) : noOwner;

		Bool showScript = FALSE;
		Bool showName = FALSE;
		AsciiString script = objectDict->getAsciiString(TheKey_objectScriptAttachment, &showScript);
		AsciiString name = objectDict->getAsciiString(TheKey_objectName, &showName);

		fprintf(theLogFile, "  %s, %s, @ (%0.0f,%0.0f), Angle %0.0f, %d%%",
			thingTemplate->getName().str(),
			ownerName.str(),
			mapObject->getLocation()->x,
			mapObject->getLocation()->y,
			mapObject->getAngle() * 180.0f / PI,
			objectDict->getInt(TheKey_objectInitialHealth));
		if (showName)
		{
			fprintf(theLogFile, ", Name %s", name.str());
		}
		else
		{
			fprintf(theLogFile, ", Unnamed");
		}
		if (showScript)
		{
			fprintf(theLogFile, ", Script %s\n", script.str());
		}
		else
		{
			fprintf(theLogFile, ", No Script\n");
		}
	}
	fprintf(theLogFile, "End of Buildings\n");

	fprintf(theLogFile, "\nUnits\n");
	for (MapObject *mapObject = MapObject::getFirstMapObject(); mapObject; mapObject = mapObject->getNext())
	{
		const ThingTemplate *thingTemplate = mapObject->getThingTemplate();
		if (thingTemplate == nullptr)
		{
			continue;
		}

		EditorSortingType sorting = thingTemplate->getEditorSorting();
		if (sorting != ES_VEHICLE && sorting != ES_INFANTRY)
		{
			continue;
		}

		Bool exists = FALSE;
		Dict *objectDict = mapObject->getProperties();
		TeamsInfo *teamInfo = TheSidesList->findTeamInfo(objectDict->getAsciiString(TheKey_originalOwner));
		Dict *teamDict = teamInfo ? teamInfo->getDict() : nullptr;
		AsciiString ownerName = teamDict ? teamDict->getAsciiString(TheKey_teamOwner) : noOwner;
		Int veterancy = objectDict->getInt(TheKey_objectVeterancy, &exists);
		if (!exists) veterancy = 0;
		Int aggressiveness = objectDict->getInt(TheKey_objectAggressiveness, &exists);
		if (!exists) aggressiveness = 0;
		aggressiveness++;

		Bool showScript = FALSE;
		Bool showName = FALSE;
		AsciiString script = objectDict->getAsciiString(TheKey_objectScriptAttachment, &showScript);
		AsciiString name = objectDict->getAsciiString(TheKey_objectName, &showName);

		fprintf(theLogFile, "  %s, %s, @ %0.0f.%0.0f, Angle %0.0f, %d%%",
			thingTemplate->getName().str(),
			ownerName.str(),
			mapObject->getLocation()->x / 10,
			mapObject->getLocation()->y / 10,
			mapObject->getAngle() * 180.0f / PI,
			objectDict->getInt(TheKey_objectInitialHealth));
		if (showName)
		{
			fprintf(theLogFile, ", Name %s", name.str());
		}
		else
		{
			fprintf(theLogFile, ", Unnamed");
		}
		if (showScript)
		{
			fprintf(theLogFile, ", Script %s", script.str());
		}
		else
		{
			fprintf(theLogFile, ", No Script");
		}
		fprintf(theLogFile, ", Team %s", objectDict->getAsciiString(TheKey_originalOwner).str());
		fprintf(theLogFile, ", %s", objectDict->getBool(TheKey_objectRecruitableAI, &exists) ? "AIRecruitable" : "Not AIRecruitable");
		fprintf(theLogFile, ", %s", objectDict->getBool(TheKey_objectSelectable, &exists) ? "Selectable" : "Not Selectable");
		fprintf(theLogFile, ", %s", aggroStrings[aggressiveness]);
		fprintf(theLogFile, ", %s\n", vetStrings[veterancy]);
	}
	fprintf(theLogFile, "End of Units\n");

	fprintf(theLogFile, "\nWaypoints\n");
	for (MapObject *mapObject = MapObject::getFirstMapObject(); mapObject; mapObject = mapObject->getNext())
	{
		if (mapObject->isWaypoint())
		{
			fprintf(theLogFile, "  %s, @ %0.0f.%0.0f\n",
				mapObject->getWaypointName().str(),
				mapObject->getLocation()->x / 10,
				mapObject->getLocation()->y / 10);
		}
	}
	fprintf(theLogFile, "End of Waypoints\n");
}

static Bool dumpMapToText(const std::string &mapPath, const std::string &outputPath)
{
	CachedFileInputStream inputStream;
	if (!inputStream.open(mapPath.c_str()))
	{
		DEBUG_CRASH(("Failed to open map '%s'", mapPath.c_str()));
		return FALSE;
	}

	FILE *theLogFile = fopen(outputPath.c_str(), "w");
	if (theLogFile == nullptr)
	{
		DEBUG_CRASH(("Failed to create dump '%s'", outputPath.c_str()));
		inputStream.close();
		return FALSE;
	}

	WorldHeightMap *heightMap = nullptr;
	Bool ok = FALSE;
	try
	{
		heightMap = new WorldHeightMap(&inputStream, TRUE);
		fprintf(theLogFile, "Dump of Map Contents\n");
		fprintf(theLogFile, "Source Map: %s\n", mapPath.c_str());
		dumpMapObjects(theLogFile);
		dumpBuildLists(theLogFile);
		dumpTeams(theLogFile);
		dumpScripts(theLogFile);
		ok = TRUE;
	}
	catch (...)
	{
		DEBUG_CRASH(("Exception while dumping map '%s'", mapPath.c_str()));
	}

	delete heightMap;
	WorldHeightMap::freeListOfMapObjects();
	if (TheSidesList)
	{
		TheSidesList->clear();
	}
	inputStream.close();
	fclose(theLogFile);
	return ok;
}

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// WinMain ====================================================================
/** Application entry point */
//=============================================================================
Int APIENTRY WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance,
                      LPSTR lpCmdLine, Int nCmdShow )
{

	// initialize the memory manager early
	initMemoryManager();

	try
	{

	// save application instance
	ApplicationHInstance = hInstance;


	// Determine the executable directory first. We may override it via --game-dir.
	char buf[_MAX_PATH];
	GetModuleFileName(nullptr, buf, sizeof(buf));
	if (char *pEnd = strrchr(buf, '\\')) {
		*pEnd = 0;
	}

	/*
	** Convert WinMain arguments to simple main argc and argv
	*/
	std::list<std::string> argvSet;
	char *token;
	token = nextParam(lpCmdLine, "\" ");
	while (token != nullptr) {
		char * str = strtrim(token);
		argvSet.push_back(str);
		DEBUG_LOG(("Adding '%s'", str));
		token = nextParam(nullptr, "\" ");
	}

	std::string gameDir;
	if (getSingleArgValue(argvSet, "--game-dir", &gameDir) && !gameDir.empty())
	{
		::SetCurrentDirectory(gameDir.c_str());
	}
	else
	{
		::SetCurrentDirectory(buf);
	}

	// not part of the subsystem list, because it should normally never be reset!
	TheNameKeyGenerator = new NameKeyGenerator;
	TheNameKeyGenerator->init();

	TheFileSystem = new FileSystem;

	initSubsystem(TheLocalFileSystem, (LocalFileSystem*)new Win32LocalFileSystem);
	initSubsystem(TheArchiveFileSystem, (ArchiveFileSystem*)new Win32BIGFileSystem);
	INI ini;
	initSubsystem(TheWritableGlobalData, new GlobalData(), "Data\\INI\\Default\\GameData.ini", "Data\\INI\\GameData.ini");
	initSubsystem(TheGameText, CreateGameTextInterface());
	initSubsystem(TheScienceStore, new ScienceStore(), "Data\\INI\\Default\\Science.ini", "Data\\INI\\Science.ini");
	initSubsystem(TheMultiplayerSettings, new MultiplayerSettings(), "Data\\INI\\Default\\Multiplayer.ini", "Data\\INI\\Multiplayer.ini");
	initSubsystem(TheTerrainTypes, new TerrainTypeCollection(), "Data\\INI\\Default\\Terrain.ini", "Data\\INI\\Terrain.ini");
	initSubsystem(TheTerrainRoads, new TerrainRoadCollection(), "Data\\INI\\Default\\Roads.ini", "Data\\INI\\Roads.ini");
	initSubsystem(TheScriptEngine, (ScriptEngine*)(new ScriptEngine()));
	initSubsystem(TheAudio, (AudioManager*)new MilesAudioManager());
	initSubsystem(TheVideoPlayer, (VideoPlayerInterface*)(new VideoPlayer()));
	initSubsystem(TheModuleFactory, (ModuleFactory*)(new W3DModuleFactory()));
	initSubsystem(TheSidesList, new SidesList());
	initSubsystem(TheCaveSystem, new CaveSystem());
	initSubsystem(TheRankInfoStore, new RankInfoStore(), nullptr, "Data\\INI\\Rank");
	initSubsystem(ThePlayerTemplateStore, new PlayerTemplateStore(), "Data\\INI\\Default\\PlayerTemplate", "Data\\INI\\PlayerTemplate");
	initSubsystem(TheSpecialPowerStore, new SpecialPowerStore(), "Data\\INI\\Default\\SpecialPower", "Data\\INI\\SpecialPower" );
	initSubsystem(TheParticleSystemManager, (ParticleSystemManager*)(new W3DParticleSystemManager()));
	initSubsystem(TheFXListStore, new FXListStore(), "Data\\INI\\Default\\FXList", "Data\\INI\\FXList");
	initSubsystem(TheWeaponStore, new WeaponStore(), nullptr, "Data\\INI\\Weapon");
	initSubsystem(TheObjectCreationListStore, new ObjectCreationListStore(), "Data\\INI\\Default\\ObjectCreationList", "Data\\INI\\ObjectCreationList");
	initSubsystem(TheLocomotorStore, new LocomotorStore(), nullptr, "Data\\INI\\Locomotor");
	initSubsystem(TheDamageFXStore, new DamageFXStore(), nullptr, "Data\\INI\\DamageFX");
	initSubsystem(TheArmorStore, new ArmorStore(), nullptr, "Data\\INI\\Armor");
	initSubsystem(TheThingFactory, new ThingFactory(), "Data\\INI\\Default\\Object", "Data\\INI\\Object");
	initSubsystem(TheCrateSystem, new CrateSystem(), "Data\\INI\\Default\\Crate", "Data\\INI\\Crate");
	initSubsystem(TheUpgradeCenter, new UpgradeCenter, "Data\\INI\\Default\\Upgrade", "Data\\INI\\Upgrade");
	initSubsystem(TheAnim2DCollection, new Anim2DCollection ); //Init's itself.

	_TheSubsystemList.postProcessLoadAll();

	std::string dumpMapPath;
	std::string dumpOutputPath;
	if (getDumpMapArgs(argvSet, &dumpMapPath, &dumpOutputPath))
	{
		if (dumpOutputPath.empty())
		{
			dumpOutputPath = dumpMapPath + ".dump.txt";
		}

		const Bool dumped = dumpMapToText(dumpMapPath, dumpOutputPath);

		_TheSubsystemList.shutdownAll();

		delete TheFileSystem;
		TheFileSystem = nullptr;

		delete TheNameKeyGenerator;
		TheNameKeyGenerator = nullptr;

		shutdownMemoryManager();

		return dumped ? 0 : 1;
	}

	TheWritableGlobalData->m_buildMapCache = TRUE;

	TheMapCache = new MapCache;

	// add in allowed maps
	for (std::list<std::string>::const_iterator cit = argvSet.begin(); cit != argvSet.end(); ++cit)
	{
		DEBUG_LOG(("Adding shipping map: '%s'", cit->c_str()));
		TheMapCache->addShippingMap((*cit).c_str());
	}

	TheMapCache->updateCache();

	delete TheMapCache;
	TheMapCache = nullptr;

	// load the dialog box
	//DialogBox( hInstance, (LPCTSTR)IMAGE_PACKER_DIALOG,
	//					 nullptr, (DLGPROC)ImagePackerProc );

	// delete TheGlobalData
	//delete TheGlobalData;
	//TheGlobalData = nullptr;

	_TheSubsystemList.shutdownAll();

	delete TheFileSystem;
	TheFileSystem = nullptr;

	delete TheNameKeyGenerator;
	TheNameKeyGenerator = nullptr;

	}
	catch (...)
	{
		DEBUG_CRASH(("Munkee munkee!"));
	}

	shutdownMemoryManager();

	// all done
	return 0;

}
