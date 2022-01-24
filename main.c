
#include "engine/app.h"
#include "engine/base.h"
#include "engine/graphics.h"
#define MICROUI_IMPLEMENTATION
#include "engine/vendor/microui.h"
#include "engine/third_party/stb_image.h"
#include "texture_jpg.h"

mu_Context muctx;
GLuint terrain_texture;
GLuint height_map_texture = 0;
GFX_Buffer height_map_buffer = {0};
GFX_Vertex height_map_vertex_buffer[1025*1025];
u32 height_map_triangles_buffer[1024*1024*6];

static f32
Get_perlin_dot(u64 seed, i32 xi, i32 yi, f32 x, f32 y) {

	// Compute a random unit vector based on the seed and cell position
	u64 cell_seed; {
		u64 xi64 = (u64)xi;
		u64 yi64 = (u64)yi;
		cell_seed = seed ^ (xi64 << 32 | yi64);
	}
	f32 angle = PI32*2.0f*(f32)wy2u01(wyrand(&cell_seed));
	f32 vec_x, vec_y;

	Fast_cos_sin(angle, &vec_x, &vec_y);

	//vec_x = Cos(angle);
	//vec_y = Sin(angle);

	// Compute the directon between the cell and the x,y
	f32 dx = x - (f32)xi;
	f32 dy = y - (f32)yi;

	f32 dot = vec_x * dx + vec_y * dy;

	return dot;
}

static f32
Get_perlin_point(u64 seed, f32 x, f32 y) {
	i32 x0 = (i32)x;
	i32 y0 = (i32)y;
	i32 x1 = x0+1;
	i32 y1 = y0+1;
	f32 dx = x - (f32)x0;
	f32 dy = y - (f32)y0;
	f32 d00 = Get_perlin_dot(seed, x0, y0, x, y);
	f32 d10 = Get_perlin_dot(seed, x1, y0, x, y);
	f32 d01 = Get_perlin_dot(seed, x0, y1, x, y);
	f32 d11 = Get_perlin_dot(seed, x1, y1, x, y);

	// Bilinear interpolation between the 4 values
	//
	// Apply the smoother polynomial https://en.wikipedia.org/wiki/Smoothstep#Variations
	f32 smoother_dx = dx*dx*dx*(dx * (dx * 6.0f-15.0f) + 10);
	f32 smoother_dy = dy*dy*dy*(dy * (dy * 6.0f-15.0f) + 10);
	f32 d00_d10 = Lerp(d00, d10, smoother_dx);
	f32 d01_d11 = Lerp(d01, d11, smoother_dx);
	f32 result  = Lerp(d00_d10, d01_d11, smoother_dy);

	return result;
}



static void
Fractal_terrain_3d_noise_synthesis(f32 *height_map, i32 partitions, f32 frecuency, int octaves, f32 lacunarity, f32 max_height, f32 H, u64 seed) {

	// Compute the initial gain to be bounded between -height_map and height_map
	f32 mgain = 1.0f;
	f32 mheight = 0.0f;
    for (int octave_i = 0; octave_i < octaves; octave_i+=1) {
		mgain *= (1.0f-H);
		mheight += mgain;
    }
	if (mheight > 1.0f) {
		mgain = 1.0f/mheight;
	}
	else {
		mgain = mheight;
	}

	f32 x = 0.0f, y = 0.0f;
	f32 step = frecuency/(f32)partitions;
	for (int row = 0; row < partitions; row+=1) {
		x = 0.0f;
		for (int col = 0; col < partitions; col+=1) {
			f32 octave_x = x;
			f32 octave_y = y;
			f32 height  = 0.0f;
			f32 gain    = mgain;
			for (int octave_i = 0; octave_i < octaves; octave_i+=1) {
				gain *= (1.0f-H);
				height += Get_perlin_point(seed+(u64)octave_i, octave_x, octave_y) * gain;
				// We wrap to ensure this doesn't overflow
				octave_x = Mod(octave_x*lacunarity, 2e9f);
				octave_y = Mod(octave_y*lacunarity, 2e9f);
			}

			height_map[row * partitions + col] = height*max_height;
			x += step;
		}
		y += step;
	}
}


