#pragma once

#include <QAction>
#include <QImage>
#include <QLabel>
#include <QListView>
#include <QMainWindow>
#include <QString>
#include <cstddef>
#include <memory>

#include "core/lru_cache.hpp"
#include "ui/image_loader.hpp"
#include "ui/photo_list_model.hpp"
#include "ui/photo_view.hpp"
#include "ui/stitch_job.hpp"

namespace streetpano::ui {

// The first screen: open a folder, move along the film strip, and inspect each photo.
class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    static constexpr std::size_t kDefaultPreviewBudgetBytes = std::size_t{256} * 1024 * 1024;

    explicit MainWindow(std::size_t previewBudgetBytes = kDefaultPreviewBudgetBytes,
                        QWidget* parent = nullptr);

    // Returns false, and shows the reason, if the folder can't be used.
    bool openFolder(const QString& folder);
    void selectPhoto(int row);

    // Builds a quick panorama using this camera position file. Returns false if no folder is
    // open, or a stitch is already running.
    bool startStitch(const QString& posesPath);

    // Stitches using the camera positions found next to the open folder, if any.
    bool startStitchFromFolder();
    void showPanorama();
    [[nodiscard]] bool isStitching() const { return stitchJob_ != nullptr; }
    [[nodiscard]] const QImage& panorama() const { return panorama_; }
    [[nodiscard]] QString stitchMessage() const { return stitchMessage_; }
    [[nodiscard]] QString panoramaPath() const { return panoramaPath_; }

    [[nodiscard]] int photoCount() const { return model_->rowCount(); }
    [[nodiscard]] int currentPhoto() const { return currentRow_; }
    [[nodiscard]] PhotoView* photoView() const { return view_; }
    [[nodiscard]] PhotoListModel* photoModel() const { return model_; }
    [[nodiscard]] QListView* filmStrip() const { return strip_; }
    [[nodiscard]] QString positionText() const { return positionLabel_->text(); }
    [[nodiscard]] std::size_t previewCacheUsedBytes() const { return previewCache_.usedBytes(); }

  private:
    void chooseFolder();
    void showPhoto(int row);
    void requestPreview(int row);
    void createPreviewLoader();
    void onPreviewLoaded(int row, const QImage& image);
    void onPreviewFailed(int row, const QString& message);
    void updateStatus();
    void chooseStitch();
    void onStitchFinished();
    void cancelStitch();
    [[nodiscard]] QString findPosesFile() const;

    PhotoListModel* model_;
    PhotoView* view_;
    QListView* strip_;
    QLabel* positionLabel_;
    QLabel* memoryLabel_;
    core::LruCache<int, QImage> previewCache_;
    std::unique_ptr<ImageLoader> previewLoader_;
    int currentRow_ = -1;

    QString currentFolder_;
    QString stitchMessage_;
    QString panoramaPath_;
    QImage panorama_;
    QAction* showPanoramaAction_ = nullptr;
    std::unique_ptr<StitchJob> stitchJob_;
};

}  // namespace streetpano::ui
