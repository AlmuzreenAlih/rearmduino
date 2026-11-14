#include "PortableBuilder.h"
#include "Portable.h"
#ifdef Q_OS_WIN
#include "Activation.h"
#endif
#include "Theme.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QUuid>
#include <QThread>
#include <QVBoxLayout>

#include <functional>

namespace {

constexpr int LibPathRole = Qt::UserRole;
constexpr int CorePathRole = Qt::UserRole;       // "packages/<vendor>/hardware/<arch>/<version>"
constexpr int CoreIdRole = Qt::UserRole + 1;     // "<vendor>:<arch>"
const char kAvrId[] = "arduino:avr";

qint64 treeSize(const QString &path)
{
    QFileInfo fi(path);
    if (fi.isFile())
        return fi.size();
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

QString sizeText(qint64 bytes)
{
    if (bytes >= 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 KB").arg(qMax<qint64>(1, bytes / 1024));
}

// "1 library", "2,289 files"
QString countText(int n, const QString &one, const QString &many)
{
    return QStringLiteral("%1 %2").arg(QLocale().toString(n), n == 1 ? one : many);
}

// library.properties "key=value" (name, depends).
QString libraryProperty(const QString &libDir, const QString &key)
{
    QFile f(QDir(libDir).filePath(QStringLiteral("library.properties")));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(key + QLatin1Char('=')))
            return line.mid(key.size() + 1).trimmed();
    }
    return {};
}

// Header files a library provides (its root and src/, as arduino-cli looks there).
QSet<QString> libraryHeaders(const QString &libDir)
{
    QSet<QString> headers;
    for (const QString &sub : {QString(), QStringLiteral("src")}) {
        const QDir d(sub.isEmpty() ? libDir : QDir(libDir).filePath(sub));
        for (const QString &h : d.entryList({QStringLiteral("*.h"), QStringLiteral("*.hpp")}, QDir::Files))
            headers.insert(h.toLower());
    }
    return headers;
}

} // namespace

// One file or folder to copy.
struct CopyItem {
    QString from;
    QString to;
};

struct PortableBuilder::Job {
    QString target;     // <out>/<name>
    QString zipPath;    // empty: no zip
    QString ch340;      // folder, .zip or .exe; empty: none
    QString copyName;   // who the copy is for
    QList<CopyItem> items;
};

