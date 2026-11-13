#include "Theme.h"

#include <Qsci/qscilexercpp.h>
#include <Qsci/qsciscintilla.h>

#include <QApplication>
#include <QDir>
#include <QVector>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSvgRenderer>

namespace Theme {
namespace {

QColor mix(QRgb a, QRgb b, qreal t)
{
    const QColor ca(a), cb(b);
    return QColor::fromRgbF(ca.redF() + (cb.redF() - ca.redF()) * t,
                            ca.greenF() + (cb.greenF() - ca.greenF()) * t,
                            ca.blueF() + (cb.blueF() - ca.blueF()) * t);
}

QString hex(const QColor &c) { return c.name(QColor::HexRgb); }

QPixmap renderSvg(const QString &resource, const QColor &color, int px)
{
    QFile file(resource);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QByteArray svg = file.readAll();
    svg.replace("currentColor", hex(color).toLatin1());
    QSvgRenderer renderer(svg);
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter);
    return pm;
}

// QSS `image:` needs a file, so the themed combo-box chevron is rendered once per
// color into the cache dir (name.png + name@2x.png). Returns "" if it can't be written.
QString chevronFile(const QColor &color)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString base = QStringLiteral("%1/chevron-%2").arg(dir, hex(color).mid(1));
    const QString file1x = base + QStringLiteral(".png");
    const QString file2x = base + QStringLiteral("@2x.png");
    if (!QFileInfo::exists(file1x) || !QFileInfo::exists(file2x)) {
        if (!QDir().mkpath(dir))
            return {};
        const QString svg = QStringLiteral(":/icons/chevron-down.svg");
        if (!renderSvg(svg, color, 12).save(file1x) || !renderSvg(svg, color, 24).save(file2x))
            return {};
    }
    return file1x;
}

} // namespace

Colors colors(Mode mode)
{
    using namespace Brand;
    Colors c;
    if (mode == Mode::Dark) {
        c.window       = QColor(Navy);
        c.editorBg     = QColor(NavyDeep);
        c.text         = QColor(White);
        c.textMuted    = QColor(Slate);
        c.border       = QColor(NavyLight);
        c.hover        = QColor(NavyLight);
        c.currentLine  = QColor(NavyLight);
        c.selection    = mix(NavyDeep, SkyBlue, 0.40);   // Sky Blue at ~40%
        c.primary      = QColor(Amber);
        c.onPrimary    = QColor(Navy);
        c.secondary    = QColor(SkyBlue);
        c.onSecondary  = QColor(White);
        c.tooltipBg    = QColor(NavyLight);
        c.tooltipText  = QColor(White);
        c.keyword      = QColor(SkyBlue);
        c.type         = QColor(Amber);
        c.number       = QColor(Amber);
        c.string       = QColor(StringGreen);
        c.comment      = QColor(Slate);
        c.preprocessor = QColor(Amber);
        c.braceMatch   = QColor(Amber);
        c.error        = QColor(ErrorRed);
        c.warning      = QColor(Amber);
        c.success      = QColor(SuccessGreen);
        c.sent         = QColor(SkyBlue);
        c.command      = QColor(Slate);
    } else {
        // White background, Navy text, Amber only as fill/border (never text).
        c.window       = mix(White, Slate, 0.12);
        c.editorBg     = QColor(White);
        c.text         = QColor(Navy);
        c.textMuted    = mix(Slate, Navy, 0.45);
        c.border       = mix(White, Slate, 0.45);
        c.hover        = mix(White, SkyBlue, 0.14);
        c.currentLine  = mix(White, SkyBlue, 0.07);
        c.selection    = mix(White, SkyBlue, 0.30);
        c.primary      = QColor(Amber);
        c.onPrimary    = QColor(Navy);
        c.secondary    = QColor(SkyBlue);
        c.onSecondary  = QColor(White);
        c.tooltipBg    = QColor(Navy);
        c.tooltipText  = QColor(White);
        c.keyword      = mix(SkyBlue, Navy, 0.35);        // Sky Blue, darkened for contrast
        c.type         = QColor(AmberOnLight);
        c.number       = QColor(AmberOnLight);
        c.string       = QColor(StringGreenOnLight);
        c.comment      = mix(Slate, Navy, 0.35);
        c.preprocessor = QColor(AmberOnLight);
        c.braceMatch   = c.keyword;
        c.error        = QColor(ErrorRedOnLight);
        c.warning      = QColor(AmberOnLight);
        c.success      = QColor(SuccessGreenOnLight);
        c.sent         = c.keyword;
        c.command      = c.textMuted;
    }
    return c;
}

