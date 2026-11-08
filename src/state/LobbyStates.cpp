/*=============================================================================
Blobby Volley 2
Copyright (C) 2006 Jonathan Sieber (jonathan_sieber@yahoo.de)
Copyright (C) 2006 Daniel Knobe (daniel-knobe@web.de)

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
=============================================================================*/

/* header include */
#include "LobbyStates.h"

/* includes */
#include <algorithm>
#include <array>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <cstring>

#include <SDL.h>

#include "raknet/RakClient.h"

#include "BlobbyApp.h"
#include "IMGUI.h"
#include "NetworkState.h"
#include "NetworkSearchState.h"
#include "UserConfig.h"
#include "GenericIO.h"
#include "GameLogic.h"
#include "TournamentProtocol.h"
#include "Global.h"
#include "JoinLink.h"

namespace
{
	/// Asks the player before the game connects to the server of a link from outside the game, since any web page can hold one.
	/// Returns true only when the player picks Yes.
	bool confirmJoin(const JoinLink& link, SDL_Window* parent)
	{
		std::string question = "Join the tournament server " + link.host + ":" + std::to_string(link.port) + "?";
		const SDL_MessageBoxButtonData buttons[] = {
			{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No" },
			{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
		};
		SDL_MessageBoxData data = {};
		data.flags = SDL_MESSAGEBOX_INFORMATION;
		data.window = parent;
		data.title = AppTitle;
		data.message = question.c_str();
		data.numbuttons = 2;
		data.buttons = buttons;
		data.colorScheme = nullptr;

		int chosen = 0;
		if (SDL_ShowMessageBox(&data, &chosen) != 0)
			return false;
		return chosen == 1;
	}

	/// The server a join link points to.
	ServerInfo serverFromLink(const JoinLink& link)
	{
		ServerInfo info(std::string(""));
		std::memset(info.hostname, 0, sizeof(info.hostname));
		std::memset(info.name, 0, sizeof(info.name));
		std::memset(info.description, 0, sizeof(info.description));
		std::strncpy(info.hostname, link.host.c_str(), sizeof(info.hostname) - 1);
		info.port = link.port;
		std::string name = link.host + ":" + std::to_string(link.port);
		std::strncpy(info.name, name.c_str(), sizeof(info.name) - 1);
		return info;
	}
}

LobbyState* joinFromLink(const JoinLink& link, JoinLinkSource source, SDL_Window* parent)
{
	if (joinNeedsConfirmation(source) && !confirmJoin(link, parent))
		return nullptr;
	return new LobbyState(serverFromLink(link), PreviousState::MAIN, link.code);
}

LobbySubstate::~LobbySubstate() = default;


LobbyState::LobbyState(ServerInfo info, PreviousState previous, std::string join_code) :
		mInfo(std::move(info)), mPrevious( previous ), mJoinCode(std::move(join_code)),
		mLobbyState(ConnectionState::CONNECTING)
{
}

LobbyState::LobbyState(ServerInfo info, std::shared_ptr<RakClient> client, const TournamentStatusData& status) :
		mClient(std::move(client)), mInfo(std::move(info)), mPrevious( PreviousState::MAIN ),
		mLobbyState(ConnectionState::CONNECTED), mTournament(true), mOwnName(status.ownName)
{
	mSubState = std::make_shared<LobbyTournamentSubstate>(status);
}

void LobbyState::init()
{
	// back from a tournament game, the connection is still open
	if (mClient)
		return;

	mClient = std::shared_ptr<RakClient>(new RakClient(), [](RakClient* client) { client->Disconnect(25); delete client; });

	if (!mClient->Connect(mInfo.hostname, mInfo.port, 0, 0, RAKNET_THREAD_SLEEP_TIME))
	{
		// a join link names a server that may not be reachable, so show that instead of closing the game
		if (!mJoinCode.empty())
			mLobbyState = ConnectionState::CONNECTION_FAILED;
		else
			throw( std::runtime_error(std::string("Could not connect to server ") + mInfo.hostname) );
	}

	// send an ENTER_SERVER packet with name and side preference

	/// \todo we need read-only access here!
	UserConfig config;
	config.loadFile("config.xml");
	PlayerSide side = (PlayerSide)config.getInteger("network_side");

	// load player identity
	if(side == LEFT_PLAYER)
	{
		mLocalPlayer = config.loadPlayerIdentity(LEFT_PLAYER, true);
	}
	else
	{
		mLocalPlayer = config.loadPlayerIdentity(RIGHT_PLAYER, true);
	}

	// the server swaps the join code for the nickname of the player
	if (!mJoinCode.empty())
		mLocalPlayer.setName(mJoinCode);
}


LobbyState::~LobbyState() = default;

void LobbyState::step_impl()
{
	// process packets
	packet_ptr packet;
	while (nullptr != (packet = mClient->Receive()))
	{
		switch(packet->data[0])
		{
			case ID_CONNECTION_REQUEST_ACCEPTED:
			{
				RakNet::BitStream stream;
				makeEnterServerPacket(stream, mLocalPlayer);
				mClient->Send(&stream, LOW_PRIORITY, RELIABLE_ORDERED, 0);

				mSubState = std::make_shared<LobbyMainSubstate>(mClient, 0, 0, 3);
				break;
			}
			case ID_CONNECTION_ATTEMPT_FAILED:
			{
				mLobbyState = ConnectionState::CONNECTION_FAILED;
				break;
			}

			case ID_CONNECTION_LOST:
			case ID_DISCONNECTION_NOTIFICATION:
			{
				// keep showing why we were refused
				if (mLobbyState != ConnectionState::REFUSED)
					mLobbyState = ConnectionState::DISCONNECTED;
				break;
			}
			// we ignore these packets. they tell us about remote connections, which we handle manually with ID_SERVER_STATUS packets.
			case ID_REMOTE_EXISTING_CONNECTION:
			case ID_REMOTE_DISCONNECTION_NOTIFICATION:
			case ID_REMOTE_NEW_INCOMING_CONNECTION:
				// not surprising, but we are not interested in his packet
				break;
			case ID_LOBBY:
				{
				RakNet::BitStream stream(packet->data, packet->length, false);
				auto in = createGenericReader( &stream );
				unsigned char t;
				in->byte(t);
				in->byte(t);
				if((LobbyPacketType)t == LobbyPacketType::ENTRY_REFUSED)
				{
					unsigned char reason = 0xFF;
					stream.Read(reason);
					mRefusedReason = reason;
					mLobbyState = ConnectionState::REFUSED;
					break;
				}
				if (mLobbyState == ConnectionState::REFUSED)
					break;
				mLobbyState = ConnectionState::CONNECTED;
				if((LobbyPacketType)t == LobbyPacketType::TOURNAMENT_STATUS)
				{
					// the server picks the matches, so the casual lobby is not shown anymore
					mTournament = true;
					TournamentStatusData data = readTournamentStatus(*in);
					if (!data.ownName.empty())
						mOwnName = data.ownName;
					mSubState = std::make_shared<LobbyTournamentSubstate>(data);
				}
				else if((LobbyPacketType)t == LobbyPacketType::SERVER_STATUS)
				{
					uint32_t player_count;
					in->uint32( player_count );
					ServerStatusLists lists;
					lists.read( *in );
					// lists that do not fit together are dropped
					if( !lists.consistent() )
						break;

					mStatus.mPossibleSpeeds = lists.speeds;
					mStatus.mPossibleRules = lists.rules;
					mStatus.mPossibleRulesAuthor = lists.rulesAuthors;
					mStatus.mOpenGames.clear();
					for( unsigned i = 0; i < lists.gameIds.size(); ++i)
					{
						mStatus.mOpenGames.push_back( ServerStatusData::OpenGame{ lists.gameIds[i], lists.gameNames[i], lists.gameRules[i], lists.gameSpeeds[i], lists.gameScores[i], lists.gamePasswords[i]});
					}

					// find out which settings most closely resemble the local config
					bool first_config = (mPreferedSpeed == -1u); // detect whether we set config for the first time
					std::shared_ptr<IUserConfigReader> config = IUserConfigReader::createUserConfigReader("config.xml");

					// speed
					int speed = config->getInteger("gamefps");
					auto& speeds = mStatus.mPossibleSpeeds;
					auto closest_speed = std::min_element( speeds.begin(), speeds.end(),
									[speed](int a, int b){ return std::abs(a-speed) < std::abs(b-speed); } );
					mPreferedSpeed = std::distance( speeds.begin(), closest_speed );

					// rules
					auto gamelogic = createGameLogic(config->getString("rules"), 1);
					std::string rule = gamelogic->getTitle();
					auto& rules = mStatus.mPossibleRules;
					auto found = std::find( rules.begin(), rules.end(), rule);
					/// \todo we need to open the lua file here to get the actual ruleset name.
					if( found != rules.end())
						mPreferedRules = std::distance( rules.begin(), found );

					// points
					int points = config->getInteger( "scoretowin" );
					std::array<unsigned, 8> scores{2, 5, 10, 15, 20, 25, 40, 50};
					auto closest_score = std::min_element( scores.begin(), scores.end(),
									[points](int a, int b){ return std::abs(a-points) < std::abs(b-points); } );
					mPreferedScore = std::distance( scores.begin(), closest_score );

					// if this is the first time we receive the config, create new main substate
					if( first_config && !mTournament )
					{
						mSubState = std::make_shared<LobbyMainSubstate>(mClient, mPreferedSpeed, mPreferedRules, mPreferedScore);
					}

				} else if((LobbyPacketType)t == LobbyPacketType::GAME_STATUS)
				{
					auto game = std::make_shared<LobbyGameSubstate>(mClient, in);
					// a game whose settings are not in the server lists is dropped
					if( game->fits(mStatus) )
						mSubState = game;
				} else if((LobbyPacketType)t == LobbyPacketType::REMOVED_FROM_GAME)
				{
					mSubState = std::make_shared<LobbyMainSubstate>(mClient, mPreferedSpeed, mPreferedRules, mPreferedScore);
				}
				}
				break;
			case ID_SPONSORS:
				// only a tournament server sends the board, after the tournament status
				if (mTournament)
					getApp().handleSponsorPacket(packet->data, packet->length, mClient);
				break;
			case ID_RULES_CHECKSUM: // this packet is sent when a game was created, so we probably are joining a game here.
				// in a casual lobby we are in the lobby game substate here. A tournament server
				// starts the game while we wait in the tournament substate.
				{
				RakNet::BitStream stream(packet->data, packet->length, false);
				RulesChecksumData rules;
				if (!readRulesChecksum(stream, rules))
					break;

				{
					auto game = new NetworkGameState( mClient, rules.checksum, rules.scoreToWin, mOwnName );
					// a tournament player goes back to this lobby after the match
					if (mTournament)
						game->setTournamentLobby(mInfo);
					switchState( game );
				}
				}
				break;
			default:
				std::cout << "Unknown packet " << int(packet->data[0]) << " received\n";
		}
	}


	IMGUI& imgui = getIMGUI();

	imgui.doCursor();
	imgui.doImage(GEN_ID, Vector2(400.0, 300.0), "background");
	imgui.doOverlay(GEN_ID, Vector2(0.0, 0.0), Vector2(800.0, 600.0));
	imgui.doInactiveMode(false);

	// server name
	imgui.doText(GEN_ID, Vector2(400 - 12 * std::strlen(mInfo.name), 20), mInfo.name);

	// server description
	if (mLobbyState != ConnectionState::CONNECTED )
	{
		// server description
		if (mLobbyState == ConnectionState::CONNECTING )
		{
			imgui.doText(GEN_ID, Vector2( 100, 55 ), TextManager::NET_CONNECTING);
		}
		else if (mLobbyState == ConnectionState::DISCONNECTED )
		{
			imgui.doText(GEN_ID, Vector2( 100, 55 ), TextManager::NET_DISCONNECT);
		}
		else if (mLobbyState == ConnectionState::CONNECTION_FAILED )
		{
			imgui.doText(GEN_ID, Vector2( 100, 55 ), TextManager::NET_CON_FAILED);
		}
		else if (mLobbyState == ConnectionState::REFUSED )
		{
			TextManager::STRING text = TextManager::NET_ENTRY_REFUSED;
			if (mRefusedReason == (unsigned char)EntryRefusedReason::UNKNOWN_CODE)
				text = TextManager::NET_UNKNOWN_CODE;
			else if (mRefusedReason == (unsigned char)EntryRefusedReason::CODE_IN_USE)
				text = TextManager::NET_CODE_IN_USE;
			else if (mRefusedReason == (unsigned char)EntryRefusedReason::OUTDATED_CLIENT)
				text = TextManager::NET_UPDATE_GAME;
			imgui.doText(GEN_ID, Vector2( 100, 55 ), text);
		}

		// empty player list
		unsigned int s;
		imgui.doSelectbox(GEN_ID, Vector2(25.0, 90.0), Vector2(375.0, 470.0), std::vector<std::string>{}, s);

		// empty info panel
		imgui.doOverlay(GEN_ID, Vector2(425.0, 90.0), Vector2(775.0, 470.0));
	}
	else
	{
		std::string description = mInfo.description;
		for (unsigned int i = 0; i < description.length(); i += 63)
		{
			imgui.doText(GEN_ID, Vector2(25, 55 + i / 63 * 15), description.substr(i, 63), TF_SMALL_FONT);
		}


		mSubState->step( imgui, mStatus );
	}



	// back button
	if (imgui.doButton(GEN_ID, Vector2(50, 530), TextManager::LBL_CANCEL))
	{
		if( mPrevious == PreviousState::ONLINE )
			switchState( new OnlineSearchState );
		else if ( mPrevious == PreviousState::LAN )
			switchState( new LANSearchState );
		else
			switchState( new MainMenuState );
	}
}

const char* LobbyState::getStateName() const
{
	return "LobbyState";
}


std::vector<std::string> describeTournamentStatus(const IMGUI& imgui, const TournamentStatusData& data)
{
	std::vector<std::string> lines;
	switch (data.status)
	{
		case TournamentStatus::NO_MATCH:
			lines.push_back(imgui.getText(TextManager::TNM_NO_MATCH));
			return lines;
		case TournamentStatus::WAITING_FOR_OPPONENT:
			lines.push_back(imgui.getText(TextManager::TNM_WAITING_OPPONENT));
			break;
		case TournamentStatus::WAITING_FOR_SLOT:
			lines.push_back(imgui.getText(TextManager::TNM_WAITING_SLOT));
			break;
		case TournamentStatus::WAITING_FOR_ORGANIZER:
			// a game was abandoned, or the organizer holds the match
			lines.push_back(imgui.getText(TextManager::TNM_GAME_STOPPED));
			lines.push_back(imgui.getText(TextManager::TNM_ORGANIZER_DECIDES));
			break;
		case TournamentStatus::PLAYING:
			lines.push_back(imgui.getText(TextManager::TNM_PLAYING));
			break;
		case TournamentStatus::NEXT_GAME:
			lines.push_back(imgui.getText(TextManager::TNM_NEXT_GAME));
			break;
		case TournamentStatus::MATCH_OVER:
			lines.push_back(imgui.getText(TextManager::TNM_MATCH_OVER));
			break;
	}

	if (!data.opponent.empty())
		lines.push_back(imgui.getText(TextManager::TNM_OPPONENT) + data.opponent);
	if (data.bestOf > 1)
	{
		lines.push_back(imgui.getText(TextManager::TNM_BEST_OF) + std::to_string(data.bestOf) + ", "
			+ imgui.getText(TextManager::TNM_GAMES_WON) + std::to_string(data.ownWins) + " - "
			+ std::to_string(data.opponentWins));
	}
	return lines;
}

// ----------------------------------------------------------------------------
// 				T o u r n a m e n t     S u b s t a t e
// ----------------------------------------------------------------------------

LobbyTournamentSubstate::LobbyTournamentSubstate(TournamentStatusData data) :
	mData(std::move(data))
{
}

#undef GEN_ID
#define GEN_ID imgui.getNextId()
void LobbyTournamentSubstate::step(IMGUI& imgui, const ServerStatusData& /*status*/)
{
	imgui.doOverlay(GEN_ID, Vector2(25.0, 90.0), Vector2(775.0, 470.0));
	auto lines = describeTournamentStatus(imgui, mData);
	for (unsigned i = 0; i < lines.size(); ++i)
	{
		imgui.doText(GEN_ID, Vector2(400.0, 200.0 + 50.0 * i), lines[i], TF_ALIGN_CENTER);
	}
}

// ----------------------------------------------------------------------------
// 				M a i n     S u b s t a t e
// ----------------------------------------------------------------------------
LobbyMainSubstate::LobbyMainSubstate(std::shared_ptr<RakClient> client,
									unsigned prefspeed, unsigned prefrules, unsigned prefscore ) :
	mClient(std::move(client)),
	mChosenSpeed( prefspeed ),
	mChosenRules( prefrules ),
	mChosenScore( prefscore ),
	mChosenPasswordPosition(0),
	mChosenPasswordVisible(false)
{

}

#undef GEN_ID
#define GEN_ID imgui.getNextId()
void LobbyMainSubstate::step(IMGUI& imgui, const ServerStatusData& status)
{
	// player list
	std::vector<std::string> gamelist;
	gamelist.push_back( imgui.getText(TextManager::NET_OPEN_GAME) );
	for ( const auto& game : status.mOpenGames)
	{
		gamelist.push_back( game.name + (game.hasPassword ? "🔒" : "") );
	}

	unsigned int previousSelectedGame = mSelectedGame;
	bool doEnterGame = imgui.doSelectbox(GEN_ID, Vector2(25.0, 90.0), Vector2(375.0, 470.0), gamelist, mSelectedGame) == SBA_DBL_CLICK;

	// Reset some configuration on change
	if (previousSelectedGame != mSelectedGame)
	{
		mChosenPasswordVisible = false;
		mChosenPassword = "";
		mChosenPasswordPosition = 0;
	}

	// if selected game is invalid (i.e. a game was removed and the reference now is wrong), set to random
	if(mSelectedGame >= gamelist.size())
		mSelectedGame = 0;

	if(mSelectedGame != 0)
	{
		unsigned gameIndex = mSelectedGame - 1;

		// info panel
		imgui.doOverlay(GEN_ID, Vector2(425.0, 90.0), Vector2(775.0, 470.0));

		// info panel contents:
		//  * gamespeed
		imgui.doText(GEN_ID, Vector2(435, 100), imgui.getText(TextManager::NET_SPEED) +
					 std::to_string(int(0.5 + 100.0 / 75.0 * status.mPossibleSpeeds.at(status.getGame(gameIndex).speed))) + "%");
		//  * points
		imgui.doText(GEN_ID, Vector2(435, 135), imgui.getText(TextManager::NET_POINTS) +
											 std::to_string(status.getGame(gameIndex).score) );

		//  * rulesfile
		imgui.doText(GEN_ID, Vector2(435, 170), imgui.getText(TextManager::NET_RULES_TITLE) );
		std::string rulesstring = status.mPossibleRules.at(status.getGame(gameIndex).rules) + imgui.getText(TextManager::NET_RULES_BY);
		rulesstring += status.mPossibleRulesAuthor.at(status.getGame(gameIndex).rules);
		for (unsigned int i = 0; i < rulesstring.length(); i += 25)
		{
			imgui.doText(GEN_ID, Vector2(445, 205 + i / 25 * 15), rulesstring.substr(i, 25), TF_SMALL_FONT);
		}
		//  * password
		if (status.getGame(gameIndex).hasPassword)
		{
			imgui.doText(GEN_ID, Vector2(435, 240), "Password:");
			if(imgui.doEditbox(GEN_ID, 
							Vector2(445, 275), 
							11,
							mChosenPassword, 
							mChosenPasswordPosition,
							mChosenPasswordVisible ? 0 : TF_OBFUSCATE))
			{
			}
			if(imgui.doButton(GEN_ID, Vector2(735, 278), "👁"))
			{
				mChosenPasswordVisible = !mChosenPasswordVisible;
			}
		}


		// open game button
		if( imgui.doButton(GEN_ID, Vector2(435, 430), imgui.getText(TextManager::NET_JOIN) ) ||
			doEnterGame)
		{
			// send open game packet to server
			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_LOBBY);
			stream.Write((unsigned char)LobbyPacketType::JOIN_GAME);
			auto writer = createGenericWriter(&stream);
			writer->generic<unsigned int>(status.getGame(gameIndex).id);
			writer->generic<std::string>(mChosenPassword);
			/// \todo add a name

			mClient->Send(&stream, LOW_PRIORITY, RELIABLE_ORDERED, 0);
		}
	}
	// open game
	else
	{
		// no lists before the first server status
		if( status.mPossibleSpeeds.empty() || status.mPossibleRules.empty() ||
			status.mPossibleRulesAuthor.size() != status.mPossibleRules.size() )
			return;
		// a newer server status can have shorter lists
		if( mChosenSpeed >= status.mPossibleSpeeds.size() )
			mChosenSpeed = 0;
		if( mChosenRules >= status.mPossibleRules.size() )
			mChosenRules = 0;

		// info panel
		imgui.doOverlay(GEN_ID, Vector2(425.0, 90.0), Vector2(775.0, 470.0));

		// info panel contents:
		//  * gamespeed
		if(imgui.doButton(GEN_ID, Vector2(435, 100), imgui.getText(TextManager::NET_SPEED) +
					 std::to_string(int(0.5 + 100.0 / 75.0 * status.mPossibleSpeeds.at(mChosenSpeed))) + "%"))
		{
			mChosenSpeed = (mChosenSpeed + 1) % status.mPossibleSpeeds.size();
		}
		//  * points
		if(imgui.doButton(GEN_ID, Vector2(435, 135), imgui.getText(TextManager::NET_POINTS) +
											 std::to_string( mPossibleScores.at(mChosenScore) ) ))
		{
			mChosenScore = (mChosenScore + 1) % mPossibleScores.size();
		}

		//  * rulesfile
		if(imgui.doButton(GEN_ID, Vector2(435, 170), imgui.getText(TextManager::NET_RULES_TITLE) ))
		{
			mChosenRules = (mChosenRules + 1) % status.mPossibleRules.size();
		}
		std::string rulesstring = status.mPossibleRules.at(mChosenRules) + imgui.getText(TextManager::NET_RULES_BY);
		rulesstring += status.mPossibleRulesAuthor.at(mChosenRules);
		for (unsigned int i = 0; i < rulesstring.length(); i += 25)
		{
			imgui.doText(GEN_ID, Vector2(445, 205 + i / 25 * 15), rulesstring.substr(i, 25), TF_SMALL_FONT);
		}
		//  * password
		imgui.doText(GEN_ID, Vector2(435, 240), "Password:");
		if(imgui.doEditbox(GEN_ID, 
		                   Vector2(445, 275), 
		                   11,
		                   mChosenPassword, 
		                   mChosenPasswordPosition,
		                   mChosenPasswordVisible ? 0 : TF_OBFUSCATE))
		{
		}
		if(imgui.doButton(GEN_ID, Vector2(735, 278), "👁"))
		{
			mChosenPasswordVisible = !mChosenPasswordVisible;
		}

		// open game button
		if( imgui.doButton(GEN_ID, Vector2(435, 430), imgui.getText(TextManager::NET_OPEN_GAME) ))
		{
			// send open game packet to server
			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_LOBBY);
			stream.Write((unsigned char)LobbyPacketType::OPEN_GAME);
			auto writer = createGenericWriter(&stream);
			writer->generic<unsigned int>(mChosenSpeed);
			writer->generic<unsigned int>(mPossibleScores.at(mChosenScore));
			writer->generic<unsigned int>(mChosenRules);
			writer->generic<std::string>(mChosenPassword);
			/// \todo add a name

			mClient->Send(&stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
		}
	}

}

// -----------------------------------------------------------------------------------------
// 			"Host" State Substate
// -----------------------------------------------------------------------------------------

LobbyGameSubstate::LobbyGameSubstate(std::shared_ptr<RakClient> client, std::shared_ptr<GenericIn> in):
	mClient(std::move( client ))
{
 	in->uint32( mGameID );
	PlayerID creator;
	in->generic<PlayerID>(creator);
	mIsHost = mClient->GetPlayerID() == creator;

	in->string(mGameName);
	in->uint32(mSpeed);
	in->uint32(mRules);
	in->uint32(mScore);
	in->generic<std::vector<PlayerID>>(mOtherPlayers);
	in->generic<std::vector<std::string>>(mOtherPlayerNames);
}

bool LobbyGameSubstate::settingsFit(const ServerStatusData& status) const
{
	return mSpeed < status.mPossibleSpeeds.size() && mRules < status.mPossibleRules.size() &&
		mRules < status.mPossibleRulesAuthor.size();
}

bool LobbyGameSubstate::fits(const ServerStatusData& status) const
{
	return settingsFit(status) && mOtherPlayers.size() == mOtherPlayerNames.size();
}

void LobbyGameSubstate::step( IMGUI& imgui, const ServerStatusData& status )
{
	// a newer server status can have shorter lists
	if( !settingsFit(status) )
		return;

	bool no_players = mOtherPlayers.empty();
	if(mOtherPlayerNames.empty())
	{
		mOtherPlayerNames.emplace_back(""); // fake entry to prevent crash! not nice!
	}

	bool doEnterGame = false;
	if( imgui.doSelectbox(GEN_ID, Vector2(25.0, 90.0), Vector2(375.0, 470.0), mOtherPlayerNames, mSelectedPlayer) == SBA_DBL_CLICK )
	{
		doEnterGame = true;
	}

	// info panel
	imgui.doOverlay(GEN_ID, Vector2(425.0, 90.0), Vector2(775.0, 470.0));

	// info panel contents:
	//  * gamespeed
	imgui.doText(GEN_ID, Vector2(435, 100), imgui.getText(TextManager::NET_SPEED) +
				 std::to_string(int(0.5 + 100.0 / 75.0 * status.mPossibleSpeeds.at(mSpeed))) + "%");
	//  * points
	imgui.doText(GEN_ID, Vector2(435, 135), imgui.getText(TextManager::NET_POINTS) +
										 std::to_string( mScore ) );
	//  * rulesfile
	imgui.doText(GEN_ID, Vector2(435, 170), imgui.getText(TextManager::NET_RULES_TITLE) );
	std::string rulesstring = status.mPossibleRules.at(mRules) + imgui.getText(TextManager::NET_RULES_BY);
	rulesstring += status.mPossibleRulesAuthor.at(mRules);
	for (unsigned int i = 0; i < rulesstring.length(); i += 25)
	{
		imgui.doText(GEN_ID, Vector2(445, 205 + i / 25 * 15), rulesstring.substr(i, 25), TF_SMALL_FONT);
	}

	// open game button
	if( mIsHost )
	{
		if( imgui.doButton(GEN_ID, Vector2(435, 395), imgui.getText(TextManager::NET_LEAVE) ) )
		{
			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_LOBBY);
			stream.Write((unsigned char)LobbyPacketType::LEAVE_GAME);
			mClient->Send(&stream, LOW_PRIORITY, RELIABLE_ORDERED, 0);
		}

		if( !no_players && mSelectedPlayer < mOtherPlayers.size() && (imgui.doButton(GEN_ID, Vector2(435, 430), imgui.getText(TextManager::MNU_LABEL_START) ) || doEnterGame) )
		{
			// Start Game
			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_LOBBY);
			stream.Write((unsigned char)LobbyPacketType::START_GAME);
			auto writer = createGenericWriter(&stream);
			writer->generic<PlayerID>( mOtherPlayers[mSelectedPlayer] );
			mClient->Send(&stream, LOW_PRIORITY, RELIABLE_ORDERED, 0);
		}

		if( no_players )
		{
			imgui.doText(GEN_ID, Vector2(435, 430), imgui.getText(TextManager::GAME_WAITING));
		}
	}
	else
	{
		if( imgui.doButton(GEN_ID, Vector2(435, 430), imgui.getText(TextManager::NET_LEAVE) ) )
		{
			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_LOBBY);
			stream.Write((unsigned char)LobbyPacketType::LEAVE_GAME);
			mClient->Send(&stream, LOW_PRIORITY, RELIABLE_ORDERED, 0);
		}
	}
}

