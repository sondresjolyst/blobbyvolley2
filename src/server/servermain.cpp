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
#include "DedicatedServer.h"

/* includes */
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <cstdio>
#include <ctime>
#include <future>
#include <csignal>
#include <functional>
#include <thread>

#include <cerrno>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include <SDL.h>

#include "DedicatedServer.h"
#include "FileUtil.h"
#include "SiteConfig.h"
#ifdef BLOBBY_SITE_SYNC
#include "SiteSync.h"
#include "CurlHttpClient.h"
#endif
#include "SpeedController.h"
#include "FileSystem.h"
#include "UserConfig.h"
#include "Global.h"

// platform specific
#ifndef WIN32
#include <sys/syslog.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <cstdarg>
#endif


/* implementation */

#ifdef WIN32

// function for logging to replacing syslog
void syslog(int pri, const char* format, ...);

#endif

static bool g_run_in_foreground = false;
static bool g_print_syslog_to_stderr = false;
static std::string g_config_file = "server.xml";
static std::atomic<bool> g_run_server(true); // set this variable to false to stop the server

// ...
void printHelp();
void process_arguments(int argc, char** argv);
void fork_to_background();
void setup_physfs(char* argv0);
std::string statistics();

// server workload statistics
std::atomic<int> SWLS_PacketCount{0};
std::atomic<int> SWLS_Connections{0};
std::atomic<int> SWLS_Games{0};
std::atomic<int> SWLS_GameSteps{0};
std::atomic<int> SWLS_RunningTime{0};

const int UPDATE_FREQUENCY = 10;

void main_loop(DedicatedServer& server);
void read_commands(DedicatedServer& server);

// docker stop and Ctrl+C end the main loop, so the server shuts down cleanly
extern "C" void stop_on_signal(int)
{
	g_run_server = false;
}

