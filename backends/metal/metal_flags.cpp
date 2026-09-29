#include "compat.h"
DEFINE_bool(metal_backend_telemetry, false,
            "Log concise Metal backend decision counters for render encoder "
            "lifetime, resolve/transfer planning, bindless binding, and "
            "texture upload/load behavior.",
            "Metal");
DEFINE_int32(metal_backend_telemetry_interval, 120,
             "Number of guest swaps between Metal backend telemetry summaries. "
             "Set to 0 to log only on shutdown.",
             "Metal");
DEFINE_bool(metal_constant_payload_cache, false,
            "Reuse identical Metal constant/descriptor payload uploads across "
            "draws within a frame. Disable to A/B hash/cache CPU cost against "
            "extra CBV uploads and root argument churn.",
            "Metal");
DEFINE_bool(metal_pipeline_binary_archive, true,
            "Use MTLBinaryArchive for Metal pipeline compilation caching. "
            "Requires store_shaders and a compatible OS/driver.",
            "Metal");
DEFINE_string(
    metal_residency_sets, "auto",
    "Use Metal residency sets for stable Metal allocations where supported.\n"
    "  auto: Enable when the runtime exposes MTLResidencySet.\n"
    "  true: Require creating a residency set, falling back only if creation "
    "fails.\n"
    "  false: Use per-encoder useResource/useHeap only.",
    "Metal");
DEFINE_bool(
    metal_root_rebuild_detail_telemetry, false,
    "Collect expensive Metal root argument rebuild diagnostics, including slot "
    "change histograms and CBV resource identity details. Leave disabled for "
    "normal profiling.",
    "Metal");
DEFINE_bool(metal_shader_disk_cache, true,
            "Cache translated Metal shader artifacts and binding metadata in "
            "the packed Metal artifact store.",
            "Metal");
DEFINE_bool(occlusion_query_log, false,
            "Log occlusion query lifetime and summary stats.", "GPU");
DEFINE_bool(submit_on_primary_buffer_end, true,
            "Submit the command buffer when a PM4 primary buffer ends if it's "
            "possible to submit immediately to try to reduce frame latency.",
            "GPU");

DEFINE_bool(ac6_ground_fix, false, "XeniOS AC6-specific texture workaround (leave disabled for Fable II)", "HACKS");
DEFINE_bool(half_pixel_offset, true, "Adjust pixel centers between guest and host graphics APIs", "GPU");
DEFINE_int32(anisotropic_override, -1, "Anisotropic filtering override (-1 follows the game)", "GPU");
DEFINE_bool(gpu_3d_to_2d_texture, true, "Handle 3D textures sampled as 2D using the first guest slice", "GPU");
