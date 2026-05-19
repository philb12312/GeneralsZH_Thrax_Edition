/*
**	Command & Conquer Generals(tm)
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

// AISkirmishPlayer.cpp
// Computerized opponent
// Author: Michael S. Booth, January 2002

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine


#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/BuildAssistant.h"
#include "Common/SpecialPower.h"
#include "Common/ThingTemplate.h"
#include "Common/WellKnownKeys.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/AISkirmishPlayer.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/RebuildHoleBehavior.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameClient/InGameUI.h"
#include "GameClient/TerrainVisual.h"
#include "Common/Radar.h"


#define USE_DOZER 1

// ---------------------------------------------------------------------------
// BotAI dedicated file logger - always active regardless of build type.
// Writes to log_HHMMSS.txt (or log_HHMMSS_N.txt) next to the exe.
// ---------------------------------------------------------------------------
static FILE *g_botAILogFile = nullptr;
static bool  g_botAILogOpened = false;

static void BotAILog(const char *fmt, ...)
{
	if (!g_botAILogOpened)
	{
		g_botAILogOpened = true;

		char exeDir[MAX_PATH] = {};
		::GetModuleFileNameA(nullptr, exeDir, MAX_PATH);
		char *slash = strrchr(exeDir, '\\');
		if (slash) *(slash + 1) = '\0'; else exeDir[0] = '\0';

		time_t now = time(nullptr);
		struct tm *t = localtime(&now);
		char path[MAX_PATH];

		// Try log_HHMMSS.txt, then log_HHMMSS_2.txt etc. to avoid collisions.
		snprintf(path, MAX_PATH, "%slog_%02d%02d%02d.txt", exeDir,
			t->tm_hour, t->tm_min, t->tm_sec);
		g_botAILogFile = fopen(path, "w");
		if (g_botAILogFile == nullptr)
		{
			for (int i = 2; i <= 9 && g_botAILogFile == nullptr; ++i)
			{
				snprintf(path, MAX_PATH, "%slog_%02d%02d%02d_%d.txt", exeDir,
					t->tm_hour, t->tm_min, t->tm_sec, i);
				g_botAILogFile = fopen(path, "w");
			}
		}

		if (g_botAILogFile)
		{
			char startBuf[64];
			strftime(startBuf, sizeof(startBuf), "%Y-%m-%d %H:%M:%S", t);
			fprintf(g_botAILogFile, "=== BotAI Alliance Cooperation log - %s ===\n", startBuf);
			fprintf(g_botAILogFile, "=== Build: Generals (Release) ===\n");
			fprintf(g_botAILogFile, "Format: [frame] BOT_NAME | message\n\n");
			fflush(g_botAILogFile);
		}
	}

	if (g_botAILogFile == nullptr)
		return;

	unsigned int frame = TheGameLogic ? (unsigned int)TheGameLogic->getFrame() : 0;
	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	fprintf(g_botAILogFile, "[%06u] %s\n", frame, buf);
	// Flush every ~10 seconds of game time to avoid per-call syscall overhead.
	static unsigned int s_lastFlushFrame = 0;
	if (frame == 0 || frame - s_lastFlushFrame >= 300)
	{
		fflush(g_botAILogFile);
		s_lastFlushFrame = frame;
	}
}

namespace
{
	const UnsignedInt BOT_CHAT_COOLDOWN_FRAMES = 30 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt BOT_PING_COOLDOWN_FRAMES = 45 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt HUMAN_ASSIST_DURATION_FRAMES = 300 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt DIRECT_MOVE_HOLD_FRAMES = 300 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt GROUP_ATTACK_WAIT_FRAMES = 30 * LOGICFRAMES_PER_SECOND;
	const UnsignedInt PLAN_CHANGE_COOLDOWN_FRAMES = 300 * LOGICFRAMES_PER_SECOND;

	enum BotFlavor
	{
		BOT_FLAVOR_GENERIC = 0,
		BOT_FLAVOR_USA,
		BOT_FLAVOR_CHINA,
		BOT_FLAVOR_GLA
	};

	BotFlavor getBotFlavor(const Player *player)
	{
		if (player == nullptr)
		{
			return BOT_FLAVOR_GENERIC;
		}

		AsciiString side = player->getSide();
		side.toLower();

		if (strstr(side.str(), "america") != nullptr || strstr(side.str(), "usa") != nullptr)
		{
			return BOT_FLAVOR_USA;
		}

		if (strstr(side.str(), "china") != nullptr)
		{
			return BOT_FLAVOR_CHINA;
		}

		if (strstr(side.str(), "gla") != nullptr)
		{
			return BOT_FLAVOR_GLA;
		}

		return BOT_FLAVOR_GENERIC;
	}

	AsciiString getFlavorText(BotFlavor flavor, const char *generic, const char *usa, const char *china, const char *gla)
	{
		switch (flavor)
		{
			case BOT_FLAVOR_USA:
				return AsciiString(usa);
			case BOT_FLAVOR_CHINA:
				return AsciiString(china);
			case BOT_FLAVOR_GLA:
				return AsciiString(gla);
			default:
				return AsciiString(generic);
		}
	}

	AsciiString getPlayerNameAscii(const Player *player)
	{
		if (player == nullptr)
		{
			return AsciiString("ally");
		}

		AsciiString playerName;
		playerName.translate(const_cast<Player *>(player)->getPlayerDisplayName());
		if (!playerName.isEmpty())
		{
			return playerName;
		}

		return TheNameKeyGenerator->keyToName(player->getPlayerNameKey());
	}

	/**
	 * Map a player's color to a human-readable color name based on RGB channels.
	 * Bots always refer to other players by color (e.g. "Red", "Blue").
	 */
	AsciiString getPlayerColorName(const Player *player)
	{
		if (player == nullptr)
		{
			return AsciiString("Unknown");
		}

		Color c = player->getPlayerColor();
		UnsignedByte r, g, b, a;
		GameGetColorComponents(c, &r, &g, &b, &a);

		// Determine the dominant color channels to pick a name.
		if (r > 180 && g < 80 && b < 80) return AsciiString("Red");
		if (r < 80 && g < 80 && b > 180) return AsciiString("Blue");
		if (r < 80 && g > 180 && b < 80) return AsciiString("Green");
		if (r > 200 && g > 150 && b < 80) return AsciiString("Orange");
		if (r > 200 && g > 200 && b < 80) return AsciiString("Yellow");
		if (r < 80 && g > 150 && b > 150) return AsciiString("Teal");
		if (r > 150 && g < 80 && b > 150) return AsciiString("Purple");
		if (r > 200 && g > 100 && b > 150) return AsciiString("Pink");
		if (r > 180 && g > 180 && b > 180) return AsciiString("White");
		if (r < 60 && g < 60 && b < 60) return AsciiString("Black");

		// Fallback: use the brightest channel.
		if (r >= g && r >= b) return AsciiString("Red");
		if (g >= r && g >= b) return AsciiString("Green");
		return AsciiString("Blue");
	}

	/**
	 * Returns a short log prefix for the bot: "Red(P3)", "Blue(P1)", etc.
	 */
	AsciiString getBotLogPrefix(const Player *player)
	{
		if (player == nullptr)
			return AsciiString("?(P?)");
		AsciiString result;
		result.format("%s(P%d)", getPlayerColorName(player).str(), player->getPlayerIndex());
		return result;
	}

	/**
	 * Returns a short log prefix for the bot: "Red(P3)", "Blue(P1)", etc.
	 */
	AsciiString getBotLogPrefix(const Player *player)
	{
		if (player == nullptr)
			return AsciiString("?(P?)");
		AsciiString result;
		result.format("%s(P%d)", getPlayerColorName(player).str(), player->getPlayerIndex());
		return result;
	}

	Bool containsText(const AsciiString &text, const char *snippet)
	{
		return snippet != nullptr && strstr(text.str(), snippet) != nullptr;
	}

	Bool equalsText(const AsciiString &text, const char *value)
	{
		return value != nullptr && strcmp(text.str(), value) == 0;
	}

	Bool startsWithText(const AsciiString &text, const char *prefix)
	{
		return prefix != nullptr && strncmp(text.str(), prefix, strlen(prefix)) == 0;
	}

	AsciiString getDirectionLabel(const AsciiString &command)
	{
		Bool hasTop    = containsText(command, "top")    || containsText(command, "north");
		Bool hasBottom = containsText(command, "bottom") || containsText(command, "south");
		Bool hasLeft   = containsText(command, "left")   || containsText(command, "west");
		Bool hasRight  = containsText(command, "right")  || containsText(command, "east");
		Bool hasCenter = containsText(command, "center") || containsText(command, "centre")
			|| containsText(command, "middle") || containsText(command, "mid");
		if (hasCenter) return AsciiString("center");
		if (hasTop    && hasLeft)  return AsciiString("top left");
		if (hasTop    && hasRight) return AsciiString("top right");
		if (hasBottom && hasLeft)  return AsciiString("bottom left");
		if (hasBottom && hasRight) return AsciiString("bottom right");
		if (hasTop)    return AsciiString("top");
		if (hasBottom) return AsciiString("bottom");
		if (hasLeft)   return AsciiString("left");
		if (hasRight)  return AsciiString("right");
		return AsciiString("");
	}

	Bool tryGetDirectionalTargetPosition(const AsciiString &command, Coord3D *targetPos)
	{
		if (targetPos == nullptr || TheTerrainLogic == nullptr)
		{
			return false;
		}

		Region3D mapBounds;
		TheTerrainLogic->getMaximumPathfindExtent(&mapBounds);

		Real targetX = mapBounds.lo.x + mapBounds.width() / 2;
		Real targetY = mapBounds.lo.y + mapBounds.height() / 2;
		Bool hasDirection = false;

		const Bool hasTop = containsText(command, "top") || containsText(command, "north");
		const Bool hasBottom = containsText(command, "bottom") || containsText(command, "south");
		const Bool hasLeft = containsText(command, "left") || containsText(command, "west");
		const Bool hasRight = containsText(command, "right") || containsText(command, "east");
		const Bool hasCenter = containsText(command, "center") || containsText(command, "centre")
			|| containsText(command, "middle") || containsText(command, "mid");

		if (hasTop)
		{
			targetY = mapBounds.lo.y + mapBounds.height() * 0.15f;
			hasDirection = true;
		}
		else if (hasBottom)
		{
			targetY = mapBounds.lo.y + mapBounds.height() * 0.85f;
			hasDirection = true;
		}

		if (hasLeft)
		{
			targetX = mapBounds.lo.x + mapBounds.width() * 0.15f;
			hasDirection = true;
		}
		else if (hasRight)
		{
			targetX = mapBounds.lo.x + mapBounds.width() * 0.85f;
			hasDirection = true;
		}

		if (hasCenter)
		{
			hasDirection = true;
		}

		if (!hasDirection)
		{
			return false;
		}

		targetPos->x = targetX;
		targetPos->y = targetY;
		targetPos->z = TheTerrainLogic->getGroundHeight(targetX, targetY);
		return true;
	}

	void appendIssuedCombatOrderSample(AsciiString &samples, Int &sampleCount, const Object *obj)
	{
		if (obj == nullptr || sampleCount >= 4)
		{
			return;
		}

		const ThingTemplate *templ = obj->getTemplate();
		const Coord3D *pos = obj->getPosition();
		AsciiString sample;
		sample.format("%s#%d@(%.0f,%.0f)",
			templ ? templ->getName().str() : "Unknown",
			obj->getID(),
			pos ? pos->x : 0.0f,
			pos ? pos->y : 0.0f);
		if (!samples.isEmpty())
		{
			samples.concat(", ");
		}
		samples.concat(sample);
		++sampleCount;
	}

	const char *resolveCombatOrderReason(const char *debugReason, const char *fallbackReason)
	{
		return (debugReason != nullptr && *debugReason != '\0') ? debugReason : fallbackReason;
	}

	void noteCombatOrderContext(AIUpdateInterface *ai, const Coord3D *targetPos, const char *reason, const char *verb, Int relatedPlayerIndex)
	{
		if (ai != nullptr)
		{
			ai->setDebugBotOrderContext(reason, verb, targetPos, relatedPlayerIndex);
		}
	}
}



///////////////////////////////////////////////////////////////////////////////////////////////////
// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

AISkirmishPlayer::AISkirmishPlayer( Player *p ) :	AIPlayer(p),
m_curFlankBaseDefense(0),
m_curFrontBaseDefense(0),
m_curFrontLeftDefenseAngle(0),
m_curFrontRightDefenseAngle(0),
m_curLeftFlankLeftDefenseAngle(0),
m_curLeftFlankRightDefenseAngle(0),
m_curRightFlankLeftDefenseAngle(0),
m_curRightFlankRightDefenseAngle(0),
m_frameToCheckEnemy(0),
m_currentEnemy(nullptr),
m_frameNextAllyAssistCheck(0),
m_frameNextCoordCheck(0),
m_coordAttackWaitStartFrame(0),
m_frameNextBotChat(0),
m_frameNextBotPing(0),
m_holdAttackUntilFrame(0),
m_requestedAssistUntilFrame(0),
m_frameNextMoveReissue(0),
m_coordAttackPending(false),
m_coordAttackEnemyPlayerIndex(PLAYER_INDEX_INVALID),
m_requestedAssistPlayerIndex(PLAYER_INDEX_INVALID),
m_frameLastPlanChange(0)

{
	m_frameLastBuildingBuilt = TheGameLogic->getFrame();
	p->setCanBuildUnits(true); // turn on ai production by default.
	m_coordAttackTarget.zero();
	m_coordRallyPoint.zero();
	m_requestedAssistTarget.zero();
}

AISkirmishPlayer::~AISkirmishPlayer()
{
	clearTeamsInQueue();
}


/**
 * Build our base.
 */
