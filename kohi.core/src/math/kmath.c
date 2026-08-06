#include "kmath.h"

#include <math.h> // for sinf, cosf, etc.

#include "debug/kassert.h"
#include "defines.h"
#include "math/math_types.h"
#include "math/mtwister.h" // for 64-bit RNG

#define KMATH_USE_CUSTOM_RAND 1
#if KMATH_USE_CUSTOM_RAND
#	define krand _krand
#	define ksrand _ksrand
static u32 rand_state = 2463534242u;
#else
#	include <stdlib.h> // (s)rand
#	define krand rand
#	define ksrand srand
#endif

static b8 rand_seeded = false;
static mtrand_state rng_u64 = {0}; // State for unsigned 64-bit RNG

static void seed_randoms (void);

#define VEC3_CHECK_NAN(v) \
	KASSERT(kis_finite(v.x) && kis_finite(v.y) && kis_finite(v.z))

b8 quat_is_identity (quat q) {
	for (u8 i = 0; i < 3; ++i) {
		if (!kfloat_compare(q.elements[i], 0.0f)) {
			return false;
		}
	}
	return kfloat_compare(q.elements[3], 1.0f);
}

/**
 * Note that these are here in order to prevent having to import the
 * entire <math.h> everywhere.
 */
f32 ksin (f32 x) { return sinf(x); }

f32 kcos (f32 x) { return cosf(x); }

f32 ktan (f32 x) { return tanf(x); }

f32 katan (f32 x) { return atanf(x); }

f32 katan2 (f32 x, f32 y) { return atan2(x, y); }

f32 kasin (f32 x) { return asinf(x); }

f32 kacos (f32 x) { return acosf(x); }

f32 ksqrt (f32 x) { return sqrtf(x); }

f32 klog (f32 x) { return logf(x); }

f32 klog2 (f32 x) { return log2f(x); }

f32 kpow (f32 x, f32 y) { return powf(x, y); }

void _ksrand (u32 seed) {
	if (seed != 0) {
		rand_state = seed;
	}
}

int _krand (void) {
	u32 x = rand_state;

	// xorshift32
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;

	rand_state = x;

	return (int)(x & 0x7fffffff);
}

i32 krandom (void) {
	if (!rand_seeded) {
		seed_randoms();
	}
	return krand();
}

i32 krandom_in_range (i32 min, i32 max) {
	if (!rand_seeded) {
		seed_randoms();
	}
	return (krand() % (max - min + 1)) + min;
}

u64 krandom_u64 (void) {
	if (!rand_seeded) {
		seed_randoms();
	}
	return mtrand_generate(&rng_u64);
}

f32 kfrandom (void) { return (float)krandom() / (f32)KRAND_MAX; }

f32 kfrandom_in_range (f32 min, f32 max) {
	return min + ((float)krandom() / ((f32)KRAND_MAX / (max - min)));
}

f32 kattenuation_min_max (f32 min, f32 max, f32 x) {
	// TODO: Maybe a good function here would be one with a min/max and falloff value...
	// so if the range was 0.4 to 0.8 with a falloff of 1.0, weight for x between
	// 0.5 and 0.7 would be 1.0, with it dwindling to 0 as it approaches 0.4 or 0.8.
	f32 half_range = kabs(max - min) * 0.5;
	f32 mid = min + half_range;	  // midpoint
	f32 distance = kabs(x - mid); // dist from mid
	// scale dist from midpoint to halfrange
	f32 att = KCLAMP((half_range - distance) / half_range, 0, 1);
	return att;
}

f32 vec2_hypot (vec2 v) {
	f32 ax = kabs(v.x);
	f32 ay = kabs(v.y);

	f32 m = KMAX(ax, ay);
	if (m == 0.0f) {
		return 0.0f;
	}

	ax /= m;
	ay /= m;

	return m * ksqrt(ax * ax + ay * ay);
}

f32 vec3_hypot (vec3 v) {
	f32 ax = kabs(v.x);
	f32 ay = kabs(v.y);
	f32 az = kabs(v.z);

	f32 m = KMAX(ax, KMAX(ay, az));
	if (m == 0.0f) {
		return 0.0f;
	}

	ax /= m;
	ay /= m;
	az /= m;

	return m * ksqrt(ax * ax + ay * ay + az * az);
}

plane_3d plane_3d_create (vec3 p1, vec3 norm) {
	plane_3d p;
	p.normal = vec3_normalized(norm);
	p.distance = vec3_dot(p.normal, p1);
	return p;
}

kfrustum kfrustum_from_view_projection (mat4 view_projection) {
	kfrustum f;

	// Get the inverse of the view_projection matrix.
	mat4 inv = mat4_inverse(view_projection);
	f32 *md = inv.data;

	// Extract the rows.
	vec4 mat0 = {md[0], md[1], md[2], md[3]};
	vec4 mat1 = {md[4], md[5], md[6], md[7]};
	vec4 mat2 = {md[8], md[9], md[10], md[11]};
	vec4 mat3 = {md[12], md[13], md[14], md[15]};

	// Calculate the projection planes and normalize them, including distances.
	vec4 sides[6];
	sides[KFRUSTUM_SIDE_LEFT] = vec4_normalized(vec4_add(mat3, mat0));
	sides[KFRUSTUM_SIDE_RIGHT] = vec4_normalized(vec4_sub(mat3, mat0));
	sides[KFRUSTUM_SIDE_TOP] = vec4_normalized(vec4_sub(mat3, mat1));
	sides[KFRUSTUM_SIDE_BOTTOM] = vec4_normalized(vec4_add(mat3, mat1));
	sides[KFRUSTUM_SIDE_NEAR] = vec4_normalized(vec4_add(mat3, mat2));
	sides[KFRUSTUM_SIDE_FAR] = vec4_normalized(vec4_sub(mat3, mat2));

	// Extract normals and distances to planes.
	for (u32 i = 0; i < 6; ++i) {
		f.sides[i].normal = vec3_from_vec4(sides[i]);
		f.sides[i].distance = sides[i].w;
	}

	return f;
}

