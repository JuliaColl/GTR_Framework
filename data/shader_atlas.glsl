//example of some shaders compiled
flat basic.vs flat.fs
texture basic.vs texture.fs
multi_pass basic.vs multi_pass.fs
single_pass basic.vs single_pass.fs
skybox basic.vs skybox.fs
depth quad.vs depth.fs
multi basic.vs multi.fs

\basic.vs

#version 330 core

in vec3 a_vertex;
in vec3 a_normal;
in vec2 a_coord;
in vec4 a_color;

uniform vec3 u_camera_pos;

uniform mat4 u_model;
uniform mat4 u_viewprojection;

//this will store the color for the pixel shader
out vec3 v_position;
out vec3 v_world_position;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_color;

uniform float u_time;

void main()
{	
	//calcule the normal in camera space (the NormalMatrix is like ViewMatrix but without traslation)
	v_normal = (u_model * vec4( a_normal, 0.0) ).xyz;
	
	//calcule the vertex in object space
	v_position = a_vertex;
	v_world_position = (u_model * vec4( v_position, 1.0) ).xyz;
	
	//store the color in the varying var to use it from the pixel shader
	v_color = a_color;

	//store the texture coordinates
	v_uv = a_coord;

	//calcule the position of the vertex using the matrices
	gl_Position = u_viewprojection * vec4( v_world_position, 1.0 );
}

\quad.vs

#version 330 core

in vec3 a_vertex;
in vec2 a_coord;
out vec2 v_uv;

void main()
{	
	v_uv = a_coord;
	gl_Position = vec4( a_vertex, 1.0 );
}


\flat.fs

#version 330 core

uniform vec4 u_color;

out vec4 FragColor;

void main()
{
	FragColor = u_color;
}


\texture.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_time;
uniform float u_alpha_cutoff;

out vec4 FragColor;

void main()
{
	vec2 uv = v_uv;
	vec4 color = u_color;
	color *= texture( u_texture, v_uv );

	if(color.a < u_alpha_cutoff)
		discard;

	FragColor = color;
}

\multi_pass.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

//material properties
uniform vec4 u_color;
uniform vec3 u_emissive_factor;
uniform sampler2D u_emissive_texture;
uniform sampler2D u_albedo_texture;
uniform sampler2D u_metalic_roughness_texture;

//global properties
uniform float u_time;
uniform float u_alpha_cutoff;
uniform vec3 u_camera_position;

//lights
uniform vec3 u_ambient_light;
uniform vec4 u_light_info; // (light_type, near_distance, far_distance, xxx);
uniform vec3 u_light_position;
uniform vec3 u_light_front;
uniform vec3 u_light_color;
uniform vec2 u_light_cone; // ( cos(min_angle), cos(max_angle) s)
uniform bool u_show_specular;  // bool (1 to show specular ligth, 0 otherwise)

//normalmap
uniform sampler2D u_normalmap;

//shadowmap
uniform mat4 u_shadow_viewproj;
uniform sampler2D u_shadowmap;
uniform vec2 u_shadow_params;  // bool (1 if it has shadowmap, 0 otherwise), bias

#define NOLIGHT 0
#define POINT_LIGHT 1
#define SPOT_LIGHT 2
#define DIRECTIONAL_LIGHT 3

out vec4 FragColor;

