#pragma once

// All UI colors live here (CLAUDE.md "Brand colors"). Nothing else hard-codes a color:
// the QPalette, the stylesheet, the QScintilla lexer, the output pane and the SVG
// icons all take their colors from Theme::colors().

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPalette>
#include <QString>

class QsciLexerCPP;
class QsciScintilla;

namespace Theme {

enum class Mode { Dark, Light };

// Kind of line in the output pane (stored per line so it can be recolored).
enum class LogKind { Normal = 0, Command, Error, Warning, Success, Sent };

// ReArm Automation brand palette.
namespace Brand {
constexpr QRgb NavyDeep  = 0xFF071A33;
constexpr QRgb Navy      = 0xFF0B2545;
constexpr QRgb NavyLight = 0xFF163A66;
constexpr QRgb Amber     = 0xFFF5A623;
constexpr QRgb SkyBlue   = 0xFF2E9BE6;
constexpr QRgb White     = 0xFFFFFFFF;
constexpr QRgb Slate     = 0xFF8FA3BF;

// Text colors CLAUDE.md specifies for the dark theme.
constexpr QRgb StringGreen  = 0xFF9ED39A;
constexpr QRgb ErrorRed     = 0xFFFF6B6B;
constexpr QRgb SuccessGreen = 0xFF6FCF97;

// Light-theme variants: the dark-theme ones (and Amber text) are unreadable on white.
constexpr QRgb StringGreenOnLight  = 0xFF2E7D32;
constexpr QRgb ErrorRedOnLight     = 0xFFC62828;
constexpr QRgb SuccessGreenOnLight = 0xFF2E7D32;
constexpr QRgb AmberOnLight        = 0xFF8A5A00;   // dark amber: types, numbers, warnings
}

// Semantic colors for one theme, derived from Brand.
struct Colors {
    QColor window;       // window, toolbar, panels
    QColor editorBg;     // editor + output background
    QColor text;
    QColor textMuted;    // secondary text, line numbers
    QColor border;       // borders, splitter handles
    QColor hover;
    QColor currentLine;
    QColor selection;
    QColor primary;      // Amber: Upload, focus rings, active items
    QColor onPrimary;
    QColor secondary;    // Sky Blue: Verify, links
    QColor onSecondary;
    QColor tooltipBg;
    QColor tooltipText;
    // C++ lexer
    QColor keyword, type, number, string, comment, preprocessor, braceMatch;
    // Output pane
    QColor error, warning, success, sent, command;
};

Colors colors(Mode mode);
QPalette palette(Mode mode);
QString styleSheet(Mode mode);
void applyToEditor(QsciScintilla *editor, QsciLexerCPP *lexer, Mode mode, const QFont &font);
QColor logColor(LogKind kind, Mode mode);

// Renders an SVG resource (using `currentColor`) in the given color, crisp at 1x-2x.
QIcon svgIcon(const QString &resource, const QColor &color, int size = 18);

// Bundled JetBrains Mono (registered on first use).
QFont monoFont(qreal pointSize);

} // namespace Theme
