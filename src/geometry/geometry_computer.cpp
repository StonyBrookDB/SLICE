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
bool geometry_computer::request_cpu(){
	if(cpu_busy==false){
		pthread_mutex_lock(&cpu_lock);
		if(cpu_busy==false){
			cpu_busy = true;
			return true;
		}else{
			pthread_mutex_unlock(&cpu_lock);
		}
	}
	return false;
}
void geometry_computer::release_cpu(){
	assert(cpu_busy);
	cpu_busy = false;
	pthread_mutex_unlock(&cpu_lock);
}
gpu_info *geometry_computer::request_gpu(int min_size, bool force){
	do{
		for(gpu_info *info:gpus){
			if(!info->busy&&info->mem_size>min_size+1){
				pthread_mutex_lock(&info->lock);
				if(!info->busy){
					info->busy = true;
					return info;
				}
				pthread_mutex_unlock(&info->lock);
			}
		}
	}while(force);
	return NULL;
}
void geometry_computer::release_gpu(gpu_info *info){
	assert(info->busy);
	pthread_mutex_unlock(&info->lock);
	info->busy = false;
}

geometry_computer::~geometry_computer(){
	for(gpu_info *info:gpus){
		clean_gpu(info);
		delete info;
	}
}

bool geometry_computer::init_gpus(){
	gpus = get_gpus();
	for(gpu_info *info:gpus){
		init_gpu(info);
	}
	return true;
}

void geometry_computer::get_distance_cpu(geometry_param &cc){
	while(!request_cpu()){
		usleep(10);
	}
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
		pthread_create(&threads[i], NULL, SegDist_unit, (void *)&params[i]);
	}
	log("%d threads started to get distance", max_thread_num);
	for(int i = 0; i < max_thread_num; i++){
		void *status;
		pthread_join(threads[i], &status);
	}
	release_cpu();
}

void geometry_computer::get_distance_gpu(geometry_param &cc){
	gpu_info *gpu = request_gpu(cc.data_size*6*sizeof(float)/1024/1024, true);
	assert(gpu);
	log("GPU %d started to get distance", gpu->device_id);
	hispeed::SegDist_batch_gpu(gpu, cc.data, cc.offset_size, cc.distances, cc.pair_num, cc.data_size);
	release_gpu(gpu);
}

void geometry_computer::get_distance(geometry_param &cc){
	if(gpus.size()>0){
		get_distance_gpu(cc);
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
	while(!request_cpu()){
		usleep(10);;
	}

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
	log("%d threads started", max_thread_num);
	for(int i = 0; i < max_thread_num; i++){
		void *status;
		pthread_join(threads[i], &status);
	}
	release_cpu();
}

}

