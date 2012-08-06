# @@@LICENSE
#
# Copyright (c) 2012 Hewlett-Packard Development Company, L.P.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# LICENSE@@@

cmake_minimum_required(VERSION 2.8.7)

project(LunaSysMgrIpc CXX)

include(webOS/webOS)
webos_modules_init(1 0 0 QUALIFIER RC2)
webos_component(2 0 0)

include(FindPkgConfig)

# -- check for glib 2.0
pkg_check_modules(GLIB2 REQUIRED glib-2.0)
webos_add_compiler_flags(ALL ${GLIB2_CFLAGS})

# -- check for gthread 2.0
pkg_check_modules(GTHREAD2 REQUIRED gthread-2.0)
webos_add_compiler_flags(ALL ${GTHREAD2_CFLAGS})

webos_build_pkgconfig()
webos_config_build_doxygen(doc Doxyfile)

webos_add_compiler_flags(ALL -g -O3 -Wall -fno-exceptions -fno-rtti -DQT_WEBOS)
webos_add_linker_options(ALL --no-undefined)

# -- add local include paths
include_directories(src/base)

file(GLOB SOURCE_FILES src/base/*.cpp src/ipc/*.cpp)
add_library(LunaSysMgrIpc SHARED ${SOURCE_FILES})
target_link_libraries(LunaSysMgrIpc ${GLIB2_LDFLAGS} ${GTHREAD2_LDFLAGS})

# -- install include files
install(DIRECTORY src/base src/ipc DESTINATION ${WEBOS_INSTALL_INCLUDEDIR}/sysmgr-ipc 
	FILES_MATCHING PATTERN "*.h"
)

# build the library without looking at include/public for headers
webos_build_library(NOHEADERS)
