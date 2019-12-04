/*
 * SpatialJoin.cpp
 *
 *  Created on: Nov 11, 2019
 *      Author: teng
 */

#include <math.h>
#include <map>
#include <tuple>
#include "SpatialJoin.h"

using namespace std;

namespace hispeed{

typedef struct candidate_info_{
	HiMesh_Wrapper *mesh_wrapper;
	range distance;
	vector<std::tuple<Voxel*, Voxel *, range>> voxel_pairs;
}distance_candiate;

inline bool update_voxel_pair_list(vector<std::tuple<Voxel*, Voxel *, range>> &voxel_pairs, range &d){
	int voxel_pair_size = voxel_pairs.size();
	for(int j=0;j<voxel_pair_size;){
		range cur_d = get<2>(voxel_pairs[j]);
		if(d>cur_d){
			return false;
		}else if(d<cur_d){
			// evict this voxel pairs
			voxel_pairs.erase(voxel_pairs.begin()+j);
			voxel_pair_size--;
		}else{
			j++;
		}
	}
	return true;
}

inline bool update_candidate_list(vector<distance_candiate> &candidate_list, range &d){
	int list_size = candidate_list.size();
	for(int i=0;i<list_size;){
		if(d>candidate_list[i].distance){
			// should not keep since there is a closer one
			// in the candidate list
			return false;
		}else if(d<candidate_list[i].distance){
			// one candidate in the list cannot be the closest
			// remove it, together with the voxels, from the candidate list
			candidate_list[i].voxel_pairs.clear();
			candidate_list.erase(candidate_list.begin()+i);
			list_size--;
		}else{
			// go deep into checking the voxel pairs
			if(!update_voxel_pair_list(candidate_list[i].voxel_pairs, d)){
				return false;
			}
			// current candidate can be removed after comparing the voxel pairs
			// if all voxel pairs are farther compared to current one
			if(candidate_list[i].voxel_pairs.size()==0){
				candidate_list.erase(candidate_list.begin()+i);
				list_size--;
			}else{
				i++;
			}
		}//end if
	}//end for
	// the target one should be kept
	return true;
}

inline int get_pair_num(vector<std::pair<HiMesh_Wrapper *, vector<distance_candiate>>> &candidates){
	int pair_num = 0;
	for(std::pair<HiMesh_Wrapper *, vector<distance_candiate>> p:candidates){
		for(distance_candiate c:p.second){
			pair_num += c.voxel_pairs.size();
		}
	}
	return pair_num;
}

/*
 * the main function for getting the nearest neighbor
 *
 * */
void SpatialJoin::nearest_neighbor(bool with_gpu, int num_threads){
	if(num_threads==0){
		num_threads = hispeed::get_num_threads();
	}
	struct timeval start = get_cur_time();

	// filtering with MBBs to get the candidate list
	vector<std::pair<HiMesh_Wrapper *, vector<distance_candiate>>> candidates;
	for(int i=0;i<tile1->num_objects();i++){
		vector<distance_candiate> candidate_list;
		HiMesh_Wrapper *wrapper1 = tile1->get_mesh_wrapper(i);
		for(int j=0;j<tile2->num_objects();j++){
			// avoid self comparing
			if(tile1==tile2&&i==j){
				continue;
			}
			HiMesh_Wrapper *wrapper2 = tile2->get_mesh_wrapper(j);
			// we firstly use the distance between the mbbs
			// of those two objects as a filter to see if this one is
			// a suitable candidate, and then we further go
			// through the voxels in two objects to shrink
			// the candidate list in a fine grained
			range d = wrapper1->box.distance(wrapper2->box);
			if(update_candidate_list(candidate_list, d)){
				distance_candiate ci;
				ci.distance = d;
				ci.mesh_wrapper = wrapper2;
				for(Voxel *v1:wrapper1->voxels){
					for(Voxel *v2:wrapper2->voxels){
						range tmpd = v1->box.distance(v2->box);
						// no voxel pair in the lists is nearer
						if(update_voxel_pair_list(ci.voxel_pairs, tmpd) &&
						   update_candidate_list(candidate_list, tmpd)){
							ci.voxel_pairs.push_back(std::make_tuple(v1, v2, tmpd));
							ci.distance.update(tmpd);
						}
					}
				}
				// some voxel pairs need be further evaluated
				if(ci.voxel_pairs.size()>0){
					candidate_list.push_back(ci);
				}
			}
		}
		// save the candidate list
		candidates.push_back(std::pair<HiMesh_Wrapper *, vector<distance_candiate>>(wrapper1, candidate_list));
	}
	report_time("comparing mbbs", start);

	// now we start to get the distances with progressive level of details
	for(int lod=0;lod<=100;lod+=50){
		if(true){
			int candidate_list_size = candidates.size();
			for(int i=0;i<candidate_list_size;){
				// find the nearest, report and remove
				// this entry from the candidate list
				if(candidates[i].second.size()==1){
					for(distance_candiate info:candidates[i].second){
						info.voxel_pairs.clear();
					}
					candidates[i].second.clear();
					candidates.erase(candidates.begin()+i);
					candidate_list_size--;
				}else{
					i++;
				}
			}
			if(candidates.size()==0){
				return;
			}
		}

		int pair_num = get_pair_num(candidates);
		struct timeval iter_start = get_cur_time();
		printf("\n%ld polyhedron has %d candidates\n", candidates.size(), pair_num);
		// retrieve the necessary meshes
		map<Voxel *, std::pair<uint, uint>> voxel_map;
		uint segment_num = 0;
		for(std::pair<HiMesh_Wrapper *, vector<distance_candiate>> c:candidates){
			HiMesh_Wrapper *wrapper1 = c.first;
			for(distance_candiate info:c.second){
				HiMesh_Wrapper *wrapper2 = info.mesh_wrapper;
				for(std::tuple<Voxel *, Voxel *, range> vp:info.voxel_pairs){
					Voxel *v1 = get<0>(vp);
					Voxel *v2 = get<1>(vp);
					assert(v1&&v2);
					// not filled yet
					if(v1->data.find(lod)==v1->data.end()){
						tile1->get_mesh(wrapper1->id, lod);
						wrapper1->fill_voxels(lod, 0);
					}
					if(v2->data.find(lod)==v2->data.end()){
						tile2->get_mesh(wrapper2->id, lod);
						wrapper2->fill_voxels(lod, 0);
					}

					// update the voxel map
					for(int i=0;i<2;i++){
						Voxel *tv = i==0?v1:v2;
						if(voxel_map.find(tv)==voxel_map.end()){
							std::pair<uint, uint> p;
							p.first = segment_num;
							p.second = tv->size[lod];
							segment_num += tv->size[lod];
							voxel_map[tv] = p;
						}
					}
				}// end for voxel_pairs
			}// end for distance_candiate list
		}// end for candidates
		printf("decoded %ld voxels with %d segments for lod %d\n", voxel_map.size(), segment_num, lod);
		report_time("getting data for voxels", start);
		if(segment_num==0){
			cout<<"no segments is filled in this round"<<endl;
			voxel_map.clear();
			continue;
		}

		// now we allocate the space and store the data in a buffer
		float *data = new float[6*segment_num];
		for (map<Voxel *, std::pair<uint, uint>>::iterator it=voxel_map.begin();
				it!=voxel_map.end(); ++it){
			memcpy(data+it->second.first*6, it->first->data[lod], it->first->size[lod]*6*sizeof(float));
		}
		// organize the data for computing
		uint *offset_size = new uint[4*pair_num];
		float *distances = new float[pair_num];
		int index = 0;
		for(std::pair<HiMesh_Wrapper *, vector<distance_candiate>> c:candidates){
			for(distance_candiate info:c.second){
				for(std::tuple<Voxel *, Voxel*, range> vp:info.voxel_pairs){
					Voxel *v1 = get<0>(vp);
					Voxel *v2 = get<1>(vp);
					assert(v1!=v2);
					offset_size[4*index] = voxel_map[v1].first;
					offset_size[4*index+1] = voxel_map[v1].second;
					offset_size[4*index+2] = voxel_map[v2].first;
					offset_size[4*index+3] = voxel_map[v2].second;
					index++;
				}
			}
		}
		assert(index==pair_num);
		report_time("organizing data", start);
		if(with_gpu){
			hispeed::SegDist_batch_gpu(data, offset_size, distances, pair_num, segment_num);
			report_time("get distance with GPU", start);
		}else{
			hispeed::SegDist_batch(data, offset_size, distances, pair_num, num_threads);
			report_time("get distance with CPU", start);
		}

		// now update the distance range with the new distances
		index = 0;
		std::vector<range> tmp_distances;
		for(int i=0;i<candidates.size();i++){
			for(int j=0;j<candidates[i].second.size();j++){
				for(int t=0;t<candidates[i].second[j].voxel_pairs.size();t++){
					// update the distance
					if(get<0>(candidates[i].second[j].voxel_pairs[t])->size[lod]>0&&
					   get<1>(candidates[i].second[j].voxel_pairs[t])->size[lod]>0){
						range dist = get<2>(candidates[i].second[j].voxel_pairs[t]);
						if(lod==100){
							// now we have a precise distance
							dist.closest = distances[index];
							dist.farthest = distances[index];
						}else{
							dist.farthest = std::min(dist.farthest, distances[index]);
						}
						get<2>(candidates[i].second[j].voxel_pairs[t]) = dist;
						tmp_distances.push_back(dist);
					}
					index++;
				}
			}
			for(range r:tmp_distances){
				update_candidate_list(candidates[i].second, r);
			}
			tmp_distances.clear();
		}
		report_time("update distance range", start);

		// reset the voxels
		for(int i=0;i<candidates.size();i++){
			candidates[i].first->reset();
			for(int j=0;j<candidates[i].second.size();j++){
				candidates[i].second[j].mesh_wrapper->reset();
			}
		}

		delete data;
		delete offset_size;
		delete distances;
		voxel_map.clear();
		report_time("current iteration", iter_start, false);
		pair_num = get_pair_num(candidates);
		if(pair_num==0){
			break;
		}
	}
}

/*
 *
 * for doing intersecting
 *
 * */

typedef struct intersect_info_{
	HiMesh_Wrapper *mesh_wrapper;
	vector<std::tuple<Voxel*, Voxel *, bool>> voxel_pairs;
}intersect_candiate;

inline int get_pair_num(vector<std::pair<HiMesh_Wrapper *, vector<intersect_candiate>>> &candidates){
	int pair_num = 0;
	for(std::pair<HiMesh_Wrapper *, vector<intersect_candiate>> p:candidates){
		for(intersect_candiate c:p.second){
			pair_num += c.voxel_pairs.size();
		}
	}
	return pair_num;
}

/*
 * the main function for detect the intersection
 * relationship among polyhedra in the tile
 *
 * */
void SpatialJoin::intersect(bool with_gpu, int num_threads){
	if(num_threads==0){
		num_threads = hispeed::get_num_threads();
	}
	struct timeval start = get_cur_time();

	// filtering with MBBs to get the candidate list
	vector<std::pair<HiMesh_Wrapper *, vector<intersect_candiate>>> candidates;
	for(int i=0;i<tile1->num_objects();i++){
		vector<intersect_candiate> candidate_list;
		HiMesh_Wrapper *wrapper1 = tile1->get_mesh_wrapper(i);
		for(int j=0;j<tile2->num_objects();j++){
			// avoid self comparing
			if(tile1==tile2&&i==j){
				continue;
			}
			HiMesh_Wrapper *wrapper2 = tile2->get_mesh_wrapper(j);
			if(wrapper1->box.intersect(wrapper2->box)){
				intersect_candiate ci;
				ci.mesh_wrapper = wrapper2;
				for(Voxel *v1:wrapper1->voxels){
					for(Voxel *v2:wrapper2->voxels){
						if(v1->box.intersect(v2->box)){
							// a candidate not sure
							ci.voxel_pairs.push_back(std::make_tuple(v1, v2, false));
						}
					}
				}
				// some voxel pairs need be further evaluated
				if(ci.voxel_pairs.size()>0){
					candidate_list.push_back(ci);
				}
			}
		}
		// save the candidate list
		candidates.push_back(std::pair<HiMesh_Wrapper *, vector<intersect_candiate>>(wrapper1, candidate_list));
	}
	report_time("comparing mbbs", start);

	// now we start to ensure the intersection with progressive level of details
	for(int lod=0;lod<=100;lod+=50){
		int candidate_list_size = candidates.size();
		for(int i=0;i<candidate_list_size;){
			// at least one intersected object is found
			bool intersected = false;
			for(intersect_candiate info:candidates[i].second){
				for(std::tuple<Voxel *, Voxel*, bool> vp:info.voxel_pairs){
					// if any voxel pair is ensured to be intersected
					if(get<2>(vp)){
						intersected = true;
						break;
					}
				}
				if(intersected){
					break;
				}
			}
			if(intersected||candidates[i].second.size()==0){
				for(intersect_candiate info:candidates[i].second){
					info.voxel_pairs.clear();
				}
				candidates[i].second.clear();
				candidates.erase(candidates.begin()+i);
				candidate_list_size--;
			}else{
				i++;
			}
		}

		int pair_num = get_pair_num(candidates);
		if(pair_num==0){
			break;
		}
		struct timeval iter_start = get_cur_time();
		printf("\n%ld polyhedron has %d candidates\n", candidates.size(), pair_num);
		// retrieve the necessary meshes
		map<Voxel *, std::pair<uint, uint>> voxel_map;
		uint triangle_num = 0;
		for(std::pair<HiMesh_Wrapper *, vector<intersect_candiate>> c:candidates){
			HiMesh_Wrapper *wrapper1 = c.first;
			for(intersect_candiate info:c.second){
				HiMesh_Wrapper *wrapper2 = info.mesh_wrapper;
				for(std::tuple<Voxel *, Voxel *, bool> vp:info.voxel_pairs){
					Voxel *v1 = get<0>(vp);
					Voxel *v2 = get<1>(vp);
					// not filled yet
					if(v1->data.find(lod)==v1->data.end()){
						tile1->get_mesh(wrapper1->id, lod);
						wrapper1->fill_voxels(lod, 1);
					}
					if(v2->data.find(lod)==v2->data.end()){
						tile2->get_mesh(wrapper2->id, lod);
						wrapper2->fill_voxels(lod, 1);
					}

					// update the voxel map
					for(int i=0;i<2;i++){
						Voxel *tv = i==0?v1:v2;
						if(voxel_map.find(tv)==voxel_map.end()){
							std::pair<uint, uint> p;
							p.first = triangle_num;
							p.second = tv->size[lod];
							triangle_num += tv->size[lod];
							voxel_map[tv] = p;
						}
					}
				}// end for voxel_pairs
			}// end for distance_candiate list
		}// end for candidates
		printf("decoded %ld voxels with %d segments for lod %d\n", voxel_map.size(), triangle_num, lod);
		report_time("getting data for voxels", start);

		// now we allocate the space and store the data in a buffer
		float *data = new float[9*triangle_num];
		for (map<Voxel *, std::pair<uint, uint>>::iterator it=voxel_map.begin();
				it!=voxel_map.end(); ++it){
			if(it->first->size[lod]>0){
				memcpy(data+it->second.first*9, it->first->data[lod], it->first->size[lod]*9*sizeof(float));
			}
		}
		// organize the data for computing
		uint *offset_size = new uint[4*pair_num];
		bool *intersect_status = new bool[pair_num];
		int index = 0;
		for(std::pair<HiMesh_Wrapper *, vector<intersect_candiate>> c:candidates){
			for(intersect_candiate info:c.second){
				for(std::tuple<Voxel *, Voxel*, bool> vp:info.voxel_pairs){
					Voxel *v1 = get<0>(vp);
					Voxel *v2 = get<1>(vp);
					assert(v1!=v2);
					offset_size[4*index] = voxel_map[v1].first;
					offset_size[4*index+1] = voxel_map[v1].second;
					offset_size[4*index+2] = voxel_map[v2].first;
					offset_size[4*index+3] = voxel_map[v2].second;
					index++;
				}
			}
		}
		assert(index==pair_num);
		report_time("organizing data", start);

		hispeed::TriInt_batch(data, offset_size, intersect_status, pair_num, num_threads);
		report_time("get distance with CPU", start);

		// now update the intersection status
		index = 0;
		for(int i=0;i<candidates.size();i++){
			for(int j=0;j<candidates[i].second.size();j++){
				for(int t=0;t<candidates[i].second[j].voxel_pairs.size();t++){
					// update the status
					get<2>(candidates[i].second[j].voxel_pairs[t]) |= intersect_status[index];
					index++;
				}
			}
		}
		report_time("update intersection status", start);

		// reset the voxels
		for(int i=0;i<candidates.size();i++){
			candidates[i].first->reset();
			for(int j=0;j<candidates[i].second.size();j++){
				candidates[i].second[j].mesh_wrapper->reset();
			}
		}

		delete data;
		delete offset_size;
		delete intersect_status;
		voxel_map.clear();
		report_time("current iteration", iter_start, false);
	}
}


}


