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
// FrameExtractor namespace
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
    static std::atomic<bool> removingBG{ false };
    static std::atomic<bool> removalDone{ false };

    static std::atomic<int> savedCount{ 0 };
    static std::atomic<int> totalFrames{ 0 };
    static std::atomic<double> fps{ 0.0 };
    static int savedFPS = 1;
    static cv::VideoCapture cap;
    static std::atomic<int> originalTotalFrames{ 0 };

    // ----------------------------
    // Helper: Centered large text
    // ----------------------------
    static void CenterLargeText(const std::string& text) {
        float windowWidth = ImGui::GetWindowSize().x;
        ImGui::SetCursorPosX((windowWidth - ImGui::CalcTextSize(text.c_str()).x) * 0.5f);
        ImGui::PushFont(ImGui::GetFont()); // keep current, but can use custom large font if registered
        ImGui::SetWindowFontScale(1.3f);   // enlarge text temporarily
        ImGui::TextUnformatted(text.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopFont();
    }

    // ----------------------------
    // Helper: Centered progress bar (green)
    // ----------------------------
    static void CenterGreenProgressBar(float fraction, const ImVec2& size) {
        float windowWidth = ImGui::GetWindowSize().x;
        ImGui::SetCursorPosX((windowWidth - size.x) * 0.5f);

        ImVec4 green = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
        ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, green);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, bg);
        ImGui::ProgressBar(fraction, size);
        ImGui::PopStyleColor(2);
    }

    // ----------------------------
    // Main Draw function
    // ----------------------------
    void Draw() {
        // Ensure output directory exists
        if (!fs::exists(outputRoot)) {
            std::error_code ec;
            fs::create_directories(outputRoot, ec);
            if (ec) {
                std::cerr << "[FrameExtractor] Failed to create output root: " << ec.message() << "\n";
            }
        }

        ImGui::Begin("Video Frame Extractor", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        // --- File Picker ---
        if (ImGui::Button("Choose Video File")) {
            std::string chosen = openFileDialog();
            if (!chosen.empty()) {
                videoPath = chosen;
                outputDir = (outputRoot / fs::path(videoPath).stem()).string();
                std::error_code ec;
                fs::create_directories(outputDir, ec);
                cap.open(videoPath);

                if (cap.isOpened()) {
                    fps = cap.get(cv::CAP_PROP_FPS);
                    originalTotalFrames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
                }
                else {
                    std::cerr << "Error: Cannot open video.\n";
                }
            }
        }

        if (!videoPath.empty()) {
            ImGui::Text("Video: %s", videoPath.c_str());
            ImGui::Text("FPS: %.2f", fps.load());
            ImGui::Text("Total Frames: %d", originalTotalFrames.load());
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

        // --- Start extraction ---
        if (!extracting && ImGui::Button("Start Extraction")) {
            if (cap.isOpened()) {
                extracting = true;
                extractionDone = false;
                removalDone = false;
                savedCount = 0;

                // --- Thread 1: Frame extraction ---
                std::thread([]() {
                    cv::Mat frame;
                    int frameIndex = 0;
                    int savedIndex = 0;
                    int step = std::max(1, static_cast<int>(fps / savedFPS));

                    while (cap.read(frame)) {
                        if (frameIndex % step == 0) {
                            frameQueue.push({ savedIndex++, frame.clone() });
                        }
                        frameIndex++;
                    }
                    extractionDone = true;
                    cap.release();
                    }).detach();

                // --- Thread 2: Frame saving + background removal ---
                std::thread([]() {
                    int totalExpectedFrames = static_cast<int>((originalTotalFrames / (fps / savedFPS)) + 1);

                    while (!extractionDone || !frameQueue.empty()) {
                        std::pair<int, cv::Mat> item;
                        if (frameQueue.pop(item)) {
                            std::string filename = (fs::path(outputDir) / ("frame_" + std::to_string(item.first) + ".png")).string();
                            cv::imwrite(filename, item.second);
                            savedCount++;

                            // print progress
                            std::cout << "[FrameExtractor] Extracting " << savedCount.load()
                                << " / " << totalExpectedFrames << " frames" << std::endl;
                        }
                        else {
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        }
                    }

                    // --- Run background removal ---
                    totalFrames.store(savedCount);
                    // After extractionDone = true; and cap.release();
                    removingBG = true;
                    std::cout << "[FrameExtractor] Starting parallel background removal..." << std::endl;

                    // Gather all frame files
                    std::vector<fs::path> frameFiles;
                    for (const auto& entry : fs::directory_iterator(outputDir)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".png") {
                            frameFiles.push_back(entry.path());
                        }
                    }

                    int totalFiles = static_cast<int>(frameFiles.size());
                    if (totalFiles == 0) {
                        std::cerr << "[FrameExtractor] No frames found for background removal.\n";
                        removingBG = false;
                        removalDone = true;
                        extracting = false;
                        return;
                    }

                    savedCount.store(0);
                    totalFrames.store(totalFiles);

                    int numThreads = std::min(5, totalFiles);
                    std::atomic<int> activeThreads = 0;
                    std::mutex coutMutex;

                    auto worker = [&](int id, int startIndex, int step) {
                        activeThreads++;
                        for (int i = startIndex; i < totalFiles; i += step) {
                            const auto& file = frameFiles[i];
                            std::string command = "python \"" + pythonScript + "\" \"" + outputDir + "\" \"" + file.string() + "\"";

                            std::cout << command;
                            {
                                std::lock_guard<std::mutex> lock(coutMutex);
                                std::cout << "[Thread " << id << "] Running: " << command << std::endl;
                            }

                            FILE* pipe = _popen(command.c_str(), "r");
                            if (pipe) {
                                char buffer[256];
                                while (fgets(buffer, sizeof(buffer), pipe)) {
                                    std::string line(buffer);
                                    std::lock_guard<std::mutex> lock(coutMutex);
                                    std::cout << "[Python T" << id << "] " << line;

                                    if (line.rfind("PROGRESS", 0) == 0) {
                                       // int done = 0, total = 0;
                                       // if (sscanf(line.c_str(), "PROGRESS %d/%d", &done, &total) == 2) {
                                            savedCount.fetch_add(1);
                                        // }
                                    }
                                    else if (line.rfind("Processed", 0) == 0) {
                                        savedCount.fetch_add(1);
                                    }
                                }
                                _pclose(pipe);
                            }
                            else {
                                std::lock_guard<std::mutex> lock(coutMutex);
                                std::cerr << "[Thread " << id << "] Failed to run script for " << file.filename() << std::endl;
                            }
                        }
                        activeThreads--;
                        };

                    // Launch threads
                    std::vector<std::thread> threads;
                    for (int i = 0; i < numThreads; ++i) {
                        threads.emplace_back(worker, i + 1, i, numThreads);
                    }

                    // Wait for all to complete
                    for (auto& t : threads) {
                        if (t.joinable()) t.join();
                    }

                    removingBG = false;
                    removalDone = true;
                    extracting = false;
                    std::cout << "[FrameExtractor] All background removals done.\n";

                    }).detach(); // ✅ Properly close and detach the thread
            }
        }

        // --- Progress Display ---
        if (extracting || removingBG) {
            int totalExpectedFrames = static_cast<int>(( originalTotalFrames / (fps / savedFPS)) + 1); 
            float frameProgress = static_cast<float>(savedCount.load()) / std::max(1, totalExpectedFrames);
            float removalProgress = static_cast<float>(savedCount.load()) / std::max(1, totalFrames.load());

            // Large centered text with count
            std::string statusText = removingBG
                ? "Removing background... " + std::to_string(savedCount.load()) + " / " + std::to_string(totalFrames.load()) + " frames"
                : "Extracting frames... " + std::to_string(savedCount.load()) + " frames";

            CenterLargeText(statusText);

            if (removingBG)
                CenterGreenProgressBar(removalProgress, ImVec2(320, 24));
            else
                CenterGreenProgressBar(frameProgress, ImVec2(320, 24));
        }
        else if (!videoPath.empty() && removalDone) {
            CenterLargeText("All done!");
            ImGui::Text("Total frames saved: %d", savedCount.load());
        }

        ImGui::End();
    } 

    void Init() {}
    void Shutdown() {}

} 
