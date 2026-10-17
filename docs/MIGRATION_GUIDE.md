# API changes and how to move your code

Every change that can break a plugin, script or project is listed here, newest first, with what to do (724).
The rules for when changes are allowed are in [PLUGIN_API.md](PLUGIN_API.md) ("Deprecation policy") and
[PROJECT_POLICY.md](PROJECT_POLICY.md).

| Version | What changed | What to do |
|---|---|---|
| 4.x to v5 | Python scripts get `world.API_VERSION` (currently 1) | Nothing. Check it if your script needs a newer `world` |
| (none other recorded) | | |

How to add a row: say what stopped working, from which version, and give the replacement in one line.
