#include "Reconstructor.h"

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
#include <fstream>
#include <algorithm>
#include <regex>
#include <numeric>
#include <map>
#include <memory>

// BGFX includes for texture handling
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/uint32_t.h>
#include "stb_image.h"

#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

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

static std::string openFolderDialog() {
    char folderPath[MAX_PATH] = "";
    BROWSEINFOA bi{};
    bi.lpszTitle = "Select Images Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl != nullptr) {
        SHGetPathFromIDListA(pidl, folderPath);
        CoTaskMemFree(pidl);
        return std::string(folderPath);
    }
    return "";
}

// ======================
// FrameExtractor namespace
// ======================
namespace Reconstructor {
    static std::string videoPath;
    static std::string outputDir;
    static std::string imagesDir;
    static std::string existingImagesDir;

    // Paths for post-extraction automation
    static std::string buildDir = fs::current_path().string();
    static std::string projectRoot = fs::absolute(buildDir + "/../../..").string();
    static std::string colmapScriptPath = (fs::path(projectRoot) / "pyngp" / "scripts" / "colmap2nerf.py").string();
    static const char* kNerfPipelineScript = "nerf_pipeline.py";
    static std::string pythonScript = (fs::path(projectRoot) / "remove_bg.py").string();
    static fs::path outputRoot = fs::path(buildDir);

    static SafeQueue<std::pair<int, cv::Mat>> frameQueue;
    static std::atomic<bool> extracting{ false };
    static std::atomic<bool> extractionDone{ false };
    static std::atomic<bool> removingBG{ false };
    static std::atomic<bool> removalDone{ false };
    static std::atomic<bool> showImageSelection{ false };
    static std::atomic<bool> showMethodChoice{ false };
    static std::atomic<bool> showStartChoice{ false };
    static std::atomic<bool> runningNeRF{ false };
    static std::atomic<bool> runningGaussian{ false };
    static std::atomic<bool> reconstructionComplete{ false };

    // OBJ watcher state
    static std::atomic<bool> watchingObj{ false };
    static std::atomic<bool> objFound{ false };
    static std::string foundObjPath;
    static std::atomic<bool> showImportPrompt{ false };
    
    // Import callback function pointer (shared with ModelGallery)
    std::function<void(const std::string&)> importCallback;
    
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
    static std::string nerfProgressFile{ "" };
    
}

// ======================
// FrameGallery namespace (based on existing Gallery implementation)
// ======================
namespace FrameGallery {
    static bool galleryOpen = false;
    static bool fullscreenOpen = false;
    static int selectedImage = -1;
    static std::vector<bgfx::TextureHandle> textures;
    static std::vector<ImVec2> imgSizes;
    static std::vector<std::string> imagePaths;
    static std::vector<bool> selectedImages;
    static int selectedCount = 0;
    
