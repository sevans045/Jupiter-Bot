/**
 * Copyright (C) 2014-2015 Justin James.
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

#include <ctime>
#include "Jupiter/INIFile.h"
#include "Jupiter/String.h"
#include "ServerManager.h"
#include "IRC_Bot.h"
#include "RenX_Server.h"
#include "RenX_PlayerInfo.h"
#include "RenX_BuildingInfo.h"
#include "RenX_GameCommand.h"
#include "RenX_Functions.h"
#include "RenX_Plugin.h"
#include "RenX_BanDatabase.h"
#include "RenX_Tags.h"

using namespace Jupiter::literals;

int RenX::Server::think()
{
	if (RenX::Server::connected == false)
	{
		if (RenX::Server::maxAttempts < 0 || RenX::Server::attempts < RenX::Server::maxAttempts)
		{
			if (std::chrono::steady_clock::now() >= RenX::Server::lastAttempt + RenX::Server::delay)
			{
				if (RenX::Server::connect())
					RenX::Server::sendLogChan(IRCCOLOR "03[RenX]" IRCCOLOR " Socket successfully reconnected to Renegade-X server.");
				else RenX::Server::sendLogChan(IRCCOLOR "04[Error]" IRCCOLOR " Failed to reconnect to Renegade-X server.");
			}
		}
		else
			return 1;
	}
	else if (RenX::Server::awaitingPong && std::chrono::steady_clock::now() - RenX::Server::lastActivity >= RenX::Server::pingTimeoutThreshold) // ping timeout
	{
		RenX::Server::sendLogChan(STRING_LITERAL_AS_REFERENCE(IRCCOLOR "04[Error]" IRCCOLOR " Disconnected from Renegade-X server (ping timeout)."));
		RenX::Server::disconnect(RenX::DisconnectReason::PingTimeout);
	}
	else
	{
		if (RenX::Server::sock.recv() > 0)
		{
			Jupiter::ReadableString::TokenizeResult<Jupiter::Reference_String> result = Jupiter::ReferenceString::tokenize(RenX::Server::sock.getBuffer(), '\n');
			if (result.token_count != 0)
			{
				RenX::Server::lastActivity = std::chrono::steady_clock::now();
				RenX::Server::lastLine.concat(result.tokens[0]);
				if (result.token_count != 1)
				{
					RenX::Server::processLine(RenX::Server::lastLine);
					RenX::Server::lastLine = result.tokens[result.token_count - 1];

					for (size_t index = 1; index != result.token_count - 1; ++index)
						RenX::Server::processLine(result.tokens[index]);
				}
			}
		}
		else if (Jupiter::Socket::getLastError() == 10035)
		{
			if (RenX::Server::awaitingPong == false && std::chrono::steady_clock::now() - RenX::Server::lastActivity >= RenX::Server::pingRate)
			{
				RenX::Server::lastActivity = std::chrono::steady_clock::now();
				RenX::Server::sock.send("cping\n"_jrs);
				RenX::Server::awaitingPong = true;
			}
		}
		else // This is a serious error
		{
			RenX::Server::wipeData();
			if (RenX::Server::maxAttempts != 0)
			{
				RenX::Server::sendLogChan(IRCCOLOR "07[Warning]" IRCCOLOR " Connection to Renegade-X server lost. Reconnection attempt in progress.");
				if (RenX::Server::reconnect(RenX::DisconnectReason::SocketError))
					RenX::Server::sendLogChan(IRCCOLOR "06[Progress]" IRCCOLOR " Connection to Renegade-X server reestablished. Initializing Renegade-X RCON protocol...");
				else
					RenX::Server::sendLogChan(IRCCOLOR "04[Error]" IRCCOLOR " Connection to Renegade-X server lost. Reconnection attempt failed.");
			}
			else
			{
				RenX::Server::sendLogChan(IRCCOLOR "04[Error]" IRCCOLOR " Connection to Renegade-X server lost. No attempt will be made to reconnect.");
				return 1;
			}
			return 0;
		}

		if (RenX::Server::rconVersion >= 3 && RenX::Server::players.size() != 0)
		{
			if (RenX::Server::clientUpdateRate != std::chrono::milliseconds::zero() && std::chrono::steady_clock::now() > RenX::Server::lastClientListUpdate + RenX::Server::clientUpdateRate)
				RenX::Server::updateClientList();

			if (RenX::Server::buildingUpdateRate != std::chrono::milliseconds::zero() && std::chrono::steady_clock::now() > RenX::Server::lastBuildingListUpdate + RenX::Server::buildingUpdateRate)
				RenX::Server::updateBuildingList();
		}
	}
	return 0;
}

int RenX::Server::OnRehash()
{
	Jupiter::StringS oldHostname = RenX::Server::hostname;
	Jupiter::StringS oldClientHostname = RenX::Server::clientHostname;
	Jupiter::StringS oldPass = RenX::Server::pass;
	unsigned short oldPort = RenX::Server::port;
	int oldSteamFormat = RenX::Server::steamFormat;
	RenX::Server::commands.emptyAndDelete();
	RenX::Server::init();
	if (RenX::Server::port == 0 || RenX::Server::hostname.isNotEmpty())
	{
		RenX::Server::hostname = oldHostname;
		RenX::Server::clientHostname = oldClientHostname;
		RenX::Server::pass = oldPass;
		RenX::Server::port = oldPort;
	}
	else if (oldHostname.equalsi(RenX::Server::hostname) == false || oldPort != RenX::Server::port || oldClientHostname.equalsi(RenX::Server::clientHostname) == false || oldPass.equalsi(RenX::Server::pass) == false)
		RenX::Server::reconnect(RenX::DisconnectReason::Rehash);
	return 0;
}

bool RenX::Server::OnBadRehash(bool removed)
{
	return removed;
}

bool RenX::Server::isConnected() const
{
	return RenX::Server::connected;
}

bool RenX::Server::hasSeenStart() const
{
	return RenX::Server::seenStart;
}

bool RenX::Server::isFirstKill() const
{
	return RenX::Server::firstKill;
}

bool RenX::Server::isFirstDeath() const
{
	return RenX::Server::firstDeath;
}

bool RenX::Server::isFirstAction() const
{
	return RenX::Server::firstAction;
}

bool RenX::Server::isSeamless() const
{
	return RenX::Server::seamless;
}

bool RenX::Server::isPublicLogChanType(int type) const
{
	return RenX::Server::logChanType == type;
}

bool RenX::Server::isAdminLogChanType(int type) const
{
	return RenX::Server::adminLogChanType == type;
}

bool RenX::Server::isLogChanType(int type) const
{
	return RenX::Server::isPublicLogChanType(type) || RenX::Server::isAdminLogChanType(type);
}

bool RenX::Server::isPure() const
{
	return RenX::Server::pure;
}

int RenX::Server::send(const Jupiter::ReadableString &command)
{
	return RenX::Server::sock.send("c"_jrs + command + "\n"_jrs);
}

int RenX::Server::sendMessage(const Jupiter::ReadableString &message)
{
	if (RenX::Server::neverSay)
	{
		int r = 0;
		if (RenX::Server::players.size() != 0)
			for (Jupiter::DLList<RenX::PlayerInfo>::Node *node = RenX::Server::players.getNode(0); node != nullptr; node = node->next)
				if (node->data->isBot == false)
					r += RenX::Server::sock.send(Jupiter::StringS::Format("chostprivatesay pid%d %.*s\n", node->data->id, message.size(), message.ptr()));
		return r;
	}
	else
		return RenX::Server::sock.send("chostsay "_jrs + message + '\n');
}

int RenX::Server::sendMessage(const RenX::PlayerInfo *player, const Jupiter::ReadableString &message)
{
	auto cmd = "chostprivatesay pid"_jrs + Jupiter::StringS::Format("%d ", player->id) + message  + '\n';
	RenX::sanitizeString(cmd);
	return RenX::Server::sock.send(cmd);
}

int RenX::Server::sendData(const Jupiter::ReadableString &data)
{
	return RenX::Server::sock.send(data);
}

RenX::BuildingInfo *RenX::Server::getBuildingByName(const Jupiter::ReadableString &name) const
{
	for (size_t index = 0; index != RenX::Server::buildings.size(); ++index)
		if (RenX::Server::buildings.get(index)->name.equalsi(name))
			return RenX::Server::buildings.get(index);
	return nullptr;
}

bool RenX::Server::hasMapInRotation(const Jupiter::ReadableString &name) const
{
	size_t index = RenX::Server::maps.size();
	while (index != 0)
		if (RenX::Server::maps.get(--index)->equalsi(name))
			return true;
	return false;
}

const Jupiter::ReadableString *RenX::Server::getMapName(const Jupiter::ReadableString &name) const
{
	size_t index = RenX::Server::maps.size();
	const Jupiter::ReadableString *map_name;
	while (index != 0)
	{
		map_name = RenX::Server::maps.get(--index);
		if (map_name->findi(name) != Jupiter::INVALID_INDEX)
			return map_name;
	}
	return nullptr;
}

const Jupiter::ReadableString &RenX::Server::getCurrentRCONCommand() const
{
	return RenX::Server::lastCommand;
}

const Jupiter::ReadableString &RenX::Server::getCurrentRCONCommandParameters() const
{
	return RenX::Server::lastCommandParams;
}

std::chrono::milliseconds RenX::Server::getGameTime() const
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - RenX::Server::gameStart);
}

std::chrono::milliseconds RenX::Server::getGameTime(const RenX::PlayerInfo *player) const
{
	if (player->joinTime < RenX::Server::gameStart)
		return RenX::Server::getGameTime();
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - player->joinTime);
}

RenX::PlayerInfo *RenX::Server::getPlayer(int id) const
{
	if (RenX::Server::players.size() == 0) return nullptr;
	for (Jupiter::DLList<RenX::PlayerInfo>::Node *node = RenX::Server::players.getNode(0); node != nullptr; node = node->next)
		if (node->data->id == id)
			return node->data;
	return nullptr;
}

RenX::PlayerInfo *RenX::Server::getPlayerByName(const Jupiter::ReadableString &name) const
{
	if (RenX::Server::players.size() == 0) return nullptr;

	for (Jupiter::DLList<RenX::PlayerInfo>::Node *node = RenX::Server::players.getNode(0); node != nullptr; node = node->next)
		if (node->data->name == name)
			return node->data;

	Jupiter::ReferenceString idToken = name;
	if (name.matchi("Player?*"))
		idToken.shiftRight(6);
	else if (name.matchi("pid?*"))
		idToken.shiftRight(3);
	else return nullptr;
	int id = idToken.asInt(10);

	for (Jupiter::DLList<RenX::PlayerInfo>::Node *node = RenX::Server::players.getNode(0); node != nullptr; node = node->next)
		if (node->data->id == id)
			return node->data;

	return nullptr;
}

RenX::PlayerInfo *RenX::Server::getPlayerByPartName(const Jupiter::ReadableString &partName) const
{
	if (RenX::Server::players.size() == 0) return nullptr;
	RenX::PlayerInfo *r = RenX::Server::getPlayerByName(partName);
	if (r != nullptr) return r;
	return RenX::Server::getPlayerByPartNameFast(partName);
}

RenX::PlayerInfo *RenX::Server::getPlayerByPartNameFast(const Jupiter::ReadableString &partName) const
{
	if (RenX::Server::players.size() == 0) return nullptr;
	for (Jupiter::DLList<RenX::PlayerInfo>::Node *node = RenX::Server::players.getNode(0); node != nullptr; node = node->next)
		if (node->data->name.findi(partName) != Jupiter::INVALID_INDEX)
			return node->data;
	return nullptr;
}

Jupiter::StringS RenX::Server::formatSteamID(const RenX::PlayerInfo *player) const
{
	return RenX::Server::formatSteamID(player->steamid);
}

Jupiter::StringS RenX::Server::formatSteamID(uint64_t id) const
{
	if (id == 0)
		return Jupiter::ReferenceString::empty;

	switch (RenX::Server::steamFormat)
	{
	default:
	case 16:
		return Jupiter::StringS::Format("0x%.16llX", id);
	case 10:
		return Jupiter::StringS::Format("%llu", id);
	case 8:
		return Jupiter::StringS::Format("0%llo", id);
	case -2:
		id -= 0x0110000100000000ULL;
		if (id % 2 == 1)
			return Jupiter::StringS::Format("STEAM_1:1:%llu", id / 2ULL);
		else
			return Jupiter::StringS::Format("STEAM_1:0:%llu", id / 2ULL);
	case -3:
		id -= 0x0110000100000000ULL;
		return Jupiter::StringS::Format("[U:1:%llu]", id);
	}
}

void RenX::Server::kickPlayer(int id, const Jupiter::ReadableString &reason)
{
	if (reason.isEmpty())
		RenX::Server::sock.send(Jupiter::StringS::Format("ckick pid%d\n", id));
	else
		RenX::Server::sock.send(Jupiter::StringS::Format("ckick pid%d %.*s\n", id, reason.size(), reason.ptr()));
}

void RenX::Server::kickPlayer(const RenX::PlayerInfo *player, const Jupiter::ReadableString &reason)
{
	RenX::Server::kickPlayer(player->id, reason);
}

void RenX::Server::forceKickPlayer(int id, const Jupiter::ReadableString &reason)
{
	if (reason.isEmpty())
		RenX::Server::sock.send(Jupiter::StringS::Format("cfkick pid%d You were kicked from the server.\n", id));
	else
		RenX::Server::sock.send(Jupiter::StringS::Format("cfkick pid%d %.*s\n", id, reason.size(), reason.ptr()));
}

void RenX::Server::forceKickPlayer(const RenX::PlayerInfo *player, const Jupiter::ReadableString &reason)
{
	RenX::Server::forceKickPlayer(player->id, reason);
}

void RenX::Server::banPlayer(int id, const Jupiter::ReadableString &reason)
{
	if (RenX::Server::rconBan)
		RenX::Server::sock.send(Jupiter::StringS::Format("ckickban pid%d %.*s\n", id, reason.size(), reason.ptr()));
	else
	{
		RenX::PlayerInfo *player = RenX::Server::getPlayer(id);
		if (player != nullptr)
			RenX::Server::banPlayer(player, reason);
	}
}

void RenX::Server::banPlayer(const RenX::PlayerInfo *player, const Jupiter::ReadableString &reason, time_t length)
{
	if (RenX::Server::localBan)
		RenX::banDatabase->add(this, player, reason, length);

	if (length == 0)
	{
		if (RenX::Server::rconBan)
			RenX::Server::sock.send(Jupiter::StringS::Format("ckickban pid%d %.*s\n", player->id, reason.size(), reason.ptr()));
		else
			RenX::Server::forceKickPlayer(player, Jupiter::StringS::Format("You are permanently banned from the server for: %.*s", reason.size(), reason.ptr()));
	}
	else
		RenX::Server::forceKickPlayer(player, Jupiter::StringS::Format("You are banned from the server for the next %d days, %d:%d:%d for: %.*s", length/3600, length%3600, length/60, length%60, reason.size(), reason.ptr()));
}

bool RenX::Server::removePlayer(int id)
{
	if (RenX::Server::players.size() == 0) return false;
	for (Jupiter::DLList<RenX::PlayerInfo>::Node *node = RenX::Server::players.getNode(0); node != nullptr; node = node->next)
	{
		if (node->data->id == id)
		{
			RenX::PlayerInfo *p = RenX::Server::players.remove(node);
			Jupiter::ArrayList<RenX::Plugin> &xPlugins = *RenX::getCore()->getPlugins();
			for (size_t i = 0; i < xPlugins.size(); i++)
				xPlugins.get(i)->RenX_OnPlayerDelete(this, p);
			delete p;
			return true;
		}
	}
	return false;
}

bool RenX::Server::removePlayer(RenX::PlayerInfo *player)
{
	return RenX::Server::removePlayer(player->id);
}

bool RenX::Server::fetchClientList()
{
	RenX::Server::lastClientListUpdate = std::chrono::steady_clock::now();
	return RenX::Server::sock.send(STRING_LITERAL_AS_REFERENCE("cclientvarlist KILLS\xA0""DEATHS\xA0""SCORE\xA0""CREDITS\xA0""CHARACTER\xA0""VEHICLE\xA0""PING\xA0""ADMIN\xA0""STEAM\xA0""IP\xA0""PLAYERLOG\n")) > 0
		&& RenX::Server::sock.send(STRING_LITERAL_AS_REFERENCE("cbotvarlist KILLS\xA0""DEATHS\xA0""SCORE\xA0""CREDITS\xA0""CHARACTER\xA0""VEHICLE\xA0""PLAYERLOG\n")) > 0;
}

bool RenX::Server::updateClientList()
{
	RenX::Server::lastClientListUpdate = std::chrono::steady_clock::now();

	size_t botCount = 0;
	for (size_t i = 0; i != RenX::Server::players.size(); i++)
		if (RenX::Server::players.get(i)->isBot)
			botCount++;

	int r = 0;
	if (RenX::Server::players.size() != botCount)
		r = RenX::Server::sock.send(STRING_LITERAL_AS_REFERENCE("cclientvarlist ID\xA0""SCORE\xA0""CREDITS\xA0""PING\n")) > 0;

	if (botCount != 0)
		r |= RenX::Server::sock.send(STRING_LITERAL_AS_REFERENCE("cbotvarlist ID\xA0""SCORE\xA0""CREDITS\n")) > 0;

	return r != 0;
}

bool RenX::Server::updateBuildingList()
{
	RenX::Server::lastBuildingListUpdate = std::chrono::steady_clock::now();
	return RenX::Server::sock.send("cbinfo\n"_jrs) > 0;
}

bool RenX::Server::gameover()
{
	return RenX::Server::send("endmap"_jrs) > 0;
}

bool RenX::Server::setMap(const Jupiter::ReadableString &map)
{
	return RenX::Server::send(Jupiter::StringS::Format("changemap %.*s", map.size(), map.ptr())) > 0;
}

bool RenX::Server::loadMutator(const Jupiter::ReadableString &mutator)
{
	return RenX::Server::send(Jupiter::StringS::Format("loadmutator %.*s", mutator.size(), mutator.ptr())) > 0;
}

bool RenX::Server::unloadMutator(const Jupiter::ReadableString &mutator)
{
	return RenX::Server::send(Jupiter::StringS::Format("unloadmutator %.*s", mutator.size(), mutator.ptr())) > 0;
}

bool RenX::Server::cancelVote(const RenX::TeamType team)
{
	switch (team)
	{
	default:
		return RenX::Server::send("cancelvote -1"_jrs) > 0;
	case TeamType::GDI:
		return RenX::Server::send("cancelvote 0"_jrs) > 0;
	case TeamType::Nod:
		return RenX::Server::send("cancelvote 1"_jrs) > 0;
	}
}

bool RenX::Server::swapTeams()
{
	return RenX::Server::send("swapteams"_jrs) > 0;
}

bool RenX::Server::recordDemo()
{
	return RenX::Server::send("recorddemo"_jrs) > 0;
}

bool RenX::Server::mute(const RenX::PlayerInfo *player)
{
	return RenX::Server::send(Jupiter::StringS::Format("textmute pid%u", player->id)) > 0;
}

bool RenX::Server::unmute(const RenX::PlayerInfo *player)
{
	return RenX::Server::send(Jupiter::StringS::Format("textunmute pid%u", player->id)) > 0;
}

bool RenX::Server::giveCredits(int id, double credits)
{
	return RenX::Server::send(Jupiter::StringS::Format("givecredits pid%d %f", id, credits)) > 0;
}

bool RenX::Server::giveCredits(RenX::PlayerInfo *player, double credits)
{
	return RenX::Server::giveCredits(player->id, credits);
}

bool RenX::Server::kill(int id)
{
	return RenX::Server::send(Jupiter::StringS::Format("kill pid%d", id)) > 0;
}

bool RenX::Server::kill(RenX::PlayerInfo *player)
{
	return RenX::Server::kill(player->id);
}

bool RenX::Server::disarm(int id)
{
	return RenX::Server::send(Jupiter::StringS::Format("disarm pid%d", id)) > 0;
}

bool RenX::Server::disarm(RenX::PlayerInfo *player)
{
	return RenX::Server::disarm(player->id);
}

bool RenX::Server::disarmC4(int id)
{
	return RenX::Server::send(Jupiter::StringS::Format("disarmc4 pid%d", id)) > 0;
}

bool RenX::Server::disarmC4(RenX::PlayerInfo *player)
{
	return RenX::Server::disarmC4(player->id);
}

bool RenX::Server::disarmBeacon(int id)
{
	return RenX::Server::send(Jupiter::StringS::Format("disarmb pid%d", id)) > 0;
}

bool RenX::Server::disarmBeacon(RenX::PlayerInfo *player)
{
	return RenX::Server::disarmBeacon(player->id);
}

bool RenX::Server::changeTeam(int id, bool resetCredits)
{
	return RenX::Server::send(Jupiter::StringS::Format(resetCredits ? "team pid%d" : "team2 pid%d", id)) > 0;
}

bool RenX::Server::changeTeam(RenX::PlayerInfo *player, bool resetCredits)
{
	return RenX::Server::changeTeam(player->id, resetCredits);
}

const Jupiter::ReadableString &RenX::Server::getPrefix() const
{
	static Jupiter::String parsed;
	RenX::processTags(parsed = RenX::Server::IRCPrefix, this);
	return parsed;
}

void RenX::Server::setPrefix(const Jupiter::ReadableString &prefix)
{
	RenX::sanitizeTags(RenX::Server::IRCPrefix = prefix);
}

const Jupiter::ReadableString &RenX::Server::getCommandPrefix() const
{
	return RenX::Server::CommandPrefix;
}

void RenX::Server::setCommandPrefix(const Jupiter::ReadableString &prefix)
{
	RenX::Server::CommandPrefix = prefix;
}

const Jupiter::ReadableString &RenX::Server::getRules() const
{
	return RenX::Server::rules;
}

void RenX::Server::setRules(const Jupiter::ReadableString &rules)
{
	RenX::Server::rules = rules;
	Jupiter::IRC::Client::Config->set(RenX::Server::configSection, "Rules"_jrs, rules);
	RenX::Server::sendMessage(Jupiter::StringS::Format("NOTICE: The rules have been modified! Rules: %.*s", rules.size(), rules.ptr()));
}

const Jupiter::ReadableString &RenX::Server::getHostname() const
{
	return RenX::Server::hostname;
}

unsigned short RenX::Server::getPort() const
{
	return RenX::Server::port;
}

const Jupiter::ReadableString &RenX::Server::getSocketHostname() const
{
	return RenX::Server::sock.getHostname();
}

unsigned short RenX::Server::getSocketPort() const
{
	return RenX::Server::sock.getPort();
}

std::chrono::steady_clock::time_point RenX::Server::getLastAttempt() const
{
	return RenX::Server::lastAttempt;
}

std::chrono::milliseconds RenX::Server::getDelay() const
{
	return RenX::Server::delay;
}

bool RenX::Server::isPassworded() const
{
	return RenX::Server::passworded;
}

const Jupiter::ReadableString &RenX::Server::getPassword() const
{
	return RenX::Server::pass;
}

const Jupiter::ReadableString &RenX::Server::getUser() const
{
	return RenX::Server::rconUser;
}

const Jupiter::ReadableString &RenX::Server::getName() const
{
	return RenX::Server::serverName;
}

const Jupiter::ReadableString &RenX::Server::getMap() const
{
	return RenX::Server::map;
}

RenX::GameCommand *RenX::Server::getCommand(unsigned int index) const
{
	return RenX::Server::commands.get(index);
}

RenX::GameCommand *RenX::Server::getCommand(const Jupiter::ReadableString &trigger) const
{
	RenX::GameCommand *cmd;
	for (size_t i = 0; i != RenX::Server::commands.size(); i++)
	{
		cmd = RenX::Server::commands.get(i);
		if (cmd->matches(trigger))
			return cmd;
	}
	return nullptr;
}

unsigned int RenX::Server::getCommandCount() const
{
	return RenX::Server::commands.size();
}

unsigned int RenX::Server::triggerCommand(const Jupiter::ReadableString &trigger, RenX::PlayerInfo *player, const Jupiter::ReadableString &parameters)
{
	unsigned int r = 0;
	RenX::GameCommand *cmd;
	for (size_t i = 0; i < RenX::Server::commands.size(); i++)
	{
		cmd = RenX::Server::commands.get(i);
		if (cmd->matches(trigger))
		{
			if (player->access >= cmd->getAccessLevel())
				cmd->trigger(this, player, parameters);
			else
				RenX::Server::sendMessage(player, "Access Denied."_jrs);
			++r;
		}
	}
	return r;
}

void RenX::Server::addCommand(RenX::GameCommand *command)
{
	RenX::Server::commands.add(command);
	if (RenX::Server::commandAccessLevels != nullptr)
	{
		const Jupiter::ReadableString &accessLevel = RenX::Server::commandAccessLevels->get(command->getTrigger());
		if (accessLevel.isNotEmpty())
			command->setAccessLevel(accessLevel.asInt());
	}
	if (RenX::Server::commandAliases != nullptr)
	{
		const Jupiter::ReadableString &aliasList = RenX::Server::commandAliases->get(command->getTrigger());
		unsigned int j = aliasList.wordCount(WHITESPACE);
		while (j != 0)
			command->addTrigger(Jupiter::ReferenceString::getWord(aliasList, --j, WHITESPACE));
	}
}

bool RenX::Server::removeCommand(RenX::GameCommand *command)
{
	for (size_t i = 0; i != RenX::Server::commands.size(); i++)
		if (RenX::Server::commands.get(i) == command)
		{
			delete RenX::Server::commands.remove(i);
			return true;
		}
	return false;
}

bool RenX::Server::removeCommand(const Jupiter::ReadableString &trigger)
{
	for (size_t i = 0; i != RenX::Server::commands.size(); i++)
		if (RenX::Server::commands.get(i)->matches(trigger))
		{
			delete RenX::Server::commands.remove(i);
			return true;
		}
	return false;
}

void RenX::Server::setUUIDFunction(RenX::Server::uuid_func func)
{
	RenX::Server::calc_uuid = func;
	if (RenX::Server::players.size() != 0)
	{
		Jupiter::DLList<PlayerInfo>::Node *node = RenX::Server::players.getNode(0);
		do
		{
			RenX::Server::setUUIDIfDifferent(node->data, RenX::Server::calc_uuid(node->data));
			node = node->next;
		}
		while (node != nullptr);
	}
}

RenX::Server::uuid_func RenX::Server::getUUIDFunction() const
{
	return RenX::Server::calc_uuid;
}

void RenX::Server::setUUID(RenX::PlayerInfo *player, const Jupiter::ReadableString &uuid)
{
	Jupiter::ArrayList<RenX::Plugin> &xPlugins = *getCore()->getPlugins();
	for (size_t index = 0; index < xPlugins.size(); index++)
		xPlugins.get(index)->RenX_OnPlayerUUIDChange(this, player, uuid);
	player->uuid = uuid;
}

bool RenX::Server::setUUIDIfDifferent(RenX::PlayerInfo *player, const Jupiter::ReadableString &uuid)
{
	if (player->uuid.equals(uuid))
		return false;
	setUUID(player, uuid);
	return true;
}

void RenX::Server::sendPubChan(const char *fmt, ...) const
{
	va_list args;
	va_start(args, fmt);
	Jupiter::StringL msg;
	const Jupiter::ReadableString &serverPrefix = RenX::Server::getPrefix();
	if (serverPrefix.isNotEmpty())
	{
		msg += serverPrefix;
		msg += ' ';
		msg.avformat(fmt, args);
	}
	else msg.vformat(fmt, args);
	va_end(args);
	for (size_t i = 0; i != serverManager->size(); i++)
		serverManager->getServer(i)->messageChannels(RenX::Server::logChanType, msg);
}

void RenX::Server::sendPubChan(const Jupiter::ReadableString &msg) const
{
	const Jupiter::ReadableString &prefix = this->getPrefix();
	if (prefix.isNotEmpty())
	{
		Jupiter::String m(msg.size() + prefix.size() + 1);
		m.set(prefix);
		m += ' ';
		m += msg;
		for (size_t i = 0; i != serverManager->size(); i++)
			serverManager->getServer(i)->messageChannels(RenX::Server::logChanType, m);
	}
	else
		for (size_t i = 0; i != serverManager->size(); i++)
			serverManager->getServer(i)->messageChannels(RenX::Server::logChanType, msg);
}

void RenX::Server::sendAdmChan(const char *fmt, ...) const
{
	va_list args;
	va_start(args, fmt);
	Jupiter::StringL msg;
	const Jupiter::ReadableString &serverPrefix = RenX::Server::getPrefix();
	if (serverPrefix.isNotEmpty())
	{
		msg += serverPrefix;
		msg += ' ';
		msg.avformat(fmt, args);
	}
	else msg.vformat(fmt, args);
	va_end(args);
	for (size_t i = 0; i != serverManager->size(); i++)
		serverManager->getServer(i)->messageChannels(RenX::Server::adminLogChanType, msg);
}

void RenX::Server::sendAdmChan(const Jupiter::ReadableString &msg) const
{
	const Jupiter::ReadableString &prefix = this->getPrefix();
	if (prefix.isNotEmpty())
	{
		Jupiter::String m(msg.size() + prefix.size() + 1);
		m.set(prefix);
		m += ' ';
		m += msg;
		for (size_t i = 0; i != serverManager->size(); i++)
			serverManager->getServer(i)->messageChannels(RenX::Server::adminLogChanType, m);
	}
	else
		for (size_t i = 0; i != serverManager->size(); i++)
			serverManager->getServer(i)->messageChannels(RenX::Server::adminLogChanType, msg);
}

void RenX::Server::sendLogChan(const char *fmt, ...) const
{
	IRC_Bot *server;
	va_list args;
	va_start(args, fmt);
	Jupiter::StringL msg;
	const Jupiter::ReadableString &serverPrefix = RenX::Server::getPrefix();
	if (serverPrefix.isNotEmpty())
	{
		msg += serverPrefix;
		msg += ' ';
		msg.avformat(fmt, args);
	}
	else msg.vformat(fmt, args);
	va_end(args);
	for (size_t i = 0; i != serverManager->size(); i++)
	{
		server = serverManager->getServer(i);
		server->messageChannels(RenX::Server::logChanType, msg);
		server->messageChannels(RenX::Server::adminLogChanType, msg);
	}
}

void RenX::Server::sendLogChan(const Jupiter::ReadableString &msg) const
{
	IRC_Bot *server;
	const Jupiter::ReadableString &prefix = this->getPrefix();
	if (prefix.isNotEmpty())
	{
		Jupiter::String m(msg.size() + prefix.size() + 1);
		m.set(prefix);
		m += ' ';
		m += msg;
		for (size_t i = 0; i != serverManager->size(); i++)
		{
			server = serverManager->getServer(i);
			server->messageChannels(RenX::Server::logChanType, m);
			server->messageChannels(RenX::Server::adminLogChanType, m);
		}
	}
	else
		for (size_t i = 0; i != serverManager->size(); i++)
		{
			server = serverManager->getServer(i);
			server->messageChannels(RenX::Server::logChanType, msg);
			server->messageChannels(RenX::Server::adminLogChanType, msg);
		}
}

#define PARSE_PLAYER_DATA_P(DATA) \
	Jupiter::ReferenceString name; \
	TeamType team; \
	int id; \
	bool isBot; \
	parsePlayerData(DATA, name, team, id, isBot);

void RenX::Server::processLine(const Jupiter::ReadableString &line)
{
	if (line.isEmpty())
		return;

	Jupiter::ArrayList<RenX::Plugin> &xPlugins = *RenX::getCore()->getPlugins();
	Jupiter::ReadableString::TokenizeResult<Jupiter::String_Strict> tokens = Jupiter::StringS::tokenize(line, RenX::DelimC);

	for (size_t index = 0; index != tokens.token_count; ++index)
		tokens.tokens[index].processEscapeSequences();

	/** Local functions */
	auto onPreGameOver = [this](RenX::WinType winType, RenX::TeamType team, int gScore, int nScore)
	{
		RenX::PlayerInfo *player;

		if (this->players.size() != 0)
		{
			for (Jupiter::DLList<RenX::PlayerInfo>::Node *n = this->players.getNode(0); n != nullptr; n = n->next)
			{
				player = n->data;
				if (player != nullptr)
				{
					if (player->team == team)
						player->wins++;
					else player->loses++;
				}
			}
		}
	};
	auto onMapChange = [this]()
	{
		this->firstAction = false;
		this->firstKill = false;
		this->firstDeath = false;
		RenX::PlayerInfo *player;

		if (this->players.size() != 0)
		{
			for (Jupiter::DLList<RenX::PlayerInfo>::Node *n = this->players.getNode(0); n != nullptr; n = n->next)
			{
				player = n->data;
				if (player != nullptr)
				{
					player->score = 0.0f;
					player->credits = 0.0f;
					player->kills = 0;
					player->deaths = 0;
					player->suicides = 0;
					player->headshots = 0;
					player->vehicleKills = 0;
					player->buildingKills = 0;
					player->defenceKills = 0;
					player->beaconPlacements = 0;
					player->beaconDisarms = 0;
					player->proxy_placements = 0;
					player->proxy_disarms = 0;
					player->captures = 0;
					player->steals = 0;
					player->stolen = 0;
				}
			}
		}
	};
	auto onChat = [this](RenX::PlayerInfo *player, const Jupiter::ReadableString &message)
	{
		const Jupiter::ReadableString &prefix = this->getCommandPrefix();
		if (message.find(prefix) == 0 && message.size() != prefix.size())
		{
			Jupiter::ReferenceString command;
			Jupiter::ReferenceString parameters;
			if (containsSymbol(WHITESPACE, message.get(prefix.size())))
			{
				command = Jupiter::ReferenceString::getWord(message, 1, WHITESPACE);
				parameters = Jupiter::ReferenceString::gotoWord(message, 2, WHITESPACE);
			}
			else
			{
				command = Jupiter::ReferenceString::getWord(message, 0, WHITESPACE);
				command.shiftRight(prefix.size());
				parameters = Jupiter::ReferenceString::gotoWord(message, 1, WHITESPACE);
			}
			this->triggerCommand(command, player, parameters);
		}
	};
	auto onAction = [this]()
	{
		if (this->firstAction == false)
		{
			this->firstAction = true;
			this->silenceJoins = false;
		}
	};
	auto parsePlayerData = [this](const Jupiter::ReadableString &data, Jupiter::ReferenceString &name, TeamType &team, int &id, bool &isBot)
	{
		Jupiter::ReferenceString idToken = Jupiter::ReferenceString::getToken(data, 1, ',');
		name = Jupiter::ReferenceString::gotoToken(data, 2, ',');
		team = RenX::getTeam(Jupiter::ReferenceString::getToken(data, 0, ','));
		if (idToken.get(0) == 'b')
		{
			idToken.shiftRight(1);
			isBot = true;
		}
		else
			isBot = false;
		id = idToken.asInt(10);
	};
	auto banCheck = [this](const RenX::PlayerInfo *player)
	{
		const Jupiter::ArrayList<RenX::BanDatabase::Entry> &entries = RenX::banDatabase->getEntries();
		RenX::BanDatabase::Entry *entry = nullptr;
		for (size_t i = 0; i != entries.size(); i++)
		{
			entry = entries.get(i);
			if (entry->active)
			{
				if (entry->length != 0 && entry->timestamp + entry->length < time(0))
					banDatabase->deactivate(i);
				else if ((this->localSteamBan && entry->steamid != 0 && entry->steamid == player->steamid)
					|| (this->localIPBan && entry->ip != 0 && entry->ip == player->ip32)
					|| (this->localNameBan && entry->name.isNotEmpty() && entry->name.equalsi(player->name)))
				{
					char timeStr[256];
					if (entry->length == 0)
					{
						strftime(timeStr, sizeof(timeStr), "%b %d %Y at %H:%M:%S", localtime(&(entry->timestamp)));
						this->forceKickPlayer(player, Jupiter::StringS::Format("You are were permanently banned from the server on %s for: %.*s", timeStr, entry->reason.size(), entry->reason.ptr()));
					}
					else
					{
						strftime(timeStr, sizeof(timeStr), "%b %d %Y at %H:%M:%S", localtime(std::addressof<const time_t>(entry->timestamp + entry->length)));
						this->forceKickPlayer(player, Jupiter::StringS::Format("You are banned from the server until %s for: %.*s", timeStr, entry->reason.size(), entry->reason.ptr()));
					}
					return;
				}
			}
		}
	};
	auto getPlayerOrAdd = [&](const Jupiter::ReadableString &name, int id, RenX::TeamType team, bool isBot, uint64_t steamid, const Jupiter::ReadableString &ip)
	{
		RenX::PlayerInfo *r = this->getPlayer(id);
		if (r == nullptr)
		{
			r = new RenX::PlayerInfo();
			r->id = id;
			r->name = name;
			r->team = team;
			r->ip = ip;
			r->ip32 = Jupiter::Socket::pton4(Jupiter::CStringS(r->ip).c_str());
			r->steamid = steamid;
			if (r->isBot = isBot)
				r->formatNamePrefix = IRCCOLOR "05[B]";
			r->joinTime = std::chrono::steady_clock::now();
			if (id != 0)
				this->players.add(r);

			r->uuid = calc_uuid(r);

			for (size_t i = 0; i < xPlugins.size(); i++)
				xPlugins.get(i)->RenX_OnPlayerCreate(this, r);
		}
		else
		{
			bool recalcUUID = false;
			r->team = team;
			if (r->ip32 == 0 && ip.isNotEmpty())
			{
				r->ip = ip;
				r->ip32 = Jupiter::Socket::pton4(Jupiter::CStringS(r->ip).c_str());
				recalcUUID = true;
			}
			if (r->steamid == 0U && steamid != 0U)
			{
				r->steamid = steamid;
				recalcUUID = true;
			}
			if (r->name.isEmpty())
			{
				r->name = name;
				recalcUUID = true;
			}
			if (recalcUUID)
			{
				this->setUUIDIfDifferent(r, calc_uuid(r));
				banCheck(r);
			}
		}
		return r;
	};
	auto parseGetPlayerOrAdd = [&parsePlayerData, &getPlayerOrAdd](const Jupiter::ReadableString &token)
	{
		PARSE_PLAYER_DATA_P(token);
		return getPlayerOrAdd(name, id, team, isBot, 0U, Jupiter::ReferenceString::empty);
	};
	auto gotoToken = [&line, &tokens](size_t index)
	{
		if (index >= tokens.token_count)
			return Jupiter::ReferenceString::empty;

		size_t offset = index;
		while (index != 0)
			offset += tokens.tokens[--index].size();

		return Jupiter::ReferenceString::substring(line, offset + 1);
	};

	if (tokens.tokens[0].isNotEmpty())
	{
		char header = tokens.tokens[0].get(0);
		tokens.tokens[0].shiftRight(1);
		switch (header)
		{
		case 'r':
			if (this->lastCommand.equalsi("clientlist"_jrs))
			{
				// ID | IP | Steam ID | Admin Status | Team | Name
				if (tokens.tokens[0].isNotEmpty())
				{
					bool isBot = false;
					int id;
					uint64_t steamid = 0;
					RenX::TeamType team = TeamType::Other;
					Jupiter::ReferenceString steamToken = tokens.getToken(2);
					Jupiter::ReferenceString adminToken = tokens.getToken(3);
					Jupiter::ReferenceString teamToken = tokens.getToken(4);
					if (tokens.tokens[0].get(0) == 'b')
					{
						isBot = true;
						tokens.tokens[0].shiftRight(1);
						id = tokens.tokens[0].asInt();
						tokens.tokens[0].shiftLeft(1);
					}
					else
						id = tokens.tokens[0].asInt();

					if (steamToken.equals("-----NO-STEAM-----") == false)
						steamid = steamToken.asUnsignedLongLong();
					team = RenX::getTeam(teamToken);

					if (adminToken.equalsi("None"_jrs))
						getPlayerOrAdd(tokens.getToken(5), id, team, isBot, steamid, tokens.getToken(1));
					else
						getPlayerOrAdd(tokens.getToken(5), id, team, isBot, steamid, tokens.getToken(1))->adminType = adminToken;
				}
			}
			else if (this->lastCommand.equalsi("clientvarlist"_jrs))
			{
				if (this->commandListFormat.token_count == 0)
					this->commandListFormat = tokens;
				else
				{
					/*
					lRCON Command; Conn4 executed: clientvarlist PlayerLog Kills PlayerKills BotKills Deaths Score Credits Character BoundVehicle Vehicle Spy RemoteC4 ATMine KDR Ping Admin Steam IP ID Name Team TeamNum
					rPlayerLog Kills PlayerKills BotKills Deaths Score Credits Character BoundVehicle Vehicle Spy RemoteC4 ATMine KDR Ping Admin Steam IP ID Name Team TeamNum
					rGDI,256,EKT-J 0 0 0 0 0 5217.9629 Rx_FamilyInfo_GDI_Soldier   False 0 0 0.0000 8 None 0x0110000104AE0666 127.0.0.1 256 EKT-J GDI 0
					*/
					Jupiter::INIFile::Section table;
					size_t i = tokens.token_count;
					while (i-- != 0)
						table.set(this->commandListFormat.getToken(i), tokens.getToken(i));
					auto parse = [&table](RenX::PlayerInfo *player)
					{
						Jupiter::INIFile::Section::KeyValuePair *pair;

						pair = table.getPair("Kills"_jrs);
						if (pair != nullptr)
							player->kills = pair->getValue().asUnsignedInt();

						pair = table.getPair("Deaths"_jrs);
						if (pair != nullptr)
							player->deaths = pair->getValue().asUnsignedInt();

						pair = table.getPair("Score"_jrs);
						if (pair != nullptr)
							player->score = pair->getValue().asDouble();

						pair = table.getPair("Credits"_jrs);
						if (pair != nullptr)
							player->credits = pair->getValue().asDouble();

						pair = table.getPair("Character"_jrs);
						if (pair != nullptr)
							player->character = pair->getValue();

						pair = table.getPair("Vehicle"_jrs);
						if (pair != nullptr)
							player->vehicle = pair->getValue();

						pair = table.getPair("Ping"_jrs);
						if (pair != nullptr)
							player->ping = pair->getValue().asUnsignedInt();

						pair = table.getPair("Admin"_jrs);
						if (pair != nullptr)
						{
							if (pair->getValue().equals("None"_jrs))
								player->adminType = "";
							else
								player->adminType = pair->getValue();
						}
					};
					Jupiter::INIFile::Section::KeyValuePair *pair = table.getPair("PlayerLog"_jrs);
					if (pair != nullptr)
						parse(getPlayerOrAdd(Jupiter::ReferenceString::getToken(pair->getValue(), 2, ','), Jupiter::ReferenceString::getToken(pair->getValue(), 1, ',').asInt(), RenX::getTeam(Jupiter::ReferenceString::getToken(pair->getValue(), 0, ',')), false, table.get("STEAM"_jrs).asUnsignedLongLong(), table.get("IP"_jrs)));
					else
					{
						Jupiter::INIFile::Section::KeyValuePair *namePair = table.getPair("Name"_jrs);
						pair = table.getPair("ID"_jrs);

						if (pair != nullptr)
						{
							RenX::PlayerInfo *player = getPlayer(pair->getValue().asInt());
							if (player != nullptr)
							{
								if (player->name.isEmpty())
								{
									player->name = table.get("Name"_jrs);
									player->name.processEscapeSequences();
								}
								if (player->ip.isEmpty())
									player->ip = table.get("IP"_jrs);
								if (player->steamid == 0)
								{
									uint64_t steamid = table.get("STEAM"_jrs).asUnsignedLongLong();
									if (steamid != 0)
									{
										player->steamid = steamid;
										if (calc_uuid == RenX::default_uuid_func)
											setUUID(player, this->formatSteamID(steamid));
										else
											this->setUUIDIfDifferent(player, calc_uuid(player));
									}
								}

								pair = table.getPair("TeamNum"_jrs);
								if (pair != nullptr)
									player->team = RenX::getTeam(pair->getValue().asInt());
								else
								{
									pair = table.getPair("Team"_jrs);
									if (pair != nullptr)
										player->team = RenX::getTeam(pair->getValue());
								}

								parse(player);
							}
							// I *could* try and fetch a player by name, but that seems like it *could* open a security hole.
							// In addition, would I update their ID?
						}
						else if (namePair != nullptr)
						{
							RenX::PlayerInfo *player = getPlayerByName(namePair->getValue());
							if (player != nullptr)
							{
								if (player->ip.isEmpty())
									player->ip = table.get("IP"_jrs);
								if (player->steamid == 0)
								{
									uint64_t steamid = table.get("STEAM"_jrs).asUnsignedLongLong();
									if (steamid != 0)
									{
										player->steamid = steamid;
										if (calc_uuid == RenX::default_uuid_func)
											setUUID(player, this->formatSteamID(steamid));
										else
											this->setUUIDIfDifferent(player, calc_uuid(player));
									}
								}

								pair = table.getPair("TeamNum"_jrs);
								if (pair != nullptr)
									player->team = RenX::getTeam(pair->getValue().asInt());
								else
								{
									pair = table.getPair("Team"_jrs);
									if (pair != nullptr)
										player->team = RenX::getTeam(pair->getValue());
								}

								parse(player);
							}
							// No other way to identify player -- worthless command format.
						}
					}
				}
			}
			else if (this->lastCommand.equalsi("botlist"))
			{
				// Team,ID,Name
				if (this->commandListFormat.token_count == 0)
					this->commandListFormat = tokens;
				else
					parseGetPlayerOrAdd(tokens.tokens[0]);
			}
			else if (this->lastCommand.equalsi("botvarlist"))
			{
				if (this->commandListFormat.token_count == 0)
					this->commandListFormat = tokens;
				else
				{
					/*
					lRCON Command; Conn4 executed: clientvarlist PlayerLog Kills PlayerKills BotKills Deaths Score Credits Character BoundVehicle Vehicle Spy RemoteC4 ATMine KDR Ping Admin Steam IP ID Name Team TeamNum
					rPlayerLog Kills PlayerKills BotKills Deaths Score Credits Character BoundVehicle Vehicle Spy RemoteC4 ATMine KDR Ping Admin Steam IP ID Name Team TeamNum
					rGDI,256,EKT-J 0 0 0 0 0 5217.9629 Rx_FamilyInfo_GDI_Soldier   False 0 0 0.0000 8 None 0x0110000104AE0666 127.0.0.1 256 EKT-J GDI 0
					*/
					Jupiter::INIFile::Section table;
					size_t i = tokens.token_count;
					while (i-- != 0)
						table.set(this->commandListFormat.getToken(i), tokens.getToken(i));
					auto parse = [&table](RenX::PlayerInfo *player)
					{
						Jupiter::INIFile::Section::KeyValuePair *pair;

						pair = table.getPair("Kills"_jrs);
						if (pair != nullptr)
							player->kills = pair->getValue().asUnsignedInt();

						pair = table.getPair("Deaths"_jrs);
						if (pair != nullptr)
							player->deaths = pair->getValue().asUnsignedInt();

						pair = table.getPair("Score"_jrs);
						if (pair != nullptr)
							player->score = pair->getValue().asDouble();

						pair = table.getPair("Credits"_jrs);
						if (pair != nullptr)
							player->credits = pair->getValue().asDouble();

						pair = table.getPair("Character"_jrs);
						if (pair != nullptr)
							player->character = pair->getValue();

						pair = table.getPair("Vehicle"_jrs);
						if (pair != nullptr)
							player->vehicle = pair->getValue();
					};
					Jupiter::INIFile::Section::KeyValuePair *pair = table.getPair("PlayerLog"_jrs);
					if (pair != nullptr)
						parse(getPlayerOrAdd(Jupiter::ReferenceString::getToken(pair->getValue(), 2, ','), Jupiter::ReferenceString::getToken(pair->getValue(), 1, ',').substring(1).asInt(), RenX::getTeam(Jupiter::ReferenceString::getToken(pair->getValue(), 0, ',')), true, 0ULL, Jupiter::ReferenceString::empty));
					else
					{
						Jupiter::INIFile::Section::KeyValuePair *namePair = table.getPair("Name"_jrs);
						pair = table.getPair("ID"_jrs);

						if (pair != nullptr)
						{
							RenX::PlayerInfo *player = getPlayer(pair->getValue().asInt());
							if (player != nullptr)
							{
								if (player->name.isEmpty())
								{
									player->name = table.get("Name"_jrs);
									player->name.processEscapeSequences();
								}

								pair = table.getPair("TeamNum"_jrs);
								if (pair != nullptr)
									player->team = RenX::getTeam(pair->getValue().asInt());
								else
								{
									pair = table.getPair("Team"_jrs);
									if (pair != nullptr)
										player->team = RenX::getTeam(pair->getValue());
								}

								parse(player);
							}
						}
						else if (namePair != nullptr)
						{
							RenX::PlayerInfo *player = getPlayerByName(namePair->getValue());
							if (player != nullptr)
							{
								pair = table.getPair("TeamNum"_jrs);
								if (pair != nullptr)
									player->team = RenX::getTeam(pair->getValue().asInt());
								else
								{
									pair = table.getPair("Team"_jrs);
									if (pair != nullptr)
										player->team = RenX::getTeam(pair->getValue());
								}

								parse(player);
							}
							// No other way to identify player -- worthless command format.
						}
					}
				}
			}
			else if (this->lastCommand.equalsi("binfo") || this->lastCommand.equalsi("buildinginfo") || this->lastCommand.equalsi("blist") || this->lastCommand.equalsi("buildinglist"))
			{
				if (this->commandListFormat.token_count == 0)
					this->commandListFormat = tokens;
				else
				{
					/*
					lRCON Command; DevBot executed: binfo
					rBuilding Health MaxHealth Team Capturable
					rRx_Building_Refinery_GDI 4000 4000 GDI False
					*/
					Jupiter::INIFile::Section table;
					size_t i = tokens.token_count;
					while (i-- != 0)
						table.set(this->commandListFormat.getToken(i), tokens.getToken(i));

					Jupiter::INIFile::Section::KeyValuePair *pair;
					RenX::BuildingInfo *building;

					pair = table.getPair("Building"_jrs);
					if (pair != nullptr)
					{
						building = this->getBuildingByName(pair->getValue());
						if (building == nullptr)
						{
							building = new RenX::BuildingInfo();
							RenX::Server::buildings.add(building);
							building->name = pair->getValue();
						}

						pair = table.getPair("Health"_jrs);
						if (pair != nullptr)
							building->health = pair->getValue().asInt(10);

						pair = table.getPair("MaxHealth"_jrs);
						if (pair != nullptr)
							building->max_health = pair->getValue().asInt(10);

						pair = table.getPair("Team"_jrs);
						if (pair != nullptr)
							building->team = RenX::getTeam(pair->getValue());

						pair = table.getPair("Capturable"_jrs);
						if (pair != nullptr)
							building->capturable = pair->getValue().asBool();
					}
				}
			}
			else if (this->lastCommand.equalsi("ping"))
				RenX::Server::awaitingPong = false;
			else if (this->lastCommand.equalsi("map"))
				this->map = std::move(Jupiter::StringS::substring(line, 1));
			else if (this->lastCommand.equalsi("serverinfo"))
			{
				if (this->lastCommandParams.isEmpty())
				{
					// "Port" | Port | "Name" | Name | "Level" | Level | "Players" | Players | "Bots" | Bots
					this->port = static_cast<unsigned short>(tokens.getToken(1).asUnsignedInt(10));
					this->serverName = tokens.getToken(3);
					this->map = tokens.getToken(5);
				}
			}
			else if (this->lastCommand.equalsi("gameinfo"_jrs))
			{
				// "PlayerLimit" | PlayerLimit | "VehicleLimit" | VehicleLimit | "MineLimit" | MineLimit | "TimeLimit" | TimeLimit | "bPassworded" | bPassworded | "bSteamRequired" | bSteamRequired | "bPrivateMessageTeamOnly" | bPrivateMessageTeamOnly | "bAllowPrivateMessaging" | bAllowPrivateMessaging | "bAutoBalanceTeams" | bAutoBalanceTeams | "bSpawnCrates" | bSpawnCrates | "CrateRespawnAfterPickup" | CrateRespawnAfterPickup
				this->playerLimit = tokens.getToken(1).asInt();
				this->vehicleLimit = tokens.getToken(3).asInt();
				this->mineLimit = tokens.getToken(5).asInt();
				this->timeLimit = tokens.getToken(7).asInt();
				this->passworded = tokens.getToken(9).asBool();
				this->steamRequired = tokens.getToken(11).asBool();
				this->privateMessageTeamOnly = tokens.getToken(13).asBool();
				this->allowPrivateMessaging = tokens.getToken(15).asBool();
				this->autoBalanceTeams = tokens.getToken(17).asBool();
				this->spawnCrates = tokens.getToken(19).asBool();
				this->crateRespawnAfterPickup = tokens.getToken(21).asDouble();
			}
			else if (this->lastCommand.equalsi("mutatorlist"_jrs))
			{
				// "The following mutators are loaded:" [ | Mutator [ | Mutator [ ... ] ] ]
				if (tokens.token_count == 1)
					RenX::Server::pure = true;
				else if (tokens.token_count == 0)
					RenX::Server::disconnect(RenX::DisconnectReason::ProtocolError);
				else
				{
					RenX::Server::mutators.emptyAndDelete();
					size_t index = tokens.token_count;
					while (--index != 0)
						RenX::Server::mutators.add(new Jupiter::StringS(tokens.tokens[index]));
				}
			}
			else if (this->lastCommand.equalsi("rotation"_jrs))
			{
				// Map
				Jupiter::ReferenceString in_map = Jupiter::ReferenceString::substring(line, 1);
				if (this->hasMapInRotation(in_map) == false)
					this->maps.add(new Jupiter::StringS(in_map));
			}
			else if (this->lastCommand.equalsi("changename"))
			{
				RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(0));
				Jupiter::StringS newName = tokens.getToken(2);
				for (size_t i = 0; i < xPlugins.size(); i++)
					xPlugins.get(i)->RenX_OnNameChange(this, player, newName);
				player->name = newName;
			}
			break;
		case 'l':
			if (RenX::Server::rconVersion >= 3)
			{
				Jupiter::ReferenceString subHeader = tokens.getToken(1);
				if (tokens.tokens[0].equals("GAME"))
				{
					if (subHeader.equals("Deployed;"))
					{
						// Object (Beacon/Mine) | Player
						// Object (Beacon/Mine) | Player | "on" | Surface
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(4));
						Jupiter::ReferenceString objectType = tokens.getToken(2);
						if (objectType.match("*Beacon"))
							++player->beaconPlacements;
						else if (objectType.equals("Rx_Weapon_DeployedProxyC4"_jrs))
							++player->proxy_placements;
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnDeploy(this, player, objectType);
						onAction();
					}
					else if (subHeader.equals("Disarmed;"))
					{
						// Object (Beacon/Mine) | "by" | Player
						// Object (Beacon/Mine) | "by" | Player | "owned by" | Owner
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(4));
						Jupiter::ReferenceString objectType = tokens.getToken(2);
						if (objectType.match("*Beacon"))
							++player->beaconDisarms;
						else if (objectType.equals("Rx_Weapon_DeployedProxyC4"_jrs))
							++player->proxy_disarms;

						if (tokens.getToken(5).equals("owned by"))
						{
							RenX::PlayerInfo *victim = parseGetPlayerOrAdd(tokens.getToken(6));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnDisarm(this, player, objectType, victim);
						}
						else
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnDisarm(this, player, objectType);
						onAction();
					}
					else if (subHeader.equals("Exploded;"))
					{
						// Explosive | "at" | Location
						// Explosive | "at" | Location | "by" | Owner
						Jupiter::ReferenceString explosive = tokens.getToken(2);
						if (tokens.getToken(5).equals("by"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(6));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnExplode(this, player, explosive);
						}
						else
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnExplode(this, explosive);
						onAction();
					}
					else if (subHeader.equals("Captured;"))
					{
						// Team ',' Building | "id" | Building ID | "by" | Player
						Jupiter::ReferenceString teamBuildingToken = tokens.getToken(2);
						Jupiter::ReferenceString building = teamBuildingToken.getToken(1, ',');
						TeamType oldTeam = RenX::getTeam(teamBuildingToken.getToken(0, ','));
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(6));
						player->captures++;
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnCapture(this, player, building, oldTeam);
						onAction();
					}
					else if (subHeader.equals("Neutralized;"))
					{
						// Team ',' Building | "id" | Building ID | "by" | Player
						Jupiter::ReferenceString teamBuildingToken = tokens.getToken(2);
						Jupiter::ReferenceString building = teamBuildingToken.getToken(1, ',');
						TeamType oldTeam = RenX::getTeam(teamBuildingToken.getToken(0, ','));
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(6));
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnNeutralize(this, player, building, oldTeam);
						onAction();
					}
					else if (subHeader.equals("Purchase;"))
					{
						// "character" | Character | "by" | Player
						// "item" | Item | "by" | Player
						// "weapon" | Weapon | "by" | Player
						// "refill" | Player
						// "vehicle" | Vehicle | "by" | Player
						Jupiter::ReferenceString type = tokens.getToken(2);
						Jupiter::ReferenceString obj = tokens.getToken(3);
						if (type.equals("character"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(5));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnCharacterPurchase(this, player, obj);
							player->character = obj;
						}
						else if (type.equals("item"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(5));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnItemPurchase(this, player, obj);
						}
						else if (type.equals("weapon"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(5));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnWeaponPurchase(this, player, obj);
						}
						else if (type.equals("refill"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(obj);
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnRefillPurchase(this, player);
						}
						else if (type.equals("vehicle"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(5));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnVehiclePurchase(this, player, obj);
						}
					}
					else if (subHeader.equals("Spawn;"))
					{
						// "vehicle" | Vehicle Team, Vehicle
						// "player" | Player | "character" | Character
						// "bot" | Player
						if (tokens.getToken(2).equals("vehicle"))
						{
							Jupiter::ReferenceString vehicle = tokens.getToken(3);
							Jupiter::ReferenceString vehicleTeamToken = vehicle.getToken(0, ',');
							vehicle.shiftRight(vehicleTeamToken.size() + 1);
							TeamType team = RenX::getTeam(vehicleTeamToken);
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnVehicleSpawn(this, team, vehicle);
						}
						else if (tokens.getToken(2).equals("player"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(3));
							Jupiter::ReferenceString character = tokens.getToken(5);
							player->character = character;
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnSpawn(this, player, character);
						}
						else if (tokens.getToken(2).equals("bot"))
						{
							RenX::PlayerInfo *bot = parseGetPlayerOrAdd(tokens.getToken(3));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnBotJoin(this, bot);
						}
					}
					else if (subHeader.equals("Crate;"))
					{
						// "vehicle" | Vehicle | "by" | Player
						// "death" | "by" | Player
						// "suicide" | "by" | Player
						// "money" | Amount | "by" | Player
						// "character" | Character | "by" | Player
						// "spy" | Character | "by" | Player
						// "refill" | "by" | Player
						// "timebomb" | "by" | Player
						// "speed" | "by" | Player
						// "nuke" | "by" | Player
						// "abduction" | "by" | Player
						// "by" | Player
						Jupiter::ReferenceString type = tokens.getToken(2);
						if (type.equals("vehicle"))
						{
							Jupiter::ReferenceString vehicle = tokens.getToken(3);
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(5));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnVehicleCrate(this, player, vehicle);
						}
						else if (type.equals("tsvehicle"))
						{
							Jupiter::ReferenceString vehicle = tokens.getToken(3);
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(5));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnVehicleCrate(this, player, vehicle);
						}
						else if (type.equals("ravehicle"))
						{
							Jupiter::ReferenceString vehicle = tokens.getToken(3);
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(5));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnVehicleCrate(this, player, vehicle);
						}
						else if (type.equals("death") || type.equals("suicide"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(4));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnDeathCrate(this, player);
						}
						else if (type.equals("money"))
						{
							int amount = tokens.getToken(3).asInt();
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(5));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnMoneyCrate(this, player, amount);
						}
						else if (type.equals("character"))
						{
							Jupiter::ReferenceString character = tokens.getToken(3);
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(5));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnCharacterCrate(this, player, character);
							player->character = character;
						}
						else if (type.equals("spy"))
						{
							Jupiter::ReferenceString character = tokens.getToken(3);
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(5));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnSpyCrate(this, player, character);
							player->character = character;
						}
						else if (type.equals("refill"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(4));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnRefillCrate(this, player);
						}
						else if (type.equals("timebomb"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(4));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnTimeBombCrate(this, player);
						}
						else if (type.equals("speed"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(4));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnSpeedCrate(this, player);
						}
						else if (type.equals("nuke"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(4));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnNukeCrate(this, player);
						}
						else if (type.equals("abduction"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(4));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnAbductionCrate(this, player);
						}
						else if (type.equals("by"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(3));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnUnspecifiedCrate(this, player);
						}
						else
						{
							RenX::PlayerInfo *player = nullptr;
							if (tokens.getToken(3).equals("by"))
								player = parseGetPlayerOrAdd(tokens.getToken(4));

							if (player != nullptr)
								for (size_t i = 0; i < xPlugins.size(); i++)
									xPlugins.get(i)->RenX_OnOtherCrate(this, player, type);
						}
					}
					else if (subHeader.equals("Death;"))
					{
						// "player" | Player | "by" | Killer Player | "with" | Damage Type
						// "player" | Player | "died by" | Damage Type
						// "player" | Player | "suicide by" | Damage Type
						//		NOTE: Filter these out when Player.isEmpty().
						Jupiter::ReferenceString playerToken = tokens.getToken(3);
						if (playerToken.isNotEmpty())
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(playerToken);
							Jupiter::ReferenceString type = tokens.getToken(4);
							Jupiter::ReferenceString damageType;
							if (type.equals("by"))
							{
								damageType = tokens.getToken(7);
								Jupiter::ReferenceString killerData = tokens.getToken(5);
								Jupiter::ReferenceString kName = killerData.getToken(2, ',');
								Jupiter::ReferenceString kIDToken = killerData.getToken(1, ',');
								RenX::TeamType vTeam = RenX::getTeam(killerData.getToken(0, ','));
								if (kIDToken.equals("ai") || kIDToken.isEmpty())
								{
									player->deaths++;
									for (size_t i = 0; i < xPlugins.size(); i++)
										xPlugins.get(i)->RenX_OnKill(this, kName, vTeam, player, damageType);
								}
								else
								{
									player->deaths++;
									int kID = 0;
									bool kIsBot = false;
									if (kIDToken.get(0) == 'b')
									{
										kIsBot = true;
										kIDToken.shiftRight(1);
										kID = kIDToken.asInt();
										kIDToken.shiftLeft(1);
									}
									else
										kID = kIDToken.asInt();
									RenX::PlayerInfo *killer = getPlayerOrAdd(kName, kID, vTeam, kIsBot, 0, Jupiter::ReferenceString::empty);
									killer->kills++;
									if (damageType.equals("Rx_DmgType_Headshot"))
										killer->headshots++;
									for (size_t i = 0; i < xPlugins.size(); i++)
										xPlugins.get(i)->RenX_OnKill(this, killer, player, damageType);
								}
							}
							else if (type.equals("died by"))
							{
								player->deaths++;
								damageType = tokens.getToken(5);
								for (size_t i = 0; i < xPlugins.size(); i++)
									xPlugins.get(i)->RenX_OnDie(this, player, damageType);
							}
							else if (type.equals("suicide by"))
							{
								player->deaths++;
								player->suicides++;
								damageType = tokens.getToken(5);
								for (size_t i = 0; i < xPlugins.size(); i++)
									xPlugins.get(i)->RenX_OnSuicide(this, player, damageType);
							}
							player->character = Jupiter::ReferenceString::empty;
						}
						onAction();
					}
					else if (subHeader.equals("Stolen;"))
					{
						// Vehicle | "by" | Player
						// Vehicle | "bound to" | Bound Player | "by" | Player
						Jupiter::ReferenceString vehicle = tokens.getToken(2);
						Jupiter::ReferenceString byLine = tokens.getToken(3);
						if (byLine.equals("by"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(4));
							player->steals++;
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnSteal(this, player, vehicle);
						}
						else if (byLine.equals("bound to"))
						{
							RenX::PlayerInfo *victim = parseGetPlayerOrAdd(tokens.getToken(4));
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(6));
							player->steals++;
							victim->stolen++;
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnSteal(this, player, vehicle, victim);
						}
						onAction();
					}
					else if (subHeader.equals("Destroyed;"))
					{
						// "vehicle" | Vehicle | "by" | Killer | "with" | Damage Type
						// "defence" | Defence | "by" | Killer | "with" | Damage Type
						// "emplacement" | Emplacement | "by" | Killer Player | "with" | Damage Type
						// "building" | Building | "by" | Killer | "with" | Damage Type
						Jupiter::ReferenceString typeToken = tokens.getToken(2);
						RenX::ObjectType type = ObjectType::None;
						if (typeToken.equals("vehicle"))
							type = ObjectType::Vehicle;
						else if (typeToken.equals("defence") || typeToken.equals("emplacement"))
							type = ObjectType::Defence;
						else if (typeToken.equals("building"))
							type = ObjectType::Building;

						if (type != ObjectType::None)
						{
							Jupiter::ReferenceString objectName = tokens.getToken(3);
							if (tokens.getToken(4).equals("by"))
							{
								Jupiter::ReferenceString killerToken = tokens.getToken(5);
								Jupiter::ReferenceString idToken = killerToken.getToken(1, ',');
								Jupiter::ReferenceString name = killerToken.gotoToken(2, ',');
								Jupiter::ReferenceString damageType = tokens.getToken(7);

								RenX::TeamType team = RenX::getTeam(killerToken.getToken(0, ','));

								if (idToken.equals("ai") || idToken.isEmpty())
									for (size_t i = 0; i < xPlugins.size(); i++)
										xPlugins.get(i)->RenX_OnDestroy(this, name, team, objectName, RenX::getEnemy(team), damageType, type);
								else
								{
									int id;
									bool isBot = false;
									if (idToken.get(0) == 'b')
									{
										isBot = true;
										idToken.shiftRight(1);
									}
									id = idToken.asInt();
									RenX::PlayerInfo *player = getPlayerOrAdd(name, id, team, isBot, 0, Jupiter::ReferenceString::empty);
									switch (type)
									{
									case RenX::ObjectType::Vehicle:
										player->vehicleKills++;
										break;
									case RenX::ObjectType::Building:
										player->buildingKills++;
										{
											auto internalsStr = "_Internals"_jrs;
											RenX::BuildingInfo *building;
											if (objectName.findi(internalsStr) != Jupiter::INVALID_INDEX)
												objectName.truncate(internalsStr.size());
											building = RenX::Server::getBuildingByName(objectName);
											if (building != nullptr)
												building->health = 0.0;
										}

										break;
									case RenX::ObjectType::Defence:
										player->defenceKills++;
										break;
									default:
										break;
									}
									for (size_t i = 0; i < xPlugins.size(); i++)
										xPlugins.get(i)->RenX_OnDestroy(this, player, objectName, RenX::getEnemy(player->team), damageType, type);
								}
							}
						}
						onAction();
					}
					else if (subHeader.equals("Donated;"))
					{
						// Amount | "to" | Recipient | "by" | Donor
						if (tokens.getToken(5).equals("by"))
						{
							double amount = tokens.getToken(2).asDouble();
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(4));
							RenX::PlayerInfo *donor = parseGetPlayerOrAdd(tokens.getToken(6));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnDonate(this, donor, player, amount);
						}
					}
					else if (subHeader.equals("MatchEnd;"))
					{
						// "winner" | Winner | Reason("TimeLimit" etc) | "GDI=" GDI Score | "Nod=" Nod Score
						// "tie" | Reason | "GDI=" GDI Score | "Nod=" Nod Score
						Jupiter::ReferenceString winTieToken = tokens.getToken(2);
						if (winTieToken.equals("winner"))
						{
							Jupiter::ReferenceString sWinType = tokens.getToken(4);
							WinType winType = WinType::Unknown;
							if (sWinType.equals("TimeLimit"))
								winType = WinType::Score;
							else if (sWinType.equals("Buildings"))
								winType = WinType::Base;
							else if (sWinType.equals("triggered"))
								winType = WinType::Shutdown;

							TeamType team = RenX::getTeam(tokens.getToken(3));

							int gScore = tokens.getToken(5).getToken(1, '=').asInt();
							int nScore = tokens.getToken(6).getToken(1, '=').asInt();

							onPreGameOver(winType, team, gScore, nScore);
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnGameOver(this, winType, team, gScore, nScore);
						}
						else if (winTieToken.equals("tie"))
						{
							int gScore = tokens.getToken(4).getToken(1, '=').asInt();
							int nScore = tokens.getToken(5).getToken(1, '=').asInt();
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnGameOver(this, RenX::WinType::Tie, RenX::TeamType::None, gScore, nScore);
						}
					}
					else
					{
						Jupiter::ReferenceString raw = gotoToken(1);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnGame(this, raw);
					}
				}
				else if (tokens.tokens[0].equals("CHAT"))
				{
					if (subHeader.equals("Say;"))
					{
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(2));
						Jupiter::ReferenceString message = tokens.getToken(4);
						onChat(player, message);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnChat(this, player, message);
						onAction();
					}
					else if (subHeader.equals("TeamSay;"))
					{
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(2));
						Jupiter::ReferenceString message = tokens.getToken(4);
						onChat(player, message);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnTeamChat(this, player, message);
						onAction();
					}
					else if (subHeader.equals("Radio;"))
					{
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(2));
						Jupiter::ReferenceString message = tokens.getToken(4);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnRadioChat(this, player, message);
						onAction();
					}
					else if (subHeader.equals("HostSay;"))
					{
						Jupiter::ReferenceString message = tokens.getToken(3);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnHostChat(this, message);
					}
					/*else if (subHeader.equals("AdminSay;"))
					{
						// Player | "said:" | Message
						onAction();
					}
					else if (subHeader.equals("ReportSay;"))
					{
						// Player | "said:" | Message
						onAction();
					}*/
					else
					{
						Jupiter::ReferenceString raw = gotoToken(1);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnOtherChat(this, raw);
					}
				}
				else if (tokens.tokens[0].equals("PLAYER"))
				{
					if (subHeader.equals("Enter;"))
					{
						PARSE_PLAYER_DATA_P(tokens.getToken(2));
						uint64_t steamid = 0;
						if (tokens.getToken(5).equals("steamid"))
							steamid = tokens.getToken(6).asUnsignedLongLong();
						RenX::PlayerInfo *player = getPlayerOrAdd(name, id, team, isBot, steamid, tokens.getToken(4));
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnJoin(this, player);
					}
					else if (subHeader.equals("TeamJoin;"))
					{
						// Player | "joined" | Team
						// Player | "joined" | Team | "left" | Old Team
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(2));
						player->character = Jupiter::ReferenceString::empty;
						if (tokens.token_count > 4)
						{
							RenX::TeamType oldTeam = RenX::getTeam(tokens.getToken(6));
							if (oldTeam != RenX::TeamType::None)
								for (size_t i = 0; i < xPlugins.size(); i++)
									xPlugins.get(i)->RenX_OnTeamChange(this, player, oldTeam);
						}
					}
					else if (subHeader.equals("Exit;"))
					{
						// Player
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(2));
						if (this->silenceParts == false)
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnPart(this, player);
						this->removePlayer(player);
					}
					else if (subHeader.equals("Kick;"))
					{
						// Player | "for" | Reason
						const Jupiter::ReadableString &reason = tokens.getToken(4);
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(2));
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnKick(this, player, reason);
					}
					else if (subHeader.equals("NameChange;"))
					{
						// Player | "to:" | New Name
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(2));
						Jupiter::StringS newName = tokens.getToken(4);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnNameChange(this, player, newName);
						player->name = newName;
						onAction();
					}
					else if (subHeader.equals("ChangeID;"))
					{
						// "to" | New ID | "from" | Old ID
						int oldID = tokens.getToken(5).asInt();
						RenX::PlayerInfo *player = this->getPlayer(oldID);
						if (player != nullptr)
						{
							player->id = tokens.getToken(3).asInt();
							banCheck(player);
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnIDChange(this, player, oldID);
						}
					}
					else
					{
						Jupiter::ReferenceString raw = gotoToken(1);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnPlayer(this, raw);
					}
				}
				else if (tokens.tokens[0].equals("RCON"))
				{
					if (subHeader.equals("Command;"))
					{
						// User | "executed:" | Command
						Jupiter::ReferenceString user = tokens.getToken(2);
						if (tokens.getToken(3).equals("executed:"))
						{
							Jupiter::ReferenceString command = gotoToken(4);
							Jupiter::ReferenceString cmd = command.getWord(0, " ");

							if (cmd.equalsi("hostprivatesay"))
							{
								RenX::PlayerInfo *player = this->getPlayerByName(command.getWord(1, " "));
								if (player != nullptr)
								{
									Jupiter::ReferenceString message = command.gotoWord(2, " ");
									for (size_t i = 0; i < xPlugins.size(); i++)
										xPlugins.get(i)->RenX_OnHostPage(this, player, message);
								}
								else
									for (size_t i = 0; i < xPlugins.size(); i++)
										xPlugins.get(i)->RenX_OnExecute(this, user, command);
							}
							else
								for (size_t i = 0; i < xPlugins.size(); i++)
									xPlugins.get(i)->RenX_OnExecute(this, user, command);
							if (this->rconUser.equals(user))
							{
								this->lastCommand = cmd;
								this->lastCommandParams = command.gotoWord(1, " ");
							}
						}
					}
					else if (subHeader.equals("Subscribed;"))
					{
						// User
						Jupiter::ReferenceString user = tokens.getToken(2);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnSubscribe(this, user);
					}
					else if (subHeader.equals("Unsubscribed;"))
					{
						// User
						Jupiter::ReferenceString user = tokens.getToken(2);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnUnsubscribe(this, user);
					}
					else if (subHeader.equals("Blocked;"))
					{
						// User | Reason="(Denied by IP Policy)" / "(Not on Whitelist)"
						Jupiter::ReferenceString user = tokens.getToken(2);
						Jupiter::ReferenceString message = tokens.getToken(3);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnBlock(this, user, message);
					}
					else if (subHeader.equals("Connected;"))
					{
						// User
						Jupiter::ReferenceString user = tokens.getToken(2);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnConnect(this, user);
					}
					else if (subHeader.equals("Authenticated;"))
					{
						// User
						Jupiter::ReferenceString user = tokens.getToken(2);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnAuthenticate(this, user);
					}
					else if (subHeader.equals("Banned;"))
					{
						// User | "reason" | Reason="(Too many password attempts)"
						Jupiter::ReferenceString user = tokens.getToken(2);
						Jupiter::ReferenceString message = tokens.getToken(4);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnBan(this, user, message);
					}
					else if (subHeader.equals("InvalidPassword;"))
					{
						// User
						Jupiter::ReferenceString user = tokens.getToken(2);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnInvalidPassword(this, user);
					}
					else if (subHeader.equals("Dropped;"))
					{
						// User | "reason" | Reason="(Auth Timeout)"
						Jupiter::ReferenceString user = tokens.getToken(2);
						Jupiter::ReferenceString message = tokens.getToken(4);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnDrop(this, user, message);
					}
					else if (subHeader.equals("Disconnected;"))
					{
						// User
						Jupiter::ReferenceString user = tokens.getToken(2);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnDisconnect(this, user);
					}
					else if (subHeader.equals("StoppedListen;"))
					{
						// Reason="(Reached Connection Limit)"
						Jupiter::ReferenceString message = tokens.getToken(2);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnStopListen(this, message);
					}
					else if (subHeader.equals("ResumedListen;"))
					{
						// Reason="(No longer at Connection Limit)"
						Jupiter::ReferenceString message = tokens.getToken(2);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnResumeListen(this, message);
					}
					else if (subHeader.equals("Warning;"))
					{
						// Warning="(Hit Max Attempt Records - You should investigate Rcon attempts and/or decrease prune time)"
						Jupiter::ReferenceString message = tokens.getToken(2);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnWarning(this, message);
					}
					else
					{
						Jupiter::ReferenceString raw = gotoToken(1);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnRCON(this, raw);
					}
				}
				else if (tokens.tokens[0].equals("ADMIN"))
				{
					if (subHeader.equals("Rcon;"))
					{
						// Player | "executed:" | Command
						if (tokens.getToken(3).equals("executed:"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(2));
							Jupiter::ReferenceString cmd = gotoToken(4);
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnExecute(this, player, cmd);
						}
					}
					else if (subHeader.equals("Login;"))
					{
						// Player | "as" | Type="moderator" / "administrator"
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(2));
						player->adminType = tokens.getToken(4);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnAdminLogin(this, player);
					}
					else if (subHeader.equals("Logout;"))
					{
						// Player | "as" | Type="moderator" / "administrator"
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(2));
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnAdminLogout(this, player);
						player->adminType = Jupiter::ReferenceString::empty;
					}
					else if (subHeader.equals("Granted;"))
					{
						// Player | "as" | Type="moderator" / "administrator"
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(2));
						player->adminType = tokens.getToken(4);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnAdminGrant(this, player);
					}
					else
					{
						Jupiter::ReferenceString raw = gotoToken(1);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnAdmin(this, raw);
					}
				}
				else if (tokens.tokens[0].equals("VOTE"))
				{
					if (subHeader.equals("Called;"))
					{
						// TeamType="Global" / "GDI" / "Nod" / "" | VoteType="Rx_VoteMenuChoice_"... | "parameters" | Parameters(Empty) | "by" | Player
						// TeamType="Global" / "GDI" / "Nod" / "" | VoteType="Rx_VoteMenuChoice_"... | "by" | Player
						Jupiter::ReferenceString voteType = tokens.getToken(3);
						Jupiter::ReferenceString teamToken = tokens.getToken(2);
						RenX::TeamType team;
						if (teamToken.equals("Global"))
							team = TeamType::None;
						else if (teamToken.equals("GDI"))
							team = TeamType::GDI;
						else if (teamToken.equals("Nod"))
							team = TeamType::Nod;
						else
							team = TeamType::Other;

						Jupiter::ReferenceString playerToken;
						Jupiter::ReferenceString parameters;
						if (tokens.getToken(4).equals("parameters"))
						{
							playerToken = tokens.getToken(tokens.token_count - 1);
							parameters = tokens.getToken(5);
						}
						else
							playerToken = tokens.getToken(5);

						RenX::PlayerInfo *player = parseGetPlayerOrAdd(playerToken);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnVoteCall(this, team, voteType, player, parameters);
						onAction();
					}
					else if (subHeader.equals("Results;"))
					{
						// TeamType="Global" / "GDI" / "Nod" / "" | VoteType="Rx_VoteMenuChoice_"... | Success="pass" / "fail" | "Yes=" Yes votes | "No=" No votes
						Jupiter::ReferenceString voteType = tokens.getToken(3);
						Jupiter::ReferenceString teamToken = tokens.getToken(2);
						RenX::TeamType team;
						if (teamToken.equals("Global"))
							team = TeamType::None;
						else if (teamToken.equals("GDI"))
							team = TeamType::GDI;
						else if (teamToken.equals("Nod"))
							team = TeamType::Nod;
						else
							team = TeamType::Other;

						bool success = true;
						if (tokens.getToken(4).equals("fail"))
							success = false;

						int yesVotes = 0;
						Jupiter::ReferenceString yesVotesToken = tokens.getToken(5);
						if (yesVotesToken.size() > 4)
						{
							yesVotesToken.shiftRight(4);
							yesVotes = yesVotesToken.asInt();
						}

						int noVotes = 0;
						Jupiter::ReferenceString noVotesToken = tokens.getToken(5);
						if (yesVotesToken.size() > 3)
						{
							yesVotesToken.shiftRight(3);
							yesVotes = yesVotesToken.asInt();
						}

						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnVoteOver(this, team, voteType, success, yesVotes, noVotes);
					}
					else if (subHeader.equals("Cancelled;"))
					{
						// TeamType="Global" / "GDI" / "Nod" | VoteType="Rx_VoteMenuChoice_"...
						Jupiter::ReferenceString voteType = tokens.getToken(3);
						Jupiter::ReferenceString teamToken = tokens.getToken(2);
						RenX::TeamType team;
						if (teamToken.equals("Global"))
							team = TeamType::None;
						else if (teamToken.equals("GDI"))
							team = TeamType::GDI;
						else if (teamToken.equals("Nod"))
							team = TeamType::Nod;
						else
							team = TeamType::Other;

						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnVoteCancel(this, team, voteType);
					}
					else
					{
						Jupiter::ReferenceString raw = gotoToken(1);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnVote(this, raw);
					}
				}
				else if (tokens.tokens[0].equals("MAP"))
				{
					if (subHeader.equals("Changing;"))
					{
						// Map | Mode="seamless" / "nonseamless"
						Jupiter::ReferenceString map = tokens.getToken(2);
						if (tokens.getToken(3).equals("seamless"))
							this->seamless = true;
						else
						{
							this->seamless = false;
							this->silenceParts = true;
						}
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnMapChange(this, map, seamless);
						this->map = map;
						onMapChange();
					}
					else if (subHeader.equals("Loaded;"))
					{
						// Map
						Jupiter::ReferenceString map = tokens.getToken(2);
						this->map = map;
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnMapLoad(this, map);
					}
					else if (subHeader.equals("Start;"))
					{
						// Map
						this->seenStart = true;
						this->gameStart = std::chrono::steady_clock::now();
						Jupiter::ReferenceString map = tokens.getToken(2);
						this->map = map;
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnMapStart(this, map);
					}
					else
					{
						Jupiter::ReferenceString raw = gotoToken(1);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnMap(this, raw);
					}
				}
				else if (tokens.tokens[0].equals("DEMO"))
				{
					if (subHeader.equals("Record;"))
					{
						// "client request by" | Player
						// "admin command by" | Player
						// "rcon command"
						Jupiter::ReferenceString type = tokens.getToken(2);
						if (type.equals("client request by") || type.equals("admin command by"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(tokens.getToken(3));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnDemoRecord(this, player);
						}
						else
						{
							Jupiter::ReferenceString user = tokens.getToken(3); // not actually used, but here for possible future usage
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnDemoRecord(this, user);
						}
					}
					else if (subHeader.equals("RecordStop;"))
					{
						// Empty
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnDemoRecordStop(this);
					}
					else
					{
						Jupiter::ReferenceString raw = gotoToken(1);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnDemo(this, raw);
					}
				}
				/*else if (tokens.tokens[0].equals("ERROR;")) // Decided to disable this entirely, since it's unreachable anyways.
				{
					// Should be under RCON.
					// "Could not open TCP Port" Port "- Rcon Disabled"
				}*/
				else
				{
					Jupiter::ReferenceString raw = Jupiter::ReferenceString::substring(line, 1);
					for (size_t i = 0; i < xPlugins.size(); i++)
						xPlugins.get(i)->RenX_OnLog(this, raw);
				}
			}
			break;

		case 'c':
			{
				Jupiter::ReferenceString raw = Jupiter::ReferenceString::substring(line, 1);
				for (size_t i = 0; i < xPlugins.size(); i++)
					xPlugins.get(i)->RenX_OnCommand(this, raw);
				this->commandListFormat.erase();
				this->lastCommand = Jupiter::ReferenceString::empty;
				this->lastCommandParams = Jupiter::ReferenceString::empty;
			}
			break;

		case 'e':
			{
				Jupiter::ReferenceString raw = Jupiter::ReferenceString::substring(line, 1);
				for (size_t i = 0; i < xPlugins.size(); i++)
					xPlugins.get(i)->RenX_OnError(this, raw);
			}
			break;

		case 'v':
			{
				Jupiter::ReferenceString raw = Jupiter::ReferenceString::substring(line, 1);
				this->rconVersion = raw.asInt(10);
				this->gameVersion = raw.substring(3);
				
				if (this->rconVersion >= 3)
				{
					RenX::Server::sock.send("s\n"_jrs);
					RenX::Server::send("serverinfo"_jrs);
					RenX::Server::send("gameinfo"_jrs);
					RenX::Server::send("mutatorlist"_jrs);
					RenX::Server::send("rotation"_jrs);
					RenX::Server::fetchClientList();
					RenX::Server::updateBuildingList();
					
					RenX::Server::gameStart = std::chrono::steady_clock::now();
					this->seenStart = false;
					this->seamless = true;
					
					for (size_t i = 0; i < xPlugins.size(); i++)
						xPlugins.get(i)->RenX_OnVersion(this, raw);
				}
				else
				{
					RenX::Server::sendLogChan(STRING_LITERAL_AS_REFERENCE(IRCCOLOR "04[Error]" IRCCOLOR " Disconnected from Renegade-X server (incompatible RCON version)."));
					this->disconnect(RenX::DisconnectReason::IncompatibleVersion);
				}
			}
			break;

		case 'a':
			{
				RenX::Server::rconUser = Jupiter::ReferenceString::substring(line, 1);;
				for (size_t i = 0; i < xPlugins.size(); i++)
					xPlugins.get(i)->RenX_OnAuthorized(this, RenX::Server::rconUser);
			}
			break;

		default:
			{
				Jupiter::ReferenceString raw = Jupiter::ReferenceString::substring(line, 1);
				for (size_t i = 0; i < xPlugins.size(); i++)
					xPlugins.get(i)->RenX_OnOther(this, header, raw);
			}
			break;
		}
		for (size_t i = 0; i < xPlugins.size(); i++)
			xPlugins.get(i)->RenX_OnRaw(this, line);
	}
}