static void
Fractal_terrain_3d_square_diamond(f32 *height_map, i32 partitions, f32 max_height, f32 H, u64 seed, f32 *max, f32 *min) {

	f32 current_max = -1e9f;
	f32 current_min =  1e9f;

	f32 proportional_height = max_height*(1.0f-H);

	// Fill the 4 corners with random values
	//
	// |-------------|
	// |o           o|
	// |             |
	// |             |
	// |             |
	// |o           o|
	// |-------------|
	//
	f32 height = (f32)(wy2gau(wyrand(&seed)) / 3.0f) * proportional_height;
	current_max = Max(height, current_max);
	current_min = Min(height, current_min);
	height_map[0 * partitions + 0] = 0;

	height = (f32)(wy2gau(wyrand(&seed)) / 3.0f) * proportional_height; 
	current_max = Max(height, current_max);
	current_min = Min(height, current_min);
	height_map[0 * partitions + partitions - 1] = 0;

	height = (f32)(wy2gau(wyrand(&seed)) / 3.0f) * proportional_height; 
	current_max = Max(height, current_max);
	current_min = Min(height, current_min);
	height_map[(partitions-1) * partitions + 0] = 0;

	height = (f32)(wy2gau(wyrand(&seed)) / 3.0f) * proportional_height; 
	current_max = Max(height, current_max);
	current_min = Min(height, current_min);
	height_map[partitions * partitions - 1] = 0;

	i32 step  = partitions-1;


	while (step > 1) {

		// Square steps
		//
		// |-------------|
		// [x]         [x]
		// |             |
		// |      o      |
		// |             |
		// [x]         [x]
		// |-------------|
		//

		for (i32 sq_y = step/2; sq_y < partitions; sq_y += step) {
			for (i32 sq_x = step/2; sq_x < partitions; sq_x += step) {
				// Mean the values of the surrounding square
				i32 y_0 = sq_y - (step/2);
				i32 y_1 = sq_y + (step/2);
				i32 x_0 = sq_x - (step/2);
				i32 x_1 = sq_x + (step/2);
				f32 mean = 0.0f;
				mean += height_map[y_0 * partitions + x_0];
				mean += height_map[y_0 * partitions + x_1];
				mean += height_map[y_1 * partitions + x_0];
				mean += height_map[y_1 * partitions + x_1];
				mean *= 0.25f;
				f32 height = mean + (f32)(wy2gau(wyrand(&seed)) / 3.0f) * proportional_height;
				current_max = Max(height, current_max);
				current_min = Min(height, current_min);
				height_map[sq_y * partitions + sq_x] = height;
			}
		}


		// Diamond steps
		//
		// |-------------|   |-------------|  |-------------|  |-------------|
		// [x]    o    [x]   [x]    x     x|  |x          [x]  |x     x     x|
		// |             |   |             |  |             |  |             |
		// |     [x]     |   |o    [x]     |  |x    [x]    o|  |x    [x]    x|
		// |             |   |             |  |             |  |             |
		// |x           x|   [x]          x|  |x          [x]  [x]    o    [x]
		// |-------------|   |-------------|  |-------------|  |-------------|
		//

		//proportional_height *= (1.0f-H);
		bool odd_row = true;
		for (i32 dmond_y = 0; dmond_y < partitions; dmond_y += step/2) {
			i32 dmond_x = 0;
			if (odd_row) {
				dmond_x = step/2;
			}
			odd_row = !odd_row;
			for (; dmond_x < partitions; dmond_x += step) {
				// interpolate the value of the nearst previous calculates points
				i32 y_0 = dmond_y - (step/2);
				i32 y_1 = dmond_y + (step/2);
				i32 x_0 = dmond_x - (step/2);
				i32 x_1 = dmond_x + (step/2);
				f32 mean = 0.0f;
				f32 total = 0.0f;
				if (y_0 >= 0) {
					mean += height_map[y_0 * partitions + dmond_x];
					total += 1.0f;
				}
				if (x_0 >= 0) {
					mean += height_map[dmond_y * partitions + x_0];
					total += 1.0f;
				}
				if (x_1 < partitions) {
					mean += height_map[dmond_y * partitions + x_1];
					total += 1.0f;
				}
				if (y_1 < partitions) {
					mean += height_map[y_1 * partitions + dmond_x];
					total += 1.0f;
				}
				mean /= total;
				f32 height =  mean + (f32)(wy2gau(wyrand(&seed)) / 3.0f) * proportional_height;
				current_max = Max(current_max, height);
				current_min = Min(current_min, height);
				height_map[dmond_y * partitions + dmond_x] = height;
			}
		}

		step /= 2;
		
		proportional_height *= (1.0f-H);
	}

	if (max) *max = current_max;
	if (min) *min = current_min;
}



