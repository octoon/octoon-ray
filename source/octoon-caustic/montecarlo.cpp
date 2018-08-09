#include "montecarlo.h"
#include "hammersley.h"
#include "math.h"
#include <assert.h>
#include <atomic>
#include <string>
#include <CL/cl.h>

#include <octoon/caustic/ACES.h>

#include "BSDF.h"
#include "halton.h"
#include "cranley_patterson.h"

namespace octoon
{
	namespace caustic
	{
		float GetPhysicalLightAttenuation(const RadeonRays::float3& L, float radius = std::numeric_limits<float>::max(), float attenuationBulbSize = 1.0f)
		{
			const float invRadius = 1.0f / radius;
			float d = std::sqrt(RadeonRays::dot(L, L));
			float fadeoutFactor = std::min(1.0f, std::max(0.0f, (radius - d) * (invRadius / 0.2f)));
			d = std::max(d - attenuationBulbSize, 0.0f);
			float denom = 1.0f + d / attenuationBulbSize;
			float attenuation = fadeoutFactor * fadeoutFactor / (denom * denom);
			return attenuation;
		}

		RadeonRays::float3 ConvertFromBarycentric(const float* vec, const int* ind, int prim_id, const RadeonRays::float4& uvwt)
		{
			RadeonRays::float3 a = { vec[ind[prim_id * 3] * 3], vec[ind[prim_id * 3] * 3 + 1], vec[ind[prim_id * 3] * 3 + 2], };
			RadeonRays::float3 b = { vec[ind[prim_id * 3 + 1] * 3], vec[ind[prim_id * 3 + 1] * 3 + 1], vec[ind[prim_id * 3 + 1] * 3 + 2], };
			RadeonRays::float3 c = { vec[ind[prim_id * 3 + 2] * 3], vec[ind[prim_id * 3 + 2] * 3 + 1], vec[ind[prim_id * 3 + 2] * 3 + 2], };
			return a * (1 - uvwt.x - uvwt.y) + b * uvwt.x + c * uvwt.y;
		}

		MonteCarlo::MonteCarlo() noexcept
			: numBounces_(6)
			, tileNums_(0)
			, width_(0)
			, height_(0)
			, api_(nullptr)
		{
			renderData_.numEstimate = 0;
			renderData_.fr_rays = nullptr;
			renderData_.fr_hits = nullptr;
			renderData_.fr_intersections = nullptr;
			renderData_.fr_shadowrays = nullptr;
			renderData_.fr_shadowhits = nullptr;
		}

		MonteCarlo::MonteCarlo(std::uint32_t w, std::uint32_t h) noexcept
			: MonteCarlo()
		{
			this->setup(w, h);
		}

		MonteCarlo::~MonteCarlo() noexcept
		{
			if (renderData_.fr_rays)
				api_->DeleteBuffer(renderData_.fr_rays);
			if (renderData_.fr_hits)
				api_->DeleteBuffer(renderData_.fr_hits);
			if (renderData_.fr_intersections)
				api_->DeleteBuffer(renderData_.fr_intersections);
			if (renderData_.fr_shadowhits)
				api_->DeleteBuffer(renderData_.fr_shadowhits);
			if (renderData_.fr_shadowrays)
				api_->DeleteBuffer(renderData_.fr_shadowrays);
			if (api_)
				RadeonRays::IntersectionApi::Delete(api_);
		}

		void
		MonteCarlo::setup(std::uint32_t w, std::uint32_t h) noexcept(false)
		{
			width_ = w;
			height_ = h;

			bxdf_ = std::make_unique<caustic::BSDF>();
			tonemapping_ = std::make_unique<caustic::ACES>();
			sequences_ = std::make_unique<caustic::CranleyPatterson>(std::make_unique<caustic::Halton>(), width_ * height_);

			if (!init_data()) throw std::runtime_error("init_data() fail");
			if (!init_Gbuffers(w, h)) throw std::runtime_error("init_Gbuffers() fail");
			if (!init_RadeonRays()) throw std::runtime_error("init_RadeonRays() fail");
			if (!init_RadeonRays_Scene()) throw std::runtime_error("init_RadeonRays_Scene() fail");
		}

