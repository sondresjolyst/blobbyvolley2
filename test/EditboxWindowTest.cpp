#include <boost/test/unit_test.hpp>

#include <string>

#include "EditboxWindow.h"

BOOST_AUTO_TEST_SUITE( editbox_window )

BOOST_AUTO_TEST_CASE( short_text_is_shown_whole )
{
	const std::string text = "blobby";
	for (std::size_t cursor = 0; cursor <= text.size(); ++cursor)
	{
		EditboxWindow window = editboxWindow(text, cursor, 15);
		BOOST_CHECK_EQUAL(window.first, 0u);
		BOOST_CHECK_EQUAL(window.count, text.size());
		BOOST_CHECK_EQUAL(window.cursor, cursor);
	}
}

BOOST_AUTO_TEST_CASE( text_as_long_as_the_box_is_shown_whole )
{
	const std::string text = "0123456789";
	EditboxWindow window = editboxWindow(text, text.size(), 10);
	BOOST_CHECK_EQUAL(window.first, 0u);
	BOOST_CHECK_EQUAL(window.count, 10u);
	BOOST_CHECK_EQUAL(window.cursor, 10u);
}

BOOST_AUTO_TEST_CASE( long_text_with_the_cursor_at_the_start_shows_the_start )
{
	const std::string text = "abcdefghijklmnopqrstuvwxyz";
	EditboxWindow window = editboxWindow(text, 0, 10);
	BOOST_CHECK_EQUAL(window.first, 0u);
	BOOST_CHECK_EQUAL(window.count, 10u);
	BOOST_CHECK_EQUAL(text.substr(window.first, window.count), "abcdefghij");
}

BOOST_AUTO_TEST_CASE( long_text_with_the_cursor_inside_the_first_box_shows_the_start )
{
	const std::string text = "abcdefghijklmnopqrstuvwxyz";
	EditboxWindow window = editboxWindow(text, 10, 10);
	BOOST_CHECK_EQUAL(window.first, 0u);
	BOOST_CHECK_EQUAL(window.count, 10u);
}

BOOST_AUTO_TEST_CASE( long_text_with_the_cursor_in_the_middle_ends_at_the_cursor )
{
	const std::string text = "abcdefghijklmnopqrstuvwxyz";
	EditboxWindow window = editboxWindow(text, 15, 10);
	BOOST_CHECK_EQUAL(window.first, 5u);
	BOOST_CHECK_EQUAL(window.count, 10u);
	BOOST_CHECK_EQUAL(text.substr(window.first, window.count), "fghijklmno");
	BOOST_CHECK_EQUAL(window.cursor - window.first, 10u);
}

BOOST_AUTO_TEST_CASE( long_text_with_the_cursor_at_the_end_shows_the_end )
{
	const std::string text = "abcdefghijklmnopqrstuvwxyz";
	EditboxWindow window = editboxWindow(text, text.size(), 10);
	BOOST_CHECK_EQUAL(window.first, 16u);
	BOOST_CHECK_EQUAL(window.count, 10u);
	BOOST_CHECK_EQUAL(text.substr(window.first, window.count), "qrstuvwxyz");
}

BOOST_AUTO_TEST_CASE( cursor_behind_the_text_moves_to_the_end )
{
	const std::string text = "abcdefghijklmnopqrstuvwxyz";
	EditboxWindow window = editboxWindow(text, 100, 10);
	BOOST_CHECK_EQUAL(window.cursor, text.size());
	BOOST_CHECK_EQUAL(window.first, 16u);

	window = editboxWindow("", 3, 10);
	BOOST_CHECK_EQUAL(window.cursor, 0u);
	BOOST_CHECK_EQUAL(window.first, 0u);
	BOOST_CHECK_EQUAL(window.count, 0u);
}

BOOST_AUTO_TEST_CASE( window_does_not_start_inside_a_utf8_sequence )
{
	// "\xc3\xb8" is o with a stroke, two bytes
	const std::string text = "ab\xc3\xb8" "cdefghijkl";
	EditboxWindow window = editboxWindow(text, text.size(), 10);
	// cursor 14 minus 10 is 4, the first byte of a character, so nothing moves
	BOOST_CHECK_EQUAL(window.first, 4u);

	window = editboxWindow(text, 13, 10);
	// 13 minus 10 is 3, the second byte of the sequence
	BOOST_CHECK_EQUAL(window.first, 4u);
	BOOST_CHECK_EQUAL(window.count, 10u);
	BOOST_CHECK_EQUAL(window.cursor, 13u);
}

BOOST_AUTO_TEST_CASE( window_does_not_end_inside_a_utf8_sequence )
{
	const std::string text = "abcdefghi\xc3\xb8xyz";
	EditboxWindow window = editboxWindow(text, 0, 10);
	// the tenth byte is the lead byte of the sequence, so the window stops before it
	BOOST_CHECK_EQUAL(window.first, 0u);
	BOOST_CHECK_EQUAL(window.count, 9u);
}

BOOST_AUTO_TEST_CASE( takes_input_up_to_the_box_length_by_default )
{
	BOOST_CHECK(editboxTakesInput(0, 15, 0));
	BOOST_CHECK(editboxTakesInput(14, 15, 0));
	BOOST_CHECK(!editboxTakesInput(15, 15, 0));
	BOOST_CHECK(!editboxTakesInput(20, 15, 0));
}

BOOST_AUTO_TEST_CASE( takes_input_up_to_max_length_when_given )
{
	BOOST_CHECK(editboxTakesInput(62, 62, 301));
	BOOST_CHECK(editboxTakesInput(300, 62, 301));
	BOOST_CHECK(!editboxTakesInput(301, 62, 301));
	// a max length below the box length also holds
	BOOST_CHECK(!editboxTakesInput(5, 10, 5));
}

BOOST_AUTO_TEST_SUITE_END()
