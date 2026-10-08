// Hand-written Markdown language definition for ImGuiColorTextEdit.
//
// The stock std::regex-based highlighters in ImGuiColorTextEdit are
// documented as disappointingly slow (only C/C++ got a fast tokenizer).
// This follows the C++ tokenizer pattern: a plain per-line scanner, no regex.
//
// Covered: ATX headers, emphasis/code span markers, fenced code blocks,
// links, HTML comments, YAML front matter, list markers.

#pragma once

#include "TextEditor.h"

namespace markdown_lang {

const TextEditor::LanguageDefinition &Markdown();

} // namespace markdown_lang