		bool
		MonteCarlo::init_Gbuffers(std::uint32_t, std::uint32_t h) noexcept
		{
			auto allocSize = width_ * height_;
			ldr_.resize(allocSize);
			hdr_.resize(allocSize);

			return true;
		}

		bool
		MonteCarlo::init_RadeonRays() noexcept
		{
			RadeonRays::IntersectionApi::SetPlatform(RadeonRays::DeviceInfo::kAny);

			auto deviceidx = std::string::npos;
			for (auto i = 0U; i < RadeonRays::IntersectionApi::GetDeviceCount(); ++i)
			{
				RadeonRays::DeviceInfo devinfo;
				RadeonRays::IntersectionApi::GetDeviceInfo(i, devinfo);

				if (devinfo.type == RadeonRays::DeviceInfo::kGpu)
				{
					std::string info_name(devinfo.name);
					if (info_name.find("Intel") != std::string::npos)
						continue;
					deviceidx = i;
					break;
				}
			}

			if (deviceidx == std::string::npos)
			{
				for (auto i = 0U; i < RadeonRays::IntersectionApi::GetDeviceCount(); ++i)
				{
					RadeonRays::DeviceInfo devinfo;
					RadeonRays::IntersectionApi::GetDeviceInfo(i, devinfo);

					if (devinfo.type == RadeonRays::DeviceInfo::kCpu)
					{
						deviceidx = i;
						break;
					}
				}
			}

			if (deviceidx == std::string::npos) return false;

			this->api_ = RadeonRays::IntersectionApi::Create(deviceidx);

			return true;
		}

		bool
		MonteCarlo::init_data()
		{
			std::string basepath = "../Resources/CornellBox/";
			std::string filename = basepath + "orig.objm";

			std::vector<tinyobj::material_t> material;
			std::string res = LoadObj(scene_, material, filename.c_str(), basepath.c_str());

			for (auto& it : material)
			{
				caustic::Material m;
				m.albedo.x = std::pow(it.diffuse[0], 2.2f) * it.illum;
				m.albedo.y = std::pow(it.diffuse[1], 2.2f) * it.illum;
				m.albedo.z = std::pow(it.diffuse[2], 2.2f) * it.illum;

				m.specular.x = std::pow(it.specular[0], 2.2f) * it.illum * 0.04f;
				m.specular.y = std::pow(it.specular[1], 2.2f) * it.illum * 0.04f;
				m.specular.z = std::pow(it.specular[2], 2.2f) * it.illum * 0.04f;

				m.emissive.x /= (4 * PI / it.illum);
				m.emissive.y /= (4 * PI / it.illum);
				m.emissive.z /= (4 * PI / it.illum);

				m.ior = it.ior;
				m.metalness = saturate(it.dissolve);
				m.roughness = std::max(1e-1f, saturate(it.shininess));

				materials_.push_back(m);
			}

			return res != "" ? false : true;
		}

		bool
		MonteCarlo::init_RadeonRays_Scene()
		{
			for (int id = 0; id < this->scene_.size(); ++id)
			{
				tinyobj::shape_t& objshape = this->scene_[id];

				float* vertdata = objshape.mesh.positions.data();
				std::size_t nvert = objshape.mesh.positions.size() / 3;
				int* indices = objshape.mesh.indices.data();
				std::size_t nfaces = objshape.mesh.indices.size() / 3;

				RadeonRays::Shape* shape = this->api_->CreateMesh(vertdata, (int)nvert, 3 * sizeof(float), indices, 0, nullptr, (int)nfaces);

				assert(shape != nullptr);
				this->api_->AttachShape(shape);

				shape->SetId(id);
			}

			this->api_->Commit();

			return true;
		}

		const std::uint32_t*
		MonteCarlo::data() const noexcept
		{
			return ldr_.data();
		}

