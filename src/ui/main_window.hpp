#pragma once

#include <QAction>
#include <QDockWidget>
#include <QImage>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QStackedWidget>
#include <QString>
#include <QToolBar>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/lru_cache.hpp"
#include "stitch/alignment.hpp"
#include "stitch/decisions.hpp"
#include "stitch/project.hpp"
#include "stitch/stitcher.hpp"
#include "ui/align_job.hpp"
#include "ui/image_loader.hpp"
#include "ui/photo_list_model.hpp"
#include "ui/photo_view.hpp"
#include "ui/stitch_job.hpp"

namespace streetpano::ui {

// The first screen: open a folder, move along the film strip, and inspect each photo.
class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    // The steps along the top, in the order a street goes through them.
    enum class Step { Projects, Photos, Align, Edit, Export };

    static constexpr std::size_t kDefaultPreviewBudgetBytes = std::size_t{256} * 1024 * 1024;

    explicit MainWindow(std::size_t previewBudgetBytes = kDefaultPreviewBudgetBytes,
                        QWidget* parent = nullptr);

    // Projects. Photos are used where they are, and the project's own files go beside them.
    // An empty name uses the folder's name. Each returns false, and shows why, on failure.
    bool createProject(const QString& name, const QString& photoFolder);
    bool openProject(const QString& projectFile);
    bool openProjectForFolder(const QString& photoFolder);
    [[nodiscard]] const std::optional<stitch::Project>& project() const { return project_; }
    [[nodiscard]] QListWidget* recentProjectsList() const { return recentList_; }
    [[nodiscard]] QString projectsMessage() const { return projectsMessage_->text(); }

    // Steps. A step that can't be used yet, such as Edit before stitching, is ignored.
    void setStep(Step step);
    [[nodiscard]] Step step() const { return step_; }
    [[nodiscard]] QAction* stepAction(Step step) const;

    // Aligning. The photos are shown side by side at the same scale, with guide lines that are
    // heights on the wall, saved with the project.
    [[nodiscard]] bool isPreparingAlignment() const { return alignJob_ != nullptr; }
    [[nodiscard]] const stitch::AlignmentView& alignmentView() const { return alignmentView_; }
    [[nodiscard]] PhotoView* alignView() const { return alignView_; }
    [[nodiscard]] std::size_t guideCount() const { return alignment_.guides.size(); }
    [[nodiscard]] QString alignmentPath() const { return alignmentPath_; }
    bool addGuide();
    bool moveGuide(int index, double row);
    bool removeGuide(int index);

    // Saves the panorama with its edits. The format follows the file's extension.
    bool savePanoramaAs(const QString& path);

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
    // The usual way to edit: an outline only marks an area, and nothing changes until one of these
    // is chosen. They act on the outline just drawn, or else on the selected area.
    void beginArea(const QPolygonF& outline);
    bool removeObject();
    bool keepObject();
    // Saves what the user wants done here. No AI acts on it yet, so the panorama doesn't change.
    bool describeFix(const QString& description);
    bool cancelOrDeleteArea();
    [[nodiscard]] bool hasPendingArea() const { return !pendingArea_.isEmpty(); }

    // The edits panel lists every area, newest at the bottom, which is drawn on top.
    void selectArea(int index);
    bool setAreaHidden(int index, bool hidden);
    bool moveArea(int from, int to);
    void setOutlinesShown(bool shown);
    [[nodiscard]] QListWidget* editsList() const { return editsList_; }
    [[nodiscard]] QAction* showOutlinesAction() const { return showOutlinesAction_; }
    // Chooses what a drag draws, and turns marking on.
    void setMarkShape(PhotoView::MarkShape shape);
    [[nodiscard]] QMenu* areaMenu() const { return areaMenu_; }
    [[nodiscard]] QString selectedAreaNote() const;
    [[nodiscard]] QString editMessage() const { return editMessage_; }

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
    [[nodiscard]] QString memoryText() const { return memoryLabel_->text(); }
    [[nodiscard]] std::size_t previewCacheUsedBytes() const { return previewCache_.usedBytes(); }

  private:
    void chooseFolder();
    void chooseNewProject();
    void chooseProject();
    void chooseSaveAs();
    void refreshRecentProjects();
    void applyStep(Step step);
    void updateStepActions();
    void displayPanorama();
    void startAlignment();
    void onAlignmentFinished();
    void showGuides();
    void saveGuides();
    [[nodiscard]] bool isStepAvailable(Step step) const;
    [[nodiscard]] QWidget* createProjectsPage();
    [[nodiscard]] QWidget* createExportPage();
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
    void showAreaMenu(QPoint screenPoint);
    void onAreaRightClicked(QPointF point, QPoint screenPoint);
    void askForFixDescription();
    [[nodiscard]] std::vector<core::WallPoint> targetOutline() const;
    bool commitArea(std::string photo, const char* kind, const QString& message);
    void refreshEditsList();
    void onEditsRowMoved(int from, int to);
    [[nodiscard]] QString describeArea(const core::Patch& patch) const;
    [[nodiscard]] bool isTargetALine() const;
    [[nodiscard]] bool hits(const QPolygonF& outline, QPointF point) const;
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
    Step step_ = Step::Projects;
    std::optional<stitch::Project> project_;
    QStackedWidget* pages_ = nullptr;
    QWidget* workPage_ = nullptr;
    QWidget* projectsPage_ = nullptr;
    QWidget* alignPage_ = nullptr;
    QWidget* exportPage_ = nullptr;
    QListWidget* recentList_ = nullptr;
    QLabel* projectsMessage_ = nullptr;
    QLabel* exportInfo_ = nullptr;
    std::vector<QAction*> stepActions_;
    QToolBar* alignTools_ = nullptr;
    PhotoView* alignView_ = nullptr;
    std::unique_ptr<AlignJob> alignJob_;
    stitch::AlignmentView alignmentView_;
    stitch::Alignment alignment_;
    QString alignmentPath_;
    QAction* showGridAction_ = nullptr;
    QToolBar* editTools_ = nullptr;
    QDockWidget* editsDock_ = nullptr;
    QPolygonF pendingArea_;
    QMenu* areaMenu_ = nullptr;
    QListWidget* editsList_ = nullptr;
    QAction* showOutlinesAction_ = nullptr;
    QAction* freehandAction_ = nullptr;
    QAction* rectangleAction_ = nullptr;
    QAction* lineAction_ = nullptr;
    bool refreshingList_ = false;
    QAction* removeObjectAction_ = nullptr;
    QAction* keepObjectAction_ = nullptr;
    QAction* describeFixAction_ = nullptr;
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
