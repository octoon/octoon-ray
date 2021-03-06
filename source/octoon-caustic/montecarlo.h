#ifndef OCTOON_CAUSTIC_MONTECARLO_H_
#define OCTOON_CAUSTIC_MONTECARLO_H_

#include <algorithm>
#include <vector>
#include <atomic>
#include <radeon_rays.h>
#include <radeon_rays_cl.h>
#include <memory>
#include "tiny_obj_loader.h"

#include <octoon/caustic/pipeline.h>
#include <octoon/caustic/tonemapping.h>
#include <octoon/caustic/material.h>
#include <octoon/caustic/light.h>
#include <octoon/caustic/camera.h>

namespace octoon
{
	namespace caustic
	{
		struct RenderData
		{
			std::int32_t numEstimate;

			std::vector<RadeonRays::ray> rays[2];
			std::vector<RadeonRays::Intersection> hits;
			std::vector<RadeonRays::ray> shadowRays;
			std::vector<RadeonRays::Intersection> shadowHits;
			std::vector<RadeonRays::float3> samples;
			std::vector<RadeonRays::float3> samplesAccum;
			std::vector<RadeonRays::float2> random;
			std::vector<RadeonRays::float3> weights;

			RadeonRays::Buffer* fr_rays;
			RadeonRays::Buffer* fr_shadowrays;
			RadeonRays::Buffer* fr_shadowhits;
			RadeonRays::Buffer* fr_hits;
			RadeonRays::Buffer* fr_hitcount;
		};

		class MonteCarlo : public Pipeline
		{
		public:
			MonteCarlo() noexcept;
			MonteCarlo(std::uint32_t w, std::uint32_t h) noexcept;
			~MonteCarlo() noexcept;

			void setup(std::uint32_t w, std::uint32_t h) noexcept(false);

			const std::uint32_t* data() const noexcept;

			void render(const Camera& camera, std::uint32_t frame, std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) noexcept;

		private:
			bool init_data();
			bool init_Gbuffers(std::uint32_t w, std::uint32_t h) noexcept;
			bool init_RadeonRays() noexcept;
			bool init_RadeonRays_Scene();

		private:
			void GenerateWorkspace(std::int32_t numEstimate);

			void GenerateNoise(std::uint32_t frame, const RadeonRays::int2& offset, const RadeonRays::int2& size) noexcept;
			void GenerateRays(std::uint32_t pass) noexcept;
			void GenerateCamera(const Camera& camera, const RadeonRays::int2& offset, const RadeonRays::int2& size) noexcept;
			void GenerateLightRays(const Light& light) noexcept;

			void GatherFirstSampling() noexcept;
			void GatherSampling(std::int32_t pass) noexcept;
			void GatherHits() noexcept;
			void GatherShadowHits() noexcept;
			void GatherLightSamples(std::uint32_t pass, const Light& light) noexcept;

			void AccumSampling(std::uint32_t frame, const RadeonRays::int2& offset, const RadeonRays::int2& size) noexcept;
			void AdaptiveSampling() noexcept;

			void ColorTonemapping(std::uint32_t frame, const RadeonRays::int2& offset, const RadeonRays::int2& size) noexcept;

			void Estimate(const Camera& camera, std::uint32_t frame, const RadeonRays::int2& offset, const RadeonRays::int2& size);

		private:
			std::uint32_t width_;
			std::uint32_t height_;

			std::int32_t numBounces_;
			std::int32_t tileNums_;

			RadeonRays::IntersectionApi* api_;

			std::vector<std::uint32_t> ldr_;
			std::vector<RadeonRays::float3> hdr_;

			RenderData renderData_;

			std::unique_ptr<Tonemapping> tonemapping_;
			std::unique_ptr<class CranleyPatterson> sequences_;

			std::vector<tinyobj::shape_t> scene_;
			std::vector<Material> materials_;
		};
	}
}

#endif