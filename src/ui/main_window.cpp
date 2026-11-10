#include "ui/main_window.hpp"

#include <QAbstractItemModel>
#include <QAction>
#include <QActionGroup>
#include <QCursor>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QMenuBar>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/photo_listing.hpp"
#include "ui/film_strip_delegate.hpp"
#include "ui/qt_ownership.hpp"
#include "ui/recent_projects.hpp"

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
    workPage_ = splitter;

    pages_ = makeChild<QStackedWidget>(this);
    projectsPage_ = createProjectsPage();
    alignView_ = makeChild<PhotoView>(this);
    alignPage_ = alignView_;
    exportPage_ = createExportPage();
    pages_->addWidget(projectsPage_);
    pages_->addWidget(workPage_);
    pages_->addWidget(alignPage_);
    pages_->addWidget(exportPage_);
    setCentralWidget(pages_);

    // The steps, along the top. Each one can be revisited at any time.
    QToolBar* steps = addToolBar(tr("Steps"));
    steps->setObjectName(QStringLiteral("steps"));
    steps->setMovable(false);
    steps->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* stepGroup = makeChild<QActionGroup>(this);
    const auto addStep = [this, steps, stepGroup](const QString& name, const QString& tip,
                                                  Step step) {
        QAction* action = steps->addAction(name);
        action->setCheckable(true);
        action->setToolTip(tip);
        stepGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, step] { setStep(step); });
        stepActions_.push_back(action);
    };
    addStep(tr("Projects"), tr("Create, open and switch between streets"), Step::Projects);
    steps->addSeparator();
    addStep(tr("\u2460 Photos"), tr("Check the photos, and stitch them"), Step::Photos);
    addStep(tr("\u2461 Align"), tr("Line the photos up with guide lines, then stitch them"),
            Step::Align);
    addStep(tr("\u2462 Edit"), tr("Remove, keep and describe objects on the panorama"), Step::Edit);
    addStep(tr("\u2463 Export"), tr("Save the finished panorama"), Step::Export);
    addToolBarBreak();

    statusBar()->addWidget(positionLabel_);
    statusBar()->addPermanentWidget(memoryLabel_);

    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    QAction* newProjectAction = fileMenu->addAction(tr("&New Project…"));
    newProjectAction->setShortcut(QKeySequence::New);
    connect(newProjectAction, &QAction::triggered, this, &MainWindow::chooseNewProject);
    QAction* openProjectAction = fileMenu->addAction(tr("Open &Project…"));
    connect(openProjectAction, &QAction::triggered, this, &MainWindow::chooseProject);
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
    // Stitching ends the Align step, once the photos are lined up.
    stitchAction->setIconText(tr("Stitch Panorama"));
    alignTools_ = addToolBar(tr("Align"));
    alignTools_->setObjectName(QStringLiteral("align"));
    alignTools_->setMovable(false);
    alignTools_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    QAction* addGuideAction = alignTools_->addAction(tr("Add Horizontal Guide"));
    addGuideAction->setToolTip(
        tr("A line across all photos. Drag it to a roofline or window line. Right-click a guide "
           "to remove it"));
    connect(addGuideAction, &QAction::triggered, this, [this] { addGuide(); });
    showGridAction_ = alignTools_->addAction(tr("Show Grid"));
    showGridAction_->setCheckable(true);
    showGridAction_->setChecked(true);
    connect(showGridAction_, &QAction::toggled, this, [this](bool shown) {
        alignView_->setGrid(
            shown && !alignmentView_.strip.isNull() ? alignmentView_.strip.height() / 12.0 : 0.0);
    });
    showOriginalAction_ = alignTools_->addAction(tr("Original Photos"));
    showOriginalAction_->setCheckable(true);
    showOriginalAction_->setToolTip(tr("Show the photos as they were taken, to compare"));
    connect(showOriginalAction_, &QAction::toggled, this, &MainWindow::setOriginalShown);
    alignTools_->addSeparator();

    // Controls for the selected photo. Each is one small step, so a few clicks fine-tune it.
    constexpr double kTurnDegrees = 0.2;
    constexpr double kMoveRows = 2.0;
    constexpr double kSizeStep = 1.005;
    const auto addControl = [this](const QString& name, const QString& tip, auto&& apply) {
        QAction* action = alignTools_->addAction(name);
        action->setToolTip(tip);
        connect(action, &QAction::triggered, this, apply);
        adjustActions_.push_back(action);
    };
    addControl(tr("Turn Left"), tr("Turn the selected photo anticlockwise"),
               [this] { rotateSelected(kTurnDegrees); });
    addControl(tr("Turn Right"), tr("Turn the selected photo clockwise"),
               [this] { rotateSelected(-kTurnDegrees); });
    addControl(tr("Tilt Back"), tr("Lean the selected photo's vertical lines back"),
               [this] { tiltSelected(kTurnDegrees); });
    addControl(tr("Tilt Forward"), tr("Lean the selected photo's vertical lines forward"),
               [this] { tiltSelected(-kTurnDegrees); });
    addControl(tr("Up"), tr("Move the selected photo up (or press the up arrow)"),
               [this] { moveSelected(kMoveRows); });
    addControl(tr("Down"), tr("Move the selected photo down (or press the down arrow)"),
               [this] { moveSelected(-kMoveRows); });
    addControl(tr("Smaller"), tr("Make the selected photo smaller"),
               [this] { scaleSelected(1.0 / kSizeStep); });
    addControl(tr("Larger"), tr("Make the selected photo larger"),
               [this] { scaleSelected(kSizeStep); });
    addControl(tr("Reset Photo"), tr("Undo all corrections to the selected photo"),
               [this] { resetSelected(); });
    alignTools_->addSeparator();
    alignTools_->addAction(stitchAction);

    // Clicking a photo selects it, and the arrow keys work while the view has the focus.
    alignView_->setFocusPolicy(Qt::StrongFocus);
    connect(alignView_, &PhotoView::imageClicked, this,
            [this](QPointF point) { selectTile(stitch::tileAt(alignmentView_, point.x())); });
    const auto addKey = [this](Qt::Key key, auto&& apply) {
        auto* shortcut = makeChild<QShortcut>(alignView_, QKeySequence(key));
        shortcut->setContext(Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, this, apply);
    };
    addKey(Qt::Key_Right, [this] { selectTile(selectedTile_ + 1); });
    addKey(Qt::Key_Left, [this] { selectTile(std::max(0, selectedTile_ - 1)); });
    addKey(Qt::Key_Up, [this] { moveSelected(kMoveRows); });
    addKey(Qt::Key_Down, [this] { moveSelected(-kMoveRows); });
    connect(alignView_, &PhotoView::guideMoved, this, &MainWindow::moveGuide);
    connect(alignView_, &PhotoView::imageRightClicked, this,
            [this](QPointF point, QPoint) { removeGuide(alignView_->guideAt(point)); });
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
    connect(deleteAreaAction_, &QAction::triggered, this, [this] { cancelOrDeleteArea(); });
    connect(view_, &PhotoView::lassoFinished, this, [this](const QPolygonF& outline) {
        beginArea(outline);
        // Opening a menu while the mouse button is still being released can close it again
        // straight away on macOS, so it opens once that has finished.
        const QPoint where = QCursor::pos();
        QTimer::singleShot(0, this, [this, where] { showAreaMenu(where); });
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
    tools->setObjectName(QStringLiteral("edit"));
    editTools_ = tools;
    tools->setMovable(false);
    tools->setToolButtonStyle(Qt::ToolButtonTextOnly);
    tools->addAction(markAreaAction_);
    tools->addSeparator();
    tools->addAction(removeObjectAction_);
    tools->addAction(keepObjectAction_);
    tools->addAction(nextAreaPhotoAction_);
    describeFixAction_->setIconText(tr("Describe for the AI…"));
    tools->addAction(describeFixAction_);
    tools->addSeparator();
    tools->addAction(undoAction_);
    tools->addAction(redoAction_);

    // What a drag draws: a freehand outline, a rectangle, or a line.
    auto* shapes = makeChild<QActionGroup>(this);
    QMenu* shapeMenu = editMenu->addMenu(tr("Mark &Shape"));
    const auto addShape = [this, shapes, shapeMenu](const QString& name, const QString& tip,
                                                    PhotoView::MarkShape shape) {
        QAction* action = shapeMenu->addAction(name);
        action->setCheckable(true);
        action->setToolTip(tip);
        shapes->addAction(action);
        connect(action, &QAction::triggered, this, [this, shape] { setMarkShape(shape); });
        return action;
    };
    freehandAction_ =
        addShape(tr("Freehand"), tr("Draw around an object"), PhotoView::MarkShape::Freehand);
    rectangleAction_ = addShape(tr("Rectangle"), tr("Drag from one corner to the opposite one"),
                                PhotoView::MarkShape::Rectangle);
    lineAction_ =
        addShape(tr("Line"), tr("Drag along something that should be straight, then describe it"),
                 PhotoView::MarkShape::Line);
    freehandAction_->setChecked(true);
    tools->insertAction(tools->actions().value(1), freehandAction_);
    tools->insertAction(tools->actions().value(2), rectangleAction_);
    tools->insertAction(tools->actions().value(3), lineAction_);

    showOutlinesAction_ = editMenu->addAction(tr("Show &All Outlines"));
    showOutlinesAction_->setCheckable(true);
    showOutlinesAction_->setToolTip(tr("Outline every edit, not just the selected one"));
    connect(showOutlinesAction_, &QAction::toggled, this, &MainWindow::setOutlinesShown);
    tools->addSeparator();
    tools->addAction(showOutlinesAction_);

    // The edits panel, on the right.
    auto* dock = makeChild<QDockWidget>(this, tr("Edits"));
    editsDock_ = dock;
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
        // The last row can be the new outline, which isn't an edit yet.
        if (!refreshingList_ && row < areaCount()) {
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
        if (row >= 0 && row < areaCount()) {
            selectArea(row);
        }
        if (row >= 0) {
            showAreaMenu(editsList_->viewport()->mapToGlobal(point));
        }
    });

    savePanoramaAction_ = fileMenu->addAction(tr("&Save Panorama"));
    savePanoramaAction_->setShortcut(QKeySequence::Save);
    connect(savePanoramaAction_, &QAction::triggered, this, [this] { savePanoramaFile(); });
    updateEditActions();

    // In Photos, the arrow keys move along the film strip. Align has its own.
    nextShortcut_ = makeChild<QShortcut>(this, QKeySequence(Qt::Key_Right));
    connect(nextShortcut_, &QShortcut::activated, this, [this] { selectPhoto(currentRow_ + 1); });
    previousShortcut_ = makeChild<QShortcut>(this, QKeySequence(Qt::Key_Left));
    connect(previousShortcut_, &QShortcut::activated, this,
            [this] { selectPhoto(currentRow_ - 1); });

    view_->setMessage(tr("Open a folder of street photos to begin.\nFile > Open Folder"));
    refreshRecentProjects();
    applyStep(Step::Projects);
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
    // A new street needs its own side-by-side view.
    alignJob_.reset();
    alignmentView_ = stitch::AlignmentView{};
    alignStreet_ = stitch::PreparedStreet{};
    alignment_ = stitch::Alignment{};
    selectedTile_ = -1;
    alignmentPath_.clear();
    alignView_->setGuides({});
    alignView_->setGrid(0.0);
    alignView_->setMessage(tr("Preparing the photos…"));
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

void MainWindow::showPanorama() { setStep(Step::Edit); }

void MainWindow::displayPanorama() {
    if (!panorama_.isNull()) {
        showingPanorama_ = true;
        applyStep(Step::Edit);
        view_->fitToWindow();
        view_->setImage(panorama_);
        redrawPanorama();
        updateEditActions();
    }
}

QAction* MainWindow::stepAction(Step step) const {
    return stepActions_[static_cast<std::size_t>(step)];
}

bool MainWindow::isStepAvailable(Step step) const {
    switch (step) {
        case Step::Projects:
            return true;
        case Step::Photos:
        case Step::Align:
            return photoCount() > 0;
        case Step::Edit:
        case Step::Export:
            return !panorama_.isNull();
    }
    return false;
}

void MainWindow::updateStepActions() {
    for (std::size_t index = 0; index < stepActions_.size(); ++index) {
        stepActions_[index]->setEnabled(isStepAvailable(static_cast<Step>(index)));
        stepActions_[index]->setChecked(static_cast<Step>(index) == step_);
    }
}

void MainWindow::applyStep(Step step) {
    step_ = step;
    switch (step) {
        case Step::Projects:
            pages_->setCurrentWidget(projectsPage_);
            break;
        case Step::Photos:
        case Step::Edit:
            pages_->setCurrentWidget(workPage_);
            break;
        case Step::Align:
            pages_->setCurrentWidget(alignPage_);
            if (alignmentView_.strip.isNull() && alignJob_ == nullptr) {
                startAlignment();
            }
            break;
        case Step::Export:
            pages_->setCurrentWidget(exportPage_);
            break;
    }
    // Each step shows only its own tools.
    strip_->setVisible(step == Step::Photos);
    alignTools_->setVisible(step == Step::Align);
    updateAlignActions();
    if (nextShortcut_ != nullptr) {
        nextShortcut_->setEnabled(step == Step::Photos);
        previousShortcut_->setEnabled(step == Step::Photos);
    }
    editTools_->setVisible(step == Step::Edit);
    editsDock_->setVisible(step == Step::Edit);
    if (step != Step::Edit && markAreaAction_->isChecked()) {
        markAreaAction_->setChecked(false);
    }
    if (step == Step::Export) {
        exportInfo_->setText(tr("Panorama: %1 x %2 px, with %n edit(s).\nIt will be saved to %3",
                                nullptr, areaCount())
                                 .arg(panorama_.width())
                                 .arg(panorama_.height())
                                 .arg(panoramaPath_));
    }
    updateStepActions();
}

void MainWindow::setStep(Step step) {
    if (!isStepAvailable(step)) {
        updateStepActions();
        return;
    }
    if (step == Step::Edit) {
        displayPanorama();
    } else if (step == Step::Photos) {
        showPhoto(currentRow_ >= 0 ? currentRow_ : 0);
    } else {
        applyStep(step);
    }
}

void MainWindow::startAlignment() {
    const QString posesPath = findPosesFile();
    if (posesPath.isEmpty()) {
        alignView_->setMessage(
            tr("These photos don't have camera positions yet.\n\nIn the Terminal, run:\n"
               "cd ~/projects/StreetPano\n./scripts/stitch.sh \"%1\"")
                .arg(currentFolder_));
        return;
    }
    constexpr int kStripHeight = 900;
    alignmentPath_ = stitch::alignmentPathFor(posesPath);
    alignJob_ = std::make_unique<AlignJob>(posesPath, QDir(currentFolder_), kStripHeight);
    connect(alignJob_.get(), &AlignJob::progressChanged, this, &MainWindow::updateStatus);
    connect(alignJob_.get(), &AlignJob::finished, this, &MainWindow::onAlignmentFinished);
    alignView_->setMessage(tr("Preparing the photos…"));
    alignJob_->start();
    updateStatus();
}

void MainWindow::onAlignmentFinished() {
    if (alignJob_ == nullptr) {
        return;
    }
    alignmentView_ = alignJob_->takeView();
    alignStreet_ = alignJob_->takeStreet();
    alignment_ = alignJob_->takeAlignment();
    alignJob_.reset();
    selectedTile_ = -1;
    if (alignmentView_.strip.isNull()) {
        alignView_->setMessage(tr("The photos couldn't be prepared: %1").arg(alignmentView_.error));
        updateStatus();
        return;
    }
    alignView_->setImage(originalShown_ ? alignmentView_.original : alignmentView_.strip);
    alignView_->fitHeight();
    alignView_->setGrid(showGridAction_->isChecked() ? alignmentView_.strip.height() / 12.0 : 0.0);
    showGuides();
    showSelectedTile();
    updateAlignActions();
    updateStatus();
}

void MainWindow::selectTile(int tile) {
    if (alignmentView_.strip.isNull()) {
        return;
    }
    selectedTile_ = tile >= 0 && tile < static_cast<int>(alignmentView_.tiles.size()) ? tile : -1;
    showSelectedTile();
    updateAlignActions();
    updateStatus();
}

void MainWindow::showSelectedTile() {
    if (selectedTile_ < 0) {
        alignView_->setOutlines({}, -1);
        return;
    }
    const stitch::AlignmentTile& tile =
        alignmentView_.tiles[static_cast<std::size_t>(selectedTile_)];
    const QRectF box(tile.left, 0, tile.width, alignmentView_.strip.height());
    alignView_->setOutlines({QPolygonF(box)}, 0);
}

void MainWindow::updateAlignActions() {
    for (QAction* action : adjustActions_) {
        action->setEnabled(selectedTile_ >= 0);
    }
}

core::PhotoAdjustment MainWindow::selectedAdjustment() const {
    if (selectedTile_ < 0) {
        return {};
    }
    return alignment_.adjustmentFor(
        alignmentView_.tiles[static_cast<std::size_t>(selectedTile_)].photo);
}

bool MainWindow::adjustSelected(const core::PhotoAdjustment& change) {
    if (selectedTile_ < 0) {
        return false;
    }
    const std::string& photo = alignmentView_.tiles[static_cast<std::size_t>(selectedTile_)].photo;
    core::PhotoAdjustment& adjustment = alignment_.photos[photo];
    adjustment.rollDegrees += change.rollDegrees;
    adjustment.pitchDegrees += change.pitchDegrees;
    adjustment.shift += change.shift;
    adjustment.scale *= change.scale;
    if (adjustment.isNeutral()) {
        alignment_.photos.erase(photo);
    }
    saveGuides();
    stitch::redrawTile(alignmentView_, alignStreet_, QDir(currentFolder_),
                       static_cast<std::size_t>(selectedTile_), alignment_.adjustmentFor(photo));
    // Corrections are only visible in the corrected view.
    if (originalShown_) {
        showOriginalAction_->setChecked(false);
    } else {
        alignView_->setImage(alignmentView_.strip);
    }
    editMessage_ = panorama_.isNull() ? QString{} : tr("Stitch again to use your corrections.");
    updateStatus();
    return true;
}

bool MainWindow::rotateSelected(double degrees) {
    return adjustSelected(core::PhotoAdjustment{degrees, 0.0, 0.0, 1.0});
}

bool MainWindow::tiltSelected(double degrees) {
    return adjustSelected(core::PhotoAdjustment{0.0, degrees, 0.0, 1.0});
}

bool MainWindow::moveSelected(double rows) {
    return adjustSelected(
        core::PhotoAdjustment{0.0, 0.0, rows * alignmentView_.unitsPerPixel, 1.0});
}

bool MainWindow::scaleSelected(double factor) {
    return adjustSelected(core::PhotoAdjustment{0.0, 0.0, 0.0, factor});
}

bool MainWindow::resetSelected() {
    const core::PhotoAdjustment current = selectedAdjustment();
    return adjustSelected(core::PhotoAdjustment{-current.rollDegrees, -current.pitchDegrees,
                                                -current.shift, 1.0 / current.scale});
}

void MainWindow::setOriginalShown(bool shown) {
    if (showOriginalAction_->isChecked() != shown) {
        showOriginalAction_->setChecked(shown);  // Calls this again through the signal.
        return;
    }
    originalShown_ = shown;
    if (alignmentView_.strip.isNull()) {
        return;
    }
    alignView_->setImage(shown ? alignmentView_.original : alignmentView_.strip);
    // Guides mark heights on the wall, which only the corrected view shows.
    if (shown) {
        alignView_->setGuides({});
    } else {
        showGuides();
    }
}

void MainWindow::showGuides() {
    std::vector<double> rows;
    rows.reserve(alignment_.guides.size());
    for (const double height : alignment_.guides) {
        rows.push_back(alignmentView_.rowForHeight(height));
    }
    alignView_->setGuides(std::move(rows));
}

void MainWindow::saveGuides() {
    if (!alignmentPath_.isEmpty() && !stitch::writeAlignment(alignment_, alignmentPath_)) {
        editMessage_ = tr("The guides couldn't be saved to %1.").arg(alignmentPath_);
        updateStatus();
    }
}

bool MainWindow::addGuide() {
    if (alignmentView_.strip.isNull()) {
        return false;
    }
    alignment_.guides.push_back(alignmentView_.heightForRow(alignmentView_.strip.height() / 2.0));
    saveGuides();
    showGuides();
    return true;
}

bool MainWindow::moveGuide(int index, double row) {
    if (index < 0 || index >= static_cast<int>(alignment_.guides.size())) {
        return false;
    }
    // Dragging a guide off the photos removes it.
    if (row < 0.0 || row > alignmentView_.strip.height()) {
        return removeGuide(index);
    }
    alignment_.guides[static_cast<std::size_t>(index)] = alignmentView_.heightForRow(row);
    saveGuides();
    showGuides();
    return true;
}

bool MainWindow::removeGuide(int index) {
    if (index < 0 || index >= static_cast<int>(alignment_.guides.size())) {
        return false;
    }
    alignment_.guides.erase(alignment_.guides.begin() + index);
    saveGuides();
    showGuides();
    return true;
}

QWidget* MainWindow::createProjectsPage() {
    auto* page = makeChild<QWidget>(this);
    auto* layout = makeChild<QVBoxLayout>(page);
    auto* title = makeChild<QLabel>(page, tr("Projects"));
    QFont font = title->font();
    font.setPointSizeF(font.pointSizeF() * 1.6);
    font.setBold(true);
    title->setFont(font);
    layout->addWidget(title);
    layout->addWidget(makeChild<QLabel>(
        page, tr("Each street is a project. Photos stay where they are and are never copied. "
                 "The project's own files go in a folder beside them.")));
    auto* row = makeChild<QWidget>(page);
    auto* buttons = makeChild<QHBoxLayout>(row);
    buttons->setContentsMargins(0, 0, 0, 0);
    auto* newButton = makeChild<QPushButton>(page, tr("New Project…"));
    auto* openButton = makeChild<QPushButton>(page, tr("Open Project…"));
    connect(newButton, &QPushButton::clicked, this, &MainWindow::chooseNewProject);
    connect(openButton, &QPushButton::clicked, this, &MainWindow::chooseProject);
    buttons->addWidget(newButton);
    buttons->addWidget(openButton);
    buttons->addStretch();
    layout->addWidget(row);
    layout->addWidget(makeChild<QLabel>(page, tr("Recent projects")));
    recentList_ = makeChild<QListWidget>(page);
    connect(recentList_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* item) { openProject(item->data(Qt::UserRole).toString()); });
    layout->addWidget(recentList_, 1);
    projectsMessage_ = makeChild<QLabel>(page);
    projectsMessage_->setWordWrap(true);
    layout->addWidget(projectsMessage_);
    return page;
}

