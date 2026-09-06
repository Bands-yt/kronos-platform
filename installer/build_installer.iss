; Kronos Engine -- Inno Setup script for a traditional, offline,
; self-contained Windows installer (KronosSetup.exe). This is separate
; from installer/src/main.cpp (kronos_installer.exe, the small
; GitHub-release bootstrap/updater app that downloads a release at
; runtime) and from KronosBootstrapper.exe (installer/src/BootstrapperMain.cpp,
; the real kronos:// URI protocol handler this script registers below --
; a tiny per-launch wrapper that forwards the clicked URI to
; engine_runtime.exe, nothing more).
;
; Prerequisite: produce a staged install tree first --
;   from engine/:     cmake --install build --prefix ..\installer\dist --config Release
;   from installer/:  cmake --install build --prefix dist --config Release
; The second command adds KronosBootstrapper.exe (and kronos_installer.exe)
; alongside the engine binaries in installer\dist\, which [Files] below
; packages verbatim.
;
; Compile with: iscc build_installer.iss

#define AppName "Kronos Engine"
#define AppVersion "0.3.0-beta"
; Kronos ("Windows Installer Build Verification"): VersionInfoVersion
; (below) feeds the PE VS_VERSIONINFO numeric version field directly --
; Inno Setup requires strictly "major.minor.build.private" (1-4 plain
; integers, no suffix), so AppVersion's own "-beta" tag (correct for the
; user-facing display strings below) can't be reused there verbatim.
; This never surfaced before because every prior Windows CI run failed
; earlier, mid-engine-compile; confirmed for real once that earlier
; failure was fixed and this build actually reached "Compile installer"
; for the first time (iscc's own error: "Value of [Setup] section
; directive "VersionInfoVersion" is invalid.").
#define AppVersionNumeric "0.3.0.0"
#define AppPublisher "Kronos"
#define AppURL "https://github.com/Bands-yt/kronos-platform"
#define SourceDir "dist"
#define IconFile "..\engine\assets\icons\kronos_icon.ico"

[Setup]
; Generated once for this app; do not change on future version bumps --
; changing it makes Windows treat upgrades as a different application.
AppId={{1AE69623-579B-4C3A-B131-D41FB0F0E14F}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
VersionInfoVersion={#AppVersionNumeric}

; Per-user, no-admin install -- {localappdata} is writable by the
; current user, and PrivilegesRequired=lowest stops Setup from
; requesting UAC elevation at all.
DefaultDirName={localappdata}\KronosEngine
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

DefaultGroupName=Kronos Engine
DisableProgramGroupPage=yes
AllowNoIcons=yes

OutputDir=output
OutputBaseFilename=KronosSetup
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\kronos_studio.exe
UninstallDisplayName={#AppName}

Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

; Kronos ("Installer Component Selection"): a real Inno Setup component
; page -- "full" preselects every real component (the "or check All"
; case), "custom" (Inno's own built-in iscustom type) lets a user
; deselect individual ones. `core` is `Flags: fixed`: the shared
; runtime files below (DLLs, shaders, assets, games, templates, docs,
; plugins, KronosBootstrapper.exe/kronos_installer.exe) are needed by
; every one of the 5 apps, so it can never be unchecked -- shown, not
; hidden, so a user can see it's there and why.
[Types]
Name: "full"; Description: "Full installation (all tools)"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "core"; Description: "Kronos Engine core files (required by every app below)"; Types: full custom; Flags: fixed
Name: "player"; Description: "Player (game client, kronos:// launch links)"; Types: full custom
Name: "studio"; Description: "Studio (full creator suite)"; Types: full custom
Name: "tools3d"; Description: "3D Tools"; Types: full custom
Name: "moviemode"; Description: "Movie Mode"; Types: full custom
Name: "audio"; Description: "Audio"; Types: full custom

[Files]
; Kronos ("Installer Component Selection"): each real per-app .exe is
; tagged to its own real component, so unchecking e.g. "3D Tools" really
; leaves kronos_3d_maker.exe out of the install rather than always
; copying everything and just hiding a shortcut. `studio` installs BOTH
; studio.exe (the legacy full-editor shell) and kronos_studio.exe (the
; standalone Full-mode app this component's own shortcut/launch entry
; below actually points at) -- both are real "Kronos Studio" under the
; hood (see engine/src/CMakeLists.txt's own comment on why they share an
; icon), and neither costs anything extra to include alongside the
; other since both are already staged in {#SourceDir} either way.
;
; Everything else (resolved DLLs, shaders, assets, games, templates,
; docs, plugins, the two installer/ helper .exes) is real shared
; support every one of the 5 apps needs regardless of which are
; selected -- tagged `core` (Flags: fixed, see [Components] above), not
; duplicated per-component.
Source: "{#SourceDir}\engine_runtime.exe"; DestDir: "{app}"; Components: player; Flags: ignoreversion
Source: "{#SourceDir}\studio.exe"; DestDir: "{app}"; Components: studio; Flags: ignoreversion
Source: "{#SourceDir}\kronos_studio.exe"; DestDir: "{app}"; Components: studio; Flags: ignoreversion
Source: "{#SourceDir}\kronos_3d_maker.exe"; DestDir: "{app}"; Components: tools3d; Flags: ignoreversion
Source: "{#SourceDir}\kronos_movie_maker.exe"; DestDir: "{app}"; Components: moviemode; Flags: ignoreversion
Source: "{#SourceDir}\kronos_audio.exe"; DestDir: "{app}"; Components: audio; Flags: ignoreversion
Source: "{#SourceDir}\KronosBootstrapper.exe"; DestDir: "{app}"; Components: core; Flags: ignoreversion
Source: "{#SourceDir}\kronos_installer.exe"; DestDir: "{app}"; Components: core; Flags: ignoreversion
Source: "{#SourceDir}\*.dll"; DestDir: "{app}"; Components: core; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourceDir}\README.md"; DestDir: "{app}"; Components: core; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourceDir}\shaders\*"; DestDir: "{app}\shaders"; Components: core; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#SourceDir}\assets\*"; DestDir: "{app}\assets"; Components: core; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#SourceDir}\games\*"; DestDir: "{app}\games"; Components: core; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#SourceDir}\templates\*"; DestDir: "{app}\templates"; Components: core; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#SourceDir}\docs\*"; DestDir: "{app}\docs"; Components: core; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#SourceDir}\plugins\*"; DestDir: "{app}\plugins"; Components: core; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\Kronos"; Filename: "{app}\engine_runtime.exe"; WorkingDir: "{app}"; Components: player
Name: "{group}\Kronos Studio"; Filename: "{app}\kronos_studio.exe"; WorkingDir: "{app}"; Components: studio
Name: "{group}\Kronos 3D Tools"; Filename: "{app}\kronos_3d_maker.exe"; WorkingDir: "{app}"; Components: tools3d
Name: "{group}\Kronos Movie Mode"; Filename: "{app}\kronos_movie_maker.exe"; WorkingDir: "{app}"; Components: moviemode
Name: "{group}\Kronos Audio"; Filename: "{app}\kronos_audio.exe"; WorkingDir: "{app}"; Components: audio
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Kronos"; Filename: "{app}\engine_runtime.exe"; WorkingDir: "{app}"; Tasks: desktopicon; Components: player

[Registry]
; kronos:// URI protocol -- HKCU (not HKLM/HKCR) so it needs no
; elevation, the same real scheme installer/src/PlatformIntegration.cpp's
; registerUrlProtocolHandler() already registers for the bootstrap
; installer's own install path. uninsdeletekey on the root key only, so
; uninstalling removes the whole "kronos" key tree in one step.
;
; Points at KronosBootstrapper.exe, not engine_runtime.exe directly --
; the bootstrapper is the one real, minimal per-launch wrapper
; (installer/src/BootstrapperMain.cpp) that forwards the raw clicked
; URI (%1, unformatted) on to engine_runtime.exe as --kronos-uri=<uri>
; itself; this command line hands it the raw URI, not a pre-built flag.
; Components: player -- the kronos:// scheme launches the game client
; specifically (KronosBootstrapper.exe forwards straight to
; engine_runtime.exe), so registering it when Player wasn't even
; installed would point the whole OS at a binary that isn't there.
Root: HKCU; Subkey: "Software\Classes\kronos"; ValueType: string; ValueName: ""; ValueData: "URL:Kronos Protocol"; Flags: uninsdeletekey; Components: player
Root: HKCU; Subkey: "Software\Classes\kronos"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""; Components: player
Root: HKCU; Subkey: "Software\Classes\kronos\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\KronosBootstrapper.exe"" ""%1"""; Components: player

[Run]
; Kronos ("Installer Component Selection"): gated per-component so the
; post-install launch checkbox only ever offers an app that was really
; installed.
Filename: "{app}\kronos_studio.exe"; Description: "Launch Kronos Studio"; Flags: nowait postinstall skipifsilent unchecked; Components: studio
Filename: "{app}\engine_runtime.exe"; Description: "Launch Kronos"; Flags: nowait postinstall skipifsilent unchecked; Components: player
