## 1. Build and resources

- [x] 1.1 Make CMake platform-aware and package a macOS app with resources and icon.
- [x] 1.2 Resolve resources and writable runtime paths for Terminal and Finder launches.

## 2. Runtime and interaction

- [x] 2.1 Implement portable literal Marp arguments, dependency discovery, and cancellable processes.
- [x] 2.2 Own export/probe lifetimes and publish export results on the main thread.
- [x] 2.3 Support macOS OpenGL, Retina, fonts, Command shortcuts, and untruncated dialog paths.

## 3. Verification and documentation

- [x] 3.1 Isolate automation fixtures and add process, rendering, navigation, export, and shortcut checks.
- [x] 3.2 Build on this Mac and run the full automation successfully.
- [x] 3.3 Launch from an unrelated directory/minimal PATH, verify visually, and record evidence.
- [x] 3.4 Document macOS setup, build, automation, and verification results; validate OpenSpec and leave all changes uncommitted.

## 4. Shared platform and build conventions

- [x] 4.1 Centralize numeric platform macros using the fury3d convention and replace application OS guards.
- [x] 4.2 Use direct CMake/CTest commands and a CMake run target for both generator types.
- [x] 4.3 Reduce README to prerequisites, common commands, shortcuts, and credits.
- [x] 4.4 Run the shared workflow and automation, verify launch, and update verification evidence without committing.

## 5. Cross-platform ZIP installation

- [x] 5.1 Embed default resources and select the static MSVC runtime for the single-executable Windows release.
- [x] 5.2 Install only the application and create one ZIP automatically with the shared CMake install command.
- [x] 5.3 Verify ZIP contents, extraction, standalone resource fallback, configuration handling, and automation; document the command and provide the macOS ZIP.