QWidget* MainWindow::createExportPage() {
    auto* page = makeChild<QWidget>(this);
    auto* layout = makeChild<QVBoxLayout>(page);
    auto* title = makeChild<QLabel>(page, tr("Export"));
    QFont font = title->font();
    font.setPointSizeF(font.pointSizeF() * 1.6);
    font.setBold(true);
    title->setFont(font);
    layout->addWidget(title);
    exportInfo_ = makeChild<QLabel>(page);
    exportInfo_->setWordWrap(true);
    layout->addWidget(exportInfo_);
    auto* row = makeChild<QWidget>(page);
    auto* buttons = makeChild<QHBoxLayout>(row);
    buttons->setContentsMargins(0, 0, 0, 0);
    auto* save = makeChild<QPushButton>(page, tr("Save Panorama"));
    auto* saveAs = makeChild<QPushButton>(page, tr("Save As…"));
    connect(save, &QPushButton::clicked, this, [this] { savePanoramaFile(); });
    connect(saveAs, &QPushButton::clicked, this, &MainWindow::chooseSaveAs);
    buttons->addWidget(save);
    buttons->addWidget(saveAs);
    buttons->addStretch();
    layout->addWidget(row);
    layout->addStretch();
    return page;
}

void MainWindow::refreshRecentProjects() {
    recentList_->clear();
    for (const QString& file : RecentProjects::load()) {
        const stitch::ProjectResult result = stitch::readProject(file);
        if (result.project.has_value()) {
            auto* item = makeChild<QListWidgetItem>(
                recentList_, QStringLiteral("%1   \u00b7   %2")
                                 .arg(result.project->name, result.project->photoFolder));
            item->setData(Qt::UserRole, file);
        }
    }
}

