#include "ManualControl.h"
#include "Theme.h"

#include <QApplication>
#include <QComboBox>
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
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
const char kFileName[] = "manual-control.json";

// JSON string literal (quotes and escapes) for the hand-formatted file.
QString quoted(const QString &s)
{
    const QString arr = QString::fromUtf8(QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact));
    return arr.mid(1, arr.size() - 2);   // strip [ ]
}

QString normalizedHotkey(const QString &text)
{
    if (text.trimmed().isEmpty())
        return {};
    const QKeySequence seq(text, QKeySequence::PortableText);
    return seq.isEmpty() ? QString() : QKeySequence(seq[0]).toString(QKeySequence::PortableText);
}

bool isModifierKey(int key)
{
    return key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta
        || key == Qt::Key_AltGr || key == 0 || key == Qt::Key_unknown;
}
}

QString ManualControl::fileName()
{
    return QString::fromLatin1(kFileName);
}

ManualControl::ManualControl(Sender sender, QWidget *parent)
    : QWidget(parent), m_sender(std::move(sender))
{
    setObjectName(QStringLiteral("manualControl"));
    setAttribute(Qt::WA_StyledBackground);
    setFocusPolicy(Qt::StrongFocus);   // clicking empty space activates the hotkeys
    installEventFilter(this);          // keys the children don't use bubble up here

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    auto separator = [] {
        auto *line = new QWidget;
        line->setObjectName(QStringLiteral("manualSeparator"));
        line->setAttribute(Qt::WA_StyledBackground);
        line->setFixedWidth(1);
        return line;
    };

    m_lists[Relays] = addColumn(row, tr("Manual Relay Control"), Relays);
    row->addWidget(separator());
    m_lists[Functions] = addColumn(row, tr("Perform Functions"), Functions);
    row->addWidget(separator());

    // JSON (read-only): what is saved in the sketch folder.
    auto *jsonColumn = new QWidget;
    auto *jsonLayout = new QVBoxLayout(jsonColumn);
    jsonLayout->setContentsMargins(10, 8, 10, 8);
    jsonLayout->setSpacing(6);
    auto *jsonHeader = new QHBoxLayout;
    auto *jsonTitle = new QLabel(tr("JSON"));
    jsonTitle->setObjectName(QStringLiteral("manualTitle"));
    m_focusHint = new QLabel;
    m_focusHint->setObjectName(QStringLiteral("manualStatus"));
    jsonHeader->addWidget(jsonTitle);
    jsonHeader->addStretch(1);
    jsonHeader->addWidget(m_focusHint);
    jsonLayout->addLayout(jsonHeader);
    m_json = new QPlainTextEdit;
    m_json->setObjectName(QStringLiteral("manualJson"));
    m_json->setReadOnly(true);
    m_json->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_json->setFrameShape(QFrame::NoFrame);
    m_json->setFont(Theme::monoFont(9));
    m_json->installEventFilter(this);
    jsonLayout->addWidget(m_json, 1);
    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("manualStatus"));
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    jsonLayout->addWidget(m_status);
    row->addWidget(jsonColumn, 1);

    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (!now || (now != this && !isAncestorOf(now)))
            releaseKeys();
        updateFocusHint();
    });
    updateFocusHint();
}

