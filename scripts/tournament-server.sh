#!/usr/bin/env bash
# Starts and manages the Blobby Volley tournament server in Docker.
#
# Commands: start, stop, restart, logs, status, update, configure, set-token.
# The first start asks for the address of the tournament site, the game server
# token from the admin page and the port. They are saved in a file that only
# the current user can read. The token is passed to Docker in a temporary env
# file, never on the command line.

set -u

# The names match the command on the admin page of the site.
# The BLOBBY_SCRIPT_* variables exist only for testing the script.
# BLOBBY_SCRIPT_IMAGE can also pick another tag, such as sondresjo/blobby-server:dev.
CONTAINER="${BLOBBY_SCRIPT_CONTAINER:-blobby-server}"
VOLUME="${BLOBBY_SCRIPT_VOLUME:-blobby-data}"
IMAGE="${BLOBBY_SCRIPT_IMAGE:-sondresjo/blobby-server:latest}"
DEFAULT_SITE="https://julalan.no"
DEFAULT_PORT=1234
CONFIG_FILE="${BLOBBY_SCRIPT_CONFIG:-${XDG_CONFIG_HOME:-$HOME/.config}/blobby-server/tournament-server.conf}"

SITE=""
TOKEN=""
PORT=""

fail() {
	echo "$1" >&2
	exit 1
}

check_docker() {
	command -v docker >/dev/null 2>&1 ||
		fail "Docker is not installed. Install Docker from https://docs.docker.com/engine/install/ and try again."
	docker info >/dev/null 2>&1 ||
		fail "Docker is not running, or this user may not use it. Start Docker, or add the user to the docker group, and try again."
}

# The checks match what the server accepts.
valid_site() {
	[[ "$1" =~ ^https://[A-Za-z0-9.-]+(:[0-9]+)?$ ]]
}

# 16 to 512 printable characters without spaces.
valid_token() {
	local LC_ALL=C
	((${#1} >= 16 && ${#1} <= 512)) && [[ "$1" =~ ^[!-~]+$ ]]
}

# Prints the port without leading zeros, or fails.
normal_port() {
	[[ "$1" =~ ^[0-9]{1,5}$ ]] && ((10#$1 >= 1 && 10#$1 <= 65535)) || return 1
	echo "$((10#$1))"
}

# Reads the saved settings. A value that is not valid is left empty, so it is asked for again.
read_config() {
	SITE=""
	TOKEN=""
	PORT=""
	[ -f "$CONFIG_FILE" ] || return 0
	local line
	while IFS= read -r line || [ -n "$line" ]; do
		line="${line%[[:cntrl:]]}"
		case "$line" in
		SITE_URL=*) SITE="${line#SITE_URL=}" ;;
		SITE_TOKEN=*) TOKEN="${line#SITE_TOKEN=}" ;;
		PORT=*) PORT="${line#PORT=}" ;;
		esac
	done <"$CONFIG_FILE"
	if [ -n "$SITE" ] && ! valid_site "$SITE"; then
		echo "The saved site address in $CONFIG_FILE is not valid." >&2
		SITE=""
	fi
	if [ -n "$TOKEN" ] && ! valid_token "$TOKEN"; then
		echo "The saved token in $CONFIG_FILE is not valid." >&2
		TOKEN=""
	fi
	if [ -n "$PORT" ] && ! PORT="$(normal_port "$PORT")"; then
		echo "The saved port in $CONFIG_FILE is not valid." >&2
		PORT=""
	fi
}

save_config() {
	mkdir -p "$(dirname "$CONFIG_FILE")" || fail "Could not make the folder for $CONFIG_FILE"
	(
		umask 077
		: >"$CONFIG_FILE"
		chmod 600 "$CONFIG_FILE"
		printf 'SITE_URL=%s\nSITE_TOKEN=%s\nPORT=%s\n' "$SITE" "$TOKEN" "$PORT" >"$CONFIG_FILE"
	) || fail "Could not save $CONFIG_FILE"
}

# Asks for the site. The argument is the value that Enter keeps.
ask_site() {
	local answer default="${1:-$DEFAULT_SITE}"
	while true; do
		read -r -p "Address of the tournament site [$default]: " answer || fail "No answer."
		answer="${answer:-$default}"
		answer="${answer%/}"
		if valid_site "$answer"; then
			SITE="$answer"
			return
		fi
		echo "The address must start with https:// and hold only the host, for example https://julalan.no or https://dev.julalan.no."
	done
}

ask_token() {
	local answer
	while true; do
		read -r -s -p "Game server token from the admin page (hidden): " answer || fail "No answer."
		echo
		if valid_token "$answer"; then
			TOKEN="$answer"
			return
		fi
		echo "That is not a game server token. Copy the whole token from the admin page of the site."
	done
}

# Asks for the port. The argument is the value that Enter keeps.
ask_port() {
	local answer default="${1:-$DEFAULT_PORT}"
	while true; do
		read -r -p "UDP port for the players [$default]: " answer || fail "No answer."
		answer="${answer:-$default}"
		if PORT="$(normal_port "$answer")"; then
			return
		fi
		echo "The port must be a number from 1 to 65535."
	done
}

load_config() {
	read_config
	local changed=0
	[ -n "$SITE" ] || { ask_site; changed=1; }
	[ -n "$TOKEN" ] || { ask_token; changed=1; }
	[ -n "$PORT" ] || { ask_port; changed=1; }
	if [ "$changed" = 1 ]; then
		save_config
		echo "Saved the settings in $CONFIG_FILE"
	fi
}

# Prints running, stopped or missing.
container_state() {
	local running
	if ! running="$(docker container inspect --format '{{.State.Running}}' "$CONTAINER" 2>/dev/null)"; then
		echo missing
	elif [ "$running" = true ]; then
		echo running
	else
		echo stopped
	fi
}

pull_image() {
	docker pull "$IMAGE" || fail "Docker could not download the server image. See the message above and check the internet connection."
}

pull_if_missing() {
	docker image inspect "$IMAGE" >/dev/null 2>&1 || pull_image
}

# An older image always listens on 1234, so another port needs the newest image.
pull_for_port() {
	if [ "$PORT" != "$DEFAULT_PORT" ]; then
		echo "Port $PORT is not the default $DEFAULT_PORT. It needs a server image from this release or later, so the script downloads the image first."
		pull_image
	else
		pull_if_missing
	fi
}

# The optional argument is a hint that is shown when Docker cannot start the server.
create_container() {
	local env_file code hint="${1:-}"
	env_file="$(umask 077 && mktemp "${TMPDIR:-/tmp}/blobby-server.XXXXXXXX")" || fail "Could not make a temporary file."
	trap 'rm -f "$env_file"' EXIT
	trap 'exit 130' INT TERM
	chmod 600 "$env_file"
	printf 'BLOBBY_TOURNAMENT=1\nBLOBBY_SITE_URL=%s\nBLOBBY_SITE_TOKEN=%s\nBLOBBY_PORT=%s\n' "$SITE" "$TOKEN" "$PORT" >"$env_file"
	# the server listens on the port of the host, because the game moves to the port the server reports
	docker run -d --name "$CONTAINER" --restart unless-stopped -p "$PORT:$PORT/udp" -v "$VOLUME:/data" \
		--env-file "$env_file" "$IMAGE" >/dev/null
	code=$?
	rm -f "$env_file"
	trap - EXIT INT TERM
	if [ "$code" != 0 ]; then
		[ -z "$hint" ] || echo "$hint" >&2
		fail "Docker could not start the server. See the message above."
	fi
	echo "The tournament server runs on UDP port $PORT and reports to $SITE."
	echo "Players on other machines need inbound UDP on port $PORT through the firewall of this machine."
}

remove_container() {
	if [ "$(container_state)" != missing ]; then
		docker rm -f "$CONTAINER" >/dev/null || fail "Docker could not remove the old server container."
	fi
}

cmd_start() {
	check_docker
	load_config
	case "$(container_state)" in
	running) echo "The tournament server is already running." ;;
	stopped)
		docker start "$CONTAINER" >/dev/null || fail "Docker could not start the server. See the message above."
		echo "Started the tournament server again."
		;;
	*)
		pull_for_port
		create_container
		;;
	esac
}

cmd_stop() {
	check_docker
	if [ "$(container_state)" = running ]; then
		docker stop "$CONTAINER" >/dev/null
		echo "Stopped the tournament server."
	else
		echo "The tournament server is not running."
	fi
}

cmd_restart() {
	check_docker
	if [ "$(container_state)" = missing ]; then
		cmd_start
	else
		docker restart "$CONTAINER" >/dev/null || fail "Docker could not restart the server."
		echo "Restarted the tournament server."
	fi
}

cmd_logs() {
	check_docker
	[ "$(container_state)" != missing ] || fail "The tournament server has not been started yet."
	docker logs -f --tail 200 "$CONTAINER"
}

cmd_status() {
	read_config
	if [ -n "$SITE" ]; then
		echo "Site:  $SITE"
		echo "Port:  $PORT"
		if [ -n "$TOKEN" ]; then echo "Token: saved"; else echo "Token: missing"; fi
	else
		echo "No settings saved yet. Run start first."
	fi
	check_docker
	case "$(container_state)" in
	running) echo "Server: running" ;;
	stopped) echo "Server: stopped" ;;
	*) echo "Server: not created" ;;
	esac
}