void AISkirmishPlayer::processBaseBuilding()
{
	//
	// Refresh base buildings. Scan through list, if a building is missing,
	// rebuild it, unless it's rebuild count is zero.
	//
	if (m_readyToBuildStructure)
	{
		const ThingTemplate *bldgPlan=nullptr;
		BuildListInfo	*bldgInfo = nullptr;
		Bool isPriority = false;
		Object *bldg = nullptr;
		const ThingTemplate *powerPlan=nullptr;
		BuildListInfo	*powerInfo = nullptr;
		Bool isUnderPowered = !m_player->getEnergy()->hasSufficientPower();
		Bool powerUnderConstruction = false;
		for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
		{
			AsciiString name = info->getTemplateName();
			if (name.isEmpty()) continue;
			const ThingTemplate *curPlan = TheThingFactory->findTemplate( name );
			if (!curPlan) {
				DEBUG_LOG(("*** ERROR - Build list building '%s' doesn't exist.", name.str()));
				continue;
			}
			bldg = TheGameLogic->findObjectByID( info->getObjectID() );
			// check for hole.
			if (info->getObjectID() != INVALID_ID) {
				// used to have a building.
				Object *bldg = TheGameLogic->findObjectByID( info->getObjectID() );
				if (bldg==nullptr) {
					// got destroyed.
					ObjectID priorID;
					priorID = info->getObjectID();
					info->setObjectID(INVALID_ID);
					info->setObjectTimestamp(TheGameLogic->getFrame()+1);
					// Scan for a GLA hole.	KINDOF_REBUILD_HOLE
					Object *obj;
					for( obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() ) {
						if (!obj->isKindOf(KINDOF_REBUILD_HOLE)) continue;
						RebuildHoleBehaviorInterface *rhbi = RebuildHoleBehavior::getRebuildHoleBehaviorInterfaceFromObject( obj );
						if( rhbi ) {
							ObjectID spawnerID = rhbi->getSpawnerID();
							if (priorID == spawnerID) {
								DEBUG_LOG(("AI Found hole to rebuild %s", curPlan->getName().str()));
								info->setObjectID(obj->getID());
							}
						}
 					}
				}	else {
					if (bldg->getControllingPlayer() == m_player) {
						// Check for built or dozer missing.
						if( bldg->getStatusBits().test( OBJECT_STATUS_UNDER_CONSTRUCTION ) )
						{
							if (bldg->isKindOf(KINDOF_FS_POWER) && !bldg->isKindOf(KINDOF_CASH_GENERATOR))
							{
								powerUnderConstruction = true;
							}
							// make sure dozer is working on him.
							ObjectID builder = bldg->getBuilderID();
							Object* myDozer = TheGameLogic->findObjectByID(builder);
							if (myDozer==nullptr) {
								DEBUG_LOG(("AI's Dozer got killed.  Find another dozer."));
								queueDozer();
 								myDozer = findDozer(bldg->getPosition());
								if (myDozer==nullptr || myDozer->getAI()==nullptr) {
									continue;
								}
								myDozer->getAI()->aiResumeConstruction(bldg, CMD_FROM_AI);
							}	else {
								// make sure he is building.
								myDozer->getAI()->aiResumeConstruction(bldg, CMD_FROM_AI);
							}
						}
					} else {
						// oops, got captured.
						info->setObjectID(INVALID_ID);
						info->setObjectTimestamp(TheGameLogic->getFrame()+1);
					}
				}
			}
			if (info->getObjectID()==INVALID_ID && info->getObjectTimestamp()>0) {
				// this object was built at some time, and got destroyed at or near objectTimestamp.
				// Wait a few seconds before initiating a rebuild.
				if (info->getObjectTimestamp()+TheAI->getAiData()->m_rebuildDelaySeconds*LOGICFRAMES_PER_SECOND > TheGameLogic->getFrame()) {
					continue;
				}	else {
					DEBUG_LOG(("Enabling rebuild for %s", info->getTemplateName().str()));
					info->setObjectTimestamp(0); // ready to build.
				}
			}
			if (bldg) {
				continue; // already built.
			}
			// Make sure it is safe to build here.
			if (!isLocationSafe(info->getLocation(), curPlan)) {
				continue;
			}
			if (info->isPriorityBuild()) {
				// Always take priority build, unless we already have priority build.
				if (!isPriority) {
					bldgPlan = curPlan;
					bldgInfo = info;
					isPriority = true;
				}
			}
			if (curPlan->isKindOf(KINDOF_FS_POWER)) {
				if (powerPlan==nullptr && !curPlan->isKindOf(KINDOF_CASH_GENERATOR)) {
					if (isUnderPowered || info->isAutomaticBuild()) {
						powerPlan = curPlan;
						powerInfo = info;
					}
				}
			}
			if (!info->isAutomaticBuild()) {
				continue; // marked to not build automatically.
			}
			Object *dozer = findDozer(info->getLocation());
			if (dozer==nullptr) {
				if (isUnderPowered) {
					queueDozer();
				}
				continue;
			}
			if (TheBuildAssistant->canMakeUnit(dozer, bldgPlan)!=CANMAKE_OK) {
				if (info->isBuildable()) {
					AsciiString bldgName = info->getTemplateName();
					bldgName.concat(" - Dozer unable to build - money or technology missing.");
					TheScriptEngine->AppendDebugMessage(bldgName, false);
				}
				continue;
			}
			// check if this building has any "rebuilds" left
			if (info->isBuildable())
			{
				if (bldgPlan == nullptr) {
					bldgPlan = curPlan;
					bldgInfo = info;
				}
			}
		}
		if (powerInfo && powerPlan && !powerPlan->isEquivalentTo(bldgPlan)) {
			if (!powerUnderConstruction) {
				bldgPlan = powerPlan;
				bldgInfo = powerInfo;
				DEBUG_LOG(("Forcing build of power plant."));
			}
		}
		if (bldgPlan && bldgInfo) {
#ifdef USE_DOZER
			// dozer-construct the building
			bldg = buildStructureWithDozer(bldgPlan, bldgInfo);
			// store the object with the build order
			if (bldg)
			{
				bldgInfo->setObjectID( bldg->getID() );
				bldgInfo->decrementNumRebuilds();

				m_readyToBuildStructure = false;
				m_structureTimer = TheAI->getAiData()->m_structureSeconds*LOGICFRAMES_PER_SECOND;
				if (m_player->getMoney()->countMoney() < TheAI->getAiData()->m_resourcesPoor) {
					m_structureTimer = m_structureTimer/TheAI->getAiData()->m_structuresPoorMod;
				}	else if (m_player->getMoney()->countMoney() > TheAI->getAiData()->m_resourcesWealthy) {
					m_structureTimer = m_structureTimer/TheAI->getAiData()->m_structuresWealthyMod;
				}
				m_frameLastBuildingBuilt = TheGameLogic->getFrame();
				// only build one building per delay loop
			}

#else
			// force delay between rebuilds
			Int framesToBuild = bldgPlan->calcTimeToBuild(m_player);
			if (TheGameLogic->getFrame() - m_frameLastBuildingBuilt < framesToBuild)
			{
				m_buildDelay = framesToBuild - (TheGameLogic->getFrame() - m_frameLastBuildingBuilt);
				return;
			}	else {
				// building is missing, (re)build it
				// deduct money to build, if we have it
				Int cost = bldgPlan->calcCostToBuild( m_player );
				if (m_player->getMoney()->countMoney() >= cost)
				{
					// we have the money, deduct it
					m_player->getMoney()->withdraw( cost );

					// inst-construct the building
					bldg = buildStructureNow(bldgPlan, bldgInfo);
					// store the object with the build order
					if (bldg)
					{
						bldgInfo->setObjectID( bldg->getID() );
						bldgInfo->decrementNumRebuilds();

						m_readyToBuildStructure = false;
						m_structureTimer = TheAI->getAiData()->m_structureSeconds*LOGICFRAMES_PER_SECOND;
						if (m_player->getMoney()->countMoney() < TheAI->getAiData()->m_resourcesPoor) {
							m_structureTimer = m_structureTimer/TheAI->getAiData()->m_structuresPoorMod;
						}	else if (m_player->getMoney()->countMoney() > TheAI->getAiData()->m_resourcesWealthy) {
							m_structureTimer = m_structureTimer/TheAI->getAiData()->m_structuresWealthyMod;
						}
						m_frameLastBuildingBuilt = TheGameLogic->getFrame();
					}
				}
			}
#endif
		}
	}
}

/**
 * Invoked when a unit I am training comes into existence
 */
void AISkirmishPlayer::onUnitProduced( Object *factory, Object *unit )
{
	AIPlayer::onUnitProduced(factory, unit);
}

/**
 * Search the computer player's buildings for one that can build the given request
 * and start training the unit.
 * If busyOK is true, it will queue a unit even if one is building.  This lets
 * script invoked teams "push" to the front of the queue.
 */
Bool AISkirmishPlayer::startTraining( WorkOrder *order, Bool busyOK, AsciiString teamName)
{
	Object *factory = findFactory(order->m_thing, busyOK);
	if( factory )
	{
		ProductionUpdateInterface *pu = factory->getProductionUpdateInterface();
		if (pu && pu->queueCreateUnit( order->m_thing, pu->requestUniqueUnitID() )) {
			order->m_factoryID = factory->getID();
			if (TheGlobalData->m_debugAI) {
				AsciiString teamStr = "Queuing ";
				teamStr.concat(order->m_thing->getName());
				teamStr.concat(" for ");
				teamStr.concat(teamName);
				TheScriptEngine->AppendDebugMessage(teamStr, false);
			}
			return true;
		}
	}

	return FALSE;

}


/**
 * Check if this team is buildable, doesn't exceed maximum limits, meets conditions, and isn't under construction.
 */
Bool AISkirmishPlayer::isAGoodIdeaToBuildTeam( TeamPrototype *proto )
{
	// Check condition.
	if (!proto->evaluateProductionCondition()) {
		return false;
	}
	// check build limit
	if (proto->countTeamInstances() >= proto->getTemplateInfo()->m_maxInstances){
		if (TheGlobalData->m_debugAI) {
			AsciiString str;
			str.format("Team %s not chosen - %d already exist.", proto->getName().str(), proto->countTeamInstances());
			TheScriptEngine->AppendDebugMessage(str, false);
		}
		return false;	// Max already built.
	}

	for ( DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamBuildQueue(); !iter.done(); iter.advance())
	{
		TeamInQueue *team = iter.cur();
		if (team->m_team->getPrototype() == proto) {
			return false; // currently building one of these.
		}
	}
	Bool needMoney;
	if (!isPossibleToBuildTeam( proto, true, needMoney)) {
		if (TheGlobalData->m_debugAI) {
			AsciiString str;
			if (needMoney) {
				str.format("Team %s not chosen - Not enough money.", proto->getName().str());
			} else {
				str.format("Team %s not chosen - Factory/tech missing or busy.", proto->getName().str());
			}
			TheScriptEngine->AppendDebugMessage(str, false);
		}
		return false;
	}
	return true;
}

/**
 * See if any existing teams need reinforcements, and have higher priority.
 */
Bool AISkirmishPlayer::selectTeamToReinforce( Int minPriority )
{
	return AIPlayer::selectTeamToReinforce(minPriority);
}

/**
 * Determine the next team to build.  Return true if one was selected.
 */
Bool AISkirmishPlayer::selectTeamToBuild()
{
	return AIPlayer::selectTeamToBuild();
}

/**
	Build a specific building.
	*/
void AISkirmishPlayer::buildSpecificAIBuilding(const AsciiString &thingName)
{
	//
	Bool found = false;
	Bool foundUnbuilt = false;
	for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
	{
		if (info->getTemplateName()==thingName)
		{
			AsciiString name = info->getTemplateName();
			if (name.isEmpty()) continue;
			const ThingTemplate *bldgPlan = TheThingFactory->findTemplate( name );
			if (!bldgPlan) {
				DEBUG_LOG(("*** ERROR - Build list building '%s' doesn't exist.", name.str()));
				continue;
			}
			Object *bldg = TheGameLogic->findObjectByID( info->getObjectID() );
			found = true;
			if (bldg) {
				continue; // already built.
			}
			if (info->isPriorityBuild()) {
				continue; // already marked for priority build.
			}
			foundUnbuilt = true;
			info->markPriorityBuild();
			break;
		}
	}
	if (foundUnbuilt) {
		m_buildDelay = 0;
		AsciiString buildingStr = "Queueing building '";
		buildingStr.concat(thingName);
		buildingStr.concat("' for construction.");
		TheScriptEngine->AppendDebugMessage(buildingStr, false);
	}	else if (found) {
		AsciiString buildingStr = "Warning - all instances of building '";
		buildingStr.concat(thingName);
		buildingStr.concat("' are already built or queued for build, not queueing.");
		TheScriptEngine->AppendDebugMessage(buildingStr, false);
	}	else {
		AsciiString buildingStr = "Error - could not find building '";
		buildingStr.concat(thingName);
		buildingStr.concat("' in the building template list.");
		TheScriptEngine->AppendDebugMessage(buildingStr, false);
	}
}



/**
	Gets the player index of my enemy.
	*/
Int AISkirmishPlayer::getMyEnemyPlayerIndex() {
	Int playerNdx;
	if (m_currentEnemy) {
		return m_currentEnemy->getPlayerIndex();
	}
	// For now, return first human player, as there should only be one. jba
	for (playerNdx=0; playerNdx<ThePlayerList->getPlayerCount(); playerNdx++) {
		if (ThePlayerList->getNthPlayer(playerNdx)->getPlayerType() == PLAYER_HUMAN) {
			break;
		}
	}
	return playerNdx;
}

/**
	Get the AI's enemy.  Recalc if it has been a while (5 seconds.)
*/
void AISkirmishPlayer::acquireEnemy()
{
	Player *bestEnemy = nullptr;
	Real bestDistanceSqr = HUGE_DIST*HUGE_DIST;

	if (m_currentEnemy) {
		Bool inBadShape = !m_currentEnemy->hasAnyUnits() || !m_currentEnemy->hasAnyBuildFacility();
		if (!inBadShape) return;
	}

	// look for the closest enemy.
	Int i;
	for (i=0; i<ThePlayerList->getPlayerCount(); i++) {
		Player *curPlayer = ThePlayerList->getNthPlayer(i);
		if (m_player->getRelationship(curPlayer->getDefaultTeam()) == ENEMIES) {
			if (curPlayer->hasAnyObjects()==false) continue; // not much of an enemy.
			// ok, we got an enemy;
			// If a player is out of units, or out of build facilities, we can lower his priority.
			Bool inBadShape = !curPlayer->hasAnyUnits() || !curPlayer->hasAnyBuildFacility();

			Coord3D enemyPos = m_baseCenter;
			Region2D bounds;
			getPlayerStructureBounds(&bounds, i);
			enemyPos.x = bounds.lo.x + bounds.width()/2;
			enemyPos.y = bounds.lo.y + bounds.height()/2;
			Real curDistSqr = sqr(enemyPos.x-m_baseCenter.x) + sqr(enemyPos.y-m_baseCenter.y);

			//Fudge for in bad shape.	 If an enemy is crippled, concentrate on the other ones.
			if (inBadShape) {
				curDistSqr = HUGE_DIST*HUGE_DIST*0.5f;
			}
			// See if other ai's are attacking this target.
			// We don't want the ai's to gang up on one enemy.
			Int k;
			for (k=0; k<ThePlayerList->getPlayerCount(); k++) {
				if (k==i) continue;  // don't count self.
				Player *somePlayer = ThePlayerList->getNthPlayer(k);
				if (somePlayer->isSkirmishAIPlayer() && (somePlayer->getCurrentEnemy()==curPlayer)) {
					// Some ai is already targeting this guy.  Add a distance penalty.
					curDistSqr += (500*500);
				}
				if (somePlayer->isSkirmishAIPlayer() && (somePlayer->getCurrentEnemy()==m_player)) {
					// he is attacking me.  So I will (gently) prefer to attack him.
					curDistSqr -= (25*25);
					if (curDistSqr<0) curDistSqr = 0;
				}
			}

			// Ai enemy - will take if we don't get a better offer.
			if (curDistSqr<bestDistanceSqr) {
				bestEnemy = curPlayer;
				bestDistanceSqr = curDistSqr;
			}
		}
	}
	if (bestEnemy!=nullptr && (bestEnemy!=m_currentEnemy)) {
		m_currentEnemy = bestEnemy;
		AsciiString msg = TheNameKeyGenerator->keyToName(m_player->getPlayerNameKey());
		msg.concat(" acquiring target enemy player: ");
		msg.concat(TheNameKeyGenerator->keyToName(m_currentEnemy->getPlayerNameKey()));
		TheScriptEngine->AppendDebugMessage( msg, false);
	}
}



/**
	Get the AI's enemy.  Recalc if it has been a while (20 seconds.)
*/
Player *AISkirmishPlayer::getAiEnemy()
{
	if (TheGameLogic->getFrame()>=m_frameToCheckEnemy) {
		m_frameToCheckEnemy = TheGameLogic->getFrame() + 5*LOGICFRAMES_PER_SECOND;
		acquireEnemy();
	}
	return m_currentEnemy;
}

/**
	Build base defense structures on the front or flank of the base.
*/
void AISkirmishPlayer::buildAIBaseDefense(Bool flank)
{
	const AISideInfo *resInfo = TheAI->getAiData()->m_sideInfo;
	AsciiString defenseTemplateName;
	while (resInfo) {
		if (resInfo->m_side == m_player->getSide()) {
			defenseTemplateName = resInfo->m_baseDefenseStructure1;
			break;
		}
		resInfo = resInfo->m_next;
	}
	if (resInfo) {
		buildAIBaseDefenseStructure(resInfo->m_baseDefenseStructure1, flank);
	}
}

