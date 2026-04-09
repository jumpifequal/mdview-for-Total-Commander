# MDView Test Document

This file exercises **all** features of the MDView lister plugin. Press **F1** for keyboard shortcuts.

## Text Formatting

This is a paragraph with **bold**, *italic*, ***bold italic***, and ~~strikethrough~~ text. Also `inline code`.

Here's a [link to Anthropic](https://www.anthropic.com) and an autolink: https://example.com

## Headings Hierarchy

### Third Level

#### Fourth Level

## Code Blocks with Syntax Highlighting

### JavaScript

```javascript
const greet = async (name) => {
    // This is a comment
    const message = `Hello, ${name}!`;
    console.log(message);
    return { status: 200, body: message };
};

/* Multi-line
   block comment */
for (let i = 0; i < 10; i++) {
    if (i % 2 === 0) {
        greet("World");
    }
}
```

### Python

```python
import os
from pathlib import Path

def fibonacci(n):
    """Calculate the nth Fibonacci number."""
    if n <= 1:
        return n
    a, b = 0, 1
    for _ in range(2, n + 1):
        a, b = b, a + b
    return b

# List comprehension
squares = [x ** 2 for x in range(10)]
result = fibonacci(42)
print(f"Result: {result}")
```

### SQL

```sql
SELECT u.name, COUNT(o.id) AS order_count,
       SUM(o.total) AS total_spent
FROM users u
LEFT JOIN orders o ON u.id = o.user_id
WHERE u.created_at >= '2024-01-01'
GROUP BY u.name
HAVING total_spent > 100
ORDER BY total_spent DESC
LIMIT 20;
```

### C

```c
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    char* name;
    int   age;
} Person;

int main(int argc, char* argv[]) {
    Person* p = malloc(sizeof(Person));
    if (!p) return 1;
    p->name = "Alice";
    p->age = 30;
    printf("Name: %s, Age: %d\n", p->name, p->age);
    free(p);
    return 0;
}
```

### Bash

```bash
#!/bin/bash

# Deploy script
export APP_ENV="production"

for service in api worker scheduler; do
    echo "Deploying $service..."
    if docker-compose up -d "$service"; then
        echo "Success: $service is running"
    else
        echo "Failed to deploy $service" >&2
        exit 1
    fi
done
```

### HTML

```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <title>Example Page</title>
    <!-- This is a comment -->
    <link rel="stylesheet" href="styles.css">
</head>
<body>
    <div class="container" id="main">
        <h1>Hello World</h1>
        <p data-count="42">Welcome!</p>
    </div>
</body>
</html>
```

## Long Code Block (Expand/Collapse Test)

```javascript
// This is a deliberately long code block to test the expand/collapse feature.
// It should show a "Show more" button when collapsed.
function processData(input) {
    var results = [];
    for (var i = 0; i < input.length; i++) {
        var item = input[i];
        if (item.type === 'string') {
            results.push(item.value.toUpperCase());
        } else if (item.type === 'number') {
            results.push(item.value * 2);
        } else if (item.type === 'boolean') {
            results.push(!item.value);
        } else if (item.type === 'array') {
            results.push(item.value.reverse());
        } else if (item.type === 'object') {
            var keys = [];
            for (var key in item.value) {
                if (item.value.hasOwnProperty(key)) {
                    keys.push(key);
                }
            }
            results.push(keys);
        } else {
            results.push(null);
        }
    }
    return results;
}

function validateInput(data) {
    if (!data) return false;
    if (!Array.isArray(data)) return false;
    for (var i = 0; i < data.length; i++) {
        if (!data[i].type) return false;
        if (data[i].value === undefined) return false;
    }
    return true;
}

var testData = [
    { type: 'string', value: 'hello' },
    { type: 'number', value: 21 },
    { type: 'boolean', value: true },
    { type: 'string', value: 'world' },
    { type: 'number', value: 7 }
];

if (validateInput(testData)) {
    var output = processData(testData);
    console.log('Processed:', output);
}
```

## Blockquote

> This is a blockquote. It can contain **formatting** and `code`.
> 
> It can also span multiple paragraphs.
> 
> > And be nested.

## Table

| Language   | Type        | Year | Creator          |
|:---------- |:-----------:| ----:| ---------------- |
| C          | Compiled    | 1972 | Dennis Ritchie   |
| Python     | Interpreted | 1991 | Guido van Rossum |
| JavaScript | JIT         | 1995 | Brendan Eich     |
| Rust       | Compiled    | 2010 | Graydon Hoare    |

## Lists

### Unordered

- First item
- Second item with **bold**
  - Nested item
  - Another nested
- Third item

### Ordered

1. Step one
2. Step two
3. Step three

### Task List

- [x] Implement markdown parser
- [x] Add syntax highlighting
- [x] Add line numbers
- [ ] World domination

---

Mermaid

```mermaid
sequenceDiagram
    Alice-->Bob: Hello Bob, how are you?
    Note right of Bob: Bob thinks
    Bob-->Alice: I am good thanks!
```

---

*End of test document. Try the keyboard shortcuts!*