void RenX::Server::disconnect(RenX::DisconnectReason reason)
{
	Jupiter::ArrayList<RenX::Plugin> xPlugins;
	for (size_t i = 0; i < xPlugins.size(); i++)
		xPlugins.get(i)->RenX_OnServerDisconnect(this, reason);

	RenX::Server::sock.closeSocket();
	RenX::Server::wipeData();
	RenX::Server::connected = false;
}

bool RenX::Server::connect()
{
	RenX::Server::lastAttempt = std::chrono::steady_clock::now();
	if (RenX::Server::sock.connect(RenX::Server::hostname.c_str(), RenX::Server::port, RenX::Server::clientHostname.isEmpty() ? nullptr : RenX::Server::clientHostname.c_str()))
	{
		RenX::Server::sock.setBlocking(false);
		RenX::Server::sock.send(Jupiter::StringS::Format("a%.*s\n", RenX::Server::pass.size(), RenX::Server::pass.ptr()));
		RenX::Server::connected = true;
		RenX::Server::silenceParts = false;
		RenX::Server::attempts = 0;
		return true;
	}
	RenX::Server::connected = false;
	++RenX::Server::attempts;
	return false;
}

bool RenX::Server::reconnect(RenX::DisconnectReason reason)
{
	RenX::Server::disconnect(static_cast<RenX::DisconnectReason>(static_cast<unsigned int>(reason) | 0x01));
	return RenX::Server::connect();
}

