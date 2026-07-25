if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/src/platform/macos/metal_host.h" host_header)
file(READ "${SOURCE_DIR}/src/platform/macos/metal_host.mm" host_source)

if(NOT host_header MATCHES
       "MetalHost is main-thread-affine: construct, use, detach, and destroy")
    message(FATAL_ERROR "MetalHost's owner-thread destruction contract is undocumented")
endif()

string(FIND "${host_source}" "void requireMainThread()" require_main_thread)
string(FIND "${host_source}" "std::terminate();" terminate_off_main_thread)
if(require_main_thread EQUAL -1 OR terminate_off_main_thread EQUAL -1)
    message(FATAL_ERROR "MetalHost must terminate rather than release native state off-main-thread")
endif()

string(FIND "${host_source}" "MetalHost::~MetalHost()" destructor_start)
if(destructor_start EQUAL -1)
    message(FATAL_ERROR "MetalHost destructor is missing")
endif()
string(SUBSTRING "${host_source}" ${destructor_start} 400 destructor_body)
string(FIND "${destructor_body}" "requireMainThread();" destructor_requires_main_thread)
if(destructor_requires_main_thread EQUAL -1)
    message(FATAL_ERROR "MetalHost destructor must enforce the main-thread contract")
endif()

if(host_source MATCHES "if \\(!impl_ \\|\\| !isMainThread\\(\\)\\) return;")
    message(FATAL_ERROR "MetalHost teardown must not silently skip off-main-thread cleanup")
endif()

# A CAMetalLayer can synchronously re-enter AppKit while finding a drawable.
# Claim the frame first so an inner draw cannot steal a request made for the
# next frame, then finish or restore that exact claim.
string(FIND "${host_source}" "if (!impl_->invalidation.beginFrame()) return;" begin_frame)
string(FIND "${host_source}" "[metalLayer nextDrawable]" next_drawable)
if(begin_frame EQUAL -1 OR next_drawable EQUAL -1 OR begin_frame GREATER next_drawable)
    message(FATAL_ERROR "MetalHost must claim the frame before nextDrawable can re-enter")
endif()

string(FIND "${host_source}" "if (drawable == nil || hostChanged())" drawable_failure)
string(FIND "${host_source}" "if (commandBuffer == nil || hostChanged())" command_buffer_failure)
string(FIND "${host_source}" "if (encoder == nil || hostChanged())" encoder_failure)
if(drawable_failure EQUAL -1 OR command_buffer_failure EQUAL -1 OR encoder_failure EQUAL -1)
    message(FATAL_ERROR "MetalHost must restore a claimed frame on drawable or encoding failure")
endif()

string(FIND "${host_source}" "[commandBuffer commit]" command_buffer_commit)
string(FIND "${host_source}" "impl_->invalidation.completeFrame(frameId);" complete_frame)
if(command_buffer_commit EQUAL -1 OR complete_frame EQUAL -1 OR
   complete_frame LESS command_buffer_commit)
    message(FATAL_ERROR "MetalHost must complete the claim only after committing")
endif()
