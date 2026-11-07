# Load test bot

`blobby-loadbot` is a headless client for load tests of a tournament server. It joins with a join code like a player, plays every game the server starts with a simple bot and stays connected for the next match. It opens no window and plays no sound.

It uses the network and game code of the game itself to read and write its packets. It answers the rules checksum of a new game at once, like the real client.

## Output

Each event is one line of JSON on stdout. Every line has the time `t` in seconds, `code` with the first 3 characters of the join code, `name` with the nickname the server sent (empty until the first status) and `event`:

- `connecting`: the bot starts a connection.
- `connect_failed`: the server could not be reached or had no free slot. The bot tries again after 2 seconds, and waits twice as long after each failure, up to 30 seconds.
- `connected`: the connection is up and the bot sent its join code.
- `refused`: the server refused the join code, with the reason in `reason`. The bot stops with exit code 3.
- `status`: the tournament status the server sent, such as the opponent and the series score.
- `game_offered`: the server starts a game and sent the rules checksum.
- `game_start`: the game runs, with the game speed and the opponent.
- `game_end`: the game is over. `why` is `won_notification` for a game played out, `opponent_disconnected` when the opponent left, `disconnected` when the bot lost its connection and `stopped` when the bot was stopped. `won` is 1 or 0 for a game played out, -1 otherwise. The line also holds the length of the game, the number of state updates, the largest gap between two updates, the round trip of the input and the score.
- `disconnected`: the connection was lost. The bot connects again after 2 seconds.
- `stopped`: the bot got SIGTERM or SIGINT and ends with exit code 0, so `docker stop` returns at once.

## Build

The bot is off in a normal build. Turn it on with `BUILD_LOADBOT`:

    cmake -B build -DBUILD_LOADBOT=ON
    cmake --build build --target blobby-loadbot

Or build the container image from the root of the repository:

    docker build -f tools/loadbot/Dockerfile -t blobby-loadbot .

## Run

    blobby-loadbot <host> <port> <join code> [skill]

The skill is 0 to 100 and sets how often the bot moves wrong. Values outside are clamped. It is 70 by default.

To run one bot per player against a server on the Docker network `lan`, with a file `codes.txt` that has one join code per line:

    n=0
    while read -r code
    do
        n=$((n + 1))
        docker run -d --name loadbot-$n --network lan blobby-loadbot blobby-server 1234 "$code"
    done < codes.txt

The server only starts the games of matches in its players file, or in the sync of the site. Stop the bots with `docker rm -f $(docker ps -aq --filter name=loadbot-)`.

## Smoke test

`smoke.sh` starts a tournament server without a site, with a players file of two players and one best of 3 match to 3 points. It starts two bots and passes when the results file of the server holds the decided series. It fails as soon as the server or a bot stops, and removes its containers, the data volume and its network when it ends.

    docker build -t blobby-server .
    docker build -f tools/loadbot/Dockerfile -t blobby-loadbot .
    tools/loadbot/smoke.sh blobby-server blobby-loadbot

The workflow `Load test bot` runs it on every pull request.
