#include "SymbolFinder.h"

#include <QSet>
#include <QVector>

namespace {

struct Tok {
    int pos;
    int len;
    char type;    // 'i' identifier, 'n' number, 'p' punctuation, 'b' boundary (a preprocessor line)
    ushort ch;    // punctuation character; "::" is ':' with len 2, "->" is ARROW
};
const ushort ARROW = 0x2192;

struct Macro {
    int pos;
    int len;
};

struct File {
    QString masked;         // the text with comments and string contents blanked (same offsets)
    QVector<Tok> toks;
    QVector<int> match;     // matching bracket of ( ) [ ] { }, or -1
    QVector<int> block;     // the '{' token that encloses this token, or -1 at file level
    QVector<Macro> macros;  // #define names
};

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

// Blank out comments, string and character literals, keeping every offset and newline.
QString mask(const QString &text)
{
    QString out = text;
    const int n = text.size();
    auto blank = [&out](int from, int to) {
        for (int k = from; k < to; ++k) {
            if (out.at(k) != QLatin1Char('\n'))
                out[k] = QLatin1Char(' ');
        }
    };
    int i = 0;
    while (i < n) {
        const QChar c = text.at(i);
        const QChar d = i + 1 < n ? text.at(i + 1) : QChar();
        if (c == QLatin1Char('/') && d == QLatin1Char('/')) {
            int end = text.indexOf(QLatin1Char('\n'), i);
            if (end < 0)
                end = n;
            blank(i, end);
            i = end;
        } else if (c == QLatin1Char('/') && d == QLatin1Char('*')) {
            int end = text.indexOf(QLatin1String("*/"), i + 2);
            end = end < 0 ? n : end + 2;
            blank(i, end);
            i = end;
        } else if (c == QLatin1Char('R') && d == QLatin1Char('"') && (i == 0 || !isWordChar(text.at(i - 1)))) {
            // Raw string R"delim( ... )delim"
            const int open = text.indexOf(QLatin1Char('('), i + 2);
            if (open < 0 || open - (i + 2) > 16) {
                ++i;
                continue;
            }
            const QString close = QLatin1Char(')') + text.mid(i + 2, open - (i + 2)) + QLatin1Char('"');
            int end = text.indexOf(close, open + 1);
            end = end < 0 ? n : end + close.size();
            blank(i, end);
            i = end;
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            int k = i + 1;
            while (k < n && text.at(k) != c && text.at(k) != QLatin1Char('\n')) {
                if (text.at(k) == QLatin1Char('\\') && k + 1 < n)
                    ++k;
                ++k;
            }
            const int end = (k < n && text.at(k) == c) ? k + 1 : k;
            blank(i + 1, qMax(i + 1, end - 1));   // keep the quotes as harmless punctuation
            out[i] = QLatin1Char(' ');
            if (end - 1 > i)
                out[end - 1] = QLatin1Char(' ');
            i = end;
        } else {
            ++i;
        }
    }
    return out;
}

File parse(const QString &text)
{
    File f;
    f.masked = mask(text);
    const QString &s = f.masked;
    const int n = s.size();
    bool lineStart = true;
    int i = 0;
    while (i < n) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('\n')) {
            lineStart = true;
            ++i;
        } else if (c.isSpace()) {
            ++i;
        } else if (c == QLatin1Char('#') && lineStart) {
            // Preprocessor line (with "\" continuations): one boundary token; remember #define names.
            int end = i;
            for (;;) {
                end = s.indexOf(QLatin1Char('\n'), end);
                if (end < 0) {
                    end = n;
                    break;
                }
                int back = end - 1;
                while (back > i && (s.at(back) == QLatin1Char('\r') || s.at(back) == QLatin1Char(' ')))
                    --back;
                if (text.at(back) != QLatin1Char('\\'))
                    break;
                ++end;
            }
            int k = i + 1;
            while (k < end && s.at(k).isSpace())
                ++k;
            if (s.midRef(k, 6) == QLatin1String("define") && k + 6 < end && s.at(k + 6).isSpace()) {
                k += 6;
                while (k < end && s.at(k).isSpace())
                    ++k;
                int e = k;
                while (e < end && isWordChar(s.at(e)))
                    ++e;
                if (e > k)
                    f.macros.append({k, e - k});
            }
            f.toks.append({i, end - i, 'b', 0});
            i = end;
        } else if (c.isLetter() || c == QLatin1Char('_')) {
            int e = i + 1;
            while (e < n && isWordChar(s.at(e)))
                ++e;
            f.toks.append({i, e - i, 'i', 0});
            i = e;
            lineStart = false;
        } else if (c.isDigit()) {
            int e = i + 1;
            while (e < n && (isWordChar(s.at(e)) || s.at(e) == QLatin1Char('.')))
                ++e;
            f.toks.append({i, e - i, 'n', 0});
            i = e;
            lineStart = false;
        } else {
            const QChar d = i + 1 < n ? s.at(i + 1) : QChar();
            if (c == QLatin1Char(':') && d == QLatin1Char(':')) {
                f.toks.append({i, 2, 'p', ':'});
                i += 2;
            } else if (c == QLatin1Char('-') && d == QLatin1Char('>')) {
                f.toks.append({i, 2, 'p', ARROW});
                i += 2;
            } else {
                f.toks.append({i, 1, 'p', c.unicode()});
                ++i;
            }
            lineStart = false;
        }
    }

    // Bracket matching and enclosing blocks.
    const int count = f.toks.size();
    f.match.fill(-1, count);
    f.block.fill(-1, count);
    QVector<int> open, braces;
    for (int t = 0; t < count; ++t) {
        const Tok &tok = f.toks.at(t);
        const ushort ch = tok.type == 'p' && tok.len == 1 ? tok.ch : 0;
        if (ch == '}' && !braces.isEmpty())
            braces.removeLast();
        f.block[t] = braces.isEmpty() ? -1 : braces.last();
        if (ch == '{')
            braces.append(t);
        if (ch == '(' || ch == '[' || ch == '{') {
            open.append(t);
        } else if (ch == ')' || ch == ']' || ch == '}') {
            const ushort want = ch == ')' ? '(' : ch == ']' ? '[' : '{';
            int k = open.size() - 1;
            while (k >= 0 && f.toks.at(open.at(k)).ch != want)
                --k;
            if (k >= 0) {
                f.match[t] = open.at(k);
                f.match[open.at(k)] = t;
                open.resize(k);
            }
        }
    }
    return f;
}