static void
Fractal_terrain_3d_demo(f32 delta_time) {

	static bool should_recompute = true;
	
	static f32  distance = 5.0f;
	static Mat4 rotate   = M4(1, 0, 0, 0,
							  0, 1, 0, 0,
							  0, 0, 1, 0,
							  0, 0, 0, 1);
	static f32 rotate_y = 0.0f;
	static f32 rotate_z = 0.0f;
	static f32 rotate_x = 0.0f;
	static int rotation_playing = 1.0f;

	#define MAX_POW_3D 10
	static const f32 MAX_WIDTH  = 5;
	static const f32 MAX_LENGHT = 5;
	static const f32 MAX_MAX_HEIGHT = 10;
	
	static i32 PART_POW = 8;
	static i32 PARTITIONS = (1 << 8) + 1;
	static f32 THICKNESS = 2.0f;
	static f32 height_map[((1<<MAX_POW_3D)+1) * ((1<<MAX_POW_3D)+1)] = {0};
	static f32 WIDTH  = 3.5f;
	static f32 LENGTH = 3.5f;
	static u32 SEED = 500;
	static f32 MAX_HEIGHT = 3.5f;
	static f32 H = 0.5f;
	static Color height_map_texture_data[((1<<MAX_POW_3D)+1) * ((1<<MAX_POW_3D)+1)] = {0};
	static f32 FRECUENCY = 1.5f;
	static int OCTAVES = 6;
	static f32 LACUNARITY = 2.0f;

	#define MODE_MIDPOINT_DISPLACEMENT 0
	#define MODE_NOISE_SYNTHESIS       1
	static int mode = MODE_MIDPOINT_DISPLACEMENT;

	{
		mu_layout_row(&muctx, 2, (int[]) {100, 150}, 0);
		{
			mu_label(&muctx, "Distance");
			mu_slider(&muctx, &distance, 0.0f, 10.0f);
			mu_label(&muctx, "Rotation x");
			mu_slider(&muctx, &rotate_x, 0.0f, 360.0f);
			mu_label(&muctx, "Rotation y");
			mu_slider(&muctx, &rotate_y, 0.0f, 360.0f);
			mu_label(&muctx, "Rotation z");
			mu_slider(&muctx, &rotate_z, 0.0f, 360.0f);
			mu_layout_row(&muctx, 1, (int[]){60}, 0);
			mu_checkbox(&muctx, "Play", &rotation_playing);
			mu_layout_row(&muctx, 2, (int[]) {100, 150}, 0);
		}
		{
			static int mode_midpoint_displacement;
			static int mode_noise_synthesis;
			mode_midpoint_displacement = (mode == MODE_MIDPOINT_DISPLACEMENT);
			if (mu_checkbox(&muctx, "Midpoint disp", &mode_midpoint_displacement)) should_recompute = true;
			if (mode_midpoint_displacement) mode = MODE_MIDPOINT_DISPLACEMENT;
			mode_noise_synthesis = (mode == MODE_NOISE_SYNTHESIS);
			if (mu_checkbox(&muctx, "Noise synth", &mode_noise_synthesis)) should_recompute = true;
			if (mode_noise_synthesis) mode = MODE_NOISE_SYNTHESIS;

			if (mode == MODE_NOISE_SYNTHESIS) {
				mu_label(&muctx, "FRECUENCY");
				if (mu_slider(&muctx, &FRECUENCY, 1, 32)) should_recompute = true;
				mu_label(&muctx, "OCTAVES");
				static f32 octaves;
				octaves = (f32)OCTAVES;
				if (mu_slider(&muctx, &octaves, 1, 32)) should_recompute = true;
				OCTAVES = (int)octaves;
				mu_label(&muctx, "LACUNARITY");
				if (mu_slider(&muctx, &LACUNARITY, 0.1f, 8.0f)) should_recompute = true;
			}

			mu_label(&muctx, "MAX HEIGHT");
			if (mu_slider(&muctx, &MAX_HEIGHT, 1, MAX_MAX_HEIGHT)) should_recompute = true;
			mu_label(&muctx, "WIDTH");
			if (mu_slider(&muctx, &WIDTH, 0.5f, MAX_WIDTH)) should_recompute = true;
			mu_label(&muctx, "LENGHT");
			if (mu_slider(&muctx, &LENGTH, 0.5f, MAX_LENGHT)) should_recompute = true;
			mu_label(&muctx, "SEED");
			static char seed_str[5] = "SEED";
			if (mu_textbox(&muctx, seed_str, sizeof(seed_str))) should_recompute = true;
			SEED = 0;
			for (int i = 0; i < 4 && seed_str[i] != 0; ++i) {
				u32 val = (u32)seed_str[i];
				SEED |= (val << (i*4));
			}
			mu_label(&muctx, "H");
			if (mu_slider(&muctx, &H, 0.0f, 1.0f)) should_recompute = true;

		}

		mu_layout_row(&muctx, 3, (int[]) {100, 100, 50}, 0);
		{
			static f32 part_pow_f32;
			part_pow_f32 = PART_POW;
			mu_label(&muctx, "PARTITIONS");
			if (mu_slider_ex(&muctx, &part_pow_f32, 1, MAX_POW_3D, 1, "", MU_OPT_ALIGNCENTER)) {
				PART_POW = (i32)part_pow_f32;
				PARTITIONS = (1 << PART_POW) + 1;
				should_recompute = true;
			}
			static char partitions_str[12] = "";
			snprintf(partitions_str, sizeof(partitions_str), "%d", PARTITIONS);
			mu_label(&muctx, partitions_str);
			
		}
	}
	
	static bool mouse_dragging = false;
	static Vec3 mouse_prev_pos = V3(0, 0, 0);

	if (muctx.focus == 0) {
		if      (APP_Get_wheel_y() < 0.0f) distance = Min(distance + 0.2f, 10.0f);
		else if (APP_Get_wheel_y() > 0.0f) distance = Max(distance - 0.2f, 0.0f);
	}

	if (muctx.focus == 0 && APP_Get_pointer_state()&APP_POINTER_STATE_DOWN) {
		Vec2 mpos = V2(APP_Get_pointer_x(), APP_Get_pointer_y());
		Vec2 screen_center = V2((f32)(APP_Get_window_width())*0.5f, (f32)(APP_Get_window_height())*0.5f);
		f32  screen_radius = V2_Len(screen_center);
		Vec3 pt;
		pt.x = (mpos.x - screen_center.x) / screen_radius;
		pt.y = (mpos.y - screen_center.y) / screen_radius;
		f32 lenSq = pt.x*pt.x + pt.y*pt.y;
		if (lenSq > 1.0f) {
			// The mouse should be outside of the screen, we normalize the coordinates and set
			// the z = 0
			f32 inv_len = Sqrt(lenSq);
			pt.x = pt.x * inv_len;
			pt.y = pt.y * inv_len;
			pt.z = 0.0f;
		}
		else {
			// Compute the value of z needed to shape a unit vector
			pt.z = Sqrt(1.0f-lenSq);
		}
		Vec3 mouse_current_pos = pt;

		if (mouse_dragging) {
			Vec3 axis = V3_Cross(mouse_current_pos, mouse_prev_pos);
			Vec3 n0   = V3_Normalize(mouse_prev_pos);
			Vec3 n1   = V3_Normalize(mouse_current_pos);
			f32 dot   = V3_Dot(n0, n1);
			if (Abs(dot) < 0.9998f) {
				f32 angle = Acos(Clamp(dot, -1.0f, 1.0f));
				Mat4 rot = M4_Mul(M4_Rotate(angle, axis), rotate);

				// rot_mat to euler angles
				// http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToEuler/index.htm
				// [m00 m01 m02]
				// [m10 m11 m12]
				// [m20 m21 m22]
				// rotation_y = atan2(-m20,m00)
				// rotation_z = asin(m10)
				// rotation_x = atan2(-m12,m11)
				// 
				// except when M10=1 (north pole)
				// which gives:
				// rotation_y = atan2(M02,M22)
				// rotation_x = 0
				// and when M10=-1 (south pole)
				// which gives:
				// rotation_y = atan2(M02,M22)
				// rotation_x = 0
				//

				if (rot.m10 > 0.9998f) { // Y axis pointing to X
					rotate_y = Mod(Degrees(Atan2(rot.m02, rot.m22))+360.0f, 360.0f);
					rotate_z = 90.0f;
					rotate_x = 0.0f;
				}
				else if (rot.m10 < -0.9998f) { // Y axis pointing to -X
					rotate_y = Mod(Degrees(Atan2(rot.m02, rot.m22))+360.0f, 360.0f);
					rotate_z = 270.0f;
					rotate_x = 0.0f;
				}
				else {
					rotate_y = Mod(Degrees(Atan2(-rot.m20, rot.m00))+360.0f, 360.0f);
					rotate_z = Mod(Degrees(Asin(rot.m10))+360.0f, 360.0f);
					rotate_x = Mod(Degrees(Atan2(-rot.m12, rot.m11))+360.0f, 360.0f);
				}
			}
		}

		mouse_prev_pos = mouse_current_pos;

		mouse_dragging = true;
	}
	else {
		mouse_dragging = false;
	}


	static f32 max_height, min_height;
	if (should_recompute) {

		should_recompute = false;
		if (mode == MODE_MIDPOINT_DISPLACEMENT) {
			Fractal_terrain_3d_square_diamond(height_map, PARTITIONS, MAX_HEIGHT, H, (u64)SEED, &max_height, &min_height);
		}
		else if (mode == MODE_NOISE_SYNTHESIS) {
			//Fractal_terrain_3d_noise_synthesis(height_map, PARTITIONS, PERLIN_CELLS, MAX_HEIGHT, H, (u64)SEED);
			Fractal_terrain_3d_noise_synthesis(height_map, PARTITIONS, FRECUENCY, OCTAVES, LACUNARITY, MAX_HEIGHT, H, (u64)SEED);
			max_height =  MAX_HEIGHT;
			min_height = -MAX_HEIGHT;
		}


		f32 total_height = max_height-min_height;
		for (int row = 0; row < PARTITIONS; row += 1) {
			for (int col = 0; col < PARTITIONS; col += 1) {
				f32 height = height_map[row*PARTITIONS+col];
				height = (height-min_height)/(total_height/255.0f);
				height = Clamp(height, 0, 255);
				u8 heightu8 = (u8) height;
				Color c = COLOR(heightu8, heightu8, heightu8, 255);
				height_map_texture_data[row*PARTITIONS+col] = c;
			}
		}
		glBindTexture(GL_TEXTURE_2D, height_map_texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // On webgl we need a pow
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // of 2 texture or set this
    	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, PARTITIONS, PARTITIONS, 0, GL_RGBA, GL_UNSIGNED_BYTE, height_map_texture_data);

		GFX_Clear_buffer_data(&height_map_buffer);
		GFX_Vertex *vertices = GFX_Alloc_vertices(&height_map_buffer, PARTITIONS*PARTITIONS, NULL);
		Assert(vertices,"Too much vertices");
		u32 *indices = GFX_Alloc_indices(&height_map_buffer, (PARTITIONS-1)*(PARTITIONS-1)*6);
		Assert(indices,"Too much indices");

		f32 step = 1.0f/(f32)(PARTITIONS-1.0f);
		f32 wstep = WIDTH/(f32)(PARTITIONS-1.0f);
		f32 z = 0.0f;
		for (int i = 0; i < PARTITIONS; i+=1) {
			f32 x = 0.0f;
			for (int j = 0; j < PARTITIONS; j+=1) {
				f32 y = height_map[i * PARTITIONS + j];
				Vec3 pos = V3((x - 0.5f)*WIDTH, y, (z - 0.5f)*LENGTH);
				bool has_neigh0 = (i > 0);
				bool has_neigh1 = (j > 0);
				bool has_neigh2 = (i < (PARTITIONS-1));
				bool has_neigh3 = (j < (PARTITIONS-1));
				Vec3 neigh0, neigh1, neigh2, neigh3;
				if (has_neigh0) {
					f32 neigh0_y = height_map[(i-1) * PARTITIONS + j] - y;
					neigh0 = V3(0.0f, neigh0_y, -wstep);
				}
				if (has_neigh1) {
					f32 neigh1_y = height_map[i * PARTITIONS + j - 1] - y;
					neigh1 = V3(-wstep, neigh1_y, 0.0f);
				}
				if (has_neigh2) {
					f32 neigh2_y = height_map[(i+1) * PARTITIONS + j] - y;
					neigh2 = V3(0.0f, neigh2_y, wstep);
				}
				if (has_neigh3) {
					f32 neigh3_y = height_map[i * PARTITIONS + j + 1] - y;
					neigh3 = V3(wstep, neigh3_y, 0.0f);
				}

				Vec3 normal_acum = V3(0, 0, 0);
				f32 normal_total = 0.0f;
				if (has_neigh0 && has_neigh1) {
					normal_acum = V3_Add(normal_acum, V3_Cross(neigh0, neigh1));
					normal_total += 1.0f;
				}
				if (has_neigh1 && has_neigh2) {
					normal_acum = V3_Add(normal_acum, V3_Cross(neigh1, neigh2));
					normal_total += 1.0f;
				}
				if (has_neigh2 && has_neigh3) {
					normal_acum = V3_Add(normal_acum, V3_Cross(neigh2, neigh3));
					normal_total += 1.0f;
				}
				if (has_neigh3 && has_neigh0) {
					normal_acum = V3_Add(normal_acum, V3_Cross(neigh3, neigh0));
					normal_total += 1.0f;
				}

				normal_acum = V3_Normalize(V3_Mulf(normal_acum, 1.0f/normal_total));
				normal_acum.y = -normal_acum.y;

				Vec2 uv = V2(x, z);
				Color normal = V4_To_Color((Vec4){.xyz=normal_acum});

				vertices[i*PARTITIONS+j].position  = pos;
				vertices[i*PARTITIONS+j].normal    = normal;
				vertices[i*PARTITIONS+j].tex_coord = uv;
				vertices[i*PARTITIONS+j].color     = WHITE;

				x += step;
			}

			z += step;
		}
		
		u32 indices_pushed = 0;
		for (int i = 1; i < PARTITIONS; i+=1) {
			for (int j = 1; j < PARTITIONS; j+=1) {
				u32 index0 = (i-1) * PARTITIONS + j-1;
				u32 index1 = (i) * PARTITIONS + j-1;
				u32 index2 = (i) * PARTITIONS + j;
				u32 index3 = (i-1) * PARTITIONS + j;
				indices[indices_pushed+0] = index0;
				indices[indices_pushed+1] = index1;
				indices[indices_pushed+2] = index2;
				indices[indices_pushed+3] = index0;
				indices[indices_pushed+4] = index2;
				indices[indices_pushed+5] = index3;
				indices_pushed += 6;
			}
		}

		GFX_Upload_buffer_to_gpu(&height_map_buffer);

	}
	

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDepthMask(GL_TRUE);
	glClear(GL_DEPTH_BUFFER_BIT);
	//glEnable(GL_CULL_FACE);
	Mat4 translate = M4_Translate(V3(0, 0, -distance));

	if (rotation_playing && !mouse_dragging) {
		rotate_y = Mod(rotate_y+60.0f*delta_time, 360.0f);
	}
	rotate = M4_Diagonal(1.0f);

	rotate = M4_Mul(M4_Rotate(Radians(rotate_x), V3(1, 0, 0)), rotate); // Inclinate a bit 
	rotate = M4_Mul(M4_Rotate(Radians(rotate_z), V3(0, 0, 1)), rotate); // Inclinate a bit 
	rotate = M4_Mul(M4_Rotate(Radians(rotate_y), V3(0, 1, 0)), rotate);
			

	f32 aspect_ratio = (f32)APP_Get_window_width() / (f32)APP_Get_window_height();
	Mat4 perspective = M4_Perspective(Radians(60), aspect_ratio, 0.1f, 100.0f);
	Mat4 tmat        = M4_Mul(M4_Rotate(Radians(30), V3(1,0,0)), rotate);
	Vec3 light_dir   = Mul_v4_m4(V4(0, 0, -1, 0), M4_Transpose(tmat)).xyz;
	tmat             = M4_Mul(translate, tmat);

	GFX_Set_matrix(M4_Mul(perspective, tmat));
	GFX_Set_light_dir(light_dir);
	GFX_Set_texture(terrain_texture);
	GFX_Draw_buffer(&height_map_buffer);
	GFX_Flush();
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	GFX_Set_texture(height_map_texture);
	GFX_Set_matrix(M4_Orthographic(0, APP_Get_window_width(), 0, APP_Get_window_height(), 1, 0));
	GFX_Set_light_dir(V3(0, 0, -1));
	GFX_Draw_rect(V2(0, 0), 200, 200, WHITE);
}



