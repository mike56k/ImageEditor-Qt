#include "imageviewer.h"
#include <QApplication>
#include <QClipboard>
#include <QColorSpace>
#include <QFileDialog>
#include <QImageReader>
#include <QImageWriter>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QStatusBar>
#include <QObject>
#include <QToolBar>
#include <effectwindow.h>
#include "commands.h"
#include <iostream>

ImageViewer::ImageViewer(QWidget *parent)
   : QMainWindow(parent), imageLabel(new ImageLabelWithRubberBand)
   , scrollArea(new QScrollArea)
{
    undoStack = new QUndoStack(this);
    imageLabel->setBackgroundRole(QPalette::Base);
    imageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    imageLabel->setScaledContents(true);

    QObject::connect(imageLabel, SIGNAL(areaSelected()), this, SLOT(showSelectedArea()));

    scrollArea->setBackgroundRole(QPalette::Dark);
    scrollArea->setWidget(imageLabel);
    scrollArea->setVisible(false);
    setCentralWidget(scrollArea);

    createActions();

    resize(QGuiApplication::primaryScreen()->availableSize() * 3 / 5);
    addToolBar(Qt::LeftToolBarArea, createToolBar());
}

QToolBar *ImageViewer::createToolBar()
{
    QToolBar* ptb = new QToolBar("Linker ToolBar");

    ptb->addAction(saveAsAct);
    ptb->addAction(copyAct);
    ptb->addAction(zoomInAct);
    ptb->addAction(zoomOutAct);
    ptb->addAction(normalSizeAct);
    ptb->addAction(fitToWindowAct);
    ptb->addAction(cropAct);
    ptb->addAction(undoAction);
    ptb->addAction(redoAction);
    ptb->addAction(paintAct);
    return ptb;
}

bool ImageViewer::loadFile(const QString &fileName)
{
    QImageReader reader(fileName);
    reader.setAutoTransform(true);
    const QImage newImage = reader.read();
    if (newImage.isNull()) {
        QMessageBox::information(this, QGuiApplication::applicationDisplayName(),
                                 tr("Cannot load %1: %2")
                                 .arg(QDir::toNativeSeparators(fileName), reader.errorString()));
        return false;
    }

    setImage(newImage);

    setWindowFilePath(fileName);

    const QString message = tr("Opened \"%1\", %2x%3, Depth: %4")
        .arg(QDir::toNativeSeparators(fileName)).arg(image.width()).arg(image.height()).arg(image.depth());
    statusBar()->showMessage(message);
    return true;
}

void ImageViewer::setImage(const QImage &newImage)
{
    image = newImage;
    imageAfterEffect = image;
    w = new effectwindow(image, imageAfterEffect);
    w->setModal(true);
    QObject::connect(w, SIGNAL(finished (int)), this, SLOT(dialogIsFinished(int)));
    QObject::connect(this, SIGNAL(imageChanged()), w, SLOT(repaintEffectWindow()));
    if (image.colorSpace().isValid())
        image.convertToColorSpace(QColorSpace::SRgb);
    imageLabel->setPixmap(QPixmap::fromImage(image));
    scaleFactor = 1.0;
    scrollArea->setVisible(true);
    fitToWindowAct->setEnabled(true);
    cropAct->setEnabled(true);
    updateActions();
    if (!fitToWindowAct->isChecked())
        imageLabel->adjustSize();

}

bool ImageViewer::saveFile(const QString &fileName)
{
    QImageWriter writer(fileName);

    if (!writer.write(image)) {
        QMessageBox::information(this, QGuiApplication::applicationDisplayName(),
                                 tr("Cannot write %1: %2")
                                 .arg(QDir::toNativeSeparators(fileName)), writer.errorString());
        return false;
    }
    const QString message = tr("Wrote \"%1\"").arg(QDir::toNativeSeparators(fileName));
    statusBar()->showMessage(message);
    return true;
}