kfrustum kfrustum_create (vec3 position, vec3 target, vec3 up, f32 aspect, f32 fov, f32 near, f32 far) {
	kfrustum f;

	// Calculate the forward vector (negative Z direction for right-handed systems)
	vec3 forward = vec3_normalized(vec3_sub(target, position));

	// Calculate the right vector (X-axis), ensuring a right-handed system
	vec3 right = vec3_normalized(vec3_cross(forward, up));

	// Recalculate the true up vector (Y-axis) to ensure orthogonality
	vec3 adjusted_up = vec3_normalized(vec3_cross(right, forward));

	// Half dimensions at the far plane
	f32 half_v = far * tanf(fov * 0.5f); // Vertical half
	f32 half_h = half_v * aspect;		 // Horizontal half

	vec3 forward_far = vec3_mul_scalar(forward, far);
	vec3 forward_near = vec3_mul_scalar(forward, near);
	vec3 right_half_h = vec3_mul_scalar(right, half_h);
	vec3 up_half_v = vec3_mul_scalar(adjusted_up, half_v);

	vec3 pos_forward_far = vec3_add(position, forward_far);

	vec3 top_dir = vec3_normalized(vec3_sub(forward_far, up_half_v));
	vec3 bot_dir = vec3_normalized(vec3_add(forward_far, up_half_v));
	vec3 left_dir = vec3_normalized(vec3_sub(forward_far, right_half_h));
	vec3 right_dir = vec3_normalized(vec3_add(forward_far, right_half_h));

	vec3 top_norm = vec3_normalized(vec3_cross(right, top_dir));
	vec3 bot_norm = vec3_normalized(vec3_cross(bot_dir, right));
	vec3 left_norm = vec3_normalized(vec3_cross(left_dir, adjusted_up));
	vec3 right_norm = vec3_normalized(vec3_cross(adjusted_up, right_dir));

	f.sides[KFRUSTUM_SIDE_TOP] = plane_3d_create(pos_forward_far, top_norm);
	f.sides[KFRUSTUM_SIDE_BOTTOM] = plane_3d_create(pos_forward_far, bot_norm);
	f.sides[KFRUSTUM_SIDE_RIGHT] = plane_3d_create(pos_forward_far, right_norm);
	f.sides[KFRUSTUM_SIDE_LEFT] = plane_3d_create(pos_forward_far, left_norm);

	f.sides[KFRUSTUM_SIDE_TOP] = plane_3d_create(position, top_norm);
	f.sides[KFRUSTUM_SIDE_BOTTOM] = plane_3d_create(position, bot_norm);
	f.sides[KFRUSTUM_SIDE_RIGHT] = plane_3d_create(position, right_norm);
	f.sides[KFRUSTUM_SIDE_LEFT] = plane_3d_create(position, left_norm);
	f.sides[KFRUSTUM_SIDE_NEAR] = plane_3d_create(vec3_add(position, forward_near), forward);
	f.sides[KFRUSTUM_SIDE_FAR] = plane_3d_create(vec3_add(position, forward_far), vec3_mul_scalar(forward, -1.0f));

	return f;
}

f32 plane_signed_distance (const plane_3d *p, const vec3 *position) {
	return vec3_dot(p->normal, *position) - p->distance;
}

b8 plane_intersects_sphere (const plane_3d *p, const vec3 *center, f32 radius) {
	return plane_signed_distance(p, center) > -radius;
}

b8 kfrustum_intersects_sphere (const kfrustum *f, const vec3 *center, f32 radius) {
	for (u8 i = 0; i < 6; ++i) {
		if (!plane_intersects_sphere(&f->sides[i], center, radius)) {
			return false;
		}
	}
	return true;
}

b8 kfrustum_intersects_ksphere (const kfrustum *f, const ksphere *sphere) {
	for (u8 i = 0; i < 6; ++i) {
		if (!plane_intersects_sphere(&f->sides[i], &sphere->position, sphere->radius)) {
			return false;
		}
	}
	return true;
}

b8 plane_intersects_aabb (const plane_3d *p, const vec3 *center, const vec3 *extents) {
	f32 r = extents->x * kabs(p->normal.x) +
			extents->y * kabs(p->normal.y) +
			extents->z * kabs(p->normal.z);

	f32 distance = plane_signed_distance(p, center);

	if (distance <= -r) {
		return false;
	}

	return true;
}

b8 kfrustum_intersects_aabb (const kfrustum *f, const vec3 *center, const vec3 *extents) {
	for (u8 i = 0; i < 6; ++i) {
		if (!plane_intersects_aabb(&f->sides[i], center, extents)) {
			return false;
		}
	}
	return true;
}

