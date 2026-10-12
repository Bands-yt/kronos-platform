# Testing with a server in Studio

Roblox Studio can start a server and several players on your PC. Kronos Studio can too.

1. Open the **Test** tab.
2. Click **Server** and pick how many players (1 to 4).
3. Studio saves the open scene as a temporary game, starts a server without a window, and opens one Player window per player (`Player1`, `Player2`, ...). Each window joins the server, like on a real multiplayer server.
4. The **Multiplayer Test** window shows each program's state and its output. Pick a name to see its log (script `print`s and errors show up there).
5. Click **Stop Server** (or **Stop** in the window) to close everything.

Use it to check what one-player Play can't show: RemoteEvents between server and players, what each player sees, `PlayerAdded`, and scripts that run differently on the server and the client.

## What it does underneath

- The game is written to a temporary folder (`kronos-studio-test-<number>` in your system's temp folder) with the project's `Scripts`, `assets`, `audio` and `images` folders and `asset_map.json`. Your project isn't changed.
- The server is `engine_runtime --server <port> --game <slug> --headless`; each player is `engine_runtime --client 127.0.0.1 <port> --game <slug> --name PlayerN`, with the Player's normal in-game menu, chat and Roblox GUI. Leaving the game from a player window closes it.
- DataStores save to a `datastores` folder inside the temporary folder, so tests don't touch real saves.
- Everything runs offline: the test game is never looked up online.

## Limits

- Players use the default avatar, not yours.
- The windows open where your desktop puts them; arrange them yourself.
- On a player's client, scripts don't see other players' `Character` models yet (see ROBLOX_BRIDGE "Client and server").
- On Windows, Stop closes the programs at once (no clean shutdown), so a server's last DataStore save may be lost.
- Not available while Play is running.
