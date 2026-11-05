#include "ui/main_window.hpp"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QMenuBar>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <algorithm>
#include <cstddef>
#include <filesystem>
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
    markAreaAction_ = editMenu->addAction(tr("&Mark Area"));
    markAreaAction_->setCheckable(true);
    markAreaAction_->setShortcut(QKeySequence(Qt::Key_L));
    connect(markAreaAction_, &QAction::toggled, view_, &PhotoView::setLassoMode);
    nextAreaPhotoAction_ = editMenu->addAction(tr("&Next Photo for Area"));
    nextAreaPhotoAction_->setShortcut(QKeySequence(Qt::Key_BracketRight));
    connect(nextAreaPhotoAction_, &QAction::triggered, this, [this] { changeAreaPhoto(1); });
    previousAreaPhotoAction_ = editMenu->addAction(tr("&Previous Photo for Area"));
    previousAreaPhotoAction_->setShortcut(QKeySequence(Qt::Key_BracketLeft));
    connect(previousAreaPhotoAction_, &QAction::triggered, this, [this] { changeAreaPhoto(-1); });
    deleteAreaAction_ = editMenu->addAction(tr("&Delete Area"));
    deleteAreaAction_->setShortcut(QKeySequence(Qt::Key_Backspace));
    connect(deleteAreaAction_, &QAction::triggered, this, [this] { deleteSelectedArea(); });
    connect(view_, &PhotoView::lassoFinished, this, &MainWindow::markArea);
    connect(view_, &PhotoView::imageClicked, this, &MainWindow::selectAreaAt);

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
    editMessage_.clear();
    showPanoramaAction_->setEnabled(true);
    stitchMessage_ = tr("Panorama: %1 x %2 px, %3% detail")
                         .arg(panorama_.width())
                         .arg(panorama_.height())
                         .arg(qRound(result.scale * 100.0));
    if (!result.warnings.isEmpty()) {
        stitchMessage_ +=
            tr(", %n photo(s) with problems", nullptr, static_cast<int>(result.warnings.size()));
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
    std::vector<QPolygonF> outlines;
    outlines.reserve(decisions_.patches.size());
    for (const core::Patch& patch : decisions_.patches) {
        outlines.push_back(toPanorama(patch.outline, stitched_.plan));
    }
    view_->setOutlines(std::move(outlines), selectedArea_);
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
    nextAreaPhotoAction_->setEnabled(selected);
    previousAreaPhotoAction_->setEnabled(selected);
    deleteAreaAction_->setEnabled(selected);
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
    view_->setOutlines({}, -1);
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
    const double used = (static_cast<double>(previewCache_.usedBytes() + model_->cacheUsedBytes()) +
                         static_cast<double>(panorama_.sizeInBytes())) /
                        kBytesPerMegabyte;
    const double budget =
        static_cast<double>(previewCache_.budgetBytes() + model_->cacheBudgetBytes()) /
        kBytesPerMegabyte;
    if (stitchJob_ != nullptr && stitchJob_->total() > 0) {
        stitchMessage_ =
            tr("Stitching photo %1 of %2…").arg(stitchJob_->done()).arg(stitchJob_->total());
    }
    if (selectedArea_ >= 0) {
        positionLabel_->setText(tr("Area %1 of %2, from %3   |   [ and ] change the photo")
                                    .arg(selectedArea_ + 1)
                                    .arg(areaCount())
                                    .arg(selectedAreaPhoto()));
    }
    if (!editMessage_.isEmpty()) {
        positionLabel_->setText(positionLabel_->text() + QStringLiteral("   |   ") + editMessage_);
    }
    if (!stitchMessage_.isEmpty()) {
        positionLabel_->setText(positionLabel_->text() + QStringLiteral("   |   ") +
                                stitchMessage_);
    }
    memoryLabel_->setText(
        tr("Image memory: %1 of %2 MB").arg(used, 0, 'f', 0).arg(budget, 0, 'f', 0));
}

}  // namespace streetpano::ui