QVBoxLayout *ManualControl::addColumn(QLayout *row, const QString &title, Group group)
{
    auto *column = new QWidget;
    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    auto *header = new QHBoxLayout;
    auto *label = new QLabel(title);
    label->setObjectName(QStringLiteral("manualTitle"));
    auto *add = m_addButtons[group] = new QToolButton;
    add->setObjectName(QStringLiteral("manualAdd"));
    add->setText(tr("+ Add"));
    add->setToolTip(group == Relays ? tr("Add a relay button (toggle or hold)") : tr("Add a function button"));
    add->setFocusPolicy(Qt::NoFocus);
    connect(add, &QToolButton::clicked, this, [this, group] { addItem(group); });
    header->addWidget(label);
    header->addStretch(1);
    header->addWidget(add);
    layout->addLayout(header);

    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("manualScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setFocusPolicy(Qt::NoFocus);   // arrow keys stay available as hotkeys
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *list = new QWidget;
    list->setObjectName(QStringLiteral("manualList"));
    list->setAttribute(Qt::WA_StyledBackground);
    auto *listLayout = new QVBoxLayout(list);
    listLayout->setContentsMargins(0, 0, 4, 0);
    listLayout->setSpacing(6);
    scroll->setWidget(list);
    layout->addWidget(scroll, 1);

    static_cast<QHBoxLayout *>(row)->addWidget(column, 1);
    return listLayout;
}

// ---------------------------------------------------------------------------
// File
// ---------------------------------------------------------------------------

void ManualControl::setSketchDir(const QString &dir)
{
    if (dir == m_dir) {
        reloadIfChanged();
        return;
    }
    m_dir = dir;
    load();
}

void ManualControl::reloadIfChanged()
{
    const QFileInfo fi(QDir(m_dir).filePath(fileName()));
    const QDateTime t = fi.exists() ? fi.lastModified() : QDateTime();
    if (t != m_mtime)
        load();
}

void ManualControl::load()
{
    releaseKeys();
    m_items[Relays].clear();
    m_items[Functions].clear();
    m_loadError.clear();
    m_rawText.clear();
    m_mtime = QDateTime();

    const QString path = QDir(m_dir).filePath(fileName());
    QFile file(path);
    if (file.exists()) {
        m_mtime = QFileInfo(path).lastModified();
        if (!file.open(QIODevice::ReadOnly)) {
            m_loadError = tr("// Cannot read %1: %2").arg(fileName(), file.errorString());
        } else {
            const QByteArray data = file.readAll();
            QJsonParseError err;
            const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
            if (!doc.isObject()) {
                m_loadError = tr("// %1 is not valid JSON (%2 at offset %3).\n"
                                 "// Fix it in a text editor; it is not overwritten meanwhile.")
                                  .arg(fileName(), err.errorString()).arg(err.offset);
                m_rawText = QString::fromUtf8(data);
            } else {
                const QJsonObject root = doc.object();
                for (Group g : {Relays, Functions}) {
                    const char *key = g == Relays ? "relays" : "functions";
                    for (const QJsonValue &v : root.value(QLatin1String(key)).toArray()) {
                        const QJsonObject o = v.toObject();
                        Item item;
                        item.name = o.value(QStringLiteral("name")).toString();
                        item.hotkey = normalizedHotkey(o.value(QStringLiteral("hotkey")).toString());
                        item.send = o.value(QStringLiteral("send")).toString();
                        item.hold = g == Relays && o.value(QStringLiteral("mode")).toString() == QLatin1String("hold");
                        m_items[g].append(item);
                    }
                }
            }
        }
    }
    rebuild();
    setStatus(QString(), false);
}

bool ManualControl::save()
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
    return true;
}

