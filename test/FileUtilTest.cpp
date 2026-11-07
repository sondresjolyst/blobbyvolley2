#include <boost/test/unit_test.hpp>

#include <string>

#include "server/FileUtil.h"
#include "TestDir.h"

BOOST_AUTO_TEST_SUITE( file_util )

BOOST_AUTO_TEST_CASE( append_line_makes_the_file_and_adds_lines )
{
	const std::string path = testPath("append.txt");
	BOOST_REQUIRE(appendLine(path, "first"));
	BOOST_REQUIRE(appendLine(path, "second"));

	std::string content;
	BOOST_REQUIRE(readWholeFile(path, 1000, content));
	BOOST_CHECK_EQUAL(content, "first\nsecond\n");
}

BOOST_AUTO_TEST_CASE( append_line_fails_without_the_folder )
{
	BOOST_CHECK(!appendLine(testPath("no_such_folder/append.txt"), "line"));
}

BOOST_AUTO_TEST_CASE( read_whole_file_reports_why_it_failed )
{
	const std::string path = testPath("read.txt");
	std::string error;
	BOOST_REQUIRE(writeFileAtomically(path, "12345", error));

	std::string content = "old";
	ReadFileError why = ReadFileError::CANNOT_READ;
	BOOST_CHECK(readWholeFile(path, 5, content, &why));
	BOOST_CHECK(why == ReadFileError::NONE);
	BOOST_CHECK_EQUAL(content, "12345");

	content = "old";
	BOOST_CHECK(!readWholeFile(path, 4, content, &why));
	BOOST_CHECK(why == ReadFileError::TOO_LARGE);
	BOOST_CHECK_EQUAL(content, "old");

	BOOST_CHECK(!readWholeFile(testPath("missing.txt"), 100, content, &why));
	BOOST_CHECK(why == ReadFileError::CANNOT_OPEN);
	BOOST_CHECK_EQUAL(content, "old");

	// the reason is optional
	BOOST_CHECK(!readWholeFile(testPath("missing.txt"), 100, content));
}

BOOST_AUTO_TEST_SUITE_END()
