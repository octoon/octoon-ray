#ifndef OCTOON_CAUSTIC_CAMERA_H_
#define OCTOON_CAUSTIC_CAMERA_H_

#include <octoon/caustic/render_object.h>

namespace octoon
{
	namespace caustic
	{
		class Camera : public RenderObject
		{
		public:
			Camera() noexcept;
			virtual ~Camera() noexcept;

		private:
			Camera(const Camera&) noexcept = delete;
			Camera& operator=(const Camera&) noexcept = delete;
		};
	}
}

#endif