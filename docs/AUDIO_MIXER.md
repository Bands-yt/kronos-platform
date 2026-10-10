# Audio Mixer

Every sound in a Kronos game plays through the **mixer**. The mixer lets you
control whole groups of sounds at once: turn all music down, muffle every
sound effect when the player is underwater, or duck the music while a
character talks.

## The basics

- **Bus**: a group of sounds with its own volume, mute, filters and reverb.
  Every sound plays on one bus (the Inspector's *Mixer bus* setting).
- **Master**: the bus everything ends up in. Other buses "go into" Master or
  into another bus, so you can build groups of groups (for example a
  `Footsteps` bus inside `SFX`).
- **Send**: feeds a copy of a bus into another bus, usually a reverb bus,
  without moving it there. `pre` sends ignore the bus's own volume, `post`
  sends follow it.
- **Ducking**: turns a bus down automatically while another bus is making
  sound, then brings it back.
- **Snapshot**: a named set of changes (such as "Paused" or "Underwater") that
  a game can fade in and out. Each value in a snapshot is where that setting
  ends up at full strength. At half strength it's halfway there.
- **Voices**: every object with a sound plays through its own voice, so ten
  objects using the same sound file can all play at once, each from its own
  position, volume and bus. Stopping one doesn't stop the others.

## The default mixer

New projects start with:

| Bus | Goes into | Notes |
|---|---|---|
| Master | (speakers) | |
| Music | Master | ducks by 9 dB while **Voice** is playing |
| SFX | Master | the default bus for sounds |
| Voice | Master | |
| UI | Master | |

Two snapshots come with it:

- **Paused**: SFX to -20 dB, Music to -6 dB and muffled (low-pass at 1500 Hz).
- **Underwater**: SFX, Music and Voice muffled (low-pass at 700 / 1200 /
  1800 Hz) and a little reverb on SFX.

## In Studio

- **Plugins → Mixer** opens the mixer panel.
  - The top row has one strip per bus: a volume fader, a level meter, **M**
    (mute) and **S** (solo). Click a strip to select its bus.
  - **Bus** tab: the selected bus's name, where it goes, volume, low-pass,
    high-pass, reverb, sends and ducking. **Remove This Bus** deletes it. Buses
    inside it move up a level, and sounds on it go to Master.
  - **Snapshots** tab: create and edit snapshots. **Try It** fades one in so
    you can hear it, and **Turn Off** fades it out.
  - **Test Sounds** tab: play a sound file through any bus while you adjust
    the mix.
  - **Save** writes `mixer.kmixer` next to your project file (save the
    project first). **Revert** goes back to the saved file, and
    **Reset to Default** restores the default mixer.
- **Inspector → Add Sound** gives an entity a sound: file, volume, pitch,
  loop, *Play when the game starts*, 3D distance settings and *Mixer bus*.
  **Preview** plays it in the editor. Undo works for all of these.
- **Play** runs the game with its sounds and mixer. While it plays, the
  faders keep showing your saved mix, and scripts change the live mix
  underneath. Stop puts the mixer back the way it was.

When the game starts, the player loads `mixer.kmixer` from the project
folder. If there isn't one, it uses the default mixer.

## Scripting: the `audio` table

Available in game scripts, both in the player and in Studio's Play mode.
Buses and snapshots are referred to by name. Entities are the same entity
IDs that `world.*` uses, and the entity needs a sound (Inspector → Add Sound).

| Function | What it does |
|---|---|
| `audio.snapshot(name, intensity?, fadeSeconds?)` | Fades a snapshot to `intensity` (0–1, default 1) over `fadeSeconds` (default 0). Use 0 to turn it off. Returns `false` if there's no snapshot with that name. |
| `audio.snapshotIntensity(name)` | How strongly the snapshot is applied right now (0–1). |
| `audio.setBusVolume(bus, dB)` | Sets a bus's volume in dB (-80 to +24). Returns `false` for an unknown bus. |
| `audio.busVolume(bus)` | The bus's volume in dB, after snapshots. |
| `audio.setBusMuted(bus, muted)` | Mutes or unmutes a bus. Returns `false` for an unknown bus. |
| `audio.busLevel(bus)` | How loud the bus is right now, in dB (-80 is silent). Useful for things that react to music. |
| `audio.play(entity)` | Plays the entity's sound from the start. Returns `true`, or `nil` if the entity has no sound. |
| `audio.stop(entity)` | Stops it. |
| `audio.isPlaying(entity)` | `true` while it's playing. |
| `audio.setVolume(entity, volume)` | Sound volume, 0 to 4 (1 is normal). |
| `audio.setPitch(entity, pitch)` | Playback speed and pitch, 0.05 to 8 (1 is normal). |
| `audio.setBus(entity, bus)` | Moves the sound to another bus. |

```lua
-- Muffle everything while the diver is below the water line.
local diver = world.findByName("Diver")
local wasUnderwater = false
events.onUpdate(function()
    local _, y = world.getPosition(diver)
    local underwater = y ~= nil and y < 0
    if underwater ~= wasUnderwater then
        audio.snapshot("Underwater", underwater and 1 or 0, 0.5)
        wasUnderwater = underwater
    end
end)

-- Let players turn the music down from a settings menu.
audio.setBusVolume("Music", -12)
```

## The `.kmixer` file

`mixer.kmixer` is a plain text file, one item per line. Names are in double
quotes, and lines starting with `#` are comments. The first line says which
format version it is.

```
kronos-mixer 1
bus "Master" parent "" volume 0 mute 0 lowpass 20000 highpass 20 reverb 0 0.5 0.5
bus "Music" parent "Master" volume 0 mute 0 lowpass 20000 highpass 20 reverb 0 0.5 0.5
bus "SFX" parent "Master" volume 0 mute 0 lowpass 20000 highpass 20 reverb 0 0.5 0.5
duck "Music" "Voice" -9 -45 80 600
send "SFX" "Reverb" -6 post
snapshot "Paused"
value "Paused" "SFX" volume -20
value "Paused" "Music" lowpass 1500
```

| Line | Meaning |
|---|---|
| `bus "name" parent "parent" volume <dB> mute <0/1> lowpass <Hz> highpass <Hz> reverb <mix> <room> <damping>` | A bus. Master has an empty parent. Settings after the name can be left out; they default to 0 dB, unmuted, filters open (20000 / 20 Hz) and no reverb (`0 0.5 0.5`). A bus must come before its own `send` and `duck` lines. |
| `send "from" "to" <dB> pre\|post` | A send from one bus into another. |
| `duck "bus" "trigger" <dB> <threshold dB> <attack ms> <release ms>` | Turns `bus` down by `<dB>` while `trigger` is louder than the threshold. |
| `snapshot "name"` | Starts a snapshot. |
| `value "snapshot" "bus" <setting> <value>` | A snapshot value. `<setting>` is `volume`, `lowpass`, `highpass` or `reverb`. For a send it's `send "target" <dB>`. |

Kronos checks the file when it loads it. There must be exactly one bus
without a parent (the master), every parent, send target and duck trigger
must exist, and sound can't loop back into a bus it came from. A file with
a problem is ignored and the default mixer is used instead. The Mixer panel
says what's wrong (for example `line 7: send from unknown bus "Foo"`).