    // Call to load images from the images directory
    void LoadFrameGallery(const std::string& folderPath) {
        textures.clear();
        imgSizes.clear();
        imagePaths.clear();
        selectedImages.clear();
        selectedCount = 0;
        
        for (auto& entry : std::filesystem::directory_iterator(folderPath)) {
            if (!entry.is_regular_file()) continue;
            auto path = entry.path().string();
            std::string extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
            
            // Only load image files
            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg") {
                int w, h, channels;
                unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
                if (!data) continue;
                
                const bgfx::Memory* mem = bgfx::copy(data, w * h * 4);
                stbi_image_free(data);
                auto tex = bgfx::createTexture2D((uint16_t)w, (uint16_t)h, false, 1,
                    bgfx::TextureFormat::RGBA8, 0, mem);
                if (bgfx::isValid(tex)) {
                    textures.push_back(tex);
                    imgSizes.push_back(ImVec2((float)w, (float)h));
                    imagePaths.push_back(path);
                    selectedImages.push_back(false);
                }
            }
        }
        std::cout << "[FrameGallery] Loaded " << textures.size() << " images" << std::endl;
    }
    
    // Delete selected images
    void DeleteSelectedImages() {
        int deletedCount = 0;
        std::string parentPath;
        
        // Get the parent path before deletion
        if (!imagePaths.empty()) {
            parentPath = fs::path(imagePaths[0]).parent_path().string();
        }
        
        for (size_t i = 0; i < imagePaths.size(); ++i) {
            if (selectedImages[i]) {
                std::error_code ec;
                if (fs::remove(imagePaths[i], ec)) {
                    deletedCount++;
                    std::cout << "[FrameGallery] Deleted: " << fs::path(imagePaths[i]).filename().string() << std::endl;
                } else {
                    std::cerr << "[FrameGallery] Failed to delete: " << imagePaths[i] << " - " << ec.message() << std::endl;
                }
            }
        }
        std::cout << "[FrameGallery] Deleted " << deletedCount << " images" << std::endl;
        
        // Reload the gallery only if we have a valid parent path
        if (!parentPath.empty() && fs::exists(parentPath)) {
            LoadFrameGallery(parentPath);
        } else {
            std::cerr << "[FrameGallery] ERROR: Cannot reload gallery - parent path invalid or deleted" << std::endl;
            // Clear gallery data if path is invalid
            textures.clear();
            imgSizes.clear();
            imagePaths.clear();
            selectedImages.clear();
            selectedCount = 0;
        }
    }
    
    // Draw the frame gallery window
    void DrawFrameGallery() {
        if (!galleryOpen) return;
        
        ImGui::SetNextWindowSize(ImVec2(1000, 700), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Frame Gallery - Select Images to Delete", &galleryOpen, ImGuiWindowFlags_None)) {
            
            ImGui::Text("Total images: %d", (int)textures.size());
            ImGui::Text("Selected for deletion: %d", selectedCount);
            ImGui::Separator();
            
            // Control buttons
            if (ImGui::Button("Select All", ImVec2(100, 30))) {
                std::fill(selectedImages.begin(), selectedImages.end(), true);
                selectedCount = static_cast<int>(selectedImages.size());
            }
            ImGui::SameLine();
            if (ImGui::Button("Deselect All", ImVec2(100, 30))) {
                std::fill(selectedImages.begin(), selectedImages.end(), false);
                selectedCount = 0;
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh", ImVec2(100, 30))) {
                if (!imagePaths.empty()) {
                    LoadFrameGallery(fs::path(imagePaths[0]).parent_path().string());
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Selected", ImVec2(120, 30))) {
                if (selectedCount > 0) {
                    DeleteSelectedImages();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Continue to Reconstruction", ImVec2(200, 30))) {
                galleryOpen = false;
                Reconstructor::showImageSelection = false;
                Reconstructor::showMethodChoice = true;
            }
            
            ImGui::Separator();
            
            // Thumbnail grid
            ImGui::BeginChild("Thumbnails", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
            
            const float maxThumbHeight = 150.0f;
            const float padding = 10.0f;
            const int imagesPerRow = 6; // Fixed number of images per row for consistent grid
            
            // Calculate available width and determine thumbnail size
            float availableWidth = ImGui::GetContentRegionAvail().x;
            float thumbWidth = (availableWidth - (imagesPerRow - 1) * padding) / imagesPerRow;
            float thumbHeight = maxThumbHeight;
            
            // Ensure thumbnails don't get too small
            if (thumbWidth < 80.0f) {
                thumbWidth = 80.0f;
            }
            
            for (int i = 0; i < (int)textures.size(); i++) {
                // Start new row every imagesPerRow images
                if (i % imagesPerRow != 0) {
                    ImGui::SameLine();
                }
                
                ImGui::PushID(i);
                
                // Create a child window for each image to contain both image and text
                std::string childId = "img_container_" + std::to_string(i);
                ImGui::BeginChild(childId.c_str(), ImVec2(thumbWidth + padding, thumbHeight + 30), false, ImGuiWindowFlags_NoScrollbar);
                
                // Calculate actual thumbnail size maintaining aspect ratio
                ImVec2 original = imgSizes[i];
                float ratio = original.x / original.y;
                ImVec2 thumbSize;
                
                if (ratio > 1.0f) {
                    // Landscape image
                    thumbSize = ImVec2(thumbWidth, thumbWidth / ratio);
                } else {
                    // Portrait or square image
                    thumbSize = ImVec2(thumbHeight * ratio, thumbHeight);
                }
                
                // Selection border
                ImVec4 borderColor = selectedImages[i] ? 
                    ImVec4(1.0f, 0.0f, 0.0f, 1.0f) : // Red when selected
                    ImVec4(0.5f, 0.5f, 0.5f, 1.0f);  // Gray when not selected
                
                ImGui::PushStyleColor(ImGuiCol_Border, borderColor);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
                
                // Image button
                std::string buttonId = "img_" + std::to_string(i);
                if (ImGui::ImageButton(buttonId.c_str(), (ImTextureID)(uintptr_t)textures[i].idx, thumbSize, 
                                     ImVec2(0, 0), ImVec2(1, 1), 
                                     selectedImages[i] ? ImVec4(1.0f, 0.8f, 0.8f, 0.3f) : ImVec4(0, 0, 0, 0))) {
                    selectedImages[i] = !selectedImages[i];
                    selectedCount = static_cast<int>(std::count(selectedImages.begin(), selectedImages.end(), true));
                }
                
                // Right-click for fullscreen view
                if (ImGui::IsItemClicked(1)) { // Right click
                    selectedImage = i;
                    fullscreenOpen = true;
                }
                
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                
                // Filename below image
                std::string filename = fs::path(imagePaths[i]).filename().string();
                if (filename.length() > 15) {
                    filename = filename.substr(0, 12) + "...";
                }
                ImGui::Text("%s", filename.c_str());
                
                ImGui::EndChild();
                ImGui::PopID();
            }
            
            ImGui::EndChild();
        }
        ImGui::End();
        
        // Fullscreen image viewer
        if (fullscreenOpen && selectedImage >= 0 && selectedImage < (int)textures.size()) {
            ImGuiIO& io = ImGui::GetIO();
            ImVec2 viewport = io.DisplaySize;
            ImVec2 imgSize = imgSizes[selectedImage];
            
            // Compute scale to fit viewport
            float scale = 1.0f;
            if (imgSize.x > viewport.x || imgSize.y > viewport.y) {
                float sx = viewport.x / imgSize.x;
                float sy = viewport.y / imgSize.y;
                scale = (sx < sy) ? sx : sy;
            }
            ImVec2 displaySize(imgSize.x * scale, imgSize.y * scale);
            ImVec2 pos((viewport.x - displaySize.x) * 0.5f, (viewport.y - displaySize.y) * 0.5f);
            
            // Draw full-screen black background
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(viewport);
            ImGui::Begin("##BgFull", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(0, 0), viewport, IM_COL32(0, 0, 0, 255));
            ImGui::End();
            
            // Draw centered image window
            ImGui::SetNextWindowPos(pos);
            ImGui::SetNextWindowSize(displaySize);
            ImGui::Begin("##FullScreen", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs);
            ImGui::Image((ImTextureID)(uintptr_t)textures[selectedImage].idx, displaySize);
            ImGui::End();
            
            // Click or ESC to close
            if (ImGui::IsMouseClicked(0) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                fullscreenOpen = false;
            }
        }
    }
}

// Forward declare Reconstructor::importCallback
namespace Reconstructor {
    extern std::function<void(const std::string&)> importCallback;
}

// ======================
// ModelGallery namespace - Manages reconstructed 3D models
// ======================
namespace ModelGallery {
    static bool galleryOpen = false;
    static std::vector<std::string> modelPaths;
    static std::vector<std::string> modelNames;
    static std::vector<fs::file_time_type> modelTimestamps;
    static int selectedModel = -1;
    static std::string galleryDir;
    
    // Get or create the gallery directory
    static std::string GetGalleryDirectory() {
        if (galleryDir.empty()) {
            galleryDir = (fs::path(Reconstructor::buildDir) / "reconstructed_models").string();
            std::error_code ec;
            fs::create_directories(galleryDir, ec);
            if (ec) {
                std::cerr << "[ModelGallery] Failed to create gallery directory: " << ec.message() << std::endl;
            }
        }
        return galleryDir;
    }
    
    // Load all OBJ files from the gallery directory
    void LoadModelGallery() {
        modelPaths.clear();
        modelNames.clear();
        modelTimestamps.clear();
        selectedModel = -1;
        
        std::string galleryPath = GetGalleryDirectory();
        if (!fs::exists(galleryPath)) {
            std::error_code ec;
            fs::create_directories(galleryPath, ec);
            if (ec) {
                std::cerr << "[ModelGallery] Failed to create gallery directory: " << ec.message() << std::endl;
                return;
            }
        }
        
        for (auto& entry : fs::directory_iterator(galleryPath)) {
            if (!entry.is_regular_file()) continue;
            auto path = entry.path();
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
            
            // Only load OBJ files
            if (extension == ".obj") {
                modelPaths.push_back(path.string());
                modelNames.push_back(path.stem().string());
                
                // Get file timestamp for sorting
                try {
                    auto timestamp = fs::last_write_time(path);
                    modelTimestamps.push_back(timestamp);
                } catch (...) {
                    modelTimestamps.push_back(fs::file_time_type::min());
                }
            }
        }
        
        // Sort by timestamp (newest first)
        std::vector<size_t> indices(modelPaths.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
            return modelTimestamps[a] > modelTimestamps[b];
        });
        
        // Reorder vectors based on sorted indices
        std::vector<std::string> sortedPaths, sortedNames;
        std::vector<fs::file_time_type> sortedTimestamps;
        for (size_t idx : indices) {
            sortedPaths.push_back(modelPaths[idx]);
            sortedNames.push_back(modelNames[idx]);
            sortedTimestamps.push_back(modelTimestamps[idx]);
        }
        modelPaths = std::move(sortedPaths);
        modelNames = std::move(sortedNames);
        modelTimestamps = std::move(sortedTimestamps);
        
        std::cout << "[ModelGallery] Loaded " << modelPaths.size() << " models" << std::endl;
    }
    
    // Copy an OBJ file to the gallery directory
    void AddModelToGallery(const std::string& objPath) {
        if (!fs::exists(objPath)) {
            std::cerr << "[ModelGallery] Model file does not exist: " << objPath << std::endl;
            return;
        }
        
        std::string galleryPath = GetGalleryDirectory();
        fs::path sourcePath(objPath);
        fs::path destPath = fs::path(galleryPath) / sourcePath.filename();
        
        // If file with same name exists, add timestamp to avoid overwriting
        if (fs::exists(destPath)) {
            auto now = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count();
            std::string newName = sourcePath.stem().string() + "_" + 
                                  std::to_string(timestamp) + sourcePath.extension().string();
            destPath = fs::path(galleryPath) / newName;
        }
        
        try {
            fs::copy_file(sourcePath, destPath, fs::copy_options::overwrite_existing);
            std::cout << "[ModelGallery] Added model to gallery: " << destPath.filename().string() << std::endl;
            
            // Reload gallery to include the new model
            LoadModelGallery();
        } catch (const std::exception& e) {
            std::cerr << "[ModelGallery] Failed to copy model to gallery: " << e.what() << std::endl;
        }
    }
    
    // Delete selected models from gallery
    void DeleteSelectedModel(int index) {
        if (index < 0 || index >= (int)modelPaths.size()) {
            return;
        }
        
        std::error_code ec;
        if (fs::remove(modelPaths[index], ec)) {
            std::cout << "[ModelGallery] Deleted: " << fs::path(modelPaths[index]).filename().string() << std::endl;
            LoadModelGallery(); // Reload gallery
        } else {
            std::cerr << "[ModelGallery] Failed to delete: " << modelPaths[index] << " - " << ec.message() << std::endl;
        }
    }
    
    // Draw the model gallery window
    void DrawModelGallery() {
        if (!galleryOpen) return;
        
        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Reconstructed Models Gallery", &galleryOpen, ImGuiWindowFlags_None)) {
            
            ImGui::Text("Total models: %d", (int)modelPaths.size());
            ImGui::Separator();
            
            // Control buttons
            if (ImGui::Button("Refresh", ImVec2(100, 30))) {
                LoadModelGallery();
            }
            ImGui::SameLine();
            if (ImGui::Button("Open Gallery Folder", ImVec2(150, 30))) {
                std::string galleryPath = GetGalleryDirectory();
                std::string command = "explorer \"" + galleryPath + "\"";
                system(command.c_str());
            }
            ImGui::SameLine();
            
            // Import and Delete buttons (disabled if no model selected)
            bool hasSelection = (selectedModel >= 0 && selectedModel < (int)modelPaths.size());
            if (!hasSelection) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Import Selected", ImVec2(130, 30))) {
                if (Reconstructor::importCallback && hasSelection) {
                    Reconstructor::importCallback(modelPaths[selectedModel]);
                    std::cout << "[ModelGallery] Importing selected model: " << modelPaths[selectedModel] << std::endl;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete Selected", ImVec2(130, 30))) {
                if (hasSelection) {
                    DeleteSelectedModel(selectedModel);
                    selectedModel = -1;
                }
            }
            if (!hasSelection) {
                ImGui::EndDisabled();
            }
            
            ImGui::Separator();
            
            // Show selected model info
            if (hasSelection) {
                ImGui::Text("Selected: %s", modelNames[selectedModel].c_str());
            } else {
                ImGui::Text("No model selected");
            }
            ImGui::Separator();
            
            // Model list with details
            ImGui::BeginChild("ModelList", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
            
            if (modelPaths.empty()) {
                ImGui::Text("No models in gallery.");
                ImGui::Text("Reconstructed models will appear here automatically.");
            } else {
                for (int i = 0; i < (int)modelPaths.size(); i++) {
                    ImGui::PushID(i);
                    
                    // Create a selectable row for each model
                    bool isSelected = (selectedModel == i);
                    if (ImGui::Selectable(("##model_" + std::to_string(i)).c_str(), isSelected, 
                                         ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, 0))) {
                        selectedModel = i;
                    }
                    
                    // Model name
                    ImGui::SameLine(20);
                    ImGui::Text("%s", modelNames[i].c_str());
                    
                    // File path (truncated if too long)
                    ImGui::SameLine(200);
                    std::string displayPath = modelPaths[i];
                    if (displayPath.length() > 50) {
                        displayPath = "..." + displayPath.substr(displayPath.length() - 47);
                    }
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", displayPath.c_str());
                    
                    // File size
                    ImGui::SameLine(450);
                    try {
                        auto fileSize = fs::file_size(modelPaths[i]);
                        std::string sizeStr;
                        if (fileSize < 1024) {
                            sizeStr = std::to_string(fileSize) + " B";
                        } else if (fileSize < 1024 * 1024) {
                            sizeStr = std::to_string(fileSize / 1024) + " KB";
                        } else {
                            sizeStr = std::to_string(fileSize / (1024 * 1024)) + " MB";
                        }
                        ImGui::Text("%s", sizeStr.c_str());
                    } catch (...) {
                        ImGui::Text("?");
                    }
                    
                    ImGui::PopID();
                }
            }
            
            ImGui::EndChild();
        }
        ImGui::End();
    }
}

