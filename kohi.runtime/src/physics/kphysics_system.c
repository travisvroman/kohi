#include "kphysics_system.h"
#include "containers/darray.h"
#include "containers/kpool.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "logger.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "physics/physics_types.h"

typedef struct kphysics_body {
    kname name;
    // Used for handle verification.
    u64 uniqueid;

    kphysics_body_type body_type;
    kphysics_shape_type shape_type;

    vec3 velocity;

    vec3 position;
    quat rotation;

    // Sphere props.
    f32 radius;

    // rectangle
    vec3 half_extents;

    // mesh
    u32 triangle_count;
    triangle_3d* tris;

} kphysics_body;

typedef struct kphysics_collision_data {
    vec3 normal;
    f32 depth;
    kphysics_body* body_0;
    kphysics_body* body_1;
} kphysics_collision_data;

typedef struct kphysics_system_state {
    kphysics_system_config config;

    kphysics_world* active_world;

    // A pool of all physics bodies created.
    kpool all_bodies;
} kphysics_system_state;

static kphysics_body* create_body(kphysics_system_state* state, khandle* out_handle);
static void destroy_body(kphysics_system_state* state, khandle* handle);
static kphysics_body* get_body(kphysics_system_state* state, khandle handle);

b8 kphysics_system_initialize(u64* memory_requirement, kphysics_system_state* state, kphysics_system_config* config) {
    if (!memory_requirement) {
        return false;
    }

    *memory_requirement = sizeof(kphysics_system_state);

    if (!state) {
        return true;
    }

    if (config) {
        state->config = *config;
    } else {
        // Set some reasonable defaults.
        state->config.steps_per_frame = 10;
    }

    state->active_world = 0;

    // TODO: May need to increase this or make it configurable.
    if (!kpool_create(sizeof(kphysics_body), 512, &state->all_bodies)) {
        KERROR("Failed to create physics body pool");
        return false;
    }

    KINFO("Physics system initialized.");

    return true;
}

void kphysics_system_shutdown(kphysics_system_state* state) {
    if (state) {
        kpool_destroy(&state->all_bodies);

        kzero_memory(state, sizeof(kphysics_system_state));
    }
}

static void resolve_collision(kphysics_body* body_0, kphysics_body* body_1, const kphysics_collision_data* collision) {
    if (body_0->body_type == KPHYSICS_BODY_TYPE_STATIC) {
        // Static bodies don't move.
        return;
    }

    // Resolve
    if (body_1->body_type == KPHYSICS_BODY_TYPE_DYNAMIC) {
        // Split the correction between the 2 of them.
        // TODO: This would need to be adjusted to take mass into account.
        f32 half_penetration = collision->depth * 0.5f;

        // First body
        // Move body out of penetration.
        body_0->position = vec3_mul_add_scalar(collision->normal, half_penetration, body_0->position);
        // NOTE: Simple velocity reflection.
        body_0->velocity = vec3_reflect(body_0->velocity, collision->normal);

        // Second body
        // The direction for the second body will need to be the opposite of the normal provided here.
        vec3 inv_collision_normal = vec3_mul_scalar(collision->normal, -1.0f);
        body_1->position = vec3_mul_add_scalar(inv_collision_normal, half_penetration, body_1->position);
        // NOTE: Simple velocity reflection.
        body_1->velocity = vec3_reflect(body_1->velocity, inv_collision_normal);

    } else {
        // Issue all of the correction to body_0.

        // Move body out of penetration.
        body_0->position = vec3_mul_add_scalar(collision->normal, collision->depth, body_0->position);

        f32 velocity_along_normal = vec3_dot(body_0->velocity, collision->normal);

        // HACK: hardcoded
        f32 NORMAL_DAMPING = 0.2f;
        f32 FRICTION_SCALE = 0.4f;
        f32 FRICTION_COEFFICIENT = 0.01f;
        if (velocity_along_normal < 0) {
            // Instead of completely removing velocity, keep a small portion of it.
            body_0->velocity = vec3_add(body_0->velocity, vec3_mul_scalar(collision->normal, -velocity_along_normal * (1.0f - NORMAL_DAMPING)));
        }

        // Tangent velocity (sliding)
        vec3 velocity_tangent = vec3_sub(body_0->velocity, vec3_mul_scalar(collision->normal, vec3_dot(body_0->velocity, collision->normal)));

        // Scale friction to prevent excessive slowdown uphill.
        body_0->velocity = vec3_add(velocity_tangent, vec3_mul_scalar(velocity_tangent, -FRICTION_COEFFICIENT * FRICTION_SCALE));

        // TODO: old
        /* // NOTE: Simple velocity reflection.
        body_0->velocity = vec3_reflect(body_0->velocity, collision->normal);

        // Apply Friction
        const f32 friction = 0.005f; // TODO: Apply based on surface properties.

        f32 dot = vec3_dot(body_0->velocity, collision->normal);

#if 0
        // Method 1 - very sticky, moves slowly.
        // Adjust velocity to slide along collision surface.
        body_0->velocity = vec3_add(body_1->velocity, vec3_mul_scalar(collision->normal, -dot));
        // Apply Friction
        body_0->velocity = vec3_add(body_1->velocity, vec3_mul_scalar(collision->normal, -friction));

#else

        // Method 2 - faster, but struggles going uphill. Very "slippery"
        // Get the velocity tangent (perpendicular to instantaneous path of motion)
        vec3 velocity_tangent = vec3_sub(body_0->velocity, vec3_mul_scalar(collision->normal, dot));
        // Apply Friction
        body_0->velocity = vec3_add(velocity_tangent, vec3_mul_scalar(velocity_tangent, -friction));
#endif */
    }
}