void frustum_corner_points_world_space (mat4 projection_view, vec4 *corners) {
	mat4 inverse_view_proj = mat4_inverse(projection_view);

	corners[0] = (vec4){-1.0f, -1.0f, 0.0f, 1.0f};
	corners[1] = (vec4){1.0f, -1.0f, 0.0f, 1.0f};
	corners[2] = (vec4){1.0f, 1.0f, 0.0f, 1.0f};
	corners[3] = (vec4){-1.0f, 1.0f, 0.0f, 1.0f};

	corners[4] = (vec4){-1.0f, -1.0f, 1.0f, 1.0f};
	corners[5] = (vec4){1.0f, -1.0f, 1.0f, 1.0f};
	corners[6] = (vec4){1.0f, 1.0f, 1.0f, 1.0f};
	corners[7] = (vec4){-1.0f, 1.0f, 1.0f, 1.0f};

	for (u32 i = 0; i < 8; ++i) {
		vec4 point = mat4_mul_vec4(inverse_view_proj, corners[i]);
		corners[i] = vec4_div_scalar(point, point.w);
	}
}

f32 vec3_distance_to_line (vec3 point, vec3 line_start, vec3 line_direction) {
	f32 magnitude = vec3_length(vec3_cross(vec3_sub(point, line_start), line_direction));
	return magnitude / vec3_length(line_direction);
}

f32 vec3_angle (vec3 v_0, vec3 v_1) {
	f32 mag_0 = vec3_length(v_0);
	f32 mag_1 = vec3_length(v_1);

	if (mag_0 == 0.0f || mag_1 == 0.0f) {
		return 0.0f;
	}

	f32 dot = vec3_dot(v_0, v_1) * (mag_0 * mag_1);
	dot = KCLAMP(dot, -1.0f, 1.0f);

	f32 radians = kacos(dot);
	return radians * K_RAD2DEG_MULTIPLIER;
}

static void seed_randoms (void) {
	u32 ptime_u32;
	u32 ptime_u64;
#if KOHI_DEBUG
	// NOTE: Use a predetermined seed for debug builds for testing purposes.
	ptime_u32 = 42;
	ptime_u64 = 42;
#else
	// TODO: Might need to use current date/time for this in case this
	// as using the absolute time is the application _run_ time, which
	// might not be random _enough_ for this to be truly useful.
	ptime_u32 = (u32)platform_get_absolute_time();
	ptime_u64 = (u64)platform_get_absolute_time();
#endif

	// Seed standard random number generator.
	ksrand(ptime_u32);
	// 64-bit RNG
	rng_u64 = mtrand_create(ptime_u64);

	rand_seeded = true;
}

ray ray_transformed (const ray *r, mat4 transform) {
	ray out = {
		.origin = vec3_transform(r->origin, 1.0f, transform),
		.direction = vec3_normalized(vec3_transform(r->direction, 0.0f, transform)),
		.max_distance = r->max_distance,
		.flags = r->flags};

	return out;
}

ray ray_from_screen (vec2i screen_pos, rect_2di viewport_rect, vec3 origin, mat4 view, mat4 projection) {
	ray r = {0};
	r.origin = origin;

	// Get normalized device coordinates (i.e. -1:1 range).
	vec3 ray_ndc;
	ray_ndc.x = (2.0f * (screen_pos.x - viewport_rect.x)) / viewport_rect.width - 1.0f;
	ray_ndc.y = 1.0f - (2.0f * (screen_pos.y - viewport_rect.y)) / viewport_rect.height;
	ray_ndc.z = 1.0f;

	// Clip space
	vec4 ray_clip = vec4_create(ray_ndc.x, ray_ndc.y, -1.0f, 1.0f);

	// Eye/Camera
	vec4 ray_eye = mat4_mul_vec4(mat4_inverse(projection), ray_clip);

	// Unproject xy, change wz to "forward".
	ray_eye = vec4_create(ray_eye.x, ray_eye.y, -1.0f, 0.0f);

	// Convert to world coordinates;
	r.direction = vec3_from_vec4(mat4_mul_vec4(view, ray_eye));
	vec3_normalize(&r.direction);

	r.max_distance = 1000.0f;

	return r;
}

b8 ray_intersects_aabb (aabb box, vec3 origin, vec3 direction, f32 max, f32 *out_min, f32 *out_max) {
	// Slab method with divide by zero handling.
	f32 min = 0.0f;
	f32 maxi = max;
	for (u32 a = 0; a < 3; ++a) {
		f32 origin_a = origin.elements[a];
		f32 direction_a = direction.elements[a];
		f32 min_a = box.min.elements[a];
		f32 max_a = box.max.elements[a];
		if (kabs(direction_a) < K_FLOAT_EPSILON) {
			if (origin_a < min_a || origin_a > max_a) {
				return false;
			}
		} else {
			f32 inv = 1.0f / direction_a;
			f32 t1 = (min_a - origin_a) * inv;
			f32 t2 = (max_a - origin_a) * inv;
			if (t1 > t2) {
				KSWAP(f32, t1, t2);
			}
			if (t1 > min) {
				min = t1;
			}
			if (t2 < maxi) {
				maxi = t2;
			}
			if (min > maxi) {
				return false;
			}
		}
	}
	if (out_min) {
		*out_min = min;
	}
	if (out_max) {
		*out_max = maxi;
	}
	return true;
}

