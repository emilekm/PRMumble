# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

find_package(nlohmann_json REQUIRED)

add_executable(TestPRGameChannels
	TestPRGameChannels.cpp
	"${CMAKE_SOURCE_DIR}/src/murmur/PRGameChannels.cpp"
)

set_target_properties(TestPRGameChannels PROPERTIES AUTOMOC ON)

target_include_directories(TestPRGameChannels PRIVATE "${CMAKE_SOURCE_DIR}/src/murmur")

target_link_libraries(TestPRGameChannels PRIVATE shared Qt6::Test nlohmann_json::nlohmann_json)

add_test(NAME TestPRGameChannels COMMAND $<TARGET_FILE:TestPRGameChannels>)
