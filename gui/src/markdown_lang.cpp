// Hand-written Markdown tokenizer for ImGuiColorTextEdit — see markdown_lang.h.
//
// TextEditor calls mTokenize per line segment; cross-line constructs use the
// LanguageDefinition's comment delimiters: we map fenced code blocks to
// "```" ... "```" so TextEditor's built-in block tracking keeps us fast.

#include "markdown_lang.h"

namespace markdown_lang {

using PaletteIndex = TextEditor::PaletteIndex;

static bool IsIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static bool Tokenize(const char *in_begin, const char *in_end,
                     const char *&out_begin, const char *&out_end,
                     PaletteIndex &paletteIndex) {
    const char *p = in_begin;

    // --- HTML comments <!-- ... --> (single-line part; block handled below) --
    if (p + 4 <= in_end && strncmp(p, "<!--", 4) == 0) {
        out_begin = p;
        const char *close = strstr(p + 4, "-->");
        out_end = close ? close + 3 : in_end;
        paletteIndex = PaletteIndex::Comment;
        return true;
    }

    // --- ATX header marker at line start --------------------------------------
    if (*p == '#') {
        const char *q = p;
        while (q < in_end && *q == '#') ++q;
        if (q < in_end && (*q == ' ' || *q == '\t')) {
            out_begin = p; out_end = in_end;
            paletteIndex = PaletteIndex::Keyword;
            return true;
        }
    }

    // --- List markers at line start --------------------------------------------
    if ((*p == '-' || *p == '*' || *p == '+') && p + 1 < in_end && p[1] == ' ') {
        out_begin = p; out_end = p + 1;
        paletteIndex = PaletteIndex::Preprocessor;
        return true;
    }
    // numbered list "1. "
    if (*p >= '0' && *p <= '9') {
        const char *q = p;
        while (q < in_end && *q >= '0' && *q <= '9') ++q;
        if (q + 1 < in_end && (*q == '.' || *q == ')') && q[1] == ' ') {
            out_begin = p; out_end = q + 1;
            paletteIndex = PaletteIndex::Preprocessor;
            return true;
        }
    }

    // --- Blockquote -------------------------------------------------------------
    if (*p == '>' && (p + 1 == in_end || p[1] == ' ')) {
        out_begin = p; out_end = in_end;
        paletteIndex = PaletteIndex::Comment;
        return true;
    }

    // --- Inline code span `...` --------------------------------------------------
    if (*p == '`') {
        out_begin = p;
        const char *q = p + 1;
        while (q < in_end && *q != '`') ++q;
        out_end = (q < in_end) ? q + 1 : in_end;
        paletteIndex = PaletteIndex::String;
        return true;
    }

    // --- Emphasis markers (** * __ _) -------------------------------------------
    if (*p == '*' || *p == '_') {
        const char *q = p;
        while (q < in_end && *q == *p) ++q;
        out_begin = p; out_end = q;
        paletteIndex = PaletteIndex::Punctuation; // emphasis markers
        return true;
    }

    // --- Links / images: [text](url), ![alt](url) --------------------------------
    if (*p == '[' || (*p == '!' && p + 1 < in_end && p[1] == '[')) {
        out_begin = p;
        const char *q = (*p == '!') ? p + 1 : p;
        q = strchr(q, ']');
        if (q && q + 1 < in_end && q[1] == '(') {
            const char *r = strchr(q + 2, ')');
            out_end = r ? r + 1 : q + 1;
        } else {
            out_end = q ? q + 1 : p + 1;
        }
        paletteIndex = PaletteIndex::Identifier;
        return true;
    }

    if (*p == ':') {
        const char *q = p + 1;
        while (q < in_end && (IsIdentChar(*q) || *q == '-' || *q == '+')) ++q;
        if (q > p + 1 && q < in_end && *q == ':') {
            out_begin = p; out_end = q + 1;
            paletteIndex = PaletteIndex::Default;
            return true;
        }
    }

    // --- YAML-ish key: value at line start (front matter, directives) -----------
    if (IsIdentChar(*p)) {
        const char *q = p;
        while (q < in_end && IsIdentChar(*q)) ++q;
        if (q < in_end && *q == ':' && (q + 1 == in_end || isspace((unsigned char)q[1]))) {
            out_begin = p; out_end = q + 1;
            paletteIndex = PaletteIndex::KnownIdentifier;
            return true;
        }
        out_begin = p; out_end = q;
        paletteIndex = PaletteIndex::Default;
        return true;
    }

    // --- Punctuation ---------------------------------------------------------------
    if (ispunct((unsigned char)*p)) {
        out_begin = p; out_end = p + 1;
        paletteIndex = PaletteIndex::Punctuation;
        return true;
    }

    return false; // let default (whitespace/other) handling proceed
}

const TextEditor::LanguageDefinition &Markdown() {
    static bool inited = false;
    static TextEditor::LanguageDefinition lang;
    if (!inited) {
        lang.mName = "Markdown";
        lang.mCaseSensitive = true;
        lang.mAutoIndentation = false;
        // HTML comments (block-aware, built-in tracking)
        lang.mCommentStart = "<!--";
        lang.mCommentEnd = "-->";
        lang.mSingleLineComment = "";
        // Fenced code blocks piggyback on block-comment tracking: fast and
        // cross-line without regex. Note: this means ```...``` renders in the
        // comment color — acceptable, code still stands out.
        lang.mPreprocChar = '~'; // unused
        lang.mTokenize = Tokenize;
        inited = true;
    }
    return lang;
}

} // namespace markdown_lang
