#include "nav.h"
#include <float.h>

inline double
cross_product(struct vector3* vt1, struct vector3* vt2) {
	return vt1->z * vt2->x - vt1->x * vt2->z;
}

inline void
cross_point(struct vector3* a, struct vector3* b, struct vector3* c, struct vector3* d, struct vector3* result) {
	result->x = ( ( b->x - a->x ) * ( c->x - d->x ) * ( c->z - a->z ) - c->x * ( b->x - a->x ) * ( c->z - d->z ) + a->x * ( b->z - a->z ) * ( c->x - d->x ) ) / ( ( b->z - a->z )*( c->x - d->x ) - ( b->x - a->x ) * ( c->z - d->z ) );
	result->z = ( ( b->z - a->z ) * ( c->z - d->z ) * ( c->x - a->x ) - c->z * ( b->z - a->z ) * ( c->x - d->x ) + a->z * ( b->x - a->x ) * ( c->z - d->z ) ) / ( ( b->x - a->x )*( c->z - d->z ) - ( b->z - a->z ) * ( c->x - d->x ) );
}

inline void
vector3_copy(struct vector3* dst, struct vector3* src) {
	dst->x = src->x;
	dst->y = src->y;
	dst->z = src->z;
}

inline void
vector3_sub(struct vector3* a, struct vector3* b, struct vector3* result) {
	result->x = a->x - b->x;
	result->y = a->y - b->y;
	result->z = a->z - b->z;
}

void
set_mask(struct nav_mesh_mask* ctx, int mask, int enable) {
	if ( mask >= ctx->size ) {
		ctx->size *= 2;
		ctx->mask = realloc(ctx->mask, sizeof(int)* ctx->size);
	}
	ctx->mask[mask] = enable;
}

inline double
dot2dot(struct vector3* a, struct vector3* b) {
	double dx = a->x - b->x;
	double dz = a->z - b->z;
	return sqrt(dx * dx + dz * dz);
}

static inline double
dot2line(struct vector3* pt, struct vector3* start, struct vector3* over) {
	double a, b, c, s;

	a = dot2dot(pt, over);
	if ( a <= 0.00001 ) {
		return 0.0f;
	}

	b = dot2dot(pt, start);
	if ( b <= 0.00001 ) {
		return 0.0f;
	}

	c = dot2dot(start, over);
	if ( c <= 0.00001 ) {
		return 0.0f;
	}

	if ( a * a >= b * b + c * c ) {
		return b;
	}

	if ( b * b >= a * a + c * c ) {
		return a;
	}

	s = ( a + b + c ) / 2;
	s = sqrt(s * ( s - a ) * ( s - b ) * ( s - c ));

	return  2 * s / c;
}

static inline double
dot2poly(struct nav_mesh_context* mesh_ctx, int poly_id, struct vector3* pt) {
	struct nav_node* nav_node = &mesh_ctx->node[poly_id];
	int i;
	double min = DBL_MAX;
	for ( i = 0; i < nav_node->size; i++ ) {
		struct vector3* vt1 = &mesh_ctx->vertices[nav_node->poly[i]];
		struct vector3* vt2 = &mesh_ctx->vertices[nav_node->poly[( i + 1 ) % nav_node->size]];
		double dist = dot2line(pt, vt1, vt2);
		if ( dist < min )
			min = dist;
	}
	return min;
}

bool
inside_poly(struct nav_mesh_context* mesh_ctx, int* poly, int size, struct vector3* vt3) {
	int sign = 0;
	int i;
	for ( i = 0; i < size; i++ ) {
		struct vector3* vt1 = &mesh_ctx->vertices[poly[i]];
		struct vector3* vt2 = &mesh_ctx->vertices[poly[( i + 1 ) % size]];

		struct vector3 vt21;
		vt21.x = vt2->x - vt1->x;
		vt21.y = 0;
		vt21.z = vt2->z - vt1->z;

		struct vector3 vt31;
		vt31.x = vt3->x - vt1->x;
		vt31.y = 0;
		vt31.z = vt3->z - vt1->z;

		double dot = cross_product(&vt21, &vt31);
		if ( dot == 0 )
			continue;

		if ( sign == 0 )
			sign = dot > 0 ? 1 : -1;
		else {
			if ( sign == 1 && dot < 0 )
				return false;
			else if ( sign == -1 && dot > 0 )
				return false;
		}
	}
	return true;
}

