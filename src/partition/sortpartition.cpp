/*
 * sortpartition.cpp
 *
 *  Created on: Nov 19, 2019
 *      Author: teng
 *
 *  partition the space with sorted points
 *
 */

#include "partition.h"


namespace hispeed{

void SPNode::genTiles(vector<aab> &tiles){
	if(children.size()==0){
		tiles.push_back(box);
	}else{
		for(SPNode *c:children){
			c->genTiles(tiles);
		}
	}
}

bool compareAAB_x(aab *a1, aab *a2){
	return a1->min[0]<a2->min[0];
}
bool compareAAB_y(aab *a1, aab *a2){
	return a1->min[1]<a2->min[1];
}
bool compareAAB_z(aab *a1, aab *a2){
	return a1->min[2]<a2->min[2];
}

SPNode *build_sort_partition(std::vector<aab*> &mbbs, int num_tiles){
	// create the root node
	SPNode *root = new SPNode();

	// get the number of cuts in each dimension
	long part_x = std::max((int)std::pow(num_tiles, 1.0/3),1);
	long part_y = part_x;
	long part_z = std::max((int)((num_tiles/part_x)/part_y),1);
	fprintf(stderr,"partitioning space into %ld * %ld * %ld = %ld tiles\n",
			part_x, part_y, part_z, part_x*part_y*part_z);

	// sort the mbbs in X dimension
	std::sort(mbbs.begin(), mbbs.end(), compareAAB_x);
	long length_r = mbbs.size(); // length of the root nodes
	long step_x = (length_r+part_x-1)/part_x;
	for(long x=0;x<length_r;x+=step_x){
		long length_x = std::min(length_r, x+step_x) - x;
		SPNode *cur_x = new SPNode();
		cur_x->parent = root;
		root->children.push_back(cur_x);

		// process the slide cut in X dimension
		std::vector<aab *> tmp_x;
		for(int t=x;t<x+length_x;t++){
			tmp_x.push_back(mbbs[t]);
		}
		std::sort(tmp_x.begin(), tmp_x.end(), compareAAB_y);
		long step_y = (length_x+part_y-1)/part_y;
		for(long y=0;y<length_x;y+=step_y){
			long length_y = std::min(length_x, y+step_y) - y;
			SPNode *cur_y = new SPNode();
			cur_y->parent = cur_x;
			cur_x->children.push_back(cur_y);

			// process the slice cut in y dimension
			std::vector<aab *> tmp_y;
			for(int t=y;t<y+length_y;t++){
				tmp_y.push_back(tmp_x[t]);
			}
			std::sort(tmp_y.begin(), tmp_y.end(), compareAAB_z);
			long step_z = (length_y+part_z-1)/part_z;
			for(long z=0;z<length_y;z+=step_z){
				long length_z = std::min(length_y, z+step_z) - z;
				// generate the leaf node, update its MBB
				// with the objects assigned to it
				SPNode *cur_z = new SPNode();
				cur_z->parent = cur_y;
				cur_y->children.push_back(cur_z);
				for(int t=z;t<z+length_z;t++){
					cur_z->box.update(*tmp_y[t]);
				}
				cur_y->box.update(cur_z->box);
			}
			cur_x->box.update(cur_y->box);
			tmp_y.clear();
		}
		root->box.update(cur_x->box);
		tmp_x.clear();
	}
	return root;
}

}
