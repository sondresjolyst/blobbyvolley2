# Load test bot

`blobby-loadbot` is a headless client for load tests of a tournament server. It joins with a join code like a player, plays every game the server starts with a simple bot and stays connected for the next match. It can also play with one of the bot scripts of the game. It opens no window and plays no sound.

It uses the network and game code of the game itself to read and write its packets. It answers the rules checksum of a new game at once, like the real client.

## Output

Each event is one line of JSON on stdout. Every line has the time `t` in seconds, `code` with the first 3 characters of the join code, `name` with the nickname the server sent (empty until the first status) and `event`:

- `connecting`: the bot starts a connection.
- `connect_failed`: the server could not be reached or had no free slot. The bot tries again after 2 seconds, and waits twice as long after each failure, up to 30 seconds.
- `connected`: the connection is up and the bot sent its join code.
- `refused`: the server refused the join code, with the reason in `reason`. The bot stops with exit code 3.
- `status`: the tournament status the server sent, such as the opponent and the series score.
- `game_offered`: the server starts a game and sent the rules checksum.
- `rules_rejected`: the rules checksum could not be read or its points to win are not valid. The game client ignores such a packet too, so the bot does not answer and no game starts.
- `game_start`: the game runs, with the game speed and the opponent.
- `script_failed`: the bot script could not be started for a game. The bot then plays that game with the simple bot.
- `game_end`: the game is over. `why` is `won_notification` for a game played out, `opponent_disconnected` when the opponent left, `disconnected` when the bot lost its connection and `stopped` when the bot was stopped. `won` is 1 or 0 for a game played out, -1 otherwise. The line also holds the length of the game, the number of state updates, the largest gap between two updates, the round trip of the input and the score. A bot with a script also has `script_max_us`, the longest time in microseconds its script took for one step.
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

### Play with a bot script

    blobby-loadbot <host> <port> <join code> --script <name> [--difficulty <0 to 25>]

`--script` plays with a bot script of the game instead of the simple bot. The name is the file name in `data/scripts` without `.lua`: `reduced`, `axji-0-2`, `com_11`, `gintonicV9`, `hyp014` or `Union`. The bot stops with exit code 2 and lists the scripts when the name is unknown, and with exit code 4 when the script cannot be loaded. The script runs the same way as against a local player, so it plays much better than the simple bot.

`--difficulty` is the bot strength from the options of the game. 0 is the strongest bot and 25 the weakest. It is 13 by default, like the computer player on the right side in a new game. The skill only applies to the simple bot, so it cannot be given together with `--script`.

To play against `hyp014` through a tournament server on your own machine, give the bot the join code of one player and join with the other:

    docker run --rm --name blobby-bot blobby-loadbot host.docker.internal 1234 CODE --script hyp014

The image holds the bot scripts in `/usr/local/share/blobby`. A bot built with CMake also finds them in the folder `data` below the working folder, so run it from the root of the repository.

## Smoke test

`smoke.sh` starts a tournament server without a site, with a players file of two players and one best of 3 match to 3 points. It starts two bots and passes when the results file of the server holds the decided series. Arguments after the two images go to the first bot, so `--script reduced` plays one script bot against the simple bot. It fails as soon as the server or a bot stops, and removes its containers, the data volume and its network when it ends.

    docker build -t blobby-server .
    docker build -f tools/loadbot/Dockerfile -t blobby-loadbot .
    tools/loadbot/smoke.sh blobby-server blobby-loadbot
    tools/loadbot/smoke.sh blobby-server blobby-loadbot --script reduced

The workflow `Load test bot` runs both on every pull request.