// ======================
// FrameExtractor namespace (continued)
// ======================
namespace Reconstructor {

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
    // NeRF (Instant-NGP) Automation
    // ----------------------------
    static void runNeRFAutomation() {
        std::thread([]() {
            nerfStartTime = std::chrono::steady_clock::now();
            runningNeRF = true;
            nerfProgress = 0;
            startObjWatcher();
            
            std::cout << "[FrameExtractor] Starting COLMAP/NeRF automation..." << std::endl;
            std::cout << "[FrameExtractor] Output directory: " << outputDir << std::endl;
            
            // Get the background-removed images directory
            std::string bgRemovedDir = (fs::path(outputDir) / "images" / "no_bg").string();
            
            if (!fs::exists(bgRemovedDir)) {
                std::cerr << "[FrameExtractor] ERROR: Background-removed images directory does not exist: " << bgRemovedDir << std::endl;
                runningNeRF = false;
                return;
            }
            
            // Step 1: Run COLMAP via colmap2nerf.py
            nerfProgress = 25;
            std::cout << "[FrameExtractor] Running COLMAP..." << std::endl;
            std::cout << "[FrameExtractor] Background-removed images directory: " << bgRemovedDir << std::endl;
            std::string colmapCmd = std::string("cd /d \"") + outputDir + "\" && python \"" + colmapScriptPath + "\" --images \"" + bgRemovedDir + "\" --run_colmap --overwrite";
            std::cout << "[FrameExtractor] COLMAP command: " << colmapCmd << std::endl;
            int colmapResult = runShellCommand(colmapCmd);
            
            if (colmapResult != 0) {
                std::cerr << "[FrameExtractor] ERROR: COLMAP step failed with exit code " << colmapResult << std::endl;
                runningNeRF = false;
                reconstructionComplete = true;
                return;
            }
            
            // Verify that transforms.json was created
            std::string transformsPath = (fs::path(outputDir) / "transforms.json").string();
            if (!fs::exists(transformsPath)) {
                std::cerr << "[FrameExtractor] ERROR: transforms.json was not created by COLMAP. Expected at: " << transformsPath << std::endl;
                std::cerr << "[FrameExtractor] This usually means COLMAP failed to generate camera poses." << std::endl;
                std::cerr << "[FrameExtractor] Check that:" << std::endl;
                std::cerr << "  1. Images exist in: " << bgRemovedDir << std::endl;
                std::cerr << "  2. Images have sufficient features for COLMAP to match" << std::endl;
                std::cerr << "  3. Camera parameters are in the dataset" << std::endl;
                runningNeRF = false;
                reconstructionComplete = true;
                return;
            }
            
            std::cout << "[FrameExtractor] COLMAP completed successfully. transforms.json found." << std::endl;
            nerfProgress = 50;
            
            // Step 2: Train NeRF model using Instant-NGP GUI
            nerfProgress = 60;
            std::cout << "[FrameExtractor] Starting NeRF training with Instant-NGP..." << std::endl;
            
            // Find and launch instant-ngp.exe
            std::string instantNgpExe = (fs::path(projectRoot) / "pyngp" / "instant-ngp.exe").string();
            
            if (!fs::exists(instantNgpExe)) {
                std::cerr << "[FrameExtractor] ERROR: instant-ngp.exe not found at: " << instantNgpExe << std::endl;
                runningNeRF = false;
                return;
            }
            
            std::cout << "[FrameExtractor] Launching instant-ngp.exe with dataset: " << outputDir << std::endl;
            
            // Launch Instant-NGP GUI application with the dataset folder
            // The application will open in a window and wait for user interaction to begin training
            // Try different argument formats in case the exe expects a specific format
            std::string ngpCmd = "\"" + instantNgpExe + "\" \"" + outputDir + "\"";
            std::cout << "[FrameExtractor] Command: " << ngpCmd << std::endl;
            
            // Use CreateProcess for better control and error reporting
            STARTUPINFOA si = {};
            PROCESS_INFORMATION pi = {};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_SHOW;
            
            // CreateProcessA requires a modifiable copy of the command line
            char cmdBuffer[1024];
            strcpy_s(cmdBuffer, sizeof(cmdBuffer), ngpCmd.c_str());
            
            // Get the pyngp directory as working directory (where the DLLs are)
            std::string pyngpDir = (fs::path(projectRoot) / "pyngp").string();
            
            BOOL success = CreateProcessA(
                NULL,                    // lpApplicationName
                cmdBuffer,               // lpCommandLine
                NULL,                    // lpProcessAttributes
                NULL,                    // lpThreadAttributes
                FALSE,                   // bInheritHandles
                CREATE_NEW_CONSOLE,      // dwCreationFlags
                NULL,                    // lpEnvironment
                pyngpDir.c_str(),        // lpCurrentDirectory - set to pyngp folder for DLL dependencies
                &si,                     // lpStartupInfo
                &pi                      // lpProcessInformation
            );
            
            if (success) {
                std::cout << "[FrameExtractor] Instant-NGP process launched successfully (PID: " << pi.dwProcessId << ")" << std::endl;
                
                // Wait for the process to complete
                DWORD waitResult = WaitForSingleObject(pi.hProcess, INFINITE);
                
                if (waitResult == WAIT_OBJECT_0) {
                    DWORD exitCode = 0;
                    if (GetExitCodeProcess(pi.hProcess, &exitCode)) {
                        std::cout << "[FrameExtractor] Instant-NGP exited with code: " << exitCode << std::endl;
                        
                        if (exitCode != 0) {
                            std::cout << "[FrameExtractor] Note: Instant-NGP may have exited due to a CLI argument error." << std::endl;
                            std::cout << "[FrameExtractor] The GUI window may have opened and closed. Check if transforms.json exists." << std::endl;
                            
                            // Check if transforms.json exists - if it does, the load was successful
                            std::string transformsPath = (fs::path(outputDir) / "transforms.json").string();
                            if (fs::exists(transformsPath)) {
                                std::cout << "[FrameExtractor] Dataset loaded successfully (transforms.json found)." << std::endl;
                            }
                        }
                    } else {
                        std::cout << "[FrameExtractor] Could not retrieve exit code." << std::endl;
                    }
                } else {
                    std::cerr << "[FrameExtractor] ERROR: WaitForSingleObject failed or timed out." << std::endl;
                }
                
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            } else {
                DWORD error = GetLastError();
                std::cerr << "[FrameExtractor] ERROR: Failed to launch instant-ngp.exe. Windows error code: " << error << std::endl;
                std::cerr << "[FrameExtractor] Command attempted: " << ngpCmd << std::endl;
                runningNeRF = false;
                return;
            }
            
            nerfProgress = 90;
            
            nerfProgress = 100;
            runningNeRF = false;
            reconstructionComplete = true;
            std::cout << "[FrameExtractor] NeRF automation completed." << std::endl;
            
            // Show import prompt if OBJ was found during automation
            if (objFound.load()) {
                ModelGallery::AddModelToGallery(foundObjPath);
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

        // Note: This function is called from within a child window in the sidebar,
        // so we don't create our own window here. The window is managed by CrossHatchEditor.cpp

        // --- Start Choice UI ---
        if (!showStartChoice && !extracting && !removalDone && !showImageSelection && !showMethodChoice && !runningNeRF && !runningGaussian && !reconstructionComplete) {
            float avail_width = ImGui::GetContentRegionAvail().x;
            float button_width = std::min(avail_width - 10.0f, 300.0f); // Responsive but max 300px
            
            ImGui::Text("Choose Input Method");
            ImGui::Spacing();
            ImGui::Text("How would you like to start the reconstruction?");
            ImGui::Spacing();
            
            if (ImGui::Button("Extract Frames from Video", ImVec2(button_width, 50))) {
                showStartChoice = true;
            }
            ImGui::Spacing();
            if (ImGui::Button("Use Existing Image Dataset", ImVec2(button_width, 50))) {
                std::string chosen = openFolderDialog();
                if (!chosen.empty()) {
                    existingImagesDir = chosen;
                    imagesDir = existingImagesDir;
                    outputDir = (fs::path(existingImagesDir).parent_path() / "reconstruction").string();
                    std::error_code ec;
                    fs::create_directories(outputDir, ec);
                    
                    // Load existing images into frame gallery
                    FrameGallery::LoadFrameGallery(existingImagesDir);
                    
                    // Check if any images were loaded
                    if (FrameGallery::textures.empty()) {
                        std::cerr << "[FrameExtractor] No images found in selected folder: " << existingImagesDir << std::endl;
                        // Reset the selection
                        existingImagesDir.clear();
                        imagesDir.clear();
                    } else {
                        showImageSelection = true;
                    }
                }
            }
            ImGui::Spacing();
            if (ImGui::Button("Open Model Gallery", ImVec2(button_width, 50))) {
                ModelGallery::galleryOpen = true;
                ModelGallery::LoadModelGallery();
            }
        }

        // --- Video File Picker (only shown when extracting from video) ---
        if (showStartChoice && !extracting && !removalDone && !showImageSelection && !showMethodChoice && !runningNeRF && !runningGaussian && !reconstructionComplete) {
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
            if (ImGui::Button("Start Extraction")) {
                if (cap.isOpened()) {
                    extracting = true;
                    extractionDone = false;
                    removalDone = false;
                    savedCount = 0;

                    // Use Instant-NGP's preprocessing script to extract frames and remove backgrounds
                    std::thread([]() {
                        extracting = true;
                        extractionDone = false;
                        removalDone = false;
                        removingBG = false;
                        savedCount = 0;

                        // Pre-compute expected total frames so UI progress has a valid denominator
                        int expectedFrames = 0;
                        try {
                            if (!videoPath.empty() && cap.isOpened() && originalTotalFrames.load() > 0 && fps.load() > 0.0) {
                                double ratio = fps.load() / static_cast<double>(savedFPS);
                                expectedFrames = static_cast<int>(originalTotalFrames.load() / std::max(1.0, ratio)) + 1;
                                totalFrames = expectedFrames;
                                std::cout << "[FrameExtractor] Expected frames (video): " << expectedFrames << std::endl;
                            } else if (!existingImagesDir.empty()) {
                                int count = 0;
                                for (auto& e : fs::directory_iterator(existingImagesDir)) {
                                    if (!e.is_regular_file()) continue;
                                    std::string ext = e.path().extension().string();
                                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") ++count;
                                }
                                totalFrames = count;
                                expectedFrames = count;
                                std::cout << "[FrameExtractor] Expected frames (existing folder): " << count << std::endl;
                            }
                        } catch (...) {
                            // ignore and proceed, totalFrames may be set later by parser
                        }

                        std::cout << "[FrameExtractor] Running Instant-NGP preprocessor (colmap2nerf.py)..." << std::endl;

                        // Build command to extract frames and remove background. This uses the pyngp/scripts/colmap2nerf.py script.
                        std::string cmd;
                        if (!videoPath.empty() && cap.isOpened()) {
                            cmd = "python \"" + colmapScriptPath + "\" --video_in \"" + videoPath + "\" --video_fps " + std::to_string(savedFPS) + " --remove_background --preprocess_only --overwrite --images \"" + imagesDir + "\" 2>&1";
                        } else if (!existingImagesDir.empty()) {
                            cmd = "python \"" + colmapScriptPath + "\" --images \"" + existingImagesDir + "\" --remove_background --preprocess_only --overwrite 2>&1";
                        } else {
                            std::cerr << "[FrameExtractor] No video or existing images found for preprocessing." << std::endl;
                            extracting = false;
                            removalDone = true;
                            return;
                        }

                        std::cout << "[FrameExtractor] Command: " << cmd << std::endl;

                        FILE* pipe = _popen(cmd.c_str(), "r");
                        if (pipe) {
                            char buffer[1024];
                            int frameCount = 0;
                            int removedCount = 0;
                            bool isRemovalPhase = false;

                            std::regex frameRe("frame=\\s*([0-9]+)");
                            // Match explicit processed/saved indicators or rembg mentions
                            std::regex processedRe("(Processed|processed|Saved|saved|rembg|Removing background|removing background)");
                            // Match concrete saved filenames like "0001.png" (avoid matching template strings like %04d.png)
                            std::regex savedFileRe("(\\d{1,6}\\.png)");
                            std::smatch m;

                            while (fgets(buffer, sizeof(buffer), pipe)) {
                                std::string line(buffer);
                                std::cout << "[colmap2nerf] " << line;

                                // If we haven't entered removal phase yet, try to extract ffmpeg frame count
                                if (!isRemovalPhase) {
                                    if (std::regex_search(line, m, frameRe) && m.size() > 1) {
                                        try {
                                            frameCount = std::stoi(m[1].str());
                                            savedCount = frameCount;
                                            std::cout << "[FrameExtractor][DEBUG] parsed frameCount=" << frameCount
                                                      << " totalFrames(current)=" << totalFrames.load()
                                                      << " isRemovalPhase=" << isRemovalPhase << std::endl;
                                        } catch (...) {}
                                    }

                                    // Heuristic: require explicit processed/saved keywords OR a concrete saved filename
                                    bool hasProcessedKeyword = std::regex_search(line, m, processedRe);
                                    bool hasConcreteSavedFile = (line.find(".png") != std::string::npos && line.find("%") == std::string::npos && std::regex_search(line, m, savedFileRe));

                                    // Avoid false positive from ffmpeg header lines that include the template "%04d.png" or "Output #0"
                                    bool looksLikeFfmpegTemplate = (line.find("%04d") != std::string::npos) || (line.find("Output #0") != std::string::npos);

                                    if ((hasProcessedKeyword || hasConcreteSavedFile) && !looksLikeFfmpegTemplate) {
                                        isRemovalPhase = true;
                                        removingBG = true;
                                        if (totalFrames.load() == 0) {
                                            totalFrames = frameCount > 0 ? frameCount : totalFrames.load();
                                        }
                                        removedCount = 0;
                                        savedCount = 0;
                                        std::cout << "[FrameExtractor] Detected removal phase. totalFrames=" << totalFrames.load() << std::endl;
                                    }
                                } else {
                                    // Removal phase: increment removed count when we see processed/saved or image filename
                                    if (std::regex_search(line, m, processedRe) || line.find(".png") != std::string::npos || line.find("Saved") != std::string::npos || line.find("saved") != std::string::npos) {
                                        removedCount++;
                                        savedCount = removedCount;
                                        std::cout << "[FrameExtractor][DEBUG] removal progress removedCount=" << removedCount
                                                  << " / " << totalFrames.load() << std::endl;
                                    }
                                }
                            }
                            _pclose(pipe);
                        } else {
                            std::cerr << "[FrameExtractor] Failed to start colmap2nerf.py" << std::endl;
                        }

                        // After preprocessing completes, prefer the background-removed images if present
                        removingBG = false;
                        extracting = false;
                        showImageSelection = true;
                        removalDone = true;
                        std::cout << "[FrameExtractor] Preprocessing complete. Extracted: " << savedCount.load() << " frames. Loading images for selection.\n";

                        try {
                            fs::path imagesPath(imagesDir);
                            fs::path bgDir = imagesPath / "no_bg";
                            if (fs::exists(bgDir) && fs::is_directory(bgDir)) {
                                std::cout << "[FrameExtractor] Found background-removed folder: " << bgDir.string() << std::endl;
                                imagesDir = bgDir.string();
                            } else {
                                std::cout << "[FrameExtractor] No background-removed folder found; using: " << imagesDir << std::endl;
                            }
                        } catch (const std::exception& e) {
                            std::cerr << "[FrameExtractor] Warning checking no_bg folder: " << e.what() << std::endl;
                        }

                        FrameGallery::LoadFrameGallery(imagesDir);
                    }).detach();
                }
            }
            
            // Back button to return to start choice
            ImGui::Spacing();
            float avail_width = ImGui::GetContentRegionAvail().x;
            if (ImGui::Button("Back to Start", ImVec2(std::min(avail_width - 10.0f, 150.0f), 30))) {
                showStartChoice = false;
                videoPath.clear();
                cap.release();
            }
        }

        // --- Image Selection UI ---
        if (showImageSelection && !showMethodChoice && !runningNeRF && !runningGaussian) {
            if (removalDone) {
                CenterLargeText("Frame Extraction Complete!");
                ImGui::Spacing();
                ImGui::Text("Total images extracted: %d", (int)FrameGallery::textures.size());
            } else {
                CenterLargeText("Image Dataset Loaded!");
                ImGui::Spacing();
                ImGui::Text("Total images found: %d", (int)FrameGallery::textures.size());
                if (!existingImagesDir.empty()) {
                    ImGui::Text("Dataset folder: %s", existingImagesDir.c_str());
                }
            }
            ImGui::Spacing();
            ImGui::Text("You can now review and select images to delete before reconstruction.");
            ImGui::Spacing();
            
            float avail_width = ImGui::GetContentRegionAvail().x;
            float button_width = std::min(avail_width - 10.0f, 200.0f); // Responsive but max 200px
            
            if (ImGui::Button("Select Images", ImVec2(button_width, 50))) {
                FrameGallery::galleryOpen = true;
            }
            ImGui::Spacing();
            if (ImGui::Button("Skip Image Selection", ImVec2(button_width, 50))) {
                showImageSelection = false;
                showMethodChoice = true;
            }
            
            // Back button to return to start choice
            ImGui::Spacing();
            if (ImGui::Button("Back to Start", ImVec2(std::min(avail_width - 10.0f, 150.0f), 30))) {
                showImageSelection = false;
                showStartChoice = false;
                videoPath.clear();
                existingImagesDir.clear();
                cap.release();
                
                // Clear the frame gallery
                FrameGallery::textures.clear();
                FrameGallery::imgSizes.clear();
                FrameGallery::imagePaths.clear();
                FrameGallery::selectedImages.clear();
                FrameGallery::selectedCount = 0;
                FrameGallery::galleryOpen = false;
                FrameGallery::fullscreenOpen = false;
                FrameGallery::selectedImage = -1;
            }
        }

        // --- Choose Reconstruction Method (NeRF or GS) ---
        if (showMethodChoice && !runningNeRF && !runningGaussian) {
            float avail_width = ImGui::GetContentRegionAvail().x;
            float button_width = std::min(avail_width - 10.0f, 300.0f); // Responsive but max 300px
            
            ImGui::Text("Choose Reconstruction Method");
            ImGui::Spacing();
            
            if (ImGui::Button("NeRF (Neural Radiance Fields)", ImVec2(button_width, 40))) {
                showMethodChoice = false;
                runNeRFAutomation();
            }
            ImGui::Spacing();
            if (ImGui::Button("Gaussian Splatting (Coming Soon)", ImVec2(button_width, 40))) {
				// TODO: Integrate another open source GS implementation
                ImGui::Text("Gaussian Splatting will be implemented next time.");
            }
        }

        // --- NeRF Progress Display ---
        if (runningNeRF) {
            // Try to read progress from file
            if (!nerfProgressFile.empty() && fs::exists(nerfProgressFile)) {
                try {
                    std::ifstream progressFile(nerfProgressFile);
                    std::string content((std::istreambuf_iterator<char>(progressFile)),
                                       std::istreambuf_iterator<char>());
                    
                    // Simple JSON parsing: extract "progress" value
                    size_t progPos = content.find("\"progress\"");
                    if (progPos != std::string::npos) {
                        size_t colonPos = content.find(":", progPos);
                        size_t commaPos = content.find(",", colonPos);
                        if (commaPos == std::string::npos) {
                            commaPos = content.find("}", colonPos);
                        }
                        if (colonPos != std::string::npos && commaPos != std::string::npos) {
                            std::string progressStr = content.substr(colonPos + 1, commaPos - colonPos - 1);
                            progressStr.erase(0, progressStr.find_first_not_of(" \t\n\r"));
                            int fileProgress = std::stoi(progressStr);
                            nerfProgress = fileProgress;
                        }
                    }
                } catch (...) {
                    // Silently continue with current progress
                }
            }
            
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

            std::string statusText = removingBG
                ? "Removing background... " + std::to_string(savedCount.load()) + " / " + std::to_string(totalFrames.load()) + " frames"
                : "Extracting frames... " + std::to_string(savedCount.load()) + " frames";

            CenterLargeText(statusText);

            if (removingBG)
                CenterGreenProgressBar(removalProgress, ImVec2(320, 24));
            else
                CenterGreenProgressBar(frameProgress, ImVec2(320, 24));
        }
        else if (!videoPath.empty() && removalDone && !showImageSelection && !showMethodChoice && !runningNeRF && !runningGaussian && !reconstructionComplete) {
            CenterLargeText("All done!");
            ImGui::Text("Total frames saved: %d", savedCount.load());
        }
        else if (reconstructionComplete) {
            CenterLargeText("Reconstruction Complete!");
            ImGui::Text("Total frames processed: %d", savedCount.load());
            ImGui::Spacing();
            ImGui::Text("The reconstruction process has finished successfully.");
            ImGui::Spacing();
            
            float avail_width = ImGui::GetContentRegionAvail().x;
            float button_width = std::min(avail_width - 10.0f, 200.0f); // Responsive but max 200px
            
            if (ImGui::Button("Reconstruct Again", ImVec2(button_width, 50))) {
                // Reset after reconstruction is complete
                extracting = false;
                extractionDone = false;
                removingBG = false;
                removalDone = false;
                showImageSelection = false;
                showMethodChoice = false;
                showStartChoice = false;
                runningNeRF = false;
                runningGaussian = false;
                reconstructionComplete = false;
                watchingObj = false;
                objFound = false;
                showImportPrompt = false;
                savedCount = 0;
                totalFrames = 0;
                nerfProgress = 0;
                
                // Clear paths
                videoPath.clear();
                existingImagesDir.clear();
                cap.release();
                
                // Clear the frame gallery
                FrameGallery::textures.clear();
                FrameGallery::imgSizes.clear();
                FrameGallery::imagePaths.clear();
                FrameGallery::selectedImages.clear();
                FrameGallery::selectedCount = 0;
                FrameGallery::galleryOpen = false;
                FrameGallery::fullscreenOpen = false;
                FrameGallery::selectedImage = -1;
                
                std::cout << "[FrameExtractor] Reset for new reconstruction" << std::endl;
            }
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
            ImGui::Spacing();
            ImGui::Text("The model has been saved to the gallery. You can access it later from the Model Gallery.");
            if (ImGui::Button("Open Model Gallery", ImVec2(200, 0))) {
                ModelGallery::galleryOpen = true;
                ModelGallery::LoadModelGallery();
                showImportPrompt.store(false);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        // Draw the frame gallery if it should be shown (separate window)
        FrameGallery::DrawFrameGallery();
        
        // Draw the model gallery if it should be shown (separate window)
        ModelGallery::DrawModelGallery();
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
