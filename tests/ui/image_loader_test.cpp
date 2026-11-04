#include "ui/image_loader.hpp"

#include <QFile>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <memory>

#include "test_helpers.hpp"

using streetpano::test::writePhoto;
using streetpano::ui::ImageLoader;
using streetpano::ui::loadImage;

class ImageLoaderTest : public QObject {
    Q_OBJECT

  private slots:
    void readsAFullSizeImage();
    void reducesAnImageToFit();
    void neverEnlargesAnImage();
    void reportsDamagedFiles_data();
    void reportsDamagedFiles();
    void rejectsAHugeDeclaredImage();
    void deliversResultsOnTheOwnerThread();
    void reportsFailuresAsSignals();
    void ignoresDuplicateRequests();
    void cancelsWaitingRequests();
    void canBeDestroyedWhileWorking();
};

void ImageLoaderTest::readsAFullSizeImage() {
    const QTemporaryDir folder;
    const auto result = loadImage(writePhoto(folder, QStringLiteral("a.png"), Qt::red), 0);
    QVERIFY(result.error.isEmpty());
    QCOMPARE(result.image.size(), QSize(320, 180));
}

void ImageLoaderTest::reducesAnImageToFit() {
    const QTemporaryDir folder;
    const auto result = loadImage(writePhoto(folder, QStringLiteral("a.png"), Qt::red), 160);
    QCOMPARE(result.image.size(), QSize(160, 90));
}

void ImageLoaderTest::neverEnlargesAnImage() {
    const QTemporaryDir folder;
    const auto result = loadImage(writePhoto(folder, QStringLiteral("a.png"), Qt::red), 1000);
    QCOMPARE(result.image.size(), QSize(320, 180));
}

void ImageLoaderTest::reportsDamagedFiles_data() {
    QTest::addColumn<QByteArray>("content");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("text") << QByteArray("this is not an image");
    QByteArray noise(4096, Qt::Uninitialized);
    for (char& value : noise) {
        value = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    QTest::newRow("noise") << noise;
    QTest::newRow("png signature only") << QByteArray("\x89PNG\r\n\x1a\n", 8);

    const QTemporaryDir folder;
    QFile real(writePhoto(folder, QStringLiteral("real.png"), Qt::green));
    QVERIFY(real.open(QIODevice::ReadOnly));
    QTest::newRow("truncated png") << real.readAll().left(40);
}

void ImageLoaderTest::reportsDamagedFiles() {
    QFETCH(QByteArray, content);
    const QTemporaryDir folder;
    QFile file(folder.filePath(QStringLiteral("bad.png")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
    file.close();

    const auto result = loadImage(file.fileName(), 0);
    QVERIFY(result.image.isNull());
    QVERIFY(!result.error.isEmpty());
}

void ImageLoaderTest::rejectsAHugeDeclaredImage() {
    // A valid PNG header claiming 60000 x 60000 pixels must not allocate that much memory.
    const QTemporaryDir folder;
    QFile real(writePhoto(folder, QStringLiteral("real.png"), Qt::green));
    QVERIFY(real.open(QIODevice::ReadOnly));
    QByteArray content = real.readAll();
    qToBigEndian<quint32>(60000, content.data() + 16);
    qToBigEndian<quint32>(60000, content.data() + 20);

    QFile file(folder.filePath(QStringLiteral("huge.png")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
    file.close();

    const auto result = loadImage(file.fileName(), 0);
    QVERIFY(result.image.isNull());
}

void ImageLoaderTest::deliversResultsOnTheOwnerThread() {
    const QTemporaryDir folder;
    const QString path = writePhoto(folder, QStringLiteral("a.png"), Qt::red);
    ImageLoader loader(2, 0);
    QThread* deliveredOn = nullptr;
    QImage delivered;
    connect(&loader, &ImageLoader::loaded, this, [&](int, const QImage& image) {
        deliveredOn = QThread::currentThread();
        delivered = image;
    });
    loader.request(7, path);
    QTRY_VERIFY(!delivered.isNull());
    QCOMPARE(deliveredOn, QThread::currentThread());
}

void ImageLoaderTest::reportsFailuresAsSignals() {
    const QTemporaryDir folder;
    ImageLoader loader(1, 0);
    QSignalSpy failed(&loader, &ImageLoader::failed);
    loader.request(3, folder.filePath(QStringLiteral("missing.png")));
    QTRY_COMPARE(failed.count(), 1);
    QCOMPARE(failed.first().at(0).toInt(), 3);
}

void ImageLoaderTest::ignoresDuplicateRequests() {
    const QTemporaryDir folder;
    const QString path = writePhoto(folder, QStringLiteral("a.png"), Qt::red);
    ImageLoader loader(1, 0);
    QSignalSpy loaded(&loader, &ImageLoader::loaded);
    loader.request(1, path);
    loader.request(1, path);
    loader.request(1, path);
    QTRY_COMPARE(loaded.count(), 1);
    QTest::qWait(50);
    QCOMPARE(loaded.count(), 1);
}

void ImageLoaderTest::cancelsWaitingRequests() {
    const QTemporaryDir folder;
    const QString path = writePhoto(folder, QStringLiteral("a.png"), Qt::red, 2000, 2000);
    ImageLoader loader(1, 0);
    QSignalSpy loaded(&loader, &ImageLoader::loaded);
    for (int key = 0; key < 50; ++key) {
        loader.request(key, path);
    }
    loader.cancelPending();
    QTest::qWait(300);
    QVERIFY(loaded.count() < 50);
}

void ImageLoaderTest::canBeDestroyedWhileWorking() {
    const QTemporaryDir folder;
    const QString path = writePhoto(folder, QStringLiteral("a.png"), Qt::red, 1500, 1500);
    for (int attempt = 0; attempt < 20; ++attempt) {
        auto loader = std::make_unique<ImageLoader>(4, 0);
        for (int key = 0; key < 8; ++key) {
            loader->request(key, path);
        }
        loader.reset();
    }
    // Undelivered results must vanish with the loader, without a crash.
    QTest::qWait(50);
}

QTEST_GUILESS_MAIN(ImageLoaderTest)
#include "image_loader_test.moc"