bool isP(const File &f, int j, ushort ch, int len = 1)
{
    if (j < 0 || j >= f.toks.size())
        return false;
    const Tok &t = f.toks.at(j);
    return t.type == 'p' && t.ch == ch && t.len == len;
}

bool isIdent(const File &f, int j)
{
    return j >= 0 && j < f.toks.size() && f.toks.at(j).type == 'i';
}

QStringRef word(const File &f, int j)
{
    return f.masked.midRef(f.toks.at(j).pos, f.toks.at(j).len);
}

// Words that can stand right before a name without making it a declaration ("return x").
bool isRejectWord(const QStringRef &w)
{
    static const QSet<QString> words{
        QStringLiteral("return"), QStringLiteral("else"), QStringLiteral("case"), QStringLiteral("goto"),
        QStringLiteral("new"), QStringLiteral("delete"), QStringLiteral("throw"), QStringLiteral("sizeof"),
        QStringLiteral("do"), QStringLiteral("using"), QStringLiteral("namespace"), QStringLiteral("operator"),
        QStringLiteral("if"), QStringLiteral("while"), QStringLiteral("for"), QStringLiteral("switch"),
        QStringLiteral("break"), QStringLiteral("continue"), QStringLiteral("default"), QStringLiteral("public"),
        QStringLiteral("private"), QStringLiteral("protected"), QStringLiteral("template"), QStringLiteral("and"),
        QStringLiteral("or"), QStringLiteral("not"), QStringLiteral("typename"), QStringLiteral("catch"),
        QStringLiteral("try"), QStringLiteral("this"), QStringLiteral("true"), QStringLiteral("false")};
    return words.contains(w.toString());
}

bool isTagWord(const QStringRef &w)
{
    return w == QLatin1String("struct") || w == QLatin1String("class") || w == QLatin1String("union")
        || w == QLatin1String("enum");
}

// The '(' that the token is inside of, or -1. Stops at a statement end unless told not to
// (the parts of a for(...) header are separated by ';').
int enclosingParen(const File &f, int i, bool stopAtSemicolon = true)
{
    for (int k = i - 1; k >= 0; --k) {
        const Tok &t = f.toks.at(k);
        if (t.type == 'b')
            return -1;
        if (t.type != 'p' || t.len != 1)
            continue;
        if (t.ch == ')' || t.ch == ']' || t.ch == '}') {
            if (f.match.at(k) < 0)
                return -1;
            k = f.match.at(k);
        } else if (t.ch == '(') {
            return k;
        } else if (t.ch == '{' || (t.ch == ';' && stopAtSemicolon)) {
            return -1;
        }
    }
    return -1;
}