static void initializeImageFileDialog(QFileDialog &dialog, QFileDialog::AcceptMode acceptMode)
{
    static bool firstDialog = true;

    if (firstDialog) {
        firstDialog = false;
        const QStringList picturesLocations = QStandardPaths::standardLocations(QStandardPaths::PicturesLocation);
        dialog.setDirectory(picturesLocations.isEmpty() ? QDir::currentPath() : picturesLocations.last());
    }

    QStringList mimeTypeFilters;
    const QByteArrayList supportedMimeTypes = acceptMode == QFileDialog::AcceptOpen
        ? QImageReader::supportedMimeTypes() : QImageWriter::supportedMimeTypes();
    for (const QByteArray &mimeTypeName : supportedMimeTypes)
        mimeTypeFilters.append(mimeTypeName);
    mimeTypeFilters.sort();
    dialog.setMimeTypeFilters(mimeTypeFilters);
    dialog.selectMimeTypeFilter("image/jpeg");
    if (acceptMode == QFileDialog::AcceptSave)
        dialog.setDefaultSuffix("jpg");
}

void ImageViewer::open()
{
    QFileDialog dialog(this, tr("Open File"));
    initializeImageFileDialog(dialog, QFileDialog::AcceptOpen);

    while (dialog.exec() == QDialog::Accepted && !loadFile(dialog.selectedFiles().first())) {}
}




void ImageViewer::copy()
{
#ifndef QT_NO_CLIPBOARD
    QGuiApplication::clipboard()->setImage(image);
#endif
}

#ifndef QT_NO_CLIPBOARD
static QImage clipboardImage()
{
    if (const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData()) {
        if (mimeData->hasImage()) {
            const QImage image = qvariant_cast<QImage>(mimeData->imageData());
            if (!image.isNull())
                return image;
        }
    }
    return QImage();
}
#endif

void ImageViewer::paste()
{
#ifndef QT_NO_CLIPBOARD
    const QImage newImage = clipboardImage();
    if (newImage.isNull()) {
        statusBar()->showMessage(tr("No image in clipboard"));
    } else {
        setImage(newImage);
        setWindowFilePath(QString());
        const QString message = tr("Obtained image from clipboard, %1x%2, Depth: %3")
            .arg(newImage.width()).arg(newImage.height()).arg(newImage.depth());
        statusBar()->showMessage(message);
    }
#endif
}

void ImageViewer::normalSize()

{
    imageLabel->adjustSize();
    scaleFactor = 1.0;
}

void ImageViewer::fitToWindow()

{
    bool fitToWindow = fitToWindowAct->isChecked();
    scrollArea->setWidgetResizable(fitToWindow);
    if (!fitToWindow)
        normalSize();
    updateActions();
}

void ImageViewer::zoomIn()

{
    scaleImage(1.25);
}

void ImageViewer::zoomOut()
{
    scaleImage(0.8);
}



void ImageViewer::saveAs()
{
    QFileDialog dialog(this, tr("Save File As"));
    initializeImageFileDialog(dialog, QFileDialog::AcceptSave);

    while (dialog.exec() == QDialog::Accepted && !saveFile(dialog.selectedFiles().first())) {}
}

void ImageViewer::about()

{
    QMessageBox::about(this, tr("About Image Viewer"),
            tr("<p>The <b>Image Viewer</b> example shows how to combine QLabel "
               "and QScrollArea to display an image. QLabel is typically used "
               "for displaying a text, but it can also display an image. "
               "QScrollArea provides a scrolling view around another widget. "
               "If the child widget exceeds the size of the frame, QScrollArea "
               "automatically provides scroll bars. </p><p>The example "
               "demonstrates how QLabel's ability to scale its contents "
               "(QLabel::scaledContents), and QScrollArea's ability to "
               "automatically resize its contents "
               "(QScrollArea::widgetResizable), can be used to implement "
               "zooming and scaling features."));
}

void ImageViewer::crop()
{

    imageLabel->cropState = cropAct->isChecked();
    updateActions();
}

void ImageViewer::paint()
{
    pw = new paintwindow(image, &imageAfterEffect);
    QObject::connect(pw, SIGNAL(finished(int)),this, SLOT(dialogIsFinished(int)));

    pw->show();
}

