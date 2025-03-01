#pragma once
#include "scene.h"
#include "prefab.h"
#include "../gfx/shader.h"

#include "light.h"

#define MAX_LIGHTS 4
//forward declarations
class Camera;
class Skeleton;
namespace GFX {
	class Shader;
	class Mesh;
	class FBO;
}

namespace SCN {

	class Prefab;
	class Material;

	class RenderCall {
	public:
		GFX::Mesh* mesh;
		SCN::Material* material;
		Matrix44 model;

		float distance_to_camera;
		static bool CompareAlphaAndDistance(RenderCall rc1, RenderCall rc2);

	};

	enum eRenderMode{
		FLAT,
		TEXTURED,
		MULTIPASS,
		SINGLEPASS
	};

	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{
	public:
		bool render_wireframe;
		bool render_boundaries;
		bool show_shadowmaps;
		bool show_specular;
		eRenderMode render_mode;

		GFX::Texture* skybox_cubemap;

		SCN::Scene* scene;

		std::vector<RenderCall> render_calls; //to store the nodes by sort them by distance

		std::vector<LightEntity*> lights;
		std::vector<LightEntity*> visibleLights;

		//updated every frame
		Renderer(const char* shaders_atlas_filename );

		//just to be sure we have everything ready for the rendering
		void setupScene(Camera* camera);

		//add here your functions
		//...

		//renders several elements of the scene
		void renderScene(SCN::Scene* scene, Camera* camera);
		void renderFrameCall(SCN::Scene* scene, Camera* camera);
		void renderFrame(SCN::Scene* scene, Camera* camera);


		//render the skybox
		void renderSkybox(GFX::Texture* cubemap);
	
		//to render one node from the prefab and its children
		void renderNode(SCN::Node* node, Camera* camera);
		void storeNode(SCN::Node* node, Camera* camera);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);
		void renderMeshWithMaterialFlat(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);
		void renderMeshWithMaterialMultiPass(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);
		void renderMeshWithMaterialSinglePass(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material);

		void uplodadMaterialUniforms(GFX::Shader* shader, Material* material);

		void generateShadowmaps();
		void debugShadowmaps(); 

		void showUI();

		void cameraToShader(Camera* camera, GFX::Shader* shader); //sends camera uniforms to shader
	};

};