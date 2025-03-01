#pragma once

#include "scene.h"
#include "../gfx/fbo.h"

namespace SCN {

	enum eLightType : uint32 {
		NO_LIGHT = 0,
		POINT = 1,
		SPOT = 2,
		DIRECTIONAL = 3
	};

	class LightEntity : public BaseEntity
	{
	public:

		eLightType light_type;
		float intensity;
		vec3 color;
		float near_distance;
		float max_distance;
		bool cast_shadows;
		float shadow_bias;
		vec2 cone_info;  // min, max
		float area; //for direct;

		//rendering
		GFX::FBO* shadowmap_fbo;
		GFX::Texture* shadowmap;
		mat4 shadow_viewproj;

		ENTITY_METHODS(LightEntity, LIGHT, 14,4);

		LightEntity();
		~LightEntity();

		void configure(cJSON* json);
		void serialize(cJSON* json);
	};

};
