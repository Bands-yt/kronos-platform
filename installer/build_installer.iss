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
UninstallDisplayIcon={app}\studio.exe
UninstallDisplayName={#AppName}

Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
; Everything cmake --install produced -- binaries, resolved DLLs,
; shaders, assets, games, templates, docs, plugins. See this file's own
; header comment for how to produce {#SourceDir} first.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\Kronos Studio"; Filename: "{app}\studio.exe"; WorkingDir: "{app}"
Name: "{group}\Kronos"; Filename: "{app}\engine_runtime.exe"; WorkingDir: "{app}"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Kronos Studio"; Filename: "{app}\studio.exe"; WorkingDir: "{app}"; Tasks: desktopicon

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
Root: HKCU; Subkey: "Software\Classes\kronos"; ValueType: string; ValueName: ""; ValueData: "URL:Kronos Protocol"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\kronos"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\kronos\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\KronosBootstrapper.exe"" ""%1"""

[Run]
Filename: "{app}\studio.exe"; Description: "Launch Kronos Studio"; Flags: nowait postinstall skipifsilent unchecked
