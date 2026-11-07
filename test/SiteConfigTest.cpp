#include <boost/test/unit_test.hpp>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "server/SiteConfig.h"
#include "TestDir.h"

namespace
{
	const char* const TOKEN = "Zx8yQ2v_Lm9pR4tW7kN1bC3dE5fG6hJ0aS-uV8wXyZq";

	EnvironmentFunction envOf(const std::map<std::string, std::string>& values)
	{
		return [values](const char* name) -> const char* {
			auto found = values.find(name);
			return found == values.end() ? nullptr : found->second.c_str();
		};
	}

	bool joined(const std::vector<std::string>& errors, const std::string& part)
	{
		for (const auto& e : errors)
		{
			if (e.find(part) != std::string::npos)
				return true;
		}
		return false;
	}
}

BOOST_AUTO_TEST_SUITE( site_config )

BOOST_AUTO_TEST_CASE( site_url_needs_https )
{
	std::string origin;
	std::string error;
	BOOST_CHECK(checkSiteUrl("https://julalan.no", false, origin, error));
	BOOST_CHECK_EQUAL(origin, "https://julalan.no");
	BOOST_CHECK(checkSiteUrl("HTTPS://Example.ORG:8443/", false, origin, error));
	BOOST_CHECK_EQUAL(origin, "https://example.org:8443");

	BOOST_CHECK(!checkSiteUrl("http://julalan.no", false, origin, error));
	BOOST_CHECK(!checkSiteUrl("http://julalan.no", true, origin, error));
	BOOST_CHECK(!checkSiteUrl("http://localhost:3000", false, origin, error));
	BOOST_CHECK(!checkSiteUrl("ftp://julalan.no", false, origin, error));
	BOOST_CHECK(!checkSiteUrl("julalan.no", false, origin, error));
}

BOOST_AUTO_TEST_CASE( site_url_allows_http_only_for_localhost_with_the_test_flag )
{
	std::string origin;
	std::string error;
	BOOST_CHECK(checkSiteUrl("http://localhost:3000", true, origin, error));
	BOOST_CHECK_EQUAL(origin, "http://localhost:3000");
	BOOST_CHECK(checkSiteUrl("http://127.0.0.1:8080", true, origin, error));
	BOOST_CHECK(checkSiteUrl("http://[::1]:8080", true, origin, error));
	BOOST_CHECK(!checkSiteUrl("http://127.0.0.2:8080", true, origin, error));
	BOOST_CHECK(!checkSiteUrl("http://localhost.evil.org", true, origin, error));
}

BOOST_AUTO_TEST_CASE( site_url_refuses_paths_users_and_odd_characters )
{
	std::string origin;
	std::string error;
	BOOST_CHECK(!checkSiteUrl("https://julalan.no/api", false, origin, error));
	BOOST_CHECK(!checkSiteUrl("https://julalan.no?x=1", false, origin, error));
	BOOST_CHECK(!checkSiteUrl("https://julalan.no#top", false, origin, error));
	BOOST_CHECK(!checkSiteUrl("https://user@julalan.no", false, origin, error));
	BOOST_CHECK(!checkSiteUrl("https://jula lan.no", false, origin, error));
	BOOST_CHECK(!checkSiteUrl("https://julalan.no\r\nX: y", false, origin, error));
	BOOST_CHECK(!checkSiteUrl("https://julalan.no:0", false, origin, error));
	BOOST_CHECK(!checkSiteUrl("https://julalan.no:70000", false, origin, error));
	BOOST_CHECK(!checkSiteUrl("https://", false, origin, error));
	BOOST_CHECK(!checkSiteUrl("https://ju_lalan.no", false, origin, error));
}

