#pragma once
#include <string>
#include <functional>

namespace FrameExtractor {
    void Draw();
    void Init();
    void Shutdown();
    void SetImportCallback(std::function<void(const std::string&)> callback);
}