QPalette palette(Mode mode)
{
    const Colors c = colors(mode);
    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.editorBg);
    p.setColor(QPalette::AlternateBase, c.window);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.window);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::BrightText, c.text);
    p.setColor(QPalette::Highlight, c.secondary);
    p.setColor(QPalette::HighlightedText, c.onSecondary);
    p.setColor(QPalette::Link, c.secondary);
    p.setColor(QPalette::ToolTipBase, c.tooltipBg);
    p.setColor(QPalette::ToolTipText, c.tooltipText);
    p.setColor(QPalette::PlaceholderText, c.textMuted);
    // Fusion draws bevels/frames from these.
    p.setColor(QPalette::Light, c.border);
    p.setColor(QPalette::Midlight, c.border);
    p.setColor(QPalette::Mid, c.border);
    p.setColor(QPalette::Dark, c.border);
    p.setColor(QPalette::Shadow, c.editorBg);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, c.textMuted);
    return p;
}

QString styleSheet(Mode mode)
{
    const Colors c = colors(mode);
    // Flat, no shadows/animations. Tokens are replaced below. Applied to the
    // toolbar, menu bar, output pane and Serial Monitor only (see MainWindow::applyTheme).
    QString qss = QStringLiteral(R"(
QToolBar#mainToolbar {
    background: @window@;
    border: none;
    border-bottom: 1px solid @border@;
    padding: 6px 10px;
    spacing: 8px;
}
QToolBar#mainToolbar QLabel { color: @muted@; padding-left: 6px; }

QToolButton#verifyButton, QToolButton#uploadButton {
    border: none;
    border-radius: 6px;
    padding: 6px 12px 6px 8px;
    margin-right: 4px;
    font-weight: 600;
}
QToolButton#verifyButton { background: @secondary@; color: @onSecondary@; }
QToolButton#verifyButton:hover { background: @secondaryHover@; }
QToolButton#verifyButton:pressed { background: @secondaryPressed@; }
QToolButton#uploadButton { background: @primary@; color: @onPrimary@; }
QToolButton#uploadButton:hover { background: @primaryHover@; }
QToolButton#uploadButton:pressed { background: @primaryPressed@; }
QToolButton#verifyButton:disabled, QToolButton#uploadButton:disabled {
    background: @border@; color: @muted@;
}

QComboBox {
    background: @editorBg@;
    color: @text@;
    border: 1px solid @border@;
    border-radius: 6px;
    padding: 5px 10px;
}
QComboBox:hover { border-color: @muted@; }
QComboBox:focus, QComboBox:on { border-color: @primary@; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow { image: url("@chevron@"); width: 12px; height: 12px; }
QComboBox QAbstractItemView {
    background: @editorBg@;
    color: @text@;
    border: 1px solid @border@;
    selection-background-color: @hover@;
    selection-color: @text@;
    outline: 0;
}

QFrame#boardPopup {
    background: @window@;
    border: 1px solid @border@;
}
QListView#boardList {
    background: @editorBg@;
    color: @text@;
    border: 1px solid @border@;
    border-radius: 4px;
    outline: 0;
}
QListView#boardList::item { padding: 4px 8px; }
QListView#boardList::item:hover { background: @hover@; }
QListView#boardList::item:selected { background: @hover@; color: @text@; border-left: 3px solid @primary@; }
QLabel#boardEmpty { color: @muted@; padding: 16px; }

QToolButton#serialButton {
    background: transparent;
    color: @text@;
    border: 1px solid @border@;
    border-radius: 6px;
    padding: 5px 12px 5px 8px;
}
QToolButton#serialButton:hover { background: @hover@; }
QToolButton#serialButton:checked { border-color: @primary@; background: @hover@; }

QWidget#serialControls { background: @window@; }
QLineEdit {
    background: @editorBg@;
    color: @text@;
    border: 1px solid @border@;
    border-radius: 6px;
    padding: 5px 8px;
    selection-background-color: @selection@;
    selection-color: @text@;
}
QLineEdit:focus { border-color: @primary@; }
QPushButton#sendButton {
    background: @secondary@;
    color: @onSecondary@;
    border: none;
    border-radius: 6px;
    padding: 6px 16px;
    font-weight: 600;
}
QPushButton#sendButton:hover { background: @secondaryHover@; }
QPushButton#sendButton:pressed { background: @secondaryPressed@; }

QLabel#outputHeader, QLabel#panelHeader {
    background: @window@;
    color: @muted@;
    font-weight: 600;
    padding: 5px 12px;
    border-bottom: 1px solid @border@;
}
QPlainTextEdit#output, QPlainTextEdit#serialView {
    background: @editorBg@;
    color: @text@;
    border: none;
    padding: 4px 6px;
    selection-background-color: @selection@;
    selection-color: @text@;
}

QMenuBar { background: @window@; color: @text@; }
QMenuBar::item { padding: 4px 10px; background: transparent; }
QMenuBar::item:selected { background: @hover@; }
QMenu { background: @window@; color: @text@; border: 1px solid @border@; padding: 4px; }
QMenu::item { padding: 5px 24px 5px 24px; border-radius: 4px; }
QMenu::item:selected { background: @hover@; }