		void
		MonteCarlo::GenerateWorkspace(std::int32_t numEstimate)
		{
			if (tileNums_ < numEstimate)
			{
				renderData_.rays.resize(numEstimate);
				renderData_.hits.resize(numEstimate);
				renderData_.samples.resize(numEstimate);
				renderData_.random.resize(numEstimate);
				renderData_.weights.resize(numEstimate);
				renderData_.shadowHits.resize(numEstimate);

				if (renderData_.fr_rays)
					api_->DeleteBuffer(renderData_.fr_rays);

				if (renderData_.fr_hits)
					api_->DeleteBuffer(renderData_.fr_hits);

				if (renderData_.fr_intersections)
					api_->DeleteBuffer(renderData_.fr_intersections);

				if (renderData_.fr_shadowrays)
					api_->DeleteBuffer(renderData_.fr_shadowrays);

				if (renderData_.fr_shadowhits)
					api_->DeleteBuffer(renderData_.fr_shadowhits);

				renderData_.fr_rays = api_->CreateBuffer(sizeof(RadeonRays::ray) * numEstimate, nullptr);
				renderData_.fr_hits = api_->CreateBuffer(sizeof(RadeonRays::Intersection) * numEstimate, nullptr);
				renderData_.fr_intersections = api_->CreateBuffer(sizeof(RadeonRays::Intersection) * numEstimate, nullptr);
				renderData_.fr_shadowrays = api_->CreateBuffer(sizeof(RadeonRays::ray) * numEstimate, nullptr);
				renderData_.fr_shadowhits = api_->CreateBuffer(sizeof(RadeonRays::Intersection) * numEstimate, nullptr);

				tileNums_ = numEstimate;
			}

			this->renderData_.numEstimate = numEstimate;
		}

		void
		MonteCarlo::GenerateNoise(std::uint32_t frame, const RadeonRays::int2& offset, const RadeonRays::int2& size) noexcept
		{
	#pragma omp parallel for
			for (std::int32_t i = 0; i < this->renderData_.numEstimate; ++i)
			{
				auto ix = offset.x + i % size.x;
				auto iy = offset.y + i / size.x;
				auto index = iy * this->width_ + ix;

				float sx = sequences_->sample(0, frame, index);
				float sy = sequences_->sample(1, frame, index);

				this->renderData_.random[i] = RadeonRays::float2(sx, sy);
			}
		}

		void
		MonteCarlo::GenerateCamera(const Camera& camera, const RadeonRays::int2& offset, const RadeonRays::int2& size) noexcept
		{
			RadeonRays::ray* rays = nullptr;
			RadeonRays::Event* e = nullptr;
			api_->MapBuffer(renderData_.fr_rays, RadeonRays::kMapWrite, 0, sizeof(RadeonRays::ray) * this->renderData_.numEstimate, (void**)&rays, &e); e->Wait(); api_->DeleteEvent(e);

			float aspect = (float)width_ / height_;
			float xstep = 2.0f / (float)this->width_;
			float ystep = 2.0f / (float)this->height_;

	#pragma omp parallel for
			for (std::int32_t i = 0; i < this->renderData_.numEstimate; ++i)
			{
				auto ix = offset.x + i % size.x;
				auto iy = offset.y + i / size.x;

				float x = xstep * ix - 1.0f + (renderData_.random[i].x * 2 - 1) / (float)this->width_;
				float y = ystep * iy - 1.0f + (renderData_.random[i].y * 2 - 1) / (float)this->height_;
				float z = 1.0f;

				auto& ray = rays[i];
				ray.o = camera.getTranslate();
				ray.d = RadeonRays::normalize(RadeonRays::float3(x * aspect, y, z - ray.o.z));
				ray.SetMaxT(std::numeric_limits<float>::max());
				ray.SetTime(0.0f);
				ray.SetMask(-1);
				ray.SetActive(true);
				ray.SetDoBackfaceCulling(true);
			}

			std::memcpy(renderData_.rays.data(), rays, sizeof(RadeonRays::ray) * this->renderData_.numEstimate);

			api_->UnmapBuffer(renderData_.fr_rays, rays, &e); e->Wait(); api_->DeleteEvent(e);
		}

