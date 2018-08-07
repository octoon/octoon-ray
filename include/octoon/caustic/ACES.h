#ifndef OCTOON_CAUSTIC_ACES_H_
#define OCTOON_CAUSTIC_ACES_H_

#include <octoon/caustic/tonemapping.h>

namespace octoon
{
	namespace caustic
	{
		class ACES final : public Tonemapping
		{
		public:
			ACES() noexcept;
			virtual ~ACES() noexcept;

			virtual float map(float x) noexcept;

		private:
			ACES(const ACES&) noexcept;
			ACES& operator=(const ACES&) noexcept;
		};
	}
}

#endif