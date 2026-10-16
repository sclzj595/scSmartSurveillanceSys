// qt界面入口
#include "MainWindow.h"
#include <QApplication>
#include <QMetaType>

int main(int argc, char *argv[])
{
    qRegisterMetaType<cv::Mat>("cv::Mat");         // 注册值类型
    qRegisterMetaType<cv::Mat>("cv::Mat&");        // 注册引用类型

    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}