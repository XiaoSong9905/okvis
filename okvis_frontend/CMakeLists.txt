cmake_minimum_required(VERSION 2.8.11)
project(okvis_frontend)

if(NOT DO_TIMING)
  add_definitions(-DDEACTIVATE_TIMERS)
  message(STATUS "Deactivating timers.")
endif()    

# libtorch
set (Torch_DIR "/home/openark/DeepKeypoints/abi/pytorch11abi0/torch/lib/tmp_install/share/cmake/Torch")
find_package(Torch REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TORCH_CXX_FLAGS}")
message(STATUS "Found Torch dir : ${TORCH_INCLUDE_DIRS}")
message(STATUS "Found Torch lib : ${TORCH_LIBRARIES}")

# require Eigen
find_package( Eigen REQUIRED )
include_directories(${EIGEN_INCLUDE_DIR}) 

# build the library
add_library(${PROJECT_NAME} SHARED
  src/Frontend.cpp
  src/VioKeyframeWindowMatchingAlgorithm.cpp
  src/stereo_triangulation.cpp
  src/ProbabilisticStereoTriangulator.cpp
  src/FrameNoncentralAbsoluteAdapter.cpp
  src/FrameRelativeAdapter.cpp
  src/gcnv2.cpp
  include/okvis/gcnv2.hpp
  include/okvis/Frontend.hpp
  include/okvis/VioKeyframeWindowMatchingAlgorithm.hpp
  include/okvis/triangulation/stereo_triangulation.hpp
  include/okvis/triangulation/ProbabilisticStereoTriangulator.hpp
  include/opengv/absolute_pose/FrameNoncentralAbsoluteAdapter.hpp
  include/opengv/relative_pose/FrameRelativeAdapter.hpp
  include/opengv/sac_problems/absolute_pose/FrameAbsolutePoseSacProblem.hpp
  include/opengv/sac_problems/relative_pose/FrameRelativePoseSacProblem.hpp
  include/opengv/sac_problems/relative_pose/FrameRotationOnlySacProblem.hpp)

# and link it                 
target_link_libraries(${PROJECT_NAME}
  PRIVATE "${BRISK_LIBRARIES}"
  PRIVATE "${OpenGV_LIBRARIES}" 
  PRIVATE "${CERES_LIBRARIES}"
  PRIVATE "${TORCH_LIBRARIES}"
  PUBLIC okvis_util
  PUBLIC okvis_cv 
  PUBLIC okvis_ceres 
  PUBLIC okvis_timing 
  PUBLIC okvis_matcher)

target_include_directories(${PROJECT_NAME} BEFORE PRIVATE ${TORCH_INCLUDE_DIRS})

set (INSTALL_LIB_DIR "/home/openark/test")
set (INSTALL_INCLUDE_DIR "/home/openark/test")

# installation if required
install(TARGETS ${PROJECT_NAME}
  EXPORT okvisTargets
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}"
)
install(DIRECTORY include/ DESTINATION ${INSTALL_INCLUDE_DIR} COMPONENT dev FILES_MATCHING PATTERN "*.hpp")

# see: https://github.com/pytorch/pytorch/issues/14352
