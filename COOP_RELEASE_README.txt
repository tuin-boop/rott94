ROTT94 CAMPAIGN CO-OP PROTOTYPE
================================

This is an experimental two-player Windows build of ROTT94 campaign co-op.
Commercial Rise of the Triad game data is not included.

QUICK START (TWO COMPUTERS ON THE SAME LAN)
--------------------------------------------

1. Extract the complete ZIP on both computers. Do not run it inside the ZIP.
2. Run Rott94CoopLauncher.exe on both computers.
3. On the first computer, click HOST.
4. Give the displayed LAN address to the second player.
5. On the second computer, enter that address and click JOIN.
6. Allow ROTT94 through Windows Firewall on Private networks if prompted.
7. Once both games have connected, the host selects CO-OP CAMPAIGN.

The first time it starts, the launcher looks for the registered DARKWAR data
in the default Steam installation of Rise of the Triad: Ludicrous Edition. If
Steam uses another library location, select that game's assets folder when the
launcher asks. The required data is copied locally into this folder's assets
directory.

Uncheck Windowed in the launcher for fullscreen. Sound, mouse, and controllers
remain enabled for LAN play.

CO-OP CONTROLS
---------------

F12        Host rallies the other player to a nearby safe tile
F10        Host gives both players test equipment and invulnerability
F9         Host defeats all currently active enemies
F8         Host restarts the current level
Shift+F9   Give yourself the real timed Dog Mode power-up
Shift+F10  Give yourself the real timed God Mode power-up
Escape     Leave or end the co-op session safely

CURRENT LIMITATIONS
--------------------

* Windows, two players, and direct IPv4 connections only.
* This release is intended for two computers on the same LAN.
* Room codes, internet relay/NAT traversal, and join-in-progress are not
  implemented yet.
* Both players must use this exact build and matching DARKWAR data.
* The automap is temporarily disabled in co-op.
* The LAN launcher is intended to start one game per computer. The source
  repository contains a separate isolated two-window launcher for local tests.

Source and issue tracker:
https://github.com/tuin-boop/rott94
