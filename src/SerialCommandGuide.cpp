#include "SerialCommandGuide.h"
#include "Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
const char kFileName[] = "serial-commands.json";

// A command row; its Edit/Delete buttons show only while the mouse is over it.
class HoverRow : public QWidget
{
public:
    QList<QWidget *> hoverOnly;

    void setHover(bool on)
    {
        for (QWidget *w : hoverOnly)
            w->setVisible(on);
    }

protected:
    void enterEvent(QEvent *event) override
    {
        setHover(true);
        QWidget::enterEvent(event);
    }
    void leaveEvent(QEvent *event) override
    {
        setHover(false);
        QWidget::leaveEvent(event);
    }
};

// JSON string literal (quotes and escapes) for the hand-formatted file.
QString quoted(const QString &s)
{
    const QString arr = QString::fromUtf8(QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact));
    return arr.mid(1, arr.size() - 2);   // strip [ ]
}

// Keyboard focus is somewhere the user types (code editor, Serial Monitor input,
// a search field): hotkeys must not fire there.
bool isTextInput(const QWidget *w)
{
    if (!w)
        return false;
    if (w->inherits("QsciScintilla") || qobject_cast<const QLineEdit *>(w) || qobject_cast<const QAbstractSpinBox *>(w))
        return true;
    if (auto *pe = qobject_cast<const QPlainTextEdit *>(w))
        return !pe->isReadOnly();
    if (auto *te = qobject_cast<const QTextEdit *>(w))
        return !te->isReadOnly();
    if (auto *cb = qobject_cast<const QComboBox *>(w))
        return cb->isEditable();
    return false;
}

QString hotkeyText(const QKeySequence &key)
{
    return key.toString(QKeySequence::NativeText);
}
}

QString SerialCommandGuide::fileName()
{
    return QString::fromLatin1(kFileName);
}

SerialCommandGuide::SerialCommandGuide(Sender sender, QWidget *parent)
    : QWidget(parent), m_sender(std::move(sender))
{
    setObjectName(QStringLiteral("commandGuide"));
    setAttribute(Qt::WA_StyledBackground);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    auto *header = new QHBoxLayout;
    auto *title = new QLabel(tr("Serial Commands"));
    title->setObjectName(QStringLiteral("manualTitle"));
    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("manualStatus"));
    m_add = new QToolButton;
    m_add->setObjectName(QStringLiteral("manualAdd"));
    m_add->setText(tr("+ Add"));
    m_add->setToolTip(tr("Add a serial command to the list"));
    m_add->setFocusPolicy(Qt::NoFocus);
    connect(m_add, &QToolButton::clicked, this, &SerialCommandGuide::addCommand);
    m_keysHint = new QLabel;
    m_keysHint->setObjectName(QStringLiteral("manualStatus"));
    m_keysHint->hide();
    header->addWidget(title);
    header->addSpacing(12);
    header->addWidget(m_status, 1);
    header->addWidget(m_keysHint);
    header->addSpacing(8);
    header->addWidget(m_add);
    // Clicking the list (not a text field) takes focus, so hotkeys work.
    setFocusPolicy(Qt::StrongFocus);
    connect(qApp, &QApplication::focusChanged, this, [this] { updateKeysHint(); });
    layout->addLayout(header);

    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("manualScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *list = new QWidget;
    list->setObjectName(QStringLiteral("manualList"));
    list->setAttribute(Qt::WA_StyledBackground);
    m_rows = new QVBoxLayout(list);
    m_rows->setContentsMargins(0, 0, 4, 0);
    m_rows->setSpacing(6);
    scroll->setWidget(list);
    layout->addWidget(scroll, 1);
}

// ---------------------------------------------------------------------------
// File
// ---------------------------------------------------------------------------

void SerialCommandGuide::setSketchDir(const QString &dir)
{
    if (dir == m_dir) {
        reloadIfChanged();
        return;
    }
    m_dir = dir;
    load();
}