// "type name(" : the '(' of a function definition or prototype.
bool isFunctionHeaderParen(const File &f, int open)
{
    const int h = open - 1;
    if (!isIdent(f, h) || isRejectWord(word(f, h)))
        return false;
    const int p = h - 1;
    if (isIdent(f, p))
        return !isRejectWord(word(f, p));
    return isP(f, p, '*') || isP(f, p, '&') || isP(f, p, '>') || isP(f, p, ':', 2);
}

// The '{' of the body that follows the ')' matching this '(' (function, for, while, if, catch), or -1.
int bodyAfterParen(const File &f, int open)
{
    const int close = f.match.at(open);
    if (close < 0)
        return -1;
    int k = close + 1;
    while (isIdent(f, k))   // const, override, noexcept...
        ++k;
    if (isP(f, k, '{'))
        return k;
    if (isP(f, k, ':')) {   // constructor: Foo(int a) : m_a(a) { ... }
        for (int steps = 0; k < f.toks.size() && steps < 200; ++k, ++steps) {
            if (isP(f, k, '(') && f.match.at(k) > k)
                k = f.match.at(k);
            else if (isP(f, k, '{'))
                return k;
            else if (isP(f, k, ';'))
                return -1;
        }
    }
    return -1;
}

// The '<' matching a template's closing '>' at j, or -1.
int templateOpen(const File &f, int j)
{
    int depth = 0;
    for (int k = j, steps = 0; k >= 0 && steps < 64; --k, ++steps) {
        const Tok &t = f.toks.at(k);
        if (t.type == 'b')
            return -1;
        if (t.type != 'p' || t.len != 1)
            continue;
        if (t.ch == '>')
            ++depth;
        else if (t.ch == '<' && --depth == 0)
            return k;
        else if (t.ch == ';' || t.ch == '{' || t.ch == '}')
            return -1;
    }
    return -1;
}

// Is this '{' the body of an enum?
bool isEnumBrace(const File &f, int brace)
{
    for (int k = brace - 1, steps = 0; k >= 0 && steps < 6; --k, ++steps) {
        if (isIdent(f, k)) {
            if (word(f, k) == QLatin1String("enum"))
                return true;
        } else if (!isP(f, k, ':')) {
            return false;
        }
    }
    return false;
}

// "int a = 1, b;" : is the ',' at token `comma` part of a declaration list?
bool isDeclarationComma(const File &f, int comma)
{
    int start = 0;
    for (int k = comma - 1; k >= 0; --k) {
        const Tok &t = f.toks.at(k);
        if (t.type == 'b') {
            start = k + 1;
            break;
        }
        if (t.type != 'p' || t.len != 1)
            continue;
        if (t.ch == ')' || t.ch == ']') {
            if (f.match.at(k) < 0)
                return false;
            k = f.match.at(k);
        } else if (t.ch == '}') {
            // "= {1, 2}" is part of the statement; any other '}' ends the statement before it.
            const int m = f.match.at(k);
            if (m > 0 && (isP(f, m - 1, '=') || isP(f, m - 1, ',') || isP(f, m - 1, '{') || isP(f, m - 1, ']'))) {
                k = m;
            } else {
                start = k + 1;
                break;
            }
        } else if (t.ch == '(') {
            if (k > 0 && isIdent(f, k - 1) && word(f, k - 1) == QLatin1String("for")) {
                start = k + 1;   // for (int i = 0, n = 5; ...)
                break;
            }
            return false;        // a call's arguments
        } else if (t.ch == ';' || t.ch == '{') {
            start = k + 1;
            break;
        }
    }
    // The statement must begin like a declaration: a type word, then a name (or * & :: <).
    if (!isIdent(f, start) || isRejectWord(word(f, start)))
        return false;
    const int second = start + 1;
    return isIdent(f, second) || isP(f, second, '*') || isP(f, second, '&') || isP(f, second, ':', 2)
        || isP(f, second, '<');
}

// Where a declaration at token i can be seen from: the '{' token of its block, -1 for the
// whole sketch, -2 for nowhere (a prototype's parameter names).
int scopeOf(const File &f, int i)
{
    const int open = enclosingParen(f, i);
    if (open >= 0) {
        const int body = bodyAfterParen(f, open);
        if (body >= 0)
            return body;   // parameter, or for (int i...) { }
        if (isFunctionHeaderParen(f, open))
            return -2;
    }
    return f.block.at(i);
}

