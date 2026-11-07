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

/* includes */
#include <algorithm>
#include <atomic>
#include <iostream>

#include <utility>

#include "raknet/RakClient.h"
#include "raknet/PacketEnumerations.h"

#include "NetworkState.h"
#include "replays/ReplayRecorder.h"
#include "DuelMatch.h"
#include "BlobbyApp.h"
#include "IMGUI.h"
#include "SoundManager.h"
#include "LocalInputSource.h"
#include "InputDevice.h"
#include "UserConfig.h"
#include "FileExceptions.h"
#include "GenericIO.h"
#include "FileRead.h"
#include "FileWrite.h"
#include "SpeedController.h"
#include "LobbyStates.h"
#include "server/MatchAssignments.h"

// global variable to save the lag
int CURRENT_NETWORK_LAG = -1;

static bool isValidMatchEvent(char event, PlayerSide side)
{
	if(event < MatchEvent::BALL_HIT_BLOB || event > MatchEvent::RESET_BALL)
		return false;
	if(event == MatchEvent::BALL_HIT_BLOB || event == MatchEvent::BALL_HIT_GROUND)
		return isPlayerSide(side);
	return side == NO_PLAYER || isPlayerSide(side);
}


/* implementation */
NetworkGameState::NetworkGameState( std::shared_ptr<RakClient> client, int rule_checksum, int score_to_win, std::string own_name)
	: GameState(new DuelMatch(true, DEFAULT_RULES_FILE, score_to_win))
	, mNetworkState(WAITING_FOR_OPPONENT)
	, mWaitingForReplay(false)
	, mClient(std::move(client))
	, mWinningPlayer(NO_PLAYER)
	, mSelectedChatmessage(0)
	, mChatCursorPosition(0)
	, mRulesChecksum(rule_checksum)
	, mOwnName(std::move(own_name))
{
}

void NetworkGameState::init()
{
	std::shared_ptr<IUserConfigReader> config = IUserConfigReader::createUserConfigReader("config.xml");
	mOwnSide = (PlayerSide)config->getInteger("network_side");
	mUseRemoteColor = config->getBool("use_remote_color");
	mLocalInput.reset(new LocalInputSource(getInputMgr().beginGame(mOwnSide)));

	// game is not started until two players are connected
	mMatch->pause();

	// load/init players

	if(mOwnSide == LEFT_PLAYER)
	{
		PlayerIdentity localplayer = config->loadPlayerIdentity(LEFT_PLAYER, true);
		PlayerIdentity remoteplayer = config->loadPlayerIdentity(RIGHT_PLAYER, true);
		mLocalPlayer = &mMatch->getPlayer( LEFT_PLAYER );
		mRemotePlayer = &mMatch->getPlayer( RIGHT_PLAYER );
		mMatch->setPlayers( localplayer, remoteplayer );
	}
	else
	{
		PlayerIdentity localplayer = config->loadPlayerIdentity(RIGHT_PLAYER, true);
		PlayerIdentity remoteplayer = config->loadPlayerIdentity(LEFT_PLAYER, true);
		mLocalPlayer = &mMatch->getPlayer( RIGHT_PLAYER );
		mRemotePlayer = &mMatch->getPlayer( LEFT_PLAYER );
		mMatch->setPlayers( remoteplayer, localplayer );
	}

	mRemotePlayer->setName("");
	if (!mOwnName.empty())
		mLocalPlayer->setName(mOwnName);

	// check the rules
	int ourChecksum = 0;
	if (mRulesChecksum != 0)
	{
		try
		{
			FileRead rulesFile("rules/"+TEMP_RULES_NAME);
			ourChecksum = rulesFile.calcChecksum(0);
			rulesFile.close();
		}
		catch( FileLoadException& ex )
		{
			// file doesn't exist - nothing to do here
		}
	}

	// the cached rules match the server, so use them instead of downloading them again
	bool haveRules = mRulesChecksum != 0 && mRulesChecksum == ourChecksum;
	if (haveRules)
		mMatch->setRules(TEMP_RULES_NAME);

	mNeedRules = mRulesChecksum != 0 && !haveRules;
	sendRulesAnswer();
}

