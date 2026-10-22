# Running a Project Reality Mumble server

`prmurmurd` sorts players into their game's team and squad channels by itself: it needs no mumo,
no Python and no Ice. The PR:BF2 game reports each player's game server, team, squad and role
through the PRMumble client, and the server uses that to put the player into the right channels and
permission groups.

## Registering a game server

Every PR:BF2 game server that should use this Mumble server is registered once:

```
prmurmurd --pr-add-game <code> --pr-name "<name>" --pr-ipport <ip>:<port>[,<ip>:<port>...]
```

- `<code>`: a short unique identifier (letters, digits, `_` and `-`), e.g. `awesome`.
- `<name>`: the name of the game's channel, e.g. `Awesome PR Server`.
- `<ip>:<port>`: the address the game server reports to players, as configured on the game
  server. Several addresses can be given for game servers reachable under more than one address.

The command:

- creates the game's channels under `PR BF2 Game Servers`;
- generates the identity secret and prints it once. Put that value into the game server's
  `realityconfig_admin.py`.

The first registration on a server also sets up the root permissions and the `PR BF2 Game Servers`
and `Lobby` channels.

Options:

| Option | Meaning |
|---|---|
| `--pr-secret <secret>` | Use this secret instead of generating one, e.g. to keep the secret an existing game server already uses. |
| `--pr-no-verify` | Don't verify player identities for this game. Anyone with a modified client can then join any team and squad, so only use this for testing. |
| `--pr-link-teams` | Link the game's channel with both team channels. |
| `--pr-server <srv>` | The virtual server to act on. Only needed if the database contains several virtual servers. |

The `--pr-*` commands work on the database directly and then exit, like `--set-su-pw`.
**Stop the server before running them, or restart it afterwards**, because a running server only
picks up the changes when it starts.

Running `--pr-add-game` again with the same settings does nothing and succeeds, so a container
entrypoint can run it on every start:

```
prmurmurd --pr-add-game awesome --pr-name "Awesome PR Server" --pr-ipport "$GAMESERVER_IP" \
          --pr-secret "$PR_SECRET" && exec prmurmurd --foreground
```

The Docker image in [docker/](../docker/README.md) does this from the `PR_GAME_*` environment variables.

Registering a code again with different settings, or an address that another game already uses,
fails and changes nothing.

## Listing and removing game servers

```
prmurmurd --pr-list-games
prmurmurd --pr-remove-game <code>
```

- `--pr-list-games` prints code, name, addresses, secret and channel of every registered game.
- `--pr-remove-game` deletes the game's channels.

To change a game's addresses or secret, remove it and add it again.

## Channels and permissions

```
Root
├─ PR BF2 Game Servers
│  └─ <name>
│     ├─ Team 1   (opfor)
│     │  ├─ Commander
│     │  └─ No Squad, Squad 1 … Squad 9
│     └─ Team 2   (blufor)
│        └─ …
└─ Lobby
```

- Nobody can speak in root, `PR BF2 Game Servers`, the game channels or `Lobby`.
- Only players the server has sorted into the game can enter or speak in its team channels.
  Their squad channel accepts whispers from the squad (squad radio), from squad leaders and from
  the commander, and the Commander channel accepts whispers from squad leaders.
- Registered users can be given extra rights over one game, such as moving and muting players, by
  adding them to that game's `bf2_<code>_admin` group.

## How players are sorted

Whenever a player's game state changes (joining a server, switching team or squad, becoming squad
leader or commander), the PRMumble client sends it to the server.

**If the game server is registered and the identity verifies against its secret**, the server:

- gives the player the game's temporary groups;
- moves them to their squad channel, or to Commander if they're commander.

**Players who aren't validly in a registered game**, for example because they left the game or
failed verification:

- lose those groups;
- are moved from the team channels to the game's channel.

Failed verifications are logged with the user name, certificate hash and address. A burst of
failures for everybody on one game usually means the secret in `realityconfig_admin.py` doesn't
match the registered one, or that the game server's clock is off by more than a minute.

## Moving from the old mumo setup

The server can't open databases of the old PR Murmur release, so start with a new database. Then,
for every `[gN]` section in the old `prbf2.ini`, run:

```
prmurmurd --pr-add-game <name value> --pr-name "<channel name>" --pr-ipport <ipport_filter value> \
          --pr-secret <secret from the [prbf2] section>
```

The game servers keep working unchanged. mumo, Python, Ice, `PRMurmur.ice` and the old setup scripts
are no longer needed.
