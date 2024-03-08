cmake_minimum_required(VERSION 3.8)

project(MazeWalker C CXX)

find_package(Threads REQUIRED)
option(protobuf_MODULE_COMPATIBLE TRUE)
find_package(Protobuf CONFIG REQUIRED)
message(STATUS "Using protobuf ${Protobuf_VERSION}")

set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
set(_REFLECTION gRPC::grpc++_reflection)
set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)

find_package(gRPC CONFIG REQUIRED)
message(STATUS "Using gRPC ${gRPC_VERSION}")

set(_GRPC_GRPCPP gRPC::grpc++)
set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
  

# Proto file
get_filename_component(maze_proto "proto/maze.proto" ABSOLUTE)
get_filename_component(maze_proto_path "${maze_proto}" PATH)

# Generated sources
set(maze_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/maze.pb.cc")
set(maze_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/maze.pb.h")
set(maze_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/maze.grpc.pb.cc")
set(maze_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/maze.grpc.pb.h")

message(STATUS "_GRPC_CPP_PLUGIN_EXECUTABLE= ${_GRPC_CPP_PLUGIN_EXECUTABLE}")

add_custom_command(
      OUTPUT "${maze_proto_srcs}" "${maze_proto_hdrs}" "${maze_grpc_srcs}" "${maze_grpc_hdrs}"
      COMMAND ${_PROTOBUF_PROTOC}
      ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${maze_proto_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${maze_proto}"
      DEPENDS "${maze_proto}")

# Include generated *.pb.h files
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

# maze_grpc_proto
add_library(maze_grpc_proto
  ${maze_grpc_srcs}
  ${maze_grpc_hdrs}
  ${maze_proto_srcs}
  ${maze_proto_hdrs})
target_link_libraries(maze_grpc_proto
  ${_REFLECTION}
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF})

add_executable(maze_client src/maze_client.cpp)

target_link_libraries(maze_client
    maze_grpc_proto
    absl::flags
    absl::flags_parse
    ${_REFLECTION}
    ${_GRPC_GRPCPP}
    ${_PROTOBUF_LIBPROTOBUF})
