#include "FrameExtractor.h"

#include <opencv2/opencv.hpp>
#include <imgui.h>

#include <filesystem>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <algorithm>

#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

namespace fs = std::filesystem;

// ======================
// SafeQueue
// ======================
template <typename T>
class SafeQueue {
    std::queue<T> q;
    std::mutex m;
public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(m);
        q.push(std::move(value));
    }
    bool pop(T& value) {
        std::lock_guard<std::mutex> lock(m);
        if (q.empty()) return false;
        value = std::move(q.front());
        q.pop();
        return true;
    }
    bool empty() {
        std::lock_guard<std::mutex> lock(m);
        return q.empty();
    }
};

// ======================
// File Dialog Helper
// ======================
static std::string openFileDialog() {
    char filename[MAX_PATH] = "";
    OPENFILENAME ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Video Files\0*.mp4;*.avi;*.mkv;*.mov\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    ofn.lpstrTitle = "Select Video File";
    return GetOpenFileName(&ofn) ? std::string(filename) : "";
}

// ======================
// State (static inside namespace)
// ======================
namespace FrameExtractor {
    static std::string videoPath;
    static std::string outputDir;

    static std::string buildDir = fs::current_path().string();
    static std::string projectRoot = fs::absolute(buildDir + "/../../..").string();
    static std::string pythonScript = (fs::path(projectRoot) / "remove_bg.py").string();

    static fs::path outputRoot = fs::path(buildDir);

    static SafeQueue<std::pair<int, cv::Mat>> frameQueue;
    static std::atomic<bool> extracting{ false };
    static std::atomic<bool> extractionDone{ false };
    static std::atomic<int> savedCount{ 0 };
    static std::atomic<int> totalFrames{ 0 };
    static std::atomic<double> fps{ 0.0 };
    static int savedFPS = 1;
    static cv::VideoCapture cap;

    void Draw() {
        // Ensure output root exists
        if (!fs::exists(outputRoot)) {
            std::error_code ec;
            fs::create_directories(outputRoot, ec);
            if (ec) {
                std::cerr << "[FrameExtractor] Failed to create output root: " << ec.message() << "\n";
            }
        }

        ImGui::Begin("Video Frame Extractor", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        if (ImGui::Button("Choose Video File")) {
            std::string chosen = openFileDialog();
            if (!chosen.empty()) {
                videoPath = chosen;
                outputDir = (outputRoot / fs::path(videoPath).stem()).string();
                std::error_code ec;
                fs::create_directories(outputDir, ec);
                if (ec) {
                    std::cerr << "[FrameExtractor] Failed to create output dir: " << ec.message() << "\n";
                }
                cap.open(videoPath);

                if (cap.isOpened()) {
                    fps = cap.get(cv::CAP_PROP_FPS);
                    totalFrames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
                }
                else {
                    std::cerr << "Error: Cannot open video.\n";
                }
            }
        }

        if (!videoPath.empty()) {
            ImGui::Text("Video: %s", videoPath.c_str());
            ImGui::Text("FPS: %.2f", fps.load());
            ImGui::Text("Total Frames: %d", totalFrames.load());
            ImGui::Text("Output Directory: %s", outputDir.c_str());
        }

        ImGui::Separator();

        ImGui::Text("Frames per second to save:");
        for (int i = 1; i <= 5; ++i) {
            char label[2]; snprintf(label, sizeof(label), "%d", i);
            if (ImGui::RadioButton(label, savedFPS == i)) savedFPS = i;
            ImGui::SameLine();
        }
        ImGui::NewLine();
        ImGui::Separator();

        if (!extracting && ImGui::Button("Start Extraction")) {
            if (cap.isOpened()) {
                extracting = true;
                extractionDone = false;
                savedCount = 0;

                // --- Thread 1: Frame extraction ---
                std::thread([]() {
                    cv::Mat frame;
                    int frameIndex = 0;
                    int savedIndex = 0;
                    int step = std::max(1, static_cast<int>(fps / savedFPS));
                    while (cap.read(frame)) {
                        if (frameIndex % step == 0) {
                            // clone frame to own memory for thread-safety
                            frameQueue.push({ savedIndex++, frame.clone() });
                        }
                        frameIndex++;
                    }
                    extractionDone = true;
                    cap.release();
                    }).detach();

                // --- Thread 2: Frame saving and post-processing ---
                std::thread([]() {
                    while (!extractionDone || !frameQueue.empty()) {
                        std::pair<int, cv::Mat> item;
                        if (frameQueue.pop(item)) {
                            std::string filename = (fs::path(outputDir) / ("frame_" + std::to_string(item.first) + ".png")).string();
                            cv::imwrite(filename, item.second);
                            savedCount++;
                        }
                        else {
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        }
                    }

                    // --- After saving all frames, run rembg batch (preserve original behavior) ---
                    if (!pythonScript.empty()) {
                        std::string command = "python \"" + pythonScript + "\" \"" + outputDir + "\"";
                        std::cout << "[FrameExtractor] Running: " << command << std::endl;
                        int rc = std::system(command.c_str());
                        if (rc != 0) {
                            std::cerr << "[FrameExtractor] Python script returned code: " << rc << std::endl;
                        }
                    }
                    else {
                        std::cerr << "[FrameExtractor] pythonScript path is empty, skipping post-processing\n";
                    }

                    extracting = false;
                    }).detach();
            }
        }

        if (extracting) {
            ImGui::Text("Extracting... saved %d frames", savedCount.load());
            // compute progress safely
            double denom = 1.0;
            double f = fps.load();
            if (f > 0.0 && totalFrames.load() > 0) {
                denom = (static_cast<double>(totalFrames.load()) / f) * static_cast<double>(savedFPS);
                if (denom <= 0.0) denom = 1.0;
            }
            float progress = static_cast<float>(std::min<double>(1.0, static_cast<double>(savedCount.load()) / denom));
            ImGui::ProgressBar(progress, ImVec2(300, 20));
        }
        else if (!videoPath.empty() && extractionDone) {
            ImGui::Text("Extraction complete! Total saved: %d", savedCount.load());
        }

 //       if (ImGui::Button("Exit")) {
   //         // if using GLFW, close current context window
     //       // caller must map this to actual window close if needed
       //     // example for GLFW:
         //   if (GLFWwindow* w = glfwGetCurrentContext()) {
           //     glfwSetWindowShouldClose(w, true);
           // }
        //}

        ImGui::End();
    }

    // Optional empty implementations (kept for API completeness)
    void Init() {}
    void Shutdown() {}
}