// TODO: move this
vec3 vec3_rotate_quat_inv(quat q, vec3 v) {
    quat inv_q = quat_inverse(q);

    // Convert vec3 to quat form.
    quat v_quat = {v.x, v.y, v.z, 0};

    // Apply inverse rotation
    quat temp = quat_mul(inv_q, v_quat);
    quat result = quat_mul(temp, q);

    return (vec3){result.x, result.y, result.z};
}

b8 check_sphere_sphere_collision(vec3 position_0, f32 radius_0, vec3 position_1, f32 radius_1, kphysics_collision_data* out_collision) {
    f32 body_0_radius_sq = radius_0 * radius_0;
    f32 body_1_radius_sq = radius_1 * radius_1;
    f32 dist_sq = vec3_distance_squared(position_0, position_1);
    if (body_0_radius_sq + body_1_radius_sq > dist_sq) {
        // Have a collision
        out_collision->depth = body_0_radius_sq + body_1_radius_sq - dist_sq;
        out_collision->normal = vec3_normalized(vec3_sub(position_0, position_1));
        return true;
    }

    return false;
}

f32 point_plane_distance(vec3 point, vec3 plane_point, vec3 plane_normal) {
    return vec3_dot(vec3_sub(point, plane_point), plane_normal);
}

b8 check_sphere_triangle_collision(vec3 sphere_center, f32 sphere_radius, const triangle_3d* tri, kphysics_collision_data* out_collision) {
    /* vec3 closest_point = triangle_3d_get_closest_point(sphere_center, tri);

    vec3 sphere_to_closest = vec3_sub(closest_point, sphere_center);
    f32 distance = vec3_length(sphere_to_closest);

    if (distance <= sphere_radius) {
        // Collision detected!
        out_collision->depth = sphere_radius - distance;
        out_collision->normal = vec3_normalized(sphere_to_closest);
        return true;
    }

    return false; */

    vec3 triangle_normal = triangle_3d_get_normal(tri);

    // Project sphere center onto triangle plane.
    f32 distance = point_plane_distance(sphere_center, tri->verts[0], triangle_normal);
    f32 abs_dist = kabs(distance);

    // Quick check to see if the triangle plane is anywhere near the sphere.
    if (abs_dist > sphere_radius) {
        // No collision.
        return false;
    }

    // Compute the closest point on the triangle to the sphere.
    vec3 projection = vec3_sub(sphere_center, vec3_mul_scalar(triangle_normal, distance));

    // Barycentric test  to check if the projection is inside the triangle.
    vec3 c_0 = vec3_cross(vec3_sub(tri->verts[1], tri->verts[0]), vec3_sub(projection, tri->verts[0]));
    vec3 c_1 = vec3_cross(vec3_sub(tri->verts[2], tri->verts[1]), vec3_sub(projection, tri->verts[1]));
    vec3 c_2 = vec3_cross(vec3_sub(tri->verts[0], tri->verts[2]), vec3_sub(projection, tri->verts[2]));

    // Is this inside the triangle?
    if (vec3_dot(c_0, triangle_normal) >= 0 && vec3_dot(c_1, triangle_normal) >= 0 && vec3_dot(c_2, triangle_normal) >= 0) {

        out_collision->normal = triangle_normal;
        out_collision->depth = sphere_radius - abs_dist;

        return true;
    }

    return false;
}