typedef struct {
	Vec2 pos;
	Vec2 src_pos;
	Vec2 dst_pos;

	f32 zoom;
	f32 src_zoom;
	f32 dst_zoom;

	bool zoom_bounded;
	f32 max_zoom;
	f32 min_zoom;

	f32 aim_time;
	f32 aim_total_time;
} CanvasCam;


void
CanvasCam_Set_target(CanvasCam *cc, f32 new_zoom, Vec2 new_pos) {

	if (cc->zoom_bounded) new_zoom = Clamp(new_zoom, cc->min_zoom, cc->max_zoom);
	f32 zoom_step = (new_zoom / cc->zoom);
	cc->src_zoom = cc->zoom;
	cc->dst_zoom = new_zoom;

	cc->src_pos = cc->pos;
	cc->dst_pos = V2_Mulf(new_pos, zoom_step);

	static const f32 CAMERA_AIM_TIME = 0.5f;
	f32 zoom_time = 0.1f * zoom_step;
	f32 position_time = 1.0f/3000.0f*V2_Len(V2_Sub(new_pos, cc->src_pos));
	cc->aim_total_time = Min(Max(zoom_time, position_time), CAMERA_AIM_TIME);
	cc->aim_time = cc->aim_total_time;
}

void
CanvasCam_Change_by(CanvasCam *cc, f32 zoom_amount, Vec2 delta_move) {
	f32 new_zoom = cc->zoom * zoom_amount;
	if (cc->zoom_bounded) {
		new_zoom = Clamp(new_zoom, cc->min_zoom, cc->max_zoom);
	}
	f32 zoom_step = (new_zoom / cc->zoom);
	cc->zoom = new_zoom;

	cc->pos = V2_Add(cc->pos, delta_move);
	cc->pos = V2_Mulf(cc->pos, zoom_step);

	cc->src_zoom = cc->zoom;
	cc->dst_zoom = cc->zoom;
	cc->src_pos  = cc->pos;
	cc->dst_pos  = cc->pos;
	cc->aim_time = 0.0f;
	cc->aim_total_time = 1.0f;
}

