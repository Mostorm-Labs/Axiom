require 'json'

Pod::Spec.new do |spec|
  spec.name = 'AxiomPoc05Fabric'
  spec.version = '0.1.0'
  spec.summary = 'POC-05 Apple React Native Fabric hybrid surface host.'
  spec.homepage = 'https://github.com/Mostorm-Labs/Axiom'
  spec.license = { :type => 'MIT' }
  spec.author = { 'Mostorm Labs' => 'engineering@mostorm.dev' }
  spec.platforms = { :ios => '17.0' }
  spec.source = { :http => 'https://github.com/Mostorm-Labs/Axiom/archive/refs/heads/main.zip' }
  # App-local POC pod. RuntimeSources contains one-line compilation adapters
  # for the authoritative POC-03/05 sources because CocoaPods deliberately
  # ignores source_files outside a local pod's root.
  spec.source_files = [
    'AxiomPoc05/*.{h,mm}',
    'AxiomPoc05/RuntimeSources/*.{c,cpp}',
  ]
  spec.public_header_files = 'AxiomPoc05/*.h'
  spec.requires_arc = true
  spec.dependency 'React-Core'
  spec.dependency 'React-RCTFabric'
  spec.dependency 'ReactCodegen'
  spec.frameworks = 'AVFoundation', 'CoreMedia', 'CoreVideo', 'Metal', 'QuartzCore', 'UIKit', 'WebKit'

  # The experiment consumes the exact locked iOS arm64 SDK. In CI and a clean
  # checkout, tools/skia/fetch.py must run before pod install/build.
  canvas_root = File.expand_path('../../../../..', __dir__)
  device_skia_root = ENV.fetch(
    'CANVAS_SKIA_DEVICE_SDK_ROOT',
    File.join(canvas_root, '.deps/skia-sdk/ios-arm64-metal')
  )
  simulator_skia_root = ENV.fetch(
    'CANVAS_SKIA_SIMULATOR_SDK_ROOT',
    File.join(canvas_root, '.deps/skia-sdk/ios-simulator-arm64-metal')
  )
  common_headers = [
    File.join(canvas_root, 'pocs/large_scene/include'),
    File.join(canvas_root, 'pocs/large_scene/src'),
    File.join(canvas_root, 'pocs/large_scene/platform/skia'),
    File.join(canvas_root, 'pocs/hybrid_surface/include'),
    File.join(canvas_root, 'docs/api'),
    File.join(canvas_root, '.deps/xxhash'),
  ]
  device_headers = common_headers + [device_skia_root]
  simulator_headers = common_headers + [simulator_skia_root]
  device_libraries = %w[skia skcms freetype2 png zlib].map do |library|
    '"' + File.join(device_skia_root, 'lib/lib' + library + '.a') + '"'
  end
  simulator_libraries = %w[skia skcms freetype2 png zlib].map do |library|
    '"' + File.join(simulator_skia_root, 'lib/lib' + library + '.a') + '"'
  end
  spec.pod_target_xcconfig = {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++20',
    'CLANG_CXX_LIBRARY' => 'libc++',
    'CANVAS_SKIA_SDK_ROOT[sdk=iphoneos*]' => device_skia_root,
    'CANVAS_SKIA_SDK_ROOT[sdk=iphonesimulator*]' => simulator_skia_root,
    'HEADER_SEARCH_PATHS' => '$(inherited) "$(PODS_ROOT)/Headers/Private/Yoga" ' + common_headers.map { |path| '"' + path + '"' }.join(' '),
    'HEADER_SEARCH_PATHS[sdk=iphoneos*]' => '$(inherited) "$(PODS_ROOT)/Headers/Private/Yoga" ' + device_headers.map { |path| '"' + path + '"' }.join(' '),
    'HEADER_SEARCH_PATHS[sdk=iphonesimulator*]' => '$(inherited) "$(PODS_ROOT)/Headers/Private/Yoga" ' + simulator_headers.map { |path| '"' + path + '"' }.join(' '),
    'OTHER_LDFLAGS' => [
      '-framework CoreFoundation', '-framework CoreGraphics',
      '-framework CoreText', '-framework Foundation', '-framework ImageIO',
      '-framework Metal', '-framework MobileCoreServices', '-framework UIKit',
    ].join(' '),
    'OTHER_LDFLAGS[sdk=iphoneos*]' => '$(inherited) ' + device_libraries.join(' '),
    'OTHER_LDFLAGS[sdk=iphonesimulator*]' => '$(inherited) ' + simulator_libraries.join(' '),
  }
  # This pod is static, so its external Skia archives must also be visible at
  # the final application link. Keep the platform-qualified propagation local
  # to this app-only POC rather than exposing Skia handles through Fabric/JS.
  spec.user_target_xcconfig = {
    'OTHER_LDFLAGS[sdk=iphoneos*]' => '$(inherited) ' + device_libraries.join(' '),
    'OTHER_LDFLAGS[sdk=iphonesimulator*]' => '$(inherited) ' + simulator_libraries.join(' '),
  }
end