int main(int argc, char** argv)
{
	process_arguments(argc, argv);

	FileSystem fileSys(argv[0]);

	if (!g_run_in_foreground)
	{
		fork_to_background();
	}

	#ifndef WIN32
	// files and folders the server makes are only for the server user
	umask(077);
	int syslog_options = LOG_CONS | LOG_PID | (g_print_syslog_to_stderr ? LOG_PERROR : 0);

	openlog("blobby-server", syslog_options, LOG_DAEMON);
	#endif

	setup_physfs(argv[0]);

	int maxClients = 100;
	std::string rulesFile = DEFAULT_RULES_FILE;
	std::string gameSpeeds = "75";
	bool tournamentMode = false;
	std::string tournamentPlayersFile;
	std::string tournamentResultsFile;
	int tournamentMaxMatches = 0;
	int tournamentNextGameDelay = DEFAULT_NEXT_GAME_DELAY;
	SiteConfigFile siteFile;

	UserConfig config;
	try
	{
		config.loadFile(g_config_file);
		maxClients = config.getInteger("maximum_clients");
		rulesFile  = config.getString("rules", DEFAULT_RULES_FILE);
		gameSpeeds = config.getString("speeds", gameSpeeds);
		siteFile.tournamentMode = config.getBool("tournament_mode", false);
		siteFile.siteUrl = config.getString("tournament_site_url");
		siteFile.siteToken = config.getString("tournament_site_token");
		siteFile.dataDir = config.getString("tournament_data_dir");

		// bring that value into a sane range
		if(maxClients <= 0 || maxClients > 150)
			maxClients = 150;
	}
	catch (std::exception& e)
	{
		syslog(LOG_ERR, "server.xml not found. Falling back to default values.");
	}

	// the environment comes before the config file, so a container needs no config file
	SiteConfig site;
	{
		std::vector<std::string> errors;
		if(!loadSiteConfig(siteFile, [](const char* name){ return std::getenv(name); }, site, errors))
		{
			for(const auto& error : errors)
				syslog(LOG_ERR, "%s", error.c_str());
			syslog(LOG_ERR, "The tournament settings are not valid, the server stops");
			return 1;
		}
	}
	tournamentMode = site.tournamentMode;
	if(tournamentMode)
	{
		if(!site.dataDir.empty() && !makeFolder(site.dataDir))
			syslog(LOG_ERR, "Cannot make the data folder %s", site.dataDir.c_str());
		tournamentPlayersFile = pathInDataDir(site.dataDir, config.getString("tournament_players_file", "tournament_players.xml"));
		tournamentResultsFile = pathInDataDir(site.dataDir, config.getString("tournament_results_file", "tournament_results.jsonl"));
		tournamentMaxMatches = config.getInteger("tournament_max_matches", 0);
		if(tournamentMaxMatches < 0)
			tournamentMaxMatches = 0;
		tournamentNextGameDelay = config.getInteger("tournament_next_game_delay", DEFAULT_NEXT_GAME_DELAY);
		if(tournamentNextGameDelay != clampNextGameDelay(tournamentNextGameDelay))
		{
			syslog(LOG_ERR, "tournament_next_game_delay must be %d to %d seconds, using %d",
				   MIN_NEXT_GAME_DELAY, MAX_NEXT_GAME_DELAY, clampNextGameDelay(tournamentNextGameDelay));
			tournamentNextGameDelay = clampNextGameDelay(tournamentNextGameDelay);
		}
	}

	ServerInfo myinfo(config);
	{
		int port = 0;
		std::string error;
		if(!loadServerPort(myinfo.port, [](const char* name){ return std::getenv(name); }, port, error))
		{
			syslog(LOG_ERR, "%s, the server stops", error.c_str());
			return 1;
		}
		myinfo.port = static_cast<uint16_t>(port);
	}
	std::vector<std::string> rule_vec;
	boost::algorithm::split(rule_vec, rulesFile, boost::algorithm::is_space(), boost::algorithm::token_compress_on);

	std::vector<std::string> speed_vec_str;
	boost::algorithm::split(speed_vec_str, gameSpeeds, boost::algorithm::is_space(), boost::algorithm::token_compress_on);

	std::vector<float> speed_vec;
	std::transform(speed_vec_str.begin(), speed_vec_str.end(), std::back_inserter(speed_vec), [](const std::string& v ){ return std::stof(v);});

	DedicatedServer server(myinfo, rule_vec, speed_vec, maxClients);
#ifdef BLOBBY_SITE_SYNC
	std::shared_ptr<SiteReporter> siteReporter;
#endif

	if(tournamentMode)
	{
		std::unique_ptr<JoinCodeGate> gate(new JoinCodeGate(tournamentPlayersFile));
		server.setJoinCodeGate(std::move(gate));

		const ServerLog logLine = logToSyslog;
		std::unique_ptr<TournamentDirector> director(new TournamentDirector(tournamentMaxMatches, (unsigned)tournamentNextGameDelay * 1000u));
		std::shared_ptr<TournamentEvents> events = std::make_shared<TournamentLog>(tournamentResultsFile, logLine);

		if(site.usesSite())
		{
#ifdef BLOBBY_SITE_SYNC
			if(!CurlHttpClient::globalInit())
			{
				syslog(LOG_ERR, "libcurl could not be set up, the server stops");
				return 1;
			}
			const std::string outboxFile = pathInDataDir(site.dataDir, "tournament_outbox.jsonl");
			std::unique_ptr<Outbox> outbox(new Outbox(outboxFile, logLine));
			outbox->load();
			SitePaths paths;
			paths.playersFile = tournamentPlayersFile;
			paths.syncFile = pathInDataDir(site.dataDir, "tournament_sync.json");
			paths.refusedFile = pathInDataDir(site.dataDir, "tournament_refused.jsonl");
			paths.sponsorsFolder = pathInDataDir(site.dataDir, "sponsors");
			std::unique_ptr<HttpClient> http(new CurlHttpClient(site.siteUrl, site.siteToken, site.caFile, site.allowHttpLocalhost));
			siteReporter = std::make_shared<SiteReporter>(events, std::move(http), std::move(outbox), paths, logLine);
			events = siteReporter;
			syslog(LOG_NOTICE, "Tournament site %s: the sync is written to %s, reports wait in %s until the site has them",
				   site.siteUrl.c_str(), tournamentPlayersFile.c_str(), outboxFile.c_str());
			if(site.allowHttpLocalhost)
				syslog(LOG_ERR, "BLOBBY_SITE_ALLOW_HTTP is set, it is only meant for tests");
#else
			syslog(LOG_ERR, "This server was built without the tournament site client, so it cannot use BLOBBY_SITE_URL. Build it with libcurl and nlohmann_json.");
			return 1;
#endif
		}

		director->setEvents(events);
		server.setTournamentDirector(std::move(director));

#ifdef BLOBBY_SITE_SYNC
		// the site sync stores the sponsor board here, and it can also be written by hand
		const std::string sponsorsFolder = pathInDataDir(site.dataDir, "sponsors");
		auto storedBoard = std::make_shared<StoredSponsorBoard>(sponsorsFolder, logLine);
		server.setSponsorSource([storedBoard](sponsors::Board& board){ return storedBoard->poll(board); });
		syslog(LOG_NOTICE, "The sponsor board in %s is sent to the players", sponsorsFolder.c_str());
#endif

		syslog(LOG_NOTICE, "Tournament mode is on, players join with codes from %s", tournamentPlayersFile.c_str());
		if(tournamentMaxMatches == 0)
			syslog(LOG_NOTICE, "Assigned matches start as soon as both players are connected");
		else
			syslog(LOG_NOTICE, "At most %d assigned matches run at the same time", tournamentMaxMatches);
		if(!tournamentResultsFile.empty())
			syslog(LOG_NOTICE, "Game results are added to %s", tournamentResultsFile.c_str());
	}

	syslog(LOG_NOTICE, "Blobby Volley 2 dedicated server %s (%s %s), protocol version %i.%i, started", BLOBBY_UPSTREAM_VERSION, BLOBBY_FORK_NAME, BLOBBY_FORK_VERSION, BLOBBY_VERSION_MAJOR, BLOBBY_VERSION_MINOR);
	syslog(LOG_NOTICE, "Listening on UDP port %i", (int)myinfo.port);

#ifdef BLOBBY_SITE_SYNC
	if(siteReporter)
		siteReporter->start();
#endif

	std::signal(SIGTERM, stop_on_signal);
	std::signal(SIGINT, stop_on_signal);

	// main loop
	auto serverthread = std::async(std::launch::async, [&](){main_loop(server);});

	// commands are read on their own thread, so a signal can end the server while it waits for input
	std::thread(read_commands, std::ref(server)).detach();

	// We wait until the dedicated server is shut down
	serverthread.wait();
#ifdef BLOBBY_SITE_SYNC
	if(siteReporter)
		siteReporter->stop();
#endif

	syslog(LOG_NOTICE, "Blobby Volley 2 dedicated server shutting down");
	#ifndef WIN32
	closelog();
	#endif

	return 0;
}

