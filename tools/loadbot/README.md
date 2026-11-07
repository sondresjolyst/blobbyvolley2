# Load test bot

`blobby-loadbot` is a headless client for load tests of a tournament server. It joins with a join code like a player, plays every game the server starts with a simple bot and stays connected for the next match. It opens no window and plays no sound.

It uses the network and game code of the game itself: the enter packet, the tournament status, the game state and the input packets. Like the game, it answers the rules checksum of a new game in the same pass that reads the packet, so it is as fast as a real client on a LAN.

Each event is one line of JSON on stdout, with the time `t` in seconds and the join code. The events are `connecting`, `connected`, `refused`, `status`, `game_offered`, `game_start`, `game_end` and `disconnected`. A `game_end` line holds whether the bot won, the length of the game, the number of state updates, the largest gap between two updates, the round trip of the input and the score.

## Build

The bot is off in a normal build. Turn it on with `BUILD_LOADBOT`:

    cmake -B build -DBUILD_LOADBOT=ON
    cmake --build build --target blobby-loadbot

Or build the container image from the root of the repository:

    docker build -f tools/loadbot/Dockerfile -t blobby-loadbot .

## Run

    blobby-loadbot <host> <port> <join code> [skill]

The skill is 0 to 100 and sets how often the bot moves wrong. It is 70 by default. A lost connection is tried again after 2 seconds.

To run one bot per player against a server on the Docker network `lan`, with a file `codes.txt` that has one join code per line:

    n=0
    while read -r code
    do
        n=$((n + 1))
        docker run -d --name loadbot-$n --network lan blobby-loadbot blobby-server 1234 "$code"
    done < codes.txt

The server only starts the games of matches in its players file, or in the sync of the site. Stop the bots with `docker rm -f $(docker ps -aq --filter name=loadbot-)`.

## Smoke test

`smoke.sh` starts a tournament server without a site, with a players file of two players and one best of 3 match to 3 points. It starts two bots and passes when the results file of the server holds the decided series. It removes its containers, the data volume and its network when it ends.

    docker build -t blobby-server .
    docker build -f tools/loadbot/Dockerfile -t blobby-loadbot .
    tools/loadbot/smoke.sh blobby-server blobby-loadbot

The workflow `Load test bot` runs it on every pull request.
