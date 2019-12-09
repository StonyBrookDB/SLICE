/*
 * geometry_computer.cpp
 *
 *  Created on: Dec 9, 2019
 *      Author: teng
 */

#include "./geometry.h"
#include "./mygpu.h"


namespace hispeed{

void *SegDist_unit(void *params_void){
	geometry_param *param = (geometry_param *)params_void;
	for(int i=0;i<param->pair_num;i++){
		param->distances[i] = SegDist_single(param->data+param->offset_size[4*i]*6,
									    param->data+param->offset_size[4*i+2]*6,
									    param->offset_size[4*i+1],
									    param->offset_size[4*i+3]);
	}
	return NULL;
}
void geometry_computer::get_distance_cpu(geometry_param &cc){
	request_cpu();
	pthread_t threads[max_thread_num];
	geometry_param params[max_thread_num];
	int each_thread = cc.pair_num/max_thread_num;
	for(int i=0;i<max_thread_num;i++){
		int start = each_thread*i;
		if(start>=cc.pair_num){
			break;
		}
		params[i] = cc;
		params[i].pair_num = min(each_thread, (int)cc.pair_num-start);
		params[i].offset_size = cc.offset_size+start*4;
		params[i].data = cc.data;
		params[i].id = i+1;
		params[i].distances = cc.distances+start;
		int rc = pthread_create(&threads[i], NULL, SegDist_unit, (void *)&params[i]);
		if (rc) {
			cout << "Error:unable to create thread," << rc << endl;
			exit(-1);
		}
	}
	cerr<<max_thread_num<<" threads started"<<endl;
	for(int i = 0; i < max_thread_num; i++){
		void *status;
		pthread_join(threads[i], &status);
	}
	release_cpu();
}

void geometry_computer::get_distance_gpu(geometry_param &cc){
	gpu_info *gpu = request_gpu();
	if(gpu){
		hispeed::SegDist_batch_gpu(gpu, cc.data, cc.offset_size, cc.distances, cc.pair_num, cc.data_size);
		release_gpu(gpu);
	}else{
		cerr<<"all gpus are busy"<<endl;
	}
}

// choose the computing resource by system
void geometry_computer::get_distance(geometry_param &cc){
	// GPU has a higher priority
	gpu_info *gpu = request_gpu();
	if(gpu){
		hispeed::SegDist_batch_gpu(gpu, cc.data, cc.offset_size, cc.distances, cc.pair_num, cc.data_size);
		release_gpu(gpu);
	}else{
		get_distance_cpu(cc);
	}
}

void *TriInt_unit(void *params_void){
	geometry_param *param = (geometry_param *)params_void;
	for(int i=0;i<param->pair_num;i++){
		param->intersect[i] = TriInt_single(param->data+param->offset_size[4*i]*9,
									    param->data+param->offset_size[4*i+2]*9,
									    param->offset_size[4*i+1],
									    param->offset_size[4*i+3]);
	}
	return NULL;
}
void geometry_computer::get_intersect(geometry_param &cc){
	request_cpu();

	// compute the minimum distance of segment pairs with multiple threads
	pthread_t threads[max_thread_num];
	geometry_param params[max_thread_num];
	int each_thread = cc.pair_num/max_thread_num;
	for(int i=0;i<max_thread_num;i++){
		int start = each_thread*i;
		if(start>=cc.pair_num){
			break;
		}
		params[i].pair_num = min(each_thread, (int)cc.pair_num-start);
		params[i].offset_size = cc.offset_size+start*4;
		params[i].data = cc.data;
		params[i].id = i+1;
		params[i].intersect = cc.intersect+start;
		pthread_create(&threads[i], NULL, TriInt_unit, (void *)&params[i]);
	}
	cerr<<max_thread_num<<" threads started"<<endl;
	for(int i = 0; i < max_thread_num; i++){
		void *status;
		pthread_join(threads[i], &status);
	}
	release_cpu();
}



}
