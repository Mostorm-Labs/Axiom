#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdlib>
#include <iostream>
#include <vector>

#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendContext.h"
#include "include/gpu/ganesh/d3d/GrD3DDirectContext.h"
#include "skia_large_scene_renderer.h"

using Microsoft::WRL::ComPtr;

int main() {
  using namespace canvas::poc03;
  ComPtr<IDXGIFactory6> factory;
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  if (FAILED(CreateDXGIFactory2(0U, IID_PPV_ARGS(&factory))) ||
      FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))) ||
      FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                               IID_PPV_ARGS(&device)))) {
    return EXIT_FAILURE;
  }
  D3D12_COMMAND_QUEUE_DESC queue_description{};
  queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (FAILED(device->CreateCommandQueue(&queue_description,
                                        IID_PPV_ARGS(&queue)))) {
    return EXIT_FAILURE;
  }
  GrD3DBackendContext backend;
  backend.fAdapter.retain(adapter.Get());
  backend.fDevice.retain(device.Get());
  backend.fQueue.retain(queue.Get());
  sk_sp<GrDirectContext> context = GrDirectContexts::MakeD3D(backend);
  const SkImageInfo info = SkImageInfo::Make(
      1280, 720, kRGBA_8888_SkColorType, kPremul_SkAlphaType,
      SkColorSpace::MakeSRGB());
  sk_sp<SkSurface> surface = context ? SkSurfaces::RenderTarget(
      context.get(), skgpu::Budgeted::kNo, info, 0,
      kTopLeft_GrSurfaceOrigin, nullptr) : nullptr;
  if (!surface) {
    return EXIT_FAILURE;
  }
  Document document = GenerateDocument(
      {100000U, 0x43414e5641533033ULL, 1000U, 32.0F});
  SceneCompiler compiler;
  RuntimeScene scene = compiler.CompileFull(document);
  NodeRecord changed = *document.Find(1U);
  changed.rgba ^= 0x00010101U;
  ++changed.content_revision;
  ChangeSet changes;
  CompileDiagnostics diagnostics;
  std::string error;
  if (!document.Apply({OperationKind::kUpdate, 1U, changed}, &changes, &error) ||
      !compiler.ApplyIncremental(document, changes, &scene, &diagnostics,
                                 &error)) {
    return EXIT_FAILURE;
  }
  const RuntimeScene oracle = compiler.CompileFull(document);
  const ViewState view{1U, 1U, 1U,
      Bounds{0.0F, 0.0F, 1280.0F, 720.0F},
      1.0F, 1.0F, 1280U, 720U};
  const ViewQueryResult query = QueryView(scene, view, std::nullopt);
  const FrameGraph frame = BuildFrame(scene, query, {});
  DrawLargeScene(*surface->getCanvas(), scene, view, frame);
  context->flushAndSubmit(surface.get(), GrSyncCpu::kYes);
  std::vector<uint8_t> rgba;
  if (!ReadRgba(*surface, 1280U, 720U, &rgba)) {
    return EXIT_FAILURE;
  }
  const std::string pixel_digest = PixelDigest(rgba);
  const ViewQueryResult oracle_query = QueryView(oracle, view, std::nullopt);
  DrawLargeScene(*surface->getCanvas(), oracle, view,
                 BuildFrame(oracle, oracle_query, {}));
  context->flushAndSubmit(surface.get(), GrSyncCpu::kYes);
  if (!ReadRgba(*surface, 1280U, 720U, &rgba)) {
    return EXIT_FAILURE;
  }
  const bool visual_equivalent = pixel_digest == PixelDigest(rgba);
  DXGI_ADAPTER_DESC1 description{};
  adapter->GetDesc1(&description);
  std::cout << "{\"backend\":\"ganesh-d3d12-warp\","
            << "\"vendor_id\":" << description.VendorId << ','
            << "\"device_id\":" << description.DeviceId << ','
            << "\"nodes\":100000,"
            << "\"document_digest\":\"" << document.Digest() << "\","
            << "\"scene_digest\":\"" << scene.Digest() << "\","
            << "\"oracle_scene_digest\":\"" << oracle.Digest() << "\","
            << "\"pixel_digest\":\"" << pixel_digest << "\","
            << "\"visual_equivalent\":"
            << (visual_equivalent ? "true" : "false") << ','
            << "\"maximum_candidates\":" << query.candidates.size() << "}\n";
  context->abandonContext();
  return visual_equivalent && scene.Digest() == oracle.Digest()
             ? EXIT_SUCCESS : EXIT_FAILURE;
}