		void
		MonteCarlo::GenerateRays() noexcept
		{
			RadeonRays::ray* rays = nullptr;
			RadeonRays::Event* e = nullptr;
			api_->MapBuffer(renderData_.fr_rays, RadeonRays::kMapWrite, 0, sizeof(RadeonRays::ray) * this->renderData_.numEstimate, (void**)&rays, &e); e->Wait(); api_->DeleteEvent(e);

			std::memset(renderData_.weights.data(), 0, sizeof(RadeonRays::float3) * this->renderData_.numEstimate);

	#pragma omp parallel for
			for (std::int32_t i = 0; i < this->renderData_.numEstimate; ++i)
			{
				auto& hit = renderData_.hits[i];
				if (hit.shapeid != RadeonRays::kNullId && hit.primid != RadeonRays::kNullId)
				{
					auto& mesh = scene_[hit.shapeid].mesh;
					auto& mat = materials_[mesh.material_ids[hit.primid]];

					if (mat.emissive.x > 0.0f || mat.emissive[1] > 0.0f || mat.emissive[2] > 0.0f)
					{
						std::memset(&renderData_.rays[i], 0, sizeof(RadeonRays::ray));
					}
					else
					{
						auto ro = ConvertFromBarycentric(mesh.positions.data(), mesh.indices.data(), hit.primid, hit.uvwt);
						auto norm = ConvertFromBarycentric(mesh.normals.data(), mesh.indices.data(), hit.primid, hit.uvwt);

						RadeonRays::float3 L = bxdf_->sample(renderData_.rays[i].d, norm, mat, renderData_.random[i]);
						if (mat.ior <= 1.0f)
						{
							if (RadeonRays::dot(norm, L) < 0.0f)
								L = -L;
						}

						assert(std::isfinite(L.x + L.y + L.z));

						renderData_.weights[i] = bxdf_->sample_weight(renderData_.rays[i].d, norm, L, mat, renderData_.random[i]);

						auto& ray = renderData_.rays[i];
						ray.d = L;
						ray.o = ro + L * 1e-5f;
						ray.SetMaxT(std::numeric_limits<float>::max());
						ray.SetTime(0.0f);
						ray.SetMask(-1);
						ray.SetActive(true);
						ray.SetDoBackfaceCulling(mat.ior > 1.0f ? false : true);
					}
				}
				else
				{
					std::memset(&renderData_.rays[i], 0, sizeof(RadeonRays::ray));
				}
			}

			std::memcpy(rays, renderData_.rays.data(), sizeof(RadeonRays::ray) * this->renderData_.numEstimate);

			api_->UnmapBuffer(renderData_.fr_rays, rays, &e); e->Wait(); api_->DeleteEvent(e);
		}

		void
		MonteCarlo::GenerateLightRays(const Light& light) noexcept
		{
			RadeonRays::ray* rays = nullptr;
			RadeonRays::Event* e = nullptr;
			api_->MapBuffer(renderData_.fr_shadowrays, RadeonRays::kMapWrite, 0, sizeof(RadeonRays::ray) * this->renderData_.numEstimate, (void**)&rays, &e); e->Wait(); api_->DeleteEvent(e);

			std::memset(rays, 0, sizeof(RadeonRays::ray));

#pragma omp parallel for
			for (std::int32_t i = 0; i < this->renderData_.numEstimate; ++i)
			{
				auto& hit = renderData_.hits[i];
				if (hit.shapeid != RadeonRays::kNullId && hit.primid != RadeonRays::kNullId)
				{
					auto& mesh = scene_[hit.shapeid].mesh;
					auto& mat = materials_[mesh.material_ids[hit.primid]];

					if (mat.emissive.x == 0.0f && mat.emissive[1] == 0.0f && mat.emissive[2] == 0.0f)
					{
						auto ro = ConvertFromBarycentric(mesh.positions.data(), mesh.indices.data(), hit.primid, hit.uvwt);
						auto norm = ConvertFromBarycentric(mesh.normals.data(), mesh.indices.data(), hit.primid, hit.uvwt);

						RadeonRays::float3 L = light.sample(ro, norm, mat, renderData_.random[i].x);
						assert(std::isfinite(L[0] + L[1] + L[2]));

						if (RadeonRays::dot(L, L) > 0.0f)
						{
							auto& ray = rays[i];
							ray.d = RadeonRays::float3(L[0], L[1], L[2]);
							ray.o = ro + ray.d * 1e-5f;
							ray.SetMaxT(std::numeric_limits<float>::max());
							ray.SetTime(0.0f);
							ray.SetMask(-1);
							ray.SetActive(true);
							ray.SetDoBackfaceCulling(mat.ior > 1.0f ? false : true);
						}
					}
				}
			}

			api_->UnmapBuffer(renderData_.fr_shadowrays, rays, &e); e->Wait(); api_->DeleteEvent(e);
		}

