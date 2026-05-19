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

// AISkirmishPlayer.h
// Computerized opponent
// Author: Michael S. Booth, January 2002

#pragma once

#include "Common/GameMemory.h"
#include "GameLogic/AIPlayer.h"

class BuildListInfo;
class SpecialPowerTemplate;


/**
 * The computer-controlled opponent.
 */
class AISkirmishPlayer : public AIPlayer
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( AISkirmishPlayer, "AISkirmishPlayer"  )

public:	 // AISkirmish specific methods.

	AISkirmishPlayer( Player *p );							///< constructor
	virtual void computeSuperweaponTarget(const SpecialPowerTemplate *power, Coord3D *pos, Int playerNdx, Real weaponRadius) override; ///< Calculates best pos for weapon given radius.

public:	// AIPlayer interface methods.

	virtual void update() override;											///< simulates the behavior of a player
	virtual void onChatMessage(const Player *sender, const UnicodeString &text, Int playerMask) override;

	virtual void newMap() override;											///< New map loaded call.

	/// Invoked when a unit I am training comes into existence
	virtual void onUnitProduced( Object *factory, Object *unit ) override;

	virtual void buildSpecificAITeam(TeamPrototype *teamProto, Bool priorityBuild) override; ///< Builds this team immediately.

	virtual void buildSpecificAIBuilding(const AsciiString &thingName) override; ///< Builds this building as soon as possible.

	virtual void buildAIBaseDefense(Bool flank) override; ///< Builds base defense on front or flank of base.

	virtual void buildAIBaseDefenseStructure(const AsciiString &thingName, Bool flank) override; ///< Builds base defense on front or flank of base.

	virtual void recruitSpecificAITeam(TeamPrototype *teamProto, Real recruitRadius) override; ///< Builds this team immediately.

	virtual Bool isSkirmishAI() override {return true;}

	virtual Bool checkBridges(Object *unit, Waypoint *way) override;

	virtual Player *getAiEnemy() override;	///< Solo AI attacks based on scripting.  Only skirmish auto-acquires an enemy at this point.  jba.

protected:

	// snapshot methods
	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess() override;

	virtual void doBaseBuilding() override;
	virtual void checkReadyTeams() override;
	virtual void checkQueuedTeams() override;
	virtual void doTeamBuilding() override;
	virtual Object *findDozer(const Coord3D *pos) override;
	virtual void queueDozer() override;

protected:

	virtual Bool selectTeamToBuild() override;			///< determine the next team to build
	virtual Bool selectTeamToReinforce( Int minPriority ) override;			///< determine the next team to reinforce
	virtual Bool startTraining( WorkOrder *order, Bool busyOK, AsciiString teamName) override;	///< find a production building that can handle the order, and start building

	virtual Bool isAGoodIdeaToBuildTeam( TeamPrototype *proto ) override;		///< return true if team should be built
	virtual void processBaseBuilding() override;		///< do base-building behaviors
	virtual void processTeamBuilding() override;		///< do team-building behaviors

