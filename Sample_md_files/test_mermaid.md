# Mermaid Support Test File

This document exercises every Mermaid diagram type currently supported by MDView.

The current built-in renderer supports:

- `graph` / `flowchart`
- `sequenceDiagram`
- `classDiagram`
- `stateDiagram-v2`

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

## Notes

- Mermaid rendering is self-contained inside the plugin binaries.
- Unsupported Mermaid syntaxes should fall back to the original source block.