void ImageViewer::showSelectedArea()
{
    QPoint a(imageLabel->begin), b(imageLabel->end);
    if (b.x() < a.x() && b.y() < a.y()){
        QPoint t = a;
        a = b;
        b = t;
    }
    else if(b.x() < a.x()){
        int t = a.x();
        a.setX(b.x());
        b.setX(t);
    }
    else if(b.y() < a.y()){
        int t = a.y();
        a.setY(b.y());
        b.setY(t);
    }
    if(b.x() > imageLabel->width()) b.setX(imageLabel->width() - 1);
    if(b.y() > imageLabel->height()) b.setY(imageLabel->height() - 1);
    if(a.x() < imageLabel->x()) a.setX(imageLabel->x());
    if(a.y() < imageLabel->y()) a.setY((imageLabel->y()));
    QRect r(a,b);
    imageAfterEffect = image.copy(r);
    w->show();
    changeImage(imageAfterEffect);
    w->slider->setEnabled(false);
}
void ImageViewer::dialogIsFinished(int result){ //this is a slot
   if(result == QDialog::Accepted){
       QUndoCommand *addCommand = new AddCommand(imageAfterEffect, image, this);
       std::cout << "HERE" << std::endl;
       undoStack->push(addCommand);
       return;
   }
   w->slider->setEnabled(true);
}

void ImageViewer::showBrightnessEffect()
{
    changeImage(image);
    QObject::connect(w->slider, SIGNAL(valueChanged (int)), this, SLOT(brightnessAlgorithm(int)));
    w->show();
}

void ImageViewer::showSepia()
{
    cv::Mat src = Convert::QImageToCvMat(image);
    cv::Mat dst ;
    cv::Mat kern = (cv::Mat_<float>(4,4) <<  0.272, 0.534, 0.131, 0,
                                                 0.349, 0.686, 0.168, 0,
                                                 0.393, 0.769, 0.189, 0,
                                                 0, 0, 0, 1);
    cv::transform(src, dst, kern);
    cv::Mat temp;
    cvtColor(dst, temp,CV_BGR2RGB);
    QImage dest((const uchar *) temp.data, temp.cols, temp.rows, temp.step, QImage::Format_RGB888);
    dest.bits();
    imageAfterEffect = dest;
    changeImage(imageAfterEffect);
    w->slider->setEnabled(false);
    w->show();
}

void ImageViewer::brightnessAlgorithm(int beta)
{
    cv::Mat main_image = Convert::QImageToCvMat(image);
    cv::Mat new_image = cv::Mat::zeros( main_image.size(), main_image.type() );
    double alpha = 2.2; /*< Simple contrast control */
    for( int y = 0; y < main_image.rows; y++ ) {
            for( int x = 0; x < main_image.cols; x++ ) {
                for( int c = 0; c < main_image.channels(); c++ ) {
                    new_image.at<cv::Vec3b>(y,x)[c] =
                      cv::saturate_cast<uchar>( alpha*main_image.at<cv::Vec3b>(y,x)[c] + beta );
                }
            }
    }
    imageAfterEffect = Convert::cvMatToQImage(new_image);
    changeImage(imageAfterEffect);
}
void ImageViewer::showHomogeneousEffect(){
    changeImage(image);
    QObject::connect(w->slider, SIGNAL(valueChanged (int)), this, SLOT(homogeneousAlgorithm(int)));
    w->show();
}
void ImageViewer::homogeneousAlgorithm(int m){
    if(m < 2) m = 2;
    int MAX_KERNEL_LENGTH = m; //Регулировка интенсивности
    cv::Mat dst;
    cv::Mat src = Convert::QImageToCvMat(image);
    for ( int i = 1; i < MAX_KERNEL_LENGTH; i = i + 2 )
        {
            blur( src, dst, cv::Size( i, i ), cv::Point(-1,-1) );
        }
    imageAfterEffect = Convert::cvMatToQImage(dst);
    changeImage(imageAfterEffect);
}
void ImageViewer::showGaussianEffect(){
    changeImage(image);
    QObject::connect(w->slider, SIGNAL(valueChanged (int)), this, SLOT(gaussianAlgorithm(int)));
    w->show();
}
void ImageViewer::gaussianAlgorithm(int m){
    if(m < 2) m = 2;
    int MAX_KERNEL_LENGTH = m; //Регулировка интенсивности
    cv::Mat dst;
    cv::Mat src = Convert::QImageToCvMat(image);
    for ( int i = 1; i < MAX_KERNEL_LENGTH; i = i + 2 ){
            GaussianBlur( src, dst, cv::Size( i, i ), 0, 0 );
     }
    imageAfterEffect = Convert::cvMatToQImage(dst);
    changeImage(imageAfterEffect);
}
void ImageViewer::showMedianEffect(){
    changeImage(image);
    QObject::connect(w->slider, SIGNAL(valueChanged (int)), this, SLOT(medianAlgorithm(int)));
    w->show();
}

