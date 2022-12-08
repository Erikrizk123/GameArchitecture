#pragma once

#include "mat4f.h"
#include "quatf.h"
#include "vec3f.h"
#include "transform.h"
#include "heap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct object_t object_t;
typedef struct collision_points_t collisionPoints_t;
typedef struct box_collider_t box_collider_t;
typedef struct collision_t collision_t;



void add_object(object_t* obj);
void remove_object(object_t* obj);
void update_physics(float dt);



#ifdef __cplusplus
}
#endif