b8 check_obb_sphere_collision(const oriented_bounding_box* obb, vec3 sphere_center, f32 sphere_radius, kphysics_collision_data* out_collision) {
    // Convert sphere center to OBB's local space.
    vec3 local_sphere_center = vec3_rotate_quat_inv(obb->rotation, vec3_sub(sphere_center, obb->center));

    // Get closest point inside OBB
    vec3 closest_point = {
        KMAX(-obb->half_extents.x, KMIN(local_sphere_center.x, obb->half_extents.x)),
        KMAX(-obb->half_extents.y, KMIN(local_sphere_center.y, obb->half_extents.y)),
        KMAX(-obb->half_extents.z, KMIN(local_sphere_center.z, obb->half_extents.z))};

    // Transform closest point back to world space.
    vec3 world_closest_point = vec3_add(obb->center, vec3_rotate(closest_point, obb->rotation));

    // Compute distance from closest point to sphere center.
    vec3 diff = vec3_sub(sphere_center, world_closest_point);
    f32 dist_sq = vec3_dot(diff, diff);
    f32 radius_sq = sphere_radius * sphere_radius;

    if (dist_sq > radius_sq) {
        return false; // no collision
    }

    // Calculate penetration depth and collision normal.
    f32 distance = ksqrt(dist_sq);
    out_collision->depth = sphere_radius - distance;
    out_collision->normal = (distance > 0) ? vec3_mul_scalar(diff, 1.0f / distance) : (vec3){1, 0, 0}; // Prevent divide by zero

    return true;
}

b8 check_obb_obb_collision(const oriented_bounding_box* obb_0, const oriented_bounding_box* obb_1, kphysics_collision_data* out_collision) {
    vec3 axes[15];
    // Local axes for bodies.
    axes[0] = vec3_rotate((vec3){1, 0, 0}, obb_0->rotation);
    axes[1] = vec3_rotate((vec3){0, 1, 0}, obb_0->rotation);
    axes[2] = vec3_rotate((vec3){0, 0, 1}, obb_0->rotation);

    axes[3] = vec3_rotate((vec3){1, 0, 0}, obb_1->rotation);
    axes[4] = vec3_rotate((vec3){0, 1, 0}, obb_1->rotation);
    axes[5] = vec3_rotate((vec3){0, 0, 1}, obb_1->rotation);

    // cross-product axes.
    u32 axis_count = 6;
    for (u32 i = 0; i < 3; ++i) {
        for (u32 j = 0; j < 3; ++j) {
            vec3 cross_product = vec3_cross(axes[i], axes[j]);
            if (cross_product.x != 0 || cross_product.y != 0 || cross_product.z != 0) {
                axes[axis_count++] = vec3_normalized(cross_product);
            }
        }
    }

    // Get the center difference.
    vec3 center_delta = vec3_sub(obb_1->center, obb_0->center);

    f32 min_penetration = K_FLOAT_MAX;
    vec3 best_axis = vec3_zero();

    // Test each axis for separation
    for (u32 i = 0; i < axis_count; ++i) {
        vec3 axis = axes[i];

        // Project both OBBs onto the axis.
        f32 proj_0 = oriented_bounding_box_project(obb_0, axis);
        f32 proj_1 = oriented_bounding_box_project(obb_1, axis);

        f32 center_proj = kabs(vec3_dot(axis, center_delta));

        // Check for a gap.
        if (center_proj > (proj_0 + proj_1)) {
            return false;
        }

        // Compute penetration_depth
        f32 penetration = (proj_0 + proj_1) - center_proj;
        if (penetration < min_penetration) {
            min_penetration = penetration;
            best_axis = axis;
        }
    }

    // If no gap was found, return collision info.
    out_collision->depth = min_penetration;
    out_collision->normal = vec3_normalized(best_axis);

    return true;
}

static void get_obb_vertices(const oriented_bounding_box* obb, vec3 out_vertices[8]) {
    vec3 axes[3] = {
        vec3_rotate((vec3){1, 0, 0}, obb->rotation),
        vec3_rotate((vec3){0, 1, 0}, obb->rotation),
        vec3_rotate((vec3){0, 0, 1}, obb->rotation)};

    for (u8 i = 0; i < 8; ++i) {
        vec3 corner_offset = {
            (i & 1 ? 1 : -1) * obb->half_extents.x,
            (i & 2 ? 1 : -1) * obb->half_extents.y,
            (i & 4 ? 1 : -1) * obb->half_extents.z};

        out_vertices[i] = vec3_add(
            obb->center,
            vec3_add(
                vec3_mul_scalar(axes[0], corner_offset.x),
                vec3_add(
                    vec3_mul_scalar(axes[1], corner_offset.y),
                    vec3_mul_scalar(axes[2], corner_offset.z))));
    }
}