void
CanvasCam_Update(CanvasCam *cc, f32 dt) {
	cc->aim_time -= dt;
	if (cc->aim_time <= 0.0f) {
		cc->aim_time = 0.0f;
		cc->pos  = cc->dst_pos;
		cc->zoom = cc->dst_zoom;
	}
	else {
		f32 t = (cc->aim_total_time-cc->aim_time) / cc->aim_total_time;
		cc->pos = V2_Lerp(cc->src_pos, cc->dst_pos, t);
		cc->zoom = Lerp(cc->src_zoom, cc->dst_zoom, t);
	}
}


Vec2
Point_to_canvas_coordinates(CanvasCam *cc, Vec2 point) {
	// First put the mouse relative to the center of the window
	Vec2 result = V2_Sub(point, V2(APP_Get_window_width()/2, APP_Get_window_height()/2));
	// Now offset by the canvas origin
	result = V2_Sub(result, cc->pos);
	// Now, divide by the zoom level
	result = V2_Mulf(result, 1.0f/cc->zoom);

	return result;
}




static f32
Urandf(u64 *seed, f32 min, f32 max) {
	f32 urand = (f32)wy2u01(wyrand(seed));
	f32 result = urand * (max-min) + min;
	return result;
}


static void
Fractal_terrain_2d_demo(f32 delta_time) {

	#define MAX_POW 20
	#define MAX_ZOOM 50.0f
	#define MIN_ZOOM 0.05f
	#define MAX_LENGHT 10000
	#define MAX_MAX_HEIGHT 10000
	
	static i32 PART_POW = 10;
	static i32 PARTITIONS = (1 << 10) + 1;
	static f32 THICKNESS = 2.0f;
	static f32 height_map[(1<<MAX_POW)+1] = {0};
	static f32 LINE_LENGTH  = 500.0f;
	static u32 SEED = 500;
	static f32 MAX_HEIGHT = 500.0f;
	static f32 H = 0.5f;
	
	
	static bool select_zoom_dragging = false;
	static Vec2 target_zoom_rect_center = V2(0.0f, 0.0f);
	static Vec2 zoom_rect_start = V2(0.0f, 0.0f);
	static Vec2 zoom_rect_end   = V2(0.0f, 0.0f);
	static Color zoom_rect_color_default = COLOR(12, 12, 255, 150);
	static Color zoom_rect_color = COLOR(12, 12, 255, 150);
	#define ZOOM_RECT_VANISH_TIME 0.5f
	static f32 zoom_rect_vanish_current_time = 0.0f;
	static bool should_draw_zoom_rect = false;


	{ // GUI options
		mu_layout_row(&muctx, 2, (int[]) {100, 150}, 0);

		{
			mu_label(&muctx, "LENGHT");
			mu_slider(&muctx, &LINE_LENGTH, 100, MAX_LENGHT);
		}

		{
			mu_label(&muctx, "THICKNESS");
			mu_slider(&muctx, &THICKNESS, 1, 5);
		}
		
		{
			mu_label(&muctx, "SEED");
			static char seed_str[5] = "1234";
			if (mu_textbox(&muctx, seed_str, sizeof(seed_str))) {
				for (int i = 0; i < 4 && seed_str[i] != 0; ++i) {
					SEED = 0;
					u32 val = (u32)seed_str[i];
					SEED |= (val << (i*4));
				}
			}
		}

		{
			mu_label(&muctx, "MAX HEIGHT");
			mu_slider(&muctx, &MAX_HEIGHT, 10, MAX_MAX_HEIGHT);
		}

		{
			mu_label(&muctx, "H");
			mu_slider(&muctx, &H, 0.0f, 1.0f);
		}

		mu_layout_row(&muctx, 3, (int[]) {100, 100, 50}, 0);
		{
			static f32 part_pow_f32;
			part_pow_f32 = PART_POW;
			mu_label(&muctx, "PARTITIONS");
			if (mu_slider_ex(&muctx, &part_pow_f32, 1, MAX_POW, 1, "", MU_OPT_ALIGNCENTER)) {
				PART_POW = (i32)part_pow_f32;
				PARTITIONS = (1 << PART_POW) + 1;
			}
			static char partitions_str[12] = "";
			snprintf(partitions_str, sizeof(partitions_str), "%d", PARTITIONS);
			mu_label(&muctx, partitions_str);
			
		}
	}

	static CanvasCam canvas_cam = {
		.pos = V2(0, 0),
		.zoom = 1.0f,
		.src_zoom = 1.0f,
		.dst_zoom = 1.0f,
		.zoom_bounded = true,
		.min_zoom = MIN_ZOOM,
		.max_zoom = MAX_ZOOM
	};

	static bool dragging = false;
	static Vec2 mouse_prev = V2(0.0f, 0.0f);

	if (muctx.focus == 0 && (APP_Get_pointer_state()&APP_POINTER_STATE_DOWN)) {
		Vec2 mouse_curr = V2(APP_Get_pointer_x(), APP_Get_pointer_y());
		if (dragging) CanvasCam_Change_by(&canvas_cam, 1.0f, V2_Sub(mouse_curr, mouse_prev));
		mouse_prev = mouse_curr;
		dragging = true;

	} else {
		dragging = false;
	}



	if (APP_Is_key_repeat(APP_KEY_EQUAL) || APP_Get_wheel_y() > 0.0f) CanvasCam_Change_by(&canvas_cam, 1.1f, V2(0.0f, 0.0f));
	if (APP_Is_key_repeat(APP_KEY_MINUS) || APP_Get_wheel_y() < 0.0f) CanvasCam_Change_by(&canvas_cam, 0.9f, V2(0.0f, 0.0f));


	if (muctx.focus == 0 && APP_Is_mouse_button_down(APP_MOUSE_BUTTON_RIGHT)) {
		// Get the mouse position relative to the canvas coordinates
		Vec2 mouse_curr = Point_to_canvas_coordinates(&canvas_cam, V2(APP_Get_mouse_x(), APP_Get_mouse_y()));
		
		if (select_zoom_dragging) {
			zoom_rect_end = mouse_curr;
		}
		else {
			// We put the rect positions relative to the center of the screen
			zoom_rect_start = mouse_curr;
			zoom_rect_end  = mouse_curr;
		}
		should_draw_zoom_rect = true;
		select_zoom_dragging = true;
	}
	else if (select_zoom_dragging) {
		if (zoom_rect_start.x != zoom_rect_end.x || zoom_rect_start.y != zoom_rect_end.y) {

			f32 rect_w = Abs(zoom_rect_end.x - zoom_rect_start.x);
			f32 rect_h = Abs(zoom_rect_end.y - zoom_rect_start.y);
			f32 window_w = (f32)APP_Get_window_width();
			f32 window_h = (f32)APP_Get_window_height();
			f32 zoom_w = window_w/rect_w;
			f32 zoom_h = window_h/rect_h;
			f32 new_zoom = zoom_w < zoom_h ? zoom_w : zoom_h;

			Vec2 rect_center = V2_Mulf(V2_Add(zoom_rect_start, zoom_rect_end), 0.5f);
			Vec2 new_pos     = V2_Mulf(rect_center, -canvas_cam.zoom);

			CanvasCam_Set_target(&canvas_cam, new_zoom, new_pos);

			zoom_rect_vanish_current_time = ZOOM_RECT_VANISH_TIME;
			zoom_rect_color = zoom_rect_color_default;
		}
		select_zoom_dragging = false;
	}

	CanvasCam_Update(&canvas_cam, delta_time);
	
	if (canvas_cam.aim_time <= 0.0f && !select_zoom_dragging) {
		// In this case we should vanish the zoom rectangle smoothly
		zoom_rect_vanish_current_time -= delta_time;
		if (zoom_rect_vanish_current_time <= 0.0f) {
			zoom_rect_vanish_current_time = 0.0f;
			should_draw_zoom_rect = false;
			zoom_rect_color = zoom_rect_color_default;
		}
		else {
			f32 t = (ZOOM_RECT_VANISH_TIME-zoom_rect_vanish_current_time) / ZOOM_RECT_VANISH_TIME;

			Color end_color = zoom_rect_color_default; // _The same color with alpha = 0
			end_color.a = 0;                           //

			zoom_rect_color = Color_lerp(zoom_rect_color_default, end_color, t);
		}
	}



	{ // Calculate the height map

		u64 rand_seed = (u64)SEED;

		// We start on the center element
		f32 proportional_height = MAX_HEIGHT;
		int step = 1 << PART_POW;
		int start = 1 << (PART_POW-1);

		height_map[0] = 0.0f;
		height_map[PARTITIONS-1] = 0;
		
		// Each iteration we divide de starting point and the step in order to fill the mid points
		while(start > 0) {
			// Calculate the new maximum height applying the fractal dimmension
			proportional_height *= (1.0f-H);
			for (int j = start; j < PARTITIONS; j+=step) {

				// interpolate the value of the nearst previous calculates points
				f32 prev_height = height_map[j - (step/2)];
				f32 next_height = height_map[j + (step/2)];
				f32 interpolated_height = (prev_height + next_height) * 0.5f;
				
				// Add to the interpolated value a random height
				height_map[j] = (interpolated_height + (f32)wy2gau(wyrand(&rand_seed)) * (proportional_height / 3.0f));
			}

			// Calculate the new step and start point
			step /= 2;
			start /= 2;

		}
	}


	{
		Mat4 canvas_matrix = M4(canvas_cam.zoom, 0,          0, canvas_cam.pos.x,
                                0,          canvas_cam.zoom, 0, canvas_cam.pos.y,
                                0,          0,          1, 0,
								0,          0,          0, 1);

		Mat4 ortho = M4_Orthographic(-APP_Get_window_width()/2, APP_Get_window_width()/2, APP_Get_window_height()/2, -APP_Get_window_height()/2, 0, 1);

		Mat4 tmat = M4_Mul(ortho, canvas_matrix);

		GFX_Set_matrix(tmat);
	}


	GFX_Set_texture(GFX_Default_texture());
	{	
		f32 x_center = -LINE_LENGTH*0.5f;
		Vec2 p0 = V2(x_center, height_map[0]);
		for (int i = 1; i < PARTITIONS; i+=1) {
			f32 dx = LINE_LENGTH*(f32)(i)/(f32)(PARTITIONS-1);
			Vec2 p1 = V2(x_center+dx, height_map[i]);
			GFX_Draw_line(p0, p1, THICKNESS/canvas_cam.zoom, BLACK);
			p0 = p1;
		}
	}

	if (should_draw_zoom_rect) {
		if (zoom_rect_start.x != zoom_rect_end.x || zoom_rect_start.y != zoom_rect_end.y) {
			f32 w = zoom_rect_end.x - zoom_rect_start.x;
			f32 h = zoom_rect_end.y - zoom_rect_start.y;
			GFX_Draw_rect(zoom_rect_start, w, h, zoom_rect_color);
		}
	}
}


