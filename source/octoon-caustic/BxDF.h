#ifndef OCTOON_BxDF
#define OCTOON_BxDF

#include "math.h"
#include <assert.h>

namespace octoon
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

	inline RadeonRays::float4 UniformSampleSphere(const RadeonRays::float2& Xi)
	{
		float phi = 2 * PI * Xi.x;

		float cosTheta = 1 - 2 * Xi.y;
		float sinTheta = fast_sqrt(1 - cosTheta * cosTheta);

		RadeonRays::float4 H;
		H.x = fast_cos(phi) * sinTheta;
		H.y = fast_sin(phi) * sinTheta;
		H.z = cosTheta;
		H.w = 1.0 / (4 * PI);

		return H;
	}

	inline RadeonRays::float3 UniformSampleHemisphere(const RadeonRays::float2& Xi)
	{
		float phi = Xi.x * 2 * PI;

		float cosTheta = Xi.y;
		float sinTheta = fast_sqrt(1.0f - cosTheta * cosTheta);

		RadeonRays::float3 H;
		H.x = fast_cos(phi) * sinTheta;
		H.y = fast_sin(phi) * sinTheta;
		H.z = cosTheta;
		H.w = 1.0f / (2 * PI);

		return H;
	}

	inline RadeonRays::float3 UniformSampleCone(const RadeonRays::float2& Xi, float CosThetaMax)
	{
		float phi = 2 * PI * Xi.x;
		float cosTheta = CosThetaMax * (1 - Xi.y) + Xi.y;
		float sinTheta = fast_sqrt(1 - cosTheta * cosTheta);

		RadeonRays::float3 H;
		H.x = sinTheta * fast_cos(phi);
		H.y = sinTheta * fast_sin(phi);
		H.z = cosTheta;
		H.w = 1.0f / (2 * PI * (1 - CosThetaMax));

		return H;
	}

	inline RadeonRays::float3 CosineSampleHemisphere(const RadeonRays::float2& Xi)
	{
		float phi = Xi.x * 2.0f * PI;

		float cosTheta = fast_sqrt(Xi.y);
		float sinTheta = fast_sqrt(1.0f - cosTheta * cosTheta);

		RadeonRays::float3 H;
		H.x = fast_cos(phi) * sinTheta;
		H.y = fast_sin(phi) * sinTheta;
		H.z = cosTheta;
		H.w = cosTheta * (1 / PI);

		return H;
	}

	inline RadeonRays::float3 ImportanceSampleGGX(const RadeonRays::float2& Xi, float roughness)
	{
		float m = roughness * roughness;
		float m2 = m * m;
		float u = (1.0f - Xi.y) / (1.0f + (m2 - 1) * Xi.y);

		return CosineSampleHemisphere(RadeonRays::float2(Xi.x, u));
	}

	inline RadeonRays::float3 ImportanceSampleBlinn(const RadeonRays::float2& Xi, float a2)
	{
		float phi = Xi.x * 2.0f * PI;

		float n = 2 / a2 - 2;
		float cosTheta = std::pow(Xi.y, 1 / (n + 1));
		float sinTheta = fast_sqrt(1 - cosTheta * cosTheta);

		RadeonRays::float3 H;
		H.x = sinTheta * cos(phi);
		H.y = sinTheta * sin(phi);
		H.z = cosTheta;

		return H;
	}

	RadeonRays::float3 DiffuseBRDF(const RadeonRays::float3& N, const RadeonRays::float3& L, const RadeonRays::float3& V, float roughness)
	{
		float nl = RadeonRays::dot(L, N);
		if (nl > 0)
		{
			auto H = RadeonRays::normalize(L + V);

			float vh = std::max(0.0f, RadeonRays::dot(V, L));
			float nv = std::max(0.1f, RadeonRays::dot(V, N));

			float Fd90 = (0.5f + 2 * vh * vh) * roughness;
			float FdV = 1 + (Fd90 - 1) * pow5(1 - nv);
			float FdL = 1 + (Fd90 - 1) * pow5(1 - nl);

			float fresnel = FdV * FdL * (1.0f - 0.3333f * roughness);

			return RadeonRays::float3(fresnel, fresnel, fresnel);
		}

		return 0;
	}

	RadeonRays::float3 SpecularBRDF_GGX(const RadeonRays::float3& N, const RadeonRays::float3& L, const RadeonRays::float3& V, const RadeonRays::float3& f0, float roughness)
	{
		float nl = RadeonRays::dot(L, N);
		if (nl > 0)
		{
			auto H = RadeonRays::normalize(L + V);

			float nv = std::abs(RadeonRays::dot(V, N)) + 0.1f;
			float vh = saturate(RadeonRays::dot(V, L));
			float nh = saturate(RadeonRays::dot(H, N));

			float m = roughness * roughness;
			float Gv = nl * (nv * (1 - m) + m);
			float Gl = nv * (nl * (1 - m) + m);
			float G = 0.5f / (Gv + Gl);

			float Fc = pow5(1 - vh);
			RadeonRays::float3 F = f0 * (1 - Fc) + RadeonRays::float3(Fc, Fc, Fc);

			return F * G * nl * (4 * nl * nv);
		}

		return 0;
	}

	RadeonRays::float3 SpecularBTDF_GGX(const RadeonRays::float3& N, const RadeonRays::float3& L, const RadeonRays::float3& V, const RadeonRays::float3& f0, float roughness)
	{
		float nl = std::abs(RadeonRays::dot(L, N));
		if (nl > 0)
		{
			auto H = RadeonRays::normalize(L + V);

			float nv = std::abs(RadeonRays::dot(V, N)) + 0.1f;
			float vh = saturate(RadeonRays::dot(V, L));
			float nh = saturate(RadeonRays::dot(H, N));

			float m = roughness * roughness;
			float m2 = m * m;
			float spec = (nh * m2 - nh) * nh + 1.0f;
			float D = m2 / (spec * spec) * PI;

			float Gv = nl * (nv * (1 - m) + m);
			float Gl = nv * (nl * (1 - m) + m);
			float G = 0.5f / (Gv + Gl);

			float Fc = pow5(1 - nv);
			RadeonRays::float3 Fr = f0 * (1 - Fc) + RadeonRays::float3(Fc, Fc, Fc);
			RadeonRays::float3 Ft = RadeonRays::float3(1.0f, 1.0f, 1.0f) - Fr;

			return Ft * G * nl * (4 * nl * nv);
		}

		return 0;
	}

	RadeonRays::float3 TangentToWorld(const RadeonRays::float3& H, const RadeonRays::float3& N)
	{
		RadeonRays::float3 Y = std::abs(N.z) < 0.999f ? RadeonRays::float3(0, 0, 1) : RadeonRays::float3(1, 0, 0);
		RadeonRays::float3 X = RadeonRays::normalize(RadeonRays::cross(Y, N));
		return RadeonRays::normalize(X * H.x + cross(N, X) * H.y + N * H.z);
	}

	RadeonRays::float3 LobeDirection(const RadeonRays::float3& n, float roughness, const RadeonRays::float2& Xi)
	{
		auto H = ImportanceSampleGGX(Xi, roughness);
		return TangentToWorld(H, n);
	}

	RadeonRays::float3 CosineDirection(const RadeonRays::float3& n, const RadeonRays::float2& Xi)
	{
		auto H = CosineSampleHemisphere(Xi);
		return TangentToWorld(H, n);
	}

	RadeonRays::float3 bsdf(const RadeonRays::float3& V, RadeonRays::float3 N, float roughness, float metalness, float ior, const RadeonRays::float2& Xi)
	{
		if (RadeonRays::dot(N, V) > 0.0f)
			N = -N;

		if (Xi.x <= lerp(0.04f, 1.0f, metalness))
			return LobeDirection(RadeonRays::normalize(reflect(V, N)), roughness, Xi);

		if (ior > 1.0f)
			return RadeonRays::normalize(refract(V, N, 1.0f / ior));

		return CosineDirection(N, Xi);
	}

	RadeonRays::float3 bsdf_weight(const RadeonRays::float3& V, const RadeonRays::float3& N, const RadeonRays::float3& L, const RadeonRays::float3& f0, float roughness, float metalness, float ior, const RadeonRays::float2& Xi)
	{
		if (Xi.x <= lerp(0.04f, 1.0f, metalness))
			return SpecularBRDF_GGX(N, L, -V, f0, roughness);

		if (ior > 1.0f)
			return SpecularBTDF_GGX(N, L, -V, f0, roughness);

		return DiffuseBRDF(N, L, -V, roughness);
	}
}

#endif