void read_commands(DedicatedServer& server)
{
	while(g_run_server)
	{
		std::string command;

		if (!(std::cin >> command))
			// If we reached eof (happens immediately when server runs in background)
			// we don't accept any input so that we have no endless loop
			break;

		std::vector<std::string> cmd_vec;
		boost::algorithm::split(cmd_vec, command, boost::algorithm::is_space(), boost::algorithm::token_compress_on);


		if( cmd_vec[0] == "exit" )
		{
			/// \todo check for confirmation if there are still players connected!
			g_run_server = false;
			break;
		}
		else if ( cmd_vec[0] == "players" )
		{
			server.printAllPlayers(std::cout);
		}
		else if ( cmd_vec[0] == "games" )
		{
			server.printAllGames(std::cout);
		}
		else if ( cmd_vec[0] == "status" )
		{
			std::cout << statistics() << std::endl;
		}

	}
}

// -----------------------------------------------------------------------------------------
//    server main loop function
// ------------------------------
void main_loop( DedicatedServer& server)
{
	SpeedController scontroller( UPDATE_FREQUENCY );

	while ( g_run_server )
	{
		// -------------------------------------------------------------------------------
		//  step through all network games and process input - if a game ended, delete it
		// -------------------------------------------------------------------------------

		SWLS_RunningTime++;

		if(SWLS_RunningTime % (UPDATE_FREQUENCY * 60 * 60 /*1h*/) == 0 )
		{
			syslog(LOG_DEBUG, "%s", statistics().c_str());
		}

		server.processPackets();
		server.updateGames();

		scontroller.update();
	}
}