void ImageViewer::medianAlgorithm(int m){
    if(m < 2) m = 2;
    int MAX_KERNEL_LENGTH = m; //Регулировка интенсивности
    cv::Mat dst;
    cv::Mat src = Convert::QImageToCvMat(image);
    for ( int i = 1; i < MAX_KERNEL_LENGTH; i = i + 2 ){
            medianBlur ( src, dst, i );
    }
    imageAfterEffect = Convert::cvMatToQImage(dst);
    changeImage(imageAfterEffect);
}
void ImageViewer::showBilateralEffect(){
    changeImage(image);
    QObject::connect(w->slider, SIGNAL(valueChanged (int)), this, SLOT(bilateralAlgorithm(int)));
    w->show();

}
void ImageViewer::bilateralAlgorithm(int m){
    if(m < 2) m = 2;
    int MAX_KERNEL_LENGTH = m; //Регулировка интенсивности
    cv::Mat dst;
    cv::Mat src = Convert::QImageToCvMat(image);
    for ( int i = 1; i < MAX_KERNEL_LENGTH; i = i + 2 ){
            bilateralFilter ( src, dst, i, i*2, i/2 );
    }
    imageAfterEffect = Convert::cvMatToQImage(dst);
    changeImage(imageAfterEffect);
}

cv::Mat ImageViewer::generateHistogram(const cv::Mat &inputImage){
    std::vector<cv::Mat> bgr_planes;
    cv::split( inputImage, bgr_planes );
    int histSize = 256;
    float range[] = { 0, 256 }; //the upper boundary is exclusive
    const float* histRange = { range };
    bool uniform = true, accumulate = false;
    cv::Mat b_hist, g_hist, r_hist;
    cv::calcHist( &bgr_planes[0], 1, 0, cv::Mat(), b_hist, 1, &histSize, &histRange, uniform, accumulate );
    cv::calcHist( &bgr_planes[1], 1, 0, cv::Mat(), g_hist, 1, &histSize, &histRange, uniform, accumulate );
    cv::calcHist( &bgr_planes[2], 1, 0, cv::Mat(), r_hist, 1, &histSize, &histRange, uniform, accumulate );
    int hist_w = 512, hist_h = 400;
    int bin_w = cvRound( (double) hist_w/histSize );
    cv::Mat histImage( hist_h, hist_w, CV_8UC3, cv::Scalar( 0,0,0) );
    cv::normalize(b_hist, b_hist, 0, histImage.rows, cv::NORM_MINMAX, -1, cv::Mat() );
    cv::normalize(g_hist, g_hist, 0, histImage.rows, cv::NORM_MINMAX, -1, cv::Mat() );
    cv::normalize(r_hist, r_hist, 0, histImage.rows, cv::NORM_MINMAX, -1, cv::Mat() );
        for( int i = 1; i < histSize; i++ )
        {
            line( histImage, cv::Point( bin_w*(i-1), hist_h - cvRound(b_hist.at<float>(i-1)) ),
                  cv::Point( bin_w*(i), hist_h - cvRound(b_hist.at<float>(i)) ),
                  cv::Scalar( 255, 0, 0), 2, 8, 0  );
            line( histImage, cv::Point( bin_w*(i-1), hist_h - cvRound(g_hist.at<float>(i-1)) ),
                  cv::Point( bin_w*(i), hist_h - cvRound(g_hist.at<float>(i)) ),
                  cv::Scalar( 0, 255, 0), 2, 8, 0  );
            line( histImage, cv::Point( bin_w*(i-1), hist_h - cvRound(r_hist.at<float>(i-1)) ),
                  cv::Point( bin_w*(i), hist_h - cvRound(r_hist.at<float>(i)) ),
                  cv::Scalar( 0, 0, 255), 2, 8, 0  );
        }

     return histImage;
}
void ImageViewer::showHistogramEqualization(){
    cv::Mat src = Convert::QImageToCvMat(image);
    cv::Mat ycrcb;

    cv::cvtColor(src,ycrcb, CV_BGR2YCrCb);

    std::vector<cv::Mat> channels;
    cv::split(ycrcb,channels);

    cv::equalizeHist(channels[0], channels[0]);

    cv::Mat dst;
    cv::merge(channels,ycrcb);

    cv::cvtColor(ycrcb,dst,CV_YCrCb2BGR);
    imageAfterEffect = Convert::cvMatToQImage(dst);
    QImage histogramBefore = Convert::cvMatToQImage(generateHistogram(src));
    QImage histogramAfter = Convert::cvMatToQImage(generateHistogram(dst));
    effectwindow *hw = new effectwindow(image, imageAfterEffect, histogramBefore, histogramAfter);
    QObject::connect(hw, SIGNAL(finished (int)), this, SLOT(dialogIsFinished(int)));
    hw->show();
}

