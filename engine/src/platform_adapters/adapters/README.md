# Why the console adapters are stubs, structurally

`WindowsAdapter`, `LinuxAdapter`, and `MacOSAdapter` are stubs because
nobody has wired them up to a real backend *yet* -- ordinary unfinished
work, safe to build out incrementally in this repository.

`XboxAdapter`, `PlayStationAdapter`, and `SwitchAdapter` are stubs for a
different reason, per docs/ARCHITECTURE.md §8.1 and Dependencies.cmake's
own note:

> Console SDK layers are thin translation shims compiled only inside each
> platform holder's NDA'd toolchain -- they never enter the open core repo.

Microsoft's GDK, Sony's PS5 SDK, and Nintendo's NX SDK are each distributed
only to registered developers under a non-disclosure agreement, and their
license terms generally prohibit redistributing SDK headers/libraries or
committing code written against them into a public or loosely-controlled
repository. That means:

- These three adapters **cannot** be completed as ordinary follow-up work
  in this codebase, the way `WindowsAdapter` can.
- A real implementation lives in a **separate, access-controlled repo (or a
  private submodule)** maintained by whoever on the team has the relevant
  platform's developer license, and is compiled only inside that platform
  holder's toolchain.
- What stays in *this* repo, forever, is the `IPlatformAdapter` interface
  these implementations satisfy -- so the engine core, `UnifiedInput`, and
  everything else in `platform_adapters/` can be written and tested against
  the interface without ever needing the real SDKs present.

`IOSAdapter` and `AndroidAdapter` are a third case: their SDKs (Xcode/
Android NDK) are publicly available, so they *could* be built out here --
but doing so for real needs each OS's actual app-lifecycle integration
(`UIApplicationDelegate`, `ANativeActivity`), which is meaningfully more
than this skeleton's scope. They're stubbed for the same "ordinary
unfinished work" reason as the three desktop adapters, not the NDA reason.
