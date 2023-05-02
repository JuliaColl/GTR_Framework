#include "renderer.h"

#include <algorithm> //sort

#include "camera.h"
#include "../gfx/gfx.h"
//#include "../gfx/shader.h"
#include "../gfx/mesh.h"
#include "../gfx/texture.h"
#include "../gfx/fbo.h"
#include "../pipeline/prefab.h"
#include "../pipeline/material.h"
#include "../pipeline/animation.h"
#include "../utils/utils.h"
#include "../extra/hdre.h"
#include "../core/ui.h"

#include "scene.h"


using namespace SCN;

//some globals
GFX::Mesh sphere;

bool SCN::RenderCall::CompareAlphaAndDistance(RenderCall rc1, RenderCall rc2)
{
	if (rc1.material->alpha_mode == eAlphaMode::NO_ALPHA && rc2.material->alpha_mode != eAlphaMode::NO_ALPHA)
		return true;
	else if (rc2.material->alpha_mode == eAlphaMode::NO_ALPHA && rc1.material->alpha_mode != eAlphaMode::NO_ALPHA)
		return false;
	return (rc1.distance_to_camera > rc2.distance_to_camera);
}


Renderer::Renderer(const char* shader_atlas_filename)
{
	render_wireframe = false;
	render_boundaries = false;
	show_shadowmaps = false;
	show_specular = false;
	render_mode = eRenderMode::MULTIPASS;
	scene = nullptr;
	skybox_cubemap = nullptr;

	if (!GFX::Shader::LoadAtlas(shader_atlas_filename))
		exit(1);
	GFX::checkGLErrors();

	sphere.createSphere(1.0f);
	sphere.uploadToVRAM();
}

void Renderer::setupScene(Camera* camera)
{
	if (scene->skybox_filename.size())
		skybox_cubemap = GFX::Texture::Get(std::string(scene->base_folder + "/" + scene->skybox_filename).c_str());
	else
		skybox_cubemap = nullptr;

	render_calls.clear();
	lights.clear();
	//process entities
	for (int i = 0; i < scene->entities.size(); ++i)
	{
		BaseEntity* ent = scene->entities[i];
		if (!ent->visible)
			continue;

		//is a prefab!
		if (ent->getType() == eEntityType::PREFAB)
		{
			PrefabEntity* pent = (SCN::PrefabEntity*)ent;
			if (pent->prefab)
				storeNode(&pent->root, camera);
		}
		else if (ent->getType() == eEntityType::LIGHT)
		{
			LightEntity* light = (SCN::LightEntity*)ent;
			lights.push_back(light);
		}
	}

	std::sort(render_calls.begin(), render_calls.end(), SCN::RenderCall::CompareAlphaAndDistance);


	generateShadowmaps();

}


void Renderer::renderScene(SCN::Scene* scene, Camera* camera)
{
	this->scene = scene;
	setupScene(camera);

	renderFrameCall(scene, camera);

	//debug
	if (show_shadowmaps)
		debugShadowmaps();
}

void Renderer::renderFrame(SCN::Scene* scene, Camera* camera)
{
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);

	//set the camera as default (used by some functions in the framework)
	camera->enable();

	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GFX::checkGLErrors();

	//render skybox
	if (skybox_cubemap && render_mode != eRenderMode::FLAT)
		renderSkybox(skybox_cubemap);
	

	//render entities
	for (int i = 0; i < scene->entities.size(); ++i)
	{
		BaseEntity* ent = scene->entities[i];
		if (!ent->visible)
			continue;

		//is a prefab!
		if (ent->getType() == eEntityType::PREFAB)
		{
			PrefabEntity* pent = (SCN::PrefabEntity*)ent;
			if (pent->prefab)
				renderNode(&pent->root, camera);
		}
	}
}


