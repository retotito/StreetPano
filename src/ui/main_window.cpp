#include "ui/main_window.hpp"

#include <QAbstractItemModel>
#include <QAction>
#include <QCursor>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QListWidgetItem>
#include <QMenuBar>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/photo_listing.hpp"
#include "ui/film_strip_delegate.hpp"
#include "ui/qt_ownership.hpp"

namespace streetpano::ui {
namespace {

constexpr int kPreviewThreads = 1;
constexpr int kStripExtraHeight = 20;  // Room for the scroll bar.
constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;

[[nodiscard]] QString toQString(const std::filesystem::path& path) {
    return QString::fromStdU16String(path.u16string());
}

}  // namespace

MainWindow::MainWindow(std::size_t previewBudgetBytes, QWidget* parent)
    : QMainWindow(parent),
      model_(makeChild<PhotoListModel>(this, PhotoListModel::kDefaultBudgetBytes)),
      view_(makeChild<PhotoView>(this)),
      strip_(makeChild<QListView>(this)),
      positionLabel_(makeChild<QLabel>(this)),
      memoryLabel_(makeChild<QLabel>(this)),
      previewCache_(previewBudgetBytes) {
    setWindowTitle(QStringLiteral("StreetPano"));
    resize(1280, 800);
    createPreviewLoader();

    strip_->setModel(model_);
    strip_->setViewMode(QListView::IconMode);
    strip_->setFlow(QListView::LeftToRight);
    strip_->setWrapping(false);
    strip_->setResizeMode(QListView::Adjust);
    strip_->setMovement(QListView::Static);
    strip_->setUniformItemSizes(true);
    strip_->setItemDelegate(makeChild<FilmStripDelegate>(strip_));
    strip_->setFixedHeight(FilmStripDelegate::kThumbnailHeight + FilmStripDelegate::kCaptionHeight +
                           2 * FilmStripDelegate::kPadding + kStripExtraHeight);
    strip_->setSelectionMode(QAbstractItemView::SingleSelection);
    strip_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    strip_->setStyleSheet(
        QStringLiteral("QListView { background: rgb(32, 34, 37); border: none; }"));

    auto* splitter = makeChild<QSplitter>(this, Qt::Vertical);
    splitter->addWidget(view_);
    splitter->addWidget(strip_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    setCentralWidget(splitter);

    statusBar()->addWidget(positionLabel_);
    statusBar()->addPermanentWidget(memoryLabel_);

    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    QAction* openAction = fileMenu->addAction(tr("&Open Folder…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::chooseFolder);
    QAction* quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    connect(strip_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) { showPhoto(current.row()); });
    connect(model_, &PhotoListModel::cacheChanged, this, &MainWindow::updateStatus);

    auto* stitchMenu = menuBar()->addMenu(tr("&Stitch"));
    QAction* stitchAction = stitchMenu->addAction(tr("&Quick Stitch…"));
    stitchAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    connect(stitchAction, &QAction::triggered, this, &MainWindow::chooseStitch);
    showPanoramaAction_ = stitchMenu->addAction(tr("Show &Panorama"));
    showPanoramaAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    showPanoramaAction_->setEnabled(false);
    connect(showPanoramaAction_, &QAction::triggered, this, &MainWindow::showPanorama);

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    undoAction_ = editMenu->addAction(tr("&Undo"));
    undoAction_->setShortcut(QKeySequence::Undo);
    connect(undoAction_, &QAction::triggered, this, [this] { undo(); });
    redoAction_ = editMenu->addAction(tr("&Redo"));
    redoAction_->setShortcut(QKeySequence::Redo);
    connect(redoAction_, &QAction::triggered, this, [this] { redo(); });
    editMenu->addSeparator();
    markAreaAction_ = editMenu->addAction(tr("&Mark an Area"));
    markAreaAction_->setCheckable(true);
    markAreaAction_->setShortcut(QKeySequence(Qt::Key_L));
    markAreaAction_->setToolTip(tr("Drag an outline around an object on the panorama"));
    connect(markAreaAction_, &QAction::toggled, view_, &PhotoView::setLassoMode);
    removeObjectAction_ = editMenu->addAction(tr("&Remove This Object"));
    removeObjectAction_->setToolTip(tr("Fill the area from the photo that shows it cleanest"));
    connect(removeObjectAction_, &QAction::triggered, this, [this] { removeObject(); });
    keepObjectAction_ = editMenu->addAction(tr("&Keep This Object Whole"));
    keepObjectAction_->setToolTip(tr("Take all of the area from one photo"));
    connect(keepObjectAction_, &QAction::triggered, this, [this] { keepObject(); });
    nextAreaPhotoAction_ = editMenu->addAction(tr("Try a &Different Photo"));
    nextAreaPhotoAction_->setShortcut(QKeySequence(Qt::Key_N));
    nextAreaPhotoAction_->setToolTip(tr("Not right? Fill the area from another photo"));
    connect(nextAreaPhotoAction_, &QAction::triggered, this, [this] { changeAreaPhoto(1); });
    previousAreaPhotoAction_ = editMenu->addAction(tr("Go Back to the &Previous Photo"));
    previousAreaPhotoAction_->setShortcut(QKeySequence(Qt::Key_P));
    connect(previousAreaPhotoAction_, &QAction::triggered, this, [this] { changeAreaPhoto(-1); });
    describeFixAction_ = editMenu->addAction(tr("Describe a Fix for the AI (AI Not Built Yet)…"));
    describeFixAction_->setToolTip(
        tr("Saves your description with this area. No AI acts on it yet"));
    connect(describeFixAction_, &QAction::triggered, this, &MainWindow::askForFixDescription);
    deleteAreaAction_ = editMenu->addAction(tr("&Cancel or Delete This Area"));
    deleteAreaAction_->setShortcut(QKeySequence(Qt::Key_Backspace));
    connect(deleteAreaAction_, &QAction::triggered, this, [this] { cancelOrDeleteArea(); });
    connect(view_, &PhotoView::lassoFinished, this, [this](const QPolygonF& outline) {
        beginArea(outline);
        showAreaMenu(QCursor::pos());
    });
    connect(view_, &PhotoView::imageClicked, this, &MainWindow::selectAreaAt);
    connect(markAreaAction_, &QAction::toggled, this, [this](bool marking) {
        editMessage_ =
            marking ? tr("Marking is on: drag around each object. Press Esc to stop.") : QString{};
        updateStatus();
    });
    auto* stopMarking = makeChild<QShortcut>(this, QKeySequence(Qt::Key_Escape));
    connect(stopMarking, &QShortcut::activated, this, [this] {
        if (markAreaAction_->isChecked()) {
            markAreaAction_->setChecked(false);
        } else if (hasPendingArea()) {
            cancelOrDeleteArea();
        }
    });
    connect(view_, &PhotoView::imageRightClicked, this, &MainWindow::onAreaRightClicked);

    areaMenu_ = makeChild<QMenu>(this);
    areaMenu_->addAction(removeObjectAction_);
    areaMenu_->addAction(keepObjectAction_);
    areaMenu_->addSeparator();
    areaMenu_->addAction(nextAreaPhotoAction_);
    areaMenu_->addAction(previousAreaPhotoAction_);
    areaMenu_->addSeparator();
    areaMenu_->addAction(describeFixAction_);
    areaMenu_->addSeparator();
    areaMenu_->addAction(deleteAreaAction_);

    QToolBar* tools = addToolBar(tr("Edit"));
    tools->setMovable(false);
    tools->setToolButtonStyle(Qt::ToolButtonTextOnly);
    tools->addAction(markAreaAction_);
    tools->addSeparator();
    tools->addAction(removeObjectAction_);
    tools->addAction(keepObjectAction_);
    tools->addAction(nextAreaPhotoAction_);
    tools->addSeparator();
    tools->addAction(undoAction_);
    tools->addAction(redoAction_);

    showOutlinesAction_ = editMenu->addAction(tr("Show &All Outlines"));
    showOutlinesAction_->setCheckable(true);
    showOutlinesAction_->setToolTip(tr("Outline every edit, not just the selected one"));
    connect(showOutlinesAction_, &QAction::toggled, this, &MainWindow::setOutlinesShown);
    tools->addSeparator();
    tools->addAction(showOutlinesAction_);

    // The edits panel, on the right.
    auto* dock = makeChild<QDockWidget>(this, tr("Edits"));
    dock->setObjectName(QStringLiteral("edits"));
    dock->setFeatures(QDockWidget::DockWidgetMovable);
    editsList_ = makeChild<QListWidget>(dock);
    editsList_->setDragDropMode(QAbstractItemView::InternalMove);
    editsList_->setContextMenuPolicy(Qt::CustomContextMenu);
    editsList_->setToolTip(
        tr("Tick to show an edit, untick to switch it off. Drag to change "
           "which edit lies on top. Right-click for choices."));
    dock->setWidget(editsList_);
    constexpr int kPanelWidth = 320;
    editsList_->setMinimumWidth(kPanelWidth);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    connect(editsList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!refreshingList_) {
            selectArea(row);
        }
    });
    connect(editsList_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        if (!refreshingList_) {
            setAreaHidden(editsList_->row(item), item->checkState() != Qt::Checked);
        }
    });
    connect(editsList_->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex& /*parent*/, int start, int /*end*/,
                   const QModelIndex& /*destination*/, int row) {
                if (!refreshingList_) {
                    onEditsRowMoved(start, row > start ? row - 1 : row);
                }
            });
    connect(editsList_, &QListWidget::customContextMenuRequested, this, [this](QPoint point) {
        const int row = editsList_->indexAt(point).row();
        if (row >= 0) {
            selectArea(row);
            showAreaMenu(editsList_->viewport()->mapToGlobal(point));
        }
    });

    savePanoramaAction_ = fileMenu->addAction(tr("&Save Panorama"));
    savePanoramaAction_->setShortcut(QKeySequence::Save);
    connect(savePanoramaAction_, &QAction::triggered, this, [this] { savePanoramaFile(); });
    updateEditActions();

    auto* next = makeChild<QShortcut>(this, QKeySequence(Qt::Key_Right));
    connect(next, &QShortcut::activated, this, [this] { selectPhoto(currentRow_ + 1); });
    auto* previous = makeChild<QShortcut>(this, QKeySequence(Qt::Key_Left));
    connect(previous, &QShortcut::activated, this, [this] { selectPhoto(currentRow_ - 1); });

    view_->setMessage(tr("Open a folder of street photos to begin.\nFile > Open Folder"));
    updateStatus();
}