static b8 check_obb_triangle_collision(const oriented_bounding_box* obb, const triangle_3d* tri, kphysics_collision_data* out_collision) {
    vec3 obb_axes[3] = {
        vec3_rotate((vec3){1, 0, 0}, obb->rotation),
        vec3_rotate((vec3){0, 1, 0}, obb->rotation),
        vec3_rotate((vec3){0, 0, 1}, obb->rotation)};

    vec3 tri_edges[3] = {
        vec3_sub(tri->verts[1], tri->verts[0]),
        vec3_sub(tri->verts[2], tri->verts[1]),
        vec3_sub(tri->verts[0], tri->verts[2])};

    vec3 tri_normal = vec3_normalized(vec3_cross(tri_edges[0], tri_edges[1]));

    vec3 obb_vertices[8];
    get_obb_vertices(obb, obb_vertices);

    f32 min_0, max_0, min_1, max_1;
    f32 min_penetration = K_FLOAT_MAX;
    vec3 best_axis = {0, 0, 0};

    // Test OBB axes
    for (u8 i = 0; i < 3; ++i) {
        vec3 tri_verts[3] = {
            tri->verts[0],
            tri->verts[1],
            tri->verts[2]};
        vec3_project_points_onto_axis(tri_verts, 3, obb_axes[i], &min_0, &max_0);
        vec3_project_points_onto_axis(obb_vertices, 8, obb_axes[i], &min_1, &max_1);

        if (max_0 < min_1 || max_1 < min_0) {
            return false; // Separating axis found, no collision
        }

        f32 penetration = KMIN(max_0 - min_1, max_1 - min_0);
        if (penetration < min_penetration) {
            min_penetration = penetration;
            best_axis = obb_axes[i];
        }
    }

    // Test triangle normal
    vec3 tri_verts[3] = {
        tri->verts[0],
        tri->verts[1],
        tri->verts[2]};
    vec3_project_points_onto_axis(tri_verts, 3, tri_normal, &min_0, &max_0);
    vec3_project_points_onto_axis(obb_vertices, 8, tri_normal, &min_1, &max_1);

    if (max_0 < min_1 || max_1 < min_0) {
        return false; // Separating axis found, no collision
    }

    f32 penetration = KMIN(max_0 - min_1, max_1 - min_0);
    if (penetration < min_penetration) {
        min_penetration = penetration;
        best_axis = tri_normal;
    }

    // Test cross products of OBB axes and triangle edges
    for (u8 i = 0; i < 3; ++i) {
        for (u8 j = 0; j < 3; ++j) {
            vec3 axis = vec3_cross(obb_axes[i], tri_edges[j]);
            if (vec3_length(axis) > 1e-6) { // Avoid zero-length vectors
                vec3_normalize(&axis);
                vec3 tri_verts[3] = {
                    tri->verts[0],
                    tri->verts[1],
                    tri->verts[2]};
                vec3_project_points_onto_axis(tri_verts, 3, axis, &min_0, &max_0);
                vec3_project_points_onto_axis(obb_vertices, 8, axis, &min_1, &max_1);

                if (max_0 < min_1 || max_1 < min_0) {
                    return false; // Separating axis found, no collision
                }

                f32 penetration = KMIN(max_0 - min_1, max_1 - min_0);
                if (penetration < min_penetration) {
                    min_penetration = penetration;
                    best_axis = axis;
                }
            }
        }
    }

    // No separating axis found, there is a collision
    out_collision->normal = best_axis;
    out_collision->depth = min_penetration;
    return true;
}

