#pragma once
#include <string>
#include <functional>

// Forward declaration for GLFW window
struct GLFWwindow;

namespace Reconstructor {
    void Draw();
    void Init();
    void Shutdown();
    void SetImportCallback(std::function<void(const std::string&)> callback);
    void SetMainWindow(GLFWwindow* window);
}