static int
App_frame(void) {

	static const f32 FPS_COUNTER_MIN = 0.2f;
	static f32 fps_time_acum = 0.0f;
	static f32 fps_acum = 0.0f;
	
	#define MODE_2D 1
	#define MODE_3D 2
	
	static int mode = MODE_2D;

	if (APP_Quit_requested()) {
		GFX_Deinit();
		APP_Destroy_window();
		return 1;
	}

	glViewport(0, 0, APP_Get_window_width(), APP_Get_window_height());
	Vec4 c = Color_to_Vec4(RAYWHITE);
	glClearColor(c.r, c.g, c.b, c.a);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	f32 delta_time = (f32)APP_Frame_duration(10)*1e-9f;

	GFX_Begin();
	mu_Process_events(&muctx);

	mu_begin(&muctx);
	if (mu_begin_window_ex(
			&muctx,
			"Control panel",
			mu_rect(0, 0, 0, 0),
			MU_OPT_NOCLOSE|MU_OPT_NORESIZE|MU_OPT_NOINTERACT|MU_OPT_NOTITLE|MU_OPT_AUTOSIZE)) {

		{
			fps_time_acum += delta_time;
			fps_acum += 1;
			static char fps_str[20] = "FPS: 00.00";
			if (fps_time_acum >= FPS_COUNTER_MIN) {
				snprintf(fps_str, sizeof(fps_str), "FPS: %.2f", fps_acum/fps_time_acum);
				fps_time_acum = 0.0f;
				fps_acum = 0.0f;
			}
			mu_label(&muctx, fps_str);
		}

		mu_layout_row(&muctx, 2, (int[]) {50, 50}, 0);
		static int mode_2d;
		static int mode_3d;
		mode_2d = (mode == MODE_2D);
		mu_checkbox(&muctx, "2D", &mode_2d);
		if (mode_2d) mode = MODE_2D;
		mode_3d = (mode == MODE_3D);
		mu_checkbox(&muctx, "3D", &mode_3d);
		if (mode_3d) mode = MODE_3D;

		if (mode == MODE_2D) {
			Fractal_terrain_2d_demo(delta_time);
		}
		else if (mode == MODE_3D) {
			Fractal_terrain_3d_demo(delta_time);
		}

		mu_end_window(&muctx);
	}
	mu_end(&muctx);

	GFX_Set_matrix(M4_Orthographic(0, APP_Get_window_width(), APP_Get_window_height(), 0, 0, 1));
	GFX_Set_light_dir(V3(0, 0, -1));
	mu_Render(&muctx);
	GFX_End();

	return 0;
}

