# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestPRVoiceRouting
	TestPRVoiceRouting.cpp
	"${CMAKE_SOURCE_DIR}/src/mumble/PRVoiceRouting.cpp"
)

set_target_properties(TestPRVoiceRouting PROPERTIES AUTOMOC ON)

target_include_directories(TestPRVoiceRouting PRIVATE "${CMAKE_SOURCE_DIR}/src/mumble")

target_link_libraries(TestPRVoiceRouting PRIVATE Qt6::Test)

add_test(NAME TestPRVoiceRouting COMMAND $<TARGET_FILE:TestPRVoiceRouting>)