/* static b8 check_obb_triangle_collision(const oriented_bounding_box* obb, const triangle_3d* tri, vec3* out_collision_normal, f32* out_penetration_depth) {
    // Transform tringle into OBB-local space.
    triangle_3d local_tri;
    for (u8 i = 0; i < 3; ++i) {
        local_tri.verts[i] = vec3_rotate_quat_inv(obb->rotation, vec3_sub(tri->verts[i], obb->center));
    }

    vec3 edge_0 = vec3_sub(tri->verts[1], tri->verts[0]);
    vec3 edge_1 = vec3_sub(tri->verts[2], tri->verts[0]);
    vec3 tri_normal = vec3_normalized(vec3_cross(edge_0, edge_1));

    // SAT axes
    vec3 obb_axes[3] = {
        vec3_rotate((vec3){1, 0, 0}, obb->rotation),
        vec3_rotate((vec3){0, 1, 0}, obb->rotation),
        vec3_rotate((vec3){0, 0, 1}, obb->rotation),
    };

    f32 min_0, min_1, max_0, max_1;

    // SAT test on each OBB axis
    for (u8 i = 0; i < 3; ++i) {
        vec3_project_points_onto_axis(local_tri.verts, 3, obb_axes[i], &min_0, &max_0);

        min_1 = -(
            obb->half_extents.x * kabs(vec3_dot(obb_axes[i], obb_axes[0])) +
            obb->half_extents.y * kabs(vec3_dot(obb_axes[i], obb_axes[1])) +
            obb->half_extents.z * kabs(vec3_dot(obb_axes[i], obb_axes[2])));

        // min_1 = -obb->half_extents.x * kabs(vec3_dot(obb_axes[i], vec3_right())) +
        //         -obb->half_extents.y * kabs(vec3_dot(obb_axes[i], vec3_up())) +
        //         -obb->half_extents.z * kabs(vec3_dot(obb_axes[i], vec3_backward()));
        max_1 = -min_1;
        if (max_0 < min_1 || max_1 < min_0) {
            return false; // separation found, no collision.
        }
    }

    // Test triangle's normal.
    vec3_project_points_onto_axis(local_tri.verts, 3, tri_normal, &min_0, &max_0);
    vec3_project_points_onto_axis(obb_axes, 3, tri_normal, &min_1, &max_1);
    if (max_0 < min_1 || max_1 < min_0) {
        return false; // separation found, no collision.
    }

    // Test cross products of OBB edges and triangle edges
    for (u8 i = 0; i < 3; ++i) {
        for (u8 j = 0; j < 3; ++j) {
            vec3 axis = vec3_cross(obb_axes[i], edge_0);
            if (vec3_length(axis) > 1e-6) { // avoid zero vectors
                vec3_normalize(&axis);
                vec3_project_points_onto_axis(local_tri.verts, 3, axis, &min_0, &max_0);
                vec3_project_points_onto_axis(obb_axes, 3, axis, &min_1, &max_1);
                if (max_0 < min_1 || max_1 < min_0) {
                    return false; // separation found, no collision.
                }
            }
        }
    }

    // No separating axis found, so there is a collision!
    *out_collision_normal = tri_normal;
    *out_penetration_depth = kabs(max_0 - min_1);

    return true;
} */

// NOTE: The first body should always be dynamic. The second can be either dynamic or static.
static b8 collide_bodies(kphysics_body* body_0, kphysics_body* body_1) {
    if (body_0->body_type != KPHYSICS_BODY_TYPE_DYNAMIC) {
        KERROR("%s - body_0 should always be dynamic.", __FUNCTION__);
        return false;
    }

    kphysics_collision_data collision = {0};

    switch (body_0->shape_type) {
    case KPHYSICS_SHAPE_TYPE_SPHERE:
        switch (body_1->shape_type) {
        case KPHYSICS_SHAPE_TYPE_SPHERE: {
            // sphere->sphere
            if (check_sphere_sphere_collision(body_0->position, body_0->radius, body_1->position, body_1->radius, &collision)) {

                // Resolve
                resolve_collision(body_0, body_1, &collision);
            }
        } break;
        case KPHYSICS_SHAPE_TYPE_RECTANGLE: {
            // sphere->rectangle
            oriented_bounding_box obb = {
                .rotation = body_1->rotation,
                .half_extents = body_1->half_extents,
                .center = body_1->position};

            if (check_obb_sphere_collision(&obb, body_0->position, body_0->radius, &collision)) {

                // Resolve
                resolve_collision(body_0, body_1, &collision);
            }
        } break;
        case KPHYSICS_SHAPE_TYPE_MESH: {
            // sphere->mesh

            // Number of collisions.
            u32 collision_count = 0;
            // Accumulate the collision normals.
            vec3 accumulated_collision_normal = vec3_zero();
            // Track the max penetration depth
            f32 max_pen_depth = 0;

            // TODO: This has to check all triangles. Perhaps a BVH would be of use here to optimize this...
            for (u32 i = 0; i < body_1->triangle_count; ++i) {
                if (check_sphere_triangle_collision(body_0->position, body_0->radius, &body_1->tris[i], &collision)) {

                    collision_count++;
                    accumulated_collision_normal = vec3_add(accumulated_collision_normal, collision.normal);
                    if (collision.depth > max_pen_depth) {
                        max_pen_depth = collision.depth;
                    }
                }
            }

            // If there were collisions, resolve
            if (collision_count) {
                // Use the normalized accumulated normal and the largest penetration depth.
                vec3_normalize(&accumulated_collision_normal);

                // Build a new collision struct
                kphysics_collision_data accumulated_collision_data = {
                    .depth = max_pen_depth,
                    .normal = accumulated_collision_normal};

                // Resolve
                resolve_collision(body_0, body_1, &accumulated_collision_data);
            }
        } break;
        }
        break;
    case KPHYSICS_SHAPE_TYPE_RECTANGLE:
        switch (body_1->shape_type) {
        case KPHYSICS_SHAPE_TYPE_SPHERE: {
            // rectangle->sphere
            oriented_bounding_box obb = {
                .rotation = body_0->rotation,
                .half_extents = body_0->half_extents,
                .center = body_0->position};

            if (check_obb_sphere_collision(&obb, body_1->position, body_1->radius, &collision)) {

                // Resolve
                resolve_collision(body_0, body_1, &collision);
            }
        } break;
        case KPHYSICS_SHAPE_TYPE_RECTANGLE: {
            // rectangle->rectangle

            oriented_bounding_box obb_0 = {
                .center = body_0->position,
                .rotation = body_0->rotation,
                .half_extents = body_0->half_extents};
            oriented_bounding_box obb_1 = {
                .center = body_1->position,
                .rotation = body_1->rotation,
                .half_extents = body_1->half_extents};

            if (check_obb_obb_collision(&obb_0, &obb_1, &collision)) {

                // Resolve
                resolve_collision(body_0, body_1, &collision);
            }

        } break;
        case KPHYSICS_SHAPE_TYPE_MESH: {
            // rectangle->mesh
            oriented_bounding_box obb = {
                .half_extents = body_1->half_extents,
                .rotation = body_1->rotation,
                .center = body_1->position};

            // TODO: This has to check all triangles. Perhaps a BVH would be of use here to optimize this...
            for (u32 i = 0; i < body_1->triangle_count; ++i) {
                if (check_obb_triangle_collision(&obb, &body_1->tris[i], &collision)) {

                    KTRACE("Rectangle collided with mesh!"); // nocheckin

                    // TODO: what if multiple triangles collide? Should an average be taken?
                    // Iterate them all?

                    // Resolve
                    resolve_collision(body_0, body_1, &collision);
                    break;
                }
            }
        } break;
        }
        break;
    case KPHYSICS_SHAPE_TYPE_MESH:
        KERROR("Dynamic mesh shapes are not supported");
        return false;
    }

    return true;
}

