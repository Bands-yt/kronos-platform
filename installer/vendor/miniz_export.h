// Kronos ("Bootstrap Installer"): a real, minimal, hand-written
// replacement for the export header CMake's own GenerateExportHeader
// module would normally produce for miniz -- this installer builds
// miniz as a plain static library (see installer/CMakeLists.txt's own
// comment on why its real CMakeLists.txt is bypassed entirely rather
// than patched), so no real DLL import/export decoration is ever
// needed; both macros are real, correct no-ops for a static build on
// every real platform this installer targets.
#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H

#define MINIZ_EXPORT
#define MINIZ_NO_EXPORT

#endif
