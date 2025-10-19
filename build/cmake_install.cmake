# Install script for directory: C:/CrossHatch-JRDW

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files/CrossHatchEditor")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/." TYPE EXECUTABLE FILES "C:/CrossHatch-JRDW/build/Debug/CrossHatchEditor.exe")
  elseif("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/." TYPE EXECUTABLE FILES "C:/CrossHatch-JRDW/build/Release/CrossHatchEditor.exe")
  elseif("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/." TYPE EXECUTABLE FILES "C:/CrossHatch-JRDW/build/MinSizeRel/CrossHatchEditor.exe")
  elseif("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/." TYPE EXECUTABLE FILES "C:/CrossHatch-JRDW/build/RelWithDebInfo/CrossHatchEditor.exe")
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/." TYPE DIRECTORY FILES "C:/CrossHatch-JRDW/shaders")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/." TYPE DIRECTORY FILES "C:/CrossHatch-JRDW/meshes")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/." TYPE DIRECTORY FILES "C:/CrossHatch-JRDW/noise textures")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/." TYPE DIRECTORY FILES "C:/CrossHatch-JRDW/videos")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/." TYPE DIRECTORY FILES "C:/CrossHatch-JRDW/fonts")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/." TYPE DIRECTORY FILES "C:/CrossHatch-JRDW/comic elements")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/." TYPE DIRECTORY FILES "C:/CrossHatch-JRDW/assets")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/." TYPE DIRECTORY FILES "C:/CrossHatch-JRDW/textures")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/." TYPE DIRECTORY FILES "C:/CrossHatch-JRDW/saves")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/." TYPE FILE FILES "C:/CrossHatch-JRDW/imgui.ini")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/." TYPE FILE FILES
    "C:/CrossHatch-JRDW/assimp/bin/assimp-vc143-mtd.dll"
    "C:/CrossHatch-JRDW/ffmpeg/bin/avcodec-61.dll"
    "C:/CrossHatch-JRDW/ffmpeg/bin/avformat-61.dll"
    "C:/CrossHatch-JRDW/ffmpeg/bin/avutil-59.dll"
    "C:/CrossHatch-JRDW/ffmpeg/bin/swscale-8.dll"
    "C:/CrossHatch-JRDW/ffmpeg/bin/swresample-5.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/opencv_calib3d4.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/opencv_core4.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/opencv_dnn4.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/opencv_features2d4.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/opencv_flann4.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/opencv_highgui4.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/opencv_imgcodecs4.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/opencv_imgproc4.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/opencv_ml4.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/opencv_objdetect4.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/opencv_photo4.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/opencv_stitching4.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/opencv_video4.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/opencv_videoio4.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/abseil_dll.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/libprotobuf-lite.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/libprotobuf.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/libprotoc.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/turbojpeg.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/jpeg62.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/liblzma.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/libpng16.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/libsharpyuv.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/libwebp.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/libwebpdecoder.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/libwebpdemux.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/libwebpmux.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/tiff.dll"
    "C:/CrossHatch-JRDW/vcpkg/installed/x64-windows/bin/zlib1.dll"
    )
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("C:/CrossHatch-JRDW/build/bgfx.cmake/cmake_install.cmake")
  include("C:/CrossHatch-JRDW/build/glfw/cmake_install.cmake")

endif()

if(CMAKE_INSTALL_COMPONENT)
  set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
file(WRITE "C:/CrossHatch-JRDW/build/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
