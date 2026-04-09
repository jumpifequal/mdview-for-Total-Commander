# Complete Demonstration of Markdown Syntax Features

This document showcases all common Markdown syntax features and can be directly saved as a `.md` file to view the rendering effect.

---

## 1. Headings

Markdown supports six levels of headings, using the `#` symbol, where the number of `#` corresponds to the heading level:

# Heading Level 1 (Largest)
## Heading Level 2
### Heading Level 3
#### Heading Level 4
##### Heading Level 5
###### Heading Level 6 (Smallest)

---

## 2. Paragraphs and Line Breaks

Separate paragraphs with a blank line.  
Add two spaces at the end of a line followed by a newline to create a **soft line break** within the same paragraph.

This is the first paragraph.  
It continues on a new line within the same paragraph.

This is the second paragraph, separated from the previous one by a blank line.

---

## 3. Text Styling

- **Bold**: `**bold text**` → **bold text**
- *Italic*: `*italic text*` → *italic text*
- ***Bold Italic***: `***bold italic text***` → ***bold italic text***
- ~~Strikethrough~~: `~~strikethrough text~~` → ~~strikethrough text~~
- `Inline code`: `` `inline code` `` → `inline code`
- <u>Underline</u>: `<u>underlined text</u>` → <u>underlined text</u> (supported by some parsers)
- ==Highlight==: `==highlighted text==` → ==highlighted text== (supported by some parsers)

---

## 4. Blockquotes

Use the `>` symbol to create blockquotes, which can be nested:

> This is a level 1 blockquote.
>
> > This is a nested level 2 blockquote.
> >
> > > This is a nested level 3 blockquote.

---

## 5. Lists

### 5.1 Unordered Lists

Use `-`, `+`, or `*` at the beginning:

- Item One
- Item Two
  - Sub-item 2.1
  - Sub-item 2.2
- Item Three

### 5.2 Ordered Lists

Use numbers followed by a `.`:

1. First step
2. Second step
3. Third step
   1. Sub-step 3.1
   2. Sub-step 3.2

### 5.3 Task Lists

Use `- [ ]` for incomplete items and `- [x]` for completed ones:

- [x] Complete document structure design
- [x] Write basic syntax examples
- [ ] Add advanced feature demonstrations
- [ ] Final proofreading and optimization

---

## 6. Code Blocks

### 6.1 Inline Code

Wrap with `` ` ``: `print("Hello Markdown!")`

### 6.2 Code Blocks (with Syntax Highlighting)

Wrap with three backticks ```, optionally specifying the language:

```python
def fibonacci(n):
    """Calculate the Fibonacci sequence."""
    if n <= 1:
        return n
    a, b = 0, 1
    for _ in range(2, n + 1):
        a, b = b, a + b
    return b

# Test
for i in range(10):
    print(f"fib({i}) = {fibonacci(i)}")
```

```javascript
// JavaScript example
const greet = (name) => {
  console.log(`Hello, ${name}!`);
};
greet('Markdown User');
```

```bash
# Bash command example
echo "Current directory: $(pwd)"
ls -l | grep ".md"
```

---

## 7. Tables

Use `|` to separate columns and `---` to separate the header from the content. Alignment can be specified:

| Name   | Age | Occupation      | City     | Rating |
| :----- | :-: | :-------------- | :------- | :----: |
| Zhang San | 28  | Software Engineer | Beijing  |  4.8   |
| Li Si     | 32  | Product Manager   | Shanghai |  4.5   |
| Wang Wu   | 25  | Designer          | Shenzhen |  4.9   |
| Zhao Liu  | 30  | Data Analyst      | Hangzhou |  4.7   |

> Alignment guide: `:---` left-aligned, `---:` right-aligned, `:---:` centered.

---

## 8. Links and Images

### 8.1 Hyperlinks

- Inline: `URL "optional title"`  
  Example: https://github.com "Visit GitHub"

- Reference-style:  
  [Markdown Guide][md-guide]  
  [MDN Web Docs][mdn-docs]  

  [md-guide]: https://www.markdownguide.org "Official Markdown Guide"
  [mdn-docs]: https://developer.mozilla.org "MDN Developer Documentation"

### 8.2 Images