// -----------------------------------------------------------------------------------------

void printHelp()
{
	std::cout << "Usage: blobby-server [OPTION...]" << std::endl;
	std::cout << "  -n, --no-daemon           Don't run as background process" << std::endl;
	std::cout << "  -p, --print-msgs          Print messages to stderr" << std::endl;
	std::cout << "  -c, --config-file <path>  Use custom config file instead of server.xml" << std::endl;
	std::cout << "  -h, --help                This message\n" << std::endl;
	std::cout << "during the run of the programme, the following commands can be used:\n"
			  << "players:   print player list\n"
			  << "games:     print game list\n"
			  << "status:    print server status\n"
			  << "exit:      exits server (kills all running games!)" << std::endl;
}


void process_arguments(int argc, char** argv)
{
	if (argc > 1)
	{
		for (int i = 1; i < argc; ++i)
		{
			if (strcmp(argv[i], "--no-daemon") == 0 || strcmp(argv[i], "-n") == 0)
			{
				g_run_in_foreground = true;
				continue;
			}
			if (strcmp(argv[i], "--print-msgs") == 0 || strcmp(argv[i], "-p") == 0)
			{
				g_print_syslog_to_stderr = true;
				continue;
			}
			if (strcmp(argv[i], "--config-file") == 0 || strcmp(argv[i], "-c") == 0)
			{
				++i;
				if (i >= argc)
				{
					std::cout << "\"config-file\" option needs an argument" << std::endl;
					printHelp();
					exit(1);
				}
				g_config_file = std::string("server/") + argv[i];
				continue;
			}
			if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
			{
				printHelp();
				exit(3);
			}
			std::cout << "Unknown option \"" << argv[i] << "\"" << std::endl;
			printHelp();
			exit(1);
		}
	}
}

void fork_to_background()
{
	#ifndef WIN32
	pid_t f_return = fork();
	if (f_return == -1)
	{
		perror("fork");
		exit(1);
	}
	if (f_return != 0)
	{
		std::cout << "Running in background as PID " << f_return << std::endl;
		exit(0);
	}
	#else
	std::cerr<<"fork is not available under windows\n";
	#endif
}

void setup_physfs(char* argv0)
{
	FileSystem& fs = FileSystem::getSingleton();

	#ifdef BLOBBY_DATA_DIR
		fs.addToSearchPath(BLOBBY_DATA_DIR);
		fs.addToSearchPath(fs.join(BLOBBY_DATA_DIR, "rules.zip"));
	#endif
	fs.addToSearchPath("data");
	fs.addToSearchPath(fs.join("data", "rules.zip"));
}

std::string statistics()
{
	std::ostringstream oss;
	oss << "Blobby Server Status Report " << (SWLS_RunningTime / UPDATE_FREQUENCY / 60 / 60) << "h running \n";
	oss << " packet count: " << SWLS_PacketCount << "\n";
	oss << " accepted connections: " << SWLS_Connections << "\n";
	oss << " started games: " << SWLS_Games << "\n";
	oss << " game steps: " << SWLS_GameSteps;
	return oss.str();
}

#ifdef WIN32

void syslog(int pri, const char* format, ...)
{
	// first, look where we want to send our message to
	FILE* target = stdout;
	switch(pri)
	{
		case LOG_ERR:
			target = stderr;
			break;
		case LOG_NOTICE:
		case LOG_DEBUG:
			target = stdout;
			break;
	}

	// create a string containing date and time
	std::time_t time_v = std::time(0);
	std::tm* time = localtime(&time_v);
	char buffer[128];
	std::strftime(buffer, sizeof(buffer), "%x - %X", time);

	// print it
	fprintf(target, "%s: ", buffer);

	// now relay the passed arguments and format string to vfprintf for output
	va_list args;
	va_start (args, format);
	vfprintf(target, format, args);
	va_end (args);

	// end finish with a newline
	fprintf(target, "\n");
}
#endif
