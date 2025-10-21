#include "FrameExtractor.h"

#include <opencv2/opencv.hpp>
#include <imgui.h>
#include <GLFW/glfw3.h>

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
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Video Files\0*.mp4;*.avi;*.mkv;*.mov\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    ofn.lpstrTitle = "Select Video File";
    return GetOpenFileNameA(&ofn) ? std::string(filename) : "";
}

// ======================
// FrameExtractor namespace
// ======================
namespace FrameExtractor {
    static std::string videoPath;
    static std::string outputDir;
    static std::string imagesDir;

    // Paths/commands for post-extraction automation
    static const char* kColmapScriptPath = "C:\\0_Thesis\\instant-ngp-rtx-3000\\scripts\\colmap2nerf.py";
    static const char* kInstantNgpCmd = "C:\\0_Thesis\\instant-ngp-rtx-3000\\instant-ngp.exe";

    static std::string buildDir = fs::current_path().string();
    static std::string projectRoot = fs::absolute(buildDir + "/../../..").string();
    static std::string pythonScript = (fs::path(projectRoot) / "remove_bg.py").string();
    static fs::path outputRoot = fs::path(buildDir);

    static SafeQueue<std::pair<int, cv::Mat>> frameQueue;
    static std::atomic<bool> extracting{ false };
    static std::atomic<bool> extractionDone{ false };
    static std::atomic<bool> removingBG{ false };
    static std::atomic<bool> removalDone{ false };
    static std::atomic<bool> showMethodChoice{ false };
    static std::atomic<bool> runningNeRF{ false };
    static std::atomic<bool> runningGaussian{ false };

    // OBJ watcher state
    static std::atomic<bool> watchingObj{ false };
    static std::atomic<bool> objFound{ false };
    static std::string foundObjPath;
    static std::atomic<bool> showImportPrompt{ false };
    
    // Import callback function pointer
    static std::function<void(const std::string&)> importCallback;
    
    // GLFW main window reference
    static GLFWwindow* mainWindow = nullptr;

    static std::atomic<int> savedCount{ 0 };
    static std::atomic<int> totalFrames{ 0 };
    static std::atomic<double> fps{ 0.0 };
    static int savedFPS = 1;
    static cv::VideoCapture cap;
    static std::atomic<int> originalTotalFrames{ 0 };
    
    // Timing and progress tracking
    static std::chrono::steady_clock::time_point nerfStartTime;
    static std::atomic<int> nerfProgress{ 0 };

