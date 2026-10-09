## ADDED Requirements

### Requirement: Relocatable macOS bundle
The build SHALL produce a macOS app bundle containing themes, the sample deck, and an app icon. The app SHALL discover resources independently of its working directory and SHALL write previews, logs, and the default editable sample outside the bundle in user-writable storage.

#### Scenario: Finder-style launch
- **WHEN** the bundle is launched with an unrelated working directory and minimal PATH
- **THEN** the sample deck and programmer theme load, installed Marp is discovered, and previews render without writing into the bundle

#### Scenario: Explicit deck path
- **WHEN** a user supplies a markdown path containing spaces, Unicode, or shell metacharacters
- **THEN** that deck loads and renders as a literal path

### Requirement: Native macOS interaction and display
The app SHALL use Command for Save, Open, palette, undo, clipboard, and Quit shortcuts, and Command+Shift+Z for redo. It SHALL retain native file dialogs, use available macOS fonts with CJK coverage, and render correctly on Retina displays.

#### Scenario: Keyboard editing
- **WHEN** a user edits text, presses Command+Z and Command+Shift+Z, then Command+S
- **THEN** undo and redo restore the expected buffer and save writes that buffer to the deck

#### Scenario: Retina preview
- **WHEN** the editor opens on a Retina display
- **THEN** the full editor, preview, and thumbnail strip render at the framebuffer's pixel dimensions with legible text

### Requirement: Safe background process lifecycle
Preview, export, and dependency processes SHALL use literal arguments on macOS and SHALL be owned until completion. Closing the app SHALL cancel outstanding work and reap children without waiting for a full render, and completed export results SHALL be applied on the main thread.

#### Scenario: Close during rendering
- **WHEN** the app closes while Marp or its browser is running
- **THEN** its process group is terminated and the app exits promptly

### Requirement: Reproducible automation and visual evidence
CTest SHALL run UI automation using a disposable deck and fail on missing previews, failed exports, or failed interactions. Verification SHALL include an actual rendered app screenshot and document commands and outcomes.

#### Scenario: Run automation
- **WHEN** a developer runs CTest after building
- **THEN** the original sample remains unchanged and tests verify preview textures, slide navigation, exports, platform shortcuts, and process cancellation
