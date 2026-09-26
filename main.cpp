#include <exception>
#include <iostream>

#include "App.h"
#include "Console.h"
#include "Errors.h"

int main() {
    eats::ui::initConsole();
    try {
        eats::ui::App app;
        app.run();
    } catch (const eats::InputClosed& e) {
        std::cout << '\n' << e.what() << "，程序退出\n";
    } catch (const eats::BizError& e) {
        std::cout << "\n操作失败：" << e.what() << '\n';
    } catch (const std::exception& e) {
        std::cout << "\n程序异常：" << e.what() << '\n';
    }
    return 0;
}