PortableBuilder::PortableBuilder(const QString &cliPath, const QProcessEnvironment &cliEnv, const QString &sketchDir,
                                 QWidget *parent)
    : QDialog(parent), m_cli(cliPath), m_env(cliEnv), m_sketchDir(sketchDir)
{
    setObjectName(QStringLiteral("portableBuilder"));
    setWindowTitle(tr("Create Portable Version"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    resize(720, 760);
    QSettings s;

    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(tr("Makes a folder you can copy or zip and send. It runs on another Windows PC "
                                "without installing anything: this app, arduino-cli, the boards you tick (AVR: Uno, Mega, "
                                "Nano... is always included) with their compilers, the libraries you tick, and USB "
                                "drivers. Its settings stay inside the folder. It also uses the libraries in the other "
                                "PC's Documents\\Arduino\\libraries."));
    intro->setWordWrap(true);
    intro->setObjectName(QStringLiteral("manualNote"));
    layout->addWidget(intro);

    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *outRow = new QHBoxLayout;
    m_outDir = new QLineEdit(s.value(QStringLiteral("portable/outDir"),
                                     QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)).toString());
    auto *browseOut = new QPushButton(tr("Browse..."));
    connect(browseOut, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Save the portable version in"), m_outDir->text());
        if (!d.isEmpty())
            m_outDir->setText(QDir::toNativeSeparators(d));
    });
    outRow->addWidget(m_outDir, 1);
    outRow->addWidget(browseOut);
    form->addRow(tr("Save in"), outRow);
    m_name = new QLineEdit(s.value(QStringLiteral("portable/name"), QStringLiteral("REARMduino-portable")).toString());
    form->addRow(tr("Folder name"), m_name);
    m_copyName = new QLineEdit;
    m_copyName->setPlaceholderText(tr("Who this copy is for, e.g. Juan's laptop (shown when it asks for approval)"));
    form->addRow(tr("Copy name"), m_copyName);

    auto *chRow = new QHBoxLayout;
    m_ch340 = new QLineEdit(s.value(QStringLiteral("portable/ch340")).toString());
    m_ch340->setPlaceholderText(tr("Optional: CH340 driver folder, .zip or .exe (for clone boards)"));
    auto *chFolder = new QPushButton(tr("Folder..."));
    auto *chFile = new QPushButton(tr("File..."));
    connect(chFolder, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("CH340 driver folder"), m_ch340->text());
        if (!d.isEmpty())
            m_ch340->setText(QDir::toNativeSeparators(d));
    });
    connect(chFile, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, tr("CH340 driver"), m_ch340->text(),
                                                       tr("Driver package (*.zip *.exe)"));
        if (!f.isEmpty())
            m_ch340->setText(QDir::toNativeSeparators(f));
    });
    chRow->addWidget(m_ch340, 1);
    chRow->addWidget(chFolder);
    chRow->addWidget(chFile);
    form->addRow(tr("CH340 driver"), chRow);
    layout->addLayout(form);

    // Boards (cores)
    auto *coreTitle = new QLabel(tr("Boards to include"));
    coreTitle->setObjectName(QStringLiteral("manualTitle"));
    layout->addWidget(coreTitle);
    m_cores = new QListWidget;
    m_cores->setObjectName(QStringLiteral("boardList"));
    layout->addWidget(m_cores, 1);

    // Libraries
    auto *libHeader = new QHBoxLayout;
    auto *libTitle = new QLabel(tr("Libraries to include"));
    libTitle->setObjectName(QStringLiteral("manualTitle"));
    m_libCount = new QLabel;
    m_libCount->setObjectName(QStringLiteral("manualNote"));
    auto *usedButton = new QPushButton(tr("Used by This Sketch"));
    usedButton->setToolTip(tr("Tick the libraries the open sketch #includes, and what they depend on"));
    auto *allButton = new QPushButton(tr("All"));
    auto *noneButton = new QPushButton(tr("None"));
    connect(usedButton, &QPushButton::clicked, this, &PortableBuilder::selectUsedBySketch);
    connect(allButton, &QPushButton::clicked, this, [this] { setAllLibraries(true); });
    connect(noneButton, &QPushButton::clicked, this, [this] { setAllLibraries(false); });
    libHeader->addWidget(libTitle);
    libHeader->addSpacing(8);
    libHeader->addWidget(m_libCount, 1);
    libHeader->addWidget(usedButton);
    libHeader->addWidget(allButton);
    libHeader->addWidget(noneButton);
    layout->addLayout(libHeader);
    m_libs = new QListWidget;
    m_libs->setObjectName(QStringLiteral("boardList"));
    connect(m_libs, &QListWidget::itemChanged, this, &PortableBuilder::updateLibraryCount);
    layout->addWidget(m_libs, 2);

    m_zip = new QCheckBox(tr("Also make a .zip of the folder (to send)"));
    m_zip->setChecked(s.value(QStringLiteral("portable/zip"), true).toBool());
    layout->addWidget(m_zip);

    m_progress = new QProgressBar;
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setMaximumHeight(8);
    layout->addWidget(m_progress);
    m_log = new QPlainTextEdit;
    m_log->setObjectName(QStringLiteral("output"));
    m_log->setReadOnly(true);
    m_log->setFont(Theme::monoFont(9));
    layout->addWidget(m_log, 1);

    auto *buttons = new QDialogButtonBox;
    m_buildButton = buttons->addButton(tr("Create"), QDialogButtonBox::AcceptRole);
    m_closeButton = buttons->addButton(QDialogButtonBox::Close);
    connect(m_buildButton, &QPushButton::clicked, this, &PortableBuilder::build);
    connect(m_closeButton, &QPushButton::clicked, this, &PortableBuilder::reject);
    layout->addWidget(buttons);

    loadCores();
    loadLibraries();
}

PortableBuilder::~PortableBuilder()
{
    if (m_worker) {
        m_cancel = true;
        m_worker->wait();
    }
}

void PortableBuilder::reject()
{
    if (m_worker) {
        if (QMessageBox::question(this, windowTitle(), tr("Stop creating the portable version?")) != QMessageBox::Yes)
            return;
        m_cancel = true;
        log(tr("Stopping..."));
        return;   // finished() closes nothing; the user closes after it stops
    }
    QDialog::reject();
}