inline bool
inside_node(struct nav_mesh_context* mesh_ctx, int polyId, double x, double y, double z) {
	struct nav_node* nav_node = &mesh_ctx->node[polyId];
	struct vector3 vt;
	vt.x = x;
	vt.y = y;
	vt.z = z;
	return inside_poly(mesh_ctx, nav_node->poly, nav_node->size, &vt);
}

struct nav_node*
search_node(struct nav_mesh_context* ctx, double x, double y, double z) {
	if ( x < ctx->lt.x || x > ctx->br.x )
		return NULL;
	if ( z < ctx->lt.z || z > ctx->br.z )
		return NULL;

	if ( ctx->tile == NULL ) {
		int i;
		for ( i = 0; i < ctx->node_size; i++ ) {
			if ( inside_node(ctx, i, x, y, z) )
				return &ctx->node[i];
		}
		return NULL;
	}

	struct vector3 pt;
	pt.x = x;
	pt.z = z;

	int x_index = ( x - ctx->lt.x ) / ctx->tile_unit;
	int z_index = ( z - ctx->lt.z ) / ctx->tile_unit;
	int index = x_index + z_index * ctx->tile_width;
	struct nav_tile* tile = &ctx->tile[index];

	double min_dist = DBL_MAX;
	int close_poly = -1;

	int i;
	for ( i = 0; i < tile->offset; i++ ) {
		if ( inside_node(ctx, tile->node[i], x, y, z) ) {
			return &ctx->node[tile->node[i]];
		}
		else {
			double dist = dot2poly(ctx, tile->node[i], &pt);
			if ( min_dist > dist ) {
				min_dist = dist;
				close_poly = tile->node[i];
			}
		}
	}

	if ( close_poly != -1 ) {
		return &ctx->node[close_poly];
	}

	int center_node = 0;
	if ( around_movable(ctx, x, z, 1, &center_node, NULL, NULL) ) {
		return &ctx->node[center_node];
	}

	return NULL;
}

struct list*
get_link(struct nav_mesh_context* mesh_ctx, struct nav_node* node) {
	int i;
	for ( i = 0; i < node->size; i++ ) {
		int border_index = node->border[i];
		struct nav_border* border = get_border(mesh_ctx, border_index);

		int linked = -1;
		if ( border->node[0] == node->id )
			linked = border->node[1];
		else
			linked = border->node[0];

		if ( linked == -1 )
			continue;

		struct nav_node* tmp = get_node(mesh_ctx, linked);
		if ( tmp->list_head.pre || tmp->list_head.next )
			continue;

		if ( get_mask(mesh_ctx->mask_ctx, tmp->mask) ) {
			list_push(( &mesh_ctx->linked ), ( ( struct list_node* )tmp ));
			tmp->reserve = border->opposite;
			vector3_copy(&tmp->pos, &border->center);
		}
	}

	if ( list_empty(( &mesh_ctx->linked )) )
		return NULL;

	return &mesh_ctx->linked;
}

static inline double
G_COST(struct nav_node* from, struct nav_node* to) {
	double dx = from->pos.x - to->pos.x;
	double dy = 0;
	double dz = from->pos.z - to->pos.z;
	return sqrt(dx*dx + dy* dy + dz* dz) * GRATE;
}

static inline double
H_COST(struct nav_node* from, struct vector3* to) {
	double dx = from->center.x - to->x;
	double dy = 0;
	double dz = from->center.z - to->z;
	return sqrt(dx*dx + dy* dy + dz* dz) * HRATE;
}

