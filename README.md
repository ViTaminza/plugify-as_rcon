# as_rcon (Plugify)

Admin rcon: the console command 'mm_rcon <command line>' lets an admin (or the server console) execute any server command through the engine, and reports every use to the other plugins through admin_system::SendAction(slot, "rcon", command). Requires the @admin/rcon permission from the admin_system core; the server console (-1) always passes. A port of the Metamod:Source module 'Rcon' of the Pisex cs2-admin_system plugin.

Plugify port (fork) of **Rcon** by Pisex (Metamod (C++)).
Original: https://github.com/Pisex/cs2-admin_system_modules
The code was rewritten for Plugify / s2sdk; behaviour follows the original.

## Requirements
- [Plugify](https://github.com/untrustedmodders/plugify) with [s2sdk](https://github.com/untrustedmodders/plugify-plugin-s2sdk)
- Plugify dependencies: s2sdk, admin_system, hudmenu (optional)

## Installation
Download `as_rcon-1.0.0.zip` from Releases and extract it into `game/csgo/`.

## Building
Requires Docker. `./build.sh` builds in the Steam Runtime 3 (sniper) SDK image; the package
is written to `build/package/as_rcon`.

`plugins/admin_system` carry only the API headers of the plugins this one uses.

## License
GPL-3.0, see LICENSE.