static void apply_gravity(kphysics_system_state* state, f64 fixed_update_time) {
    vec3 step_gravity = vec3_mul_scalar(state->active_world->gravity, fixed_update_time);

    u32 body_count = darray_length(state->active_world->bodies);
    for (u32 i = 0; i < body_count; ++i) {

        if (khandle_is_valid(state->active_world->bodies[i])) {
            kphysics_body* body_0 = kpool_get_by_index(&state->all_bodies, state->active_world->bodies[i].handle_index);
            if (body_0) {
                // Only want to affect dynamic bodies.
                if (body_0->body_type == KPHYSICS_BODY_TYPE_DYNAMIC) {

                    // Apply gravity.
                    body_0->velocity = vec3_add(body_0->velocity, step_gravity);

                    // HACK: terminal velocity
                    // TODO: define a ground plane.
                    if (body_0->velocity.y < step_gravity.y) {
                        body_0->velocity.y = step_gravity.y;
                    }

                    // TODO: Should this be applied here?
                    body_0->position = vec3_add(body_0->position, body_0->velocity);
                }
            }
        }
    }
}

static b8 physics_step(kphysics_system_state* state, f64 delta_time) {

    u32 body_count = darray_length(state->active_world->bodies);
    for (u32 i = 0; i < body_count; ++i) {

        if (khandle_is_valid(state->active_world->bodies[i])) {
            kphysics_body* body_0 = kpool_get_by_index(&state->all_bodies, state->active_world->bodies[i].handle_index);
            if (body_0) {
                // Only want to affect dynamic bodies.
                if (body_0->body_type == KPHYSICS_BODY_TYPE_DYNAMIC) {

                    // Check against all other bodies in the world.
                    // TODO: Need some sort of spatial partitioning here to speed this up.
                    for (u32 j = 0; j < body_count; ++j) {
                        if (khandle_is_valid(state->active_world->bodies[i])) {
                            kphysics_body* body_1 = kpool_get_by_index(&state->all_bodies, state->active_world->bodies[j].handle_index);
                            if (body_0 == body_1) {
                                // Skip self.
                                continue;
                            }
                            if (body_1) {
                                if (!collide_bodies(body_0, body_1)) {
                                    KERROR("Failed to handle body collision. See logs for details. Physics step failed.");
                                    return false;
                                }
                            }
                        }
                    }
                    // TODO: May want to apply some sort of "air friction" if no
                    // other collision happened.
                }
            }
        }
    }

    return true;
}