bool
raycast(struct nav_mesh_context* ctx, struct vector3* pt0, struct vector3* pt1, struct vector3* result, search_dumper dumper, void* userdata) {
	struct nav_node* curr_node = search_node(ctx, pt0->x, pt0->y, pt0->z);

	int index = 0;
	struct vector3 vt10;
	vector3_sub(pt1, pt0, &vt10);

	while ( curr_node ) {
		if ( inside_node(ctx, curr_node->id, pt1->x, pt1->y, pt1->z) ) {
			vector3_copy(result, pt1);
			return true;
		}

		bool crossed = false;
		int i;
		for ( i = 0; i < curr_node->size; i++ ) {
			struct nav_border* border = get_border(ctx, curr_node->border[i]);

			struct vector3* pt3 = &ctx->vertices[border->a];
			struct vector3* pt4 = &ctx->vertices[border->b];

			struct vector3 vt30, vt40;
			vector3_sub(pt3, pt0, &vt30);
			vector3_sub(pt4, pt0, &vt40);

			double sign_a = cross_product(&vt30, &vt10);
			double sign_b = cross_product(&vt40, &vt10);

			if ( ( sign_a < 0 && sign_b > 0 ) || ( sign_a == 0 && sign_b > 0 ) || ( sign_a < 0 && sign_b == 0 ) ) {
				int next = -1;
				if ( border->node[0] != -1 ) {
					if ( border->node[0] == curr_node->id )
						next = border->node[1];
					else
						next = border->node[0];
				}
				else
					assert(border->node[1] == curr_node->id);

				if ( next == -1 ) {
					cross_point(pt3, pt4, pt1, pt0, result);
					result->x = pt0->x + ( result->x - pt0->x ) * 0.9;
					result->z = pt0->z + ( result->z - pt0->z ) * 0.9;
					return true;
				}
				else {
					struct nav_node* next_node = get_node(ctx, next);
					if ( get_mask(ctx->mask_ctx, next_node->mask) == 0 ) {
						cross_point(pt3, pt4, pt1, pt0, result);
						result->x = pt0->x + ( result->x - pt0->x ) * 0.9;
						result->z = pt0->z + ( result->z - pt0->z ) * 0.9;
						return true;
					}
					if ( dumper )
						dumper(userdata, next);

					crossed = true;
					curr_node = next_node;
					break;
				}
			}
		}

		if ( !crossed ) {
			assert(index == 0);
			pt0->x = curr_node->center.x;
			pt0->z = curr_node->center.z;
			vector3_sub(pt1, pt0, &vt10);
		}

		++index;
	}
	return false;
}


static inline void
clear_node(struct nav_node* n) {
	n->link_parent = NULL;
	n->link_border = -1;
	n->F = n->G = n->H = 0;
	n->elt.index = 0;
}

static inline void
heap_clear(struct element* elt) {
	struct nav_node *node = cast_node(elt);
	clear_node(node);
}

static inline void
reset(struct nav_mesh_context* ctx) {
	struct nav_node * node = NULL;
	while ( ( node = ( struct nav_node* )list_pop(&ctx->closelist) ) ) {
		clear_node(node);
	}
	minheap_clear(( ctx )->openlist, heap_clear);
}

struct nav_node*
next_border(struct nav_mesh_context* ctx, struct nav_node* node, struct vector3* wp, int *link_border) {
	struct vector3 vt0, vt1;
	*link_border = node->link_border;
	while ( *link_border != -1 ) {
		struct nav_border* border = get_border(ctx, *link_border);
		vector3_sub(&ctx->vertices[border->a], wp, &vt0);
		vector3_sub(&ctx->vertices[border->b], wp, &vt1);
		if ( ( vt0.x == 0 && vt0.z == 0 ) || ( vt1.x == 0 && vt1.z == 0 ) ) {
			node = node->link_parent;
			*link_border = node->link_border;
		}
		else
			break;
	}
	if ( *link_border != -1 )
		return node;

	return NULL;
}