BOOST_AUTO_TEST_CASE( token_must_fit_in_a_header )
{
	BOOST_CHECK(isValidSiteToken(TOKEN));
	BOOST_CHECK(!isValidSiteToken(""));
	BOOST_CHECK(!isValidSiteToken("short"));
	BOOST_CHECK(!isValidSiteToken(std::string(TOKEN) + "\r\nX-Evil: 1"));
	BOOST_CHECK(!isValidSiteToken(std::string(TOKEN) + " more"));
	BOOST_CHECK(!isValidSiteToken(std::string(600, 'a')));
}

BOOST_AUTO_TEST_CASE( environment_comes_before_the_config_file )
{
	SiteConfigFile file;
	file.tournamentMode = false;
	file.siteUrl = "https://from-file.example";
	file.siteToken = "file-token-0123456789";
	file.dataDir = "filedata";

	SiteConfig config;
	std::vector<std::string> errors;
	BOOST_REQUIRE(loadSiteConfig(file, envOf({{"BLOBBY_TOURNAMENT", "1"}, {"BLOBBY_SITE_URL", "https://julalan.no"},
		{"BLOBBY_SITE_TOKEN", TOKEN}, {"BLOBBY_DATA_DIR", "/data"}}), config, errors));
	BOOST_CHECK(config.tournamentMode);
	BOOST_CHECK(config.usesSite());
	BOOST_CHECK_EQUAL(config.siteUrl, "https://julalan.no");
	BOOST_CHECK_EQUAL(config.siteToken, TOKEN);
	BOOST_CHECK_EQUAL(config.dataDir, "/data");
	BOOST_CHECK(!config.allowHttpLocalhost);
	BOOST_CHECK(config.caFile.empty());
}

BOOST_AUTO_TEST_CASE( config_file_is_used_without_environment )
{
	SiteConfigFile file;
	file.tournamentMode = true;
	file.siteUrl = "https://julalan.no/";
	file.siteToken = TOKEN;
	file.dataDir = "filedata";

	SiteConfig config;
	std::vector<std::string> errors;
	BOOST_REQUIRE(loadSiteConfig(file, envOf({}), config, errors));
	BOOST_CHECK(config.usesSite());
	BOOST_CHECK_EQUAL(config.siteUrl, "https://julalan.no");
	BOOST_CHECK_EQUAL(config.dataDir, "filedata");
}

BOOST_AUTO_TEST_CASE( tournament_can_be_turned_off_by_environment )
{
	SiteConfigFile file;
	file.tournamentMode = true;
	SiteConfig config;
	std::vector<std::string> errors;
	BOOST_REQUIRE(loadSiteConfig(file, envOf({{"BLOBBY_TOURNAMENT", "0"}}), config, errors));
	BOOST_CHECK(!config.tournamentMode);

	BOOST_CHECK(!loadSiteConfig(file, envOf({{"BLOBBY_TOURNAMENT", "yes"}}), config, errors));
	BOOST_CHECK(joined(errors, "BLOBBY_TOURNAMENT"));
}

BOOST_AUTO_TEST_CASE( casual_server_ignores_the_site )
{
	SiteConfigFile file;
	SiteConfig config;
	std::vector<std::string> errors;
	BOOST_REQUIRE(loadSiteConfig(file, envOf({{"BLOBBY_SITE_URL", "http://bad.example"}}), config, errors));
	BOOST_CHECK(!config.tournamentMode);
	BOOST_CHECK(!config.usesSite());
	BOOST_CHECK(errors.empty());
}

BOOST_AUTO_TEST_CASE( tournament_without_site_uses_the_players_file )
{
	SiteConfigFile file;
	file.tournamentMode = true;
	SiteConfig config;
	std::vector<std::string> errors;
	BOOST_REQUIRE(loadSiteConfig(file, envOf({}), config, errors));
	BOOST_CHECK(config.tournamentMode);
	BOOST_CHECK(!config.usesSite());
}

