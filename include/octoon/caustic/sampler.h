#ifndef OCTOON_CAUSTIC_SAMPLER
#define OCTOON_CAUSTIC_SAMPLER

#include <algorithm>
#include <octoon/caustic/texture.h>

namespace octoon
{
    namespace caustic
    {
        template<class T>
        class Interpolation
        {
        public:
			enum class Wrap : std::uint8_t
			{
				Repeat = 1,
				Mirror = 2,
				ClampToEdge = 3,
			};

		public:
			Interpolation() noexcept : wrapMode_(Wrap::ClampToEdge) {}
			virtual ~Interpolation() noexcept {}

			void setWrapMode(Wrap mode) { wrapMode_ = mode; }
			Wrap getWrapMode() const { return wrapMode_; }

		public:
            virtual T sample(const Texture1D<T>& texels, float u, float lod) noexcept = 0;
            virtual T sample(const Texture2D<T>& texels, float u, float v, float lod) noexcept = 0;
            virtual T sample(const Texture2DArray<T>& texels, float u, float v, float w, float lod) noexcept = 0;
            virtual T sample(const Texture3D<T>& texels, float u, float v, float w, float lod) noexcept = 0;

		private:
			Wrap wrapMode_;
		};

        template<class T>
        class NearestNeighborInterpolation : public Interpolation<T>
        {
        public:
            virtual T sample(const Texture1D<T>& texels, float u, float lod) noexcept
            {
				switch (this->getWrapMode())
				{
				case Wrap::Repeat:
				{
					auto x = u - std::floor(u);
					return texels.fetch((int)std::round(x * texels.width()));
				}
				break;
				case Wrap::Mirror:
				{
					auto iu = ((std::uint32_t)std::floor(u));
					auto x = (iu % 2) > 0 ? 1.0f - (u - iu) : (u - iu);
					return texels.fetch((int)std::round(x * texels.width()));
				}
				break;
				case Wrap::ClampToEdge:
				{
					auto x = clamp(u, 0.f, 1.f);
					return texels.fetch((int)std::round(x * texels.width()));
				}
				break;
				}

                return n;
            }

            virtual T sample(const Texture2D<T>& texels, float u, float v, float lod) noexcept
            {
				switch (this->getWrapMode())
				{
				case Wrap::Repeat:
				{
					auto x = u - std::floor(u);
					auto y = v - std::floor(v);
					return texels.fetch((int)std::round(x * texels.width()), (int)std::round(y * texels.height()));
				}
				break;
				case Wrap::Mirror:
				{
					auto iu = ((std::uint32_t)std::floor(u));
					auto iv = ((std::uint32_t)std::floor(v));
					auto x = (iu % 2) > 0 ? 1.0f - (u - iu) : (u - iu);
					auto y = (iv % 2) > 0 ? 1.0f - (v - iv) : (v - iv);
					return texels.fetch((int)std::round(x * texels.width()), (int)std::round(y * texels.height()));
				}
				break;
				case Wrap::ClampToEdge:
				{
					auto x = clamp(u, 0.f, 1.f);
					auto y = clamp(v, 0.f, 1.f);
					return texels.fetch((int)std::round(x * texels.width()), (int)std::round(y * texels.height()));
				}
				break;
				}

                return n;
            }

            virtual T sample(const Texture2DArray<T>& texels, float u, float v, float layer, float lod) noexcept
            {
            	throw std::runtime_error("Not implemented yet.");
            }

            virtual T sample(const Texture3D<T>& texels, float u, float v, float w, float lod) noexcept
            {
				switch (this->getWrapMode())
				{
				case Wrap::Repeat:
				{
					auto x = u - std::floor(u);
					auto y = v - std::floor(v);
					auto z = z - std::floor(z);
					return texels.fetch((int)std::round(x), (int)std::round(y), (int)std::round(z));
				}
				break;
				case Wrap::Mirror:
				{
					auto iu = ((std::uint32_t)std::floor(u));
					auto iv = ((std::uint32_t)std::floor(v));
					auto iw = ((std::uint32_t)std::floor(w));
					auto x = (iu % 2) > 0 ? 1.0f - (u - iu) : (u - iu);
					auto y = (iv % 2) > 0 ? 1.0f - (v - iv) : (v - iv);
					auto z = (iw % 2) > 0 ? 1.0f - (w - iv) : (w - iv);
					return texels.fetch((int)std::round(x), (int)std::round(y), (int)std::round(z));
				}
				break;
				case Wrap::ClampToEdge:
				{
					auto x = clamp(u, 0.f, 1.f);
					auto y = clamp(v, 0.f, 1.f);
					auto z = clamp(w, 0.f, 1.f);
					return texels.fetch((int)std::round(x), (int)std::round(y), (int)std::round(z));
				}
				break;
				}
            }
        };