b8 kphysics_system_fixed_update(kphysics_system_state* state, f64 fixed_update_time) {
    if (!state && fixed_update_time <= 0) {
        return false;
    }

    if (state->active_world) {
        // Apply gravity first.
        apply_gravity(state, fixed_update_time);

        // Perform collision tests (stepped).
        f64 step_delta = fixed_update_time / state->config.steps_per_frame;
        for (u32 i = 0; i < state->config.steps_per_frame; ++i) {

            if (!physics_step(state, step_delta)) {
                KERROR("Failed to apply physics step. See logs for details.");
                return false;
            }
        }
    }

    return true;
}

b8 kphysics_world_create(struct kphysics_system_state* state, kname name, vec3 gravity, kphysics_world* out_world) {
    if (!out_world) {
        KERROR("kphysics_world_create requires a valid pointer to out_world");
        return false;
    }

    out_world->name = name;
    out_world->gravity = gravity;

    out_world->bodies = darray_create(khandle);

    return true;
}

void kphysics_world_destroy(struct kphysics_system_state* state, kphysics_world* world) {
    if (world) {
        if (world->bodies) {
            darray_destroy(world->bodies);
        }

        kzero_memory(world, sizeof(kphysics_world));
    }
}

b8 kphysics_set_world(kphysics_system_state* state, kphysics_world* world) {
    if (!state || !world) {
        KERROR("kphysics_set_world requires valid pointers to state and world, ya dingus!");
        return false;
    }

    state->active_world = world;

    return true;
}

b8 kphysics_world_add_body(struct kphysics_system_state* state, kphysics_world* world, khandle body) {
    if (!world || khandle_is_invalid(body)) {
        KERROR("kphysics_world_add_body requires a valid pointer to world and a valid handle to body!");
        return false;
    }

    if (!world->bodies) {
        return false;
    }

    u32 body_count = darray_length(world->bodies);
    for (u32 i = 0; i < body_count; ++i) {
        if (khandle_is_invalid(world->bodies[i])) {
            world->bodies[i] = body;
            return true;
        }
    }

    // No free space found, push a new entry.
    darray_push(world->bodies, body);
    return true;
}

b8 kphysics_world_remove_body(struct kphysics_system_state* state, kphysics_world* world, khandle body) {
    if (!world || khandle_is_invalid(body)) {
        KERROR("kphysics_world_remove_body requires a valid pointer to world and a valid handle to a body!");
        return false;
    }

    u32 body_count = darray_length(world->bodies);
    for (u32 i = 0; i < body_count; ++i) {
        if (world->bodies[i].handle_index == body.handle_index) {
            khandle_invalidate(&world->bodies[i]);
            return true;
        }
    }

    KWARN("kphysics_world_remove_body - body not found in world, nothing to be done.");
    return false;
}

b8 kphysics_body_create_sphere(struct kphysics_system_state* state, kname name, vec3 position, f32 radius, kphysics_body_type body_type, khandle* out_handle) {
    if (!state || !out_handle) {
        KERROR("%s - A pointer to handle out_body is required.", __FUNCTION__);
        return false;
    }

    kphysics_body* body = create_body(state, out_handle);

    body->name = name;
    body->body_type = body_type;
    body->shape_type = KPHYSICS_SHAPE_TYPE_SPHERE;
    body->position = position;
    body->radius = radius;

    return true;
}

b8 kphysics_body_create_rectangle(struct kphysics_system_state* state, kname name, vec3 position, vec3 half_extents, kphysics_body_type body_type, khandle* out_handle) {
    if (!state || !out_handle) {
        KERROR("%s - A pointer to out_body is required.", __FUNCTION__);
        return false;
    }

    kphysics_body* body = create_body(state, out_handle);

    body->name = name;
    body->body_type = body_type;
    body->shape_type = KPHYSICS_SHAPE_TYPE_RECTANGLE;
    body->position = position;
    body->half_extents = half_extents;

    return true;
}

b8 kphysics_body_create_mesh(struct kphysics_system_state* state, kname name, vec3 position, u32 triangle_count, triangle_3d* tris, kphysics_body_type body_type, khandle* out_handle) {
    if (!state || !out_handle) {
        KERROR("%s - A pointer to out_body is required.", __FUNCTION__);
        return false;
    }

    kphysics_body* body = create_body(state, out_handle);

    body->name = name;
    body->body_type = body_type;
    body->shape_type = KPHYSICS_SHAPE_TYPE_MESH;
    body->position = position;
    body->triangle_count = triangle_count;

    body->tris = KALLOC_TYPE_CARRAY(triangle_3d, triangle_count);
    KCOPY_TYPE_CARRAY(body->tris, tris, triangle_3d, triangle_count);

    return true;
}

