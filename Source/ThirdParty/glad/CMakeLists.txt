#
# Copyright (c) 2008-2022 the Urho3D project.
# Copyright (c) 2022 Amer Koleci and Contributors.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#

# Define target name
file (GLOB_RECURSE SOURCE_FILES *.h *.c)
add_library (glad STATIC ${SOURCE_FILES})

target_include_directories(glad PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

if (NOT URHO3D_LIB_TYPE STREQUAL STATIC)
    set_property(TARGET glad PROPERTY POSITION_INDEPENDENT_CODE ON)
endif ()

set_property(TARGET glad PROPERTY FOLDER "ThirdParty")

# Install headers for building and using the Urho3D library (install dependency for OGLGraphicsImpl.h)
install(TARGETS glad EXPORT Urho3D ARCHIVE DESTINATION ${INSTALL_THIRDPARTY_DIR})