/**
	Build base defense structures on the front or flank of the base.
	Base defenses are placed as follows:
	m_baseCenter and m_baseRadius are calculated on map load.
	Defenses are placed along the this circle.
	Front defenses (!flank) are placed starting at the "Center" approach path.
	The first front defense is placed towards th Center path.  Number 2 is placed
	to the left of #1, #3 is placed to the right of #1, #4 is placed to the left of
	#2 and so on.  So it looks like:

												#1
									 #2 			#3
					#6  #4								 #5	  #7
		  #8																	#9

	The flank base defenses cover the "Flank" approach, and the "Backdoor" approach.
	They alternate between these two, so the first flank defense covers flank, and the second
	covers backdoor, and continue to alternate.  They cover the approach using the same
	pattern as front above.
	John A.

	*/
void AISkirmishPlayer::buildAIBaseDefenseStructure(const AsciiString &thingName, Bool flank)
{
	const ThingTemplate *tTemplate = TheThingFactory->findTemplate(thingName);
	if (tTemplate==nullptr) {
		DEBUG_CRASH(("Couldn't find base defense structure '%s' for side %s", thingName.str(), m_player->getSide().str()));
		return;
	}
	do {
		AsciiString pathLabel;
		if (flank) {
			if (m_curFlankBaseDefense&1) {
				pathLabel.format("%s%d", SKIRMISH_FLANK, m_player->getMpStartIndex()+1);
			}	else {
				pathLabel.format("%s%d", SKIRMISH_BACKDOOR, m_player->getMpStartIndex()+1);
			}
		}	else {
			pathLabel.format("%s%d", SKIRMISH_CENTER, m_player->getMpStartIndex()+1);
		}

		Coord3D goalPos = m_baseCenter;
		Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &goalPos, pathLabel );
		if (way) {
			goalPos = *way->getLocation();
		} else {
			if (flank) return;
			Region2D bounds;
			getPlayerStructureBounds(&bounds, getMyEnemyPlayerIndex());
			goalPos.x = bounds.lo.x + bounds.width()/2;
			goalPos.y = bounds.lo.y + bounds.height()/2;
		}
		Coord2D offset;
		offset.x = goalPos.x-m_baseCenter.x;
		offset.y = goalPos.y-m_baseCenter.y;
		offset.normalize();
		offset.x *= m_baseRadius;
		offset.y *= m_baseRadius;

		Real structureRadius = tTemplate->getTemplateGeometryInfo().getBoundingCircleRadius();
		Real baseCircumference = 2*PI*m_baseRadius;
		Real angleOffset = 2*PI*(structureRadius*4/baseCircumference);

		Int selector;
		Real angle;
		if (flank) {
			selector = m_curFlankBaseDefense>>1;
			if (m_curFlankBaseDefense&1) {
				if (selector&1) {
					m_curLeftFlankRightDefenseAngle -= angleOffset;
					angle = m_curLeftFlankRightDefenseAngle;
				}	else {
					angle = m_curLeftFlankLeftDefenseAngle;
					m_curLeftFlankLeftDefenseAngle += angleOffset;
				}
			}	else {
				if (selector&1) {
					m_curRightFlankRightDefenseAngle -= angleOffset;
					angle = m_curRightFlankRightDefenseAngle;
				}	else {
					angle = m_curRightFlankLeftDefenseAngle;
					m_curRightFlankLeftDefenseAngle += angleOffset;
				}
			}

		} else {
			selector = m_curFrontBaseDefense;
			if (selector&1) {
				m_curFrontRightDefenseAngle -= angleOffset;
				angle = m_curFrontRightDefenseAngle;
			}	else {
				angle = m_curFrontLeftDefenseAngle;
				m_curFrontLeftDefenseAngle += angleOffset;
			}
		}

		if (angle > PI/3) break;
		Real s = sin(angle);
		Real c = cos(angle);

// TheSuperHackers @info helmutbuhler 21/04/2025 This debug mutates the code to become CRC incompatible
#if defined(RTS_DEBUG) || !RETAIL_COMPATIBLE_CRC
		DEBUG_LOG(("Angle is %f sin %f, cos %f ", 180*angle/PI, s, c));
		DEBUG_LOG(("Offset is %f  %f, new is %f, %f ",
			offset.x, offset.y,
			offset.x*c - offset.y*s,
			offset.y*c + offset.x*s
			));
#endif
		Coord3D buildPos = m_baseCenter;
		buildPos.x += offset.x*c - offset.y*s;
		buildPos.y += offset.y*c + offset.x*s;

		/* See if we can build there. */
		Bool canBuild;
		Real placeAngle = tTemplate->getPlacementViewAngle();
		canBuild = LBC_OK == TheBuildAssistant->isLocationLegalToBuild(&buildPos, tTemplate, placeAngle,
			BuildAssistant::TERRAIN_RESTRICTIONS|BuildAssistant::NO_OBJECT_OVERLAP, nullptr, m_player);
		TheTerrainVisual->removeAllBibs();	// isLocationLegalToBuild adds bib feedback, turn it off.  jba.
		if (flank) {
			m_curFlankBaseDefense++;
		} else {
			m_curFrontBaseDefense++;
		}
		if (canBuild) {
			m_player->addToPriorityBuildList(thingName, &buildPos, placeAngle);
			break;
		}
	}	while (true);

}


/**
	Checks bridges along a waypoint path.  If any are destroyed, sends a dozer to fix, and returns true.
	If there is no bridge problem, returns false.
	*/
Bool AISkirmishPlayer::checkBridges(Object *unit, Waypoint *way)
{
	Coord3D unitPos = *unit->getPosition();
	AIUpdateInterface *ai = unit->getAI();
	if (!ai) return false; // no ai
	const LocomotorSet& locoSet = ai->getLocomotorSet();
	Waypoint *curWay;
	for (curWay = way; curWay; curWay = curWay->getNext()) {
		if (TheAI->pathfinder()->clientSafeQuickDoesPathExist(locoSet, &unitPos, curWay->getLocation())) {
			continue;
		}
		ObjectID brokenBridge = INVALID_ID;
		if (TheAI->pathfinder()->findBrokenBridge(locoSet, &unitPos, curWay->getLocation(), &brokenBridge)) {
			repairStructure(brokenBridge);
			return true;
		}
	}
	return false;

}


/**
	Build a specific team.  If priorityBuild, put at front of queue with priority set.
	*/
void AISkirmishPlayer::buildSpecificAITeam( TeamPrototype *teamProto, Bool priorityBuild)
{
	AIPlayer::buildSpecificAITeam(teamProto, priorityBuild);
}


/**
	Recruit a specific team, within the specific radius of the home position.
	*/
void AISkirmishPlayer::recruitSpecificAITeam(TeamPrototype *teamProto, Real recruitRadius)
{
	if (recruitRadius < 1) recruitRadius = 99999.0f;
	//
	// Create "Team in queue" based on team population
	//
	if (teamProto)
	{
		if (teamProto->getIsSingleton()) {
			Team *singletonTeam = TheTeamFactory->findTeam( teamProto->getName() );
			if (singletonTeam && singletonTeam->hasAnyObjects()) {
				AsciiString teamStr = "Unable to recruit singleton team '";
				teamStr.concat("' because team already exists.");
				TheScriptEngine->AppendDebugMessage(teamStr, false);
				return;
			}
		}
		if (!teamProto->getTemplateInfo()->m_hasHomeLocation)
		{
			AsciiString teamStr = "Error : team '";
			teamStr.concat(teamProto->getName());
			teamStr.concat("' has no Home Position (or Origin).");
			TheScriptEngine->AppendDebugMessage(teamStr, false);
		}
		// create inactive team to place members into as they are built
		// when team is complete, the team is activated
		Team *theTeam = TheTeamFactory->createInactiveTeam( teamProto->getName() );
		AsciiString teamName = teamProto->getName();
		teamName.concat(" - Recruiting.");
		TheScriptEngine->AppendDebugMessage(teamName, false);
		const TCreateUnitsInfo *unitInfo = &teamProto->getTemplateInfo()->m_unitsInfo[0];
//		WorkOrder *orders = nullptr;
		Int i;
		Int unitsRecruited = 0;
		// Recruit.
		for( i=0; i<teamProto->getTemplateInfo()->m_numUnitsInfo; i++ )
		{
			const ThingTemplate *thing = TheThingFactory->findTemplate( unitInfo[i].unitThingName );
			if (thing)
			{
				int count = unitInfo[i].maxUnits;
				while (count>0) {
					Object *unit = theTeam->tryToRecruit(thing, &teamProto->getTemplateInfo()->m_homeLocation, recruitRadius);
					if (unit)
					{
						unitsRecruited++;

						AsciiString teamStr = "Team '";
						teamStr.concat(theTeam->getPrototype()->getName());
						teamStr.concat("' recruits ");
						teamStr.concat(thing->getName());
						teamStr.concat(" from team '");
						teamStr.concat(unit->getTeam()->getPrototype()->getName());
						teamStr.concat("'");
						TheScriptEngine->AppendDebugMessage(teamStr, false);

						unit->setTeam(theTeam);

						AIUpdateInterface *ai = unit->getAIUpdateInterface();
						if (ai)
						{
#ifdef DEBUG_LOGGING
							Coord3D pos = *unit->getPosition();
							Coord3D to = teamProto->getTemplateInfo()->m_homeLocation;
							DEBUG_LOG(("Moving unit from %f,%f to %f,%f", pos.x, pos.y , to.x, to.y ));
#endif
							ai->aiMoveToPosition( &teamProto->getTemplateInfo()->m_homeLocation, CMD_FROM_AI);
						}
					} else {
						break;
					}
					count--;
				}
			}
		}
		if (unitsRecruited>0)
		{
			/* We have something to build. */
			TeamInQueue *team = newInstance(TeamInQueue);
			// Put in front of queue.
			prependTo_TeamReadyQueue(team);
			team->m_priorityBuild = false;
			team->m_workOrders = nullptr;
			team->m_frameStarted = TheGameLogic->getFrame();
			team->m_team = theTeam;
			AsciiString teamName = teamProto->getName();
			teamName.concat(" - Finished recruiting.");
			TheScriptEngine->AppendDebugMessage(teamName, false);
		}	else {
			//disband.
			if (!theTeam->getPrototype()->getIsSingleton()) {
				deleteInstance(theTeam);
				theTeam = nullptr;
			}
			AsciiString teamName = teamProto->getName();
			teamName.concat(" - Recruited 0 units, disbanding.");
			TheScriptEngine->AppendDebugMessage(teamName, false);
		}
	}
}




/**
 * Train our teams.
 */