		void
		MonteCarlo::GatherHits() noexcept
		{
			RadeonRays::Event* e = nullptr;
			RadeonRays::Intersection* hit = nullptr;

			api_->MapBuffer(renderData_.fr_hits, RadeonRays::kMapRead, 0, sizeof(RadeonRays::Intersection) * this->renderData_.numEstimate, (void**)&hit, &e); e->Wait(); api_->DeleteEvent(e);

			std::memcpy(renderData_.hits.data(), hit, sizeof(RadeonRays::Intersection) * this->renderData_.numEstimate);

			api_->UnmapBuffer(renderData_.fr_hits, hit, &e); e->Wait(); api_->DeleteEvent(e);
		}

		void
		MonteCarlo::GatherShadowHits() noexcept
		{
			RadeonRays::Event* e = nullptr;
			RadeonRays::Intersection* hit = nullptr;

			api_->MapBuffer(renderData_.fr_shadowhits, RadeonRays::kMapRead, 0, sizeof(RadeonRays::Intersection) * this->renderData_.numEstimate, (void**)&hit, &e); e->Wait(); api_->DeleteEvent(e);

			std::memcpy(renderData_.shadowHits.data(), hit, sizeof(RadeonRays::Intersection) * this->renderData_.numEstimate);

			api_->UnmapBuffer(renderData_.fr_shadowhits, hit, &e); e->Wait(); api_->DeleteEvent(e);
		}

		void
		MonteCarlo::GatherFirstSampling(std::atomic_uint32_t& sampleCounter) noexcept
		{
			std::memset(renderData_.samples.data(), 0, sizeof(RadeonRays::float3) * this->renderData_.numEstimate);

	#pragma omp parallel for
			for (std::int32_t i = 0; i < this->renderData_.numEstimate; ++i)
			{
				if (!renderData_.rays[i].IsActive())
					continue;

				auto& hit = renderData_.hits[i];
				auto& sample = renderData_.samples[i];
				if (hit.shapeid == RadeonRays::kNullId || hit.primid == RadeonRays::kNullId)
					continue;

				auto& mesh = scene_[hit.shapeid].mesh;
				auto& mat = materials_[mesh.material_ids[hit.primid]];

				if (mat.emissive[0] > 0.0f || mat.emissive[1] > 0.0f || mat.emissive[2] > 0.0f)
				{
					sample.x = mat.emissive[0];
					sample.y = mat.emissive[1];
					sample.z = mat.emissive[2];
				}
				else
				{
					sample.x = 1.0f;
					sample.y = 1.0f;
					sample.z = 1.0f;

					sampleCounter++;
				}
			}
		}

		void
		MonteCarlo::GatherSampling(std::int32_t pass) noexcept
		{
	#pragma omp parallel for
			for (std::int32_t i = 0; i < this->renderData_.numEstimate; ++i)
			{
				if (!renderData_.rays[i].IsActive())
					continue;

				auto& hit = renderData_.hits[i];
				auto& sample = renderData_.samples[i];

				if (hit.shapeid != RadeonRays::kNullId && hit.primid != RadeonRays::kNullId)
				{
					auto& mesh = scene_[hit.shapeid].mesh;
					auto& mat = materials_[mesh.material_ids[hit.primid]];

					float atten = GetPhysicalLightAttenuation(renderData_.rays[i].o - ConvertFromBarycentric(mesh.positions.data(), mesh.indices.data(), hit.primid, hit.uvwt));
					if (mat.emissive[0] > 0.0f || mat.emissive[1] > 0.0f || mat.emissive[2] > 0.0f)
					{
						sample.x *= mat.emissive[0] * atten;
						sample.y *= mat.emissive[1] * atten;
						sample.z *= mat.emissive[2] * atten;
						sample *= renderData_.weights[i];
					}
					else
					{
						if (pass + 1 >= numBounces_)
							sample *= 0.0f;
						else
							sample *= renderData_.weights[i] * atten;
					}
				}
			}
		}

