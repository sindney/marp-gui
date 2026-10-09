---
marp: true
theme: programmer
size: 16:9
paginate: true
---

<!-- _class: lead -->
<!-- _paginate: skip -->

# Hello, Marp

A clean starter deck using the **programmer** theme.

---

<!-- _header: Getting Started -->

## What is this?

- A **Marp** slide deck — markdown in, slides out
- Edit `slides.md` on the left, preview live on the right
- `Cmd+S` (macOS) / `Ctrl+S` (Windows) saves; the preview rebuilds automatically

> Press `Cmd+P` (macOS) / `Ctrl+P` (Windows) for commands. File → Export for PDF/PPTX/HTML.

---

<!-- _header: Code -->

## Code looks great too

```cpp
#include <cstdio>

int main() {
    std::printf("Hello, Marp!\n");
    return 0;
}
```

Inline code works as well: `marp slides.md --html`
