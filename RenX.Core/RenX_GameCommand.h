/**
 * Copyright (C) 2014 Justin James.
 *
 * This license must be preserved.
 * Any applications, libraries, or code which make any use of any
 * component of this program must not be commercial, unless explicit
 * permission is granted from the original author. The use of this
 * program for non-profit purposes is permitted.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * In the event that this license restricts you from making desired use of this program, contact the original author.
 * Written by Justin James <justin.aj@hotmail.com>
 */

#if !defined _RENX_GAMECOMMAND_H_HEADER
#define _RENX_GAMECOMMAND_H_HEADER

/**
 * @file RenX_GameCommand.h
 * @brief Provides the basis of the in-game Renegade-X chat command system.
 */

#include "Jupiter/Command.h"
#include "RenX.h"
#include "RenX_Core.h" // getCore().

namespace RenX
{
	/** Forward delcarations */
	class Server;
	struct PlayerInfo;
	class GameCommand;

	/** Master command list */
	RENX_API extern Jupiter::ArrayList<GameCommand> *GameMasterCommandList;

	/**
	* @brief Provides an extendable interface from which in-game commands can be created.
	* TODO: Add access levels
	*/
	class RENX_API GameCommand : public Jupiter::Command
	{
	public:

		/**
		* @brief Called when a player with the proper access privledges executes this command.
		*
		* @param source Renegade-X server where the player is located.
		* @param player Player who executed the command.
		* @param parameters Parameters following the command.
		*/
		virtual void trigger(RenX::Server *source, RenX::PlayerInfo *player, const Jupiter::ReadableString &parameters) = 0;

		/**
		* @brief Called when the command is intially created. Define triggers and access levels here.
		*/
		virtual void create() = 0;

		/**
		* @brief Used internally to provide per-server configurable commands.
		* Note: This is automatically generated by the GAME_COMMAND_INIT macro.
		*/
		virtual GameCommand *copy() = 0;

		/**
		* @brief Copy constructor for a Game Command.
		* Note: This is automatically generated by the GENERIC_GAME_COMMAND macro.
		*/
		GameCommand(const GameCommand &command);

		/**
		* @brief Default constructor for a Game Command.
		* Note: This is automatically generated by the BASE_GAME_COMMAND macro.
		*/
		GameCommand();

		/**
		* @brief Destructor for a Game Command.
		* Note: This is not automatically generated by any macro, and is available for use.
		*/
		virtual ~GameCommand();
	};
}

/** Game Command Macros */

/** Defines the core of a game command's declaration. This should be included in every game command. */
#define BASE_GAME_COMMAND(CLASS) \
	public: \
	void trigger(RenX::Server *source, RenX::PlayerInfo *player, const Jupiter::ReadableString &parameters); \
	const Jupiter::ReadableString &getHelp(const Jupiter::ReadableString &parameters); \
	CLASS *copy(); \
	void create(); \
	CLASS() { this->create(); RenX::getCore()->addCommand(this); }

/** Expands to become the entire declaration for a game command. In most cases, this will be sufficient. */
#define GENERIC_GAME_COMMAND(CLASS) \
class CLASS : public RenX::GameCommand { \
	BASE_GAME_COMMAND(CLASS) \
	CLASS(CLASS &c) : RenX::GameCommand(c) { this->create(); } };

/** Instantiates a game command, and also defines neccessary core functions for compatibility. */
#define GAME_COMMAND_INIT(CLASS) \
	CLASS CLASS ## _instance; \
	CLASS *CLASS::copy() { return new CLASS(*this); }

#endif // _RENX_GAMECOMMAND_H_HEADER