		void
		MonteCarlo::GatherLightSamples(const Light& light) noexcept
		{
			float color[3];
			light.getColor(color);

#pragma omp parallel for
			for (std::int32_t i = 0; i < this->renderData_.numEstimate; ++i)
			{
				if (!renderData_.rays[i].IsActive())
					continue;

				auto& hit = renderData_.hits[i];
				auto& sample = renderData_.samples[i];

				if (hit.shapeid == RadeonRays::kNullId || hit.primid == RadeonRays::kNullId)
				{
					sample.x *= color[0];
					sample.y *= color[1];
					sample.z *= color[2];
					sample *= renderData_.weights[i];
				}
			}
		}

		void
		MonteCarlo::Estimate(const Camera& camera, const Scene& scene, std::uint32_t frame, const RadeonRays::int2& offset, const RadeonRays::int2& size)
		{
			this->GenerateWorkspace(size.x * size.y);
			this->GenerateNoise(frame, offset, size);

			this->GenerateCamera(camera, offset, size);

			for (std::int32_t pass = 0; pass < numBounces_; pass++)
			{
				api_->QueryIntersection(
					renderData_.fr_rays,
					renderData_.numEstimate,
					renderData_.fr_hits,
					nullptr,
					nullptr
				);

				this->GatherHits();

				if (pass == 0)
				{
					std::atomic_uint32_t count = 0;
					this->GatherFirstSampling(count);

					if (count == 0)
						break;
				}

				if (pass > 0)
				{
					this->GatherSampling(pass);
				}

				for (auto& light : scene.getLightList())
				{
					this->GenerateLightRays(*light);

					api_->QueryOcclusion(
						renderData_.fr_shadowrays,
						renderData_.numEstimate,
						renderData_.fr_shadowhits,
						nullptr,
						nullptr
					);

					this->GatherShadowHits();
					this->GatherLightSamples(*light);
				}

				// prepare ray for indirect lighting gathering
				this->GenerateRays();
			}

			this->AccumSampling(frame, offset, size);
			this->AdaptiveSampling();

			this->ColorTonemapping(frame, offset, size);
		}

		void
		MonteCarlo::AccumSampling(std::uint32_t frame, const RadeonRays::int2& offset, const RadeonRays::int2& size) noexcept
		{
	#pragma omp parallel for
			for (std::int32_t i = 0; i < size.x * size.y; ++i)
			{
				auto ix = offset.x + i % size.x;
				auto iy = offset.y + i / size.x;
				auto index = iy * this->width_ + ix;

				auto& hdr = hdr_[index];
				hdr.x += renderData_.samples[i].x;
				hdr.y += renderData_.samples[i].y;
				hdr.z += renderData_.samples[i].z;
			}
		}

		void
		MonteCarlo::AdaptiveSampling() noexcept
		{
		}

		void
		MonteCarlo::ColorTonemapping(std::uint32_t frame, const RadeonRays::int2& offset, const RadeonRays::int2& size) noexcept
		{
	#pragma omp parallel for
			for (std::int32_t i = 0; i < size.x * size.y; ++i)
			{
				auto ix = offset.x + i % size.x;
				auto iy = offset.y + i / size.x;
				auto index = iy * this->width_ + ix;

				auto& hdr = hdr_[index];
				assert(std::isfinite(hdr.x));
				assert(std::isfinite(hdr.y));
				assert(std::isfinite(hdr.z));

				std::uint8_t r = tonemapping_->map(hdr.x / frame) * 255;
				std::uint8_t g = tonemapping_->map(hdr.y / frame) * 255;
				std::uint8_t b = tonemapping_->map(hdr.z / frame) * 255;

				ldr_[index] = 0xFF << 24 | b << 16 | g << 8 | r;
			}
		}

		void
		MonteCarlo::render(const Scene& scene, std::uint32_t frame, std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) noexcept
		{
			auto& cameras = scene.getCameraList();
			for (auto& camera : cameras)
				this->Estimate(*camera, scene, frame, RadeonRays::int2(x, y), RadeonRays::int2(w, h));
		}
	}
}