// Hand-formatted (QJsonDocument sorts keys): name, hotkey, send, mode.
QString ManualControl::toJson() const
{
    QString out = QStringLiteral("{\n");
    for (Group g : {Relays, Functions}) {
        out += g == Relays ? QStringLiteral("  \"relays\": [") : QStringLiteral("  \"functions\": [");
        const QList<Item> &items = m_items[g];
        for (int i = 0; i < items.size(); ++i) {
            const Item &it = items[i];
            out += QStringLiteral("\n    {\n      \"name\": %1,\n      \"hotkey\": %2,\n      \"send\": %3")
                       .arg(quoted(it.name), quoted(it.hotkey), quoted(it.send));
            if (g == Relays)
                out += QStringLiteral(",\n      \"mode\": %1").arg(quoted(it.hold ? QStringLiteral("hold") : QStringLiteral("toggle")));
            out += QStringLiteral("\n    }");
            if (i + 1 < items.size())
                out += QLatin1Char(',');
        }
        out += items.isEmpty() ? QStringLiteral("]") : QStringLiteral("\n  ]");
        out += g == Relays ? QStringLiteral(",\n") : QStringLiteral("\n");
    }
    out += QStringLiteral("}\n");
    return out;
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

QString ManualControl::buttonText(const Item &item) const
{
    if (item.hotkey.isEmpty())
        return item.name;
    return QKeySequence(item.hotkey, QKeySequence::PortableText).toString(QKeySequence::NativeText)
           + QStringLiteral(" - ") + item.name;
}

void ManualControl::rebuild()
{
    releaseKeys();
    for (Group g : {Relays, Functions}) {
        QLayoutItem *li;
        while ((li = m_lists[g]->takeAt(0)) != nullptr) {
            if (QWidget *w = li->widget()) {
                w->hide();
                w->deleteLater();   // may be the button whose menu triggered this
            }
            delete li;
        }
        m_buttons[g].clear();
        m_addButtons[g]->setEnabled(m_loadError.isEmpty());

        if (m_items[g].isEmpty()) {
            auto *empty = new QLabel(g == Relays ? tr("No relays yet. Click + Add.")
                                                 : tr("No functions yet. Click + Add."));
            empty->setObjectName(QStringLiteral("manualEmpty"));
            m_lists[g]->addWidget(empty);
        }
        for (int i = 0; i < m_items[g].size(); ++i) {
            QPushButton *b = createButton(g, i);
            m_buttons[g].append(b);
            m_lists[g]->addWidget(b);
        }
        m_lists[g]->addStretch(1);
    }
    m_json->setPlainText(m_loadError.isEmpty() ? toJson() : m_loadError + QStringLiteral("\n\n") + m_rawText);
    m_json->setToolTip(QDir::toNativeSeparators(QDir(m_dir).filePath(fileName())));
}

QPushButton *ManualControl::createButton(Group group, int index)
{
    const Item &item = m_items[group][index];
    auto *b = new QPushButton(buttonText(item));
    b->setObjectName(group == Relays ? QStringLiteral("relayButton") : QStringLiteral("functionButton"));
    b->setContextMenuPolicy(Qt::CustomContextMenu);
    b->setAutoDefault(false);
    b->installEventFilter(this);
    const QString sendText = quoted(item.send);
    QString tip;
    if (group == Functions)
        tip = tr("Sends %1").arg(sendText);
    else if (item.hold)
        tip = tr("Hold: sends %1 on press and again on release").arg(sendText);
    else
        tip = tr("Toggle: sends %1 on release (lit = ON)").arg(sendText);
    b->setToolTip(tip + tr("\nRight-click to edit, move or delete."));

    if (group == Relays && item.hold) {
        connect(b, &QPushButton::pressed, this, [this, index] { holdPress(index); });
        connect(b, &QPushButton::released, this, [this, index] { holdRelease(index); });
    } else if (group == Relays) {
        b->setCheckable(true);
        b->setChecked(item.latched);
        connect(b, &QPushButton::clicked, this, [this, index](bool checked) { onToggleClicked(index, checked); });
    } else {
        connect(b, &QPushButton::clicked, this, [this, index] { send(m_items[Functions].value(index), QString()); });
    }
    connect(b, &QPushButton::customContextMenuRequested, this, [this, b, group, index](const QPoint &pos) {
        showItemMenu(group, index, b->mapToGlobal(pos));
    });
    return b;
}

bool ManualControl::send(const Item &item, const QString &what)
{
    if (item.send.isEmpty())
        return false;
    const QString label = what.isEmpty() ? item.name : item.name + QStringLiteral(", ") + what;
    const QString error = m_sender(item.send.toUtf8(), label);
    if (error.isEmpty())
        setStatus(tr("Sent %1  (%2)").arg(quoted(item.send), label), false);
    else
        setStatus(tr("Not sent: %1").arg(error), true);
    return error.isEmpty();
}

void ManualControl::holdPress(int index)
{
    if (index >= m_items[Relays].size() || m_items[Relays][index].down)
        return;
    if (send(m_items[Relays][index], tr("press")))
        m_items[Relays][index].down = true;   // release is only sent if the press was
}

void ManualControl::holdRelease(int index)
{
    if (index >= m_items[Relays].size() || !m_items[Relays][index].down)
        return;
    m_items[Relays][index].down = false;
    send(m_items[Relays][index], tr("release"));
}

void ManualControl::onToggleClicked(int index, bool checked)
{
    if (index >= m_items[Relays].size())
        return;
    QPushButton *b = m_buttons[Relays].value(index);
    if (!send(m_items[Relays][index], checked ? tr("on") : tr("off"))) {
        if (b)
            b->setChecked(!checked);   // nothing reached the board: keep the old state
        return;
    }
    m_items[Relays][index].latched = checked;
}

void ManualControl::setStatus(const QString &text, bool isError)
{
    m_status->setText(text);
    m_status->setProperty("error", isError);
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
}

void ManualControl::updateFocusHint()
{
    QWidget *f = QApplication::focusWidget();
    const bool active = f && (f == this || isAncestorOf(f));
    m_focusHint->setText(active ? tr("Hotkeys active") : tr("Click here to use hotkeys"));
}

// ---------------------------------------------------------------------------
// Hotkeys (while the panel has focus)
// ---------------------------------------------------------------------------

int ManualControl::findHotkey(Group *group, const QKeyEvent *event) const
{
    const int key = event->key();
    if (isModifierKey(key))
        return -1;
    const Qt::KeyboardModifiers mods = event->modifiers()
        & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    QStringList candidates{QKeySequence(int(mods) | key).toString(QKeySequence::PortableText)};
    if (mods & Qt::ShiftModifier)   // symbols typed with Shift are recorded without it
        candidates << QKeySequence(int(mods & ~Qt::ShiftModifier) | key).toString(QKeySequence::PortableText);
    for (const QString &c : candidates) {
        for (Group g : {Relays, Functions}) {
            for (int i = 0; i < m_items[g].size(); ++i) {
                if (!m_items[g][i].hotkey.isEmpty() && m_items[g][i].hotkey == c) {
                    *group = g;
                    return i;
                }
            }
        }
    }
    return -1;
}

// Press: the button looks pressed (hold relays send now). Release: hold relays
// send again; toggles and functions are clicked (sent) now. The release is
// matched by key code alone, so letting go of Shift first doesn't lose it.
bool ManualControl::handleKey(QKeyEvent *event, bool press)
{
    Group group;
    if (press) {
        const int index = findHotkey(&group, event);
        if (index < 0)
            return false;
        if (event->isAutoRepeat() || m_keysDown.contains(event->key()))
            return true;
        m_keysDown.insert(event->key(), {group, index});
        if (QPushButton *b = m_buttons[group].value(index))
            b->setDown(true);
        if (group == Relays && m_items[Relays][index].hold)
            holdPress(index);
        return true;
    }

    if (!m_keysDown.contains(event->key()))
        return findHotkey(&group, event) >= 0;
    if (event->isAutoRepeat())
        return true;
    const KeyHold h = m_keysDown.take(event->key());
    QPushButton *b = m_buttons[h.group].value(h.index);
    if (b)
        b->setDown(false);
    if (h.group == Relays && m_items[Relays].value(h.index).hold)
        holdRelease(h.index);
    else if (b)
        b->click();
    return true;
}

void ManualControl::releaseKeys()
{
    const QHash<int, KeyHold> held = m_keysDown;
    m_keysDown.clear();
    for (const KeyHold &h : held) {
        if (QPushButton *b = m_buttons[h.group].value(h.index))
            b->setDown(false);
        if (h.group == Relays && m_items[Relays].value(h.index).hold)
            holdRelease(h.index);   // a hold must always end with its release
    }
}

bool ManualControl::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::ShortcutOverride: {
        // A hotkey wins over a menu shortcut while the panel has focus.
        Group g;
        if (findHotkey(&g, static_cast<QKeyEvent *>(event)) >= 0) {
            event->accept();
            return true;
        }
        break;
    }
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
        if (handleKey(static_cast<QKeyEvent *>(event), event->type() == QEvent::KeyPress))
            return true;
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Add / edit / move / delete
// ---------------------------------------------------------------------------

void ManualControl::addItem(Group group)
{
    Item item;
    if (!runDialog(group, item, -1, group == Relays ? tr("Add Relay") : tr("Add Function")))
        return;
    m_items[group].append(item);
    save();
    rebuild();
}

void ManualControl::editItem(Group group, int index)
{
    if (index >= m_items[group].size())
        return;
    Item item = m_items[group][index];
    if (!runDialog(group, item, index, group == Relays ? tr("Edit Relay") : tr("Edit Function")))
        return;
    if (item.hold != m_items[group][index].hold)
        item.latched = false;
    m_items[group][index] = item;
    save();
    rebuild();
}

void ManualControl::removeItem(Group group, int index)
{
    if (index >= m_items[group].size())
        return;
    const auto answer = QMessageBox::question(this, tr("Delete Button"),
                                              tr("Delete \"%1\"?").arg(m_items[group][index].name));
    if (answer != QMessageBox::Yes)
        return;
    m_items[group].removeAt(index);
    save();
    rebuild();
}

void ManualControl::moveItem(Group group, int index, int delta)
{
    const int to = index + delta;
    if (index >= m_items[group].size() || to < 0 || to >= m_items[group].size())
        return;
    m_items[group].swapItemsAt(index, to);
    save();
    rebuild();
}

void ManualControl::showItemMenu(Group group, int index, const QPoint &globalPos)
{
    if (!m_loadError.isEmpty())
        return;
    releaseKeys();
    QMenu menu(this);
    QAction *edit = menu.addAction(tr("Edit..."));
    QAction *up = menu.addAction(tr("Move Up"));
    QAction *down = menu.addAction(tr("Move Down"));
    menu.addSeparator();
    QAction *del = menu.addAction(tr("Delete"));
    up->setEnabled(index > 0);
    down->setEnabled(index + 1 < m_items[group].size());
    QAction *chosen = menu.exec(globalPos);
    // Deferred: the button that asked for this menu may be rebuilt (deleted).
    if (chosen == edit)
        QTimer::singleShot(0, this, [this, group, index] { editItem(group, index); });
    else if (chosen == up)
        QTimer::singleShot(0, this, [this, group, index] { moveItem(group, index, -1); });
    else if (chosen == down)
        QTimer::singleShot(0, this, [this, group, index] { moveItem(group, index, 1); });
    else if (chosen == del)
        QTimer::singleShot(0, this, [this, group, index] { removeItem(group, index); });
}

bool ManualControl::runDialog(Group group, Item &item, int editingIndex, const QString &title)
{
    releaseKeys();
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("manualDialog"));
    dlg.setWindowTitle(title);
    dlg.setWindowFlags(dlg.windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *layout = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto *name = new QLineEdit(item.name);
    name->setPlaceholderText(group == Relays ? tr("e.g. UPPER MIXER FWD") : tr("e.g. MIXER HOME"));
    name->setMinimumWidth(280);
    form->addRow(tr("Name"), name);

    auto *keyRow = new QHBoxLayout;
    auto *keyEdit = new QKeySequenceEdit(QKeySequence(item.hotkey, QKeySequence::PortableText));
    keyEdit->setToolTip(tr("Click, then press the key. Optional."));
    auto *clearKey = new QPushButton(tr("Clear"));
    clearKey->setAutoDefault(false);
    connect(clearKey, &QPushButton::clicked, keyEdit, &QKeySequenceEdit::clear);
    // One key only (QKeySequenceEdit records up to four).
    connect(keyEdit, &QKeySequenceEdit::keySequenceChanged, keyEdit, [keyEdit](const QKeySequence &seq) {
        if (seq.count() > 1)
            keyEdit->setKeySequence(QKeySequence(seq[0]));
    });
    keyRow->addWidget(keyEdit, 1);
    keyRow->addWidget(clearKey);
    form->addRow(tr("Hotkey"), keyRow);

    auto *sendEdit = new QLineEdit(item.send);
    sendEdit->setFont(Theme::monoFont(10));
    sendEdit->setPlaceholderText(group == Relays ? tr("e.g. q") : tr("e.g. MIXER_HOME"));
    form->addRow(tr("Send"), sendEdit);

    QComboBox *mode = nullptr;
    if (group == Relays) {
        mode = new QComboBox;
        mode->addItem(tr("Toggle: send once, on release"));
        mode->addItem(tr("Hold: send on press and again on release"));
        mode->setCurrentIndex(item.hold ? 1 : 0);
        form->addRow(tr("Mode"), mode);
    }
    layout->addLayout(form);

    auto *note = new QLabel(tr("Sent as typed, with no line ending, to the toolbar's port.\n"
                               "Hotkeys work while the Manual Control tab has focus."));
    note->setObjectName(QStringLiteral("manualNote"));
    layout->addWidget(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, [&] {
        if (name->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dlg, title, tr("Enter a name."));
            name->setFocus();
            return;
        }
        if (sendEdit->text().isEmpty()) {
            QMessageBox::warning(&dlg, title, tr("Enter the text to send."));
            sendEdit->setFocus();
            return;
        }
        const QKeySequence seq = keyEdit->keySequence();
        const QString key = seq.isEmpty() ? QString() : QKeySequence(seq[0]).toString(QKeySequence::PortableText);
        if (!key.isEmpty()) {
            for (Group g : {Relays, Functions}) {
                for (int i = 0; i < m_items[g].size(); ++i) {
                    if (g == group && i == editingIndex)
                        continue;
                    if (m_items[g][i].hotkey == key) {
                        QMessageBox::warning(&dlg, title, tr("Hotkey %1 is already used by \"%2\".")
                                                              .arg(QKeySequence(seq[0]).toString(QKeySequence::NativeText),
                                                                   m_items[g][i].name));
                        return;
                    }
                }
            }
        }
        dlg.accept();
    });
    name->setFocus();

    if (dlg.exec() != QDialog::Accepted)
        return false;
    item.name = name->text().trimmed();
    const QKeySequence seq = keyEdit->keySequence();
    item.hotkey = seq.isEmpty() ? QString() : QKeySequence(seq[0]).toString(QKeySequence::PortableText);
    item.send = sendEdit->text();
    item.hold = mode && mode->currentIndex() == 1;
    return true;
}
