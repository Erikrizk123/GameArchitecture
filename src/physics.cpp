#include <vector>
#include "mat4f.h"
#include "quatf.h"
#include "vec3f.h"
#include "transform.h"
#include "heap.h"

struct object_t {
	vec3f_t Velocity;
	vec3f_t Force;
	float Mass;

	transform_t* transform;
	box_collider_t* Collider;

};

struct collision_points_t {
	vec3f_t A; // Furthest point of A into B
	vec3f_t B; // Furthest point of B into A
	vec3f_t Normal; // B – A normalized
	float Depth;    // Length of B – A
	bool HasCollision;
};

struct collision_t {
	object_t* ObjA; // Collision object 
	object_t* ObjB; // Collision object
	collision_points_t Points; // Collision points
};

struct box_collider_t
{
	// Box location and size
	vec3f_t position;
	float width;
	float length;
	float height;


	collision_points_t check_collision(
		const transform_t* transform,
		const box_collider_t* box,
		const transform_t* boxTransform) const override
	{
		vec3f_t A = this->position + transform->WorldPosition();
		vec3f_t B = box->position + boxTransform->WorldPosition();

		float Aw = this->width * transform->WorldScale().major();
		float Bw = box->width * boxTransform->WorldScale().major();

		float Al = this->length * transform->WorldScale().major();
		float Bl = box->length * boxTransform->WorldScale().major();

		float Ah = this->height * transform->WorldScale().major();
		float Bh = box->height * boxTransform->WorldScale().major();

		vec3f_t AtoB = vec3f_sub(B, A);
		vec3f_t BtoA = vec3f_sub(A, B);

		if (vec3f_mag(AtoB) > Aw + Bw) {
			if (vec3f_mag(AtoB) > Al + Bl) {
				if (vec3f_mag(AtoB) > Ah + Bh) {
					collision_points_t c = NULL;
					c->A = vec3f_zero();
					c->B = vec3f_zero();
					c->Normal = vec3f_zero();
					c->Depth = 0;
					c->HasCollision = false;
					return c;
				}
			}
			
		}

		A += vec3f_norm(AtoB) * Ar;
		B += vec3f_norm(BtoA) * Br;

		AtoB = B - A;

		collision_points_t c = NULL;
		c->A = A;
		c->B = B;
		c->Normal = vec3f_norm(AtoB);
		c->Depth = 0;
		c->HasCollision = true;
		return c;

	}

};

class Physics {
private:
	std::vector<object_t*> physics_objects;
	vec3f_t gravity;

public:

	void add_object(object_t* obj)
	{
		physics_objects.push_back(obj);
	}

	void remove_object(object_t* obj)
	{
		int count = 0;
		for (object_t* a : physics_objects) {
			if (a == obj) {
				physics_objects.erase(physics_objects.begin() + count);
				break;
			}
			else {
				count++;
			}
		}
	}

	void update_physics(float dt)
	{

		ResolveCollisions(dt);		

		vec3f_t gravity = vec3f_zero();
		gravity  = -9.81f;

		for (object_t* obj : physics_objects) { 
			obj->Force += obj->Mass * gravity; // apply a force

			obj->Velocity += obj->Force / obj->Mass * dt;

			obj->Force = vec3f_zero(); // reset net force at the end
		}
	}

	void ResolveCollisions(float dt)
	{

		std::vector<collision_t> collisions;
		for (object_t* a : physics_objects) {
			for (object_t* b : physics_objects) {
				if (a == b) break;

				if (!a->Collider || !b->Collider)
				{
					continue;
				}

				collision_points_t points = a->Collider->check_collision(
					a->Transform,
					b->Collider,
					b->Transform);

				if (points.HasCollision) {
					collision_t collision = NULL;
					collision->ObjA = a;
					collision->ObjB = b;
					collision->points = points;
					collisions.emplace_back(collision);
				}
			}
		}

		// Solve collisions
	}
};