void Renderer::renderFrameCall(SCN::Scene* scene, Camera* camera) {

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);

	//set the camera as default (used by some functions in the framework)
	camera->enable();

	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GFX::checkGLErrors();

	//render skybox
	if (skybox_cubemap && render_mode != eRenderMode::FLAT)
		renderSkybox(skybox_cubemap);
	
	for (int i = 0; i < render_calls.size(); i++) {
		switch (render_mode)
		{
		case eRenderMode::FLAT: renderMeshWithMaterialFlat(render_calls[i].model, render_calls[i].mesh, render_calls[i].material); break;
		case eRenderMode::TEXTURED: renderMeshWithMaterial(render_calls[i].model, render_calls[i].mesh, render_calls[i].material); break;
		case eRenderMode::MULTIPASS: renderMeshWithMaterialMultiPass(render_calls[i].model, render_calls[i].mesh, render_calls[i].material); break;
		case eRenderMode::SINGLEPASS:renderMeshWithMaterialSinglePass(render_calls[i].model, render_calls[i].mesh, render_calls[i].material); break;
		}
	}
}



void Renderer::renderSkybox(GFX::Texture* cubemap)
{
	Camera* camera = Camera::current;

	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	GFX::Shader* shader = GFX::Shader::Get("skybox");
	if (!shader)
		return;
	shader->enable();

	Matrix44 m;
	m.setTranslation(camera->eye.x, camera->eye.y, camera->eye.z);
	m.scale(10, 10, 10);
	shader->setUniform("u_model", m);
	cameraToShader(camera, shader);
	shader->setUniform("u_texture", cubemap, 0);
	sphere.render(GL_TRIANGLES);
	shader->disable();
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_DEPTH_TEST);
}

//renders a node of the prefab and its children
void Renderer::renderNode(SCN::Node* node, Camera* camera)
{
	if (!node->visible)
		return;

	//compute global matrix
	Matrix44 node_model = node->getGlobalMatrix(true);

	//does this node have a mesh? then we must render it
	if (node->mesh && node->material)
	{
		//compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
		BoundingBox world_bounding = transformBoundingBox(node_model, node->mesh->box);

		//if bounding box is inside the camera frustum then the object is probably visible
		if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize))
		{
			if (render_boundaries)
				node->mesh->renderBounding(node_model, true);
			//renderMeshWithMaterialFlat(node_model, node->mesh, node->material);

			switch (render_mode)
			{
			case eRenderMode::FLAT: renderMeshWithMaterialFlat(node_model, node->mesh, node->material); break;
			case eRenderMode::TEXTURED: renderMeshWithMaterial(node_model, node->mesh, node->material); break;
			case eRenderMode::MULTIPASS: renderMeshWithMaterialMultiPass(node_model, node->mesh, node->material); break;
			case eRenderMode::SINGLEPASS:renderMeshWithMaterialSinglePass(node_model, node->mesh, node->material); break;
			}
		}
	}

	//iterate recursively with children
	for (int i = 0; i < node->children.size(); ++i)
		renderNode(node->children[i], camera);
}
//store a node of the prefab and its children
void Renderer::storeNode(SCN::Node* node, Camera* camera)
{
	if (!node->visible)
		return;

	//compute global matrix
	Matrix44 node_model = node->getGlobalMatrix(true);

	//does this node have a mesh? then we must render it
	if (node->mesh && node->material)
	{
		//compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
		BoundingBox world_bounding = transformBoundingBox(node_model, node->mesh->box);

		//if bounding box is inside the camera frustum then the object is probably visible
		if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize))
		{
			if (render_boundaries)
				node->mesh->renderBounding(node_model, true);

			//instead of render, we store it
			//renderMeshWithMaterial(node_model, node->mesh, node->material);
			SCN::RenderCall rc;
			Vector3f nodepos = node_model.getTranslation();
			rc.mesh = node->mesh;
			rc.material = node->material;
			rc.model = node_model;
			rc.distance_to_camera = camera->eye.distance(nodepos);
			render_calls.push_back(rc);
		}
	}

	//iterate recursively with children
	for (int i = 0; i < node->children.size(); ++i)
		storeNode(node->children[i], camera);
}