bool MainWindow::openProjectForFolder(const QString& photoFolder) {
    return createProject({}, photoFolder);
}

bool MainWindow::createProject(const QString& name, const QString& photoFolder) {
    const stitch::ProjectResult result = stitch::openOrCreateProject(photoFolder, name);
    if (!result.project.has_value()) {
        projectsMessage_->setText(result.error);
        return false;
    }
    return openProject(result.project->projectFile());
}

bool MainWindow::openProject(const QString& projectFile) {
    const stitch::ProjectResult result = stitch::readProject(projectFile);
    if (!result.project.has_value()) {
        projectsMessage_->setText(result.error);
        RecentProjects::remove(projectFile);
        refreshRecentProjects();
        return false;
    }
    const stitch::Project& opened = *result.project;
    if (!openFolder(opened.photoFolder)) {
        projectsMessage_->setText(
            tr("The photos for %1 aren't in %2 any more.").arg(opened.name, opened.photoFolder));
        return false;
    }
    project_ = opened;
    projectsMessage_->clear();
    RecentProjects::add(opened.projectFile());
    refreshRecentProjects();
    setWindowTitle(QStringLiteral("%1 - StreetPano").arg(opened.name));
    return true;
}

void MainWindow::chooseNewProject() {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("New Project"), tr("Name of the street:"),
                                               QLineEdit::Normal, {}, &accepted);
    if (!accepted) {
        return;
    }
    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("Choose the folder with your photos. They stay where they are."));
    if (!folder.isEmpty()) {
        createProject(name, folder);
    }
}