void ImageViewer::createActions()

{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    QAction *openAct = fileMenu->addAction(tr("&Open..."), this, &ImageViewer::open);
    openAct->setShortcut(QKeySequence::Open);

    saveAsAct = fileMenu->addAction(tr("&Save As..."), this, &ImageViewer::saveAs);
    saveAsAct->setEnabled(false);


    fileMenu->addSeparator();

    QAction *exitAct = fileMenu->addAction(tr("E&xit"), this, &QWidget::close);
    exitAct->setShortcut(tr("Ctrl+Q"));

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));

    copyAct = editMenu->addAction(tr("&Copy"), this, &ImageViewer::copy);
    copyAct->setShortcut(QKeySequence::Copy);
    copyAct->setEnabled(false);

    cropAct = editMenu->addAction(tr("&Crop Mode"), this, &ImageViewer::crop);
    cropAct->setEnabled(false);
    cropAct->setCheckable(true);
    cropAct->setShortcut(tr("Ctrl+R"));

    paintAct = editMenu->addAction(tr("&Paint"), this, &ImageViewer::paint);
    paintAct->setShortcut(tr("Ctrl+P"));
    paintAct->setEnabled(false);

    undoAction = undoStack->createUndoAction(this, tr("&Undo"));
    undoAction->setShortcuts(QKeySequence::Undo);

    redoAction = undoStack->createRedoAction(this, tr("&Redo"));
    redoAction->setShortcuts(QKeySequence::Redo);
    editMenu->addAction(undoAction);
    editMenu->addAction(redoAction);


    QAction *pasteAct = editMenu->addAction(tr("&Paste"), this, &ImageViewer::paste);
    pasteAct->setShortcut(QKeySequence::Paste);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));

    zoomInAct = viewMenu->addAction(tr("Zoom &In (25%)"), this, &ImageViewer::zoomIn);
    zoomInAct->setShortcut(QKeySequence::ZoomIn);
    zoomInAct->setEnabled(false);

    zoomOutAct = viewMenu->addAction(tr("Zoom &Out (25%)"), this, &ImageViewer::zoomOut);
    zoomOutAct->setShortcut(QKeySequence::ZoomOut);
    zoomOutAct->setEnabled(false);

    normalSizeAct = viewMenu->addAction(tr("&Normal Size"), this, &ImageViewer::normalSize);
    normalSizeAct->setShortcut(tr("Ctrl+S"));
    normalSizeAct->setEnabled(false);

    viewMenu->addSeparator();

    fitToWindowAct = viewMenu->addAction(tr("&Fit to Window"), this, &ImageViewer::fitToWindow);
    fitToWindowAct->setEnabled(false);
    fitToWindowAct->setCheckable(true);
    fitToWindowAct->setShortcut(tr("Ctrl+F"));

    QMenu *filterMenu = menuBar()->addMenu(tr("&Filter"));
    brightnessAct = filterMenu->addAction(tr("Brightness"),this,&ImageViewer::showBrightnessEffect);
    brightnessAct->setShortcut(tr("Ctrl+B"));
    brightnessAct->setEnabled(false);

    histAct = filterMenu->addAction(tr("Histogram Equalization"), this, &ImageViewer::showHistogramEqualization);
    histAct->setShortcut(tr("Ctrl+H"));
    histAct->setEnabled(false);


    sepiaAct = filterMenu->addAction(tr("Sepia"), this, &ImageViewer::showSepia);
    sepiaAct->setShortcut(tr("Ctrl+A"));
    sepiaAct->setEnabled(false);

    QMenu *blurSection = filterMenu->addMenu(tr("&Blur"));
    blurHAct = blurSection->addAction(tr("Homogeneus Blur"),this,&ImageViewer::showHomogeneousEffect);
    blurHAct->setShortcut(tr("Ctrl+L"));
    blurHAct->setEnabled(false);

    blurGAct = blurSection->addAction(tr("Gaussian Blur"), this, &ImageViewer::showGaussianEffect);
    blurGAct->setShortcut(tr("Ctrl+G"));
    blurGAct->setEnabled(false);

    blurMAct = blurSection->addAction(tr("Median Blur"), this, &ImageViewer::showMedianEffect);
    blurMAct->setShortcut(tr("Ctrl+M"));
    blurMAct->setEnabled(false);

    blurBAct = blurSection->addAction(tr("Bilateral Blur"), this, &ImageViewer::showBilateralEffect);
    blurBAct->setShortcut(tr("Ctrl+T"));
    blurBAct->setEnabled(false);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));

    helpMenu->addAction(tr("&About"), this, &ImageViewer::about);
}