b8 raycast_plane_3d (const ray *r, const plane_3d *p, vec3 *out_point, f32 *out_distance) {
	f32 normal_dir = vec3_dot(r->direction, p->normal);
	f32 point_normal = vec3_dot(r->origin, p->normal);

	// If the ray and plane normal point in the same direction, there can't be a hit.
	if (normal_dir >= 0.0f) {
		return false;
	}

	// Calculate the distance.
	f32 t = (p->distance - point_normal) / normal_dir;

	// Distance must be positive or 0, otherwise the ray hits behind the plane,
	// which technically isn't a hit at all.
	if (t >= 0.0f) {
		*out_distance = t;
		*out_point = vec3_add(r->origin, vec3_mul_scalar(r->direction, t));
		return true;
	}

	return false;
}

b8 raycast_disc_3d (const ray *r, vec3 center, vec3 normal, f32 outer_radius, f32 inner_radius, vec3 *out_point, f32 *out_distance) {
	if (!r) {
		return false;
	}

	plane_3d p = plane_3d_create(center, normal);
	if (raycast_plane_3d(r, &p, out_point, out_distance)) {
		// Square the radii and compare against squared distance
		f32 orad_sq = outer_radius * outer_radius;
		f32 irad_sq = inner_radius * inner_radius;
		f32 dist_sq = vec3_distance_squared(center, *out_point);
		if (dist_sq > orad_sq) {
			return false;
		}
		if (/*inner_radius > 0 &&*/ dist_sq < irad_sq) {
			return false;
		}
		return true;
	}

	return false;
}

static b8 ray_intersects_triangle_internal (const ray *r, const triangle *t, b8 backface_cull, f32 *out_t, vec3 *out_pos, vec3 *out_normal) {
	vec3 edge_1 = vec3_sub(t->verts[1], t->verts[0]);
	vec3 edge_2 = vec3_sub(t->verts[2], t->verts[0]);
	vec3 p = vec3_cross(r->direction, edge_2);
	f32 determinant = vec3_dot(edge_1, p);

	if (backface_cull) {
		if (determinant < K_FLOAT_EPSILON) {
			return false;
		}
	} else {
		if (determinant > -K_FLOAT_EPSILON && determinant < K_FLOAT_EPSILON) {
			return false;
		}
	}

	f32 inv_det = 1.0f / determinant;

	vec3 s = vec3_sub(r->origin, t->verts[0]);
	f32 u = inv_det * vec3_dot(s, p);
	if (u < 0.0f || u > 1.0f) {
		return false;
	}

	vec3 q = vec3_cross(s, edge_1);
	f32 v = inv_det * vec3_dot(r->direction, q);
	if (v < 0.0f || (u + v) > 1.0f) {
		return false;
	}

	f32 t_hit = inv_det * vec3_dot(edge_2, q);
	if (t_hit < 0.0f || t_hit > r->max_distance) {
		return false;
	}

	if (out_t) {
		*out_t = t_hit;
	}
	if (out_pos) {
		*out_pos = vec3_add(r->origin, vec3_mul_scalar(r->direction, t_hit));
	}
	if (out_normal) {
		*out_normal = vec3_normalized(vec3_cross(edge_1, edge_2));
	}

	return true;
}

b8 ray_intersects_triangle (const ray *r, const triangle *t) {
	return ray_intersects_triangle_internal(r, t, false, KNULL, KNULL, KNULL);
}

vec3 get_pos_from_vector_at (u32 vertex_count, u32 vertex_element_size, const void *vertices, u32 index) {
	vec3 *v = (vec3 *)(((u64)vertices) + (vertex_element_size * index));
	return *v;
}

b8 ray_pick_triangle (const ray *r, b8 backface_cull, u32 vertex_count, u32 vertex_element_size, const void *vertices, u32 index_count, const u32 *indices, triangle *out_triangle, vec3 *out_hit_pos, vec3 *out_hit_normal) {
	b8 hit_any = false;
	f32 closest_dist = r->max_distance;

	for (u32 i = 0; i < index_count; i += 3) {
		triangle t = {
			get_pos_from_vector_at(vertex_count, vertex_element_size, vertices, indices[i + 0]),
			get_pos_from_vector_at(vertex_count, vertex_element_size, vertices, indices[i + 1]),
			get_pos_from_vector_at(vertex_count, vertex_element_size, vertices, indices[i + 2])};

		f32 t_hit;
		vec3 hit_pos, hit_normal;
		if (ray_intersects_triangle_internal(r, &t, backface_cull, &t_hit, &hit_pos, &hit_normal)) {
			if (t_hit < closest_dist) {
				closest_dist = t_hit;
				hit_any = true;

				if (out_triangle) {
					*out_triangle = t;
				}

				if (out_hit_pos) {
					*out_hit_pos = hit_pos;
				}

				if (out_hit_normal) {
					*out_hit_normal = hit_normal;
				}
			}
		}
	}

	return hit_any;
}