protected:
	void adjustBuildList(BuildListInfo *list);
	Int getMyEnemyPlayerIndex();
	void acquireEnemy();

	// TheSuperHackers @feature Alliance cooperation - assist allies under attack
	void assistAlliedPlayers();		///< Send idle military units to help allies under attack (if self not in trouble).
	Bool isBaseUnderAttack() const;	///< Returns true if our base structures have been recently damaged.

	// TheSuperHackers @feature Coordinated group attacks - wait for allies before attacking
	void coordinateGroupAttack();	///< If we have a pending attack, wait up to 30s for allied bots to join.
	void cancelCoordinatedAttack();
	void queueCoordinatedAttack(const Coord3D *targetPos, const Player *enemy, UnsignedInt waitStartFrame);
	void launchCoordinatedAttack(const Coord3D *targetPos, const Player *enemy, Bool dueToTimeout);
	void sendBotChat(const AsciiString &message, Bool ping = false, Bool force = false);
	Bool chooseAssistTarget(Player **targetPlayer, Coord3D *targetPos, Bool *fromHumanRequest);
	Bool hasIdleCombatTeams() const;
	Bool hasCombatTeams() const;
	Int sendIdleCombatTeamsToPosition(const Coord3D *targetPos, const char *debugReason = nullptr, Int relatedPlayerIndex = -1);
	Int sendCombatTeamToPosition(Team *team, const Coord3D *targetPos, Bool guardMode, const char *debugReason = nullptr, Int relatedPlayerIndex = -1);
	Int sendAllCombatTeamsToPosition(const Coord3D *targetPos, const char *debugReason = nullptr, Int relatedPlayerIndex = -1);
	Int sendAllCombatTeamsToGuardPosition(const Coord3D *targetPos, const char *debugReason = nullptr, Int relatedPlayerIndex = -1); ///< Hold/assist: guard mode keeps units at position without reissue.
	Bool isDeployableCombatTeam(const Team *team) const;
	Bool isFirstAlliedBot() const;	///< True if this is the lowest-index allied skirmish bot (used for single-response commands).
	Bool isFirstAlliedBotForPlayer(const Player *ally) const; ///< Deterministic variant for synchronized multiplayer chat.
	Player *findEnemyInDirection(const AsciiString &command);	///< Find enemy player in a named map direction.
	Player *findEnemyByColorName(const AsciiString &command) const;	///< Find enemy player by color-name keyword in the command string.
	Player *findPlayerByColorName(const AsciiString &command) const;	///< Find any non-self player by color-name keyword in the command string.
	void rallyTeamsToStagingPoint(const char *debugReason = nullptr, Int relatedPlayerIndex = -1);		///< Move idle combat teams to coordinated attack rally point.

protected:
	Int m_curFrontBaseDefense; // First is 0.
	Int m_curFlankBaseDefense; // First is 0.
	Real m_curFrontLeftDefenseAngle;
	Real m_curFrontRightDefenseAngle;
	Real m_curLeftFlankLeftDefenseAngle;
	Real m_curLeftFlankRightDefenseAngle;
	Real m_curRightFlankLeftDefenseAngle;
	Real m_curRightFlankRightDefenseAngle;

	UnsignedInt m_frameToCheckEnemy;
	Player			*m_currentEnemy;

	// Alliance cooperation state
	UnsignedInt m_frameNextAllyAssistCheck;	///< Next frame to check if allies need help.
	UnsignedInt m_frameNextCoordCheck;		///< Next frame to run coordinated-attack logic.
	UnsignedInt m_coordAttackWaitStartFrame;///< Frame when we started waiting for allies to join attack (0 = not waiting).
	UnsignedInt m_frameNextBotChat;			///< General cooldown for status chatter.
	UnsignedInt m_frameNextBotPing;			///< General cooldown for ping chatter.
	UnsignedInt m_holdAttackUntilFrame;	///< Hold attacks until this frame.
	UnsignedInt m_requestedAssistUntilFrame; ///< Human-requested assist is valid until this frame.
	UnsignedInt m_frameNextMoveReissue;		///< Next frame to re-issue move command during hold/assist periods.
	UnsignedInt m_frameLastPlanChange;		///< Frame when the bot last changed its plan (3-min cooldown).
	Coord3D m_coordAttackTarget;				///< Target position for the coordinated attack.
	Coord3D m_coordRallyPoint;				///< Staging rally point for coordinated attack grouping.
	Coord3D m_requestedAssistTarget;		///< Target position requested by a human ally.
	Bool m_coordAttackPending;				///< True if we are staging a coordinated attack.
	Int m_coordAttackEnemyPlayerIndex;	///< Enemy player index for the coordinated attack.
	Int m_requestedAssistPlayerIndex;	///< Human ally player index for an active assist request.

};