// Is the identifier token i a declaration of its name? Fills kind and scope.
bool classify(const File &f, int i, SymbolFinder::Kind *kind, int *scope)
{
    using SymbolFinder::Kind;
    const int next = i + 1;

    // struct Name { ... }   class Name : Base   enum Name { ... }
    if (isIdent(f, i - 1) && isTagWord(word(f, i - 1))) {
        if (!isP(f, next, '{') && !isP(f, next, ':'))
            return false;   // "struct Name x;" only uses the name
        *kind = Kind::Type;
        *scope = f.block.at(i);
        return true;
    }
    // typedef struct { ... } Name;      struct { ... } name;
    if (isP(f, i - 1, '}') && (isP(f, next, ';') || isP(f, next, ','))) {
        const int open = f.match.at(i - 1);
        bool tag = false, isTypedef = false;
        for (int k = open - 1, steps = 0; open >= 0 && isIdent(f, k) && steps < 4; --k, ++steps) {
            tag = tag || isTagWord(word(f, k));
            isTypedef = isTypedef || word(f, k) == QLatin1String("typedef");
        }
        if (!tag)
            return false;
        *kind = isTypedef ? Kind::Type : Kind::Variable;
        *scope = f.block.at(i);
        return true;
    }

    // Walk back over "* &", then over the type's words.
    int j = i - 1;
    bool pointer = false;
    while (isP(f, j, '*') || isP(f, j, '&')) {
        pointer = true;
        --j;
    }
    int typeWords = 0;
    bool isExtern = false, isTypedef = false;
    while (j >= 0) {
        if (isIdent(f, j)) {
            const QStringRef w = word(f, j);
            if (isRejectWord(w))
                return false;
            isExtern = isExtern || w == QLatin1String("extern");
            isTypedef = isTypedef || w == QLatin1String("typedef");
            ++typeWords;
            --j;
        } else if (isP(f, j, ':', 2)) {
            --j;
        } else if (isP(f, j, '>')) {
            const int open = templateOpen(f, j);
            if (open <= 0 || !isIdent(f, open - 1))
                return false;
            j = open - 1;
        } else {
            break;
        }
    }

    if (typeWords == 0) {
        if (!pointer && (isP(f, j, ',') || isP(f, j, '{'))) {
            const int brace = f.block.at(i);
            if (brace >= 0 && isEnumBrace(f, brace)) {
                *kind = Kind::EnumConstant;
                *scope = f.block.at(brace);   // plain enum constants belong to the scope around the enum
                return true;
            }
        }
        if (isP(f, j, ',') && isDeclarationComma(f, j)) {
            *kind = Kind::Variable;
            *scope = scopeOf(f, i);
            return true;
        }
        return false;
    }

    // What stands before the type must end a statement or open a parameter list.
    if (j >= 0) {
        const Tok &t = f.toks.at(j);
        const bool boundary = t.type == 'b'
            || (t.type == 'p' && t.len == 1
                && (t.ch == ';' || t.ch == '{' || t.ch == '}' || t.ch == '(' || t.ch == ',' || t.ch == ')'
                    || t.ch == ':'));
        if (!boundary)
            return false;
    }
    // What follows the name must fit a declaration.
    if (next < f.toks.size()) {
        const Tok &t = f.toks.at(next);
        const bool fits = t.type == 'b'
            || (t.type == 'p' && t.len == 1
                && (t.ch == '=' || t.ch == ';' || t.ch == ',' || t.ch == '[' || t.ch == ')' || t.ch == '('
                    || t.ch == '{' || t.ch == ':'));
        if (!fits)
            return false;
    }
    // "(a * x)" is a multiplication unless the brackets are a parameter list.
    if (pointer && (isP(f, j, '(') || isP(f, j, ','))) {
        const int open = enclosingParen(f, i);
        if (open >= 0 && !isFunctionHeaderParen(f, open)) {
            const bool loop = isIdent(f, open - 1)
                && (word(f, open - 1) == QLatin1String("for") || word(f, open - 1) == QLatin1String("catch"));
            if (!loop)
                return false;
        }
    }

    if (isTypedef)
        *kind = Kind::Type;
    else if (isP(f, next, '('))
        *kind = bodyAfterParen(f, next) >= 0 ? Kind::Function : Kind::Prototype;
    else
        *kind = isExtern ? Kind::Prototype : Kind::Variable;
    *scope = scopeOf(f, i);
    return true;
}

struct Candidate {
    int file;
    int pos;
    int len;
    SymbolFinder::Kind kind;
    int scope;   // '{' token index in its file, -1 whole sketch, -2 nowhere
};