static inline void
path_init(struct nav_mesh_context* mesh_ctx) {
	mesh_ctx->result.offset = 0;
}

void
path_add(struct nav_mesh_context* mesh_ctx, struct vector3* wp) {
	if ( mesh_ctx->result.offset >= mesh_ctx->result.size ) {
		mesh_ctx->result.size *= 2;
		mesh_ctx->result.wp = ( struct vector3* )realloc(mesh_ctx->result.wp, sizeof( struct vector3 )*mesh_ctx->result.size);
	}

	mesh_ctx->result.wp[mesh_ctx->result.offset].x = wp->x;
	mesh_ctx->result.wp[mesh_ctx->result.offset].z = wp->z;
	mesh_ctx->result.offset++;
}

void
make_waypoint(struct nav_mesh_context* mesh_ctx, struct vector3* pt0, struct vector3* pt1, struct nav_node * node) {
	path_add(mesh_ctx, pt1);

	struct vector3* pt_wp = pt1;

	int link_border = node->link_border;

	struct nav_border* border = get_border(mesh_ctx, link_border);

	struct vector3 pt_left, pt_right;
	vector3_copy(&pt_left, &mesh_ctx->vertices[border->a]);
	vector3_copy(&pt_right, &mesh_ctx->vertices[border->b]);

	struct vector3 vt_left, vt_right;
	vector3_sub(&pt_left, pt_wp, &vt_left);
	vector3_sub(&pt_right, pt_wp, &vt_right);

	struct nav_node* left_node = node->link_parent;
	struct nav_node* right_node = node->link_parent;

	struct nav_node* tmp = node->link_parent;
	while ( tmp )
	{
		int link_border = tmp->link_border;
		if ( link_border == -1 )
		{
			struct vector3 tmp_target;
			tmp_target.x = pt0->x - pt_wp->x;
			tmp_target.z = pt0->z - pt_wp->z;

			double forward_a = cross_product(&vt_left, &tmp_target);
			double forward_b = cross_product(&vt_right, &tmp_target);

			if ( forward_a < 0 && forward_b > 0 )
			{
				path_add(mesh_ctx, pt0);
				break;
			}
			else
			{
				if ( forward_a > 0 && forward_b > 0 )
				{
					pt_wp->x = pt_left.x;
					pt_wp->z = pt_left.z;

					path_add(mesh_ctx, pt_wp);

					left_node = next_border(mesh_ctx, left_node, pt_wp, &link_border);
					if ( left_node == NULL )
					{
						path_add(mesh_ctx, pt0);
						break;
					}

					border = get_border(mesh_ctx, link_border);
					pt_left.x = mesh_ctx->vertices[border->a].x;
					pt_left.z = mesh_ctx->vertices[border->a].z;

					pt_right.x = mesh_ctx->vertices[border->b].x;
					pt_right.z = mesh_ctx->vertices[border->b].z;

					vt_left.x = pt_left.x - pt_wp->x;
					vt_left.z = pt_left.z - pt_wp->z;

					vt_right.x = pt_right.x - pt_wp->x;
					vt_right.z = pt_right.z - pt_wp->z;

					tmp = left_node->link_parent;
					left_node = tmp;
					right_node = tmp;
					continue;
				}
				else if ( forward_a < 0 && forward_b < 0 )
				{
					pt_wp->x = pt_right.x;
					pt_wp->z = pt_right.z;

					path_add(mesh_ctx, pt_wp);

					right_node = next_border(mesh_ctx, right_node, pt_wp, &link_border);
					if ( right_node == NULL )
					{
						path_add(mesh_ctx, pt0);
						break;
					}

					border = get_border(mesh_ctx, link_border);
					pt_left.x = mesh_ctx->vertices[border->a].x;
					pt_left.z = mesh_ctx->vertices[border->a].z;

					pt_right.x = mesh_ctx->vertices[border->b].x;
					pt_right.z = mesh_ctx->vertices[border->b].z;

					vt_left.x = pt_left.x - pt_wp->x;
					vt_left.z = pt_left.z - pt_wp->z;

					vt_right.x = pt_right.x - pt_wp->x;
					vt_right.z = pt_right.z - pt_wp->z;

					tmp = right_node->link_parent;
					left_node = tmp;
					right_node = tmp;
					continue;
				}
				break;
			}

		}

		border = get_border(mesh_ctx, link_border);

		struct vector3 tmp_pt_left, tmp_pt_right;
		vector3_copy(&tmp_pt_left, &mesh_ctx->vertices[border->a]);
		vector3_copy(&tmp_pt_right, &mesh_ctx->vertices[border->b]);

		struct vector3 tmp_vt_left, tmp_vt_right;
		vector3_sub(&tmp_pt_left, pt_wp, &tmp_vt_left);
		vector3_sub(&tmp_pt_right, pt_wp, &tmp_vt_right);

		double forward_left_a = cross_product(&vt_left, &tmp_vt_left);
		double forward_left_b = cross_product(&vt_right, &tmp_vt_left);
		double forward_right_a = cross_product(&vt_left, &tmp_vt_right);
		double forward_right_b = cross_product(&vt_right, &tmp_vt_right);

		if ( forward_left_a < 0 && forward_left_b > 0 )
		{
			left_node = tmp->link_parent;
			vector3_copy(&pt_left, &tmp_pt_left);
			vector3_sub(&pt_left, pt_wp, &vt_left);
		}

		if ( forward_right_a < 0 && forward_right_b > 0 )
		{
			right_node = tmp->link_parent;
			vector3_copy(&pt_right, &tmp_pt_right);
			vector3_sub(&pt_right, pt_wp, &vt_right);
		}

		if ( forward_left_a > 0 && forward_left_b > 0 && forward_right_a > 0 && forward_right_b > 0 )
		{
			vector3_copy(pt_wp, &pt_left);

			left_node = next_border(mesh_ctx, left_node, pt_wp, &link_border);
			if ( left_node == NULL )
			{
				path_add(mesh_ctx, pt0);
				break;
			}

			border = get_border(mesh_ctx, link_border);
			vector3_copy(&pt_left, &mesh_ctx->vertices[border->a]);
			vector3_copy(&pt_right, &mesh_ctx->vertices[border->b]);

			vector3_sub(&mesh_ctx->vertices[border->a], pt_wp, &vt_left);
			vector3_sub(&mesh_ctx->vertices[border->b], pt_wp, &vt_right);

			path_add(mesh_ctx, pt_wp);

			tmp = left_node->link_parent;
			left_node = tmp;
			right_node = tmp;

			continue;
		}

		if ( forward_left_a < 0 && forward_left_b < 0 && forward_right_a < 0 && forward_right_b < 0 )
		{
			vector3_copy(pt_wp, &pt_right);

			right_node = next_border(mesh_ctx, right_node, pt_wp, &link_border);
			if ( right_node == NULL )
			{
				path_add(mesh_ctx, pt0);
				break;
			}

			border = get_border(mesh_ctx, link_border);
			vector3_copy(&pt_left, &mesh_ctx->vertices[border->a]);
			vector3_copy(&pt_right, &mesh_ctx->vertices[border->b]);

			vector3_sub(&mesh_ctx->vertices[border->a], pt_wp, &vt_left);
			vector3_sub(&mesh_ctx->vertices[border->b], pt_wp, &vt_right);

			path_add(mesh_ctx, pt_wp);

			tmp = right_node->link_parent;
			left_node = tmp;
			right_node = tmp;
			continue;
		}

		tmp = tmp->link_parent;
	}
}

