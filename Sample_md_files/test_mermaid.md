# Mermaid Support Test File

This document exercises every Mermaid diagram type currently supported by MDView.

The current built-in renderer supports:

- `graph` / `flowchart`
- `sequenceDiagram`
- `classDiagram`
- `stateDiagram` / `stateDiagram-v2`
- `erDiagram`
- `requirementDiagram`
- `gantt`
- `timeline`
- `gitGraph`
- `mindmap`
- `radar-beta`
- `treemap-beta`
- `venn-beta`
- `packet` / `packet-beta`
- `zenuml`
- `eventmodeling`
- `ishikawa-beta`

## Flowchart Top To Bottom

```mermaid
graph TD
    Start([Open file]) --> Parse[Parse markdown]
    Parse --> Choice{Mermaid block?}
    Choice -->|No| Render[Render regular HTML]
    Choice -->|Yes| Mermaid[Render Mermaid block]
    Mermaid --> Done([Show preview])
    Render --> Done
```

## Flowchart Left To Right

```mermaid
flowchart LR
    User[User] --> Plugin[MDView]
    Plugin --> Preview[Preview pane]
    Plugin -.-> Source[Raw markdown pane]
    Preview ==> Result[Readable output]
```

## Sequence Diagram

This sample intentionally mirrors the less common Mermaid fragment placed near the end of `test.md`.

```mermaid
sequenceDiagram
    Alice-->Bob: Hello Bob, how are you?
    Note right of Bob: Bob thinks
    Bob-->Alice: I am good thanks!
```

## Class Diagram

```mermaid
classDiagram
    class Document {
        +String path
        +load()
        +save()
    }
    class MarkdownDocument {
        +renderHtml()
        +renderMermaid()
    }
    class ViewerSettings {
        +fontSize
        +darkMode
    }
    Document <|-- MarkdownDocument
    MarkdownDocument --> ViewerSettings : uses
```

## State Diagram

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Loading : open file
    Loading --> Rendering : markdown parsed
    Rendering --> Viewing : preview ready
    Viewing --> Idle : close file
    Viewing --> [*]
```

## Entity Relationship

```mermaid
erDiagram
    direction LR
    person["Customer Account"] {
        int account_id PK
        string? email UK "Optional contact address"
    }
    PURCHASE {
        int account_id PK, FK
        int order_id PK
    }
    person ||--|{ PURCHASE : places
    PURCHASE }o..o| COUPON : applies
    person only one to zero or more COUPON : receives
    COUPON |o..|| person : "was sent to"
    "Archived records"
```

## Requirements

```mermaid
requirementDiagram
    direction TB
    requirement "Offline reading" {
        id: R-01
        text: The reader can open a document when no network connection is available.
        risk: medium
        verifymethod: test
    }
    functionalRequirement preview {
        id: R-01.1
        text: Render the local document.
        risk: low
        verifymethod: inspection
    }
    element viewer {
        type: component
        docref: "Design guide §3"
    }
    "Offline reading" - contains -> preview
    preview <- satisfies - viewer
    viewer - traces -> "Offline reading"
```

## Gantt

```mermaid
gantt
    title Preview release schedule
    dateFormat YYYY-MM-DD
    axisFormat %b %d
    todayMarker off
    excludes weekends, 2026-03-11
    section Planning
    Draft notes :done, draft, 2026-03-02, 2d
    Review scope :active, review, after draft, 3d
    section Verification
    Check images :images, after review, 2d
    Check keyboard shortcuts :crit, keys, after review, 3d
    Prepare ZIP :package, after images keys, until launch
    Publish sample ZIP :milestone, launch, 2026-03-17, 0d
```

## Timeline

```mermaid
timeline
    title How a document preview reaches the reader
    section Preparation
        Draft : Write the content : Add examples
        Review : Check formatting<br>and navigation
               : Verify diagrams
    section Release
        Package : Build the 32-bit and 64-bit plugins
        Publish : Share the ZIP and release notes