b8 ray_intersects_sphere (const ray *r, vec3 center, f32 radius, vec3 *out_point, f32 *out_distance) {
	vec3 center_to_origin = vec3_sub(r->origin, center);

	f32 b = vec3_dot(center_to_origin, r->direction);
	f32 c = vec3_dot(center_to_origin, center_to_origin) - (radius * radius);

	// Ray origin is outside the sphere and pointing away from it.
	if (c > 0.0f && b > 0.0f) {
		return false;
	}

	f32 discriminant = b * b - c;
	if (discriminant < 0.0f) {
		return false;
	}

	// Closest intersection distance
	f32 t = -b - ksqrt(discriminant);

	// Ray starts inside sphere.
	if (t < 0.0f) {
		t = 0.0f;
	}

	if (r->max_distance > 0.0f && t > r->max_distance) {
		return false;
	}

	if (out_distance) {
		*out_distance = t;
	}

	if (out_point) {
		*out_point = vec3_add(r->origin, vec3_mul_scalar(r->direction, t));
	}

	return true;
}

vec3 normal_from_triangle (const triangle *tri) {
	vec3 edge_0 = vec3_sub(tri->verts[1], tri->verts[0]);
	vec3 edge_1 = vec3_sub(tri->verts[2], tri->verts[0]);

	return vec3_normalized(
		vec3_create(
			edge_0.y * edge_1.z - edge_0.z * edge_1.y,
			edge_0.z * edge_1.x - edge_0.x * edge_1.z,
			edge_0.x * edge_1.y - edge_0.y * edge_1.x));
}

vec3 closest_point_on_segment (vec3 a, vec3 b, vec3 pos) {
	vec3 line_ab = vec3_sub(b, a);
	f32 t = vec3_dot(vec3_sub(pos, a), line_ab) / vec3_dot(line_ab, line_ab);
	t = KCLAMP(t, 0.0f, 1.0f);
	return vec3_add(a, vec3_mul_scalar(line_ab, t));
}

vec3 vec3_project_on_plane (vec3 pos, vec3 normal) {
	return vec3_sub(pos, vec3_mul_scalar(normal, vec3_dot(pos, normal)));
}

vec3 closest_point_on_triangle (vec3 pos, const triangle *t) {
	vec3 edge_0 = vec3_sub(t->verts[1], t->verts[0]);
	vec3 edge_1 = vec3_sub(t->verts[2], t->verts[0]);
	vec3 ap = vec3_sub(pos, t->verts[0]);

	f32 d1 = vec3_dot(edge_0, ap);
	f32 d2 = vec3_dot(edge_1, ap);
	if (d1 <= 0.0f && d2 <= 0.0f) {
		return t->verts[0];
	}

	vec3 bp = vec3_sub(pos, t->verts[1]);
	f32 d3 = vec3_dot(edge_0, bp);
	f32 d4 = vec3_dot(edge_1, bp);
	if (d3 >= 0.0f && d4 <= d3) {
		return t->verts[1];
	}

	f32 vc = d1 * d4 - d3 * d2;
	if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
		f32 v = d1 / (d1 - d3);
		return vec3_add(t->verts[0], vec3_mul_scalar(edge_0, v));
	}

	vec3 cp = vec3_sub(pos, t->verts[2]);
	f32 d5 = vec3_dot(edge_0, cp);
	f32 d6 = vec3_dot(edge_1, cp);
	if (d6 >= 0.0f && d5 <= d6) {
		return t->verts[2];
	}

	f32 vb = d5 * d2 - d1 * d6;
	if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
		f32 w = d2 / (d2 - d6);
		return vec3_add(t->verts[0], vec3_mul_scalar(edge_1, w));
	}

	f32 va = d3 * d6 - d5 * d4;
	f32 denom = 1.0f / (va + vb + vc);
	f32 v = vb * denom;
	f32 w = vc * denom;

	return vec3_add(t->verts[0], vec3_add(vec3_mul_scalar(edge_0, v), vec3_mul_scalar(edge_1, w)));
}

// Return whether point P is contained inside 3D region delimited by triangle T0,T1,T2 edges.
b8 point_inside_triangle (vec3 p, const triangle *tri) {
	// Real-Time Collision Detection: 3.4: Barycentric Coordinates (pages 46-52).
	//
	// The book also has a subsection dedicated to point inside triangle tests:
	// Real-Time Collision Detection: 5.4.2: Testing Point in Triangle (pages 203-206).
	// But those tests only work for CCW triangles. This seems to work for either orientation.
	vec3 t0 = vec3_sub(tri->verts[1], tri->verts[0]);
	vec3 t1 = vec3_sub(tri->verts[2], tri->verts[0]);
	vec3 tp = vec3_sub(p, tri->verts[0]);
	f32 t0_dot_t0 = vec3_dot(t0, t0);
	f32 t0_dot_t1 = vec3_dot(t0, t1);
	f32 t1_dot_t1 = vec3_dot(t1, t1);
	f32 tp_dot_t0 = vec3_dot(tp, t0);
	f32 tp_dot_t1 = vec3_dot(tp, t1);
	f32 denom = t0_dot_t0 * t1_dot_t1 - t0_dot_t1 * t0_dot_t1;

	// Normally I would have to divide vd,wd by denom to get v,w. But divisions are
	// expensive and cause troubles around 0. If denom isn't negative then we don't
	// ever need to divide. If in the future it does turn out denom can be negative
	// then we can always multiply by denom instead of dividing to keep sign the same.
	f32 vd = t1_dot_t1 * tp_dot_t0 - t0_dot_t1 * tp_dot_t1;
	f32 wd = t0_dot_t0 * tp_dot_t1 - t0_dot_t1 * tp_dot_t0;
	return vd >= 0 && wd >= 0 && vd + wd <= denom;
}