void MainWindow::createPreviewLoader() {
    // Replacing the loader waits for its workers and discards undelivered results, so a
    // photo from a previous folder can't appear in this one.
    previewLoader_.reset();
    previewLoader_ = std::make_unique<ImageLoader>(kPreviewThreads, 0);
    connect(previewLoader_.get(), &ImageLoader::loaded, this, &MainWindow::onPreviewLoaded);
    connect(previewLoader_.get(), &ImageLoader::failed, this, &MainWindow::onPreviewFailed);
}

void MainWindow::chooseFolder() {
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Open Folder of Photos"));
    if (!folder.isEmpty()) {
        openFolder(folder);
    }
}

bool MainWindow::openFolder(const QString& folder) {
    cancelStitch();
    panorama_ = QImage{};
    panoramaPath_.clear();
    stitchMessage_.clear();
    showPanoramaAction_->setEnabled(false);
    currentFolder_ = folder;

    const core::PhotoListing listing =
        core::listPhotos(std::filesystem::path(folder.toStdU16String()));

    createPreviewLoader();
    previewCache_.clear();
    currentRow_ = -1;

    QStringList paths;
    paths.reserve(static_cast<qsizetype>(listing.photos.size()));
    for (const std::filesystem::path& photo : listing.photos) {
        paths.append(toQString(photo));
    }
    model_->setPhotos(paths);

    if (!listing.ok()) {
        view_->setMessage(QString::fromStdString(listing.error));
        setWindowTitle(QStringLiteral("StreetPano"));
        updateStatus();
        return false;
    }
    if (paths.isEmpty()) {
        view_->setMessage(tr("This folder has no photos.\nSupported: JPEG, PNG and TIFF."));
        setWindowTitle(QStringLiteral("StreetPano"));
        updateStatus();
        return false;
    }
    setWindowTitle(QStringLiteral("%1 - StreetPano").arg(QDir(folder).dirName()));
    selectPhoto(0);
    return true;
}