struct nav_path*
astar_find(struct nav_mesh_context* mesh_ctx, struct vector3* pt_start, struct vector3* pt_over, search_dumper dumper, void* userdata) {
	path_init(mesh_ctx);

	struct nav_node* node_start = search_node(mesh_ctx, pt_start->x, pt_start->y, pt_start->z);
	struct nav_node* node_over = search_node(mesh_ctx, pt_over->x, pt_over->y, pt_over->z);

	if ( !node_start || !node_over )
		return NULL;

	if ( node_start == node_over ) {
		path_add(mesh_ctx, pt_over);
		path_add(mesh_ctx, pt_start);
		return &mesh_ctx->result;
	}

	vector3_copy(&node_start->pos, pt_start);

	minheap_push(mesh_ctx->openlist, &node_start->elt);

	struct nav_node* node_current = NULL;
	for ( ;; ) {
		struct element* elt = minheap_pop(mesh_ctx->openlist);
		if ( !elt ) {
			reset(mesh_ctx);
			return NULL;
		}
		node_current = cast_node(elt);

		if ( node_current == node_over ) {
			make_waypoint(mesh_ctx, pt_start, pt_over, node_current);
			reset(mesh_ctx);
			clear_node(node_current);
			return &mesh_ctx->result;
		}

		list_push(&mesh_ctx->closelist, ( struct list_node* )node_current);

		struct list* linked = get_link(mesh_ctx, node_current);
		if ( linked ) {
			struct nav_node* linked_node;
			while ( ( linked_node = ( struct nav_node* )list_pop(linked) ) ) {
				if ( linked_node->elt.index ) {
					double nG = node_current->G + G_COST(node_current, linked_node);
					if ( nG < linked_node->G ) {
						linked_node->G = nG;
						linked_node->F = linked_node->G + linked_node->H;
						linked_node->link_parent = node_current;
						linked_node->link_border = linked_node->reserve;
						minheap_change(mesh_ctx->openlist, &linked_node->elt);
					}
				}
				else {
					linked_node->G = node_current->G + G_COST(node_current, linked_node);
					linked_node->H = H_COST(linked_node, pt_over);
					linked_node->F = linked_node->G + linked_node->H;
					linked_node->link_parent = node_current;
					linked_node->link_border = linked_node->reserve;
					minheap_push(mesh_ctx->openlist, &linked_node->elt);
					if ( dumper != NULL )
						dumper(userdata, linked_node->id);
				}
			}
		}
	}
}