QString PortableBuilder::cliConfig(const QString &key) const
{
    if (m_cli.isEmpty())
        return {};
    QProcess p;
    p.setProcessEnvironment(m_env);
    p.start(m_cli, {QStringLiteral("config"), QStringLiteral("get"), key});
    if (!p.waitForFinished(15000) || p.exitCode() != 0)
        return {};
    return QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
}

// Every installed core, from <data>/packages/<vendor>/hardware/<arch>/<version>. AVR is always
// bundled (ticked and locked); the others are remembered from last time.
void PortableBuilder::loadCores()
{
    m_dataDir = cliConfig(QStringLiteral("directories.data"));
    const QDir packages(QDir(m_dataDir).filePath(QStringLiteral("packages")));
    const QStringList saved = QSettings().value(QStringLiteral("portable/cores")).toStringList();
    const QDir::Filters dirs = QDir::Dirs | QDir::NoDotAndDotDot;
    for (const QString &vendor : packages.entryList(dirs, QDir::Name)) {
        const QDir hardware(packages.filePath(vendor + QStringLiteral("/hardware")));
        for (const QString &arch : hardware.entryList(dirs, QDir::Name)) {
            for (const QString &version : QDir(hardware.filePath(arch)).entryList(dirs, QDir::Name)) {
                const QString path = QStringLiteral("packages/%1/hardware/%2/%3").arg(vendor, arch, version);
                const QString id = vendor + QLatin1Char(':') + arch;
                // Display name ("Arduino AVR Boards") from the core's installed.json.
                QString name;
                QFile installed(QDir(m_dataDir).filePath(path + QStringLiteral("/installed.json")));
                if (installed.open(QIODevice::ReadOnly)) {
                    const QJsonArray pkgs = QJsonDocument::fromJson(installed.readAll()).object()
                                                .value(QStringLiteral("packages")).toArray();
                    for (const QJsonValue &pkg : pkgs) {
                        for (const QJsonValue &pl : pkg.toObject().value(QStringLiteral("platforms")).toArray()) {
                            if (pl.toObject().value(QStringLiteral("architecture")).toString() == arch)
                                name = pl.toObject().value(QStringLiteral("name")).toString();
                        }
                    }
                }
                const bool isAvr = id == QLatin1String(kAvrId);
                auto *item = new QListWidgetItem(
                    QStringLiteral("%1    (%2 %3)%4").arg(name.isEmpty() ? id : name, id, version,
                                                           isAvr ? tr("    always included") : QString()));
                item->setData(CorePathRole, path);
                item->setData(CoreIdRole, id);
                item->setFlags(isAvr ? item->flags() & ~Qt::ItemIsEnabled : item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(isAvr || saved.contains(id) ? Qt::Checked : Qt::Unchecked);
                m_cores->addItem(item);
            }
        }
    }
}

void PortableBuilder::loadLibraries()
{
    m_userDir = cliConfig(QStringLiteral("directories.user"));
    const QString libRoot = QDir(m_userDir).filePath(QStringLiteral("libraries"));
    const QStringList saved = QSettings().value(QStringLiteral("portable/libraries")).toStringList();
    QSignalBlocker block(m_libs);
    for (const QFileInfo &fi : QDir(libRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
        auto *item = new QListWidgetItem(QStringLiteral("%1    (%2)").arg(fi.fileName(), sizeText(treeSize(fi.filePath()))));
        item->setData(LibPathRole, fi.absoluteFilePath());
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(saved.contains(fi.fileName()) ? Qt::Checked : Qt::Unchecked);
        const QString name = libraryProperty(fi.filePath(), QStringLiteral("name"));
        if (!name.isEmpty() && name != fi.fileName())
            item->setToolTip(name);
        m_libs->addItem(item);
    }
    if (m_libs->count() == 0)
        log(m_userDir.isEmpty() ? tr("Could not ask arduino-cli for the sketchbook folder; no libraries listed.")
                                : tr("No libraries found in %1.").arg(QDir::toNativeSeparators(libRoot)));
    updateLibraryCount();
}

void PortableBuilder::updateLibraryCount()
{
    int checked = 0;
    for (int i = 0; i < m_libs->count(); ++i)
        checked += m_libs->item(i)->checkState() == Qt::Checked;
    m_libCount->setText(tr("%1 of %2 selected, from %3").arg(checked).arg(m_libs->count())
                            .arg(QDir::toNativeSeparators(QDir(m_userDir).filePath(QStringLiteral("libraries")))));
}

void PortableBuilder::setAllLibraries(bool checked)
{
    for (int i = 0; i < m_libs->count(); ++i)
        m_libs->item(i)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
}

void PortableBuilder::selectUsedBySketch()
{
    // #include <X.h> / "X.h" in the sketch's files.
    static const QRegularExpression includeRe(QStringLiteral("^\\s*#\\s*include\\s*[<\"]([^>\"]+)[>\"]"),
                                              QRegularExpression::MultilineOption);
    QSet<QString> wanted;
    const QDir sketch(m_sketchDir);
    for (const QString &f : sketch.entryList({QStringLiteral("*.ino"), QStringLiteral("*.h"), QStringLiteral("*.cpp"),
                                              QStringLiteral("*.c"), QStringLiteral("*.hpp")}, QDir::Files)) {
        QFile file(sketch.filePath(f));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        auto it = includeRe.globalMatch(QString::fromUtf8(file.readAll()));
        while (it.hasNext())
            wanted.insert(QFileInfo(it.next().captured(1)).fileName().toLower());
    }

    // Header -> library rows, and library name -> row (for "depends=").
    QList<int> queue;
    QHash<QString, int> byName;
    for (int i = 0; i < m_libs->count(); ++i) {
        const QString dir = m_libs->item(i)->data(LibPathRole).toString();
        byName.insert(QFileInfo(dir).fileName().toLower(), i);
        const QString name = libraryProperty(dir, QStringLiteral("name"));
        if (!name.isEmpty())
            byName.insert(name.toLower(), i);
        if (libraryHeaders(dir).intersects(wanted))
            queue.append(i);
    }
    QSet<int> picked;
    while (!queue.isEmpty()) {
        const int i = queue.takeFirst();
        if (picked.contains(i))
            continue;
        picked.insert(i);
        const QString depends = libraryProperty(m_libs->item(i)->data(LibPathRole).toString(), QStringLiteral("depends"));
        for (QString dep : depends.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            dep = dep.section(QLatin1Char('('), 0, 0).trimmed().toLower();   // "Name (>=1.0)"
            if (byName.contains(dep))
                queue.append(byName.value(dep));
        }
    }
    for (int i : picked)
        m_libs->item(i)->setCheckState(Qt::Checked);
    log(picked.isEmpty() ? tr("The open sketch uses none of these libraries.")
                         : tr("Ticked %1 used by %2.").arg(countText(picked.size(), tr("library"), tr("libraries")),
                                                           sketch.dirName()));
}

void PortableBuilder::log(const QString &line, bool isError)
{
    m_log->appendPlainText((isError ? tr("Error: ") : QString()) + line);
}

void PortableBuilder::build()
{
    const QString outDir = QDir::fromNativeSeparators(m_outDir->text().trimmed());
    const QString name = m_name->text().trimmed();
    if (outDir.isEmpty() || !QFileInfo(outDir).isDir()) {
        QMessageBox::warning(this, windowTitle(), tr("Choose an existing folder to save in."));
        return;
    }
    if (name.isEmpty() || name.contains(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")))) {
        QMessageBox::warning(this, windowTitle(), tr("Enter a folder name without \\ / : * ? \" < > |."));
        return;
    }
    const QString ch340 = QDir::fromNativeSeparators(m_ch340->text().trimmed());
    if (!ch340.isEmpty() && !QFileInfo::exists(ch340)) {
        QMessageBox::warning(this, windowTitle(), tr("The CH340 driver path does not exist."));
        return;
    }
    m_log->clear();

    Job job;
    job.target = QDir(outDir).filePath(name);
    job.zipPath = m_zip->isChecked() ? job.target + QStringLiteral(".zip") : QString();
    job.ch340 = ch340;
    job.copyName = m_copyName->text().trimmed().isEmpty() ? name : m_copyName->text().trimmed();

    // The ESP8266 compiler needs a much shorter folder path than the others (Portable.h).
    bool hasEsp8266 = false;
    for (int i = 0; i < m_cores->count(); ++i)
        hasEsp8266 |= m_cores->item(i)->checkState() == Qt::Checked
                      && m_cores->item(i)->data(CoreIdRole).toString() == QLatin1String(Portable::kEsp8266Id);
    const int maxPath = hasEsp8266 ? Portable::kMaxFolderPathEsp8266 : Portable::kMaxFolderPath;
    if (QDir::toNativeSeparators(job.target).size() > maxPath
        && QMessageBox::question(this, windowTitle(),
               tr("%1 is a long path (%2 characters). Compiling%4 fails from folders with paths longer than "
                  "about %3 characters (a Windows limit), so this copy would only work after being moved "
                  "somewhere shorter, like C:\\REARMduino.\n\nCreate it here anyway?")
                   .arg(QDir::toNativeSeparators(job.target)).arg(QDir::toNativeSeparators(job.target).size())
                   .arg(maxPath).arg(hasEsp8266 ? tr(" for ESP8266 boards") : QString()))
               != QMessageBox::Yes)
        return;
    if (hasEsp8266)
        log(tr("Note: ESP8266 boards only compile when this folder's path is at most about %1 characters "
               "on the PC where it is used (for example C:\\REARMduino). Other boards allow about %2.")
                .arg(Portable::kMaxFolderPathEsp8266).arg(Portable::kMaxFolderPath));

    // Never delete a folder that isn't an earlier portable build.
    if (QFileInfo::exists(job.target)) {
        if (!QFileInfo::exists(QDir(job.target).filePath(Portable::markerFileName()))) {
            QMessageBox::warning(this, windowTitle(), tr("%1 already exists and is not a portable version made "
                                                         "here. Choose another folder name.")
                                                          .arg(QDir::toNativeSeparators(job.target)));
            return;
        }
        if (QMessageBox::question(this, windowTitle(), tr("Replace the existing portable version in %1?")
                                                            .arg(QDir::toNativeSeparators(job.target)))
            != QMessageBox::Yes)
            return;
    }
    if (!job.zipPath.isEmpty() && QFileInfo::exists(job.zipPath)
        && QMessageBox::question(this, windowTitle(), tr("Replace %1?").arg(QDir::toNativeSeparators(job.zipPath)))
               != QMessageBox::Yes)
        return;

    // ---- What to copy (checked here, on the UI thread, so problems show before anything is written)
    const QDir app(Portable::appDir());
    if (app.exists(QStringLiteral("Qt5Cored.dll"))) {
        QMessageBox::warning(this, windowTitle(), tr("This is a Debug build; its DLLs don't run on other PCs. "
                                                     "Create the portable version from the Release build "
                                                     "(build-win-Release\\REARMduino.exe)."));
        return;
    }
    const QDir dst(job.target);
    job.items.append({app.filePath(QStringLiteral("REARMduino.exe")), dst.filePath(QStringLiteral("REARMduino.exe"))});
    for (const QString &dll : app.entryList({QStringLiteral("*.dll")}, QDir::Files))
        job.items.append({app.filePath(dll), dst.filePath(dll)});
    for (const QString &plugins : {QStringLiteral("platforms"), QStringLiteral("styles"), QStringLiteral("imageformats"),
                                   QStringLiteral("iconengines"), QStringLiteral("printsupport")}) {
        if (app.exists(plugins))
            job.items.append({app.filePath(plugins), dst.filePath(plugins)});
    }
    // Visual C++ runtime next to the exe (app-local), so the other PC needs no redistributable.
    const QString sysDir = QDir::fromNativeSeparators(qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows")))
                           + QStringLiteral("/System32");
    for (const QString &dll : {QStringLiteral("vcruntime140.dll"), QStringLiteral("vcruntime140_1.dll"),
                               QStringLiteral("msvcp140.dll"), QStringLiteral("msvcp140_1.dll")}) {
        if (!app.exists(dll) && QFileInfo::exists(QDir(sysDir).filePath(dll)))
            job.items.append({QDir(sysDir).filePath(dll), dst.filePath(dll)});
    }

    if (m_cli.isEmpty()) {
        log(tr("arduino-cli was not found."), true);
        return;
    }
    job.items.append({m_cli, dst.filePath(QStringLiteral("arduino-cli/") + QFileInfo(m_cli).fileName())});

    const QDir data(m_dataDir);
    QStringList corePaths;   // ticked cores, as paths under the data folder
    QStringList coreIds;     // for the settings (without AVR, which is always there)
    QStringList coreLabels;  // for the log
    bool hasAvr = false;
    for (int i = 0; i < m_cores->count(); ++i) {
        const QListWidgetItem *item = m_cores->item(i);
        if (item->checkState() != Qt::Checked)
            continue;
        const QString id = item->data(CoreIdRole).toString();
        corePaths << item->data(CorePathRole).toString();
        coreLabels << QStringLiteral("%1 %2").arg(id, QFileInfo(corePaths.last()).fileName());
        if (id == QLatin1String(kAvrId))
            hasAvr = true;
        else if (!coreIds.contains(id))
            coreIds << id;
    }
    if (m_dataDir.isEmpty() || !hasAvr) {
        log(tr("The AVR core (arduino:avr) is not installed here, so it can't be bundled. "
               "Install it once with: arduino-cli core install arduino:avr"), true);
        return;
    }
    const QDir dstData(dst.filePath(QStringLiteral("arduino/data")));
    // Indexes: without them arduino-cli downloads them on every command (fails offline).
    // package_*index.json: other vendors' board indexes (ESP32, ESP8266...).
    for (const QString &f : data.entryList({QStringLiteral("package_index.json*"), QStringLiteral("package_*index.json*"),
                                            QStringLiteral("library_index.json*")}, QDir::Files))
        job.items.append({data.filePath(f), dstData.filePath(f)});
    // Discovery/monitor tools arduino-cli needs to list ports and upload.
    if (data.exists(QStringLiteral("packages/builtin")))
        job.items.append({data.filePath(QStringLiteral("packages/builtin")), dstData.filePath(QStringLiteral("packages/builtin"))});
    QSet<QString> toolsAdded;   // cores can share a tool (ESP32 uses arduino's dfu-util)
    for (const QString &platform : corePaths) {
        job.items.append({data.filePath(platform), dstData.filePath(platform)});
        if (platform.startsWith(QLatin1String("packages/arduino/hardware/avr/")))
            job.items.append({data.filePath(platform + QStringLiteral("/drivers")), dst.filePath(QStringLiteral("drivers/arduino"))});
        // Its tools (compiler, uploader...) at the versions it was installed with.
        QFile installed(data.filePath(platform + QStringLiteral("/installed.json")));
        if (installed.open(QIODevice::ReadOnly)) {
            const QJsonArray packages = QJsonDocument::fromJson(installed.readAll()).object().value(QStringLiteral("packages")).toArray();
            for (const QJsonValue &pkg : packages) {
                for (const QJsonValue &pl : pkg.toObject().value(QStringLiteral("platforms")).toArray()) {
                    for (const QJsonValue &t : pl.toObject().value(QStringLiteral("toolsDependencies")).toArray()) {
                        const QJsonObject o = t.toObject();
                        const QString tool = QStringLiteral("packages/%1/tools/%2/%3")
                                                 .arg(o.value(QStringLiteral("packager")).toString(),
                                                      o.value(QStringLiteral("name")).toString(),
                                                      o.value(QStringLiteral("version")).toString());
                        if (toolsAdded.contains(tool))
                            continue;
                        toolsAdded.insert(tool);
                        if (data.exists(tool))
                            job.items.append({data.filePath(tool), dstData.filePath(tool)});
                        else
                            log(tr("Warning: tool %1 is missing here; uploads may need it.").arg(tool));
                    }
                }
            }
        }
    }

    QStringList libNames;
    for (int i = 0; i < m_libs->count(); ++i) {
        QListWidgetItem *item = m_libs->item(i);
        if (item->checkState() != Qt::Checked)
            continue;
        const QString dir = item->data(LibPathRole).toString();
        libNames << QFileInfo(dir).fileName();
        job.items.append({dir, dst.filePath(QStringLiteral("arduino/user/libraries/") + QFileInfo(dir).fileName())});
    }

    QSettings s;
    s.setValue(QStringLiteral("portable/outDir"), QDir::toNativeSeparators(outDir));
    s.setValue(QStringLiteral("portable/name"), name);
    s.setValue(QStringLiteral("portable/ch340"), m_ch340->text().trimmed());
    s.setValue(QStringLiteral("portable/zip"), m_zip->isChecked());
    s.setValue(QStringLiteral("portable/libraries"), libNames);
    s.setValue(QStringLiteral("portable/cores"), coreIds);

    log(tr("Creating %1 (boards: %2; %3%4)...")
            .arg(QDir::toNativeSeparators(job.target), coreLabels.join(QStringLiteral(", ")),
                 countText(libNames.size(), tr("library"), tr("libraries")),
                 ch340.isEmpty() ? tr(", no CH340 driver") : tr(", CH340 driver")));
    m_buildButton->setEnabled(false);
    m_progress->setValue(0);
    m_cancel = false;

    // ---- Worker: copying ~300 MB takes a while; keep the UI responsive.
    auto post = [this](std::function<void()> f) { QMetaObject::invokeMethod(this, std::move(f), Qt::QueuedConnection); };
    m_worker = QThread::create([this, job, post] {
        auto fail = [&](const QString &msg) { post([this, msg] { finished(false, msg); }); };
        auto say = [&](const QString &msg) { post([this, msg] { log(msg); }); };

        if (QFileInfo::exists(job.target) && !QDir(job.target).removeRecursively())
            return fail(tr("Could not remove the old %1 (is it open?).").arg(QDir::toNativeSeparators(job.target)));
        if (!job.zipPath.isEmpty() && QFileInfo::exists(job.zipPath) && !QFile::remove(job.zipPath))
            return fail(tr("Could not replace %1 (is it open?).").arg(QDir::toNativeSeparators(job.zipPath)));

        // Expand folders into files, then copy with a byte-based progress bar.
        QList<CopyItem> files;
        qint64 total = 0;
        for (const CopyItem &item : job.items) {
            const QFileInfo fi(item.from);
            if (fi.isFile()) {
                files.append(item);
                total += fi.size();
                continue;
            }
            QDirIterator it(item.from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                files.append({it.filePath(), QDir(item.to).filePath(QDir(item.from).relativeFilePath(it.filePath()))});
                total += it.fileInfo().size();
            }
        }
        say(tr("Copying %1, %2...").arg(countText(files.size(), tr("file"), tr("files")), sizeText(total)));
        qint64 done = 0;
        int lastPermille = -1;
        for (const CopyItem &f : files) {
            if (m_cancel)
                return fail(tr("Stopped. The folder is incomplete; delete it or create it again."));
            QDir().mkpath(QFileInfo(f.to).absolutePath());
            if (!QFile::copy(f.from, f.to))
                return fail(tr("Could not copy %1").arg(QDir::toNativeSeparators(f.from)));
            done += QFileInfo(f.to).size();
            const int permille = total ? int(done * 900 / total) : 900;
            if (permille != lastPermille) {
                lastPermille = permille;
                post([this, permille] { m_progress->setValue(permille); });
            }
        }

        // CH340 driver: a folder (copied), a .zip (extracted with Windows' tar) or an installer .exe.
        if (!job.ch340.isEmpty()) {
            const QString chDst = QDir(job.target).filePath(QStringLiteral("drivers/CH340"));
            QDir().mkpath(chDst);
            const QFileInfo src(job.ch340);
            bool ok = true;
            if (src.isDir()) {
                QDirIterator it(job.ch340, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
                while (ok && it.hasNext()) {
                    it.next();
                    const QString to = QDir(chDst).filePath(QDir(job.ch340).relativeFilePath(it.filePath()));
                    QDir().mkpath(QFileInfo(to).absolutePath());
                    ok = QFile::copy(it.filePath(), to);
                }
            } else if (src.suffix().compare(QLatin1String("zip"), Qt::CaseInsensitive) == 0) {
                QProcess tar;
                tar.start(QStringLiteral("tar"), {QStringLiteral("-xf"), QDir::toNativeSeparators(job.ch340),
                                                  QStringLiteral("-C"), QDir::toNativeSeparators(chDst)});
                ok = tar.waitForFinished(120000) && tar.exitCode() == 0;
            } else {
                ok = QFile::copy(job.ch340, QDir(chDst).filePath(src.fileName()));
            }
            if (!ok)
                return fail(tr("Could not add the CH340 driver from %1").arg(QDir::toNativeSeparators(job.ch340)));
            say(tr("Added the CH340 driver."));
        }

        // Marker (turns on portable mode) + a short readme for the recipient.
        QFile marker(QDir(job.target).filePath(Portable::markerFileName()));
        if (!marker.open(QIODevice::WriteOnly | QIODevice::Text))
            return fail(tr("Could not write %1").arg(Portable::markerFileName()));
        marker.write(
            "REARMduino portable version\r\n"
            "===========================\r\n\r\n"
            "Start: double-click REARMduino.exe. Nothing needs to be installed.\r\n"
            "The first start on a PC shows a request code: send it to REARM Automation to have this\r\n"
            "PC approved (needs internet that one time). After that it also works offline.\r\n"
            "Keep this folder together (do not move REARMduino.exe out of it), somewhere you can write\r\n"
            "(e.g. Desktop or Documents, not Program Files), with a short path: compiling fails if\r\n"
            "the folder's path is longer than about 100 characters, or about 55 for ESP8266 boards\r\n"
            "(a Windows limit). C:\\REARMduino always works.\r\n\r\n"
            "Included: arduino-cli, the boards in arduino\\data (AVR: Uno, Mega, Nano, ... and any others\r\n"
            "that were added), libraries in arduino\\user\\libraries, USB drivers in drivers\\.\r\n"
            "Libraries in this PC's Documents\\Arduino\\libraries are used too. Settings are saved in settings\\.\r\n\r\n"
            "Board not showing up? Tools > Check USB Drivers installs the bundled driver if the\r\n"
            "plugged-in board has none (Windows asks for administrator permission).\r\n\r\n"
            "This file switches on portable mode; do not delete it.\r\n");
        marker.close();

#ifdef Q_OS_WIN
        // This copy's identity for activation (Activation.h): a fresh random ID and its name.
        // A license is never copied: settings/ is not among the copied items.
        QFile copyFile(QDir(job.target).filePath(Activation::copyFileName()));
        if (!copyFile.open(QIODevice::WriteOnly))
            return fail(tr("Could not write %1").arg(Activation::copyFileName()));
        copyFile.write(QJsonDocument(QJsonObject{
            {QStringLiteral("copyId"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {QStringLiteral("copyName"), job.copyName},
        }).toJson());
        copyFile.close();
#endif

        if (!job.zipPath.isEmpty()) {
            say(tr("Zipping..."));
            post([this] { m_progress->setValue(920); });
            QProcess tar;
            tar.setWorkingDirectory(QFileInfo(job.target).absolutePath());
            tar.start(QStringLiteral("tar"), {QStringLiteral("-a"), QStringLiteral("-c"), QStringLiteral("-f"),
                                              QDir::toNativeSeparators(job.zipPath), QFileInfo(job.target).fileName()});
            if (!tar.waitForStarted(10000))
                return fail(tr("Could not start tar.exe to make the zip (it comes with Windows 10 and later)."));
            while (!tar.waitForFinished(500)) {
                if (m_cancel) {
                    tar.kill();
                    tar.waitForFinished();
                    QFile::remove(job.zipPath);
                    return fail(tr("Stopped while zipping; the folder is complete, the zip was removed."));
                }
            }
            if (tar.exitCode() != 0)
                return fail(tr("Zipping failed: %1").arg(QString::fromLocal8Bit(tar.readAllStandardError()).trimmed()));
        }
        const QString summary = job.zipPath.isEmpty()
            ? tr("Done: %1 (%2).").arg(QDir::toNativeSeparators(job.target), sizeText(total))
            : tr("Done: %1 (%2), and %3 (%4) to send.")
                  .arg(QDir::toNativeSeparators(job.target), sizeText(total), QDir::toNativeSeparators(job.zipPath),
                       sizeText(QFileInfo(job.zipPath).size()));
        post([this, summary] { finished(true, summary); });
    });
    connect(m_worker, &QThread::finished, m_worker, &QObject::deleteLater);
    m_worker->start();
}

void PortableBuilder::finished(bool ok, const QString &summary)
{
    if (m_worker) {
        m_worker->wait();
        m_worker = nullptr;   // deleteLater is connected to finished
    }
    m_progress->setValue(ok ? 1000 : m_progress->value());
    log(summary, !ok);
    m_buildButton->setEnabled(true);
}
