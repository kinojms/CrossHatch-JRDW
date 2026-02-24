# AnitoCrosshatch: A 3D Editor Tool for Cross Hatch Rendered 3D Environments

<img src="https://lh3.googleusercontent.com/d/1pqBux0JVWd3ZCFdFZ2vBPN1Gue9xLHck" alt="AnitoCrosshatchLogo" width="200">
<br>
<img src="https://lh3.googleusercontent.com/d/1tqxoyqiiqj6jN5S9bheEnmuailcY-oWu" alt="AnitoCrosshatchLogo" width="400">

<b>Email: 

neil.delgallego@dlsu.edu.ph
</b>

<b>Socials: 

https://www.facebook.com/DLSUGAMELab

</b>

## DESCRIPTION
AnitoCrosshatch is a 3D editor tool designed to create and edit 3D environments rendered in a cross-hatch style. It provides a user-friendly interface for artists and developers to manipulate 3D models, textures, and lighting to achieve the desired aesthetic.

Our application can perform real-time rendering with the stylized shaders, and can produce a variety of different crosshatching styles with the available parameters of our crosshatching system.

## FEATURES
- **Cross Hatch Rendering**: Utilizes a custom shader to render 3D models with a cross-hatch style.
- **Model Import/Export**: Supports importing and exporting 3D models in various formats.
- **Scene Creation**: Allows users to create and manage 3D scenes with multiple objects.
- **Texture Management**: Allows users to apply and manage textures on 3D models.
- **Lighting Control**: Provides tools to adjust lighting conditions in the 3D environment.
- **User Interface**: Features an intuitive UI for easy navigation and manipulation of 3D objects.
- **Real-time Editing**: Enables real-time editing of 3D models and environments.
- **Crosshatch Shader**: Implements a custom shader that applies a cross-hatch effect to 3D models, enhancing the visual style.
- **Crosshatch Parameters**: Offers a range of parameters to customize the crosshatch effect, including line thickness, angle, and density.
- **Scene Management**: Allows users to create, save, and load scenes with multiple 3D objects.
- **Real-time Preview**: Provides a real-time preview of the cross-hatch rendering as changes are made.
- **Image Export**: Allows users to export rendered images of the 3D scenes with cross-hatch effects.
- **Sample Models**: Includes built-in 3D reconstruction samples (cube, pyramid, sphere) for quick prototyping.

## GALLERY
<img src="https://lh3.googleusercontent.com/d/1d5xtsXshLkUXKhrfJdCzLPpBAhXf1Jj0" alt="AnitoCrosshatchGallery1" width="700">
<img src="https://lh3.googleusercontent.com/d/1RUC_zcxj59OC6yxK4LLPzBImvr8U5j4i" alt="AnitoCrosshatchGallery2" width="700">
<img src="https://lh3.googleusercontent.com/d/1jz6zFNuqS7J2FBdShbV-0NoUdYeEUlMa" alt="AnitoCrosshatchGallery3" width="700">
<img src="https://lh3.googleusercontent.com/d/1bV-QmuvlffX11LuVGDYMu3yRtFZt3OOf" alt="AnitoCrosshatchGallery4" width="700">
<img src="https://lh3.googleusercontent.com/d/1f1YmFVKUfXxWtaNeDfiWOUKAZF3JPIi7" alt="AnitoCrosshatchGallery5" width="700">
<img src="https://lh3.googleusercontent.com/d/1ygrKi-orm5MzOszY1C52RS0wwYTWH79c" alt="AnitoCrosshatchGallery6" width="700">

## INSTALLER

Coming soom ;)

## REQUIREMENTS
- Windows 10 and above
- Visual Studio 2022 or later
- CMake 3.10 or later

## SETUP & BUILD

### Prerequisites

