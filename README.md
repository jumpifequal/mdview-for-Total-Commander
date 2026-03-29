# MDView

**A Markdown viewer plugin for Total Commander.**

Press `F3` on any `.md` file and get a clean, fully rendered preview with dark mode, syntax highlighting, table of contents, find-in-page, split view, and built-in Mermaid support.

![License](https://img.shields.io/badge/license-MIT-blue.svg)

---

## Screenshots

> *TODO: Add screenshots of light mode, dark mode, split view, and Mermaid rendering.*

## Features

- **Full Markdown rendering** — headings, bold, italic, strikethrough, links, images, tables with column alignment, fenced and indented code blocks, blockquotes (nested), ordered and unordered lists, task lists, horizontal rules, autolinks, and escape sequences
- **Syntax highlighting** — JavaScript, TypeScript, Python, C, C++, C#, Java, Rust, Go, SQL, Bash, CSS/SCSS, PHP, HTML, and XML
- **Built-in Mermaid diagrams** — self-contained support for `graph` / `flowchart`, `sequenceDiagram`, `classDiagram`, `stateDiagram`, and `stateDiagram-v2`
- **Consistent Mermaid typography** — supported Mermaid diagrams follow the active viewer font family and font size from MDView settings
- **Dark / light mode** — toggle with `Ctrl+D`, or auto-detected from the Windows theme on first launch
- **Adjustable layout** — zoom in and out, optionally constrain reading column width
- **Line numbers** — toggle on code blocks with `Ctrl+L`
- **Table of Contents** — auto-generated sidebar from headings
- **Find in page** — incremental search with match highlighting and navigation
- **Split view** — rendered Markdown alongside the raw Markdown source with `Ctrl+M`
- **Raw Markdown viewer** — implemented with the Windows RichEdit control using a configurable monospace font
- **Scroll synchronisation** — rendered HTML and raw Markdown views stay aligned using ratio-based document scrolling
- **Smart clipboard behaviour**
  - Copy from rendered view → formatted HTML + plain text
  - Copy from raw view → original Markdown text
- **Expand / collapse** — long code blocks and blockquotes are collapsed by default with a "Show more" button
- **Persistent settings** — font size, theme, column width, line numbers, and raw view settings are saved and restored between sessions
- **Print support** — `Ctrl+P` renders a clean printable version
- **Progress bar** — subtle reading position indicator at the top of the viewport
- **Full window resize** — content fills the viewport correctly when the lister window is resized or maximised

## Mermaid Support

Mermaid rendering is built directly into the plugin and is fully self-contained inside the `.wlx` and `.wlx64` binaries. No external JavaScript files are required in the distribution.

Currently supported Mermaid diagram types:

- `graph` / `flowchart`
- `sequenceDiagram`
- `classDiagram`
- `stateDiagram`
- `stateDiagram-v2`

For the supported diagram types, MDView keeps Mermaid output aligned with the surrounding document typography and scales SVG output to fit the available preview width without pathological oversizing.

Unsupported Mermaid syntaxes fall back safely to the original source block instead of breaking the preview.

## Keyboard Shortcuts

| Shortcut           | Action                                                     |
| ------------------ | ---------------------------------------------------------- |
| `Ctrl` `+`         | Zoom in                                                    |
| `Ctrl` `-`         | Zoom out                                                   |
| `Ctrl` `0`         | Reset zoom                                                 |
| `Ctrl` `W`         | Constrain column width                                     |
| `Ctrl` `Shift` `W` | Widen or remove column constraint                          |
| `Ctrl` `D`         | Toggle dark / light mode                                   |
| `Ctrl` `L`         | Toggle line numbers                                        |
| `Ctrl` `T`         | Table of Contents                                          |
| `Ctrl` `F`         | Find in page                                               |
| `Ctrl` `P`         | Print                                                      |
| `Ctrl` `G`         | Go to top                                                  |
| `Ctrl` `M`         | Toggle split view                                          |
| `Ctrl` `C`         | Copy selection                                             |
| `Ctrl` `A`         | Select All text in one of the window (md render or source) |
| `Esc`              | Close viewer                                               |
| `F1`               | Show shortcut reference                                    |

Press `F1` inside the viewer for an on-screen reference.

## Installation

### Automatic

Open the downloaded `.zip` file inside Total Commander. The included `pluginst.inf` triggers the automatic plugin installer.

### Manual

1. Extract `mdview.wlx` or `mdview.wlx64` to a directory of your choice.
2. In Total Commander open **Configuration → Options → Plugins → Lister (WLX) → Add**.
3. Select the `.wlx` / `.wlx64` file.
4. The detect string auto-configures for `.md`, `.markdown`, `.mkd`, and `.mkdn` extensions.

## Usage

1. Navigate to any Markdown file in Total Commander.
2. Press `F3` to open the lister.
3. Use the keyboard shortcuts to customise the view. Preferences are saved automatically.

For Mermaid validation, use `test_mermaid.md`. It covers the Mermaid diagram families currently supported by MDView. For a broad Markdown regression sample, use `test.md`.

## Building from Source

The plugin is implemented in a single C source file and can be built natively on Windows with MSVC.

The raw Markdown view uses the built-in **RichEdit (Msftedit.dll)** control available on modern Windows systems.

## How It Works

MDView is a WLX lister plugin that Total Commander loads when you press `F3` on a matching file type. It contains a built-in Markdown-to-HTML converter and embeds an MSHTML WebBrowser control to render the output. Keyboard input is handled by subclassing the browser's internal window, giving reliable hotkey interception without interfering with normal scrolling or Total Commander key handling. The OLE control and its child window hierarchy are resized via `IOleInPlaceObject::SetObjectRects` and `MoveWindow` so the viewer fills the lister window at any size. Settings are persisted via the standard Total Commander INI mechanism.

## File List

| File              | Description                                                                                        |
| ----------------- | -------------------------------------------------------------------------------------------------- |
| `mdview.c`        | Complete plugin source                                                                             |
| `mdview.def`      | DLL export definitions                                                                             |
| `pluginst.inf`    | Total Commander auto-install manifest                                                              |
| `test.md`         | Broad Markdown regression and feature sample                                                       |
| `test_mermaid.md` | Dedicated Mermaid sample document covering the Mermaid diagram types currently supported by MDView |

## License

[MIT](LICENSE)