		template<class T>
        class BilinearInterpolation : public Interpolation<T>
        {
        public:
            virtual T sample(const Texture1D<T>& texels, float u, float lod) noexcept
            {
				// normalization
				switch (this->getWrapMode())
				{
				case Wrap::Repeat:
				{
					u = u - std::floor(u);
				}
				break;
				case Wrap::Mirror:
				{
					auto iu = ((std::uint32_t)std::floor(u));
					u = (iu % 2) > 0 ? 1.0f - (u - iu) : (u - iu);
				}
				break;
				case Wrap::ClampToEdge:
				{
					u = clamp(u, 0.f, 1.f);
				}
				break;
				}

				float x = clamp(u, 0.f, 1.f) * texels.width();
				float x2 = x >= texels.width() - 1 ? texels.width() - 1 : x + 1;

				float xw = frac(x);

				// bilinear interpolation
				float xw1 = 1.0f - xw;
				float xw2 = xw;

				auto v1 = texels.fetch((int)std::round(x));
				auto v2 = texels.fetch((int)std::round(x2));

				v1 = v1 * xw1;
				v2 = v2 * xw2;

				return v1 + v2;
            }

            virtual T sample(const Texture2D<T>& texels, float u, float v, float lod) noexcept
            {
				// normalization
				switch (this->getWrapMode())
				{
				case Wrap::Repeat:
				{
					u = u - std::floor(u);
					v = v - std::floor(v);
				}
				break;
				case Wrap::Mirror:
				{
					auto iu = ((std::uint32_t)std::floor(u));
					auto iv = ((std::uint32_t)std::floor(v));
					u = (iu % 2) > 0 ? 1.0f - (u - iu) : (u - iu);
					v = (iv % 2) > 0 ? 1.0f - (v - iv) : (v - iv);
				}
				break;
				case Wrap::ClampToEdge:
				{
					u = clamp(u, 0.f, 1.f);
					v = clamp(v, 0.f, 1.f);
				}
				break;
				}

				float x = u * texels.width();
				float y = v * texels.height();
				float x2 = x >= texels.width() - 1 ? texels.width() - 1 : x + 1;
				float y2 = y >= texels.height() - 1 ? texels.height() - 1 : y + 1;

				float xw = frac(x);
				float yw = frac(y);

				// bilinear interpolation
				float xw1 = 1.0f - xw;
				float xw2 = xw;
				float yw1 = 1.0f - yw;
				float yw2 = yw;

				auto v1 = texels.fetch((int)std::round(x), (int)std::round(y));
				auto v2 = texels.fetch((int)std::round(x2), (int)std::round(y));
				auto v3 = texels.fetch((int)std::round(x), (int)std::round(y2));
				auto v4 = texels.fetch((int)std::round(x2), (int)std::round(y2));

				v1 = v1 * xw1 * yw1;
				v2 = v2 * xw2 * yw1;
				v3 = v3 * xw1 * yw2;
				v4 = v4 * xw2 * yw2;

				return v1 + v2 + v3 + v4;
            }

            virtual T sample(const Texture2DArray<T>& texels, float u, float v, float layer, float lod) noexcept
            {
            	throw std::runtime_error("Not implemented yet.");
            }

            virtual T sample(const Texture3D<T>& texels, float u, float v, float w, float lod) noexcept
            {
				throw std::runtime_error("Not implemented yet.");
            }
        };