Ensure you have the following installed:
- **CMake 3.10 or later** ([download](https://cmake.org/download/))
- **Visual Studio 2022** with C++ development tools
- **Git** for cloning repositories

### Quick Start (Recommended)

**Windows:**
```powershell
# 1. Clone and setup dependencies
git clone <repo-url> CrossHatch-JRDW
cd CrossHatch-JRDW
.\setup.bat  # Run setup script (if available)

# 2. Build using CMake
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release

# 3. Run the application
.\run.bat
```

**macOS/Linux:**
```bash
# 1. Clone and setup dependencies
git clone <repo-url> CrossHatch-JRDW
cd CrossHatch-JRDW

# 2. Build using CMake
cmake -S . -B build
cmake --build build --config Release

# 3. Run the application
./run.sh
```

### Detailed Setup (Manual)

#### Option A: CLI Build (Recommended for Development)

```powershell
# Configure CMake project
cmake -S . -B build -G "Visual Studio 17 2022"

# Build in Release mode
cmake --build build --config Release

# Run from the build directory (important for assets/shaders)
cd build
.\bin\CrossHatchEditor.exe
cd ..
```

**Or simply use the provided run script:**
```powershell
.\run.bat
```

#### Option B: Visual Studio IDE

1. Open the project folder in Visual Studio 2022
2. Visual Studio will automatically detect CMake configuration
3. Select the desired configuration and click Build
4. Run directly from the IDE

> ⚠️ **Note:** Do not mix Option A and Option B. If you've used both, delete either `.\build` or `.\out` directory to avoid conflicts.

### Resolving Common Build Issues

#### CMake Toolchain File Error
```
"CMake toolchain file in the cache is different from the current toolchain file"
```
**Solution:**
```powershell
Remove-Item -Recurse -Force build
cmake -S . -B build
cmake --build build --config Release
```

#### Missing vcpkg
**Solution:**
```powershell
cd vcpkg
.\bootstrap-vcpkg.bat
cd ..
cmake -S . -B build
```

#### Shader Files Not Found at Runtime
**Solution:** Use the provided `run.bat` script which ensures the correct working directory, or always run the executable from the `build/` directory.

### Manual Dependency Setup (Advanced)

If the automatic setup doesn't work, manually clone these dependencies in the project root:

#### bgfx.cmake
```powershell
git clone https://github.com/bkaradzic/bgfx.cmake.git
cd bgfx.cmake
git submodule init
git submodule update
cmake -S. -Bcmake-build
cmake --build cmake-build
cd ..
```

#### glfw
```powershell
git clone https://github.com/glfw/glfw
cd glfw
cmake -S. -Bcmake-build
cmake --build cmake-build
cd ..
```

#### assimp
```powershell
git clone https://github.com/assimp/assimp.git
cd assimp
cmake CMakeLists.txt
cmake --build . --config Release
cd ..
```

#### ImGui
⚠️ **Note:** Current ImGui from GitHub is not compatible. Use the specific version from:
**[ImGui Archive](https://drive.google.com/file/d/1TZdXJ-motQRek31hDl7U2WD4qSkyYMCl/view?usp=sharing)**

Extract to project root as `imgui/` directory.

### imguizmo

- inside the root directory input the following commands:
```
git clone https://github.com/CedricGuillemet/ImGuizmo
```

### FFmpeg

- Download FFmpeg from https://github.com/GyanD/codexffmpeg/releases/download/7.1.1/ffmpeg-7.1.1-full_build-shared.7z
- Unzip the file and place it in the root directory of the project.
- rename the folder to <b>"ffmpeg"</b> so that the path is `AnitoCrosshatch/ffmpeg`."

## USING SAMPLE MODELS

AnitoCrosshatch includes built-in sample 3D models created via 3D reconstruction. These are perfect for learning and testing the editor:

### Quick Start with Samples

1. **Build and run the application** (follow build instructions above)
2. **Go to Menu → Add → Load Sample Models**
3. **Select a sample model:**
   - **sample_cube** - Simple cube for testing basic crosshatch effects
   - **sample_pyramid** - Pyramid model demonstrating faceted geometry
   - **sample_sphere** - Spherical model for curved surface testing

4. **Experiment with:**
   - Transform tools (translate, rotate, scale)
   - Crosshatch parameters (line thickness, angle, density)
   - Materials and textures
   - Lighting effects

### Sample Models Location

All sample models are stored in: `meshes/samples/`

To add your own sample models:
1. Place OBJ files in `meshes/samples/`
2. Rebuild the project
3. New models automatically appear in the "Load Sample Models" menu

See [meshes/samples/README.md](meshes/samples/README.md) for detailed information about sample models.

## BUILDING THE PROJECT

- Everything is handled by Cmake, so you just need to run the following commands in the root directory:
```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
- This will generate the Visual Studio solution files in the `build` directory and build the project in Release mode.
- You can also open Visual Studio with Cmake support and build the project from there. Make sure that the <b>CmakeLists.txt</b> highlighted in the Solution Explorer is the one in the root directory of the project.




## HOW TO PACKAGE AND CREATE AN INSTALLER

1. download nsis https://nsis.sourceforge.io/Download

2. open cmd in build folder

3. run the following command:
```
cmake --build . --config Release
cmake --install . --prefix ./package
cpack
```
4. this will create files in the package folder and create an installer for the app.

- if error occurs try using this command:
```
"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
```