void SerialCommandGuide::reloadIfChanged()
{
    const QFileInfo fi(QDir(m_dir).filePath(fileName()));
    const QDateTime t = fi.exists() ? fi.lastModified() : QDateTime();
    if (t != m_mtime)
        load();
}

void SerialCommandGuide::load()
{
    m_commands.clear();
    m_loadError.clear();
    m_mtime = QDateTime();

    const QString path = QDir(m_dir).filePath(fileName());
    QFile file(path);
    if (file.exists()) {
        m_mtime = QFileInfo(path).lastModified();
        if (!file.open(QIODevice::ReadOnly)) {
            m_loadError = tr("Cannot read %1: %2").arg(fileName(), file.errorString());
        } else {
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
            if (!doc.isObject()) {
                m_loadError = tr("%1 is not valid JSON (%2 at offset %3); fix it in a text editor.")
                                  .arg(fileName(), err.errorString()).arg(err.offset);
            } else {
                for (const QJsonValue &v : doc.object().value(QStringLiteral("commands")).toArray()) {
                    const QJsonObject o = v.toObject();
                    m_commands.append({o.value(QStringLiteral("command")).toString(),
                                       o.value(QStringLiteral("name")).toString(),
                                       QKeySequence::fromString(o.value(QStringLiteral("hotkey")).toString(),
                                                                QKeySequence::PortableText)});
                }
            }
        }
    }
    rebuild();
    setStatus(m_loadError, !m_loadError.isEmpty());
}

bool SerialCommandGuide::save()
{
    if (!m_loadError.isEmpty())
        return false;   // never overwrite a file we couldn't read
    const QString path = QDir(m_dir).filePath(fileName());
    QDir().mkpath(m_dir);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(toJson().toUtf8()) < 0 || !file.commit()) {
        setStatus(tr("Cannot save %1: %2").arg(QDir::toNativeSeparators(path), file.errorString()), true);
        return false;
    }
    m_mtime = QFileInfo(path).lastModified();
    setStatus(QString(), false);
    return true;
}

// Hand-formatted (QJsonDocument sorts keys): command, name.
QString SerialCommandGuide::toJson() const
{
    QString out = QStringLiteral("{\n  \"commands\": [");
    for (int i = 0; i < m_commands.size(); ++i) {
        const Command &c = m_commands[i];
        const QString hotkey = c.hotkey.isEmpty()
            ? QString() : QStringLiteral(", \"hotkey\": %1").arg(quoted(c.hotkey.toString(QKeySequence::PortableText)));
        out += QStringLiteral("\n    { \"command\": %1, \"name\": %2%3 }")
                   .arg(quoted(c.command), quoted(c.name), hotkey);
        if (i + 1 < m_commands.size())
            out += QLatin1Char(',');
    }
    out += m_commands.isEmpty() ? QStringLiteral("]\n}\n") : QStringLiteral("\n  ]\n}\n");
    return out;
}

// ---------------------------------------------------------------------------
// List
// ---------------------------------------------------------------------------

