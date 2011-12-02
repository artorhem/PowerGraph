/**  
 * Copyright (c) 2009 Carnegie Mellon University. 
 *     All rights reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing,
 *  software distributed under the License is distributed on an "AS
 *  IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 *  express or implied.  See the License for the specific language
 *  governing permissions and limitations under the License.
 *
 * For more about this software visit:
 *
 *      http://www.graphlab.ml.cmu.edu
 *
 */


/**
 * Functionality: The code solves the linear system Ax = b using
 * The Jacobi algorithm. (A is a square matrix). 
 * A assumed to be full column rank.  Algorithm is described
 * http://en.wikipedia.org/wiki/Jacobi_method
 * Written by Danny Bickson 
 */

#ifndef JACOBI_HPP
#define JACOBI_HPP

#include <cmath>
#include <cstdio>
#include <limits>
#include <iostream>
#include "graphlab.hpp"
#include "../shared/io.hpp"
#include "../shared/types.hpp"
using namespace graphlab;

#include <graphlab/macros_def.hpp>

bool debug = false;
int max_iter = 50;
ivec active_nodes_num;
ivec active_links_num;
int iiter = 0; //current iteration

enum kcore_output_fields{
  KCORE_INDEX = 1
};

struct vertex_data {
  bool active;
  int kcore, degree;

  vertex_data() : active(true), kcore(-1), degree(0)  {}

  void add_self_edge(double value) { }

  void set_val(double value, int field_type) { 
  }
  //only one output for jacobi - solution x
  double get_output(int field_type){ 
    if (field_type == KCORE_INDEX)
      return kcore;
    return -1;
  }
}; // end of vertex_data

struct edge_data {
  edge_data()  { }
  //compatible with parser which have edge value (we don't need it)
  edge_data(double val)  { }
};

typedef graphlab::graph<vertex_data, edge_data> graph_type;

void calc_initial_degree(graph_type * g, matrix_descriptor & desc){
  int active = 0;
  for (int i=0; i< desc.total(); i++){
     vertex_data & data = g->vertex_data(i);
     data.degree = g->out_edges(i).size() + g->in_edges(i).size();
     data.active = data.degree > 0;
     if (data.active)
       active++;
  }
  printf("Number of active nodes in round 0 is %d\n", active);
  printf("Number of active links in round 0 is %d\n", (int)g->num_edges());

  active_nodes_num[0] = active;
  active_links_num[0] = g->num_edges();
}


struct kcore_update :
  public graphlab::iupdate_functor<graph_type, kcore_update> {
  void operator()(icontext_type& context) {
  } 
};

class accumulator :
  public graphlab::iaccumulator<graph_type, kcore_update, accumulator> {
private:
  int num_active;
  int links;
public:
  accumulator() : num_active(0), links(0) { }

  void operator()(icontext_type& context) {
   

    vertex_data & vdata = context.vertex_data();
    if (!vdata.active)
      return;

    int cur_iter = iiter;
    int cur_links = 0;
    int increasing_links = 0;
    
    edge_list_type outedgeid = context.out_edges();
    edge_list_type inedgeid = context.in_edges();

    for(size_t i = 0; i < outedgeid.size(); i++) {
      const vertex_data & other = context.const_vertex_data(outedgeid[i].target());
        if (other.active){
	  cur_links++;
          increasing_links++;
        }
    }
    for (size_t i =0; i < inedgeid.size(); i++){
      const vertex_data & other = context.const_vertex_data(inedgeid[i].source());
        if (other.active)
          cur_links++;
    }
    if (cur_links <= cur_iter){
        vdata.active = false;
        vdata.kcore = cur_iter;
	//links -= (outedgeid.size() + inedgeid.size());
    }
    links += increasing_links;
    if (vdata.active)
      num_active++;
  };

  void operator+=(const accumulator& other) { 
    num_active += other.num_active;
    links += other.links;
  }

  void finalize(iglobal_context_type& context) {
   active_nodes_num[iiter] = num_active;
   if (num_active == 0)
	links = 0;
   printf("Number of active nodes in round %d is %di, links: %d\n", iiter, num_active, links);
   active_links_num[iiter] = links;

   if (num_active == 0){
     context.terminate(); 
     max_iter = iiter;
   }
 }
}; // end of  accumulator





