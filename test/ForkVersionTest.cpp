#include <fstream>
#include <string>
#include <boost/test/unit_test.hpp>

#include "Global.h"

BOOST_AUTO_TEST_SUITE(ForkVersionTest)

BOOST_AUTO_TEST_CASE(fork_version_matches_version_file)
{
	std::ifstream file(BLOBBY_TEST_VERSION_FILE);
	BOOST_REQUIRE(file.good());
	std::string version;
	std::getline(file, version);
	if (!version.empty() && version.back() == '\r')
		version.pop_back();
	BOOST_CHECK_EQUAL(version, BLOBBY_FORK_VERSION);
}

BOOST_AUTO_TEST_CASE(title_shows_fork_name_and_version)
{
	BOOST_CHECK_EQUAL(std::string(AppTitle), std::string("Blobby Volley 2 Tournament ") + BLOBBY_FORK_VERSION);
}

BOOST_AUTO_TEST_SUITE_END()