void SerialCommandGuide::rebuild()
{
    QLayoutItem *li;
    while ((li = m_rows->takeAt(0)) != nullptr) {
        if (QWidget *w = li->widget()) {
            w->hide();
            w->deleteLater();   // may hold the button whose menu triggered this
        }
        delete li;
    }
    m_add->setEnabled(m_loadError.isEmpty());

    if (m_commands.isEmpty() && m_loadError.isEmpty()) {
        auto *empty = new QLabel(tr("No commands yet. Click + Add."));
        empty->setObjectName(QStringLiteral("manualEmpty"));
        m_rows->addWidget(empty);
    }
    const QFont mono = Theme::monoFont(10);
    for (int i = 0; i < m_commands.size(); ++i) {
        // [command] - name
        auto *row = new HoverRow;
        row->setObjectName(QStringLiteral("commandRow"));
        row->setAttribute(Qt::WA_StyledBackground);   // hover highlight from the stylesheet
        row->setContextMenuPolicy(Qt::CustomContextMenu);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        auto *button = new QPushButton(m_commands[i].command);
        button->setObjectName(QStringLiteral("commandButton"));
        button->setFont(mono);
        button->setAutoDefault(false);
        button->setCursor(Qt::PointingHandCursor);
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        button->setToolTip(tr("Click to send \"%1\" to the serial port.\nRight-click to copy or move it.")
                               .arg(m_commands[i].command));
        connect(button, &QPushButton::clicked, this, [this, i] { sendCommand(i); });
        connect(button, &QPushButton::customContextMenuRequested, this, [this, button, i](const QPoint &pos) {
            showMenu(i, button->mapToGlobal(pos));
        });
        connect(row, &QWidget::customContextMenuRequested, this, [this, row, i](const QPoint &pos) {
            showMenu(i, row->mapToGlobal(pos));
        });
        auto *name = new QLabel(QStringLiteral("- ") + m_commands[i].name);
        name->setObjectName(QStringLiteral("commandName"));
        QLabel *key = nullptr;
        if (!m_commands[i].hotkey.isEmpty()) {
            key = new QLabel(QStringLiteral("(%1)").arg(hotkeyText(m_commands[i].hotkey)));
            key->setObjectName(QStringLiteral("commandKey"));
            key->setToolTip(tr("Hotkey: press %1 to send \"%2\"").arg(hotkeyText(m_commands[i].hotkey), m_commands[i].command));
        }
        // Edit / Delete, shown on hover (space kept so the row doesn't shift).
        auto *edit = new QToolButton;
        edit->setObjectName(QStringLiteral("rowEdit"));
        edit->setText(tr("Edit"));
        edit->setToolTip(tr("Edit this command"));
        auto *del = new QToolButton;
        del->setObjectName(QStringLiteral("rowDelete"));
        del->setText(tr("Delete"));
        del->setToolTip(tr("Delete this command"));
        for (QToolButton *b : {edit, del}) {
            b->setCursor(Qt::PointingHandCursor);
            b->setEnabled(m_loadError.isEmpty());
            QSizePolicy policy = b->sizePolicy();
            policy.setRetainSizeWhenHidden(true);
            b->setSizePolicy(policy);
            b->hide();
            row->hoverOnly.append(b);
        }
        // Deferred: these rebuild (delete) the rows.
        connect(edit, &QToolButton::clicked, this, [this, i] {
            QTimer::singleShot(0, this, [this, i] { editCommand(i); });
        });
        connect(del, &QToolButton::clicked, this, [this, i] {
            QTimer::singleShot(0, this, [this, i] { removeCommand(i); });
        });
        rowLayout->addWidget(button);
        if (key)
            rowLayout->addWidget(key);
        rowLayout->addWidget(name);
        rowLayout->addSpacing(8);
        rowLayout->addWidget(edit);
        rowLayout->addWidget(del);
        rowLayout->addStretch(1);
        m_rows->addWidget(row);
        // Rebuilt under the mouse (after an edit): no Enter event comes, so check now.
        QTimer::singleShot(0, row, [row] { row->setHover(row->rect().contains(row->mapFromGlobal(QCursor::pos()))); });
    }
    m_rows->addStretch(1);
    updateKeysHint();
}

void SerialCommandGuide::sendCommand(int index)
{
    if (index < 0 || index >= m_commands.size())
        return;
    const Command &c = m_commands[index];
    const QString error = m_sender(c.command.toUtf8(), c.name);
    if (error.isEmpty())
        setStatus(tr("Sent \"%1\"  (%2)").arg(c.command, c.name), false);
    else
        setStatus(tr("Not sent: %1").arg(error), true);
}

void SerialCommandGuide::setStatus(const QString &text, bool isError)
{
    m_status->setText(text);
    m_status->setProperty("error", isError);
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
}

// ---------------------------------------------------------------------------
// Hotkeys
// ---------------------------------------------------------------------------

void SerialCommandGuide::showEvent(QShowEvent *event)
{
    qApp->installEventFilter(this);
    updateKeysHint();
    QWidget::showEvent(event);
}