int main(int argc,  char *argv[]) {
  
  global_logger().set_log_level(LOG_INFO);
  global_logger().set_log_to_console(true);

  graphlab::command_line_options clopts("GraphLab Linear Solver Library");

  std::string datafile;
  std::string format = "matrixmarket";
  int unittest = 0;
  int lineformat = MATRIX_MARKET_3;

  clopts.attach_option("data", &datafile, datafile,
                       "matrix A input file");
  clopts.add_positional("data");
  clopts.attach_option("format", &format, format, "matrix format");
  clopts.attach_option("lineformat", &lineformat, lineformat, "matrix line format");
  
  clopts.attach_option("debug", &debug, debug, "Display debug output.");
  clopts.attach_option("unittest", &unittest, unittest, 
		       "unit testing 0=None, 1=TBD");
  clopts.attach_option("max_iter", &max_iter, max_iter, "maximal number of cores");
  
  // Parse the command line arguments
  if(!clopts.parse(argc, argv)) {
    std::cout << "Invalid arguments!" << std::endl;
    return EXIT_FAILURE;
  }

  active_nodes_num = ivec(max_iter+1);
  active_links_num = ivec(max_iter+1);


  logstream(LOG_WARNING)
    << "Eigen detected. (This is actually good news!)" << std::endl;
  logstream(LOG_INFO) 
    << "GraphLab Linear solver library code by Danny Bickson, CMU" 
    << std::endl 
    << "Send comments and bug reports to danny.bickson@gmail.com" 
    << std::endl 
    << "Currently implemented algorithms are: Gaussian Belief Propagation, "
    << "Jacobi method, Conjugate Gradient" << std::endl;



  // Create a core
  graphlab::core<graph_type, kcore_update> core;
  core.set_options(clopts); // Set the engine options

  //unit testing
  if (unittest == 1){
     datafile = "kcores_unittest1";
  }

  std::cout << "Load graph" << std::endl;
  matrix_descriptor matrix_info;
  load_graph(datafile, format, matrix_info, core.graph(), lineformat);

  calc_initial_degree(&core.graph(), matrix_info);

  //std::cout << "Schedule all vertices" << std::endl;
  //core.schedule_all(kcore_update());
 
  accumulator acum;
  core.add_sync("sync", acum, 1000);
  core.add_global("NUM_ACTIVE", int(0));

  graphlab::timer mytimer; mytimer.start();

  for (iiter=1; iiter< max_iter+1; iiter++){
    logstream(LOG_INFO)<<mytimer.current_time() << ") Going to run k-cores iteration " << iiter << std::endl;
    while(true){
      int prev_nodes = active_nodes_num[iiter];
      core.sync_now("sync");
      int cur_nodes = active_nodes_num[iiter];
      if (prev_nodes == cur_nodes)
        break; 
    }
    if (active_nodes_num[iiter] == 0)
	break;
  }
 
  std::cout << "KCORES finished in " << mytimer.current_time() << std::endl;

  imat retmat = imat(max_iter+1, 4);
  memset((int*)data(retmat),0,sizeof(int)*retmat.size());

  std::cout<<active_nodes_num<<std::endl;
  std::cout<<active_links_num<<std::endl;

  for (int i=0; i <= max_iter; i++){
    set_val(retmat, i, 0, i);
    if (i >= 1){
      set_val(retmat, i, 1, active_nodes_num[i-1]-active_nodes_num[i]);
      set_val(retmat, i, 2, active_nodes_num[0]-active_nodes_num[i]);
      set_val(retmat, i, 3, core.graph().num_edges() - active_links_num[i]);
    }
  } 
  //write_output_matrix(datafile + ".kcores.out", format, retmat);
  std::cout<<retmat<<std::endl;


  vec ret = fill_output(&core.graph(), matrix_info, KCORE_INDEX);
  write_output_vector(datafile + "x.out", format, ret);


  if (unittest == 1){
    imat sol = init_imat("0 0 0 0; 1 1 1 1; 2 4 5 7; 3 4 9 13", 4, 4);
    assert(sumsum(sol - retmat) == 0);
  }

   return EXIT_SUCCESS;
}



#include <graphlab/macros_undef.hpp>
#endif