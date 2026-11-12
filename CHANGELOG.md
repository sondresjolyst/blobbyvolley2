# Changelog

## [0.3.0](https://github.com/sondresjolyst/blobbyvolley2-tournament/compare/v0.2.0...v0.3.0) (2026-11-12)


### Features

* **release:** publish a signed update manifest with each release ([#86](https://github.com/sondresjolyst/blobbyvolley2-tournament/issues/86)) ([191ff81](https://github.com/sondresjolyst/blobbyvolley2-tournament/commit/191ff81775d71c9a31229f356b332c0a1820b15b))
* ship the game as a launcher that installs signed updates ([#88](https://github.com/sondresjolyst/blobbyvolley2-tournament/issues/88)) ([2d993c6](https://github.com/sondresjolyst/blobbyvolley2-tournament/commit/2d993c63b551e2878c74bcc78aaf21494878c5af)), closes [#83](https://github.com/sondresjolyst/blobbyvolley2-tournament/issues/83) [#84](https://github.com/sondresjolyst/blobbyvolley2-tournament/issues/84)
* tell players with an older game that a new version is out ([#87](https://github.com/sondresjolyst/blobbyvolley2-tournament/issues/87)) ([a496db8](https://github.com/sondresjolyst/blobbyvolley2-tournament/commit/a496db89ada8ad6f5dc8651d1c464dc3092b8877))


### Bug Fixes

* **server:** send the tournament status and the sponsor board before a game starts ([#80](https://github.com/sondresjolyst/blobbyvolley2-tournament/issues/80)) ([2b101fb](https://github.com/sondresjolyst/blobbyvolley2-tournament/commit/2b101fb74e9c2bde7dc08fe25f2190b4197d095a))

## [0.2.0](https://github.com/sondresjolyst/blobbyvolley2-tournament/compare/v0.1.0...v0.2.0) (2026-11-10)


### Features

* add Windows file properties to the game, server and load test bot ([#74](https://github.com/sondresjolyst/blobbyvolley2-tournament/issues/74)) ([89974b2](https://github.com/sondresjolyst/blobbyvolley2-tournament/commit/89974b2c32b88e7f6e39aad12b163f2f7c400434))
* **server:** play training matches against a bot script on the server ([#76](https://github.com/sondresjolyst/blobbyvolley2-tournament/issues/76)) ([38ad9ef](https://github.com/sondresjolyst/blobbyvolley2-tournament/commit/38ad9eff5872f41bf9b9041d2780f832edd0a5cc))


### Bug Fixes

* **ci:** read the release tag from the manifest when release-please gives no tag for the root package ([#68](https://github.com/sondresjolyst/blobbyvolley2-tournament/issues/68)) ([56f30b4](https://github.com/sondresjolyst/blobbyvolley2-tournament/commit/56f30b4ffa4189796d21cfaa9985f6c901679d3c))
* clear compiler warnings in the game logic, network code and RakNet ([#73](https://github.com/sondresjolyst/blobbyvolley2-tournament/issues/73)) ([1f2d50f](https://github.com/sondresjolyst/blobbyvolley2-tournament/commit/1f2d50fe54601e244420708b04200b6cca56038a))
* log the final loadbot score and client addresses correctly ([#77](https://github.com/sondresjolyst/blobbyvolley2-tournament/issues/77)) ([4b8bee4](https://github.com/sondresjolyst/blobbyvolley2-tournament/commit/4b8bee47bb7460c11e690b3ed7f10134cfe0dfea))
* send the final score with the win notification and the match points with the rules ([#75](https://github.com/sondresjolyst/blobbyvolley2-tournament/issues/75)) ([d98dddd](https://github.com/sondresjolyst/blobbyvolley2-tournament/commit/d98dddd519d0b27a98f4a48bb0cd71d28be2a8c4))
* **server:** pause a tournament game while a player sends nothing ([#78](https://github.com/sondresjolyst/blobbyvolley2-tournament/issues/78)) ([f66fbea](https://github.com/sondresjolyst/blobbyvolley2-tournament/commit/f66fbeab630b59d809aa99978aef2ce0528f82b3))

## 0.1.0 (2026-11-09)


### Features

* add a container image for the dedicated server ([#32](https://github.com/sondresjolyst/blobbyvolley2/issues/32)) ([5ee2980](https://github.com/sondresjolyst/blobbyvolley2/commit/5ee29806f84f3aeff5d7093a8c0dbc347b1ebb04))
* add a headless load test bot ([#56](https://github.com/sondresjolyst/blobbyvolley2/issues/56)) ([5f98300](https://github.com/sondresjolyst/blobbyvolley2/commit/5f98300207a2c1337e72cae2496c36c6cca7aa78))
* add a scrolling sponsor board to the game ([#41](https://github.com/sondresjolyst/blobbyvolley2/issues/41)) ([43ce65b](https://github.com/sondresjolyst/blobbyvolley2/commit/43ce65b4d293473f76991f51820f9f791da40e4a))
* add scripts to start the tournament server ([#48](https://github.com/sondresjolyst/blobbyvolley2/issues/48)) ([454017e](https://github.com/sondresjolyst/blobbyvolley2/commit/454017efcbd904628f21d75eb2e098a6836d6401))
* add tournament mode with join codes ([#40](https://github.com/sondresjolyst/blobbyvolley2/issues/40)) ([1bac3da](https://github.com/sondresjolyst/blobbyvolley2/commit/1bac3daef4201c2961aebd78bb9c038a6bf619d3))
* enter join links in a text field ([#58](https://github.com/sondresjolyst/blobbyvolley2/issues/58)) ([d2062f2](https://github.com/sondresjolyst/blobbyvolley2/commit/d2062f2b74796e6c72bc9be6136d7ee41cc82cbf))
* **loadbot:** play with a bot script of the game ([#63](https://github.com/sondresjolyst/blobbyvolley2/issues/63)) ([3bea5f6](https://github.com/sondresjolyst/blobbyvolley2/commit/3bea5f62975da05e025fefbecc8010b1fd4e7f7e))
* send the sponsor board to tournament players ([#51](https://github.com/sondresjolyst/blobbyvolley2/issues/51)) ([ac9b949](https://github.com/sondresjolyst/blobbyvolley2/commit/ac9b949a51371809d2a2d9efb56901d3106a5818))
* **server:** set the server port with BLOBBY_PORT ([#65](https://github.com/sondresjolyst/blobbyvolley2/issues/65)) ([313e458](https://github.com/sondresjolyst/blobbyvolley2/commit/313e4589f8bfb3bad9296746ba13c1418c028a78))
* start assigned tournament matches ([#43](https://github.com/sondresjolyst/blobbyvolley2/issues/43)) ([b4dce42](https://github.com/sondresjolyst/blobbyvolley2/commit/b4dce4225bce823c217b4074e82bf136c08ebfff))
* start the game from blobby:// join links ([#42](https://github.com/sondresjolyst/blobbyvolley2/issues/42)) ([ea27eeb](https://github.com/sondresjolyst/blobbyvolley2/commit/ea27eebba7cae30ac4f0149df1436e8fbb06acc8))
* sync the tournament server with the site and report results reliably ([#47](https://github.com/sondresjolyst/blobbyvolley2/issues/47)) ([52b53af](https://github.com/sondresjolyst/blobbyvolley2/commit/52b53af9310458621d952b58edbb9ea10a3d0524))


### Bug Fixes

* add limits and checks before the first release ([#53](https://github.com/sondresjolyst/blobbyvolley2/issues/53)) ([92e04f9](https://github.com/sondresjolyst/blobbyvolley2/commit/92e04f9c03534ae7f661c628a8f233483f9effff))
* compare RakNet times by their signed difference ([#59](https://github.com/sondresjolyst/blobbyvolley2/issues/59)) ([9fbebc7](https://github.com/sondresjolyst/blobbyvolley2/commit/9fbebc7a43217086f42a6b066a429be2bf647fc0))
* copy the version files into the server image build ([#39](https://github.com/sondresjolyst/blobbyvolley2/issues/39)) ([d1e4af8](https://github.com/sondresjolyst/blobbyvolley2/commit/d1e4af811bf680ec62c3c05c288f0869a5041c4e))
* join without a confirmation from the join link screen ([#64](https://github.com/sondresjolyst/blobbyvolley2/issues/64)) ([a1b1536](https://github.com/sondresjolyst/blobbyvolley2/commit/a1b1536067f95a460a19c557da2c0bd94bf60d47))
* keep server search RakNet calls on the main thread ([#54](https://github.com/sondresjolyst/blobbyvolley2/issues/54)) ([dfa4d7a](https://github.com/sondresjolyst/blobbyvolley2/commit/dfa4d7a99be58080f6a60bd7a75a1415db685008))
* keep the dialled server port when a connection is accepted ([#66](https://github.com/sondresjolyst/blobbyvolley2/issues/66)) ([b07c1c0](https://github.com/sondresjolyst/blobbyvolley2/commit/b07c1c0e49ebfffc95e088439b89c43cc003886d))
* prepare packaging, docs, CI and tests for the first release ([#52](https://github.com/sondresjolyst/blobbyvolley2/issues/52)) ([7ddc7aa](https://github.com/sondresjolyst/blobbyvolley2/commit/7ddc7aa56949b14be2014549ab8d603c37c518c9))
* **raknet:** validate split packet parts and message reads ([#26](https://github.com/sondresjolyst/blobbyvolley2/issues/26)) ([ff2f151](https://github.com/sondresjolyst/blobbyvolley2/commit/ff2f1512ab5aad93b68426e4cce7d543d858b3db))
* rename the paste join link button to join link ([#44](https://github.com/sondresjolyst/blobbyvolley2/issues/44)) ([807b2f4](https://github.com/sondresjolyst/blobbyvolley2/commit/807b2f40f11fd98364e9c859ae158551d542fcc5))
* smaller rules and state fixes ([#28](https://github.com/sondresjolyst/blobbyvolley2/issues/28)) ([6522f81](https://github.com/sondresjolyst/blobbyvolley2/commit/6522f810bb4fcef1f39830985a460464ddb23fa1))
* start network games reliably ([#55](https://github.com/sondresjolyst/blobbyvolley2/issues/55)) ([c3e00fe](https://github.com/sondresjolyst/blobbyvolley2/commit/c3e00fe1b9778348e2943e184616d59d0c58a8f0))
* validate lengths in network messages ([#24](https://github.com/sondresjolyst/blobbyvolley2/issues/24)) ([7616a16](https://github.com/sondresjolyst/blobbyvolley2/commit/7616a16f4b28a5177c08954ccbed3b714c8ef377))
* validate replay data while loading ([#27](https://github.com/sondresjolyst/blobbyvolley2/issues/27)) ([fbc2401](https://github.com/sondresjolyst/blobbyvolley2/commit/fbc2401a16c5e15d96767ac41d1b4b4907935183))
* validate server state and synchronize shared data ([#25](https://github.com/sondresjolyst/blobbyvolley2/issues/25)) ([5256bc7](https://github.com/sondresjolyst/blobbyvolley2/commit/5256bc7315b620c4af5b6d8264489feb900fc7ce))
* validate values from scripts and network messages ([#9](https://github.com/sondresjolyst/blobbyvolley2/issues/9)) ([e8815cb](https://github.com/sondresjolyst/blobbyvolley2/commit/e8815cbfb36dc8565303c09311b8e297f7219569))
