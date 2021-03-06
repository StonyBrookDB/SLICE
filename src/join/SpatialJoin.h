/*
 * SpatialJoin.h
 *
 *  Created on: Nov 11, 2019
 *      Author: teng
 */

#ifndef SPATIALJOIN_H_
#define SPATIALJOIN_H_

#include "../storage/tile.h"
#include "../geometry/geometry.h"
#include <queue>

using namespace std;

namespace hispeed{

class voxel_pair{
public:
	Voxel *v1;
	Voxel *v2;
	range dist;
	bool intersect = false;
	voxel_pair(Voxel *v1, Voxel *v2, range dist){
		this->v1 = v1;
		this->v2 = v2;
		this->dist = dist;
	};
	voxel_pair(Voxel *v1, Voxel *v2){
		this->v1 = v1;
		this->v2 = v2;
	}
};

typedef struct candidate_info_{
	HiMesh_Wrapper *mesh_wrapper;
	range distance;
	vector<voxel_pair> voxel_pairs;
}candidate_info;

typedef std::pair<HiMesh_Wrapper *, vector<candidate_info>> candidate_entry;

// type of the workers, GPU or CPU
// each worker took a batch of jobs (X*Y) from the job queue
// and conduct the join, the result is then stored to
// the target result addresses
enum Worker_Type{
	WT_GPU,
	WT_CPU
};

enum Join_Type{
	JT_intersect,
	JT_distance,
	JT_nearest
};

// size of the buffer is 1GB
const static long VOXEL_BUFFER_SIZE = 1<<30;

/* todo: need be updated
 * all computations will be aligned into computation units
 * of N*N. For instance, after checking the index, the
 * nearest neighbor of object a is b or c is not decided.
 * We further decode the polyhedron a, b and c if they
 * are not decoded yet. The edges and surfaces are
 * decoded and cached. Then the computation across those
 * segments and triangles are organized as many N*N
 * computing units as possible. Notice that padding may be needed to
 * align the computing units. then space in buffer is claimed
 * to store the data of those computing units. the true computation
 * is done by the GPU or CPU, and results will be copied into the result_addr
 * Corresponding to the buffer space claimed.
*/

class SpatialJoin{

	geometry_computer *computer = NULL;
	int base_lod = 0;
	int lod_gap = 50;
	int top_lod = 100;
	vector<int> lods;
	double global_total_time = 0;
	double global_index_time = 0;
	double global_decode_time = 0;
	double global_packing_time = 0;
	double global_computation_time = 0;
	double global_updatelist_time = 0;
	pthread_mutex_t g_lock;

public:
	void set_lods(vector<int> &ls){
		sort(ls.begin(), ls.end());
		for(int l:ls){
			assert(l>=100&&l>=0);
			lods.push_back(l);
		}
	}
	void set_base_lod(int v){
		assert(v>=0&&v<=100);
		base_lod = v;
	}
	void set_top_lod(int v){
		assert(v>=0&&v<=100);
		top_lod = v;
	}
	void set_lod_gap(int v){
		assert(v>0&&v<=100);
		lod_gap = v;
	}
	SpatialJoin(geometry_computer *c){
		assert(c);
		pthread_mutex_init(&g_lock, NULL);
		computer = c;
	}
	~SpatialJoin(){
	}
	void report_time(double t);

	/*
	 *
	 * the main entry function to conduct next round of computation
	 * each object in tile1 need to compare with all objects in tile2.
	 * to avoid unnecessary computation, we need to build index on tile2.
	 * The unit for building the index for tile2 is the ABB for all or part
	 * of the surface (mostly triangle) of a polyhedron.
	 *
	 * */
	vector<candidate_entry> mbb_distance(Tile *tile1, Tile *tile2);
	void nearest_neighbor(Tile *tile1, Tile *tile2);
	void nearest_neighbor_aabb(Tile *tile1, Tile *tile2);

	vector<candidate_entry> mbb_intersect(Tile *tile1, Tile *tile2);
	void intersect(Tile *tile1, Tile *tile2);

	void nearest_neighbor_batch(vector<pair<Tile *, Tile *>> &tile_pairs, int num_threads, bool ispeed);
	void intersect_batch(vector<pair<Tile *, Tile *>> &tile_pairs, int num_threads);

	/*
	 *
	 * go check the index
	 *
	 * */
	void check_index();

	// register job to gpu
	// worker can register work to gpu
	// if the GPU is idle and queue is not full
	// otherwise do it locally with CPU
	float *register_computation(char *data, int num_cu);

	/*
	 * do the geometry computation in a batch with GPU
	 *
	 * */
	void compute_gpu();

	/*
	 * do the geometry computation in a batch with CPU
	 *
	 * */
	void compute_cpu();


};

}


#endif /* SPATIALJOIN_H_ */