- Inline: `!image_url "optional title"`  
  ![icon256](https://markdown-here.com/img/icon256.png) "Markdown Icon online image"  
  ![icon256](Icon/icon256.png) "Markdown Icon local image"  
  
- Reference-style:  
  ![Sample Image][sample-img]

  [sample-img]: https://placehold.co/150x150.png "Placeholder Image"

### 8.3 Linked Images

Use an image as the link content:

[![icon256](https://markdown-here.com/img/icon256.png "Shiprock")](https://www.markdownguide.org)

---

## 9. Horizontal Rules

Use three or more `*`, `-`, or `_` characters, optionally with spaces:

***

---

___

---

## 10. Escaping Characters

Use `\` to escape special characters, making them display as plain text:

- Asterisk: `\*not italic\*`
- Underscore: `\_not underlined\_`
- Backtick: `` \`not code\` ``
- Square brackets: `\[not a link\]`
- Parentheses: `\(not a link\)`

Display result:  
\*not italic\*  
\_not underlined\_  
\`not code\`  
\[not a link\]  
\(not a link\)

---

## 11. Embedded HTML

Markdown supports embedded HTML tags for richer styling:

<p style="color: #2c3e50; font-size: 18px; line-height: 1.6;">
  This is a paragraph embedded via HTML, allowing custom colors, font sizes, and other styles.
</p>

<div style="background-color: #f8f9fa; padding: 15px; border-radius: 5px; margin: 20px 0;">
  <h4 style="margin-top: 0;">HTML Info Box</h4>
  <p>The HTML div tag can create styled containers, which are difficult to achieve with pure Markdown.</p>
</div>

<kbd>Ctrl</kbd> + <kbd>C</kbd> to copy, <kbd>Ctrl</kbd> + <kbd>V</kbd> to paste

---

## 12. Mathematical Formulas (Supported by Some Parsers)

Use `$` for inline formulas and `$$` for block formulas:

- Inline formula: `$E = mc^2$` → $E = mc^2$
- Block formula:

$$
\sum_{i=1}^{n} i = \frac{n(n+1)}{2}
$$

$$
\int_{a}^{b} f(x) \, dx = F(b) - F(a)
$$

---

## 13. Footnotes

Define footnotes using `[^label]` and explain them at the bottom or a designated location:

Here is an example with a footnote[^note1].

[^note1]: This is the detailed content of the footnote, which can span multiple lines and include links.

---

## 14. Table of Contents

Use `[TOC]` to automatically generate a table of contents (supported by some parsers like Typora and VS Code extensions):

[TOC]

---

## 15. Emoji

Some parsers support emoji shortcodes (e.g., :smile:):

:smile: :heart: :thumbsup: :rocket: :book: :computer: :coffee: :muscle:

---

## 16. Collapsible Content (Supported by Some Parsers)

Create collapsible sections using `<details>` and `<summary>` tags:

<details>
  <summary>Click to expand for details</summary>
  <p>This is the hidden detailed content, which can contain any Markdown elements.</p>
  <ul>
    <li>Hidden item 1</li>
    <li>Hidden item 2</li>
  </ul>
</details>

---

## 17. Combined Example

The following is a comprehensive example applying various syntaxes:

> **Note**: This section demonstrates how to combine headings, lists, code, links, and more.

### Key Features

1. **Cross-platform Compatibility**: Renders correctly on GitHub, GitLab, VS Code, etc.
2. **Lightweight & Efficient**: Plain text format, friendly to version control.
3. **Extensible**: Supports more advanced features through HTML and plugins.

### Quick Start

```bash
# Clone the repository
git clone https://github.com/example/markdown-demo.git

# Enter the directory
cd markdown-demo

# Install dependencies
npm install
```

For more usage, please refer to the [official documentation][doc-link].

[doc-link]: https://www.markdownguide.org/getting-started "Markdown Getting Started Guide"

---

## 18. Summary

This document covers the core syntax features of Markdown, including:

- Headings, paragraphs, and text styling
- Blockquotes, lists, and task lists
- Code blocks and tables
- Links, images, and escaping characters
- Embedded HTML, mathematical formulas, and footnotes
- Table of contents, emojis, and collapsible content

Mastering these syntaxes will enable you to write well-structured and richly formatted documents efficiently.

---

> Last Updated: 2026-02-26  
> Author: Markdown Learner  
> License: CC BY-NC-SA 4.0
