#pragma once

#include <QString>
#include <QStringList>

// Go to Definition (F12): finds where a name is declared in the sketch's tabs by reading the
// text itself. No compiler or language server runs (startup and Pi speed), so it is a careful
// guess, not a full C++ parser: it knows #define, functions, variables, parameters,
// struct/class/enum names and enum constants, and it respects { } scopes.
// Names from libraries and the Arduino core (digitalWrite, Serial...) are not found.
namespace SymbolFinder {

enum class Kind { None, Macro, Function, Prototype, Variable, Type, EnumConstant };

struct Result {
    int file = -1;     // index into the list of texts; -1 = not found
    int pos = 0;       // offset of the name in that text (UTF-16 units, like QString)
    int length = 0;
    Kind kind = Kind::None;
};

// The identifier at (or ending right before) pos; empty if there is none.
QString identifierAt(const QString &text, int pos, int *start = nullptr);

bool isKeyword(const QString &word);   // C++ keywords and built-in type names

// texts: every tab of the sketch. file/wordStart: where the name was pressed (decides which
// local variable is meant). Local scopes win over globals; the current tab wins over others.
Result findDefinition(const QStringList &texts, int file, int wordStart, const QString &name);

}   // namespace SymbolFinder