QString MainWindow::findPosesFile() const {
    // The developer tools write poses.json in or next to the photo folder.
    const QDir folder(currentFolder_);
    const QStringList candidates = {
        QDir::cleanPath(folder.absolutePath()) + QStringLiteral(".streetpano/poses.json"),
        folder.filePath(QStringLiteral("poses.json")),
        folder.filePath(QStringLiteral("../poses.json"))};
    for (const QString& candidate : candidates) {
        if (QFile::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }
    return {};
}

bool MainWindow::startStitchFromFolder() {
    const QString posesPath = findPosesFile();
    return !posesPath.isEmpty() && startStitch(posesPath);
}

void MainWindow::chooseStitch() {
    QString posesPath = findPosesFile();
    if (posesPath.isEmpty()) {
        posesPath = QFileDialog::getOpenFileName(this, tr("Choose Camera Positions"),
                                                 currentFolder_, tr("Camera positions (*.json)"));
    }
    if (!posesPath.isEmpty()) {
        startStitch(posesPath);
    }
}

bool MainWindow::startStitch(const QString& posesPath) {
    if (stitchJob_ != nullptr || photoCount() == 0) {
        return false;
    }
    const QString output =
        QFileInfo(posesPath).absoluteDir().filePath(QStringLiteral("panorama.jpg"));
    stitchJob_ = std::make_unique<StitchJob>(posesPath, QDir(currentFolder_), output);
    connect(stitchJob_.get(), &StitchJob::progressChanged, this, &MainWindow::updateStatus);
    connect(stitchJob_.get(), &StitchJob::finished, this, &MainWindow::onStitchFinished);
    stitchMessage_ = tr("Stitching…");
    stitchJob_->start();
    updateStatus();
    return true;
}

void MainWindow::cancelStitch() {
    // Destroying the job stops and joins its worker, and drops its queued signals.
    stitchJob_.reset();
}

void MainWindow::onStitchFinished() {
    if (stitchJob_ == nullptr) {
        return;
    }
    stitch::StitchResult result = stitchJob_->takeResult();
    const QString output = stitchJob_->outputPath();
    stitch::Decisions loaded = stitchJob_->takeDecisions();
    decisionsPath_ = stitchJob_->decisionsPath();
    stitchJob_.reset();
    if (result.panorama.isNull()) {
        stitchMessage_ = tr("Stitching failed: %1").arg(result.error);
        updateStatus();
        return;
    }
    decisions_ = std::move(loaded);
    panorama_ = result.panorama;
    panoramaPath_ = output;
    stitched_ = std::move(result);
    undoStack_.clear();
    redoStack_.clear();
    selectedArea_ = -1;
    pendingArea_.clear();
    editMessage_.clear();
    showPanoramaAction_->setEnabled(true);
    stitchMessage_ = tr("Panorama: %1 x %2 px, %3% detail")
                         .arg(panorama_.width())
                         .arg(panorama_.height())
                         .arg(qRound(stitched_.scale * 100.0));
    if (!stitched_.warnings.isEmpty()) {
        stitchMessage_ +=
            tr(", %n photo(s) with problems", nullptr, static_cast<int>(stitched_.warnings.size()));
    }
    showPanorama();
    updateStatus();
}

void MainWindow::showPanorama() {
    if (!panorama_.isNull()) {
        showingPanorama_ = true;
        view_->fitToWindow();
        view_->setImage(panorama_);
        redrawPanorama();
        updateEditActions();
    }
}

namespace {

constexpr int kPhotoNotFound = -1;

[[nodiscard]] std::vector<core::WallPoint> toWall(const QPolygonF& outline,
                                                  const core::StitchPlan& plan) {
    std::vector<core::WallPoint> wall;
    wall.reserve(static_cast<std::size_t>(outline.size()));
    for (const QPointF& point : outline) {
        wall.push_back(core::WallPoint{plan.leftArc + point.x() * plan.unitsPerPixel,
                                       plan.topHeight - point.y() * plan.unitsPerPixel});
    }
    return wall;
}

[[nodiscard]] QPolygonF toPanorama(const std::vector<core::WallPoint>& outline,
                                   const core::StitchPlan& plan) {
    QPolygonF polygon;
    polygon.reserve(static_cast<qsizetype>(outline.size()));
    for (const core::WallPoint& point : outline) {
        polygon.append(QPointF((point.arc - plan.leftArc) / plan.unitsPerPixel,
                               (plan.topHeight - point.height) / plan.unitsPerPixel));
    }
    return polygon;
}

}  // namespace

std::vector<std::size_t> MainWindow::photoChoices(const core::Patch& patch) const {
    return core::patchChoices(stitched_.surface, stitched_.poses, patch.outline,
                              stitched_.focalPixels, stitched_.photoSize.width(),
                              stitched_.photoSize.height());
}

void MainWindow::redrawPanorama() {
    // A finished edit should look finished, so only the one being worked on is outlined.
    std::vector<QPolygonF> outlines;
    int highlighted = -1;
    const bool all = showOutlinesAction_->isChecked();
    for (std::size_t index = 0; index < decisions_.patches.size(); ++index) {
        const bool selected = static_cast<int>(index) == selectedArea_;
        if (all || selected) {
            if (selected) {
                highlighted = static_cast<int>(outlines.size());
            }
            outlines.push_back(toPanorama(decisions_.patches[index].outline, stitched_.plan));
        }
    }
    if (!pendingArea_.isEmpty()) {
        outlines.push_back(pendingArea_);
        highlighted = static_cast<int>(outlines.size()) - 1;
    }
    view_->setOutlines(std::move(outlines), highlighted);
    refreshEditsList();
}

QString MainWindow::describeArea(const core::Patch& patch) const {
    QString text;
    if (patch.kind == "remove") {
        text = tr("Remove");
    } else if (patch.kind == "keep") {
        text = tr("Keep whole");
    } else if (patch.photo.empty()) {
        text = tr("For the AI");
    } else {
        text = tr("Filled from a photo");
    }
    if (!patch.photo.empty()) {
        text += tr(" \u00b7 %1").arg(QFileInfo(QString::fromStdString(patch.photo)).baseName());
    }
    if (!patch.note.empty()) {
        text += tr(" \u00b7 \u201c%1\u201d").arg(QString::fromStdString(patch.note));
        if (patch.photo.empty()) {
            text += tr(" (waiting, no AI yet)");
        }
    }
    return text;
}

void MainWindow::refreshEditsList() {
    refreshingList_ = true;
    editsList_->clear();
    if (showingPanorama_) {
        for (std::size_t index = 0; index < decisions_.patches.size(); ++index) {
            const core::Patch& patch = decisions_.patches[index];
            auto* item = makeChild<QListWidgetItem>(
                editsList_, QStringLiteral("%1. %2").arg(index + 1).arg(describeArea(patch)));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
            item->setCheckState(patch.hidden ? Qt::Unchecked : Qt::Checked);
            item->setToolTip(item->text());
        }
        editsList_->setCurrentRow(selectedArea_);
    }
    refreshingList_ = false;
}

void MainWindow::setOutlinesShown(bool shown) {
    if (showOutlinesAction_->isChecked() != shown) {
        showOutlinesAction_->setChecked(shown);  // Calls this again through the signal.
        return;
    }
    redrawPanorama();
}

void MainWindow::selectArea(int index) {
    if (!showingPanorama_ || index < -1 || index >= areaCount()) {
        return;
    }
    selectedArea_ = index;
    pendingArea_.clear();
    editMessage_.clear();
    if (index >= 0) {
        view_->showArea(
            toPanorama(decisions_.patches[static_cast<std::size_t>(index)].outline, stitched_.plan)
                .boundingRect());
    }
    redrawPanorama();
    updateEditActions();
    updateStatus();
}

bool MainWindow::setAreaHidden(int index, bool hidden) {
    if (index < 0 || index >= areaCount() ||
        decisions_.patches[static_cast<std::size_t>(index)].hidden == hidden) {
        return false;
    }
    undoStack_.push_back(decisions_);
    redoStack_.clear();
    stitch::Decisions changed = decisions_;
    changed.patches[static_cast<std::size_t>(index)].hidden = hidden;
    applyDecisions(std::move(changed), index);
    return true;
}

bool MainWindow::moveArea(int from, int to) {
    if (from < 0 || to < 0 || from >= areaCount() || to >= areaCount() || from == to) {
        return false;
    }
    undoStack_.push_back(decisions_);
    redoStack_.clear();
    stitch::Decisions changed = decisions_;
    core::Patch moved = std::move(changed.patches[static_cast<std::size_t>(from)]);
    changed.patches.erase(changed.patches.begin() + from);
    changed.patches.insert(changed.patches.begin() + to, std::move(moved));
    applyDecisions(std::move(changed), to);
    return true;
}

void MainWindow::onEditsRowMoved(int from, int to) {
    if (!moveArea(from, to)) {
        refreshEditsList();
    }
}

void MainWindow::applyDecisions(stitch::Decisions decisions, int selected) {
    decisions_ = std::move(decisions);
    selectedArea_ = selected;
    editMessage_.clear();
    QStringList warnings;
    panorama_ = stitch::withPatches(stitched_, QDir(currentFolder_), decisions_.patches, warnings);
    if (!warnings.isEmpty()) {
        editMessage_ = warnings.front();
    }
    if (!decisionsPath_.isEmpty() && !stitch::writeDecisions(decisions_, decisionsPath_)) {
        editMessage_ = tr("Your edits couldn't be saved to %1.").arg(decisionsPath_);
    }
    if (showingPanorama_) {
        view_->setImage(panorama_);
    }
    redrawPanorama();
    updateEditActions();
    updateStatus();
}

void MainWindow::beginArea(const QPolygonF& outline) {
    if (!showingPanorama_ || panorama_.isNull() || outline.size() < 3) {
        return;
    }
    pendingArea_ = outline;
    selectedArea_ = -1;
    editMessage_ = tr("Choose what to do with this area.");
    redrawPanorama();
    updateEditActions();
    updateStatus();
}

std::vector<core::WallPoint> MainWindow::targetOutline() const {
    if (!pendingArea_.isEmpty()) {
        return toWall(pendingArea_, stitched_.plan);
    }
    if (selectedArea_ >= 0) {
        return decisions_.patches[static_cast<std::size_t>(selectedArea_)].outline;
    }
    return {};
}

bool MainWindow::commitArea(std::string photo, const char* kind, const QString& message) {
    const std::vector<core::WallPoint> outline = targetOutline();
    undoStack_.push_back(decisions_);
    redoStack_.clear();
    stitch::Decisions changed = decisions_;
    int selected = selectedArea_;
    if (!pendingArea_.isEmpty()) {
        changed.patches.push_back(core::Patch{outline, std::move(photo), {}, kind, false});
        selected = static_cast<int>(changed.patches.size()) - 1;
        pendingArea_.clear();
    } else {
        core::Patch& patch = changed.patches[static_cast<std::size_t>(selected)];
        patch.photo = std::move(photo);
        patch.kind = kind;
        patch.hidden = false;
    }
    applyDecisions(std::move(changed), selected);
    if (editMessage_.isEmpty()) {
        editMessage_ = message;
    }
    updateStatus();
    return true;
}

bool MainWindow::removeObject() {
    const std::vector<core::WallPoint> outline = targetOutline();
    if (outline.empty()) {
        return false;
    }
    std::optional<std::string> photo =
        stitch::cleanestPhoto(stitched_, QDir(currentFolder_), outline);
    if (!photo.has_value()) {
        editMessage_ = tr("Too few photos see this area to remove what's in it.");
        updateStatus();
        return false;
    }
    return commitArea(std::move(*photo), "remove",
                      tr("Removed using the cleanest photo. Not right? Try a different photo."));
}

bool MainWindow::keepObject() {
    const std::vector<core::WallPoint> outline = targetOutline();
    if (outline.empty()) {
        return false;
    }
    core::Patch patch;
    patch.outline = outline;
    const std::vector<std::size_t> choices = photoChoices(patch);
    if (choices.empty()) {
        editMessage_ = tr("No photo can see that area.");
        updateStatus();
        return false;
    }
    return commitArea(stitched_.poses[choices.front()].name, "keep",
                      tr("Kept whole from one photo. Not right? Try a different photo."));
}

bool MainWindow::describeFix(const QString& description) {
    const std::vector<core::WallPoint> outline = targetOutline();
    if (outline.empty() || description.trimmed().isEmpty()) {
        return false;
    }
    undoStack_.push_back(decisions_);
    redoStack_.clear();
    stitch::Decisions changed = decisions_;
    int selected = selectedArea_;
    if (!pendingArea_.isEmpty()) {
        changed.patches.push_back(
            core::Patch{outline, {}, description.trimmed().toStdString(), "describe", false});
        selected = static_cast<int>(changed.patches.size()) - 1;
        pendingArea_.clear();
    } else {
        changed.patches[static_cast<std::size_t>(selected)].note =
            description.trimmed().toStdString();
    }
    applyDecisions(std::move(changed), selected);
    editMessage_ = tr("Your description is saved. No AI acts on it yet.");
    updateStatus();
    return true;
}

void MainWindow::askForFixDescription() {
    bool accepted = false;
    const QString description = QInputDialog::getMultiLineText(
        this, tr("Describe a Fix for the AI"),
        tr("What should happen here? For example: \"remove the white van\".\n"
           "This is saved with the area. The AI isn't built yet, so nothing changes for now."),
        selectedAreaNote(), &accepted);
    if (accepted) {
        describeFix(description);
    }
}

bool MainWindow::cancelOrDeleteArea() {
    if (!pendingArea_.isEmpty()) {
        pendingArea_.clear();
        editMessage_.clear();
        redrawPanorama();
        updateEditActions();
        updateStatus();
        return true;
    }
    return deleteSelectedArea();
}

void MainWindow::showAreaMenu(QPoint screenPoint) {
    if (hasPendingArea() || selectedArea_ >= 0) {
        areaMenu_->popup(screenPoint);
    }
}

void MainWindow::onAreaRightClicked(QPointF point, QPoint screenPoint) {
    if (!showingPanorama_) {
        return;
    }
    if (pendingArea_.isEmpty() || !pendingArea_.containsPoint(point, Qt::OddEvenFill)) {
        pendingArea_.clear();
        selectAreaAt(point);
    }
    if (hasPendingArea() || selectedArea_ >= 0) {
        showAreaMenu(screenPoint);
    } else {
        editMessage_ = tr("Choose Mark an Area, then drag an outline around an object.");
        updateStatus();
    }
}

bool MainWindow::markArea(const QPolygonF& outline) {
    if (!showingPanorama_ || panorama_.isNull() || outline.size() < 3) {
        return false;
    }
    core::Patch patch;
    patch.outline = toWall(outline, stitched_.plan);
    const std::vector<std::size_t> choices = photoChoices(patch);
    if (choices.empty()) {
        editMessage_ = tr("No photo can see that area.");
        updateStatus();
        return false;
    }
    patch.photo = stitched_.poses[choices.front()].name;
    patch.kind = "keep";
    undoStack_.push_back(decisions_);
    redoStack_.clear();
    stitch::Decisions changed = decisions_;
    changed.patches.push_back(std::move(patch));
    const int selected = static_cast<int>(changed.patches.size()) - 1;
    applyDecisions(std::move(changed), selected);
    return true;
}

bool MainWindow::changeAreaPhoto(int step) {
    if (selectedArea_ < 0) {
        return false;
    }
    const core::Patch& patch = decisions_.patches[static_cast<std::size_t>(selectedArea_)];
    const std::vector<std::size_t> choices = photoChoices(patch);
    if (choices.size() < 2) {
        return false;
    }
    int current = kPhotoNotFound;
    for (std::size_t index = 0; index < choices.size(); ++index) {
        if (stitched_.poses[choices[index]].name == patch.photo) {
            current = static_cast<int>(index);
        }
    }
    const int count = static_cast<int>(choices.size());
    const int target = (std::max(current, 0) + step % count + count) % count;
    undoStack_.push_back(decisions_);
    redoStack_.clear();
    stitch::Decisions changed = decisions_;
    changed.patches[static_cast<std::size_t>(selectedArea_)].photo =
        stitched_.poses[choices[static_cast<std::size_t>(target)]].name;
    applyDecisions(std::move(changed), selectedArea_);
    return true;
}

bool MainWindow::deleteSelectedArea() {
    if (selectedArea_ < 0) {
        return false;
    }
    undoStack_.push_back(decisions_);
    redoStack_.clear();
    stitch::Decisions changed = decisions_;
    changed.patches.erase(changed.patches.begin() + selectedArea_);
    applyDecisions(std::move(changed), -1);
    return true;
}

bool MainWindow::undo() {
    if (undoStack_.empty()) {
        return false;
    }
    redoStack_.push_back(decisions_);
    stitch::Decisions previous = std::move(undoStack_.back());
    undoStack_.pop_back();
    applyDecisions(std::move(previous), -1);
    return true;
}

bool MainWindow::redo() {
    if (redoStack_.empty()) {
        return false;
    }
    undoStack_.push_back(decisions_);
    stitch::Decisions next = std::move(redoStack_.back());
    redoStack_.pop_back();
    applyDecisions(std::move(next), -1);
    return true;
}

void MainWindow::selectAreaAt(QPointF point) {
    if (!showingPanorama_) {
        return;
    }
    int found = -1;
    // The newest area is drawn on top, so it's found first.
    for (int index = areaCount() - 1; index >= 0 && found < 0; --index) {
        const core::Patch& patch = decisions_.patches[static_cast<std::size_t>(index)];
        if (toPanorama(patch.outline, stitched_.plan).containsPoint(point, Qt::OddEvenFill)) {
            found = index;
        }
    }
    selectedArea_ = found;
    pendingArea_.clear();
    editMessage_.clear();
    redrawPanorama();
    updateEditActions();
    updateStatus();
}

bool MainWindow::savePanoramaFile() {
    if (panorama_.isNull() || panoramaPath_.isEmpty()) {
        return false;
    }
    const bool saved = stitch::savePanorama(panorama_, panoramaPath_);
    editMessage_ = saved ? tr("Saved %1").arg(panoramaPath_)
                         : tr("The panorama couldn't be saved to %1.").arg(panoramaPath_);
    updateStatus();
    return saved;
}

QString MainWindow::selectedAreaNote() const {
    if (selectedArea_ < 0) {
        return {};
    }
    return QString::fromStdString(decisions_.patches[static_cast<std::size_t>(selectedArea_)].note);
}

QString MainWindow::selectedAreaPhoto() const {
    if (selectedArea_ < 0) {
        return {};
    }
    return QString::fromStdString(
        decisions_.patches[static_cast<std::size_t>(selectedArea_)].photo);
}

void MainWindow::updateEditActions() {
    const bool canEdit = showingPanorama_ && !panorama_.isNull();
    markAreaAction_->setEnabled(canEdit);
    if (!canEdit && markAreaAction_->isChecked()) {
        markAreaAction_->setChecked(false);
    }
    const bool selected = canEdit && selectedArea_ >= 0;
    const bool target = canEdit && (selected || hasPendingArea());
    removeObjectAction_->setEnabled(target);
    keepObjectAction_->setEnabled(target);
    describeFixAction_->setEnabled(target);
    nextAreaPhotoAction_->setEnabled(selected);
    previousAreaPhotoAction_->setEnabled(selected);
    deleteAreaAction_->setEnabled(target);
    undoAction_->setEnabled(canEdit && !undoStack_.empty());
    redoAction_->setEnabled(canEdit && !redoStack_.empty());
    savePanoramaAction_->setEnabled(!panorama_.isNull());
}

void MainWindow::selectPhoto(int row) {
    if (row < 0 || row >= model_->rowCount()) {
        return;
    }
    strip_->setCurrentIndex(model_->index(row));
    strip_->scrollTo(model_->index(row), QAbstractItemView::PositionAtCenter);
}

void MainWindow::showPhoto(int row) {
    if (row < 0 || row >= model_->rowCount()) {
        return;
    }
    currentRow_ = row;
    showingPanorama_ = false;
    pendingArea_.clear();
    selectedArea_ = -1;
    view_->setOutlines({}, -1);
    refreshEditsList();
    updateEditActions();
    previewLoader_->cancelPending();
    const std::optional<QImage> cached = previewCache_.get(row);
    if (cached.has_value()) {
        view_->setImage(*cached);
    } else {
        requestPreview(row);
    }
    // Loading the neighbours first makes moving along the street feel immediate.
    requestPreview(row + 1);
    requestPreview(row - 1);
    updateStatus();
}

void MainWindow::requestPreview(int row) {
    if (row >= 0 && row < model_->rowCount() && !previewCache_.contains(row)) {
        previewLoader_->request(row, model_->pathAt(row));
    }
}

void MainWindow::onPreviewLoaded(int row, const QImage& image) {
    previewCache_.put(row, image, static_cast<std::size_t>(image.sizeInBytes()));
    if (row == currentRow_) {
        view_->setImage(image);
    }
    updateStatus();
}

void MainWindow::onPreviewFailed(int row, const QString& message) {
    if (row == currentRow_) {
        view_->setMessage(tr("Can't read this photo:\n%1").arg(message));
    }
}

void MainWindow::updateStatus() {
    if (currentRow_ >= 0) {
        positionLabel_->setText(
            tr("Photo %1 of %2: %3")
                .arg(currentRow_ + 1)
                .arg(model_->rowCount())
                .arg(model_->data(model_->index(currentRow_), Qt::DisplayRole).toString()));
    } else {
        positionLabel_->setText(tr("No photos open"));
    }
    // Previews have a budget. The panorama is shown on its own, because its size depends on the
    // street, and the automatic result is kept apart from it once there are edits.
    const double previews =
        static_cast<double>(previewCache_.usedBytes() + model_->cacheUsedBytes()) /
        kBytesPerMegabyte;
    const double budget =
        static_cast<double>(previewCache_.budgetBytes() + model_->cacheBudgetBytes()) /
        kBytesPerMegabyte;
    double panoramaBytes = static_cast<double>(panorama_.sizeInBytes());
    if (!stitched_.base.isNull() && stitched_.base.cacheKey() != panorama_.cacheKey()) {
        panoramaBytes += static_cast<double>(stitched_.base.sizeInBytes());
    }
    const double panorama = panoramaBytes / kBytesPerMegabyte;
    if (stitchJob_ != nullptr && stitchJob_->total() > 0) {
        stitchMessage_ =
            tr("Stitching photo %1 of %2…").arg(stitchJob_->done()).arg(stitchJob_->total());
    }
    if (selectedArea_ >= 0) {
        const QString photo = selectedAreaPhoto();
        positionLabel_->setText(
            photo.isEmpty()
                ? tr("Area %1 of %2, waiting for the AI").arg(selectedArea_ + 1).arg(areaCount())
                : tr("Area %1 of %2, from %3").arg(selectedArea_ + 1).arg(areaCount()).arg(photo));
    }
    if (!editMessage_.isEmpty()) {
        positionLabel_->setText(positionLabel_->text() + QStringLiteral("   |   ") + editMessage_);
    }
    if (!stitchMessage_.isEmpty()) {
        positionLabel_->setText(positionLabel_->text() + QStringLiteral("   |   ") +
                                stitchMessage_);
    }
    memoryLabel_->setText(
        panorama > 0.0
            ? tr("Previews: %1 of %2 MB, panorama: %3 MB")
                  .arg(previews, 0, 'f', 0)
                  .arg(budget, 0, 'f', 0)
                  .arg(panorama, 0, 'f', 0)
            : tr("Previews: %1 of %2 MB").arg(previews, 0, 'f', 0).arg(budget, 0, 'f', 0));
}

}  // namespace streetpano::ui