		template<class T>
		class Sampler : public Interpolation<T>
		{
		public:
			enum class Filter : std::uint8_t
			{
				Nearest = 0,
				Linear = 1,
				NearestMipmapLinear = 2,
				NearestMipmapNearest = 3,
				LinearMipmapNearest = 4,
				LinearMipmapLinear = 5,
				Anisotropic = 6
			};

		public:
			Sampler() noexcept {}
			virtual ~Sampler() noexcept {}

			void setMinFilter(Filter filter) noexcept(false)
			{
				switch (filter)
				{
				case Filter::Nearest:              minFilter_ = std::make_unique<NearestNeighborInterpolation>(); break;
				case Filter::Linear:               throw std::runtime_error("Not implemented yet.");
				case Filter::NearestMipmapLinear:  throw std::runtime_error("Not implemented yet.");
				case Filter::NearestMipmapNearest: throw std::runtime_error("Not implemented yet.");
				case Filter::LinearMipmapNearest:  throw std::runtime_error("Not implemented yet.");
				case Filter::LinearMipmapLinear:   throw std::runtime_error("Not implemented yet.");
				default:
					throw std::runtime_error("Invalid filter");
				}
			}

			void setMagFilter(Filter filter) noexcept(false)
			{
				switch (filter)
				{
				case Filter::Nearest:              magFilter_ = std::make_unique<NearestNeighborInterpolation>(); break;
				case Filter::Linear:               throw std::runtime_error("Not implemented yet.");
				case Filter::NearestMipmapLinear:  throw std::runtime_error(R"(Cannot use "NearestMipmapLinear" method directly on this function)");
				case Filter::NearestMipmapNearest: throw std::runtime_error(R"(Cannot use "NearestMipmapNearest" method directly on this function)");
				case Filter::LinearMipmapNearest:  throw std::runtime_error(R"(Cannot use "LinearMipmapNearest" method directly on this function)");
				case Filter::LinearMipmapLinear:   throw std::runtime_error(R"(Cannot use "LinearMipmapLinear" method directly on this function)");
				default:
					throw std::runtime_error("Invalid filter");
				}
			}

		public:
			T sample(const Texture1D<T>& texels, float u, float lod) noexcept override
			{
				if (lod > 0)
					return minFilter_->sample(texels, u, lod);
				else
					return magFilter_->sample(texels, u, 0);
			}

			T sample(const Texture2D<T>& texels, float u, float v, float lod) noexcept override
			{
				if (lod > 0)
					return minFilter_->sample(texels, u, v, lod);
				else
					return magFilter_->sample(texels, u, v, 0);
			}

			T sample(const Texture2DArray<T>& texels, float u, float v, float layer, float lod) noexcept override
			{
				if (lod > 0)
					return minFilter_->sample(texels, u, v, layer, lod);
				else
					return magFilter_->sample(texels, u, v, layer, 0);
			}

			T sample(const Texture3D<T>& texels, float u, float v, float w, float lod) noexcept override
			{
				if (lod > 0)
					return minFilter_->sample(texels, u, v, w, lod);
				else
					return magFilter_->sample(texels, u, v, w, 0);
			}

			T sample(const Texture1D<T>& texels, float u, float dx, float dy) noexcept
			{
				float d = std::max(dx, dy);
				float lod = std::log2(d * d) * 0.5f;
				return this->sample(texels, u, lod);
			}

			T sample(const Texture2D<T>& texels, float u, float v, float dx, float dy) noexcept
			{
				float d = std::max(dx, dy);
				float lod = std::log2(d * d) * 0.5f;
				return this->sample(texels, u, v, lod);
			}

			T sample(const Texture2DArray<T>& texels, float u, float v, float layer, float dx, float dy) noexcept
			{
				float d = std::max(dx, dy);
				float lod = std::log2(d * d) * 0.5f;
				return this->sample(texels, u, v, layer, lod);
			}

			T sample(const Texture3D<T>& texels, float u, float v, float w, float dx, float dy) noexcept
			{
				float d = std::max(dx, dy);
				float lod = std::log2(d * d) * 0.5f;
				return this->sample(texels, u, v, w, lod);
			}

		private:
			std::unique_ptr<Interpolation<T>> minFilter_;
			std::unique_ptr<Interpolation<T>> magFilter_;
		};
    }
}

#endif