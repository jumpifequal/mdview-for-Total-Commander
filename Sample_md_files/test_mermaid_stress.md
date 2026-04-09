# Mermaid Regression Stress Cases

This file is meant to exercise the currently supported Mermaid subsets in MDView with more demanding layouts.

## Flowchart TB Dense

```mermaid
flowchart TB
    A([Open project]) --> B[Scan markdown]
    B --> C{Contains diagram?}
    C -->|No| D[Render plain HTML]
    C -->|Yes| E[Pick renderer]
    E --> F[Flowchart parser]
    E --> G[Sequence parser]
    E --> H[Class parser]
    E --> I[State parser]
    F --> J[SVG output]
    G --> J
    H --> J
    I --> J
    J --> K([Paint preview])
    D --> K
```

## Flowchart LR Mixed Links

```mermaid
flowchart LR
    User[User] --> UI[Preview host]
    UI ==> Render[Rendered document]
    UI -.-> Source[Source buffer]
    Render --> Cache[Render cache]
    Source --> Parse[Markdown parse]
    Parse --> Render
```

## Sequence Diagram With Notes

```mermaid
sequenceDiagram
    participant User
    participant Host
    participant Parser
    participant Renderer
    User->>Host: Open markdown
    Host->>Parser: Parse document
    Note right of Parser: Build internal model
    Parser->>Renderer: Emit HTML + diagrams
    Renderer-->>Host: SVG fragments
    Host-->>User: Display preview
```

## Sequence Diagram Cross Traffic

```mermaid
sequenceDiagram
    participant A
    participant B
    participant C
    A->>B: Start
    B->>C: Validate
    C-->>B: OK
    B->>A: Continue
    Note left of A: UI remains responsive
```

## Class Diagram Dense Relations

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
    class MermaidRenderer {
        +renderFlow()
        +renderState()
    }
    Document <|-- MarkdownDocument
    MarkdownDocument --> ViewerSettings : uses
    MarkdownDocument --> MermaidRenderer : delegates
    MermaidRenderer --> ViewerSettings : reads
```

## Class Diagram Vertical Link

```mermaid
classDiagram
    class Session {
        +start()
        +stop()
    }
    class Cache {
        +put()
        +get()
    }
    class Storage {
        +open()
        +close()
    }
    Session --> Cache : keeps
    Cache --> Storage : persists
```

## State Diagram Linear Return

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Loading : open file
    Loading --> Rendering : markdown parsed
    Rendering --> Viewing : preview ready
    Viewing --> Idle : close file
    Viewing --> [*]
```

## State Diagram Retry Path

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Working : start
    Working --> Failed : exception
    Failed --> Working : retry
    Working --> Done : complete
    Done --> [*]
```

## State Diagram Long Return

```mermaid
stateDiagram-v2
    [*] --> Draft
    Draft --> Review : submit
    Review --> Approved : accept
    Review --> Draft : request changes
    Approved --> Published : publish
    Published --> [*]
```
