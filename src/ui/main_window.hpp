#pragma once

#include <QAction>
#include <QImage>
#include <QLabel>
#include <QListView>
#include <QMainWindow>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <cstddef>
#include <memory>
#include <vector>

#include "core/lru_cache.hpp"
#include "stitch/decisions.hpp"
#include "stitch/stitcher.hpp"
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
    [[nodiscard]] bool isShowingPanorama() const { return showingPanorama_; }

    // Editing the panorama. Each edit is saved at once as a decision, and the panorama is drawn
    // again from the original photos, so any edit can be changed or undone later.
    // Marks an area, given in panorama pixels, and fills it from the best photo. Returns false if
    // no photo can supply it.
    bool markArea(const QPolygonF& outline);
    // Moves the selected area to the next or previous photo that can supply it.
    bool changeAreaPhoto(int step);
    bool deleteSelectedArea();
    bool undo();
    bool redo();
    void selectAreaAt(QPointF point);
    bool savePanoramaFile();
    [[nodiscard]] int areaCount() const { return static_cast<int>(decisions_.patches.size()); }
    [[nodiscard]] int selectedArea() const { return selectedArea_; }
    [[nodiscard]] QString selectedAreaPhoto() const;
    [[nodiscard]] QString decisionsPath() const { return decisionsPath_; }
    [[nodiscard]] QAction* markAreaAction() const { return markAreaAction_; }
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
    void redrawPanorama();
    void applyDecisions(stitch::Decisions decisions, int selected);
    void updateEditActions();
    [[nodiscard]] std::vector<std::size_t> photoChoices(const core::Patch& patch) const;
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
    stitch::StitchResult stitched_;
    stitch::Decisions decisions_;
    std::vector<stitch::Decisions> undoStack_;
    std::vector<stitch::Decisions> redoStack_;
    QString decisionsPath_;
    QString editMessage_;
    int selectedArea_ = -1;
    bool showingPanorama_ = false;
    QAction* markAreaAction_ = nullptr;
    QAction* nextAreaPhotoAction_ = nullptr;
    QAction* previousAreaPhotoAction_ = nullptr;
    QAction* deleteAreaAction_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* savePanoramaAction_ = nullptr;
    QAction* showPanoramaAction_ = nullptr;
    std::unique_ptr<StitchJob> stitchJob_;
};

}  // namespace streetpano::ui