int
main() {
	if (0 != APP_Init("Fractal terrain", 640, 480)) Panic("Oops");
	if (0 != GFX_Init()) Panic("Oops");
	if (0 != mu_Setup(&muctx)) Panic("Oops");
	muctx.style->colors[MU_COLOR_WINDOWBG].a = 230;
	
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);

	{ // Gen texture
		u32 image_data_size;

		int texture_width, texture_height, channels;
    	unsigned char *image = stbi_load_from_memory(
			texture_jpg,
			texture_jpg_len,
			&texture_width,
			&texture_height,
			&channels,
			STBI_rgb_alpha
		);

    	if (image == NULL) {
    	    fprintf(stderr, "Couldnt load texture\n");
			return -1;
    	}

    	glGenTextures(1, &terrain_texture);
    	glBindTexture(GL_TEXTURE_2D, terrain_texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // On webgl we need a pow
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // of 2 texture or set this
    	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture_width, texture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    	//glGenerateMipmap(GL_TEXTURE_2D);

    	stbi_image_free(image);
	}

    glGenTextures(1, &height_map_texture);
	GFX_Create_buffer(
			&height_map_buffer,
			height_map_vertex_buffer,
			sizeof(height_map_vertex_buffer),
			height_map_triangles_buffer,
			sizeof(height_map_triangles_buffer),
			GFX_BUFFER_INDEX_TYPE_32
		);

	return APP_Run_application_loop(App_frame);
}



