// Native Markdown highlighting: bounded byte scanning, no allocations or regex.
#include "markdown_lang.h"

namespace markdown_lang {
namespace {
using PaletteIndex = TextEditor::PaletteIndex;

static bool IsIdent(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}
static bool IsSpace(unsigned char c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}
static bool IsPunctuation(unsigned char c) {
    return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') ||
           (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

struct ByteView {
    const char* bytes;
    size_t size;
    unsigned char operator[](size_t i) const { return (unsigned char)bytes[i]; }
};
struct GlyphView {
    const TextEditor::Line& glyphs;
    size_t size;
    unsigned char operator[](size_t i) const { return glyphs[i].mChar; }
};
struct Token {
    size_t first, end;
    PaletteIndex color;
};

// Same scanner serves the editor's glyph buffer and the legacy token callback.
// Closing-bracket limits are computed once per line, when a link is encountered.
// Missing ] or ) then costs a constant-time check rather than repeated suffix
// searches (e.g. while typing a long line of unfinished links).
template<class Text> class Scanner {
public:
    explicit Scanner(Text text) : mText(text) {}

    Token Next(size_t first) {
        const size_t size = mText.size;
        const unsigned char c = mText[first];
        auto token = [&](size_t end, PaletteIndex color) { return Token{first, end, color}; };
        size_t q;
        switch (c) {
        case '<':
            if (size - first >= 4 && mText[first + 1] == '!' &&
                mText[first + 2] == '-' && mText[first + 3] == '-') {
                q = first + 4;
                while (size - q >= 3 && !(mText[q] == '-' && mText[q + 1] == '-' && mText[q + 2] == '>')) ++q;
                return token(size - q >= 3 ? q + 3 : size, PaletteIndex::Comment);
            }
            break;
        case '#':
            q = first;
            while (q < size && mText[q] == '#') ++q;
            if (q < size && (mText[q] == ' ' || mText[q] == '\t'))
                return token(size, PaletteIndex::Keyword);
            return token(q, PaletteIndex::Punctuation);
        case '-': case '+': case '*':
            if (size - first > 1 && mText[first + 1] == ' ')
                return token(first + 1, PaletteIndex::Preprocessor);
            break;
        case '>':
            if (size - first == 1 || mText[first + 1] == ' ')
                return token(size, PaletteIndex::Comment);
            break;
        case '`':
            q = first + 1;
            while (q < size && mText[q] != '`') ++q;
            return token(q < size ? q + 1 : size, PaletteIndex::String);
        case ':':
            q = first + 1;
            while (q < size && (IsIdent(mText[q]) || mText[q] == '-' || mText[q] == '+')) ++q;
            if (q > first + 1 && q < size && mText[q] == ':')
                return token(q + 1, PaletteIndex::Default);
            break;
        }
        if (c >= '0' && c <= '9') {
            q = first;
            while (q < size && mText[q] >= '0' && mText[q] <= '9') ++q;
            if (size - q > 1 && (mText[q] == '.' || mText[q] == ')') && mText[q + 1] == ' ')
                return token(q + 1, PaletteIndex::Preprocessor);
        }
        if (c == '*' || c == '_') {
            q = first;
            while (q < size && mText[q] == c) ++q;
            return token(q, PaletteIndex::Punctuation);
        }
        if (c == '[' || (c == '!' && size - first > 1 && mText[first + 1] == '[')) {
            FindLinkLimits();
            q = first + (c == '!' ? 1 : 0);
            if (!mHasBracket || q >= mLastBracket)
                return token(first + 1, PaletteIndex::Identifier);
            while (mText[q] != ']') ++q; // bounded by mLastBracket
            if (size - q > 1 && mText[q + 1] == '(' && mHasParen && q + 2 <= mLastParen) {
                size_t end = q + 2;
                while (mText[end] != ')') ++end; // bounded by mLastParen
                return token(end + 1, PaletteIndex::Identifier);
            }
            return token(q + 1, PaletteIndex::Identifier);
        }
        if (IsIdent(c)) {
            q = first;
            while (q < size && IsIdent(mText[q])) ++q;
            if (q < size && mText[q] == ':' && (q + 1 == size || IsSpace(mText[q + 1])))
                return token(q + 1, PaletteIndex::KnownIdentifier);
            return token(q, PaletteIndex::Default);
        }
        if (IsPunctuation(c)) return token(first + 1, PaletteIndex::Punctuation);
        // Consume UTF-8 prose and whitespace together. ASCII delimiters cannot
        // occur inside UTF-8 sequences, so no decoding or locale lookup is needed.
        q = first + 1;
        while (q < size && !IsIdent(mText[q]) && !IsPunctuation(mText[q])) ++q;
        return token(q, PaletteIndex::Default);
    }

private:
    void FindLinkLimits() {
        if (mLinkLimitsKnown) return;
        mLinkLimitsKnown = true;
        for (size_t i = mText.size; i > 0;) {
            --i;
            if (!mHasBracket && mText[i] == ']') { mLastBracket = i; mHasBracket = true; }
            if (!mHasParen && mText[i] == ')') { mLastParen = i; mHasParen = true; }
            if (mHasBracket && mHasParen) break;
        }
    }
    Text mText;
    bool mLinkLimitsKnown = false, mHasBracket = false, mHasParen = false;
    size_t mLastBracket = 0, mLastParen = 0;
};

static bool Tokenize(const char* begin, const char* end, const char*& tokenBegin,
                     const char*& tokenEnd, PaletteIndex& color) {
    if (begin == end) return false;
    Scanner<ByteView> scanner({begin, (size_t)(end - begin)});
    Token token = scanner.Next(0);
    tokenBegin = begin + token.first;
    tokenEnd = begin + token.end;
    color = token.color;
    return true;
}

static void ColorizeLine(TextEditor::Line& line) {
    Scanner<GlyphView> scanner({line, line.size()});
    for (size_t first = 0; first < line.size();) {
        Token token = scanner.Next(first);
        for (size_t i = first; i < token.end; ++i) line[i].mColorIndex = token.color;
        first = token.end;
    }
}
} // namespace

const TextEditor::LanguageDefinition& Markdown() {
    static const TextEditor::LanguageDefinition lang = [] {
        TextEditor::LanguageDefinition definition;
        definition.mName = "Markdown";
        definition.mAutoIndentation = false;
        definition.mCommentStart = "<!--";
        definition.mCommentEnd = "-->";
        definition.mPreprocChar = '~';
        definition.mTokenize = Tokenize;
        definition.mColorizeLine = ColorizeLine;
        return definition;
    }();
    return lang;
}
} // namespace markdown_lang
