# LAN tournament and Foza betting design

## Why

The owner runs an annual winter LAN and wants Blobby Volley 2 to be the competitive centre of it. Friends play a customizable bracket, bet fake points called Foza on the matches and follow leaderboards on a fun Grandiosa themed website. Grandiosa is the Norwegian frozen pizza.

Success means the owner can set up a tournament on the day, everyone joins with little help, results reach the site by themselves and the room has fun watching and betting.

## Goals

- Run a tournament with an adjustable number of players, with or without a loser bracket.
- Let players join their match from a link, with the correct name enforced.
- Let everyone bet Foza on matches and see leaderboards.
- Give admins a page to run the event.
- Report match results from the game server to the site with no manual entry in the normal case.

## Non-goals

- Real money, or Foza with any real value.
- Integration with the existing `Foza-backend` and `Foza-frontend` repositories. Only the name is shared.
- Strong account security. This is a friendly event, and an admin can correct mistakes.
- Contact with, or contributions to, the upstream project. This fork stands alone.
- Compatibility between tournament mode and upstream servers or clients.

## Anti-goals

The work fails, even if it technically runs, if any of these happen:

- Normal gameplay changes. Physics, rules and match logic must stay the same, and the golden match test in `test/MatchGoldenTest.cpp` must keep passing with unchanged values.
- A dropped connection between the game server and the site loses a match result.
- Setup is so fiddly that the owner spends the LAN doing support.
- A problem on the site stops people from playing matches that are already running.

## Decisions made by the owner

- The points are called Foza, and the site has a Grandiosa theme.
- The bracket is customizable: number of players, and loser brackets.
- Normal rules are the default. Rules per round are optional.
- Players may bet on their own matches.
- An admin page controls daily Foza and similar settings, behind GitHub login, in the same way as `tumogroup-monitor`.
- Players sign in with a nickname and a PIN, as described below.
- Names are enforced by the server through secret join codes.
- The game server runs on the owner's machine, in a container in Docker Desktop, so that it is close to the players.
- The website and everything except the game server may use the owner's Kubernetes cluster and other infrastructure.
- Code lives in 2 repositories: this game repository and one new public repository for everything else. The new repository holds no secrets.
- One event per LAN. An event holds several tournaments and standalone 1v1 matches, and everyone has one Foza balance for that event.
- Next year's LAN is a new event with new Foza. Past events and their history are kept.
- Expect at most 12 players, possibly far fewer.
- Matches can run one at a time or several at once, chosen by the admin. Upcoming matches are shown in advance, so everyone has time to bet.
- If a player disconnects mid-match, the current game is replayed from 0-0 or an admin awards the win. There is no rejoin feature inside a game. A best-of-N series keeps its score, so a series at 1-1 stays 1-1 until the players are ready again.
- Admins can manage matches: restart a game, set a series score and enter or correct a result by hand. This also covers results the server could not deliver.
- Anyone signed in can create a standalone 1v1 match with another player.
- A forfeit or no-show refunds all bets on that match, so nobody profits from betting against themselves and leaving.
- If the site or internet goes down, assigned matches continue using codes and assignments saved on the server. Results are sent when the connection returns. New sign-ups and bets wait until then.
- Best-of-N and per-round rules are both in the first version. Normal rules stay the default.
- There is no deadline pressure, so features are not cut for time.

## Proposals by Claude, accepted in discussion or not rejected

- Players sign up with a shared event code, shown as a QR code on the big screen, then choose a nickname and a 4 digit PIN. The site remembers the device. An admin can reset a PIN.
- The stack follows `tumogroup-monitor`: Next.js, `next-auth` with GitHub for admins, `pg` with Postgres, Vitest and Playwright.
- Join codes travel in the existing 16 byte player name field, so the message format does not change.

## Claude's calls, for the owner to check

- Betting against yourself is allowed, since betting on your own matches is. Foza has no value.
- Odds are parimutuel: all bets on a match form a pool, and winners share it in proportion to their stake. Upsets therefore pay well. If nobody bet on the winner, stakes are refunded. Payouts round down, and leftovers are discarded.
- Everyone starts the event with the same Foza and gets a daily amount, both set on the admin page, so nobody goes broke and drops out.
- A standalone 1v1 starts only after the other player accepts it. It can be bet on like any other match, and an admin can cancel it.
- A match opens for betting when it is listed as upcoming. An admin calls it when the players should join, and bets close when it starts.
- Each game in a best-of-N is reported by the server. Bets settle on the winner of the whole match, not on single games.
- A replay after a disconnect keeps existing bets open, since the match has no result yet. Bets stay closed during the replay.
- Bracket generation supports up to 32 players, though about 12 is the expected size.
- The first version has no local copy of the site. Losing the site pauses betting and sign-up only.
- Bets close when a match starts. A cancelled match refunds its bets. Correcting a result reverses and recalculates its payouts.
- The site is reachable from the internet, and the event code keeps outsiders out.
- The game server only makes outbound calls to the site. The site never needs to reach the owner's machine, so no inbound firewall rule is needed for this link.
- Tournament mode is a server setting, off by default. A separate server instance can run casual games.

