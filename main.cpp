/**
 * @file main.cpp
 * @brief MediaStudio 入口点 - Qt版本
 * 
 * 程序启动流程：
 * 1. 初始化COM（WASAPI音频播放需要）
 * 2. 创建QApplication（Qt应用实例）
 * 3. 创建MainWindow（主窗口）
 * 4. 可选：连接MySQL数据库
 * 5. 加载命令行指定的媒体文件
 * 6. 进入Qt事件循环（exec）
 * 
 * Qt事件循环 vs GLFW事件循环：
 * - GLFW：手动while循环 + glfwPollEvents()
 * - Qt：QApplication::exec() 自动管理事件循环
 * - Qt的优势：内置定时器、信号槽、布局系统、样式表
 * 
 * 使用方式：
 *   MediaStudio.exe <video_file1> [video_file2] ...
 *   MediaStudio.exe               （然后通过菜单打开文件）
 */

#include <QApplication>
#include <iostream>
#include <vector>
#include <string>
#include <windows.h>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    // --------------------------------------------------------
    // 初始化COM库（必须在QApplication之前）
    // Qt内部调用OleInitialize(NULL)即STA模式，必须与此一致
    // 否则Qt的QFileDialog等依赖STA COM的功能会死锁
    // WASAPI在STA模式下同样可以正常工作
    // --------------------------------------------------------
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != S_FALSE) {
        std::cerr << "COM初始化失败: " << std::hex << hr << std::endl;
        return 1;
    }

    // --------------------------------------------------------
    // 创建Qt应用实例
    // QApplication管理Qt事件循环、窗口系统、样式等
    // 必须在任何Qt控件创建之前创建
    // --------------------------------------------------------
    QApplication app(argc, argv);
    app.setApplicationName("MediaStudio");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("MediaStudio");

    // --------------------------------------------------------
    // 创建主窗口
    // --------------------------------------------------------
    MainWindow window;

    // --------------------------------------------------------
    // 解析命令行参数（可选的媒体文件路径）
    // --------------------------------------------------------
    std::vector<std::string> playlist;
    for (int i = 1; i < argc; i++) {
        playlist.push_back(argv[i]);
    }

    if (!playlist.empty()) {
        window.setInitialFiles(playlist);
    }

    // --------------------------------------------------------
    // 可选：连接MySQL数据库
    // 取消注释并填入密码即可连接
    //首次使用前需运行 sql/init_database.sql
    // --------------------------------------------------------
    // window.connectDatabase("127.0.0.1", "root", "your_password", "media_center");

    // --------------------------------------------------------
    // 显示窗口并进入Qt事件循环
    // exec()阻塞直到窗口关闭
    // --------------------------------------------------------
    window.show();

    int result = app.exec();

    // --------------------------------------------------------
    // 清理
    // --------------------------------------------------------
    CoUninitialize();
    return result;
}