struct vector3*
around_movable(struct nav_mesh_context* ctx, double x, double z, int range, int* center_node, search_dumper dumper, void* userdata) {
	if ( ctx->tile == NULL )
		return NULL;

	struct vector3 pt;
	pt.x = x;
	pt.z = z;
	struct vector3* result = NULL;
	int result_node = -1;
	double min_distance = DBL_MAX;

	int x_index = ( x - ctx->lt.x ) / ctx->tile_unit;
	int z_index = ( z - ctx->lt.z ) / ctx->tile_unit;

	int r;
	for ( r = 1; r <= range; ++r ) {
		int x_min = x_index - r;
		int x_max = x_index + r;
		int z_min = z_index - r;
		int z_max = z_index + r;

		int x, z;

		int z_range[2] = { z_min, z_max };

		int j;
		for ( j = 0; j < 2; j++ ) {
			z = z_range[j];

			if ( z < 0 || z >= ctx->tile_heigh )
				continue;

			for ( x = x_min; x <= x_max; x++ ) {

				if ( x < 0 || x >= ctx->tile_width )
					continue;

				int index = x + z * ctx->tile_width;
				struct nav_tile* tile = &ctx->tile[index];
				if ( dumper )
					dumper(userdata, index);

				if ( tile->center_node != -1 ) {
					double distance = dot2dot(&pt, &tile->center);
					if ( distance < min_distance ) {
						result = &tile->center;
						result_node = tile->center_node;
						min_distance = distance;
					}
				}
			}
		}

		int x_range[2] = { x_min, x_max };

		for ( j = 0; j < 2; j++ ) {
			x = x_range[j];
			if ( x < 0 || x >= ctx->tile_width )
				continue;

			for ( z = z_min; z < z_max; z++ ) {
				if ( z < 0 || z >= ctx->tile_heigh )
					continue;

				int index = x + z * ctx->tile_width;
				struct nav_tile* tile = &ctx->tile[index];
				if ( dumper )
					dumper(userdata, index);
				if ( tile->center_node != -1 ) {
					double distance = dot2dot(&pt, &tile->center);
					if ( distance < min_distance ) {
						result = &tile->center;
						result_node = tile->center_node;
						min_distance = distance;
					}
				}

			}
		}
	}

	if ( center_node ) {
		*center_node = result_node;
	}
	return result;
}