RETRY_HINT="The old server container is removed. Fix the problem and run start again. Reports that wait stay in the data volume."

# Makes the container again with the saved settings, for example after they changed.
recreate_container() {
	pull_for_port
	remove_container
	create_container "$RETRY_HINT"
}

cmd_update() {
	check_docker
	load_config
	# The old server keeps running if the download fails.
	pull_image
	remove_container
	create_container "$RETRY_HINT"
}

cmd_configure() {
	check_docker
	read_config
	ask_site "$SITE"
	ask_port "$PORT"
	[ -n "$TOKEN" ] || ask_token
	save_config
	echo "Saved the settings in $CONFIG_FILE"
	if [ "$(container_state)" = running ]; then
		recreate_container
	else
		echo "The next start uses them."
	fi
}

cmd_set_token() {
	check_docker
	read_config
	[ -n "$SITE" ] || ask_site
	[ -n "$PORT" ] || ask_port
	ask_token
	save_config
	echo "Saved the new token in $CONFIG_FILE"
	recreate_container
}

usage() {
	cat <<EOF
Usage: $0 <command>

  start      Start the tournament server. Asks for the site and token the first time.
  stop       Stop the server. Reports that wait stay in the data volume.
  restart    Restart the server.
  logs       Follow the server log. Press Ctrl+C to stop following.
  status     Show whether the server runs and which site it uses.
  update     Download the newest server image and start the server on it.
  configure  Change the address of the site and the port.
  set-token  Replace the game server token and start the server with it.

Settings are kept in $CONFIG_FILE
EOF
}

case "${1:-help}" in
start) cmd_start ;;
stop) cmd_stop ;;
restart) cmd_restart ;;
logs) cmd_logs ;;
status) cmd_status ;;
update) cmd_update ;;
configure) cmd_configure ;;
set-token) cmd_set_token ;;
help | -h | --help) usage ;;
*)
	echo "Unknown command: $1" >&2
	usage >&2
	exit 1
	;;
esac