// Return whether point P is contained inside 3D region delimited by parallelogram P0,P1,P2 edges.
b8 point_inside_parallelogram (vec3 p, vec3 p0, vec3 p1, vec3 p2) {
	// There may be a better way.
	// https://math.stackexchange.com/questions/4381852/point-in-parallelogram-in-3d-space
	vec3 p3 = vec3_add(p2, vec3_sub(p1, p0));
	triangle t0, t1;
	t0.verts[0] = p0;
	t0.verts[1] = p1;
	t0.verts[2] = p2;

	t1.verts[0] = p1;
	t1.verts[1] = p3;
	t1.verts[2] = p2;
	return point_inside_triangle(p, &t0) || point_inside_triangle(p, &t1);
}

// Return whether point P is contained inside a triangular prism A0,A1,A2-B0,B1,B2.
b8 point_inside_triangular_prism (vec3 p, vec3 a0, vec3 a1, vec3 a2, vec3 b0, vec3 b1, vec3 b2) {
	vec3 faces[5][3] = {{a0, a1, a2}, {b0, b2, b1}, {a0, b0, a1}, {a1, b1, a2}, {a2, b2, a0}};
	f32 sgn = 0;
	for (u32 i = 0; i < 5; i++) {
		vec3 p0 = faces[i][0];
		vec3 p1 = faces[i][1];
		vec3 p2 = faces[i][2];

		// Check which side of plane point is in. If it's always on the same side, it's colliding.
		vec3 p01 = vec3_sub(p1, p0);
		vec3 p02 = vec3_sub(p2, p0);
		vec3 n = vec3_cross(p01, p02);
		VEC3_CHECK_NAN(n);
		f32 d = vec3_dot(n, vec3_sub(p, p0));
		if (i == 0) {
			sgn = d;
		}
		if (sgn * d <= 0) {
			return false;
		}
	}
	return true;
}

// Sweep sphere C,r with velocity Sv against plane N of triangle T0,T1,T2, ignoring edges.
b8 sweep_sphere_triangle_plane (sweep_result *sweep, vec3 c, f32 r, vec3 v, vec3 t0, vec3 t1, vec3 t2, vec3 n) {
	// Real-Time Collision Detection 5.5.3: Intersecting Moving Sphere Against Plane (pages 219-223).
	f32 t;
	f32 d = vec3_dot(n, vec3_sub(c, t0));
	f32 pen = r - d;
	if (pen > 0) {
		t = 0; // Sphere already starts coliding with triangle plane.
	} else {
		// Sphere isn't immediately colliding with the plane. Check if it's moving away.
		f32 denom = vec3_dot(n, v);
		if (denom >= 0) {
			return false; // Sphere is moving away from plane.
		}

		// Sphere will collide with plane at some point.
		t = (r - d) / denom;
		pen = 0;
	}

	// If sphere misses entire triangle plane, then it definitely misses the triangle too.
	if (t >= sweep->time) {
		return false;
	}

	// Is the plane collision point inside the triangle?
	// Real-Time Collision Detection: 5.4.2: Testing Point in Triangle (pg 203-206).
	/* vec3 collision = vec3_add(c, vec3_sub(vec3_mul_scalar(v, t), vec3_mul_scalar(n, r))); */
	vec3 collision = vec3_sub(vec3_add(c, vec3_mul_scalar(v, t)), vec3_mul_scalar(n, r));
	triangle tri;
	tri.verts[0] = t0;
	tri.verts[1] = t1;
	tri.verts[2] = t2;
	if (!point_inside_triangle(collision, &tri)) {
		return false;
	}

	// Plane collision point is inside the triangle. So the sphere collides with the triangle.
	sweep->time = t;
	sweep->depth = pen;
	sweep->point = collision;
	sweep->normal = n;
	VEC3_CHECK_NAN(sweep->normal);
	VEC3_CHECK_NAN(sweep->point);
	return true;
}

// Sweep sphere C,r with velocity V against plane N of parallelogram P0,P1,P2 ignoring edges.
b8 sweep_sphere_parallelogram_plane (sweep_result *sweep, vec3 c, f32 r, vec3 v, vec3 p0, vec3 p1, vec3 p2, vec3 n) {
	// Real-Time Collision Detection 5.5.3: Intersecting Moving Sphere Against Plane (pages 219-223).
	f32 t;
	/* f32 d = vec3_dot(c, vec3_sub(n, p0)); */
	f32 d = vec3_dot(n, vec3_sub(c, p0));
	f32 pen = r - d;
	if (pen > 0) {
		t = 0; // Sphere already starts coliding with the quad plane.
	} else {
		// Sphere isn't immediately colliding with the plane. Check if it's moving away.
		f32 denom = vec3_dot(n, v);
		if (denom >= 0) {
			return false; // Sphere is moving away from plane.
		}

		// Sphere will collide with plane at some point.
		t = (r - d) / denom;
		pen = 0;
	}

	// If sphere misses entire quad plane, then it definitely misses the quad too.
	if (t >= sweep->time) {
		return false;
	}

	// Is the plane collision point inside the quad?
	// Real-Time Collision Detection: 5.4.2: Testing Point in Triangle (pages 203-206).
	/* vec3 collision = vec3_add(c, vec3_sub(vec3_mul_scalar(v, t), vec3_mul_scalar(n, r))); */
	vec3 collision = vec3_sub(vec3_add(c, vec3_mul_scalar(v, t)), vec3_mul_scalar(n, r));
	if (!point_inside_parallelogram(collision, p0, p1, p2)) {
		return false;
	}

	// Plane collision point is inside the quad. So the sphere collides with the quad.
	sweep->time = t;
	sweep->depth = pen;
	sweep->point = collision;
	sweep->normal = n;
	VEC3_CHECK_NAN(sweep->normal);
	VEC3_CHECK_NAN(sweep->point);
	return true;
}