SymbolFinder::Result toResult(const Candidate &c)
{
    SymbolFinder::Result r;
    r.file = c.file;
    r.pos = c.pos;
    r.length = c.len;
    r.kind = c.kind;
    return r;
}

}   // namespace

namespace SymbolFinder {

QString identifierAt(const QString &text, int pos, int *start)
{
    pos = qBound(0, pos, text.size());
    if (pos >= text.size() || !isWordChar(text.at(pos))) {
        if (pos == 0 || !isWordChar(text.at(pos - 1)))
            return QString();
        --pos;   // the caret sits right after the word
    }
    int from = pos, to = pos;
    while (from > 0 && isWordChar(text.at(from - 1)))
        --from;
    while (to < text.size() && isWordChar(text.at(to)))
        ++to;
    if (text.at(from).isDigit())
        return QString();   // a number
    if (start)
        *start = from;
    return text.mid(from, to - from);
}

bool isKeyword(const QString &word)
{
    static const QSet<QString> words = [] {
        const char *list =
            "alignas alignof and asm auto bool break case catch char class const constexpr continue "
            "default delete do double else enum explicit extern false float for friend goto if inline int "
            "long mutable namespace new noexcept not nullptr operator or private protected public register "
            "return short signed sizeof static struct switch template this throw true try typedef typename "
            "union unsigned using virtual void volatile while";
        const QStringList parts = QString::fromLatin1(list).split(QLatin1Char(' '));
        return QSet<QString>(parts.begin(), parts.end());
    }();
    return words.contains(word);
}

Result findDefinition(const QStringList &texts, int file, int wordStart, const QString &name)
{
    if (name.isEmpty() || file < 0 || file >= texts.size())
        return Result();

    QVector<File> files;
    files.reserve(texts.size());
    QVector<Candidate> found;
    for (int n = 0; n < texts.size(); ++n) {
        // Only parse tabs that contain the name at all (the current tab always, for its scopes).
        files.append(n == file || texts.at(n).contains(name) ? parse(texts.at(n)) : File());
        const File &f = files.last();
        for (const Macro &m : f.macros) {
            if (m.len == name.size() && f.masked.midRef(m.pos, m.len) == name)
                found.append({n, m.pos, m.len, Kind::Macro, -1});
        }
        for (int i = 0; i < f.toks.size(); ++i) {
            const Tok &t = f.toks.at(i);
            if (t.type != 'i' || t.len != name.size() || f.masked.midRef(t.pos, t.len) != name)
                continue;
            Kind kind = Kind::None;
            int scope = -1;
            if (classify(f, i, &kind, &scope))
                found.append({n, t.pos, t.len, kind, scope});
        }
    }
    if (found.isEmpty())
        return Result();

    // 1. Local: the blocks around the cursor, innermost first; the first declaration in a block.
    const File &cur = files.at(file);
    int at = -1;   // last token at or before the cursor word
    for (int i = 0; i < cur.toks.size() && cur.toks.at(i).pos <= wordStart; ++i)
        at = i;
    QVector<int> chain;
    if (at >= 0) {
        const bool onToken = cur.toks.at(at).pos == wordStart;
        const int paren = enclosingParen(cur, onToken ? at : at + 1, false);
        if (paren >= 0) {
            const int body = bodyAfterParen(cur, paren);   // inside for (...) or a parameter list
            if (body >= 0)
                chain.append(body);
        }
        int b = (!onToken && isP(cur, at, '{')) ? at : cur.block.at(at);
        for (; b >= 0; b = cur.block.at(b))
            chain.append(b);
    }
    for (int scope : qAsConst(chain)) {
        for (const Candidate &c : qAsConst(found)) {
            if (c.file == file && c.scope == scope && c.pos <= wordStart)
                return toResult(c);
        }
    }

    // 2. Whole sketch: a real definition before a prototype/extern, the current tab first.
    auto rank = [file](const Candidate &c) {
        const int weak = (c.kind == Kind::Prototype) ? 2 : 0;
        return weak + (c.file == file ? 0 : 1);
    };
    const Candidate *best = nullptr;
    for (const Candidate &c : qAsConst(found)) {
        if (c.scope == -1 && (!best || rank(c) < rank(*best)))
            best = &c;
    }
    if (best)
        return toResult(*best);

    // 3. Anything else with that name (struct members, methods declared inside a class).
    for (const Candidate &c : qAsConst(found)) {
        if (c.scope != -2 && (!best || rank(c) < rank(*best)))
            best = &c;
    }
    return best ? toResult(*best) : Result();
}

}   // namespace SymbolFinder