void RenX::Server::wipeData()
{
	RenX::PlayerInfo *player;
	Jupiter::ArrayList<RenX::Plugin> &xPlugins = *RenX::getCore()->getPlugins();
	while (RenX::Server::players.size() != 0)
	{
		player = RenX::Server::players.remove(0U);
		for (size_t index = 0; index < xPlugins.size(); ++index)
			xPlugins.get(index)->RenX_OnPlayerDelete(this, player);
		delete player;
	}
	RenX::Server::buildings.emptyAndDelete();
	RenX::Server::mutators.emptyAndDelete();
	RenX::Server::maps.emptyAndDelete();
	RenX::Server::awaitingPong = false;
	RenX::Server::rconVersion = 0;
	RenX::Server::rconUser.truncate(RenX::Server::rconUser.size());
}

unsigned int RenX::Server::getVersion() const
{
	return RenX::Server::rconVersion;
}

const Jupiter::ReadableString &RenX::Server::getGameVersion() const
{
	return RenX::Server::gameVersion;
}

const Jupiter::ReadableString &RenX::Server::getRCONUsername() const
{
	return RenX::Server::rconUser;
}

RenX::Server::Server(Jupiter::Socket &&socket, const Jupiter::ReadableString &configurationSection) : Server(configurationSection)
{
	RenX::Server::sock = std::move(socket);
	RenX::Server::hostname = RenX::Server::sock.getHostname();
	RenX::Server::sock.send(Jupiter::StringS::Format("a%.*s\n", RenX::Server::pass.size(), RenX::Server::pass.ptr()));
	RenX::Server::connected = true;
	RenX::Server::silenceParts = false;
}