void ImageViewer::updateActions()
{
    saveAsAct->setEnabled(!image.isNull());
    copyAct->setEnabled(!image.isNull());
    brightnessAct->setEnabled(!image.isNull());
    sepiaAct->setEnabled(!image.isNull());
    histAct->setEnabled(!image.isNull());
    blurHAct->setEnabled(!image.isNull());
    blurGAct->setEnabled(!image.isNull());
    blurMAct->setEnabled(!image.isNull());
    blurBAct->setEnabled(!image.isNull());
    paintAct->setEnabled(!image.isNull());
    zoomInAct->setEnabled(!fitToWindowAct->isChecked());
    zoomOutAct->setEnabled(!fitToWindowAct->isChecked());
    normalSizeAct->setEnabled(!fitToWindowAct->isChecked());
    cropAct->setEnabled(!fitToWindowAct->isChecked());

    zoomInAct->setEnabled(!cropAct->isChecked() && !fitToWindowAct->isChecked());
    zoomOutAct->setEnabled(!cropAct->isChecked() && !fitToWindowAct->isChecked());
    fitToWindowAct->setEnabled(!cropAct->isChecked());
    normalSizeAct->setEnabled(!cropAct->isChecked() && !fitToWindowAct->isChecked());

}

void ImageViewer::scaleImage(double factor)

{
    scaleFactor *= factor;
    imageLabel->resize(scaleFactor * imageLabel->pixmap(Qt::ReturnByValue).size());

    adjustScrollBar(scrollArea->horizontalScrollBar(), factor);
    adjustScrollBar(scrollArea->verticalScrollBar(), factor);

    zoomInAct->setEnabled(scaleFactor < 3.0);
    zoomOutAct->setEnabled(scaleFactor > 0.333);
}

void ImageViewer::adjustScrollBar(QScrollBar *scrollBar, double factor)

{
    scrollBar->setValue(int(factor * scrollBar->value()
                            + ((factor - 1) * scrollBar->pageStep()/2)));
}

void ImageViewer::changeImage(QImage &newImage)
{
    w->imageAfter = newImage;
    emit imageChanged();
}