BOOST_AUTO_TEST_CASE( bad_site_settings_are_errors_without_the_token )
{
	SiteConfigFile file;
	SiteConfig config;
	std::vector<std::string> errors;
	BOOST_CHECK(!loadSiteConfig(file, envOf({{"BLOBBY_TOURNAMENT", "1"}, {"BLOBBY_SITE_URL", "http://julalan.no"},
		{"BLOBBY_SITE_TOKEN", TOKEN}}), config, errors));
	BOOST_CHECK(joined(errors, "https"));
	for (const auto& e : errors)
		BOOST_CHECK(e.find(TOKEN) == std::string::npos);

	errors.clear();
	BOOST_CHECK(!loadSiteConfig(file, envOf({{"BLOBBY_TOURNAMENT", "1"}, {"BLOBBY_SITE_URL", "https://julalan.no"}}), config, errors));
	BOOST_CHECK(joined(errors, "BLOBBY_SITE_TOKEN is not set"));

	errors.clear();
	BOOST_CHECK(!loadSiteConfig(file, envOf({{"BLOBBY_TOURNAMENT", "1"}, {"BLOBBY_SITE_URL", "https://julalan.no"},
		{"BLOBBY_SITE_TOKEN", "bad token with spaces"}}), config, errors));
	BOOST_CHECK(joined(errors, "BLOBBY_SITE_TOKEN must be"));
	for (const auto& e : errors)
		BOOST_CHECK(e.find("bad token") == std::string::npos);
}

BOOST_AUTO_TEST_CASE( test_flags_allow_http_on_localhost_and_a_ca_file )
{
	SiteConfigFile file;
	SiteConfig config;
	std::vector<std::string> errors;
	BOOST_REQUIRE(loadSiteConfig(file, envOf({{"BLOBBY_TOURNAMENT", "1"}, {"BLOBBY_SITE_URL", "http://127.0.0.1:8099"},
		{"BLOBBY_SITE_TOKEN", TOKEN}, {"BLOBBY_SITE_ALLOW_HTTP", "1"}, {"BLOBBY_SITE_CA_FILE", "ca.pem"}}), config, errors));
	BOOST_CHECK(config.allowHttpLocalhost);
	BOOST_CHECK_EQUAL(config.siteUrl, "http://127.0.0.1:8099");
	BOOST_CHECK_EQUAL(config.caFile, "ca.pem");
}

BOOST_AUTO_TEST_CASE( paths_go_below_the_data_dir )
{
	BOOST_CHECK_EQUAL(pathInDataDir("/data", "players.xml"), "/data/players.xml");
	BOOST_CHECK_EQUAL(pathInDataDir("/data/", "players.xml"), "/data/players.xml");
	BOOST_CHECK_EQUAL(pathInDataDir("", "players.xml"), "players.xml");
	BOOST_CHECK_EQUAL(pathInDataDir("/data", "/etc/players.xml"), "/etc/players.xml");
	BOOST_CHECK_EQUAL(pathInDataDir("C:\\data", "D:\\x.xml"), "D:\\x.xml");
	BOOST_CHECK_EQUAL(pathInDataDir("/data", ""), "");
}

BOOST_AUTO_TEST_CASE( atomic_write_replaces_the_whole_file )
{
	const std::string path = testPath("atomic.txt");
	std::string error;
	BOOST_REQUIRE(writeFileAtomically(path, "first version, which is longer", error));
	BOOST_REQUIRE(writeFileAtomically(path, "second", error));

	std::string content;
	BOOST_REQUIRE(readWholeFile(path, 1024, content));
	BOOST_CHECK_EQUAL(content, "second");
	// no temporary file stays behind
	std::string temp;
	BOOST_CHECK(!readWholeFile(path + ".tmp", 1024, temp));
	BOOST_CHECK(!readWholeFile(path, 3, content));
	std::remove(path.c_str());
}

BOOST_AUTO_TEST_CASE( atomic_write_fails_cleanly_in_a_missing_folder )
{
	std::string error;
	BOOST_CHECK(!writeFileAtomically(testPath("no_such_folder/file.txt"), "x", error));
	BOOST_CHECK(!error.empty());
}

BOOST_AUTO_TEST_SUITE_END()