void SerialCommandGuide::hideEvent(QHideEvent *event)
{
    qApp->removeEventFilter(this);
    QWidget::hideEvent(event);
}

bool SerialCommandGuide::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::KeyPress || !isVisible() || !m_loadError.isEmpty())
        return QWidget::eventFilter(watched, event);
    // Only in the main window (not in a dialog), and never while typing.
    if (QApplication::activeWindow() != window() || isTextInput(QApplication::focusWidget()))
        return QWidget::eventFilter(watched, event);
    auto *key = static_cast<QKeyEvent *>(event);
    const int code = key->key();
    if (code == Qt::Key_unknown || code == Qt::Key_Shift || code == Qt::Key_Control || code == Qt::Key_Alt
        || code == Qt::Key_Meta)
        return QWidget::eventFilter(watched, event);
    const QKeySequence pressed(int(key->modifiers() & ~Qt::KeypadModifier) | code);
    for (int i = 0; i < m_commands.size(); ++i) {
        if (m_commands[i].hotkey.isEmpty() || m_commands[i].hotkey != pressed)
            continue;
        if (!key->isAutoRepeat())   // holding the key sends once
            sendCommand(i);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

// "Hotkeys on" / "Hotkeys paused (typing): click the list" next to + Add.
void SerialCommandGuide::updateKeysHint()
{
    bool any = false;
    for (const Command &c : m_commands)
        any = any || !c.hotkey.isEmpty();
    if (!any || !m_loadError.isEmpty()) {
        m_keysHint->hide();
        return;
    }
    const bool paused = isTextInput(QApplication::focusWidget());
    m_keysHint->setText(paused ? tr("Hotkeys paused while typing: click this list to use them")
                               : tr("Hotkeys on"));
    m_keysHint->setProperty("error", false);
    m_keysHint->show();
}

void SerialCommandGuide::showMenu(int index, const QPoint &globalPos)
{
    if (!m_loadError.isEmpty() || index < 0 || index >= m_commands.size())
        return;
    QMenu menu(this);
    QAction *copy = menu.addAction(tr("Copy Command"));
    QAction *edit = menu.addAction(tr("Edit..."));
    QAction *up = menu.addAction(tr("Move Up"));
    QAction *down = menu.addAction(tr("Move Down"));
    menu.addSeparator();
    QAction *del = menu.addAction(tr("Delete"));
    up->setEnabled(index > 0);
    down->setEnabled(index + 1 < m_commands.size());
    QAction *chosen = menu.exec(globalPos);
    // Deferred: the rows are rebuilt (deleted) by these.
    if (chosen == copy)
        copyCommand(index);
    else if (chosen == edit)
        QTimer::singleShot(0, this, [this, index] { editCommand(index); });
    else if (chosen == up)
        QTimer::singleShot(0, this, [this, index] { moveCommand(index, -1); });
    else if (chosen == down)
        QTimer::singleShot(0, this, [this, index] { moveCommand(index, 1); });
    else if (chosen == del)
        QTimer::singleShot(0, this, [this, index] { removeCommand(index); });
}

// ---------------------------------------------------------------------------
// Add / edit / move / delete
// ---------------------------------------------------------------------------

void SerialCommandGuide::addCommand()
{
    if (!m_loadError.isEmpty())
        return;
    Command cmd;
    if (!runDialog(cmd, -1, tr("Add Serial Command")))
        return;
    m_commands.append(cmd);
    save();
    rebuild();
}

void SerialCommandGuide::editCommand(int index)
{
    if (index < 0 || index >= m_commands.size() || !m_loadError.isEmpty())
        return;
    Command cmd = m_commands[index];
    if (!runDialog(cmd, index, tr("Edit Serial Command")))
        return;
    m_commands[index] = cmd;
    save();
    rebuild();
}

void SerialCommandGuide::removeCommand(int index)
{
    if (index < 0 || index >= m_commands.size())
        return;
    const auto answer = QMessageBox::question(this, tr("Delete Command"),
                                              tr("Delete \"%1\"?").arg(m_commands[index].command));
    if (answer != QMessageBox::Yes)
        return;
    m_commands.removeAt(index);
    save();
    rebuild();
}

void SerialCommandGuide::moveCommand(int index, int delta)
{
    const int to = index + delta;
    if (index < 0 || index >= m_commands.size() || to < 0 || to >= m_commands.size())
        return;
    m_commands.swapItemsAt(index, to);
    save();
    rebuild();
}

void SerialCommandGuide::copyCommand(int index)
{
    if (index < 0 || index >= m_commands.size())
        return;
    QApplication::clipboard()->setText(m_commands[index].command);
    setStatus(tr("Copied \"%1\"").arg(m_commands[index].command), false);
}

bool SerialCommandGuide::runDialog(Command &cmd, int editingIndex, const QString &title)
{
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("manualDialog"));
    dlg.setWindowTitle(title);
    dlg.setWindowFlags(dlg.windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *layout = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto *command = new QLineEdit(cmd.command);
    command->setFont(Theme::monoFont(10));
    command->setPlaceholderText(tr("e.g. MIXER_HOME"));
    command->setMinimumWidth(280);
    form->addRow(tr("Serial Text Command"), command);

    auto *name = new QLineEdit(cmd.name);
    name->setPlaceholderText(tr("e.g. Mixer Home"));
    form->addRow(tr("Command Name"), name);

    // Hotkey: one key or key combination (optional).
    auto *hotkey = new QKeySequenceEdit(cmd.hotkey);
    hotkey->setToolTip(tr("Click, then press a key (e.g. Q). Optional."));
    connect(hotkey, &QKeySequenceEdit::keySequenceChanged, hotkey, [hotkey](const QKeySequence &seq) {
        if (seq.count() > 1)
            hotkey->setKeySequence(QKeySequence(seq[0]));   // one key, not a chord
    });
    auto *clearKey = new QToolButton;
    clearKey->setText(tr("Clear"));
    connect(clearKey, &QToolButton::clicked, hotkey, &QKeySequenceEdit::clear);
    auto *keyRow = new QHBoxLayout;
    keyRow->setContentsMargins(0, 0, 0, 0);
    keyRow->addWidget(hotkey, 1);
    keyRow->addWidget(clearKey);
    form->addRow(tr("Hotkey (optional)"), keyRow);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, [&] {
        if (command->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dlg, title, tr("Enter the serial text command."));
            command->setFocus();
            return;
        }
        if (name->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dlg, title, tr("Enter a command name."));
            name->setFocus();
            return;
        }
        for (int i = 0; i < m_commands.size(); ++i) {
            if (i != editingIndex && m_commands[i].command == command->text().trimmed()) {
                QMessageBox::warning(&dlg, title, tr("\"%1\" is already in the list (%2).")
                                                      .arg(m_commands[i].command, m_commands[i].name));
                command->setFocus();
                return;
            }
        }
        const QKeySequence key = hotkey->keySequence();
        if (!key.isEmpty()) {
            for (int i = 0; i < m_commands.size(); ++i) {
                if (i != editingIndex && m_commands[i].hotkey == key) {
                    QMessageBox::warning(&dlg, title, tr("%1 is already the hotkey of \"%2\".")
                                                          .arg(hotkeyText(key), m_commands[i].command));
                    hotkey->setFocus();
                    return;
                }
            }
            // Menu shortcuts (Ctrl+S, F5, ...) win over hotkeys: don't allow those.
            for (QAction *a : window()->findChildren<QAction *>()) {
                if (a->shortcuts().contains(key)) {
                    QMessageBox::warning(&dlg, title, tr("%1 is already used by \"%2\" in the menus.")
                                                          .arg(hotkeyText(key), a->text().remove(QLatin1Char('&'))));
                    hotkey->setFocus();
                    return;
                }
            }
        }
        dlg.accept();
    });
    command->setFocus();

    if (dlg.exec() != QDialog::Accepted)
        return false;
    cmd.command = command->text().trimmed();
    cmd.name = name->text().trimmed();
    cmd.hotkey = hotkey->keySequence();
    return true;
}