```

### Top-down timeline

```mermaid
timeline TD
    title Diagram rendering
    Parse : Read the source
    Layout : Place headings and events
    Paint : Draw the SVG : Fit it to the preview
```

## Git graph

```mermaid
gitGraph LR:
    commit id: "Start"
    branch feature
    commit id: "Preview" type: HIGHLIGHT
    branch "quick fix" order: 2
    commit id: "Fix links" type: REVERSE
    checkout main
    commit id: "Docs"
    cherry-pick id: "Fix links" tag: "backport"
    merge feature id: "Release" tag: "v3.9.3"
    commit
```

### Top-to-bottom Git graph

```mermaid
gitGraph TB:
    commit id: "Base"
    branch review
    commit id: "Review"
    checkout main
    merge review tag: "approved"
```

## Mind map

```mermaid
mindmap
    root((Markdown preview))
        source[Read the source]
            encoding(Detect the encoding)
            structure{{Parse Markdown}}
                Headings and links
        themes{{Apply a theme}}
            palette)Light and dark palettes(
            zoom))Adjust the text size((
        diagrams[Draw the diagram]
            Nodes and connectors<br/>with readable labels
            Fit the preview width
```

## Radar

```mermaid
---
title: "Preview checks"
---
radar-beta
    axis text["Text"], code["Code blocks"], img["Images"]
    axis links["Links"], diagrams["Diagrams"]
    curve baseline["Baseline"]{7, 8, 6, 7, 5}
    curve candidate["Candidate"]{img: 8, diagrams: 9, code: 9, links: 9, text: 8}
    max 10
    min 0
    ticks 5
    graticule polygon
```

## Treemap

```mermaid
treemap-beta
    title Distribution contents
    "Plugin binaries"
        "64-bit": 46
        "32-bit": 42
    "Supporting files"
        "Documentation"
            "Readme": 8
            "Examples": 17
        "Themes": 9
    "Screenshots": 32
```

## Venn

```mermaid
venn-beta
    title Review coverage
    set content["Content"]:20
        text headings["Headings"]
    set layout["Layout"]:18
        text spacing["Spacing"]
    set access["Accessibility"]:18
    union content,layout["Tables"]:5
    union layout,access["Contrast"]:5
    union content,access["Alt text"]:5
    union content,layout,access["Readable"]:2
```

## Packet

```mermaid
---
title: Preview cache record
config:
  packet:
    bitsPerRow: 16
    showBits: true
    bitOrder: descending
---
packet
    +4: "Version"
    4: "Valid"
    +3: "Flags"
    8-19: "Document ID"
    +12: "Data length"
```

## ZenUML

```mermaid
zenuml
    title Open a document
    @Actor R as Reader
    V as Preview
    @Database C as Cache
    R->V.openDocument(path) {
        if(cached) {
            entry = C.lookup(path)
            return entry
        } else {
            new Parser {
                Parser.read(path)
            }
            V->R: Preview ready
        }
    }
```

## Event Modeling

```mermaid
eventmodeling
    title Save a preview preference
    tf 01 ui Settings
    timeframe 02 command SavePreferences { palette: "dark" }
    tf 03 evt PreferencesSaved [[Saved]]
    rf 04 rmo Settings.CurrentPreferences ->> 03
    tf 05 ui Preview

    data Saved {
        palette: string
        savedAt: string
    }
```

## Ishikawa

```mermaid
ishikawa-beta
    Slow document preview
    Input
        Large file
        Many pictures
            High resolution
            Slow storage
    Layout
        Wide tables
        Nested Mermaid diagrams
    Settings
        Large type
        Word wrap
    Environment
        Busy system
        Slow display driver
```

## Notes

- Mermaid rendering is self-contained inside the plugin binaries.
- Unsupported Mermaid syntaxes should fall back to the original source block.