bool
point_movable(struct nav_mesh_context* ctx, double x, double z) {
	if ( x < ctx->lt.x || x > ctx->br.x )
		return NULL;
	if ( z < ctx->lt.z || z > ctx->br.z )
		return NULL;

	struct nav_node* node = NULL;

	if ( ctx->tile == NULL ) {
		int i;
		for ( i = 0; i < ctx->node_size; i++ ) {
			if ( inside_node(ctx, i, x, 0, z) ) {
				node = &ctx->node[i];
				break;
			}
		}
	}
	else {
		int x_index = ( x - ctx->lt.x ) / ctx->tile_unit;
		int z_index = ( z - ctx->lt.z ) / ctx->tile_unit;
		int index = x_index + z_index * ctx->tile_width;

		struct nav_tile* tile = &ctx->tile[index];

		int i;
		for ( i = 0; i < tile->offset; i++ ) {
			if ( inside_node(ctx, tile->node[i], x, 0, z) ) {
				node = &ctx->node[tile->node[i]];
				break;
			}
		}
	}

	if ( node ) {
		if ( get_mask(ctx->mask_ctx, node->mask) == 1 ) {
			return true;
		}
	}
	return false;
}

bool
point_height(struct nav_mesh_context* ctx, double x, double z, double* height) {
	struct nav_node* node = search_node(ctx,x, 0, z);
	if ( !node ) {
		return false;
	}

	struct vector3 start;
	start.x = node->center.x;
	start.z = node->center.z;

	struct vector3 over;
	over.x = x;
	over.z = z;

	struct vector3 vt10;
	vector3_sub(&over, &start, &vt10);

	struct vector3 result;
	struct nav_border* cross_border = NULL;

	int i;
	for ( i = 0; i < node->size; i++ ) {
		struct nav_border* border = get_border(ctx, node->border[i]);

		struct vector3* pt3 = &ctx->vertices[border->a];
		struct vector3* pt4 = &ctx->vertices[border->b];

		struct vector3 vt30, vt40;
		vector3_sub(pt3, &start, &vt30);
		vector3_sub(pt4, &start, &vt40);

		double sign_a = cross_product(&vt30, &vt10);
		double sign_b = cross_product(&vt40, &vt10);

		if ( ( sign_a < 0 && sign_b > 0 ) || ( sign_a == 0 && sign_b > 0 ) || ( sign_a < 0 && sign_b == 0 ) ) {
			cross_point(pt3, pt4, &over, &start, &result);
			cross_border = border;
			break;
		}
	}

	if ( !cross_border ) {
		return false;
	}

	struct vector3* pt_border0 = &ctx->vertices[cross_border->a];
	struct vector3* pt_border1 = &ctx->vertices[cross_border->b];

	double dt_border = dot2dot(pt_border0, pt_border1);
	double dt_cross = dot2dot(pt_border0, &result);

	double border_height;
	if (dt_border < 0.0001) {
		border_height = pt_border1->y;
	}
	else {
		border_height  = pt_border0->y + ( pt_border1->y - pt_border0->y ) * ( dt_cross / dt_border );
	}
	
	double center_height = node->center.y;

	double dt_center = dot2dot(&node->center, &result);
	double dt_point = dot2dot(&over, &result);

	double pt_height;
	if (dt_center < 0.0001) {
		pt_height = node->center.y;
	}
	else {
		pt_height = border_height + ( center_height - border_height ) * ( dt_point / dt_center );
	}

	*height = pt_height;

	return true;
}