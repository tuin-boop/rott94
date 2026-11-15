ROTT94 CAMPAIGN CO-OP PROTOTYPE
================================

This is an experimental Windows build of ROTT94 campaign co-op for up to five
players.
Commercial Rise of the Triad game data is not included.

QUICK START (TWO COMPUTERS ON THE SAME LAN)
--------------------------------------------

1. Extract the complete ZIP on both computers. Do not run it inside the ZIP.
2. Copy DARKWAR.WAD, DARKWAR.RTL, and REMOTE1.RTS from the registered game
   into the build's assets folder on every computer.
3. Run Rott94.Windows.Release.exe directly on every computer.
4. On the first computer, choose MULTIPLAYER / CO-OP -> HOST LAN GAME and
   select the total number of players.
5. On the other computers, choose MULTIPLAYER / CO-OP -> JOIN LAN GAME and
   select the discovered host. A manual IPv4 address can also be entered.
6. Allow ROTT94 through Windows Firewall on Private networks if prompted.
7. Once everyone has connected, the host selects CO-OP CAMPAIGN.

The launcher has been retired. Single-player, hosting, joining, display
options, and controls are all available inside the game.

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

* Windows, up to five players, and direct IPv4 connections only.
* This release is intended for two computers on the same LAN.
* Room codes, internet relay/NAT traversal, and join-in-progress are not
  implemented yet.
* Both players must use this exact build and matching DARKWAR data.
* The automap is temporarily disabled in co-op.
* The source repository contains a separate PowerShell script for isolated
  two-window testing on one computer.

Source and issue tracker:
https://github.com/tuin-boop/rott94
