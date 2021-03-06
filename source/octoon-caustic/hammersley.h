#ifndef OCTOON_CAUSTIC_HAMMERSLEY
#define OCTOON_CAUSTIC_HAMMERSLEY

#include <octoon/caustic/sequences.h>

namespace octoon
{
	namespace caustic
	{
		class Hammersley final : public Sequences
		{
		public:
			Hammersley() noexcept;
			Hammersley(std::uint32_t maxSamples) noexcept;
			~Hammersley() noexcept;

			float sample(std::uint32_t dimension, std::uint32_t index) const noexcept override;
			float sample(std::uint32_t dimension, std::uint32_t index, std::uint32_t seed) const noexcept;

		private:
			std::uint32_t maxSamples_;
		};
	}
}

#endif