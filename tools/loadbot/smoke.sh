#!/usr/bin/env bash
# Plays one short best of 3 match between two load test bots on a tournament server without a site.
# Usage: tools/loadbot/smoke.sh <server image> <bot image> [arguments of the first bot]
# The arguments after the images go to the first bot, so it can play with a bot script of the game:
#   tools/loadbot/smoke.sh blobby-server blobby-loadbot --script reduced
# It passes when the results file of the server holds the decided series. It removes what it starts.
set -euo pipefail

SERVER_IMAGE=${1:?server image}
BOT_IMAGE=${2:?bot image}
shift 2
BOT1_ARGS=("$@")
TIMEOUT=${SMOKE_TIMEOUT:-300}
PREFIX=blobby-smoke-$$
PLAYERS=$(mktemp)

cleanup() {
	# -v also removes the data volume of the server
	docker rm -f -v "$PREFIX-server" "$PREFIX-bot1" "$PREFIX-bot2" >/dev/null 2>&1 || true
	docker network rm "$PREFIX" >/dev/null 2>&1 || true
	rm -f "$PLAYERS"
}
trap cleanup EXIT

# keeps Git Bash on Windows from changing the paths in the docker commands
export MSYS_NO_PATHCONV=1
SOURCE=$PLAYERS
if command -v cygpath >/dev/null 2>&1
then
	SOURCE=$(cygpath -w "$PLAYERS")
fi

cat > "$PLAYERS" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<tournament>
	<player id="1" nickname="Bot1" code="SMKAAAAAA2"/>
	<player id="2" nickname="Bot2" code="SMKBBBBBB3"/>
	<match id="1" player1="1" player2="2" scoreToWin="3" bestOf="3" nextGameDelay="3"/>
</tournament>
XML
# docker cp keeps the owner of the file, and the server reads it as another user
chmod 644 "$PLAYERS"

docker network create "$PREFIX" >/dev/null
# the players file goes into the data volume of the server, where it also writes its results
docker create --name "$PREFIX-server" --network "$PREFIX" \
	-e BLOBBY_TOURNAMENT=1 -e BLOBBY_DATA_DIR=/data "$SERVER_IMAGE" >/dev/null
docker cp "$SOURCE" "$PREFIX-server:/data/tournament_players.xml" >/dev/null
docker start "$PREFIX-server" >/dev/null
sleep 2
docker run -d --name "$PREFIX-bot1" --network "$PREFIX" "$BOT_IMAGE" "$PREFIX-server" 1234 SMKAAAAAA2 ${BOT1_ARGS[@]+"${BOT1_ARGS[@]}"} >/dev/null
docker run -d --name "$PREFIX-bot2" --network "$PREFIX" "$BOT_IMAGE" "$PREFIX-server" 1234 SMKBBBBBB3 >/dev/null

running() {
	[ "$(docker inspect -f '{{.State.Running}}' "$1" 2>/dev/null)" = true ]
}

for _ in $(seq 1 "$TIMEOUT"); do
	for container in "$PREFIX-server" "$PREFIX-bot1" "$PREFIX-bot2"; do
		if ! running "$container"; then
			echo "smoke failed: $container stopped"
			docker logs "$container" 2>&1 | tail -n 40
			exit 1
		fi
	done
	results=$(docker exec "$PREFIX-server" cat /data/tournament_results.jsonl 2>/dev/null || true)
	if grep -q '"event":"series"' <<< "$results"; then
		echo "$results"
		if grep -q '"status":"abandoned"' <<< "$results"; then
			echo "smoke failed: a game was abandoned"
			exit 1
		fi
		if [[ " ${BOT1_ARGS[*]-} " == *" --script"* ]]; then
			# a bot with a script shows in each game how long its script took
			ends=$(docker logs "$PREFIX-bot1" 2>/dev/null | grep '"event":"game_end"' || true)
			echo "$ends"
			if ! grep -q '"script_max_us"' <<< "$ends"; then
				echo "smoke failed: the first bot did not play with its script"
				exit 1
			fi
		fi
		echo "smoke passed: the series was decided after $(grep -c '"event":"game"' <<< "$results") games"
		exit 0
	fi
	sleep 1
done

echo "smoke failed: no decided series after $TIMEOUT seconds"
docker logs "$PREFIX-server" 2>&1 | tail -n 40
docker logs "$PREFIX-bot1" 2>&1 | tail -n 10
docker logs "$PREFIX-bot2" 2>&1 | tail -n 10
exit 1
