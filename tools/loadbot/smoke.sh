#!/usr/bin/env bash
# Plays one short best of 3 match between two load test bots on a tournament server without a site.
# Usage: tools/loadbot/smoke.sh <server image> <bot image>
# It passes when the results file of the server holds the decided series. It removes what it starts.
set -euo pipefail

SERVER_IMAGE=${1:?server image}
BOT_IMAGE=${2:?bot image}
TIMEOUT=${SMOKE_TIMEOUT:-300}
PREFIX=blobby-smoke-$$
DATA=$(mktemp -d)

cleanup() {
	docker rm -f "$PREFIX-server" "$PREFIX-bot1" "$PREFIX-bot2" >/dev/null 2>&1 || true
	docker network rm "$PREFIX" >/dev/null 2>&1 || true
	rm -rf "$DATA"
}
trap cleanup EXIT

# Git Bash on Windows needs a Windows path for the volume
MOUNT=$DATA
if command -v cygpath >/dev/null 2>&1; then
	MOUNT=$(cygpath -m "$DATA")
	export MSYS_NO_PATHCONV=1
fi

cat > "$DATA/tournament_players.xml" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<tournament>
	<player id="1" nickname="Bot1" code="SMKAAAAAA2"/>
	<player id="2" nickname="Bot2" code="SMKBBBBBB3"/>
	<match id="1" player1="1" player2="2" scoreToWin="3" bestOf="3" nextGameDelay="3"/>
</tournament>
XML
# the server runs as uid 999 and writes its results next to the players file
chmod 777 "$DATA"

docker network create "$PREFIX" >/dev/null
docker run -d --name "$PREFIX-server" --network "$PREFIX" -v "$MOUNT:/data" \
	-e BLOBBY_TOURNAMENT=1 -e BLOBBY_DATA_DIR=/data "$SERVER_IMAGE" >/dev/null
sleep 2
docker run -d --name "$PREFIX-bot1" --network "$PREFIX" "$BOT_IMAGE" "$PREFIX-server" 1234 SMKAAAAAA2 >/dev/null
docker run -d --name "$PREFIX-bot2" --network "$PREFIX" "$BOT_IMAGE" "$PREFIX-server" 1234 SMKBBBBBB3 >/dev/null

results=$DATA/tournament_results.jsonl
for _ in $(seq 1 "$TIMEOUT"); do
	if [ -f "$results" ] && grep -q '"event":"series"' "$results"; then
		cat "$results"
		games=$(grep -c '"event":"game"' "$results")
		if grep -q '"status":"abandoned"' "$results"; then
			echo "smoke failed: a game was abandoned"
			exit 1
		fi
		echo "smoke passed: the series was decided after $games games"
		exit 0
	fi
	sleep 1
done

echo "smoke failed: no decided series after $TIMEOUT seconds"
docker logs "$PREFIX-server" 2>&1 | tail -n 40
docker logs "$PREFIX-bot1" 2>&1 | tail -n 10
docker logs "$PREFIX-bot2" 2>&1 | tail -n 10
exit 1