float testShadow(vec3 pos)
{
    //project our 3D position to the shadowmap
    vec4 proj_pos = u_shadow_viewproj * vec4(pos,1.0);

    //from homogeneus space to clip space
    vec2 shadow_uv = proj_pos.xy / proj_pos.w;

    //from clip space to uv space
    shadow_uv = shadow_uv * 0.5 + vec2(0.5);

    //get point depth [-1 .. +1] in non-linear space
    float real_depth = (proj_pos.z - u_shadow_params.y) / proj_pos.w;

    //normalize from [-1..+1] to [0..+1] still non-linear
    real_depth = real_depth * 0.5 + 0.5;

    //read depth from depth buffer in [0..+1] non-linear
    float shadow_depth = texture( u_shadowmap, shadow_uv).x;

    //it is outside on the sides
    if( shadow_uv.x < 0.0  || shadow_uv.x > 1.0 || shadow_uv.y < 0.0 || shadow_uv.y > 1.0 )
            return 0.0;

    //it is before near or behind far plane
    if(real_depth < 0.0 || real_depth > 1.0)
        return 1.0;

    //compute final shadow factor by comparing
    float shadow_factor = 1.0;

    //we can compare them, even if they are not linear
    if( shadow_depth < real_depth )
        shadow_factor = 0.0;
    return shadow_factor;
}

mat3 cotangent_frame(vec3 N, vec3 p, vec2 uv)
{
	// get edge vectors of the pixel triangle
	vec3 dp1 = dFdx( p );
	vec3 dp2 = dFdy( p );
	vec2 duv1 = dFdx( uv );
	vec2 duv2 = dFdy( uv );
	
	// solve the linear system
	vec3 dp2perp = cross( dp2, N );
	vec3 dp1perp = cross( N, dp1 );
	vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
 
	// construct a scale-invariant frame 
	float invmax = inversesqrt( max( dot(T,T), dot(B,B) ) );
	return mat3( T * invmax, B * invmax, N );
}

// assume N, the interpolated vertex normal and WP the world position

vec3 perturbNormal(vec3 N, vec3 WP, vec2 uv, vec3 normal_pixel)
{
	normal_pixel = normal_pixel * 255./127. - 128./127.;
	mat3 TBN = cotangent_frame(N, WP, uv);
	return normalize(TBN * normal_pixel);
}


void main()
{
	vec2 uv = v_uv;
	vec4 albedo = u_color;
	albedo *= texture( u_albedo_texture, v_uv );

	if(albedo.a < u_alpha_cutoff)
		discard;


	vec3 normal_pixel = texture( u_normalmap, v_uv ).xyz; 
	vec3 N = perturbNormal(v_normal, v_world_position, v_uv, normal_pixel);

	vec4 tex = texture(u_metalic_roughness_texture, v_uv);
	float occlussion_factor = tex.r;
	float metalness = tex.g;
	float roughness = tex.b;
	float shininess = roughness;

	vec3 light = vec3(0.0);

	float shadow_factor =  1.0;

	if(u_shadow_params.x != 0 && u_light_info.x != NOLIGHT)
	{
		shadow_factor = testShadow(v_world_position);
	}


	if( int(u_light_info.x) == DIRECTIONAL_LIGHT)
	{
		float Ndot = dot(N,u_light_front);
		light += max( Ndot, 0.0 ) * u_light_color * shadow_factor;
		
		//add specular
		if (u_show_specular && shininess != 0.0)
		{
			vec3 R = normalize(-(reflect(u_light_front, N))); 
			vec3 V = normalize( u_camera_position - v_world_position);
			light += metalness * pow(clamp(dot(R,V), 0, 1), shininess) * u_light_color;
		}
		
				
	}

	else if( int(u_light_info.x) == POINT_LIGHT || int(u_light_info.x) == SPOT_LIGHT)
	{
		vec3 L = u_light_position - v_world_position;
		float dist = length(L);
		L /= dist; //normilize vector L

		float Ndot = dot(N,L);
		float att = (u_light_info.z - dist) / u_light_info.z;
		att = max(att, 0.0);

		//add specular
		if (u_show_specular && shininess != 0.0)
		{	
			vec3 R = normalize(-(reflect(L, N))); 
			vec3 V = normalize( u_camera_position - v_world_position);
			light += metalness * pow(clamp(dot(R,V), 0, 1), shininess);
		}
		

		if (int(u_light_info.x) == SPOT_LIGHT)
		{
			float cos_angle = dot( u_light_front, L);
			if ( cos_angle < u_light_cone.y)
				att = 0.0;
			else if ( cos_angle < u_light_cone.x)
				att *= 1.0 - (cos_angle - u_light_cone.x) / ( u_light_cone.y - u_light_cone.x);

		}

		light +=  max( Ndot, 0.0 );

		//attenuation
		light *=  u_light_color * att * shadow_factor;
	}

	
	light += u_ambient_light * occlussion_factor;


	
	vec3 color = albedo.xyz * light;
	color += u_emissive_factor * texture(u_emissive_texture, v_uv).xyz;
	FragColor = vec4( color, albedo.a );
}