    // ----------------------------
    // Helper: Run a shell command and stream output
    // ----------------------------
    static int runShellCommand(const std::string& command) {
        std::cout << "[Shell] " << command << std::endl;
        FILE* pipe = _popen(command.c_str(), "r");
        if (!pipe) {
            std::cerr << "[Shell] Failed to run command." << std::endl;
            return -1;
        }
        char buffer[512];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            std::cout << buffer;
        }
        int rc = _pclose(pipe);
        std::cout << "[Shell] Exit code: " << rc << std::endl;
        return rc;
    }

    // ----------------------------
    // Helper: Start background OBJ watcher
    // ----------------------------
    static void startObjWatcher() {
        if (watchingObj.load()) return;
        objFound.store(false);
        foundObjPath.clear();
        watchingObj.store(true);
        std::thread([]() {
            while (watchingObj.load() && !objFound.load()) {
                std::error_code ec;
                if (fs::exists(outputDir, ec)) {
                    for (fs::directory_iterator it(outputDir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
                        const auto& entry = *it;
                        if (entry.is_regular_file(ec) && entry.path().extension() == ".obj") {
                            foundObjPath = entry.path().string();
                            objFound.store(true);
                            watchingObj.store(false);
                            break;
                        }
                    }
                }
                if (!objFound.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(750));
                }
            }
        }).detach();
    }

    // ----------------------------
    // Helper: Format elapsed time
    // ----------------------------
    static std::string formatElapsedTime(std::chrono::steady_clock::time_point startTime) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime);
        int hours = elapsed.count() / 3600;
        int minutes = (elapsed.count() % 3600) / 60;
        int seconds = elapsed.count() % 60;
        
        if (hours > 0) {
            return std::to_string(hours) + "h " + std::to_string(minutes) + "m " + std::to_string(seconds) + "s";
        } else if (minutes > 0) {
            return std::to_string(minutes) + "m " + std::to_string(seconds) + "s";
        } else {
            return std::to_string(seconds) + "s";
        }
    }

    // ----------------------------
    // Helper: Hide main window using GLFW
    // ----------------------------
    static void hideMainWindow() {
        if (mainWindow) {
            glfwHideWindow(mainWindow);
            std::cout << "[FrameExtractor] Main window completely hidden using GLFW" << std::endl;
        } else {
            std::cout << "[FrameExtractor] Warning: Main window not set, cannot hide" << std::endl;
        }
    }
    
    // ----------------------------
    // Helper: Restore main window using GLFW
    // ----------------------------
    static void restoreMainWindow() {
        if (mainWindow) {
            glfwShowWindow(mainWindow);
            glfwFocusWindow(mainWindow);
            std::cout << "[FrameExtractor] Main window shown and focused using GLFW" << std::endl;
        } else {
            std::cout << "[FrameExtractor] Warning: Main window not set, cannot restore" << std::endl;
        }
    }

    // ----------------------------
    // Helper: Import OBJ mesh into editor
    // ----------------------------
    static void importMeshToEditor(const std::string& objPath) {
        if (importCallback) {
            std::cout << "[FrameExtractor] Calling import callback for: " << objPath << std::endl;
            importCallback(objPath);
        } else {
            std::cout << "[FrameExtractor] No import callback set. Please use File > Import OBJ in the editor to import: " << objPath << std::endl;
        }
    }

    // ----------------------------
    // NeRF Automation Function
    // ----------------------------
    static void runNeRFAutomation() {
        std::thread([]() {
            nerfStartTime = std::chrono::steady_clock::now();
            runningNeRF = true;
            nerfProgress = 0;
            startObjWatcher();
            
            std::cout << "[FrameExtractor] Starting COLMAP/NeRF automation..." << std::endl;
            std::string cdPrefix = std::string("cd /d \"") + outputDir + "\" && ";

            // 1) colmap2nerf.py with --images images
            nerfProgress = 25;
            {
                std::string cmd1 = cdPrefix + "python \"" + kColmapScriptPath + "\" --images images --run_colmap --overwrite";
                runShellCommand(cmd1);
            }

            // 2) colmap2nerf.py exhaustive matching with aabb scale
            nerfProgress = 50;
            {
                std::string cmd2 = cdPrefix + "python \"" + kColmapScriptPath + "\" --colmap_matcher exhaustive --run_colmap --aabb_scale 16 --overwrite";
                runShellCommand(cmd2);
            }

            // 3) instant-ngp on the video's folder (hide window first)
            nerfProgress = 75;
            {
                std::cout << "[FrameExtractor] Hiding main window before running instant-ngp..." << std::endl;
                hideMainWindow();
                std::string cmd3 = std::string(kInstantNgpCmd) + " \"" + outputDir + "\"";
                runShellCommand(cmd3);
                
                // Restore window after instant-ngp completes
                std::cout << "[FrameExtractor] Restoring main window after instant-ngp..." << std::endl;
                restoreMainWindow();
            }

            nerfProgress = 100;
            runningNeRF = false;
            std::cout << "[FrameExtractor] NeRF automation completed." << std::endl;
            
            // Show import prompt if OBJ was found during automation
            if (objFound.load()) {
                showImportPrompt.store(true);
            }
        }).detach();
    }

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
                imagesDir = (fs::path(outputDir) / "images").string();
                std::error_code ec;
                fs::create_directories(imagesDir, ec);
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
            ImGui::Text("Images Directory: %s", imagesDir.c_str());
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
                            std::string filename = (fs::path(imagesDir) / ("frame_" + std::to_string(item.first) + ".png")).string();
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
                    for (const auto& entry : fs::directory_iterator(imagesDir)) {
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
                    showMethodChoice = true;
                    removalDone = true;
                    extracting = false;
                    std::cout << "[FrameExtractor] All background removals done. Choose reconstruction method.\n";

                    }).detach(); // ✅ Properly close and detach the thread
            }
        }

        // --- Choose Reconstruction Method (NeRF or GS) ---
        if (showMethodChoice && !runningNeRF && !runningGaussian) {
            CenterLargeText("Choose Reconstruction Method");
            ImGui::Spacing();
            
            if (ImGui::Button("NeRF (Neural Radiance Fields)", ImVec2(300, 40))) {
                showMethodChoice = false;
                runNeRFAutomation();
            }
            ImGui::Spacing();
            if (ImGui::Button("Gaussian Splatting (Coming Soon)", ImVec2(300, 40))) {
				// TODO: Integrate another open source GS implementation
                ImGui::Text("Gaussian Splatting will be implemented next time.");
            }
        }

        // --- NeRF Progress Display ---
		// TODO: Fix NeRF progress tracking based on actual process output (similar to frame extraction progress tracker)
        if (runningNeRF) {
            std::string statusText = "Running NeRF reconstruction... " + std::to_string(nerfProgress.load()) + "%";
            CenterLargeText(statusText);
            
            std::string elapsedText = "Elapsed time: " + formatElapsedTime(nerfStartTime);
            ImGui::Text("%s", elapsedText.c_str());
            
            float progress = static_cast<float>(nerfProgress.load()) / 100.0f;
            CenterGreenProgressBar(progress, ImVec2(320, 24));
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
        else if (!videoPath.empty() && removalDone && !showMethodChoice && !runningNeRF && !runningGaussian) {
            CenterLargeText("All done!");
            ImGui::Text("Total frames saved: %d", savedCount.load());
        }

        // --- Import Prompt ---
        if (showImportPrompt.load()) {
            ImGui::OpenPopup("Import Mesh?");
        }
        if (ImGui::BeginPopupModal("Import Mesh?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Mesh reconstruction completed!");
            ImGui::Text("Mesh saved to: %s", foundObjPath.c_str());
            ImGui::Spacing();
            ImGui::Text("Do you want to import the reconstructed mesh?");
            ImGui::Spacing();

            if (ImGui::Button("Yes, Import Mesh", ImVec2(150, 0))) {
                importMeshToEditor(foundObjPath);
                showImportPrompt.store(false);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("No, Don't Import", ImVec2(150, 0))) {
                std::cout << "[FrameExtractor] User declined mesh import." << std::endl;
                showImportPrompt.store(false);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::End();
    } 

    void Init() {}
    void Shutdown() {}
    
    // Set the import callback function
    void SetImportCallback(std::function<void(const std::string&)> callback) {
        importCallback = callback;
    }
    
    // Set the main GLFW window reference
    void SetMainWindow(GLFWwindow* window) {
        mainWindow = window;
        std::cout << "[FrameExtractor] Main window reference set" << std::endl;
    }

} 
