/*=============================================================================
Blobby Volley 2
Copyright (C) 2026 Sjølyst Innovation AS

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

#pragma once

// Limits of a tournament match. Light enough for code that does not need the network messages.

/// Limits of the match settings. They are the limits of the tournament site.
constexpr int MIN_MATCH_SCORE_TO_WIN = 1;
constexpr int MAX_MATCH_SCORE_TO_WIN = 99;
constexpr int MIN_MATCH_SPEED = 30;
constexpr int MAX_MATCH_SPEED = 120;
constexpr int MAX_MATCH_BEST_OF = 99;
constexpr int MAX_MATCH_WINS = 99;
/// Pause between two games of a series, in seconds.
constexpr int MIN_NEXT_GAME_DELAY = 3;
constexpr int MAX_NEXT_GAME_DELAY = 120;
constexpr int DEFAULT_NEXT_GAME_DELAY = 10;

/// Strength of the server bot of a training match. 0 is the strongest bot and 25 the weakest, as in the options of the game.
constexpr int MIN_BOT_DIFFICULTY = 0;
constexpr int MAX_BOT_DIFFICULTY = 25;
constexpr int DEFAULT_BOT_DIFFICULTY = 13;

/// Longest wait for the next game that a client accepts from a server, the longest next game delay.
constexpr unsigned MAX_NEXT_GAME_DELAY_MS = MAX_NEXT_GAME_DELAY * 1000;

/// True if a score to win, from a file or from a server, is in the range a match can have.
bool isValidScoreToWin(int scoreToWin);