\single_pass.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;

//material properties
uniform vec4 u_color;
uniform vec3 u_emissive_factor;
uniform sampler2D u_emissive_texture;
uniform sampler2D u_albedo_texture;
uniform sampler2D u_metalic_roughness_texture;

//global properties
uniform float u_time;
uniform float u_alpha_cutoff;
uniform vec3 u_camera_position;

//lights
const int MAX_LIGHTS = 4;
uniform vec3 u_ambient_light;
uniform vec3 u_light_info[MAX_LIGHTS]; // (light_type, near_distance, far_distance);
uniform vec3 u_light_position[MAX_LIGHTS];
uniform vec3 u_light_front[MAX_LIGHTS];
uniform vec3 u_light_color[MAX_LIGHTS];
uniform vec2 u_light_cone[MAX_LIGHTS]; // ( cos(min_angle), cos(max_angle) s)
uniform int u_num_lights;
uniform bool u_show_specular;  // bool (1 to show specular ligth, 0 otherwise)

//normalmap
uniform sampler2D u_normalmap;

#define NOLIGHT 0
#define POINT_LIGHT 1
#define SPOT_LIGHT 2
#define DIRECTIONAL_LIGHT 3

out vec4 FragColor;


mat3 cotangent_frame(vec3 N, vec3 p, vec2 uv)
{
	// get edge vectors of the pixel triangle
	vec3 dp1 = dFdx( p );
	vec3 dp2 = dFdy( p );
	vec2 duv1 = dFdx( uv );
	vec2 duv2 = dFdy( uv );
	
	// solve the linear system
	vec3 dp2perp = cross( dp2, N );
	vec3 dp1perp = cross( N, dp1 );
	vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
 
	// construct a scale-invariant frame 
	float invmax = inversesqrt( max( dot(T,T), dot(B,B) ) );
	return mat3( T * invmax, B * invmax, N );
}

// assume N, the interpolated vertex normal and WP the world position

vec3 perturbNormal(vec3 N, vec3 WP, vec2 uv, vec3 normal_pixel)
{
	normal_pixel = normal_pixel * 255./127. - 128./127.;
	mat3 TBN = cotangent_frame(N, WP, uv);
	return normalize(TBN * normal_pixel);
}