## How it works

### One match

1. The admin creates a tournament in the current event, and the site generates the bracket.
2. Upcoming matches appear on the site and open for betting. The admin calls a match when it should be played, either one at a time or several together.
3. Each of the 2 players selects "Join match". The site gives them a link of the form `blobby://join?server=<host>:<port>&code=<code>`.
4. The link opens the game, which connects directly to the server and sends the code in the name field.
5. The server looks up the code, replaces it with the real nickname and refuses unknown codes.
6. When both players have connected, the server starts their match with the settings the site gave it: rule set, points and speed. Bets close at this point.
7. The server reports each game result. When the match is decided, the site pays out Foza, updates the bracket and leaderboards and lists the next matches.

### When things go wrong

- **Disconnect:** the server reports that the game ended without a winner. The series score stays as it was. An admin restarts the game when both players are ready, or awards the win.
- **Forfeit or no-show:** an admin records it, the bracket moves on and all bets on that match are refunded.
- **Site or internet down:** the server keeps playing assigned matches from its saved data and delivers results later. A match that started during an outage has its bets closed as of its reported start time. If the server cannot deliver a result, an admin enters it by hand, and a late report of the same game must not change or double count it.
- **Server restart:** a game in progress is treated like a disconnect.

### Game repository

- **Link handling:** the game accepts a join link at startup, then goes straight to the server without the search screen. Windows must register the link type.
- **Tournament mode on the server:** fetches codes and match assignments from the site and saves them to disk, swaps codes for nicknames, refuses unknown codes, starts assigned matches, runs several matches at once when assigned and reports results, including games that end without a winner.
- **Result delivery:** results are stored on disk and retried until the site confirms them, and the site treats a repeated report as the same result.
- **Authentication to the site:** the server uses a shared secret from its configuration, never stored in the repository.
- **Container:** a Linux image, which also runs in Docker Desktop on Windows, with `blobby-server` and its data, published through CI, using UDP port 1234. Running `blobby-server.exe` directly must remain a working fallback.
- **Distribution:** a downloadable Windows build that includes its DLLs, produced by CI, because players must have this fork's build for links to work.
- **Hardening:** the open input validation work must be merged before the LAN.

`src/server/MatchMaker.cpp` already has `startGame` and `makeMatch`, which assignment can build on. Lobby behaviour must not change when tournament mode is off.

### Site repository

- **Player pages:** sign-up, bracket, live score, betting, leaderboards, creating and accepting 1v1 matches and the join link.
- **Admin pages:** event and tournament setup, calling matches, match management (restart a game, set a series score, enter or correct a result, cancel), Foza settings, player and PIN management, no-show handling and replacing a player.
- **History:** past events stay readable, with their brackets, results and leaderboards.
- **Big-screen page:** bracket, current score, odds and next match, for a TV in the room.
- **Game server API:** assignments, codes and results, protected by the shared secret.
- **Formats:** single and double elimination and standalone 1v1 in the first version, with best-of-N and optional rules per round. The bracket logic should allow more formats later.

### Constraints

- Nicknames are limited to 15 bytes because of the name field. Norwegian letters use 2 bytes, so the site counts bytes.
- Join codes also fit in 15 bytes, must be hard to guess and are unique per player.
- PINs are stored hashed, with a limit on failed attempts.
- Docker Desktop must carry UDP correctly with real players. Test this early with `-p 1234:1234/udp` and a Windows firewall rule.
- The owner's machine needs a stable LAN address, so links keep working.
- This repository is public. Issues and PRs must not describe unmerged defects.
- Commits and PR titles use Conventional Commits, and work goes through branches and PRs.

## Later versions

Round robin, groups into a bracket, Swiss, rule set bans, side bets such as tournament winner, awards like "worst bet of the night", saved replays of the final and a hall of fame across years.

## Left to the builder

- Database schema and API shape.
- How the site shows live scores, including how often the server sends them.
- Exact visual design, within the Grandiosa theme. Mockups should be shown to the owner first.
- How best-of-N is modelled, within the rules above.