QToolTip { background: @tooltipBg@; color: @tooltipText@; border: 1px solid @border@; padding: 4px 6px; }
)");

    const QList<QPair<const char *, QColor>> tokens = {
        {"@window@", c.window},
        {"@editorBg@", c.editorBg},
        {"@text@", c.text},
        {"@muted@", c.textMuted},
        {"@border@", c.border},
        {"@hover@", c.hover},
        {"@selection@", c.selection},
        {"@primary@", c.primary},
        {"@primaryHover@", c.primary.lighter(110)},
        {"@primaryPressed@", c.primary.darker(112)},
        {"@onPrimary@", c.onPrimary},
        {"@secondary@", c.secondary},
        {"@secondaryHover@", c.secondary.lighter(112)},
        {"@secondaryPressed@", c.secondary.darker(112)},
        {"@onSecondary@", c.onSecondary},
        {"@tooltipBg@", c.tooltipBg},
        {"@tooltipText@", c.tooltipText},
    };
    for (const auto &t : tokens)
        qss.replace(QLatin1String(t.first), hex(t.second));

    const QString chevron = chevronFile(c.textMuted);
    if (chevron.isEmpty())
        qss.remove(QRegularExpression(QStringLiteral("QComboBox::(drop-down|down-arrow)[^\\n]*\\n")));   // keep Fusion's arrow
    else
        qss.replace(QLatin1String("@chevron@"), chevron);
    return qss;
}

void applyToEditor(QsciScintilla *editor, QsciLexerCPP *lexer, Mode mode, const QFont &font)
{
    const Colors c = colors(mode);
    using L = QsciLexerCPP;

    // Base style for every lexer style (including the "inactive" +64 ones).
    lexer->setDefaultPaper(c.editorBg);
    lexer->setDefaultColor(c.text);
    lexer->setDefaultFont(font);
    lexer->setPaper(c.editorBg, -1);
    lexer->setColor(c.text, -1);
    lexer->setFont(font, -1);

    QFont italic(font);
    italic.setItalic(true);
    for (int s : {L::Comment, L::CommentLine, L::CommentDoc, L::CommentLineDoc, L::CommentDocKeyword,
                  L::CommentDocKeywordError, L::PreProcessorComment, L::PreProcessorCommentLineDoc}) {
        lexer->setColor(c.comment, s);
        lexer->setFont(italic, s);
    }
    lexer->setColor(c.keyword, L::Keyword);
    lexer->setColor(c.type, L::KeywordSet2);
    lexer->setColor(c.number, L::Number);
    lexer->setColor(c.preprocessor, L::PreProcessor);
    for (int s : {L::DoubleQuotedString, L::SingleQuotedString, L::UnclosedString, L::VerbatimString,
                  L::RawString, L::TripleQuotedVerbatimString, L::HashQuotedString, L::EscapeSequence})
        lexer->setColor(c.string, s);
    // Code disabled by #if: muted.
    for (int s = 0; s <= L::EscapeSequence; ++s)
        lexer->setColor(c.textMuted, s + 64);

    // Editor chrome (must follow setLexer(), which resets these).
    editor->setMarginsBackgroundColor(c.editorBg);
    editor->setMarginsForegroundColor(c.textMuted);
    editor->setMarginsFont(font);
    editor->setCaretForegroundColor(c.text);
    editor->setCaretLineVisible(true);
    editor->setCaretLineBackgroundColor(c.currentLine);
    editor->setSelectionBackgroundColor(c.selection);
    editor->resetSelectionForegroundColor();   // keep syntax colors inside the selection
    editor->setMatchedBraceBackgroundColor(c.hover);
    editor->setMatchedBraceForegroundColor(c.braceMatch);
    editor->setUnmatchedBraceBackgroundColor(c.editorBg);
    editor->setUnmatchedBraceForegroundColor(c.error);
}

QColor logColor(LogKind kind, Mode mode)
{
    const Colors c = colors(mode);
    switch (kind) {
    case LogKind::Command: return c.command;
    case LogKind::Error:   return c.error;
    case LogKind::Warning: return c.warning;
    case LogKind::Success: return c.success;
    case LogKind::Sent:    return c.sent;
    case LogKind::Normal:  break;
    }
    return c.text;
}

QIcon svgIcon(const QString &resource, const QColor &color, int size)
{
    // Only 1x and the current scale factor (each extra render costs startup time).
    QIcon icon;
    const qreal screenDpr = qApp->devicePixelRatio();
    QVector<qreal> dprs{1.0};
    if (screenDpr > 1.0)
        dprs.append(screenDpr);
    for (qreal dpr : dprs) {
        QPixmap pm = renderSvg(resource, color, qRound(size * dpr));
        if (pm.isNull())
            return {};
        pm.setDevicePixelRatio(dpr);
        icon.addPixmap(pm);
    }
    return icon;
}

QFont monoFont(qreal pointSize)
{
    static const QString family = [] {
        const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/JetBrainsMonoNL-Regular.ttf"));
        QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/JetBrainsMonoNL-Italic.ttf"));
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        return families.isEmpty() ? QString() : families.first();
    }();
    QFont font = family.isEmpty() ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : QFont(family);
    font.setPointSizeF(pointSize);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
}

} // namespace Theme
