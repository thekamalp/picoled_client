# CMakeList.txt : CMake project for picoled_client, include source and define
# project specific logic here.
#

# Add source to this project's executable.
add_executable (picoled_client
	"main.cpp"
	"picoled_client.cpp"
	"picoled_client.h"
	${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/passthru_vs.cso
	${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tex1_ps.cso
	)

function(add_hlsl_shader SHADER_TYPE INPUT_FILE OUTPUT_FILE)
	add_custom_command(
		OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${OUTPUT_FILE}
		COMMAND fxc -E \"main\" -T \"${SHADER_TYPE}\" -Fo ${OUTPUT_FILE} ../picoled_client/${INPUT_FILE}
		MAIN_DEPENDENCY ${INPUT_FILE}
		WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
	)
endfunction()

add_hlsl_shader(vs_5_0 "shaders/passthru_vs.hlsl" "passthru_vs.cso")
add_hlsl_shader(ps_5_0 "shaders/tex1_ps.hlsl" "tex1_ps.cso")

target_link_libraries(picoled_client k3)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET picoled_client PROPERTY CXX_STANDARD 20)
endif()

# TODO: Add tests and install targets if needed.