void AISkirmishPlayer::processTeamBuilding()
{
	// select a new team
	if (selectTeamToBuild()) {
		queueUnits();
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if it's time to build another base building.
 */
void AISkirmishPlayer::doBaseBuilding()
{
	if (m_player->getCanBuildBase()) {
		// See if we are ready to start trying a structure.
		if (!m_readyToBuildStructure) {
			m_structureTimer--;
			if (m_structureTimer<=0) {
				m_readyToBuildStructure = true;
				m_buildDelay = 0;
			}
			if (m_structureTimer > 3*LOGICFRAMES_PER_SECOND) {
				m_structureTimer = 3*LOGICFRAMES_PER_SECOND;
			}
		}
		// This timer is to keep from banging on the logic each frame.  If something interesting
		// happens, like a building is added or a unit finished, the timers are shortcut.
		m_buildDelay--;
		if (m_buildDelay<1) {
			if (m_readyToBuildStructure) {
				processBaseBuilding();
			}
			if (m_buildDelay<1) {	// processBaseBuilding may reset m_buildDelay.
				m_buildDelay = 2*LOGICFRAMES_PER_SECOND; // check again in 2 seconds.
			}
			// Note that this timer gets shortcut when a building is completed.
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if any ready teams have finished moving to the rally point.
 */
void AISkirmishPlayer::checkReadyTeams()
{
	// Remember ready-queue teams before activation so only newly graduated teams
	// get redirected. Reissuing every combat team here was a major pathfind spike.
	static const Int MAX_TRACKED_READY_TEAMS = 64;
	Team *readyTeamsBefore[MAX_TRACKED_READY_TEAMS];
	Int readyTeamCountBefore = 0;

	Int countBefore = 0;
	for (DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamReadyQueue(); !iter.done(); iter.advance())
	{
		TeamInQueue *teamInQueue = iter.cur();
		if (teamInQueue != nullptr && readyTeamCountBefore < MAX_TRACKED_READY_TEAMS)
		{
			readyTeamsBefore[readyTeamCountBefore++] = teamInQueue->m_team;
		}
		++countBefore;
	}

	AIPlayer::checkReadyTeams();

	if (countBefore == 0)
		return; // Nothing was waiting, nothing could have been activated.

	Int countAfter = 0;
	for (DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamReadyQueue(); !iter.done(); iter.advance())
		++countAfter;

	if (countAfter >= countBefore)
		return; // No team graduated this call.

	Team *graduatedTeams[MAX_TRACKED_READY_TEAMS];
	Int graduatedTeamCount = 0;
	for (Int beforeIndex = 0; beforeIndex < readyTeamCountBefore; ++beforeIndex)
	{
		Team *candidate = readyTeamsBefore[beforeIndex];
		Bool stillReady = false;
		for (DLINK_ITERATOR<TeamInQueue> iter = iterate_TeamReadyQueue(); !iter.done(); iter.advance())
		{
			TeamInQueue *teamInQueue = iter.cur();
			if (teamInQueue != nullptr && teamInQueue->m_team == candidate)
			{
				stillReady = true;
				break;
			}
		}
		if (!stillReady && candidate != nullptr && graduatedTeamCount < MAX_TRACKED_READY_TEAMS)
		{
			graduatedTeams[graduatedTeamCount++] = candidate;
		}
	}

	if (graduatedTeamCount == 0)
		return;

	// At least one team was just activated. Its OnCreate script may have sent
	// units toward the enemy; redirect only those new teams to the active order.
	UnsignedInt curFrame = TheGameLogic->getFrame();
	if (m_coordAttackPending && (m_coordRallyPoint.x != 0.0f || m_coordRallyPoint.y != 0.0f))
	{
		Int sent = 0;
		for (Int i = 0; i < graduatedTeamCount; ++i)
			sent += sendCombatTeamToPosition(graduatedTeams[i], &m_coordRallyPoint, false);
		BotAILog("%s | checkReadyTeams: %d team(s) graduated during staged attack -> %d units rally (%.0f,%.0f)",
			getBotLogPrefix(m_player).str(), graduatedTeamCount, sent, m_coordRallyPoint.x, m_coordRallyPoint.y);
	}
	else if (!m_coordAttackPending && m_holdAttackUntilFrame > curFrame
		&& (m_coordRallyPoint.x != 0.0f || m_coordRallyPoint.y != 0.0f))
	{
		Int sent = 0;
		for (Int i = 0; i < graduatedTeamCount; ++i)
			sent += sendCombatTeamToPosition(graduatedTeams[i], &m_coordRallyPoint, true);
		BotAILog("%s | checkReadyTeams: %d team(s) graduated during hold -> %d units guard (%.0f,%.0f)",
			getBotLogPrefix(m_player).str(), graduatedTeamCount, sent, m_coordRallyPoint.x, m_coordRallyPoint.y);
	}
	else if (m_requestedAssistUntilFrame > curFrame && m_requestedAssistPlayerIndex != PLAYER_INDEX_INVALID
		&& (m_requestedAssistTarget.x != 0.0f || m_requestedAssistTarget.y != 0.0f))
	{
		Int sent = 0;
		for (Int i = 0; i < graduatedTeamCount; ++i)
			sent += sendCombatTeamToPosition(graduatedTeams[i], &m_requestedAssistTarget, true);
		BotAILog("%s | checkReadyTeams: %d team(s) graduated during assist -> %d units guard (%.0f,%.0f)",
			getBotLogPrefix(m_player).str(), graduatedTeamCount, sent, m_requestedAssistTarget.x, m_requestedAssistTarget.y);
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if any queued teams have finished building, or have run out of time.
 */
void AISkirmishPlayer::checkQueuedTeams()
{
	AIPlayer::checkQueuedTeams();
}

//----------------------------------------------------------------------------------------------------------
/**
 * See if it is time to start another ai team building.
 */
void AISkirmishPlayer::doTeamBuilding()
{
	// See if any teams are expired.
	if (m_player->getCanBuildUnits()) {
		// See if we are ready to start trying a team.
		if (!m_readyToBuildTeam) {
			m_teamTimer--;
			if (m_teamTimer<=0) {
				m_readyToBuildTeam = true;
				m_teamDelay = 0;
			}
			if (m_teamTimer > 3*LOGICFRAMES_PER_SECOND) {
				m_teamTimer = 3*LOGICFRAMES_PER_SECOND;
			}
		}

		// This timer is to keep from banging on the logic each frame.  If something interesting
		// happens, like a building is added or a unit finished, the timers are shortcut.
		m_teamDelay--;
		if (m_teamDelay<1) {
			queueUnits(); // update the queues.
			if (m_readyToBuildTeam) {
				processTeamBuilding();
			}
			m_teamDelay = 2*LOGICFRAMES_PER_SECOND; // check again in 5 seconds.
			// Note that this timer gets shortcut when a unit or building is completed.
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * Perform computer-controlled player AI
 */
void AISkirmishPlayer::update()
{
	AIPlayer::update();

	UnsignedInt curFrame = TheGameLogic->getFrame();
	if (!m_coordAttackPending && m_holdAttackUntilFrame > 0 && curFrame >= m_holdAttackUntilFrame)
	{
		m_holdAttackUntilFrame = 0;
		m_coordRallyPoint.zero();
		m_frameNextMoveReissue = 0;
	}

	// Fallback guard refresh — fires every 30s in case a team script overrode guard mode.
	// checkReadyTeams() intercepts newly-built teams immediately; this is a safety net only.
	if (curFrame >= m_frameNextMoveReissue)
	{
		const UnsignedInt REISSUE_INTERVAL = 30 * LOGICFRAMES_PER_SECOND;
		if (!m_coordAttackPending && m_holdAttackUntilFrame > curFrame && (m_coordRallyPoint.x != 0.0f || m_coordRallyPoint.y != 0.0f))
		{
			m_frameNextMoveReissue = curFrame + REISSUE_INTERVAL;
			Int sent = sendAllCombatTeamsToGuardPosition(&m_coordRallyPoint);
			BotAILog("%s | hold guard refresh -> %d units at (%.0f,%.0f)  holdUntil=%u",
				getBotLogPrefix(m_player).str(), sent, m_coordRallyPoint.x, m_coordRallyPoint.y, m_holdAttackUntilFrame);
		}
		else if (m_requestedAssistUntilFrame > curFrame && m_requestedAssistPlayerIndex != PLAYER_INDEX_INVALID
			&& (m_requestedAssistTarget.x != 0.0f || m_requestedAssistTarget.y != 0.0f))
		{
			m_frameNextMoveReissue = curFrame + REISSUE_INTERVAL;
			Int sent = sendAllCombatTeamsToGuardPosition(&m_requestedAssistTarget);
			BotAILog("%s | assist guard refresh -> %d units at (%.0f,%.0f)  assistUntil=%u",
				getBotLogPrefix(m_player).str(), sent, m_requestedAssistTarget.x, m_requestedAssistTarget.y, m_requestedAssistUntilFrame);
		}
		else
		{
			m_frameNextMoveReissue = curFrame + REISSUE_INTERVAL;
		}
	}

	// TheSuperHackers @feature Alliance cooperation
	assistAlliedPlayers();
	coordinateGroupAttack();
}

//----------------------------------------------------------------------------------------------------------
/**
 * Adjusts the build list to match the starting position.
 */
void AISkirmishPlayer::adjustBuildList(BuildListInfo *list)
{
	Bool foundStart = false;
	Coord3D startPos;

	// Find our command center location.
	Object *obj;
	for( obj = TheGameLogic->getFirstObject(); obj; obj = obj->getNextObject() )
	{

		Player *owner = obj->getControllingPlayer();
		if (owner==m_player) {
			// See if it's a command center.
			if (obj->isKindOf(KINDOF_COMMANDCENTER)) {
				foundStart = true;
				startPos = *obj->getPosition();
				m_player->onStructureUndone(obj);
				TheAI->pathfinder()->removeObjectFromPathfindMap(obj);
				TheGameLogic->destroyObject(obj);
				break;
			}
		}
	}
	if (!foundStart) {
		DEBUG_LOG(("Couldn't find starting command center for ai player."));
		return;
	}
	// Find the location of the command center in the build list.
	Bool foundInBuildList = false;
	Coord3D buildPos;
	BuildListInfo *cur = list;
	while (cur) {
		const ThingTemplate *tTemplate = TheThingFactory->findTemplate(cur->getTemplateName());
		if (tTemplate && tTemplate->isKindOf(KINDOF_COMMANDCENTER)) {
			foundInBuildList = true;
			buildPos = *cur->getLocation();
			cur->setInitiallyBuilt(true);
		}
		cur = cur->getNext();
	}
	Region3D bounds;
	TheTerrainLogic->getMaximumPathfindExtent(&bounds);
	/* calculate section of 3x3 grid:
		6 7 8
		3 4 5
		0 1 2 */

	Int gridIndex = 0;
	if (startPos.x > bounds.lo.x + bounds.width()/3) {
		gridIndex++;
	}
	if (startPos.x > bounds.lo.x + 2*bounds.width()/3) {
		gridIndex++;
	}

	if (startPos.y > bounds.lo.y + bounds.height()/3) {
		gridIndex+=3;
	}
	if (startPos.y > bounds.lo.y + 2*bounds.height()/3) {
		gridIndex+=3;
	}

	Real angle = 0;
	if (TheAI->getAiData()->m_rotateSkirmishBases) {
		switch (gridIndex) {
			case 0 : angle = 0; break;
			case 1 : angle = PI/4; break;// 45 degrees.
			case 2 : angle = PI/2; break; // 90 degrees;
			case 3 : angle = -PI/4; break; // -45 degrees.
			case 4 : angle = 0; break;
			case 5 : angle = 3*PI/4; break; // 135 degrees.
			case 6 : angle = -PI/2; break; // -90 degrees;
			case 7 : angle = -3*PI/4; break; // -135 degrees.
			case 8 : angle = PI; break; // 180 degrees.
		}
	}

	angle += 3*PI/4;

	Real s = sin(angle);
	Real c = cos(angle);

	cur = list;
	while (cur) {
		const ThingTemplate *tTemplate = TheThingFactory->findTemplate(list->getTemplateName());
		if (tTemplate && tTemplate->isKindOf(KINDOF_COMMANDCENTER)) {
			foundInBuildList = true;
			Coord3D curPos = *cur->getLocation();
			// Transform to new coords.
			curPos.x -= buildPos.x;
			curPos.y -= buildPos.y;
			Real newX = curPos.x*c - curPos.y*s;
			Real newY = curPos.y*c + curPos.x*s;
			curPos.x = newX + startPos.x;
			curPos.y = newY + startPos.y;
			cur->setLocation(curPos);
			cur->setAngle(cur->getAngle());
		}
		cur = cur->getNext();
	}

}



//----------------------------------------------------------------------------------------------------------
/**
 * Find any things that build stuff & add them to the build list.  Then build any initially built
 * buildings.
 */
void AISkirmishPlayer::newMap()
{
	cancelCoordinatedAttack();
	m_frameNextAllyAssistCheck = 0;
	m_frameNextCoordCheck = 0;
	m_frameNextBotChat = 0;
	m_frameNextBotPing = 0;
	m_requestedAssistUntilFrame = 0;
	m_requestedAssistPlayerIndex = PLAYER_INDEX_INVALID;
	m_requestedAssistTarget.zero();
	m_coordRallyPoint.zero();
	m_frameLastPlanChange = 0;

	/* Get our proper build list. */
	AsciiString mySide = m_player->getSide();
	DEBUG_LOG(("AI Player side is %s", mySide.str()));
	const AISideBuildList *build = TheAI->getAiData()->m_sideBuildLists;
	while (build) {
		if (build->m_side == mySide) {
			BuildListInfo *buildList = build->m_buildList->duplicate();
			adjustBuildList(buildList); // adjust to  our start position.
			m_player->setBuildList(buildList);
			computeCenterAndRadiusOfBase(&m_baseCenter, &m_baseRadius);
			break;
		}
		build = build->m_next;
	}
	DEBUG_ASSERTLOG(build!=nullptr, ("Couldn't find build list for skirmish player."));

	// Build any with the initially built flag.
	for( BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
	{
		AsciiString name = info->getTemplateName();
		if (name.isEmpty()) continue;
		const ThingTemplate *bldgPlan = TheThingFactory->findTemplate( name );
		if (!bldgPlan) {
			DEBUG_LOG(("*** ERROR - Build list building '%s' doesn't exist.", name.str()));
			continue;
		}
		if (info->isInitiallyBuilt()) {
			buildStructureNow(bldgPlan, info);
		} else {
			info->incrementNumRebuilds(); // the initial build in the normal build list consumes a rebuild, so add one.
		}
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * Queues up a dozer.
 */
void AISkirmishPlayer::queueDozer()
{
	AIPlayer::queueDozer();
}

//----------------------------------------------------------------------------------------------------------
/**
 * Finds a dozer that isn't building or collecting resources.
 */
Object * AISkirmishPlayer::findDozer( const Coord3D *pos )
{
	return AIPlayer::findDozer(pos);
}


//----------------------------------------------------------------------------------------------------------
/**
 * Find a good spot to fire a superweapon.
 */
void AISkirmishPlayer::computeSuperweaponTarget(const SpecialPowerTemplate *power, Coord3D *retPos, Int playerNdx, Real weaponRadius)
{

	Region2D bounds;
	getPlayerStructureBounds(&bounds, playerNdx);

	if (power->getName() == "SuperweaponClusterMines") {
		// hackus brutus - mine the entrances to our base.
		AsciiString pathLabel;
		Int mode = GameLogicRandomValue(0, 2);
		if (mode==1) {
				pathLabel.format("%s%d", SKIRMISH_FLANK, m_player->getMpStartIndex()+1);
		}	else if (mode==2) {
				pathLabel.format("%s%d", SKIRMISH_BACKDOOR, m_player->getMpStartIndex()+1);
		}	else {
			pathLabel.format("%s%d", SKIRMISH_CENTER, m_player->getMpStartIndex()+1);
		}

		Coord3D goalPos = m_baseCenter;
		Waypoint *way = TheTerrainLogic->getClosestWaypointOnPath( &goalPos, pathLabel );
		if (way) {
			goalPos = *way->getLocation();
		} else {
			Region2D bounds;
			getPlayerStructureBounds(&bounds, getMyEnemyPlayerIndex());
			goalPos.x = bounds.lo.x + bounds.width()/2;
			goalPos.y = bounds.lo.y + bounds.height()/2;
		}
		Coord2D offset;
		offset.x = goalPos.x-m_baseCenter.x;
		offset.y = goalPos.y-m_baseCenter.y;
		offset.normalize();
		offset.x *= m_baseRadius;
		offset.y *= m_baseRadius;
		*retPos = m_baseCenter;
		retPos->x += offset.x;
		retPos->y += offset.y;
		retPos->z = TheTerrainLogic->getGroundHeight(retPos->x, retPos->y);
		return;
	}
	AIPlayer::computeSuperweaponTarget(power, retPos, playerNdx, weaponRadius);

}

// TheSuperHackers @feature Alliance cooperation ////////////////////////////////////////////////////////

//----------------------------------------------------------------------------------------------------------
/**
 * React to allied chat commands so human players can direct this bot.
 */
void AISkirmishPlayer::onChatMessage(const Player *sender, const UnicodeString &text, Int playerMask)
{
	if (sender == nullptr || sender == m_player || m_player == nullptr)
	{
		return;
	}

	Int myPlayerIndex = m_player->getPlayerIndex();
	if (myPlayerIndex < 0 || myPlayerIndex >= 8 * sizeof(UnsignedInt))
	{
		return;
	}

	// Check alliance in BOTH directions - respond if either party considers the other an ally.
	const Bool botConsidersSenderAlly = (m_player->getRelationship(sender->getDefaultTeam()) == ALLIES);
	const Bool senderConsidersBotAlly = (sender->getRelationship(m_player->getDefaultTeam()) == ALLIES);
	const Bool inMask = (static_cast<UnsignedInt>(playerMask) & (1u << myPlayerIndex)) != 0u;
	const Bool allied = botConsidersSenderAlly || senderConsidersBotAlly;

	if (!inMask)
	{
		if (!allied)
		{
			BotAILog("%s | onChatMessage MASKED OUT and no alliance (mask=0x%X)",
				getBotLogPrefix(m_player).str(), (unsigned)playerMask);
			return;
		}
		// Allied bot outside the broadcast mask - process the command anyway.
		BotAILog("%s | onChatMessage: ally outside broadcast mask (0x%X) - processing command",
			getBotLogPrefix(m_player).str(), (unsigned)playerMask);
	}
	else if (!allied)
	{
		BotAILog("%s | onChatMessage NOT ALLY of sender '%s' (bot->sender=%d, sender->bot=%d)",
			getBotLogPrefix(m_player).str(), getPlayerNameAscii(sender).str(),
			(int)botConsidersSenderAlly, (int)senderConsidersBotAlly);
		return;
	}

	AsciiString command;
	command.translate(text);
	command.trim();
	command.toLower();
	if (command.isEmpty())
	{
		return;
	}

	UnsignedInt curFrame = TheGameLogic->getFrame();
	DEBUG_LOG(("BotAI [%s]: onChatMessage from '%s': '%s'",
		getPlayerNameAscii(m_player).str(), getPlayerNameAscii(sender).str(), command.str()));
	BotAILog("%s | CHAT RECEIVED from '%s': \"%s\"  (planLock=%d, coordPending=%d, underAttack=%d, idleTeams=%d)",
		getBotLogPrefix(m_player).str(), getPlayerNameAscii(sender).str(), command.str(),
		(int)(curFrame < m_frameLastPlanChange + PLAN_CHANGE_COOLDOWN_FRAMES),
		(int)m_coordAttackPending, (int)isBaseUnderAttack(), (int)hasIdleCombatTeams());

	// --- Color-prefix targeting: "teal move center" -> only Teal responds; "move center" -> all ---
	Bool isDirectlyTargeted = false;
	{
		AsciiString myColorLower = getPlayerColorName(m_player);
		myColorLower.toLower();
		static const char * const s_colorNames[] = {
			"red", "blue", "green", "orange", "yellow", "teal", "purple", "pink", "white", "black", nullptr
		};
		for (Int ci = 0; s_colorNames[ci] != nullptr; ++ci)
		{
			const char *cname = s_colorNames[ci];
			const Int clen = (Int)strlen(cname);
			const char *cmdStr = command.str();
			if (strncmp(cmdStr, cname, clen) == 0 && cmdStr[clen] == ' ')
			{
				if (strcmp(cname, myColorLower.str()) != 0)
				{
					BotAILog("%s | color-prefix '%s': not my color, ignoring",
						getBotLogPrefix(m_player).str(), cname);
					return;
				}
				isDirectlyTargeted = true;
				AsciiString stripped;
				stripped.format("%s", cmdStr + clen + 1);
				BotAILog("%s | color-prefix '%s': targeted at me -> cmd='%s'",
					getBotLogPrefix(m_player).str(), cname, stripped.str());
				command = stripped;
				break;
			}
		}
	}

	// --- "go away" / "stand down" / "cancel" / "stop" ---
	if (containsText(command, "stand down") || containsText(command, "cancel")
		|| containsText(command, "never mind") || containsText(command, "go away") || containsText(command, "stop"))
	{
		BotAILog("%s | MATCHED: stand-down command -> cancelling all plans", getBotLogPrefix(m_player).str());
		cancelCoordinatedAttack();
		m_requestedAssistUntilFrame = 0;
		m_requestedAssistPlayerIndex = PLAYER_INDEX_INVALID;
		m_requestedAssistTarget.zero();
		m_frameNextMoveReissue = 0;
		m_frameLastPlanChange = curFrame;
		sendBotChat("Standing down.", false, true);
		return;
	}

	// --- "status" / "report" ---
	if (containsText(command, "status") || containsText(command, "report"))
	{
		AsciiString status;
		if (isBaseUnderAttack())
		{
			status = "Defending my base.";
		}
		else if (!m_coordAttackPending && m_holdAttackUntilFrame > curFrame)
		{
			status = "Moving to the marked position.";
		}
		else if (m_coordAttackPending && m_coordAttackEnemyPlayerIndex != PLAYER_INDEX_INVALID)
		{
			Player *enemy = ThePlayerList->getNthPlayer(m_coordAttackEnemyPlayerIndex);
			status.format("Attacking %s.", getPlayerColorName(enemy).str());
		}
		else if (m_requestedAssistUntilFrame > curFrame && m_requestedAssistPlayerIndex != PLAYER_INDEX_INVALID)
		{
			Player *ally = ThePlayerList->getNthPlayer(m_requestedAssistPlayerIndex);
			status.format("Supporting %s.", getPlayerColorName(ally).str());
		}
		else
		{
			Player *enemy = getAiEnemy();
			if (enemy != nullptr)
				status.format("Watching %s.", getPlayerColorName(enemy).str());
			else
				status = "Building up.";
		}
		sendBotChat(status, false, true);
		return;
	}

	// --- "ping" / "where are you" / "location" ---
	if (containsText(command, "ping") || containsText(command, "where are you") || containsText(command, "location"))
	{
		AsciiString pingText;
		if (!m_coordAttackPending && m_holdAttackUntilFrame > curFrame)
		{
			pingText = "Marking my move order.";
		}
		else if (m_coordAttackPending && m_coordAttackEnemyPlayerIndex != PLAYER_INDEX_INVALID)
		{
			Player *enemy = ThePlayerList->getNthPlayer(m_coordAttackEnemyPlayerIndex);
			pingText.format("Marking attack on %s.", getPlayerColorName(enemy).str());
		}
		else if (m_requestedAssistUntilFrame > curFrame && m_requestedAssistPlayerIndex != PLAYER_INDEX_INVALID)
		{
			Player *ally = ThePlayerList->getNthPlayer(m_requestedAssistPlayerIndex);
			pingText.format("Marking support for %s.", getPlayerColorName(ally).str());
		}
		else
		{
			pingText = "Marking my base.";
		}

		sendBotChat(pingText, true, true);
		return;
	}

	// --- "help me" / "assist me" / "support me" ---
	if (containsText(command, "help me") || containsText(command, "assist me")
		|| containsText(command, "defend me") || containsText(command, "cover me")
		|| containsText(command, "support me") || containsText(command, "need backup")
		|| containsText(command, "backup me"))
	{
		Coord3D assistPos;
		if (!const_cast<Player *>(sender)->getAiBaseCenter(&assistPos))
		{
			Region2D bounds;
			getPlayerStructureBounds(&bounds, sender->getPlayerIndex());
			assistPos.x = bounds.lo.x + bounds.width() / 2;
			assistPos.y = bounds.lo.y + bounds.height() / 2;
			assistPos.z = TheTerrainLogic->getGroundHeight(assistPos.x, assistPos.y);
		}

		cancelCoordinatedAttack();
		m_requestedAssistPlayerIndex = sender->getPlayerIndex();
		m_requestedAssistUntilFrame = curFrame + HUMAN_ASSIST_DURATION_FRAMES;
		m_requestedAssistTarget = assistPos;
		m_frameLastPlanChange = curFrame;
		m_frameNextMoveReissue = curFrame + 30 * LOGICFRAMES_PER_SECOND; // backup guard refresh in 30s

		Int unitsSent = sendAllCombatTeamsToPosition(&assistPos);
		BotAILog("%s | MATCHED: assist-me -> %d units attack-moving to sender base (%.0f,%.0f)",
			getBotLogPrefix(m_player).str(), unitsSent, assistPos.x, assistPos.y);
		if (unitsSent > 0)
		{
			sendBotChat("On my way to support you.", true, true);
		}
		else
		{
			sendBotChat("I'm stretched thin, but I'll support you when I can.", false, true);
		}
		return;
	}

	const Bool wantsMoveOrder = equalsText(command, "move")
		|| startsWithText(command, "move ")
		|| startsWithText(command, "go ")
		|| startsWithText(command, "go to")
		|| containsText(command, "come here")
		|| containsText(command, "meet")
		|| containsText(command, "rally")
		|| containsText(command, "regroup");

	// --- "move [direction/color]" / "come here" / "rally" ---
	if (wantsMoveOrder)
	{
		BotAILog("%s | MATCHED: move order  command='%s'", getBotLogPrefix(m_player).str(), command.str());
		Coord3D movePos;
		AsciiString moveLabel;
		Bool isPlayerTarget = false;
		Bool haveMovePos = tryGetDirectionalTargetPosition(command, &movePos);

		if (haveMovePos)
		{
			moveLabel = getDirectionLabel(command);
			BotAILog("%s |   directional parse OK -> label='%s' pos=(%.0f,%.0f)", getBotLogPrefix(m_player).str(), moveLabel.str(), movePos.x, movePos.y);
		}
		else if (containsText(command, "come here") || containsText(command, "meet") || containsText(command, "rally"))
		{
			if (!const_cast<Player *>(sender)->getAiBaseCenter(&movePos))
			{
				Region2D bounds;
				getPlayerStructureBounds(&bounds, sender->getPlayerIndex());
				movePos.x = bounds.lo.x + bounds.width() / 2;
				movePos.y = bounds.lo.y + bounds.height() / 2;
				movePos.z = TheTerrainLogic->getGroundHeight(movePos.x, movePos.y);
			}
			haveMovePos = true;
			BotAILog("%s |   'come here' -> moving to sender base at (%.0f,%.0f)", getBotLogPrefix(m_player).str(), movePos.x, movePos.y);
			// "come here" moves toward the sender - announce as "On my way."
		}
		else
		{
			// Try to find a player by color name: "move red", "move blue base", etc.
			Player *colorTarget = findPlayerByColorName(command);
			BotAILog("%s |   no direction keyword  findPlayerByColorName='%s'",
				getBotLogPrefix(m_player).str(), colorTarget ? getPlayerNameAscii(colorTarget).str() : "NULL");
			if (colorTarget != nullptr)
			{
				Region2D bounds;
				getPlayerStructureBounds(&bounds, colorTarget->getPlayerIndex());
				movePos.x = bounds.lo.x + bounds.width() / 2;
				movePos.y = bounds.lo.y + bounds.height() / 2;
				movePos.z = TheTerrainLogic->getGroundHeight(movePos.x, movePos.y);
				moveLabel = getPlayerColorName(colorTarget);
				isPlayerTarget = true;
				haveMovePos = true;
				BotAILog("%s |   color-name match '%s' -> pos=(%.0f,%.0f)", getBotLogPrefix(m_player).str(), moveLabel.str(), movePos.x, movePos.y);
			}
		}

		if (!haveMovePos)
		{
			BotAILog("%s |   FAILED to resolve move position -> telling player", getBotLogPrefix(m_player).str());
			if (isFirstAlliedBotForPlayer(sender))
				sendBotChat("Tell me where to move: center, top, bottom, left, or right.", false, true);
			return;
		}

		cancelCoordinatedAttack();
		m_requestedAssistUntilFrame = 0;
		m_requestedAssistPlayerIndex = PLAYER_INDEX_INVALID;
		m_requestedAssistTarget.zero();
		m_holdAttackUntilFrame = curFrame + DIRECT_MOVE_HOLD_FRAMES;
		m_coordRallyPoint = movePos;
		m_frameLastPlanChange = curFrame;
		m_frameNextMoveReissue = curFrame + 30 * LOGICFRAMES_PER_SECOND; // backup guard refresh in 30s

		Int unitsSent = sendAllCombatTeamsToPosition(&movePos);
		BotAILog("%s |   sendAllCombatTeamsToPosition -> %d units attack-moving to (%.0f,%.0f)  holdUntil=%u",
			getBotLogPrefix(m_player).str(), unitsSent, movePos.x, movePos.y, m_holdAttackUntilFrame);
		if (unitsSent > 0)
		{
			AsciiString moveMsg;
			if (isPlayerTarget && !moveLabel.isEmpty())
				moveMsg.format("Moving toward %s.", moveLabel.str());
			else if (!moveLabel.isEmpty())
				moveMsg.format("Moving to %s.", moveLabel.str());
			else
				moveMsg = "On my way.";
			sendBotChat(moveMsg, true, true);
		}
		else
		{
			sendBotChat("No units available.", false, true);
		}
		return;
	}

	const Bool wantsAttackOrder = containsText(command, "attack now")
		|| containsText(command, "push now")
		|| equalsText(command, "attack")
		|| startsWithText(command, "attack ")
		|| equalsText(command, "push")
		|| startsWithText(command, "push ")
		|| equalsText(command, "rush")
		|| startsWithText(command, "rush ");

	// --- "attack [direction/color]" / "push" / "rush" ---
	if (wantsAttackOrder)
	{
		BotAILog("%s | MATCHED: attack order  command='%s'", getBotLogPrefix(m_player).str(), command.str());

		m_requestedAssistUntilFrame = 0;
		m_requestedAssistPlayerIndex = PLAYER_INDEX_INVALID;
		m_requestedAssistTarget.zero();

		// Priority: explicit color name > directional > current tracked enemy.
		Player *enemy = findEnemyByColorName(command);
		BotAILog("%s |   findEnemyByColorName='%s'", getBotLogPrefix(m_player).str(), enemy ? getPlayerColorName(enemy).str() : "NULL");
		if (enemy == nullptr)
		{
			enemy = findEnemyInDirection(command);
			BotAILog("%s |   findEnemyInDirection='%s'", getBotLogPrefix(m_player).str(), enemy ? getPlayerColorName(enemy).str() : "NULL");
		}
		if (enemy == nullptr)
		{
			enemy = getAiEnemy();
			if (enemy == nullptr)
			{
				acquireEnemy();
				enemy = getAiEnemy();
			}
			BotAILog("%s |   getAiEnemy fallback='%s'", getBotLogPrefix(m_player).str(), enemy ? getPlayerColorName(enemy).str() : "NULL");
		}

		if (enemy == nullptr)
		{
			BotAILog("%s |   NO ENEMY FOUND -> aborting attack", getBotLogPrefix(m_player).str());
			if (isFirstAlliedBotForPlayer(sender))
				sendBotChat("No target found.", false, true);
			return;
		}

		Coord3D targetPos;
		Region2D bounds;
		getPlayerStructureBounds(&bounds, enemy->getPlayerIndex());
		targetPos.x = bounds.lo.x + bounds.width() / 2;
		targetPos.y = bounds.lo.y + bounds.height() / 2;
		targetPos.z = TheTerrainLogic->getGroundHeight(targetPos.x, targetPos.y);

		BotAILog("%s |   target='%s'  pos=(%.0f,%.0f)  isFirstBot=%d  idleTeams=%d",
			getBotLogPrefix(m_player).str(), getPlayerNameAscii(enemy).str(),
			targetPos.x, targetPos.y, (int)isFirstAlliedBotForPlayer(sender), (int)hasIdleCombatTeams());

		// Cancel any staged coordination and lock in this human command for 3 minutes.
		cancelCoordinatedAttack();
		m_frameLastPlanChange = curFrame;

		// Launch units directly - no staging delay for explicit human commands.
		Int unitsSent = sendAllCombatTeamsToPosition(&targetPos);
		BotAILog("%s |   sendAllCombatTeamsToPosition -> %d units dispatched to (%.0f,%.0f)",
			getBotLogPrefix(m_player).str(), unitsSent, targetPos.x, targetPos.y);

		// Every targeted bot announces (no isFirstAlliedBot gate).
		{
			AsciiString msg;
			msg.format("Attacking %s.", getPlayerColorName(enemy).str());
			// Temporarily set attack state so sendBotChat pings the target, not the base.
			m_coordAttackTarget = targetPos;
			m_coordAttackEnemyPlayerIndex = enemy->getPlayerIndex();
			m_coordAttackPending = true;
			BotAILog("%s |   sending chat+ping  msg='%s'  pingPos=(%.0f,%.0f)", getBotLogPrefix(m_player).str(), msg.str(), targetPos.x, targetPos.y);
			sendBotChat(msg, true, true);
			m_coordAttackPending = false;
			m_coordAttackEnemyPlayerIndex = PLAYER_INDEX_INVALID;
			m_coordAttackTarget.zero();
		}
		return;
	}

	// --- "help" / "commands" ---
	if (containsText(command, "help") || containsText(command, "commands") || containsText(command, "what can you do"))
	{
		if (isFirstAlliedBotForPlayer(sender))
		{
			sendBotChat("Commands: move center, come here, attack [direction], assist me, ping, status, stop", false, true);
		}
		return;
	}

	// --- Unknown command - only one bot responds ---
	if (isFirstAlliedBotForPlayer(sender))
	{
		sendBotChat("Commands: move center, come here, attack [direction], assist me, ping, status, stop", false, true);
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * Returns true if our base structures have been damaged in the last 10 seconds.
 */
Bool AISkirmishPlayer::isBaseUnderAttack() const
{
	const Int ATTACK_RECENCY = 10 * LOGICFRAMES_PER_SECOND;
	UnsignedInt curFrame = TheGameLogic->getFrame();

	if (m_player->getAttackedFrame() + ATTACK_RECENCY > curFrame) {
		// Player was attacked recently - check if it was a base structure.
		for( const BuildListInfo *info = m_player->getBuildList(); info; info = info->getNext() )
		{
			Object *bldg = TheGameLogic->findObjectByID( info->getObjectID() );
			if (bldg == nullptr) continue;
			if (bldg->getControllingPlayer() != m_player) continue;
			BodyModuleInterface *body = bldg->getBodyModule();
			if (body) {
				const DamageInfo *dmg = body->getLastDamageInfo();
				if (dmg && !dmg->out.m_noEffect) {
					if (body->getLastDamageTimestamp() + ATTACK_RECENCY > curFrame) {
						DEBUG_LOG(("BotAI [%s]: isBaseUnderAttack -> TRUE (building hit, frame=%d)", getPlayerNameAscii(m_player).str(), (Int)curFrame));
						return true;
					}
				}
			}
		}
	}
	return false;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Cancel the current staged attack.
 */
void AISkirmishPlayer::cancelCoordinatedAttack()
{
	DEBUG_LOG(("BotAI [%s]: cancelCoordinatedAttack (pending=%d, enemyIdx=%d)",
		m_player ? getPlayerNameAscii(m_player).str() : "?",
		(Int)m_coordAttackPending, m_coordAttackEnemyPlayerIndex));
	m_coordAttackPending = false;
	m_coordAttackWaitStartFrame = 0;
	m_holdAttackUntilFrame = 0;
	m_coordAttackEnemyPlayerIndex = PLAYER_INDEX_INVALID;
	m_coordAttackTarget.zero();
	m_coordRallyPoint.zero();
}

//----------------------------------------------------------------------------------------------------------
/**
 * Mark this bot as part of a coordinated attack.
 */
void AISkirmishPlayer::queueCoordinatedAttack(const Coord3D *targetPos, const Player *enemy, UnsignedInt waitStartFrame)
{
	if (targetPos == nullptr || enemy == nullptr)
	{
		return;
	}

	DEBUG_LOG(("BotAI [%s]: queueCoordinatedAttack on '%s' at (%.0f,%.0f) waitStart=%d",
		getPlayerNameAscii(m_player).str(), getPlayerNameAscii(enemy).str(),
		targetPos->x, targetPos->y, (Int)waitStartFrame));
	Bool wasPending = m_coordAttackPending;
	m_coordAttackPending = true;
	m_coordAttackTarget = *targetPos;
	m_coordAttackEnemyPlayerIndex = enemy->getPlayerIndex();
	if (waitStartFrame == 0)
	{
		waitStartFrame = TheGameLogic->getFrame();
	}

	if (m_coordAttackWaitStartFrame == 0 || waitStartFrame < m_coordAttackWaitStartFrame)
	{
		m_coordAttackWaitStartFrame = waitStartFrame;
	}

	m_holdAttackUntilFrame = m_coordAttackWaitStartFrame + GROUP_ATTACK_WAIT_FRAMES;

	// Calculate rally point for grouping: just outside base, toward the target.
	Coord2D dir;
	dir.x = targetPos->x - m_baseCenter.x;
	dir.y = targetPos->y - m_baseCenter.y;
	Real len = sqr(dir.x) + sqr(dir.y);
	if (len > 1.0f)
	{
		len = sqrt(len);
		dir.x /= len;
		dir.y /= len;
	}
	m_coordRallyPoint = m_baseCenter;
	m_coordRallyPoint.x += dir.x * m_baseRadius * 1.3f;
	m_coordRallyPoint.y += dir.y * m_baseRadius * 1.3f;
	m_coordRallyPoint.z = TheTerrainLogic->getGroundHeight(m_coordRallyPoint.x, m_coordRallyPoint.y);

	if (!wasPending)
	{
		Int sent = sendAllCombatTeamsToPosition(&m_coordRallyPoint);
		BotAILog("%s | GROUP: staging order -> %d units rally to (%.0f,%.0f)",
			getBotLogPrefix(m_player).str(), sent, m_coordRallyPoint.x, m_coordRallyPoint.y);

		// Only the first bot to initiate announces; others stay quiet when joining.
		AsciiString msg;
		msg.format("Grouping to attack %s.", getPlayerColorName(enemy).str());
		sendBotChat(msg, true, false);
	}
	else
	{
		// Keep newly-idle teams moving toward staging without re-pathing every unit.
		rallyTeamsToStagingPoint();
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * Broadcast an in-character bot chat line in the bot's player color.
 */
void AISkirmishPlayer::sendBotChat(const AsciiString &message, Bool ping, Bool force)
{
	if (TheInGameUI == nullptr || message.isEmpty())
	{
		BotAILog("%s | sendBotChat BLOCKED: %s", getBotLogPrefix(m_player).str(),
			TheInGameUI == nullptr ? "TheInGameUI is null" : "empty message");
		return;
	}

	Bool visibleToLocalPlayer = true;
	// Only show messages to the local player if there's any alliance in either direction.
	// Cooldowns are still advanced even when hidden so multiplayer clients keep
	// identical AI state regardless of which human player is local.
	const Player *localPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	if (localPlayer != nullptr && localPlayer != m_player)
	{
		const Bool botConsidersLocalAlly = (m_player->getRelationship(localPlayer->getDefaultTeam()) == ALLIES);
		const Bool localConsidersBotAlly = (localPlayer->getRelationship(m_player->getDefaultTeam()) == ALLIES);
		if (!botConsidersLocalAlly && !localConsidersBotAlly)
			visibleToLocalPlayer = false;
	}

	UnsignedInt curFrame = TheGameLogic->getFrame();
	UnsignedInt *nextAllowedFrame = ping ? &m_frameNextBotPing : &m_frameNextBotChat;
	UnsignedInt cooldown = ping ? BOT_PING_COOLDOWN_FRAMES : BOT_CHAT_COOLDOWN_FRAMES;

	if (!force && curFrame < *nextAllowedFrame)
	{
		DEBUG_LOG(("BotAI [%s]: sendBotChat THROTTLED: '%s'",
			m_player ? getPlayerNameAscii(m_player).str() : "?", message.str()));
		BotAILog("%s | CHAT THROTTLED  msg='%s'  ping=%d  nextAllowed=%u  cur=%u",
			getBotLogPrefix(m_player).str(), message.str(), (int)ping, *nextAllowedFrame, curFrame);
		return;
	}

	*nextAllowedFrame = curFrame + cooldown;

	if (!visibleToLocalPlayer)
	{
		return;
	}

	UnicodeString unicodeMessage;
	unicodeMessage.translate(message);
	UnicodeString finalLine;
	finalLine.format(L"%ls: %ls", m_player->getPlayerDisplayName().str(), unicodeMessage.str());

	RGBColor rgb;
	rgb.setFromInt(m_player->getPlayerColor() & 0x00FFFFFF);
	TheInGameUI->messageColor(&rgb, UnicodeString(L"%ls"), finalLine.str());

	BotAILog("%s | CHAT OUT: \"%s\"  (ping=%d)", getBotLogPrefix(m_player).str(), message.str(), (int)ping);

	// Place an actual radar ping at the relevant position when ping is requested.
	// Uses RADAR_EVENT_INFORMATION so the ping plays an audio cue and flashes visibly.
	if (ping && TheRadar != nullptr)
	{
		Coord3D pingPos;
		Bool havePingPos = false;

		if (m_coordAttackPending && m_coordAttackEnemyPlayerIndex != PLAYER_INDEX_INVALID)
		{
			pingPos = m_coordAttackTarget;
			havePingPos = true;
			BotAILog("%s |   ping -> ATTACK TARGET  (%.0f,%.0f)  enemy=%d", getBotLogPrefix(m_player).str(), pingPos.x, pingPos.y, m_coordAttackEnemyPlayerIndex);
		}
		else if (m_requestedAssistUntilFrame > curFrame && m_requestedAssistPlayerIndex != PLAYER_INDEX_INVALID)
		{
			pingPos = m_requestedAssistTarget;
			havePingPos = true;
			BotAILog("%s |   ping -> ASSIST TARGET  (%.0f,%.0f)", getBotLogPrefix(m_player).str(), pingPos.x, pingPos.y);
		}
		else if (!m_coordAttackPending && m_holdAttackUntilFrame > curFrame)
		{
			pingPos = m_coordRallyPoint;
			havePingPos = true;
			BotAILog("%s |   ping -> MOVE RALLY POINT  (%.0f,%.0f)", getBotLogPrefix(m_player).str(), pingPos.x, pingPos.y);
		}
		else
		{
			pingPos = m_baseCenter;
			havePingPos = true;
			BotAILog("%s |   ping -> BASE (no active order)  (%.0f,%.0f)  [NOTE: coordPending=%d holdUntil=%u curFrame=%u]",
				getBotLogPrefix(m_player).str(), pingPos.x, pingPos.y,
				(int)m_coordAttackPending, m_holdAttackUntilFrame, curFrame);
		}

		if (havePingPos)
		{
			BotAILog("%s |   ping FIRED at (%.0f,%.0f)", getBotLogPrefix(m_player).str(), pingPos.x, pingPos.y);
			// Use createEvent (bright yellow from color table) instead of createPlayerEvent
			// (player color) so the ping is highly visible on the minimap.
			TheRadar->createEvent(&pingPos, RADAR_EVENT_INFORMATION, 12.0f);
		}
	}
	else if (ping && TheRadar == nullptr)
	{
		BotAILog("%s |   ping BLOCKED: TheRadar is null", getBotLogPrefix(m_player).str());
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * Pick the best allied support target, prioritizing explicit human requests.
 */
Bool AISkirmishPlayer::chooseAssistTarget(Player **targetPlayer, Coord3D *targetPos, Bool *fromHumanRequest)
{
	if (targetPlayer != nullptr)
	{
		*targetPlayer = nullptr;
	}
	if (targetPos != nullptr)
	{
		targetPos->zero();
	}
	if (fromHumanRequest != nullptr)
	{
		*fromHumanRequest = false;
	}

	UnsignedInt curFrame = TheGameLogic->getFrame();
	if (m_requestedAssistUntilFrame > curFrame && m_requestedAssistPlayerIndex != PLAYER_INDEX_INVALID)
	{
		Player *requestedPlayer = ThePlayerList->getNthPlayer(m_requestedAssistPlayerIndex);
		if (requestedPlayer != nullptr
			&& requestedPlayer != m_player
			&& requestedPlayer->hasAnyObjects()
			&& m_player->getRelationship(requestedPlayer->getDefaultTeam()) == ALLIES)
		{
			if (targetPlayer != nullptr)
			{
				*targetPlayer = requestedPlayer;
			}
			if (targetPos != nullptr)
			{
				*targetPos = m_requestedAssistTarget;
			}
			if (fromHumanRequest != nullptr)
			{
				*fromHumanRequest = true;
			}
			return true;
		}

		m_requestedAssistUntilFrame = 0;
		m_requestedAssistPlayerIndex = PLAYER_INDEX_INVALID;
		m_requestedAssistTarget.zero();
	}

	Player *bestTarget = nullptr;
	UnsignedInt bestAttackFrame = 0;
	Bool bestTargetIsHuman = false;
	const Int allyAttackRecency = 15 * LOGICFRAMES_PER_SECOND;

	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *otherPlayer = ThePlayerList->getNthPlayer(i);
		if (otherPlayer == nullptr || otherPlayer == m_player)
		{
			continue;
		}

		if (!otherPlayer->hasAnyObjects())
		{
			continue;
		}

		if (m_player->getRelationship(otherPlayer->getDefaultTeam()) != ALLIES)
		{
			continue;
		}

		if (otherPlayer->getAttackedFrame() + allyAttackRecency <= curFrame)
		{
			continue;
		}

		Bool isHumanAlly = !otherPlayer->isSkirmishAIPlayer();
		if (bestTarget == nullptr
			|| (isHumanAlly && !bestTargetIsHuman)
			|| (isHumanAlly == bestTargetIsHuman && otherPlayer->getAttackedFrame() > bestAttackFrame))
		{
			bestTarget = otherPlayer;
			bestAttackFrame = otherPlayer->getAttackedFrame();
			bestTargetIsHuman = isHumanAlly;
		}
	}

	if (bestTarget == nullptr)
	{
		return false;
	}

	Coord3D bestTargetPos;
	if (!bestTarget->getAiBaseCenter(&bestTargetPos))
	{
		Region2D bounds;
		getPlayerStructureBounds(&bounds, bestTarget->getPlayerIndex());
		bestTargetPos.x = bounds.lo.x + bounds.width() / 2;
		bestTargetPos.y = bounds.lo.y + bounds.height() / 2;
		bestTargetPos.z = TheTerrainLogic->getGroundHeight(bestTargetPos.x, bestTargetPos.y);
	}

	if (targetPlayer != nullptr)
	{
		*targetPlayer = bestTarget;
	}
	if (targetPos != nullptr)
	{
		*targetPos = bestTargetPos;
	}
	return true;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Returns true if we have at least one idle combat team available to deploy.
 */
Bool AISkirmishPlayer::hasIdleCombatTeams() const
{
	for (Player::PlayerTeamList::const_iterator teamProtoIt = m_player->getPlayerTeams()->begin();
		teamProtoIt != m_player->getPlayerTeams()->end();
		++teamProtoIt)
	{
		for (DLINK_ITERATOR<Team> iter = (*teamProtoIt)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
		{
			if (isDeployableCombatTeam(iter.cur()))
			{
				return true;
			}
		}
	}

	return false;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Returns true if we have at least one combat team with controllable units,
 * regardless of whether it is currently idle.
 */
Bool AISkirmishPlayer::hasCombatTeams() const
{
	for (Player::PlayerTeamList::const_iterator teamProtoIt = m_player->getPlayerTeams()->begin();
		teamProtoIt != m_player->getPlayerTeams()->end();
		++teamProtoIt)
	{
		for (DLINK_ITERATOR<Team> iter = (*teamProtoIt)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
		{
			const Team *team = iter.cur();
			if (team == nullptr || !team->hasAnyUnits())
			{
				continue;
			}

			for (DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance())
			{
				Object *obj = objIter.cur();
				if (obj == nullptr)
				{
					continue;
				}

				if (obj->isKindOf(KINDOF_HARVESTER) || obj->isKindOf(KINDOF_DOZER)
					|| obj->isKindOf(KINDOF_STRUCTURE) || obj->isKindOf(KINDOF_COMMANDCENTER))
				{
					continue;
				}

				if (obj->getAIUpdateInterface() != nullptr)
				{
					return true;
				}
			}
		}
	}

	return false;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Returns true if a team can be peeled off for support or a coordinated attack.
 */
Bool AISkirmishPlayer::isDeployableCombatTeam(const Team *team) const
{
	if (team == nullptr || !team->hasAnyUnits() || !team->isIdle())
	{
		return false;
	}

	for (DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance())
	{
		Object *obj = objIter.cur();
		if (obj == nullptr)
		{
			continue;
		}

		if (obj->isKindOf(KINDOF_HARVESTER) || obj->isKindOf(KINDOF_DOZER)
			|| obj->isKindOf(KINDOF_STRUCTURE) || obj->isKindOf(KINDOF_COMMANDCENTER))
		{
			continue;
		}

		if (obj->getAIUpdateInterface() != nullptr)
		{
			return true;
		}
	}

	return false;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Send one combat team to a position. Used when a team graduates from the
 * ready queue so we do not re-path every combat unit owned by the bot.
 */
Int AISkirmishPlayer::sendCombatTeamToPosition(Team *team, const Coord3D *targetPos, Bool guardMode, const char *debugReason, Int relatedPlayerIndex)
{
	if (team == nullptr || targetPos == nullptr || !team->hasAnyUnits())
	{
		return 0;
	}

	const UnsignedInt curFrame = TheGameLogic ? TheGameLogic->getFrame() : 0;
	const UnsignedInt lockUntil = curFrame + 32 * LOGICFRAMES_PER_SECOND;
	const char *orderReason = resolveCombatOrderReason(debugReason, guardMode ? "combat_team_guard" : "combat_team_attackmove");
	const char *orderVerb = guardMode ? "guard" : "attackmove";
	if (lockUntil > team->getCommandLockUntilFrame())
	{
		team->setCommandLockUntilFrame(lockUntil);
	}

	Int unitsSent = 0;
	AsciiString issuedSamples;
	Int issuedSampleCount = 0;
	for (DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance())
	{
		Object *obj = objIter.cur();
		if (obj == nullptr)
			continue;

		if (obj->isKindOf(KINDOF_HARVESTER) || obj->isKindOf(KINDOF_DOZER)
			|| obj->isKindOf(KINDOF_STRUCTURE) || obj->isKindOf(KINDOF_COMMANDCENTER))
		{
			continue;
		}

		AIUpdateInterface *ai = obj->getAIUpdateInterface();
		if (ai != nullptr)
		{
			noteCombatOrderContext(ai, targetPos, orderReason, orderVerb, relatedPlayerIndex);
			if (guardMode)
			{
				ai->aiGuardPosition(targetPos, GUARDMODE_NORMAL, CMD_FROM_AI);
			}
			else
			{
				ai->aiAttackMoveToPosition(targetPos, 0, CMD_FROM_AI);
			}
			appendIssuedCombatOrderSample(issuedSamples, issuedSampleCount, obj);
			++unitsSent;
		}
	}

	if (unitsSent > 0 && !issuedSamples.isEmpty())
	{
		BotAILog("%s | sendCombatTeamToPosition %s (%.0f,%.0f): sample=%s",
			getBotLogPrefix(m_player).str(), guardMode ? "guard" : "attack-move",
			targetPos->x, targetPos->y, issuedSamples.str());
	}

	return unitsSent;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Send every available idle combat team toward a target position.
 */
Int AISkirmishPlayer::sendIdleCombatTeamsToPosition(const Coord3D *targetPos, const char *debugReason, Int relatedPlayerIndex)
{
	if (targetPos == nullptr)
	{
		return 0;
	}

	const char *orderReason = resolveCombatOrderReason(debugReason, "idle_attackmove");

	Int unitsSent = 0;
	Int teamsTotal = 0;
	Int teamsSkippedNotDeployable = 0;
	Int unitsSkippedNonCombat = 0;
	Int unitsSkippedBusy = 0;
	Int unitsSkippedNoAI = 0;
	AsciiString issuedSamples;
	Int issuedSampleCount = 0;
	for (Player::PlayerTeamList::const_iterator teamProtoIt = m_player->getPlayerTeams()->begin();
		teamProtoIt != m_player->getPlayerTeams()->end();
		++teamProtoIt)
	{
		for (DLINK_ITERATOR<Team> iter = (*teamProtoIt)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
		{
			Team *team = iter.cur();
			++teamsTotal;
			if (!isDeployableCombatTeam(team))
			{
				++teamsSkippedNotDeployable;
				continue;
			}

			for (DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance())
			{
				Object *obj = objIter.cur();
				if (obj == nullptr)
					continue;

				if (obj->isKindOf(KINDOF_HARVESTER) || obj->isKindOf(KINDOF_DOZER)
					|| obj->isKindOf(KINDOF_STRUCTURE) || obj->isKindOf(KINDOF_COMMANDCENTER))
				{
					++unitsSkippedNonCombat;
					continue;
				}

				AIUpdateInterface *ai = obj->getAIUpdateInterface();
				if (ai != nullptr && ai->isIdle())
				{
					noteCombatOrderContext(ai, targetPos, orderReason, "attackmove", relatedPlayerIndex);
					ai->aiAttackMoveToPosition(targetPos, 0, CMD_FROM_AI);
					appendIssuedCombatOrderSample(issuedSamples, issuedSampleCount, obj);
					++unitsSent;
				}
				else if (ai != nullptr)
				{
					++unitsSkippedBusy;
				}
				else
				{
					++unitsSkippedNoAI;
				}
			}
		}
	}

	BotAILog("%s | sendIdleCombatTeams to (%.0f,%.0f): sent=%d  teams=%d(skip_notdeployable=%d)  skipped non-combat=%d busy=%d no-ai=%d",
		getBotLogPrefix(m_player).str(), targetPos->x, targetPos->y,
		unitsSent, teamsTotal, teamsSkippedNotDeployable, unitsSkippedNonCombat, unitsSkippedBusy, unitsSkippedNoAI);
	if (!issuedSamples.isEmpty())
	{
		BotAILog("%s |   sendIdleCombatTeams sample: %s",
			getBotLogPrefix(m_player).str(), issuedSamples.str());
	}
	return unitsSent;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Send ALL combat teams (not just idle ones) toward a target position.
 * Used for explicit human player commands which take highest priority.
 */
Int AISkirmishPlayer::sendAllCombatTeamsToPosition(const Coord3D *targetPos, const char *debugReason, Int relatedPlayerIndex)
{
	if (targetPos == nullptr)
	{
		BotAILog("%s | sendAllCombatTeamsToPosition called with null targetPos!", getBotLogPrefix(m_player).str());
		return 0;
	}

	const char *orderReason = resolveCombatOrderReason(debugReason, "all_attackmove");

	Int unitsSent = 0;
	Int teamsTotal = 0;
	Int teamsSkippedEmpty = 0;
	Int unitsSkippedNonCombat = 0;
	Int unitsSkippedNoAI = 0;
	AsciiString issuedSamples;
	Int issuedSampleCount = 0;
	for (Player::PlayerTeamList::const_iterator teamProtoIt = m_player->getPlayerTeams()->begin();
		teamProtoIt != m_player->getPlayerTeams()->end();
		++teamProtoIt)
	{
		for (DLINK_ITERATOR<Team> iter = (*teamProtoIt)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
		{
			Team *team = iter.cur();
			++teamsTotal;
			if (team == nullptr || !team->hasAnyUnits())
			{
				++teamsSkippedEmpty;
				continue;
			}

			for (DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance())
			{
				Object *obj = objIter.cur();
				if (obj == nullptr)
					continue;

				if (obj->isKindOf(KINDOF_HARVESTER) || obj->isKindOf(KINDOF_DOZER)
					|| obj->isKindOf(KINDOF_STRUCTURE) || obj->isKindOf(KINDOF_COMMANDCENTER))
				{
					++unitsSkippedNonCombat;
					continue;
				}

				AIUpdateInterface *ai = obj->getAIUpdateInterface();
				if (ai != nullptr)
				{
					noteCombatOrderContext(ai, targetPos, orderReason, "attackmove", relatedPlayerIndex);
					ai->aiAttackMoveToPosition(targetPos, 0, CMD_FROM_AI);
					appendIssuedCombatOrderSample(issuedSamples, issuedSampleCount, obj);
					++unitsSent;
				}
				else
				{
					++unitsSkippedNoAI;
				}
			}
		}
	}

	BotAILog("%s | sendAllCombatTeams to (%.0f,%.0f): sent=%d  teams=%d(skip_empty=%d)  skipped non-combat=%d no-ai=%d",
		getBotLogPrefix(m_player).str(), targetPos->x, targetPos->y,
		unitsSent, teamsTotal, teamsSkippedEmpty, unitsSkippedNonCombat, unitsSkippedNoAI);
	if (!issuedSamples.isEmpty())
	{
		BotAILog("%s |   sendAllCombatTeams sample: %s",
			getBotLogPrefix(m_player).str(), issuedSamples.str());
	}
	return unitsSent;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Move all combat teams to a guard position.
 * Guard mode is persistent: units fight nearby enemies then automatically return.
 * Used for player-commanded hold/assist so no periodic reissue is needed.
 */
Int AISkirmishPlayer::sendAllCombatTeamsToGuardPosition(const Coord3D *targetPos, const char *debugReason, Int relatedPlayerIndex)
{
	if (targetPos == nullptr)
	{
		BotAILog("%s | sendAllCombatTeamsToGuardPosition called with null targetPos!", getBotLogPrefix(m_player).str());
		return 0;
	}

	const char *orderReason = resolveCombatOrderReason(debugReason, "all_guard");

	Int unitsSent = 0;
	AsciiString issuedSamples;
	Int issuedSampleCount = 0;
	for (Player::PlayerTeamList::const_iterator teamProtoIt = m_player->getPlayerTeams()->begin();
		teamProtoIt != m_player->getPlayerTeams()->end();
		++teamProtoIt)
	{
		for (DLINK_ITERATOR<Team> iter = (*teamProtoIt)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
		{
			Team *team = iter.cur();
			if (team == nullptr || !team->hasAnyUnits())
				continue;

			for (DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance())
			{
				Object *obj = objIter.cur();
				if (obj == nullptr)
					continue;

				if (obj->isKindOf(KINDOF_HARVESTER) || obj->isKindOf(KINDOF_DOZER)
					|| obj->isKindOf(KINDOF_STRUCTURE) || obj->isKindOf(KINDOF_COMMANDCENTER))
					continue;

				AIUpdateInterface *ai = obj->getAIUpdateInterface();
				if (ai != nullptr)
				{
					noteCombatOrderContext(ai, targetPos, orderReason, "guard", relatedPlayerIndex);
					ai->aiGuardPosition(targetPos, GUARDMODE_NORMAL, CMD_FROM_AI);
					appendIssuedCombatOrderSample(issuedSamples, issuedSampleCount, obj);
					++unitsSent;
				}
			}
		}
	}

	BotAILog("%s | sendAllCombatTeamsToGuardPosition (%.0f,%.0f): %d units in guard mode",
		getBotLogPrefix(m_player).str(), targetPos->x, targetPos->y, unitsSent);
	if (!issuedSamples.isEmpty())
	{
		BotAILog("%s |   sendAllCombatTeamsToGuardPosition sample: %s",
			getBotLogPrefix(m_player).str(), issuedSamples.str());
	}
	return unitsSent;
}

void AISkirmishPlayer::assistAlliedPlayers()
{
	UnsignedInt curFrame = TheGameLogic->getFrame();
	if (curFrame < m_frameNextAllyAssistCheck) return;
	m_frameNextAllyAssistCheck = curFrame + 5 * LOGICFRAMES_PER_SECOND; // check every 5 seconds.

	// Don't assist if our own base is under attack.
	if (isBaseUnderAttack())
		return;

	if (m_coordAttackPending)
		return;

	if (m_requestedAssistUntilFrame <= curFrame && !m_coordAttackPending && m_holdAttackUntilFrame > curFrame)
		return;

	Player *assistTarget = nullptr;
	Coord3D targetPos;
	Bool fromHumanRequest = false;
	if (!chooseAssistTarget(&assistTarget, &targetPos, &fromHumanRequest))
		return;

	Int unitsSent = sendIdleCombatTeamsToPosition(&targetPos);
	if (unitsSent <= 0)
		return;
	BotAILog("%s | assistAlliedPlayers: sending %d units to '%s' at (%.0f,%.0f) (humanReq=%d)",
		getBotLogPrefix(m_player).str(), unitsSent, getPlayerNameAscii(assistTarget).str(), targetPos.x, targetPos.y, (Int)fromHumanRequest);

	AsciiString msg;
	msg.format("Sending support to %s.", getPlayerColorName(assistTarget).str());
	sendBotChat(msg, true, false);
}

//----------------------------------------------------------------------------------------------------------
/**
 * Coordinated group attack: when starting an attack, wait up to 30 seconds for allied
 * AI bots to gather their forces, then attack as a group.
 *
 * The mechanism:
 * - When this bot gets a team ready to attack (has enemy and teams are idle), it sets
 *   m_coordAttackPending = true and records the start frame.
 * - Each update, it checks if allied skirmish AI bots also have idle teams ready.
 * - After 30 seconds OR when all allies have idle teams, it launches the coordinated attack.
 */
void AISkirmishPlayer::coordinateGroupAttack()
{
	UnsignedInt curFrame = TheGameLogic->getFrame();

	if (curFrame < m_frameNextCoordCheck) return;
	m_frameNextCoordCheck = curFrame + 2 * LOGICFRAMES_PER_SECOND; // check every 2 seconds.

	// Don't coordinate attacks while our base is under attack.
	if (isBaseUnderAttack()) {
		cancelCoordinatedAttack();
		return;
	}

	if (m_requestedAssistUntilFrame > curFrame && m_requestedAssistPlayerIndex != PLAYER_INDEX_INVALID)
		return;

	if (!m_coordAttackPending && m_holdAttackUntilFrame > curFrame)
		return;

	Player *enemy = getAiEnemy();
	if (enemy == nullptr)
	{
		acquireEnemy();
		enemy = getAiEnemy();
	}

	AISkirmishPlayer *leadAI = nullptr;
	Player *leadEnemy = nullptr;
	Coord3D leadTarget;
	leadTarget.zero();
	UnsignedInt leadWaitStart = 0;

	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *otherPlayer = ThePlayerList->getNthPlayer(i);
		if (otherPlayer == nullptr || otherPlayer == m_player || !otherPlayer->isSkirmishAIPlayer())
		{
			continue;
		}

		if (m_player->getRelationship(otherPlayer->getDefaultTeam()) != ALLIES)
		{
			continue;
		}

		AISkirmishPlayer *allyAI = static_cast<AISkirmishPlayer *>(otherPlayer->getAIPlayer());
		if (allyAI == nullptr || !allyAI->m_coordAttackPending || allyAI->m_coordAttackEnemyPlayerIndex == PLAYER_INDEX_INVALID)
		{
			continue;
		}

		if (leadAI == nullptr || allyAI->m_coordAttackWaitStartFrame < leadWaitStart)
		{
			leadAI = allyAI;
			leadWaitStart = allyAI->m_coordAttackWaitStartFrame;
			leadTarget = allyAI->m_coordAttackTarget;
			leadEnemy = ThePlayerList->getNthPlayer(allyAI->m_coordAttackEnemyPlayerIndex);
		}
	}

	if (leadAI != nullptr && leadAI != this && hasCombatTeams())
	{
		// Don't join a lead bot's attack while a recent human command is still locked in.
		if (curFrame < m_frameLastPlanChange + PLAN_CHANGE_COOLDOWN_FRAMES)
		{
			DEBUG_LOG(("BotAI [%s]: coordinateGroupAttack -> skipping join-lead (human command locked for %d more frames)",
				getPlayerNameAscii(m_player).str(),
				(Int)(m_frameLastPlanChange + PLAN_CHANGE_COOLDOWN_FRAMES - curFrame)));
		}
		else
		{
			DEBUG_LOG(("BotAI [%s]: coordinateGroupAttack -> joining lead attack on '%s'",
				getPlayerNameAscii(m_player).str(), leadEnemy ? getPlayerNameAscii(leadEnemy).str() : "?"));
			BotAILog("%s | GROUP: joining lead bot '%s' attack on '%s'  target=(%.0f,%.0f)",
				getBotLogPrefix(m_player).str(),
				getBotLogPrefix(leadAI->m_player).str(),
				leadEnemy ? getPlayerNameAscii(leadEnemy).str() : "?",
				leadTarget.x, leadTarget.y);
			queueCoordinatedAttack(&leadTarget, leadEnemy, leadWaitStart);
		}
	}
	else if (!m_coordAttackPending && enemy != nullptr && hasCombatTeams())
	{
		// Don't self-initiate a new plan if we changed plans recently (3-minute cooldown).
		if (curFrame < m_frameLastPlanChange + PLAN_CHANGE_COOLDOWN_FRAMES)
		{
			return;
		}

		DEBUG_LOG(("BotAI [%s]: coordinateGroupAttack -> self-initiating attack on '%s'",
			getPlayerNameAscii(m_player).str(), getPlayerNameAscii(enemy).str()));
		BotAILog("%s | GROUP: self-initiating attack on '%s'", getBotLogPrefix(m_player).str(), getPlayerColorName(enemy).str());
		Coord3D targetPos;
		Region2D bounds;
		getPlayerStructureBounds(&bounds, enemy->getPlayerIndex());
		targetPos.x = bounds.lo.x + bounds.width() / 2;
		targetPos.y = bounds.lo.y + bounds.height() / 2;
		targetPos.z = TheTerrainLogic->getGroundHeight(targetPos.x, targetPos.y);
		m_frameLastPlanChange = curFrame;
		queueCoordinatedAttack(&targetPos, enemy, curFrame);
	}

	if (!m_coordAttackPending)
	{
		return;
	}

	if (m_coordAttackEnemyPlayerIndex == PLAYER_INDEX_INVALID)
	{
		cancelCoordinatedAttack();
		return;
	}

	Player *coordEnemy = ThePlayerList->getNthPlayer(m_coordAttackEnemyPlayerIndex);
	if (coordEnemy == nullptr || !coordEnemy->hasAnyObjects())
	{
		cancelCoordinatedAttack();
		return;
	}

	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *otherPlayer = ThePlayerList->getNthPlayer(i);
		if (otherPlayer == nullptr || otherPlayer == m_player || !otherPlayer->isSkirmishAIPlayer())
		{
			continue;
		}

		if (m_player->getRelationship(otherPlayer->getDefaultTeam()) != ALLIES)
		{
			continue;
		}

		AISkirmishPlayer *allyAI = static_cast<AISkirmishPlayer *>(otherPlayer->getAIPlayer());
		if (allyAI == nullptr || allyAI->isBaseUnderAttack() || !allyAI->hasCombatTeams())
		{
			continue;
		}
		// Don't invite allies that are locked into a recent human-commanded plan or active move order.
		if (curFrame < allyAI->m_frameLastPlanChange + PLAN_CHANGE_COOLDOWN_FRAMES
			|| allyAI->m_holdAttackUntilFrame > curFrame
			|| (allyAI->m_requestedAssistUntilFrame > curFrame && allyAI->m_requestedAssistPlayerIndex != PLAYER_INDEX_INVALID))
		{
			continue;
		}
		allyAI->queueCoordinatedAttack(&m_coordAttackTarget, coordEnemy, m_coordAttackWaitStartFrame);
	}

	Bool allAlliesReady = true;
	Int alliedBotsConsidered = 0;
	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *otherPlayer = ThePlayerList->getNthPlayer(i);
		if (otherPlayer == nullptr || otherPlayer == m_player || !otherPlayer->isSkirmishAIPlayer())
		{
			continue;
		}

		if (m_player->getRelationship(otherPlayer->getDefaultTeam()) != ALLIES || !otherPlayer->hasAnyObjects())
		{
			continue;
		}

		AISkirmishPlayer *allyAI = static_cast<AISkirmishPlayer *>(otherPlayer->getAIPlayer());
		if (allyAI == nullptr || allyAI->isBaseUnderAttack() || !allyAI->hasCombatTeams())
		{
			continue;
		}

		++alliedBotsConsidered;
		if (!allyAI->m_coordAttackPending || allyAI->m_coordAttackEnemyPlayerIndex != coordEnemy->getPlayerIndex())
		{
			allAlliesReady = false;
			break;
		}
	}

	Bool timeExpired = curFrame >= m_coordAttackWaitStartFrame + GROUP_ATTACK_WAIT_FRAMES;
	BotAILog("%s | GROUP: staging  enemy='%s'  target=(%.0f,%.0f)  elapsed=%u/%u  alliesConsidered=%d  allReady=%d  timeExpired=%d",
		getBotLogPrefix(m_player).str(), getPlayerColorName(coordEnemy).str(),
		m_coordAttackTarget.x, m_coordAttackTarget.y,
		(unsigned)(curFrame - m_coordAttackWaitStartFrame), (unsigned)GROUP_ATTACK_WAIT_FRAMES,
		alliedBotsConsidered, (int)allAlliesReady, (int)timeExpired);
	if (!timeExpired)
	{
		DEBUG_LOG(("BotAI [%s]: coordinateGroupAttack -> waiting for allies (considered=%d allReady=%d elapsed=%d/%d)",
			getPlayerNameAscii(m_player).str(), alliedBotsConsidered, (Int)allAlliesReady,
			(Int)(curFrame - m_coordAttackWaitStartFrame), (Int)GROUP_ATTACK_WAIT_FRAMES));
		// Keep rallying idle teams to staging point while waiting.
		rallyTeamsToStagingPoint();
		return;
	}

	DEBUG_LOG(("BotAI [%s]: coordinateGroupAttack -> LAUNCHING on '%s' (timeExpired=%d alliesConsidered=%d)",
		getPlayerNameAscii(m_player).str(), getPlayerNameAscii(coordEnemy).str(),
		(Int)timeExpired, alliedBotsConsidered));
	BotAILog("%s | GROUP: **LAUNCHING ATTACK** on '%s'  target=(%.0f,%.0f)  timeExpired=%d  alliesConsidered=%d",
		getBotLogPrefix(m_player).str(), getPlayerColorName(coordEnemy).str(),
		m_coordAttackTarget.x, m_coordAttackTarget.y, (int)timeExpired, alliedBotsConsidered);
	Coord3D launchTarget = m_coordAttackTarget;
	UnsignedInt launchWaitStart = m_coordAttackWaitStartFrame;
	launchCoordinatedAttack(&launchTarget, coordEnemy, timeExpired);

	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *otherPlayer = ThePlayerList->getNthPlayer(i);
		if (otherPlayer == nullptr || otherPlayer == m_player || !otherPlayer->isSkirmishAIPlayer())
		{
			continue;
		}

		if (m_player->getRelationship(otherPlayer->getDefaultTeam()) != ALLIES)
		{
			continue;
		}

		AISkirmishPlayer *allyAI = static_cast<AISkirmishPlayer *>(otherPlayer->getAIPlayer());
		if (allyAI == nullptr || allyAI->isBaseUnderAttack())
		{
			continue;
		}
		// Don't force-launch allies that are locked into a recent human-commanded plan or active move order.
		if (curFrame < allyAI->m_frameLastPlanChange + PLAN_CHANGE_COOLDOWN_FRAMES
			|| allyAI->m_holdAttackUntilFrame > curFrame
			|| (allyAI->m_requestedAssistUntilFrame > curFrame && allyAI->m_requestedAssistPlayerIndex != PLAYER_INDEX_INVALID))
		{
			BotAILog("%s | GROUP: skipping force-launch on %s (ally plan-locked, move-held, or assisting)",
				getBotLogPrefix(m_player).str(), getBotLogPrefix(otherPlayer).str());
			continue;
		}
		allyAI->queueCoordinatedAttack(&launchTarget, coordEnemy, launchWaitStart);
		allyAI->launchCoordinatedAttack(&launchTarget, coordEnemy, timeExpired);
	}
}

//----------------------------------------------------------------------------------------------------------
/**
 * Launch our portion of the synchronized attack.
 */
void AISkirmishPlayer::launchCoordinatedAttack(const Coord3D *targetPos, const Player *enemy, Bool dueToTimeout)
{
	if (targetPos == nullptr || enemy == nullptr)
	{
		cancelCoordinatedAttack();
		return;
	}

	DEBUG_LOG(("BotAI [%s]: launchCoordinatedAttack on '%s' at (%.0f,%.0f) timeout=%d",
		m_player ? getPlayerNameAscii(m_player).str() : "?",
		enemy ? getPlayerNameAscii(enemy).str() : "?",
		targetPos->x, targetPos->y, (Int)dueToTimeout));
	Int unitsSent = sendAllCombatTeamsToPosition(targetPos);
	if (unitsSent > 0)
	{
		AsciiString msg;
		msg.format("Attacking %s.", getPlayerColorName(enemy).str());
		sendBotChat(msg, true, true);
	}

	cancelCoordinatedAttack();
}

// ////////////////////////////////////////////////////////////////////////////////////////////////

//----------------------------------------------------------------------------------------------------------
/**
 * Returns true if this bot is the lowest-index allied skirmish AI.
 * Used so that only ONE bot responds to generic commands (help, unknown command).
 */
Bool AISkirmishPlayer::isFirstAlliedBot() const
{
	const Player *localPlayer = ThePlayerList ? ThePlayerList->getLocalPlayer() : nullptr;
	if (localPlayer == nullptr)
	{
		return true;
	}

	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *otherPlayer = ThePlayerList->getNthPlayer(i);
		if (otherPlayer == nullptr || !otherPlayer->isSkirmishAIPlayer())
		{
			continue;
		}

		if (localPlayer->getRelationship(otherPlayer->getDefaultTeam()) != ALLIES)
		{
			continue;
		}

		// The first allied bot we find: return true only if it's us.
		return (otherPlayer == m_player);
	}

	return true;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Deterministic version of isFirstAlliedBot() for synchronized chat commands.
 * It is based on the sender, not the local human client.
 */
Bool AISkirmishPlayer::isFirstAlliedBotForPlayer(const Player *ally) const
{
	if (ally == nullptr || ThePlayerList == nullptr)
	{
		return true;
	}

	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *otherPlayer = ThePlayerList->getNthPlayer(i);
		if (otherPlayer == nullptr || !otherPlayer->isSkirmishAIPlayer())
		{
			continue;
		}

		const Bool allyConsidersOther = (ally->getRelationship(otherPlayer->getDefaultTeam()) == ALLIES);
		const Bool otherConsidersAlly = (otherPlayer->getRelationship(ally->getDefaultTeam()) == ALLIES);
		if (!allyConsidersOther && !otherConsidersAlly)
		{
			continue;
		}

		return (otherPlayer == m_player);
	}

	return true;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Find an enemy player whose color name appears in the command string.
 * e.g. "attack red" returns the enemy whose getPlayerColorName() yields "Red".
 */
Player *AISkirmishPlayer::findEnemyByColorName(const AsciiString &command) const
{
	if (ThePlayerList == nullptr || m_player == nullptr)
		return nullptr;

	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *candidate = ThePlayerList->getNthPlayer(i);
		if (candidate == nullptr || !candidate->hasAnyObjects())
			continue;

		if (m_player->getRelationship(candidate->getDefaultTeam()) != ENEMIES)
			continue;

		AsciiString colorName = getPlayerColorName(candidate);
		colorName.toLower();
		if (!colorName.isEmpty() && containsText(command, colorName.str()))
			return candidate;
	}
	return nullptr;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Find any non-self player (enemy or ally) whose color name appears in the command string.
 * e.g. "move red" returns the player whose getPlayerColorName() yields "Red".
 */
Player *AISkirmishPlayer::findPlayerByColorName(const AsciiString &command) const
{
	if (ThePlayerList == nullptr || m_player == nullptr)
		return nullptr;

	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *candidate = ThePlayerList->getNthPlayer(i);
		if (candidate == nullptr || candidate == m_player || !candidate->hasAnyObjects())
			continue;

		AsciiString colorName = getPlayerColorName(candidate);
		colorName.toLower();
		if (!colorName.isEmpty() && containsText(command, colorName.str()))
			return candidate;
	}
	return nullptr;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Parse directional keywords from a command string and find the enemy player
 * whose base is closest to that region of the map.
 * Supports: left, right, top, bottom, center, top left, top right, bottom left, bottom right.
 */
Player *AISkirmishPlayer::findEnemyInDirection(const AsciiString &command)
{
	Coord3D targetPos;
	if (!tryGetDirectionalTargetPosition(command, &targetPos))
	{
		return nullptr;
	}

	Player *bestEnemy = nullptr;
	Real bestDistSqr = HUGE_DIST * HUGE_DIST;

	for (Int i = 0; i < ThePlayerList->getPlayerCount(); ++i)
	{
		Player *candidate = ThePlayerList->getNthPlayer(i);
		if (candidate == nullptr || !candidate->hasAnyObjects())
		{
			continue;
		}

		if (m_player->getRelationship(candidate->getDefaultTeam()) != ENEMIES)
		{
			continue;
		}

		Region2D bounds;
		getPlayerStructureBounds(&bounds, i);
		Real cx = bounds.lo.x + bounds.width() / 2;
		Real cy = bounds.lo.y + bounds.height() / 2;
		Real distSqr = sqr(cx - targetPos.x) + sqr(cy - targetPos.y);

		if (distSqr < bestDistSqr)
		{
			bestDistSqr = distSqr;
			bestEnemy = candidate;
		}
	}

	return bestEnemy;
}

//----------------------------------------------------------------------------------------------------------
/**
 * Move idle combat teams toward the rally point for a coordinated attack.
 * This helps units physically group up before the attack launches.
 */
void AISkirmishPlayer::rallyTeamsToStagingPoint(const char *debugReason, Int relatedPlayerIndex)
{
	if (!m_coordAttackPending)
	{
		return;
	}

	if (m_coordRallyPoint.x == 0.0f && m_coordRallyPoint.y == 0.0f)
	{
		return;
	}

	const char *orderReason = resolveCombatOrderReason(debugReason, "rally_idle_move");

	Int unitsSent = 0;
	AsciiString issuedSamples;
	Int issuedSampleCount = 0;

	for (Player::PlayerTeamList::const_iterator teamProtoIt = m_player->getPlayerTeams()->begin();
		teamProtoIt != m_player->getPlayerTeams()->end();
		++teamProtoIt)
	{
		for (DLINK_ITERATOR<Team> iter = (*teamProtoIt)->iterate_TeamInstanceList(); !iter.done(); iter.advance())
		{
			Team *team = iter.cur();
			if (!isDeployableCombatTeam(team))
			{
				continue;
			}

			for (DLINK_ITERATOR<Object> objIter = team->iterate_TeamMemberList(); !objIter.done(); objIter.advance())
			{
				Object *obj = objIter.cur();
				if (obj == nullptr)
				{
					continue;
				}

				if (obj->isKindOf(KINDOF_HARVESTER) || obj->isKindOf(KINDOF_DOZER)
					|| obj->isKindOf(KINDOF_STRUCTURE) || obj->isKindOf(KINDOF_COMMANDCENTER))
				{
					continue;
				}

				AIUpdateInterface *ai = obj->getAIUpdateInterface();
				if (ai != nullptr && ai->isIdle())
				{
					noteCombatOrderContext(ai, &m_coordRallyPoint, orderReason, "move", relatedPlayerIndex);
					ai->aiMoveToPosition(&m_coordRallyPoint, CMD_FROM_AI);
					appendIssuedCombatOrderSample(issuedSamples, issuedSampleCount, obj);
					++unitsSent;
				}
			}
		}
	}

	if (unitsSent > 0)
	{
		BotAILog("%s | rallyTeamsToStagingPoint -> %d idle units move-to (%.0f,%.0f)",
			getBotLogPrefix(m_player).str(), unitsSent, m_coordRallyPoint.x, m_coordRallyPoint.y);
		if (!issuedSamples.isEmpty())
		{
			BotAILog("%s |   rallyTeamsToStagingPoint sample: %s",
				getBotLogPrefix(m_player).str(), issuedSamples.str());
		}
	}
}

// ////////////////////////////////////////////////////////////////////////////////////////////////

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void AISkirmishPlayer::crc( Xfer *xfer )
{

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info;
	* 1: Initial version
	* 2: Alliance support and coordinated attack state
	* 3: Plan change cooldown */
// ------------------------------------------------------------------------------------------------
void AISkirmishPlayer::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// xfer base class info
	AIPlayer::xfer( xfer );

	// front base defense
	xfer->xferInt( &m_curFrontBaseDefense );

	// flank base defense
	xfer->xferInt( &m_curFlankBaseDefense );

	// front left defense angle
	xfer->xferReal( &m_curFrontLeftDefenseAngle );

	// front right defense angle
	xfer->xferReal( &m_curFrontRightDefenseAngle );

	// left flank left defense angle
	xfer->xferReal( &m_curLeftFlankLeftDefenseAngle );

	// left flank right defense angle
	xfer->xferReal( &m_curLeftFlankRightDefenseAngle );

	// right flank left defense angle
	xfer->xferReal( &m_curRightFlankLeftDefenseAngle );

	// right flank right defense angle
	xfer->xferReal( &m_curRightFlankRightDefenseAngle );

	if (version >= 2)
	{
		xfer->xferUnsignedInt(&m_frameNextAllyAssistCheck);
		xfer->xferUnsignedInt(&m_frameNextCoordCheck);
		xfer->xferUnsignedInt(&m_coordAttackWaitStartFrame);
		xfer->xferUnsignedInt(&m_frameNextBotChat);
		xfer->xferUnsignedInt(&m_frameNextBotPing);
		xfer->xferUnsignedInt(&m_holdAttackUntilFrame);
		xfer->xferUnsignedInt(&m_requestedAssistUntilFrame);
		xfer->xferCoord3D(&m_coordAttackTarget);
		xfer->xferCoord3D(&m_coordRallyPoint);
		xfer->xferCoord3D(&m_requestedAssistTarget);
		xfer->xferBool(&m_coordAttackPending);
		xfer->xferInt(&m_coordAttackEnemyPlayerIndex);
		xfer->xferInt(&m_requestedAssistPlayerIndex);
	}

	if (version >= 3)
	{
		xfer->xferUnsignedInt(&m_frameLastPlanChange);
	}

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void AISkirmishPlayer::loadPostProcess()
{
	if (m_coordAttackEnemyPlayerIndex < PLAYER_INDEX_INVALID || m_coordAttackEnemyPlayerIndex >= MAX_PLAYER_COUNT)
	{
		m_coordAttackEnemyPlayerIndex = PLAYER_INDEX_INVALID;
	}

	if (m_requestedAssistPlayerIndex < PLAYER_INDEX_INVALID || m_requestedAssistPlayerIndex >= MAX_PLAYER_COUNT)
	{
		m_requestedAssistPlayerIndex = PLAYER_INDEX_INVALID;
	}

	if (!m_coordAttackPending)
	{
		cancelCoordinatedAttack();
	}

	if (m_requestedAssistPlayerIndex == PLAYER_INDEX_INVALID || m_requestedAssistUntilFrame == 0)
	{
		m_requestedAssistTarget.zero();
	}
}