void NetworkGameState::sendRulesAnswer()
{
	RakNet::BitStream stream2;
	makeRulesAnswerPacket(stream2, mNeedRules);
	mClient->Send(&stream2, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
}

void NetworkGameState::startNextGame()
{
	mNextGamePending = false;
	mKeepConnection = true;
	auto game = new NetworkGameState(mClient, mNextGameChecksum, mNextGameScoreToWin, mOwnName);
	if (mTournamentLobby)
		game->setTournamentLobby(*mTournamentLobby);
	switchState(game);
}

void NetworkGameState::setTournamentLobby(const ServerInfo& info)
{
	mTournamentLobby = std::make_shared<ServerInfo>(info);
}

void NetworkGameState::backToTournamentLobby()
{
	if (!mTournamentLobby)
	{
		switchState(new MainMenuState());
		return;
	}
	mKeepConnection = true;
	switchState(new LobbyState(*mTournamentLobby, mClient, mTournamentStatus));
}

NetworkGameState::~NetworkGameState()
{
	CURRENT_NETWORK_LAG = -1;
	if (!mKeepConnection)
		mClient->Disconnect(50);
}

void NetworkGameState::step_impl()
{
	IMGUI& imgui = getIMGUI();

	packet_ptr packet;
	// once the next game started, its state reads the following packets
	while (!mKeepConnection && nullptr != (packet = mClient->Receive()))
	{
		switch(packet->data[0])
		{
			case ID_GAME_UPDATE:
			{
				RakNet::BitStream stream(packet->data, packet->length, false);
				unsigned timeBack = readGameUpdateTime(stream);
				CURRENT_NETWORK_LAG = SDL_GetTicks() - timeBack;
				DuelMatchState ms;
				readGameUpdateState(stream, ms);
				// inject network data into game
				mMatch->setState( ms );
				break;
			}

			case ID_GAME_EVENTS:
			{
				RakNet::BitStream stream(packet->data, packet->length, false);
				stream.IgnoreBytes(1);	//ID_GAME_EVENTS
				//printf("Physic packet received. Time: %d\n", ival);
				// read events
				char event = 0;
				while(stream.Read(event) && event != 0)
				{
					// the side is written as a byte, so NO_PLAYER arrives as 255
					char sideByte = 0;
					float intensity = -1;
					if(!stream.Read(sideByte))
						break;
					const PlayerSide side = (PlayerSide)static_cast<signed char>(sideByte);
					if( event == MatchEvent::BALL_HIT_BLOB )
					{
						if(!stream.Read(intensity))
							break;
						// negated, so that a NaN also ends up as 0
						if(!(intensity > 0))
							intensity = 0;
						else if(intensity > 1)
							intensity = 1;
					}
					if(!isValidMatchEvent(event, side))
						continue;
					MatchEvent me{ MatchEvent::EventType(event), side, intensity };
					mMatch->trigger( me );
				}
				break;
			}
			case ID_WIN_NOTIFICATION:
			{
				RakNet::BitStream stream(packet->data, packet->length, false);
				int winner = NO_PLAYER;
				if(!readWinNotification(stream, winner))
					break;
				mWinningPlayer = (PlayerSide)winner;

				// last point must not be added anymore, because
				// the score is also simulated local, so it is already
				// right. under strange circumstances this need not
				// be true, but then the score is set to the correy value
				// by ID_BALL_RESET

				mNetworkState = PLAYER_WON;
				break;
			}
			case ID_OPPONENT_DISCONNECTED:
			{
				// In this state, a leaving opponent would not be very surprising
				if (mNetworkState != PLAYER_WON)
					mNetworkState = OPPONENT_DISCONNECTED;
				break;
			}
			case ID_PAUSE:
				if (mNetworkState == PLAYING)
				{
					mNetworkState = PAUSING;
					mMatch->pause();
				}
				break;
			case ID_UNPAUSE:
				if (mNetworkState == PAUSING)
				{
					SDL_StopTextInput();
					mNetworkState = PLAYING;
					mMatch->unpause();
				}
				break;
			case ID_GAME_READY:
			{
				RakNet::BitStream stream(packet->data, packet->length, false);
				GameReadyData ready = readGameReady(stream);
				SpeedController::getMainInstance()->setGameSpeed(ready.speed);
				auto ncolor = Color(ready.color);

				mRemotePlayer->setName(ready.opponent);

				setDefaultReplayName(mLocalPlayer->getName(), mRemotePlayer->getName());

				// check whether to use remote player color
				if(mUseRemoteColor)
				{
					mRemotePlayer->setStaticColor(ncolor);
				}

				mNetworkState = PLAYING;
				// start game
				mMatch->unpause();

				// game ready whistle
				playSound(SoundManager::WHISTLE, ROUND_START_SOUND_VOLUME);
				break;
			}
			case ID_RULES_CHECKSUM:
			{
				// a server that did not get the answer to its checksum asks again
				if (mNetworkState == WAITING_FOR_OPPONENT)
				{
					sendRulesAnswer();
					break;
				}
				// only a tournament server sends this during a game. It starts the next
				// game of the match on the same connection.
				if (mNetworkState != PLAYER_WON && mNetworkState != OPPONENT_DISCONNECTED)
					break;
				RakNet::BitStream stream(packet->data, packet->length, false);
				RulesChecksumData rules;
				if (!readRulesChecksum(stream, rules))
					break;
				mNextGamePending = true;
				mNextGameChecksum = rules.checksum;
				mNextGameScoreToWin = rules.scoreToWin;
				// a replay that is asked for or on its way is saved first
				if (!mSaveReplay && !mWaitingForReplay)
					startNextGame();
				break;
			}
			case ID_LOBBY:
			{
				RakNet::BitStream stream(packet->data, packet->length, false);
				auto in = createGenericReader(&stream);
				unsigned char type = 0;
				in->byte(type);	// ID_LOBBY
				in->byte(type);
				if ((LobbyPacketType)type == LobbyPacketType::ENTRY_REFUSED)
				{
					// the join code moved to another connection, which closes this one
					unsigned char reason = 0xFF;
					in->byte(reason);
					mCodeInUse = reason == (unsigned char)EntryRefusedReason::CODE_IN_USE;
					mNetworkState = DISCONNECTED;
				}
				else if ((LobbyPacketType)type == LobbyPacketType::TOURNAMENT_STATUS)
				{
					mTournamentStatus = readTournamentStatus(*in);
					mHasTournamentStatus = true;
					if (!mTournamentStatus.ownName.empty())
					{
						mOwnName = mTournamentStatus.ownName;
						mLocalPlayer->setName(mOwnName);
					}
					mNextGameTicks = SDL_GetTicks() + mTournamentStatus.nextGameInMs;
				}
				break;
			}
			case ID_RULES:
			{
				RakNet::BitStream stream(packet->data, packet->length, false);

				stream.IgnoreBytes(1);	// ignore ID_RULES

				int rulesLength = 0;
				stream.Read(rulesLength);
				if (rulesLength > 0 && rulesLength <= stream.GetNumberOfUnreadBits() / 8)
				{
					std::vector<char> rulesString(rulesLength + 1, '\0');
					stream.Read(rulesString.data(), rulesLength);
					FileWrite rulesFile("rules/"+TEMP_RULES_NAME);
					rulesFile.write(rulesString.data(), rulesLength);
					rulesFile.close();
					mMatch->setRules(TEMP_RULES_NAME);
				}
				else
				{
					// either old server, or we have to use fallback ruleset
					mMatch->setRules( FALLBACK_RULES_NAME );
				}

				break;
			}
			case ID_SPONSORS:
				// only a tournament server sends the board, after the tournament status
				if (mTournamentLobby || mHasTournamentStatus)
					getApp().handleSponsorPacket(packet->data, packet->length, mClient);
				break;
			// status messages we don't care about
			case ID_REMOTE_DISCONNECTION_NOTIFICATION:
			case ID_REMOTE_CONNECTION_LOST:
			case ID_SERVER_STATUS:
			case ID_REMOTE_NEW_INCOMING_CONNECTION:
			case ID_REMOTE_EXISTING_CONNECTION:
				break;
			case ID_DISCONNECTION_NOTIFICATION:
			case ID_CONNECTION_LOST:
				if (mNetworkState != PLAYER_WON)
					mNetworkState = DISCONNECTED;
				break;
			case ID_NO_FREE_INCOMING_CONNECTIONS:
				mNetworkState = SERVER_FULL;
				break;
			case ID_CHAT_MESSAGE:
			{
				RakNet::BitStream stream(packet->data, packet->length, false);
				stream.IgnoreBytes(1);	// ID_CHAT_MESSAGE
				// Insert Message in the log and focus the last element
				char message[31] = {0};
				if(!stream.Read(message, sizeof(message)))
					break;
				message[30] = '\0';

				// Insert Message in the log and focus the last element
				mChatlog.push_back((std::string) message);
				mChatOrigin.push_back(false);
				mSelectedChatmessage = mChatlog.size() - 1;
				playSound(SoundManager::CHAT, ROUND_START_SOUND_VOLUME);
				break;
			}
			case ID_REPLAY:
			{
				/// \todo we should take more action if server sends replay
				///		even if not requested!
				if(!mWaitingForReplay)
					break;

				RakNet::BitStream stream(packet->data, packet->length, false);
				stream.IgnoreBytes(1);	// ID_REPLAY

				// read stream into a dummy replay recorder
				std::shared_ptr<GenericIn> reader = createGenericReader( &stream );
				ReplayRecorder dummyRec;
				dummyRec.receive( *reader );
				// and save that
				saveReplay(dummyRec);

				// mWaitingForReplay will be set to false even if replay could not be saved because
				// the server won't send it again.
				mWaitingForReplay = false;

				break;
			}

			// we never do anything that should cause such a packet to be received!
			case ID_CONNECTION_REQUEST_ACCEPTED:
			case ID_CONNECTION_ATTEMPT_FAILED:
				assert( 0 );
				break;

			case ID_BLOBBY_SERVER_PRESENT:
			{
				// this should only be called if we use the stay on server option
				RakNet::BitStream stream(packet->data, packet->length, false);
				stream.IgnoreBytes(1);	//ID_BLOBBY_SERVER_PRESENT
				ServerInfo info(stream,	mClient->PlayerIDToDottedIP(packet->playerId), packet->playerId.port);

				if (packet->length == ServerInfo::BLOBBY_SERVER_PRESENT_PACKET_SIZE )
				{
					switchState(new LobbyState(info, PreviousState::MAIN));
				}
				break;
			}
			default:
				printf("Received unknown Packet %d\n", packet->data[0]);
				break;
		}
	}

	// does this generate any problems if we pause at the exact moment an event is set ( i.e. the ball hit sound
	// could be played in a loop)?
	presentGame();
	presentGameUI();

	if (is_exiting() && mNetworkState != PLAYING)
	{
		if(mNetworkState == PAUSING)
		{
			// end pause
			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_UNPAUSE);
			mClient->Send(&stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
		}
		else
		{
			switchState(new MainMenuState);
		}
	}
	else if (is_exiting() && mSaveReplay)
	{
		mSaveReplay = false;
		getIMGUI().resetSelection();
	}
	else if (!mErrorMessage.empty())
	{
		displayErrorMessageBox();
	}
	else if (mSaveReplay)
	{
		if ( displaySaveReplayPrompt() )
		{

			// request replay from server
			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_REPLAY);
			mClient->Send(&stream, LOW_PRIORITY, RELIABLE_ORDERED, 0);

			mSaveReplay = false;
			mWaitingForReplay = true;
		}
	}
	else if (mNextGamePending && !mWaitingForReplay)
	{
		// the replay was saved or the user gave up on it
		startNextGame();
	}
	else if (mWaitingForReplay)
	{
		imgui.doOverlay(GEN_ID, Vector2(150, 200), Vector2(650, 400));
		imgui.doText(GEN_ID, Vector2(190, 220), TextManager::RP_WAIT_REPLAY);
		if (imgui.doButton(GEN_ID, Vector2(440, 330), TextManager::LBL_CANCEL))
		{
			mSaveReplay = false;
			mWaitingForReplay = false;
			imgui.resetSelection();
		}
		imgui.doCursor();
	}
	else switch (mNetworkState)
	{
		case WAITING_FOR_OPPONENT:
		{
			imgui.doOverlay(GEN_ID, Vector2(100.0, 210.0),
					Vector2(700.0, 310.0));
			imgui.doText(GEN_ID, Vector2(150.0, 250.0),
					TextManager::GAME_WAITING);
			break;
		}
		case OPPONENT_DISCONNECTED:
		{
			imgui.doCursor();

			displayQueryPrompt(200,
				TextManager::GAME_OPP_LEFT,
				std::make_tuple(TextManager::LBL_OK, [&](){ if (mTournamentLobby) backToTournamentLobby(); else switchState(new MainMenuState); }),
				std::make_tuple(TextManager::RP_SAVE, [&](){ mSaveReplay = true; imgui.resetSelection(); }),
				std::make_tuple(TextManager::NET_STAY_ON_SERVER, [&](){
					// Send a blobby server connection request
					RakNet::BitStream stream;
					stream.Write((unsigned char)ID_BLOBBY_SERVER_PRESENT);
					stream.Write(BLOBBY_VERSION_MAJOR);
					stream.Write(BLOBBY_VERSION_MINOR);
					mClient->Send(&stream, LOW_PRIORITY, RELIABLE_ORDERED, 0);
				 }));
			break;
		}
		case DISCONNECTED:
		{
			imgui.doCursor();
			imgui.doOverlay(GEN_ID, Vector2(100.0, 210.0),
					Vector2(700.0, 370.0));
			imgui.doText(GEN_ID, Vector2(120.0, 250.0),
					mCodeInUse ? TextManager::NET_CODE_IN_USE : TextManager::NET_DISCONNECT);
			if (imgui.doButton(GEN_ID, Vector2(230.0, 320.0),
					TextManager::LBL_OK))
			{
				switchState(new MainMenuState);
			}
			if (imgui.doButton(GEN_ID, Vector2(350.0, 320.0), TextManager::RP_SAVE))
			{
				mSaveReplay = true;
				imgui.resetSelection();
			}
			break;
		}
		case SERVER_FULL:
		{
			imgui.doCursor();
			imgui.doOverlay(GEN_ID, Vector2(100.0, 210.0),Vector2(700.0, 370.0));
			imgui.doText(GEN_ID, Vector2(200.0, 250.0),	TextManager::NET_SERVER_FULL);
			if (imgui.doButton(GEN_ID, Vector2(350.0, 300.0), TextManager::LBL_OK))
			{
				switchState(new MainMenuState);
			}
			break;
		}
		case PLAYING:
		{
			mMatch->step();

			mLocalInput->updateInput();
			PlayerInputAbs input = mLocalInput->getRealInput();

			if (is_exiting())
			{
				RakNet::BitStream stream;
				stream.Write((unsigned char)ID_PAUSE);
				mClient->Send(&stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
			}
			RakNet::BitStream stream;
			stream.Write((unsigned char)ID_INPUT_UPDATE);
			stream.Write( SDL_GetTicks() );
			input.writeTo(stream);
			mClient->Send(&stream, HIGH_PRIORITY, UNRELIABLE_SEQUENCED, 0);
			break;
		}
		case PLAYER_WON:
		{
			mMatch->updateEvents(); // so the last whistle will be sounded
			displayWinningPlayerScreen(mWinningPlayer);
			// between two games of a series there is no OK, so nobody leaves the server by mistake
			const bool seriesGoesOn = mHasTournamentStatus &&
				(mTournamentStatus.status == TournamentStatus::NEXT_GAME || mTournamentStatus.status == TournamentStatus::PLAYING);
			if (mHasTournamentStatus && mTournamentStatus.status != TournamentStatus::PLAYING)
			{
				auto lines = describeTournamentStatus(imgui, mTournamentStatus);
				if (mTournamentStatus.status == TournamentStatus::NEXT_GAME)
				{
					unsigned now = SDL_GetTicks();
					unsigned left = mNextGameTicks > now ? (mNextGameTicks - now + 999) / 1000 : 0;
					lines.front() = imgui.getText(TextManager::TNM_NEXT_GAME_IN) + std::to_string(left);
				}
				// centred under the winner text, so the lines stay clear of the trophy
				for (unsigned i = 0; i < lines.size(); ++i)
					imgui.doText(GEN_ID, Vector2(500, 400 + 18 * i), lines[i], TF_ALIGN_CENTER | TF_SMALL_FONT);
			}
			if (!seriesGoesOn && imgui.doButton(GEN_ID, Vector2(290, 360), TextManager::LBL_OK))
			{
				if (mTournamentLobby)
					backToTournamentLobby();
				else
					switchState(new MainMenuState());
			}
			if (imgui.doButton(GEN_ID, Vector2(380, 360), TextManager::RP_SAVE))
			{
				mSaveReplay = true;
				imgui.resetSelection();
			}
			break;
		}
		case PAUSING:
		{
			// Query
			displayQueryPrompt(20,
				TextManager::GAME_PAUSED,
				std::make_tuple(TextManager::LBL_CONTINUE, [&](){
					RakNet::BitStream stream;
					stream.Write((unsigned char)ID_UNPAUSE);
					mClient->Send(&stream, HIGH_PRIORITY, RELIABLE_ORDERED, 0);
				}),
				std::make_tuple(TextManager::GAME_QUIT,    [&](){ switchState(new MainMenuState); }),
				std::make_tuple(TextManager::RP_SAVE, [&](){ mSaveReplay = true; imgui.resetSelection(); }));

			// Chat
			imgui.doChatbox(GEN_ID, Vector2(10, 240), Vector2(790, 500), mChatlog, mSelectedChatmessage, mChatOrigin);
			if (imgui.doEditbox(GEN_ID, Vector2(30, 510), 30, mChattext, mChatCursorPosition, 0, true))
			{

				// GUI-Hack, so that we can send messages
				if ((getInputMgr().getLastActionKey() == "Return") && (!mChattext.empty()))
				{
					RakNet::BitStream stream;
					char message[31];

					strncpy(message, mChattext.c_str(), sizeof(message));
					stream.Write((unsigned char)ID_CHAT_MESSAGE);
					stream.Write(message, sizeof(message));
					mClient->Send(&stream, LOW_PRIORITY, RELIABLE_ORDERED, 0);
					mChatlog.push_back(mChattext);
					mChatOrigin.push_back(true);
					mSelectedChatmessage = mChatlog.size() - 1;
					mChattext = "";
					mChatCursorPosition = 0;
					playSound(SoundManager::CHAT, ROUND_START_SOUND_VOLUME);
				}
			}
			imgui.doCursor();
		}
	}
}

const char* NetworkGameState::getStateName() const
{
	return "NetworkGameState";
}

// definition of syslog for client hosted games
void syslog(int pri, const char* format, ...)
{
	// do nothing?
}

// debug counters
std::atomic<int> SWLS_PacketCount{0};
std::atomic<int> SWLS_Connections{0};
std::atomic<int> SWLS_Games{0};
std::atomic<int> SWLS_GameSteps{0};
std::atomic<int> SWLS_ServerEntered{0};