void main()
{

	vec2 uv = v_uv;
	vec4 albedo = u_color;
	albedo *= texture( u_albedo_texture, v_uv );

	if(albedo.a < u_alpha_cutoff)
		discard;

	vec3 normal_pixel = texture( u_normalmap, v_uv ).xyz; 
	vec3 N = perturbNormal(v_normal, v_world_position, v_uv, normal_pixel);

	vec4 tex = texture(u_metalic_roughness_texture, v_uv);
	float occlussion_factor = tex.r;
	float metalness = tex.g;
	float roughness = tex.b;
	float shininess = roughness;

	vec3 light = vec3(0.0);
	light += u_ambient_light * occlussion_factor;

	for( int i = 0; i < MAX_LIGHTS; ++i )
	{
		if(i < u_num_lights)
		{
			int type = int(u_light_info[i].x);
			
			if( type == DIRECTIONAL_LIGHT)
			{
				float Ndot = dot(N,u_light_front[i]);
				light += max( Ndot, 0.0 ) * u_light_color[i];

				//add specular
				if (u_show_specular && shininess != 0.0)
				{
					vec3 R = normalize(-(reflect(u_light_front[i], N))); 
					vec3 V = normalize( u_camera_position - v_world_position);
					light += metalness * pow(clamp(dot(R,V), 0.0001, 1), shininess) * u_light_color[i];
				}

			}

			else if( type == POINT_LIGHT || type == SPOT_LIGHT)
			{
				vec3 L = u_light_position[i] - v_world_position;
				float dist = length(L);
				L /= dist; //normilize vector L

				float Ndot = dot(N,L);
				float att = (u_light_info[i].z - dist) / u_light_info[i].z;
				att = max(att, 0.0);

				if (type == SPOT_LIGHT)
				{
					float cos_angle = dot( u_light_front[i], L);
					if ( cos_angle < u_light_cone[i].y)
						att = 0.0;
					else if ( cos_angle < u_light_cone[i].x)
						att *= 1.0 - (cos_angle - u_light_cone[i].x) / ( u_light_cone[i].y - u_light_cone[i].x);
				}

				light += max( Ndot, 0.0 ) * u_light_color[i] * att;

				//add specular
				if (u_show_specular && shininess != 0.0)
				{
					vec3 R = normalize(-(reflect(L, N))); 
					vec3 V = normalize( u_camera_position - v_world_position);
					light += metalness * pow(clamp(dot(R,V), 0.0001, 1), shininess) * att * u_light_color[i];
				}

			}
				
		}
	}
	
	vec3 color = albedo.xyz * light;
	color += u_emissive_factor * texture(u_emissive_texture, v_uv).xyz;
	FragColor = vec4( color, albedo.a );
}

\skybox.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;

uniform samplerCube u_texture;
uniform vec3 u_camera_position;
out vec4 FragColor;

void main()
{
	vec3 E = v_world_position - u_camera_position;
	vec4 color = texture( u_texture, E );
	FragColor = color;
}


\multi.fs

#version 330 core

in vec3 v_position;
in vec3 v_world_position;
in vec3 v_normal;
in vec2 v_uv;

uniform vec4 u_color;
uniform sampler2D u_texture;
uniform float u_time;
uniform float u_alpha_cutoff;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 NormalColor;

void main()
{
	vec2 uv = v_uv;
	vec4 color = u_color;
	color *= texture( u_texture, uv );

	if(color.a < u_alpha_cutoff)
		discard;

	vec3 N = normalize(v_normal);

	FragColor = color;
	NormalColor = vec4(N,1.0);
}


\depth.fs

#version 330 core

uniform vec2 u_camera_nearfar;
uniform sampler2D u_texture; //depth map
in vec2 v_uv;
out vec4 FragColor;

void main()
{
	float n = u_camera_nearfar.x;
	float f = u_camera_nearfar.y;
	float z = texture2D(u_texture,v_uv).x;
	if( n == 0.0 && f == 1.0 )
		FragColor = vec4(z);
	else
		FragColor = vec4( n * (z + 1.0) / (f + n - z * (f - n)) );
}


\instanced.vs

#version 330 core

in vec3 a_vertex;
in vec3 a_normal;
in vec2 a_coord;

in mat4 u_model;

uniform vec3 u_camera_pos;

uniform mat4 u_viewprojection;

//this will store the color for the pixel shader
out vec3 v_position;
out vec3 v_world_position;
out vec3 v_normal;
out vec2 v_uv;

void main()
{	
	//calcule the normal in camera space (the NormalMatrix is like ViewMatrix but without traslation)
	v_normal = (u_model * vec4( a_normal, 0.0) ).xyz;
	
	//calcule the vertex in object space
	v_position = a_vertex;
	v_world_position = (u_model * vec4( a_vertex, 1.0) ).xyz;
	
	//store the texture coordinates
	v_uv = a_coord;

	//calcule the position of the vertex using the matrices
	gl_Position = u_viewprojection * vec4( v_world_position, 1.0 );
}