// Sweep point P with velocity V against sphere S,r.
b8 sweep_point_sphere (sweep_result *sweep, vec3 p, vec3 v, vec3 s, f32 r, vec3 fallback_normal) {
	// Real-Time Collision Detection 5.3.2: Intersecting Ray or Segment Against Sphere (pages 177-179).

	// Set up quadratic equation.
	vec3 d = vec3_sub(p, s);
	f32 b = vec3_dot(d, v);
	f32 c = vec3_dot(d, d) - r * r;
	if (c > 0 && b > 0) {
		return false; // Point starts outside (c > 0) and moves away from sphere (b > 0).
	}
	f32 a = vec3_dot(v, v);
	f32 discr = b * b - a * c;
	if (discr < 0) {
		return false; // Point misses sphere.
	}

	// Point hits sphere. Compute time of first impact.
	f32 t = (-b - ksqrt(discr)) / a;
	if (t >= sweep->time) {
		return false;
	}

	// The sphere is the first thing the point hits so far.
	t = KMAX(t, 0);
	vec3 collision = vec3_add(p, vec3_mul_scalar(v, t));
	vec3 vec = vec3_sub(collision, s);
	f32 len = vec3_length(vec);
	sweep->time = t;
	sweep->depth = t > 0 ? 0 : r - len;
	sweep->point = collision;
	sweep->normal = (len >= SWEEP_EPSILON) ? vec3_div_scalar(vec, len) : fallback_normal;
	VEC3_CHECK_NAN(sweep->normal);
	VEC3_CHECK_NAN(sweep->point);
	return true;
}

// Sweep point P with velocity V against cylinder C0,C1,r, ignoring the endcaps.
b8 sweep_point_uncapped_cylinder (sweep_result *sweep, vec3 p, vec3 v, vec3 c0, vec3 c1, f32 r, vec3 fallback_normal) {
	// Real-Time Collision Detection 5.3.7: Intersecting Ray or Segment Against Cylinder (pages 194-198).

	// Test if swept point is fully outside of either endcap.
	vec3 n = vec3_sub(c1, c0);
	vec3 d = vec3_sub(p, c0);
	f32 dn = vec3_dot(d, n);
	f32 vn = vec3_dot(v, n);
	f32 nn = vec3_dot(n, n);
	if (dn < 0 && dn + vn < 0) {
		return false; // Fully outside c0 end of cylinder.
	}
	if (dn > nn && dn + vn > nn) {
		return false; // Fully outside c1 end of cylinder.
	}

	// Set up quadratic equations and check if sweep direction is parallel to cylinder.
	f32 t;
	f32 vv = vec3_dot(v, v);
	f32 dv = vec3_dot(d, v);
	f32 dd = vec3_dot(d, d);
	f32 a = nn * vv - vn * vn;
	f32 c = nn * (dd - r * r) - dn * dn;
	if (kabs(a) < SWEEP_EPSILON) {
		// Sweep direction is parallel to cylinder.
		if (c > 0) {
			return false; // Point starts outside of cylinder, so it never collides.
		}
		if (dn < 0) {
			return false; // Point starts outside of c0 endcap.
		}
		if (dn > nn) {
			return false; // Point starts outside of c1 endcap.
		}
		t = 0;
	} else {
		// Sweep direction is not parallel to cylinder. Solve for time of first contact.
		f32 b = nn * dv - vn * dn;
		f32 discr = b * b - a * c;
		if (discr < 0) {
			return false; // Sweep misses cylinder.
		}
		t = (-b - ksqrt(discr)) / a;
	}

	// Check if the sweep missed, or if it hits but another collision happens sooner.
	if (t < 0 || t >= sweep->time) {
		return false;
	}

	// This is the first collision. Find the closest point on the center of the cylinder.
	vec3 collision = vec3_add(p, vec3_mul_scalar(v, t));
	vec3 center;
	if (nn < SWEEP_EPSILON) {
		center = c0; // The cylinder is actually a circle.
	} else {
		f32 proj = vec3_dot(vec3_sub(collision, c0), n) / nn;
		center = vec3_add(c0, vec3_mul_scalar(n, proj));
	}

	// Update collision time, depth, and normal.
	vec3 vec = vec3_sub(collision, center);
	f32 len = vec3_length(vec);
	f32 depth = r - len;
	sweep->time = t;
	sweep->depth = t > 0 ? 0 : depth;
	sweep->point = collision;
	sweep->normal = (len >= SWEEP_EPSILON) ? vec3_div_scalar(vec, len) : fallback_normal;
	VEC3_CHECK_NAN(sweep->normal);
	VEC3_CHECK_NAN(sweep->point);
	return true;
}