void Renderer::renderMeshWithMaterialFlat(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	Camera* camera = Camera::current;

	//select the blending
	if (material->alpha_mode == SCN::eAlphaMode::BLEND)
		return;
	
	glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if (material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	glEnable(GL_DEPTH_TEST);

	//chose a shader
	shader = GFX::Shader::Get("flat");

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_model", model);
	cameraToShader(camera, shader);
	
	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	//shader->setUniform("u_alpha_cutoff", material->alpha_mode == SCN::eAlphaMode::MASK ? material->alpha_cutoff : 0.001f);

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();

}
//renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material )
		return;
    assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	GFX::Texture* texture = NULL;
	Camera* camera = Camera::current;
	
	texture = material->textures[SCN::eTextureChannel::ALBEDO].texture;
	//texture = material->emissive_texture;
	//texture = material->metallic_roughness_texture;
	//texture = material->normal_texture;
	//texture = material->occlusion_texture;
	if (texture == NULL)
		texture = GFX::Texture::getWhiteTexture(); //a 1x1 white texture

	//select the blending
	if (material->alpha_mode == SCN::eAlphaMode::BLEND)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
		glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if(material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
    assert(glGetError() == GL_NO_ERROR);

	glEnable(GL_DEPTH_TEST);

	//chose a shader
	shader = GFX::Shader::Get("texture");

    assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_model", model);
	cameraToShader(camera, shader);
	float t = getTime();
	shader->setUniform("u_time", t );

	shader->setUniform("u_color", material->color);
	if(texture)
		shader->setUniform("u_texture", texture, 0);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == SCN::eAlphaMode::MASK ? material->alpha_cutoff : 0.001f);

	if (render_wireframe)
		glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
}