void kphysics_body_destroy(struct kphysics_system_state* state, khandle* body) {
    if (!state || !body) {
        KWARN("%s - A pointer to body handle is required.", __FUNCTION__);
        return;
    }
}

b8 kphysics_body_position_set(struct kphysics_system_state* state, khandle body, vec3 position) {
    if (!state) {
        return false;
    }

    kphysics_body* b = get_body(state, body);
    if (!b) {
        return false;
    }

    b->position = position;

    return true;
}

b8 kphysics_body_rotation_set(struct kphysics_system_state* state, khandle body, quat rotation) {
    if (!state) {
        return false;
    }

    kphysics_body* b = get_body(state, body);
    if (!b) {
        return false;
    }

    b->rotation = rotation;

    return true;
}

b8 kphysics_body_rotate(struct kphysics_system_state* state, khandle body, quat rotation) {
    if (!state) {
        return false;
    }

    kphysics_body* b = get_body(state, body);
    if (!b) {
        return false;
    }

    b->rotation = quat_mul(b->rotation, rotation);

    return true;
}

b8 kphysics_body_apply_velocity(struct kphysics_system_state* state, khandle body, vec3 velocity) {
    if (!state) {
        return false;
    }

    kphysics_body* b = get_body(state, body);
    if (!b) {
        return false;
    }

    b->velocity = vec3_add(b->velocity, velocity);

    return true;
}

b8 kphysics_body_orientation_get(struct kphysics_system_state* state, khandle body, mat4* out_orientation) {
    if (!state) {
        return false;
    }

    kphysics_body* b = get_body(state, body);
    if (!b) {
        return false;
    }

    // FIXME: Store this in mat4 form instead and extract properties if needed.
    *out_orientation = mat4_from_translation_rotation_scale(b->position, b->rotation, vec3_one());

    return true;
}

b8 kphysics_body_velocity_get(struct kphysics_system_state* state, khandle body, vec3* out_velocity) {
    if (!state) {
        return false;
    }

    kphysics_body* b = get_body(state, body);
    if (!b) {
        return false;
    }

    *out_velocity = b->velocity;

    return true;
}

b8 kphysics_body_apply_impulse(struct kphysics_system_state* state, khandle body, vec3 point, vec3 force) {
    if (!state) {
        return false;
    }

    kphysics_body* b = get_body(state, body);
    if (!b) {
        return false;
    }

    // TODO: implement this.

    return true;
}

static kphysics_body* create_body(kphysics_system_state* state, khandle* out_handle) {
    if (!state || !out_handle) {
        return 0;
    }

    u32 handle_index;
    kphysics_body* new_body = kpool_allocate(&state->all_bodies, &handle_index);
    kzero_memory(new_body, sizeof(kphysics_body));
    // Always setup a default rotation.
    new_body->rotation = quat_identity();
    if (!new_body) {
        KERROR("Failed to allocate from body pool. Pool is full. Increase pool size.");
        return 0;
    }

    *out_handle = khandle_create(handle_index);
    new_body->uniqueid = out_handle->unique_id.uniqueid;

    return new_body;
}

static void destroy_body(kphysics_system_state* state, khandle* handle) {
    if (!state || !handle) {
        return;
    }

    if (khandle_is_valid(*handle)) {
        kphysics_body* body = kpool_get_by_index(&state->all_bodies, handle->handle_index);
        if (khandle_is_pristine(*handle, body->uniqueid)) {

            if (body->shape_type == KPHYSICS_SHAPE_TYPE_MESH) {
                if (body->triangle_count && body->tris) {
                    KFREE_TYPE_CARRAY(body->tris, triangle_3d, body->triangle_count);
                }
            }

            kzero_memory(body, sizeof(kphysics_body));
            body->uniqueid = INVALID_ID_U64;

            kpool_free_by_index(&state->all_bodies, handle->handle_index);

            khandle_invalidate(handle);
        }
    }
}

static kphysics_body* get_body(kphysics_system_state* state, khandle handle) {
    if (!state) {
        return 0;
    }

    if (khandle_is_valid(handle)) {
        kphysics_body* body = kpool_get_by_index(&state->all_bodies, handle.handle_index);
        if (khandle_is_pristine(handle, body->uniqueid)) {
            return body;
        }
    }

    return 0;
}