// Sweep a capsule C0,C1,Cr with velocity Cv against the triangle T0,T1,T2.
//   c0,c1      capsule line segment endpoints
//   r          capsule radius
//   v          capsule velocity
//   t0,t1,t2   3 triangle vertices
//   returns    whether the capsule and triangle intersect
b8 sweep_capsule_triangle (sweep_result *s, vec3 c0, vec3 c1, f32 r, vec3 v, vec3 t0, vec3 t1, vec3 t2) {
	// Compute triangle plane equation.
	vec3 t01 = vec3_sub(t1, t0);
	vec3 t02 = vec3_sub(t2, t0);
	vec3 normal = vec3_normalized(vec3_cross(t01, t02));

	// Extrude triangle along capsule direction.
	vec3 c01 = vec3_sub(c1, c0);
	vec3 a0 = t0;
	vec3 a1 = t1;
	vec3 a2 = t2;
	vec3 b0 = vec3_sub(t0, c01);
	vec3 b1 = vec3_sub(t1, c01);
	vec3 b2 = vec3_sub(t2, c01);

	// Test for initial collision with the extruded triangle prism.
	if (point_inside_triangular_prism(c0, a0, a1, a2, b0, b1, b2)) {
		// Capsule starts off penetrating triangle. Push it out from the triangle plane.
		f32 d0 = vec3_dot(normal, vec3_sub(c0, t0));
		f32 d1 = vec3_dot(normal, vec3_sub(c1, t0));
		f32 d = kabs(d0) <= kabs(d1) ? d0 : d1;
		vec3 n = d >= 0 ? normal : vec3_mul_scalar(normal, -1.0f);
		s->time = 0;
		s->depth = kabs(d) + r;
		s->normal = n;
		s->point = vec3_add(c0, vec3_mul_scalar(normal, d0));

		VEC3_CHECK_NAN(s->normal);
		VEC3_CHECK_NAN(s->point);
		return true;
	}

	// Decompose capsule triangle sweep into: 2 sphere-triangle + 3 sphere-parallelogram + 9 point-cylinder + 6 point-sphere sweeps.
	b8 hit = false;
	vec3 triangles[2][3] = {{a0, a1, a2}, {b0, b1, b2}};
	vec3 parallelograms[3][3] = {{a0, a1, b0}, {a1, a2, b1}, {a2, a0, b2}};
	vec3 cylinders[9][2] = {{a0, a1}, {a1, a2}, {a2, a0}, {b0, b1}, {b1, b2}, {b2, b0}, {a0, b0}, {a1, b1}, {a2, b2}};
	vec3 spheres[6] = {a0, a1, a2, b0, b1, b2};

	// Do sphere-triangle sweeps.
	vec3 triangle_normals[2];
	for (u32 i = 0; i < 2; i++) {
		vec3 p0 = triangles[i][0];
		vec3 p1 = triangles[i][1];
		vec3 p2 = triangles[i][2];

		// Compute triangle plane normal.
		vec3 n = normal;
		if (vec3_dot(n, vec3_sub(c0, p0)) < 0) {
			n = vec3_mul_scalar(n, -1.0f); // Orient towards sphere.
		}
		triangle_normals[i] = n;
		VEC3_CHECK_NAN(n);

		// Test for triangle-plane sphere intersection.
		hit = hit || sweep_sphere_triangle_plane(s, c0, r, v, p0, p1, p2, n);
	}

	// Do sphere-parallelogram sweeps.
	vec3 parallelogram_normals[3];
	for (u32 i = 0; i < 3; i++) {
		vec3 p0 = parallelograms[i][0];
		vec3 p1 = parallelograms[i][1];
		vec3 p2 = parallelograms[i][2];

		// Check if quad is degenerate. Happens when triangle edge completely parallel to capsule.
		vec3 p01 = vec3_sub(p1, p0);
		vec3 p02 = vec3_sub(p2, p0);
		vec3 c = vec3_cross(p01, p02);
		f32 len = vec3_length(c);
		if (len > SWEEP_EPSILON) {
			// Compute quad plane equation.
			vec3 n = vec3_div_scalar(c, len);
			if (vec3_dot(n, vec3_sub(c0, p0)) < 0) {
				n = vec3_mul_scalar(n, -1.0f); // Orient towards sphere.
			}
			parallelogram_normals[i] = n;
			VEC3_CHECK_NAN(n);

			// Do the sweep test.
			hit = hit || sweep_sphere_parallelogram_plane(s, c0, r, v, p0, p1, p2, n);
		} else {
			parallelogram_normals[i] = triangle_normals[0];
		}
	}

	// Do point-cylinder sweeps.
	for (u32 i = 0; i < 9; i++) {
		vec3 p0 = cylinders[i][0];
		vec3 p1 = cylinders[i][1];
		vec3 n;
		if (i < 6) {
			n = triangle_normals[i / 3];
		} else {
			n = parallelogram_normals[i - 6];
		}
		VEC3_CHECK_NAN(n);
		hit = hit || sweep_point_uncapped_cylinder(s, c0, v, p0, p1, r, n);
	}

	// Do point-sphere sweeps.
	for (u32 i = 0; i < 6; i++) {
		vec3 c = spheres[i];
		vec3 n = triangle_normals[i / 3];
		VEC3_CHECK_NAN(n);
		hit = hit || sweep_point_sphere(s, c0, v, c, r, n);
	}

	return hit;
}