void SCN::Renderer::renderMeshWithMaterialMultiPass(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{

	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	Camera* camera = Camera::current;

	
	//select the blending
	if (material->alpha_mode == SCN::eAlphaMode::BLEND)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
		glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if (material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	glEnable(GL_DEPTH_TEST);

	//chose a shader
	shader = GFX::Shader::Get("multi_pass");

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_model", model);
	cameraToShader(camera, shader);
	float t = getTime();
	shader->setUniform("u_time", t);


	uplodadMaterialUniforms(shader, material);
	
	
	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	
	glDepthFunc(GL_LEQUAL);  //Render if the z is less or equal than the current one

	// lights
	shader->setUniform("u_ambient_light", scene->ambient_light);
	shader->setUniform("u_show_specular", show_specular);

	visibleLights.clear();

	for (int i = 0; i < lights.size(); i++)
	{
		LightEntity* light = lights[i];

		if(light->light_type == eLightType::DIRECTIONAL)
			visibleLights.push_back(light);
		else
		{
			vec3 center = light->root.model.getTranslation();
			float radius = light->max_distance;

			BoundingBox world_bounding = transformBoundingBox(model, mesh->box);

			if (BoundingBoxSphereOverlap(world_bounding, center, radius) )
				visibleLights.push_back(light);
		}
	}


	if (visibleLights.size() == 0)
	{
		shader->setUniform("u_light_info", vec4((int) eLightType::NO_LIGHT, 0, 0, 0));
		mesh->render(GL_TRIANGLES);

	}
	else
	{
		for (int i = 0; i < visibleLights.size(); i++)
		{
			LightEntity* light = visibleLights[i];
			shader->setUniform("u_light_position", light->root.model.getTranslation());
			shader->setUniform("u_light_front", light->root.model.rotateVector(vec3(0,0,1)) ); //we pass the forward vector  
			shader->setUniform("u_light_color", light->color * light->intensity);
			shader->setUniform("u_light_info",vec4((int)light->light_type, light->near_distance, light->max_distance, 0));

			shader->setUniform("u_shadow_params", vec2((light->shadowmap && light->cast_shadows) ? 1:0, light->shadow_bias));
			if (light->shadowmap && light->cast_shadows)
			{
				shader->setTexture("u_shadowmap", light->shadowmap, 8);
				shader->setUniform("u_shadow_viewproj", light->shadow_viewproj);

			}

			if (light->light_type == eLightType::SPOT )
				shader->setUniform("u_light_cone", vec2( cos( light->cone_info.x * DEG2RAD ), cos(light->cone_info.y * DEG2RAD)));

			//do the draw call that renders the mesh into the screen
			mesh->render(GL_TRIANGLES);

			glEnable(GL_BLEND); //additive (the blending property)
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);

			shader->setUniform("u_ambient_light", vec3(0.0));
			shader->setUniform("u_emissive_factor", vec3(0.0));

		}
	}
	
	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDepthFunc(GL_LESS);  

}


void SCN::Renderer::renderMeshWithMaterialSinglePass(const Matrix44 model, GFX::Mesh* mesh, SCN::Material* material)
{

	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	GFX::Shader* shader = NULL;
	GFX::Texture* texture = NULL;
	Camera* camera = Camera::current;



	//select the blending
	if (material->alpha_mode == SCN::eAlphaMode::BLEND)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
		glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if (material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	glEnable(GL_DEPTH_TEST);

	//chose a shader
	shader = GFX::Shader::Get("single_pass");

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_model", model);
	cameraToShader(camera, shader);
	float t = getTime();
	shader->setUniform("u_time", t);

	uplodadMaterialUniforms(shader, material);


	//Lights
	shader->setUniform("u_ambient_light", scene->ambient_light);
	shader->setUniform("u_show_specular", show_specular);

	Vector3f light_position[MAX_LIGHTS];
	Vector3f light_color[MAX_LIGHTS];
	Vector3f light_front[MAX_LIGHTS];
	Vector3f light_info[MAX_LIGHTS];
	Vector2f light_cone[MAX_LIGHTS];


	for (int i = 0; i < lights.size(); i++) {
		LightEntity* light = lights[i];
		light_position[i] = light->root.model.getTranslation();
		light_color[i] = light->color * light->intensity;
		light_front[i] = light->root.model.rotateVector(vec3(0, 0, 1));
		light_info[i] = vec3((int)light->light_type, light->near_distance, light->max_distance);
		if (light->light_type == eLightType::SPOT)
			light_cone[i] = vec2(cos(light->cone_info.x * DEG2RAD), cos(light->cone_info.y * DEG2RAD));


	}

	shader->setUniform3Array("u_light_position", (float*)&light_position, MAX_LIGHTS);
	shader->setUniform3Array("u_light_color", (float*)&light_color, MAX_LIGHTS);
	shader->setUniform3Array("u_light_front", (float*)&light_front, MAX_LIGHTS);
	shader->setUniform3Array("u_light_info", (float*)&light_info, MAX_LIGHTS);
	shader->setUniform2Array("u_light_cone", (float*)&light_cone, MAX_LIGHTS);

	shader->setUniform1("u_num_lights", (int) lights.size());

	if (render_wireframe)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

}

void SCN::Renderer::uplodadMaterialUniforms(GFX::Shader* shader, Material* material)
{
	GFX::Texture* white = GFX::Texture::getWhiteTexture(); //a 1x1 white texture that we can use when other textures are null;

	GFX::Texture* albedo_texture = material->textures[SCN::eTextureChannel::ALBEDO].texture;
	GFX::Texture* emissive_texture = material->textures[SCN::eTextureChannel::EMISSIVE].texture;
	GFX::Texture* metalic_roughness_texture = material->textures[SCN::eTextureChannel::METALLIC_ROUGHNESS].texture;
	GFX::Texture* normalmap_texture = material->textures[SCN::eTextureChannel::NORMALMAP].texture;

	shader->setUniform("u_color", material->color);
	shader->setUniform("u_emissive_factor", material->emissive_factor);  //is a vector 3 (not a factor)
	shader->setUniform("u_albedo_texture", albedo_texture ? albedo_texture : white, 0);
	shader->setUniform("u_emissive_texture", emissive_texture ? emissive_texture : white, 1);
	shader->setUniform("u_metalic_roughness_texture", metalic_roughness_texture ? metalic_roughness_texture : white, 2);  //TODO check white texture
	shader->setUniform("u_normalmap", normalmap_texture ? normalmap_texture : white, 3);


	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == SCN::eAlphaMode::MASK ? material->alpha_cutoff : 0.001f);

}



void SCN::Renderer::generateShadowmaps()
{
	Camera camera;

	GFX::startGPULabel("Shadowmaps");

	eRenderMode prev = render_mode;
	render_mode = eRenderMode::FLAT;

	for (auto light : lights) 
	{
		if (!light->cast_shadows)
			continue;

		if (light->light_type == eLightType::POINT || light->light_type == eLightType::NO_LIGHT )
			continue;

		// check if light inside camera
		// TODO

		if(!light->shadowmap_fbo)
		{
			light->shadowmap_fbo = new GFX::FBO();
			light->shadowmap_fbo->setDepthOnly(1024, 1024);
			light->shadowmap = light->shadowmap_fbo->depth_texture;
		}

		vec3 pos = light->root.model.getTranslation();
		vec3 front = light->root.model.rotateVector(vec3(0, 0, -1));
		vec3 up = vec3(0, 1, 0);

		vec3 cross = up.cross(front);
		if (cross.x == 0.0 && cross.y == 0.0 && cross.z == 0.0)
			up = vec3(1, 0, 0);
		
		camera.lookAt(pos, pos + front, up);

		if (light->light_type == eLightType::SPOT)
			camera.setPerspective(light->cone_info.y * 2, 1.0, light->near_distance, light->max_distance);
		
		if(light->light_type == eLightType::DIRECTIONAL)
		{
			//use light area to define how big the frustum is
			float halfarea = light->area / 2;

			camera.setOrthographic(-halfarea, halfarea, halfarea , -halfarea, 0.1, light->max_distance);
		}

		light->shadowmap_fbo->bind();

		renderFrame(scene, &camera);

		light->shadowmap_fbo->unbind();

		light->shadow_viewproj = camera.viewprojection_matrix;
	}

	render_mode = prev;

	GFX::endGPULabel();

}

void SCN::Renderer::debugShadowmaps()
{
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	int x = 310;
	for (auto light : lights)
	{
		if (!light->shadowmap)
			continue;

		GFX::Shader* shader = GFX::Shader::getDefaultShader("linear_depth");
		shader->enable();
		shader->setUniform("u_camera_nearfar", vec2(light->near_distance, light->max_distance));
		glViewport(x, 100, 256, 256);
		light->shadowmap->toViewport(shader);
		x += 260;
	}

	vec2 size = CORE::getWindowSize();
	glViewport(0, 0, size.x, size.y);

}

void SCN::Renderer::cameraToShader(Camera* camera, GFX::Shader* shader)
{
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix );
	shader->setUniform("u_camera_position", camera->eye);
}

#ifndef SKIP_IMGUI

void Renderer::showUI()
{
		
	ImGui::Checkbox("Wireframe", &render_wireframe);
	ImGui::Checkbox("Boundaries", &render_boundaries);


	//add here your stuff
	ImGui::Checkbox("Show Shadowmaps", &show_shadowmaps);
	ImGui::Checkbox("Show Specular", &show_specular);

	ImGui::Combo("Render Mode", (int*)&render_mode, "FLAT\0TEXTURED\0MULTIPASS\0SINGLEPASS", 4);

}

#else
void Renderer::showUI() {}
#endif

