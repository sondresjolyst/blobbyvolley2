#ifndef LOBBYSTATES_H_INCLUDED
#define LOBBYSTATES_H_INCLUDED

#include <vector>
#include <memory>
#include "NetworkMessage.h"
#include "PlayerIdentity.h"
#include "State.h"
#include "GenericIOFwd.h"

class RakClient;
class IMGUI;

/// What a tournament server told the player in a TOURNAMENT_STATUS packet.
struct TournamentStatusData
{
	TournamentStatus status = TournamentStatus::NO_MATCH;
	std::string opponent;
	unsigned game = 0;
	unsigned bestOf = 0;
	unsigned ownWins = 0;
	unsigned opponentWins = 0;
};

/// Reads the body of a TOURNAMENT_STATUS packet, after the packet type.
TournamentStatusData readTournamentStatus(GenericIn& in);

/// The lines of text that describe a tournament status to the player.
std::vector<std::string> describeTournamentStatus(const IMGUI& imgui, const TournamentStatusData& data);

enum class ConnectionState
{
	CONNECTING,
	CONNECTED,
	DISCONNECTED,
	CONNECTION_FAILED,
	REFUSED
};

/// \todo this currently also contains server config data
struct ServerStatusData
{
	struct OpenGame
	{
		unsigned id;
		std::string name;
		unsigned rules;
		unsigned speed;
		unsigned score;
		bool hasPassword;
	};

	const OpenGame& getGame( unsigned id ) const
	{
		return mOpenGames.at(id);
	}

	std::vector<OpenGame> mOpenGames;
	std::vector<unsigned int> mPossibleSpeeds;
	std::vector<std::string> mPossibleRules;
	std::vector<std::string> mPossibleRulesAuthor;
};

enum class PreviousState
{
	ONLINE,
	LAN,
	MAIN
};

class LobbySubstate
{
	public:
		virtual ~LobbySubstate();
		virtual void step(IMGUI& imgui, const ServerStatusData& status) = 0;
};

class LobbyState : public State
{
	public:
		LobbyState(ServerInfo info, PreviousState previous);
		~LobbyState() override;

		void step_impl() override;
		void init() override;
		const char* getStateName() const override;

	private:
		std::shared_ptr<RakClient> mClient;
		PlayerIdentity mLocalPlayer;
		ServerInfo mInfo;
		PreviousState mPrevious;

		ConnectionState mLobbyState;
		// why a tournament server refused us, an EntryRefusedReason
		unsigned char mRefusedReason = 0;

		ServerStatusData mStatus;
		std::shared_ptr<LobbySubstate> mSubState;
		// true once the server said it runs a tournament
		bool mTournament = false;

		// indices of settings that resemble most closely those of local settings
		unsigned mPreferedSpeed = -1;
		unsigned mPreferedRules = 0;
		unsigned mPreferedScore = 3;
};


class LobbyMainSubstate : public LobbySubstate
{
public:
	LobbyMainSubstate(std::shared_ptr<RakClient> client, unsigned speed, unsigned rules, unsigned score);
	void step( IMGUI& imgui, const ServerStatusData& status ) override;
private:
	std::shared_ptr<RakClient> mClient;

	unsigned int mSelectedGame = 0;

	// temp variables for open game
	unsigned mChosenSpeed;
	unsigned mChosenRules;
	unsigned mChosenScore;
	std::string mChosenPassword;
	unsigned int mChosenPasswordPosition;
	bool mChosenPasswordVisible;

	std::vector<unsigned> mPossibleScores{2, 5, 10, 15, 20, 25, 40, 50};
};


/// Shown on a tournament server, where the server starts the matches.
class LobbyTournamentSubstate : public LobbySubstate
{
public:
	explicit LobbyTournamentSubstate(TournamentStatusData data);
	void step( IMGUI& imgui, const ServerStatusData& status ) override;
private:
	TournamentStatusData mData;
};


class LobbyGameSubstate : public LobbySubstate
{
public:
	LobbyGameSubstate(std::shared_ptr<RakClient> client, std::shared_ptr<GenericIn>);

	void step( IMGUI& imgui, const ServerStatusData& status ) override;
private:
	std::shared_ptr<RakClient> mClient;

	unsigned mGameID = 0;
	std::string mGameName;
	bool mIsHost = false;
	unsigned mSpeed = 0;
	unsigned mRules = 0;
	unsigned mScore = 3;

	unsigned mSelectedPlayer = 0;

	std::vector<PlayerID> mOtherPlayers;
	std::vector<std::string> mOtherPlayerNames;
};

#endif // LOBBYSTATES_H_INCLUDED