RenX::Server::Server(const Jupiter::ReadableString &configurationSection)
{
	RenX::Server::configSection = configurationSection;
	RenX::Server::calc_uuid = RenX::default_uuid_func;
	init();
	Jupiter::ArrayList<RenX::Plugin> &xPlugins = *RenX::getCore()->getPlugins();
	for (size_t i = 0; i < xPlugins.size(); i++)
		xPlugins.get(i)->RenX_OnServerCreate(this);
}

void RenX::Server::init()
{
	RenX::Server::hostname = Jupiter::IRC::Client::Config->get(RenX::Server::configSection, "Hostname"_jrs, "localhost"_jrs);
	RenX::Server::port = static_cast<unsigned short>(Jupiter::IRC::Client::Config->getInt(RenX::Server::configSection, "Port"_jrs, 7777));
	RenX::Server::clientHostname = Jupiter::IRC::Client::Config->get(RenX::Server::configSection, "ClientAddress"_jrs);
	RenX::Server::pass = Jupiter::IRC::Client::Config->get(RenX::Server::configSection, "Password"_jrs, "renx"_jrs);

	RenX::Server::logChanType = Jupiter::IRC::Client::Config->getShort(RenX::Server::configSection, "ChanType"_jrs);
	RenX::Server::adminLogChanType = Jupiter::IRC::Client::Config->getShort(RenX::Server::configSection, "AdminChanType"_jrs);

	RenX::Server::setCommandPrefix(Jupiter::IRC::Client::Config->get(RenX::Server::configSection, "CommandPrefix"_jrs));
	RenX::Server::setPrefix(Jupiter::IRC::Client::Config->get(RenX::Server::configSection, "IRCPrefix"_jrs));

	RenX::Server::rules = Jupiter::IRC::Client::Config->get(RenX::Server::configSection, "Rules"_jrs, "Anarchy!"_jrs);
	RenX::Server::delay = std::chrono::milliseconds(Jupiter::IRC::Client::Config->getInt(RenX::Server::configSection, "ReconnectDelay"_jrs, 10000));
	RenX::Server::maxAttempts = Jupiter::IRC::Client::Config->getInt(RenX::Server::configSection, "MaxReconnectAttempts"_jrs, -1);
	RenX::Server::rconBan = Jupiter::IRC::Client::Config->getBool(RenX::Server::configSection, "RCONBan"_jrs, false);
	RenX::Server::localSteamBan = Jupiter::IRC::Client::Config->getBool(RenX::Server::configSection, "LocalSteamBan"_jrs, true);
	RenX::Server::localIPBan = Jupiter::IRC::Client::Config->getBool(RenX::Server::configSection, "LocalIPBan"_jrs, true);
	RenX::Server::localNameBan = Jupiter::IRC::Client::Config->getBool(RenX::Server::configSection, "LocalNameBan"_jrs, false);
	RenX::Server::localBan = RenX::Server::localIPBan || RenX::Server::localSteamBan || RenX::Server::localNameBan;
	RenX::Server::steamFormat = Jupiter::IRC::Client::Config->getInt(RenX::Server::configSection, "SteamFormat"_jrs, 16);
	RenX::Server::neverSay = Jupiter::IRC::Client::Config->getBool(RenX::Server::configSection, "NeverSay"_jrs, false);
	RenX::Server::clientUpdateRate = std::chrono::milliseconds(Jupiter::IRC::Client::Config->getInt(RenX::Server::configSection, "ClientUpdateRate"_jrs, 2500));
	RenX::Server::buildingUpdateRate = std::chrono::milliseconds(Jupiter::IRC::Client::Config->getInt(RenX::Server::configSection, "BuildingUpdateRate"_jrs, 7500));
	RenX::Server::pingRate = std::chrono::milliseconds(Jupiter::IRC::Client::Config->getInt(RenX::Server::configSection, "PingUpdateRate"_jrs, 60000));
	RenX::Server::pingTimeoutThreshold = std::chrono::milliseconds(Jupiter::IRC::Client::Config->getInt(RenX::Server::configSection, "PingTimeoutThreshold"_jrs, 10000));

	Jupiter::INIFile &commandsFile = RenX::getCore()->getCommandsFile();
	RenX::Server::commandAccessLevels = commandsFile.getSection(RenX::Server::configSection);
	RenX::Server::commandAliases = commandsFile.getSection(Jupiter::StringS::Format("%.*s.Aliases", RenX::Server::configSection.size(), RenX::Server::configSection.ptr()));

	for (size_t i = 0; i < RenX::GameMasterCommandList->size(); i++)
		RenX::Server::addCommand(RenX::GameMasterCommandList->get(i)->copy());
}

RenX::Server::~Server()
{
	sock.closeSocket();
	RenX::Server::wipeData();
	RenX::Server::commands.emptyAndDelete();
}
