#include "ui/main_window.hpp"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QMenuBar>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <filesystem>

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
    const double used = static_cast<double>(previewCache_.usedBytes() + model_->cacheUsedBytes()) /
                        kBytesPerMegabyte;
    const double budget =
        static_cast<double>(previewCache_.budgetBytes() + model_->cacheBudgetBytes()) /
        kBytesPerMegabyte;
    memoryLabel_->setText(
        tr("Image memory: %1 of %2 MB").arg(used, 0, 'f', 0).arg(budget, 0, 'f', 0));
}

}  // namespace streetpano::ui