void MainWindow::chooseProject() {
    const QString folder = QFileDialog::getExistingDirectory(
        this, tr("Choose a folder of photos, or its .streetpano folder"));
    if (folder.isEmpty()) {
        return;
    }
    const QString projectFile = QDir(folder).filePath(QStringLiteral("project.json"));
    if (QFile::exists(projectFile)) {
        openProject(projectFile);
    } else {
        openProjectForFolder(folder);
    }
}

void MainWindow::chooseSaveAs() {
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Panorama As"), panoramaPath_,
                                                      tr("Images (*.jpg *.png *.tif)"));
    if (!path.isEmpty()) {
        savePanoramaAs(path);
    }
}

bool MainWindow::savePanoramaAs(const QString& path) {
    if (panorama_.isNull()) {
        return false;
    }
    const bool saved = stitch::savePanorama(panorama_, path);
    editMessage_ =
        saved ? tr("Saved %1").arg(path) : tr("The panorama couldn't be saved to %1.").arg(path);
    exportInfo_->setText(exportInfo_->text().section(u'\n', 0, 0) + u'\n' + editMessage_);
    updateStatus();
    return saved;
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
    if (patch.kind == "marked" && patch.note.empty()) {
        text = patch.outline.size() == 2 ? tr("Line, no choice yet") : tr("Marked, no choice yet");
    } else if (patch.outline.size() == 2) {
        text = tr("Line");
    } else if (patch.kind == "remove") {
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
        if (hasPendingArea()) {
            auto* item = makeChild<QListWidgetItem>(
                editsList_, tr("%1. New %2: choose what to do")
                                .arg(decisions_.patches.size() + 1)
                                .arg(pendingArea_.size() == 2 ? tr("line") : tr("area")));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            QFont font = item->font();
            font.setItalic(true);
            item->setFont(font);
            item->setToolTip(tr("Right-click for choices, or use the toolbar"));
            editsList_->setCurrentRow(editsList_->count() - 1);
        } else {
            editsList_->setCurrentRow(selectedArea_);
        }
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

void MainWindow::setMarkShape(PhotoView::MarkShape shape) {
    view_->setMarkShape(shape);
    freehandAction_->setChecked(shape == PhotoView::MarkShape::Freehand);
    rectangleAction_->setChecked(shape == PhotoView::MarkShape::Rectangle);
    lineAction_->setChecked(shape == PhotoView::MarkShape::Line);
    if (markAreaAction_->isEnabled()) {
        markAreaAction_->setChecked(true);
    }
}

bool MainWindow::isTargetALine() const {
    if (!pendingArea_.isEmpty()) {
        return pendingArea_.size() == 2;
    }
    return selectedArea_ >= 0 &&
           decisions_.patches[static_cast<std::size_t>(selectedArea_)].outline.size() == 2;
}

bool MainWindow::hits(const QPolygonF& outline, QPointF point) const {
    if (outline.size() != 2) {
        return outline.containsPoint(point, Qt::OddEvenFill);
    }
    // A line has no inside, so a click near it counts.
    constexpr double kReachScreenPixels = 8.0;
    const double reach = kReachScreenPixels / std::max(view_->zoom(), 1e-9);
    const QPointF start = outline.at(0);
    const QPointF along = outline.at(1) - start;
    const double length2 = QPointF::dotProduct(along, along);
    const double share =
        length2 > 0.0 ? std::clamp(QPointF::dotProduct(point - start, along) / length2, 0.0, 1.0)
                      : 0.0;
    const QPointF nearest = start + along * share;
    return std::hypot(point.x() - nearest.x(), point.y() - nearest.y()) <= reach;
}

void MainWindow::beginArea(const QPolygonF& outline) {
    if (!showingPanorama_ || panorama_.isNull() || outline.size() < 2) {
        return;
    }
    // A previous outline without a choice is kept, so drawing several first loses nothing.
    if (hasPendingArea()) {
        undoStack_.push_back(decisions_);
        redoStack_.clear();
        stitch::Decisions changed = decisions_;
        changed.patches.push_back(
            core::Patch{toWall(pendingArea_, stitched_.plan), {}, {}, "marked", false});
        pendingArea_.clear();
        applyDecisions(std::move(changed), -1);
    }
    pendingArea_ = outline;
    selectedArea_ = -1;
    editMessage_ = outline.size() == 2 ? tr("Describe what's wrong along this line.")
                                       : tr("Choose what to do with this area.");
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
    if (outline.size() < 3) {
        return false;  // Nothing marked, or a line, which has no area to fill.
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
    if (outline.size() < 3) {
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
    if (pendingArea_.isEmpty() || !hits(pendingArea_, point)) {
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
        if (hits(toPanorama(patch.outline, stitched_.plan), point)) {
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
    updateStepActions();
    const bool canEdit = showingPanorama_ && !panorama_.isNull();
    markAreaAction_->setEnabled(canEdit);
    if (!canEdit && markAreaAction_->isChecked()) {
        markAreaAction_->setChecked(false);
    }
    const bool selected = canEdit && selectedArea_ >= 0;
    const bool target = canEdit && (selected || hasPendingArea());
    const bool area = target && !isTargetALine();
    removeObjectAction_->setEnabled(area);
    keepObjectAction_->setEnabled(area);
    describeFixAction_->setEnabled(target);
    nextAreaPhotoAction_->setEnabled(selected && area);
    previousAreaPhotoAction_->setEnabled(selected && area);
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
    applyStep(Step::Photos);
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
    if (showingPanorama_) {
        positionLabel_->setText(project_.has_value() ? project_->name : tr("Panorama"));
    } else if (currentRow_ >= 0) {
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
    if (step_ == Step::Align && selectedTile_ >= 0) {
        const core::PhotoAdjustment adjustment = selectedAdjustment();
        positionLabel_->setText(
            tr("Photo %1 of %2: %3   |   turned %4\u00b0, tilted %5\u00b0, moved %6 px, %7% size")
                .arg(selectedTile_ + 1)
                .arg(alignmentView_.tiles.size())
                .arg(QString::fromStdString(
                    alignmentView_.tiles[static_cast<std::size_t>(selectedTile_)].photo))
                .arg(adjustment.rollDegrees, 0, 'f', 1)
                .arg(adjustment.pitchDegrees, 0, 'f', 1)
                .arg(adjustment.shift / alignmentView_.unitsPerPixel, 0, 'f', 0)
                .arg(adjustment.scale * 100.0, 0, 'f', 1));
    }
    if (alignJob_ != nullptr) {
        positionLabel_->setText(
            alignJob_->total() > 0
                ? tr("Preparing photo %1 of %2…").arg(alignJob_->done()).arg(alignJob_->total())
                : tr("Preparing the photos…"));
    }
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
