/* Copyright 2014 Stanford University
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <cstdio>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <time.h>

#include "circuit.h"
#include "circuit_mapper.h"
#include "../legion_runtime/legion.h"

extern "C" void panic(const char*, ...);
extern "C" unsigned long long nk_alloc_page_region(unsigned);

using namespace LegionRuntime::HighLevel;
using namespace LegionRuntime::Accessor;

extern unsigned long calc_new_current_start;
extern unsigned long calc_new_current_end;


namespace TaskHelper {
    void * numa_current_task[8] = {0,0,0,0,0,0,0,0};
};


extern "C" int printk(const char * fmt, ...);

union test_dbl { double dval; unsigned long long uval; };

LegionRuntime::Logger::Category log_circuit("circuit");

// Utility functions (forward declarations)
void parse_input_args(char **argv, int argc, int &num_loops, int &num_pieces,
                      int &nodes_per_piece, int &wires_per_piece,
                      int &pct_wire_in_piece, int &random_seed,
		      int &steps, int &sync, bool &perform_checks);

Partitions load_circuit(Circuit &ckt, std::vector<CircuitPiece> &pieces, Context ctx,
                        HighLevelRuntime *runtime, int num_pieces, int nodes_per_piece,
                        int wires_per_piece, int pct_wire_in_piece, int random_seed,
			int steps);

void allocate_node_fields(Context ctx, HighLevelRuntime *runtime, FieldSpace node_space);
void allocate_wire_fields(Context ctx, HighLevelRuntime *runtime, FieldSpace wire_space);
void allocate_locator_fields(Context ctx, HighLevelRuntime *runtime, FieldSpace locator_space);

void top_level_task(const Task *task,
                    const std::vector<PhysicalRegion> &regions,
                    Context ctx, HighLevelRuntime *runtime)
{
  //int num_loops = 2;
  int num_loops = 16;
  //int num_pieces = 4;
  int num_pieces = 32;
  //int nodes_per_piece = 2;
  int nodes_per_piece = 16;
  //int wires_per_piece = 4;
  int wires_per_piece = 32;
  int pct_wire_in_piece = 95;
  int random_seed = 12345;
  //int steps = STEPS;
  int steps = 100000;
  int sync = 0;
  bool perform_checks = false;
  {
    const InputArgs &command_args = HighLevelRuntime::get_input_args();
    char **argv = command_args.argv;
    int argc = command_args.argc;

    parse_input_args(argv, argc, num_loops, num_pieces, nodes_per_piece, 
		     wires_per_piece, pct_wire_in_piece, random_seed,
		     steps, sync, perform_checks);

    log_circuit(LEVEL_PRINT,"circuit settings: loops=%d pieces=%d nodes/piece=%d "
                            "wires/piece=%d pct_in_piece=%d seed=%d",
       num_loops, num_pieces, nodes_per_piece, wires_per_piece,
       pct_wire_in_piece, random_seed);
  }

  Circuit circuit;
  {
    int num_circuit_nodes = num_pieces * nodes_per_piece;
    int num_circuit_wires = num_pieces * wires_per_piece;
    // Make index spaces
    IndexSpace node_index_space = runtime->create_index_space(ctx,num_circuit_nodes);
    IndexSpace wire_index_space = runtime->create_index_space(ctx,num_circuit_wires);
    // Make field spaces
    FieldSpace node_field_space = runtime->create_field_space(ctx);
    FieldSpace wire_field_space = runtime->create_field_space(ctx);
    FieldSpace locator_field_space = runtime->create_field_space(ctx);
    // Allocate fields
    allocate_node_fields(ctx, runtime, node_field_space);
    allocate_wire_fields(ctx, runtime, wire_field_space);
    allocate_locator_fields(ctx, runtime, locator_field_space);
    // Make logical regions
    circuit.all_nodes = runtime->create_logical_region(ctx,node_index_space,node_field_space);
    circuit.all_wires = runtime->create_logical_region(ctx,wire_index_space,wire_field_space);
    circuit.node_locator = runtime->create_logical_region(ctx,node_index_space,locator_field_space);
  }

  // Load the circuit
  std::vector<CircuitPiece> pieces(num_pieces);
  Partitions parts = load_circuit(circuit, pieces, ctx, runtime, num_pieces, nodes_per_piece,
                                  wires_per_piece, pct_wire_in_piece, random_seed, steps);

  // Arguments for each point
  ArgumentMap local_args;
  for (int idx = 0; idx < num_pieces; idx++)
  {
    DomainPoint point = DomainPoint::from_point<1>(Point<1>(idx));
    local_args.set_point(point, TaskArgument(&(pieces[idx]),sizeof(CircuitPiece)));
  }

  // Make the launchers
  Rect<1> launch_rect(Point<1>(0), Point<1>(num_pieces-1)); 
  Domain launch_domain = Domain::from_rect<1>(launch_rect);
  CalcNewCurrentsTask cnc_launcher(parts.pvt_wires, parts.pvt_nodes, parts.shr_nodes, parts.ghost_nodes,
                                   circuit.all_wires, circuit.all_nodes, launch_domain, local_args);

  DistributeChargeTask dsc_launcher(parts.pvt_wires, parts.pvt_nodes, parts.shr_nodes, parts.ghost_nodes,
                                    circuit.all_wires, circuit.all_nodes, launch_domain, local_args);

  UpdateVoltagesTask upv_launcher(parts.pvt_nodes, parts.shr_nodes, parts.node_locations,
                                 circuit.all_nodes, circuit.node_locator, launch_domain, local_args);

  printf("Starting main simulation loop\n");
  struct timespec ts_start, ts_end;
  clock_gettime(CLOCK_MONOTONIC, &ts_start);
  // Run the main loop
  bool simulation_success = true;
  for (int i = 0; i < num_loops; i++)
  {
    TaskHelper::dispatch_task<CalcNewCurrentsTask>(cnc_launcher, ctx, runtime, 
                                                   perform_checks, simulation_success);
    TaskHelper::dispatch_task<DistributeChargeTask>(dsc_launcher, ctx, runtime, 
                                                    perform_checks, simulation_success);
    TaskHelper::dispatch_task<UpdateVoltagesTask>(upv_launcher, ctx, runtime, 
                                                  perform_checks, simulation_success,
                                                  ((i+1)==num_loops));
  }
  clock_gettime(CLOCK_MONOTONIC, &ts_end);
  if (simulation_success)
    printf("SUCCESS!\n");
  else
    printf("FAILURE!\n");
  {
    double sim_time = ((1.0 * (ts_end.tv_sec - ts_start.tv_sec)) +
                       (1e-9 * (ts_end.tv_nsec - ts_start.tv_nsec)));
    union test_dbl t;
    t.dval = sim_time;
    //printf("ELAPSED TIME = %7.3f s\n", sim_time);
    printf("ELAPSED TIME = 0x%016llx\n", t.uval);

    // Compute the floating point operations per second
    long num_circuit_nodes = num_pieces * nodes_per_piece;
    long num_circuit_wires = num_pieces * wires_per_piece;
    // calculate currents
    long operations = num_circuit_wires * (WIRE_SEGMENTS*6 + (WIRE_SEGMENTS-1)*4) * steps;
    // distribute charge
    operations += (num_circuit_wires * 4);
    // update voltages
    operations += (num_circuit_nodes * 4);
    // multiply by the number of loops
    operations *= num_loops;

    // Compute the number of gflops
    double gflops = (1e-9*operations)/sim_time;
    t.dval = gflops;
    //printf("GFLOPS = %7.3f GFLOPS\n", gflops);
    printf("GFLOPS = 0x%016llx GFLOPS\n", t.uval);
  }
  log_circuit(LEVEL_PRINT,"simulation complete - destroying regions");

  // Now we can destroy all the things that we made
  {
    runtime->destroy_logical_region(ctx,circuit.all_nodes);
    runtime->destroy_logical_region(ctx,circuit.all_wires);
    runtime->destroy_logical_region(ctx,circuit.node_locator);
    runtime->destroy_field_space(ctx,circuit.all_nodes.get_field_space());
    runtime->destroy_field_space(ctx,circuit.all_wires.get_field_space());
    runtime->destroy_field_space(ctx,circuit.node_locator.get_field_space());
    runtime->destroy_index_space(ctx,circuit.all_nodes.get_index_space());
    runtime->destroy_index_space(ctx,circuit.all_wires.get_index_space());
  }
}

static void update_mappers(Machine *machine, HighLevelRuntime *rt,
                           const std::set<Processor> &local_procs)
{
  for (std::set<Processor>::const_iterator it = local_procs.begin();
        it != local_procs.end(); it++)
  {
    rt->replace_default_mapper(new CircuitMapper(machine, rt, *it), *it);
  }
}

int go_circuit(int argc, char **argv);
int go_circuit(int argc, char **argv) 
{
  HighLevelRuntime::set_top_level_task_id(TOP_LEVEL_TASK_ID);
  HighLevelRuntime::register_legion_task<top_level_task>(TOP_LEVEL_TASK_ID,
      Processor::LOC_PROC, true/*single*/, false/*index*/);
  // If we're running on the shared low-level then only register cpu tasks
#ifdef SHARED_LOWLEVEL
  TaskHelper::register_cpu_variants<CalcNewCurrentsTask>();
  TaskHelper::register_cpu_variants<DistributeChargeTask>();
  TaskHelper::register_cpu_variants<UpdateVoltagesTask>();
#else
  TaskHelper::register_hybrid_variants<CalcNewCurrentsTask>();
  TaskHelper::register_hybrid_variants<DistributeChargeTask>();
  TaskHelper::register_hybrid_variants<UpdateVoltagesTask>();
#endif
  CheckTask::register_task();
  HighLevelRuntime::register_reduction_op<AccumulateCharge>(REDUCE_ID);
  HighLevelRuntime::set_registration_callback(update_mappers);

#if 0
  /* I will now pull some devious NUMA hackery out of my... */
  //size_t size = (&calc_new_current_end - &calc_new_current_start);
  for (int i = 0; i < 8; i++) {
      void * funcptr = (void*)nk_alloc_page_region(i);
      if (!funcptr) {
          panic("Legion couldn't allocate NUMA page\n");
      }
      printk("copying CalcNewCurrents to addr %p (size=%u)\n", funcptr);
      TaskHelper::numa_current_task[i] = funcptr;
      memcpy(funcptr, (void*)CalcNewCurrentsTask::cpu_base_impl, 0x1000);
  }
#endif

  return HighLevelRuntime::start(argc, argv);
}

void parse_input_args(char **argv, int argc, int &num_loops, int &num_pieces,
                      int &nodes_per_piece, int &wires_per_piece,
                      int &pct_wire_in_piece, int &random_seed,
		      int &steps, int &sync, bool &perform_checks)
{
  for (int i = 1; i < argc; i++) 
  {
    if (!strcmp(argv[i], "-l")) 
    {
      num_loops = atoi(argv[++i]);
      continue;
    }

    if (!strcmp(argv[i], "-i")) 
    {
      steps = atoi(argv[++i]);
      continue;
    }

    if(!strcmp(argv[i], "-p")) 
    {
      num_pieces = atoi(argv[++i]);
      continue;
    }

    if(!strcmp(argv[i], "-npp")) 
    {
      nodes_per_piece = atoi(argv[++i]);
      continue;
    }

    if(!strcmp(argv[i], "-wpp")) 
    {
      wires_per_piece = atoi(argv[++i]);
      continue;
    }

    if(!strcmp(argv[i], "-pct")) 
    {
      pct_wire_in_piece = atoi(argv[++i]);
      continue;
    }

    if(!strcmp(argv[i], "-s")) 
    {
      random_seed = atoi(argv[++i]);
      continue;
    }

    if(!strcmp(argv[i], "-sync")) 
    {
      sync = atoi(argv[++i]);
      continue;
    }

    if(!strcmp(argv[i], "-checks"))
    {
      perform_checks = true;
      continue;
    }
  }
}

void allocate_node_fields(Context ctx, HighLevelRuntime *runtime, FieldSpace node_space)
{
  FieldAllocator allocator = runtime->create_field_allocator(ctx, node_space);
  allocator.allocate_field(sizeof(float), FID_NODE_CAP);
  allocator.allocate_field(sizeof(float), FID_LEAKAGE);
  allocator.allocate_field(sizeof(float), FID_CHARGE);
  allocator.allocate_field(sizeof(float), FID_NODE_VOLTAGE);
}

void allocate_wire_fields(Context ctx, HighLevelRuntime *runtime, FieldSpace wire_space)
{
  FieldAllocator allocator = runtime->create_field_allocator(ctx, wire_space);
  allocator.allocate_field(sizeof(ptr_t), FID_IN_PTR);
  allocator.allocate_field(sizeof(ptr_t), FID_OUT_PTR);
  allocator.allocate_field(sizeof(PointerLocation), FID_IN_LOC);
  allocator.allocate_field(sizeof(PointerLocation), FID_OUT_LOC);
  allocator.allocate_field(sizeof(float), FID_INDUCTANCE);
  allocator.allocate_field(sizeof(float), FID_RESISTANCE);
  allocator.allocate_field(sizeof(float), FID_WIRE_CAP);
  for (int i = 0; i < WIRE_SEGMENTS; i++)
    allocator.allocate_field(sizeof(float), FID_CURRENT+i);
  for (int i = 0; i < (WIRE_SEGMENTS-1); i++)
    allocator.allocate_field(sizeof(float), FID_WIRE_VOLTAGE+i);
}

void allocate_locator_fields(Context ctx, HighLevelRuntime *runtime, FieldSpace locator_space)
{
  FieldAllocator allocator = runtime->create_field_allocator(ctx, locator_space);
  allocator.allocate_field(sizeof(float), FID_LOCATOR);
}

PointerLocation find_location(ptr_t ptr, const std::set<ptr_t> &private_nodes,
                              const std::set<ptr_t> &shared_nodes, const std::set<ptr_t> &ghost_nodes)
{
  if (private_nodes.find(ptr) != private_nodes.end())
  {
    return PRIVATE_PTR;
  }
  else if (shared_nodes.find(ptr) != shared_nodes.end())
  {
    return SHARED_PTR;
  }
  else if (ghost_nodes.find(ptr) != ghost_nodes.end())
  {
    return GHOST_PTR;
  }
  // Should never make it here, if we do something bad happened
  assert(false);
  return PRIVATE_PTR;
}

template<typename T>
static T random_element(const std::set<T> &set)
{
  int index = int(drand48() * set.size());
  typename std::set<T>::const_iterator it = set.begin();
  while (index-- > 0) it++;
  return *it;
}

template<typename T>
static T random_element(const std::vector<T> &vec)
{
  int index = int(drand48() * vec.size());
  return vec[index];
}

Partitions load_circuit(Circuit &ckt, std::vector<CircuitPiece> &pieces, Context ctx,
                        HighLevelRuntime *runtime, int num_pieces, int nodes_per_piece,
                        int wires_per_piece, int pct_wire_in_piece, int random_seed,
			int steps)
{
  log_circuit(LEVEL_PRINT,"Initializing circuit simulation...");
  // inline map physical instances for the nodes and wire regions
  RegionRequirement wires_req(ckt.all_wires, READ_WRITE, EXCLUSIVE, ckt.all_wires);
  wires_req.add_field(FID_IN_PTR);
  wires_req.add_field(FID_OUT_PTR);
  wires_req.add_field(FID_IN_LOC);
  wires_req.add_field(FID_OUT_LOC);
  wires_req.add_field(FID_INDUCTANCE);
  wires_req.add_field(FID_RESISTANCE);
  wires_req.add_field(FID_WIRE_CAP);
  for (int i = 0; i < WIRE_SEGMENTS; i++)
    wires_req.add_field(FID_CURRENT+i);
  for (int i = 0; i < (WIRE_SEGMENTS-1); i++)
    wires_req.add_field(FID_WIRE_VOLTAGE+i);
  RegionRequirement nodes_req(ckt.all_nodes, READ_WRITE, EXCLUSIVE, ckt.all_nodes);
  nodes_req.add_field(FID_NODE_CAP);
  nodes_req.add_field(FID_LEAKAGE);
  nodes_req.add_field(FID_CHARGE);
  nodes_req.add_field(FID_NODE_VOLTAGE);
  RegionRequirement locator_req(ckt.node_locator, READ_WRITE, EXCLUSIVE, ckt.node_locator);
  locator_req.add_field(FID_LOCATOR);
  PhysicalRegion wires = runtime->map_region(ctx, wires_req);
  PhysicalRegion nodes = runtime->map_region(ctx, nodes_req);
  PhysicalRegion locator = runtime->map_region(ctx, locator_req);

  Coloring wire_owner_map;
  Coloring private_node_map;
  Coloring shared_node_map;
  Coloring ghost_node_map;
  Coloring locator_node_map;

  Coloring privacy_map;

  // keep a O(1) indexable list of nodes in each piece for connecting wires
  std::vector<std::vector<ptr_t> > piece_node_ptrs(num_pieces);
  std::vector<int> piece_shared_nodes(num_pieces, 0);

  srand48(random_seed);

  nodes.wait_until_valid();
  RegionAccessor<AccessorType::Generic, float> fa_node_cap = 
    nodes.get_field_accessor(FID_NODE_CAP).typeify<float>();
  RegionAccessor<AccessorType::Generic, float> fa_node_leakage = 
    nodes.get_field_accessor(FID_LEAKAGE).typeify<float>();
  RegionAccessor<AccessorType::Generic, float> fa_node_charge = 
    nodes.get_field_accessor(FID_CHARGE).typeify<float>();
  RegionAccessor<AccessorType::Generic, float> fa_node_voltage = 
    nodes.get_field_accessor(FID_NODE_VOLTAGE).typeify<float>();
  locator.wait_until_valid();
  RegionAccessor<AccessorType::Generic, PointerLocation> locator_acc = 
    locator.get_field_accessor(FID_LOCATOR).typeify<PointerLocation>();
  ptr_t *first_nodes = new ptr_t[num_pieces];
  {
    IndexAllocator node_allocator = runtime->create_index_allocator(ctx, ckt.all_nodes.get_index_space());
    node_allocator.alloc(num_pieces * nodes_per_piece);
  }
  {
    IndexIterator itr(ckt.all_nodes.get_index_space());
    for (int n = 0; n < num_pieces; n++)
    {
      for (int i = 0; i < nodes_per_piece; i++)
      {
        assert(itr.has_next());
        ptr_t node_ptr = itr.next();
        if (i == 0)
          first_nodes[n] = node_ptr;
        float capacitance = drand48() + 1.f;
        fa_node_cap.write(node_ptr, capacitance);
        float leakage = 0.1f * drand48();
        fa_node_leakage.write(node_ptr, leakage);
        fa_node_charge.write(node_ptr, 0.f);
        float init_voltage = 2*drand48() - 1.f;
        fa_node_voltage.write(node_ptr, init_voltage);
        // Just put everything in everyones private map at the moment       
        // We'll pull pointers out of here later as nodes get tied to 
        // wires that are non-local
        private_node_map[n].points.insert(node_ptr);
        privacy_map[0].points.insert(node_ptr);
        locator_node_map[n].points.insert(node_ptr);
	piece_node_ptrs[n].push_back(node_ptr);
      }
    }
  }

  wires.wait_until_valid();
  RegionAccessor<AccessorType::Generic, float> fa_wire_currents[WIRE_SEGMENTS];
  for (int i = 0; i < WIRE_SEGMENTS; i++)
    fa_wire_currents[i] = wires.get_field_accessor(FID_CURRENT+i).typeify<float>();
  RegionAccessor<AccessorType::Generic, float> fa_wire_voltages[WIRE_SEGMENTS-1];
  for (int i = 0; i < (WIRE_SEGMENTS-1); i++)
    fa_wire_voltages[i] = wires.get_field_accessor(FID_WIRE_VOLTAGE+i).typeify<float>();
  RegionAccessor<AccessorType::Generic, ptr_t> fa_wire_in_ptr = 
    wires.get_field_accessor(FID_IN_PTR).typeify<ptr_t>();
  RegionAccessor<AccessorType::Generic, ptr_t> fa_wire_out_ptr = 
    wires.get_field_accessor(FID_OUT_PTR).typeify<ptr_t>();
  RegionAccessor<AccessorType::Generic, PointerLocation> fa_wire_in_loc = 
    wires.get_field_accessor(FID_IN_LOC).typeify<PointerLocation>();
  RegionAccessor<AccessorType::Generic, PointerLocation> fa_wire_out_loc = 
    wires.get_field_accessor(FID_OUT_LOC).typeify<PointerLocation>();
  RegionAccessor<AccessorType::Generic, float> fa_wire_inductance = 
    wires.get_field_accessor(FID_INDUCTANCE).typeify<float>();
  RegionAccessor<AccessorType::Generic, float> fa_wire_resistance = 
    wires.get_field_accessor(FID_RESISTANCE).typeify<float>();
  RegionAccessor<AccessorType::Generic, float> fa_wire_cap = 
    wires.get_field_accessor(FID_WIRE_CAP).typeify<float>();
  ptr_t *first_wires = new ptr_t[num_pieces];
  // Allocate all the wires
  {
    IndexAllocator wire_allocator = runtime->create_index_allocator(ctx, ckt.all_wires.get_index_space());
    wire_allocator.alloc(num_pieces * wires_per_piece);
  }
  {
    IndexIterator itr(ckt.all_wires.get_index_space());
    for (int n = 0; n < num_pieces; n++)
    {
      for (int i = 0; i < wires_per_piece; i++)
      {
        assert(itr.has_next());
        ptr_t wire_ptr = itr.next();
        // Record the first wire pointer for this piece
        if (i == 0)
          first_wires[n] = wire_ptr;
        for (int j = 0; j < WIRE_SEGMENTS; j++)
          fa_wire_currents[j].write(wire_ptr, 0.f);
        for (int j = 0; j < WIRE_SEGMENTS-1; j++) 
          fa_wire_voltages[j].write(wire_ptr, 0.f);

        float resistance = drand48() * 10.0 + 1.0;
        fa_wire_resistance.write(wire_ptr, resistance);
        // Keep inductance on the order of 1e-3 * dt to avoid resonance problems
        float inductance = (drand48() + 0.1) * DELTAT * 1e-3;
        fa_wire_inductance.write(wire_ptr, inductance);
        float capacitance = drand48() * 0.1;
        fa_wire_cap.write(wire_ptr, capacitance);

        fa_wire_in_ptr.write(wire_ptr, random_element(piece_node_ptrs[n])); //private_node_map[n].points));

        if ((100 * drand48()) < pct_wire_in_piece)
        {
          fa_wire_out_ptr.write(wire_ptr, random_element(piece_node_ptrs[n])); //private_node_map[n].points));
        }
        else
        {
          // pick a random other piece and a node from there
          int nn = int(drand48() * (num_pieces - 1));
          if(nn >= n) nn++;

	  // pick an arbitrary node, except that if it's one that didn't used to be shared, make the 
	  //  sequentially next pointer shared instead so that each node's shared pointers stay compact
	  int idx = int(drand48() * piece_node_ptrs[nn].size());
	  if(idx > piece_shared_nodes[nn])
	    idx = piece_shared_nodes[nn]++;
	  ptr_t out_ptr = piece_node_ptrs[nn][idx];

          fa_wire_out_ptr.write(wire_ptr, out_ptr); 
          // This node is no longer private
          privacy_map[0].points.erase(out_ptr);
          privacy_map[1].points.insert(out_ptr);
          ghost_node_map[n].points.insert(out_ptr);
        }
        wire_owner_map[n].points.insert(wire_ptr);
      }
    }
  }

  // Second pass: make some random fraction of the private nodes shared
  {
    IndexIterator itr(ckt.all_nodes.get_index_space()); 
    for (int n = 0; n < num_pieces; n++)
    {
      for (int i = 0; i < nodes_per_piece; i++)
      {
        assert(itr.has_next());
        ptr_t node_ptr = itr.next();
        if (privacy_map[0].points.find(node_ptr) == privacy_map[0].points.end())
        {
          private_node_map[n].points.erase(node_ptr);
          // node is now shared
          shared_node_map[n].points.insert(node_ptr);
          locator_acc.write(node_ptr,SHARED_PTR); // node is shared 
        }
        else
        {
          locator_acc.write(node_ptr,PRIVATE_PTR); // node is private 
        }
      }
    }
  }
  // Second pass (part 2): go through the wires and update the locations
  {
    IndexIterator itr(ckt.all_wires.get_index_space());
    for (int n = 0; n < num_pieces; n++)
    {
      for (int i = 0; i < wires_per_piece; i++)
      {
        assert(itr.has_next());
        ptr_t wire_ptr = itr.next();
        ptr_t in_ptr = fa_wire_in_ptr.read(wire_ptr);
        ptr_t out_ptr = fa_wire_out_ptr.read(wire_ptr);

        fa_wire_in_loc.write(wire_ptr, 
            find_location(in_ptr, private_node_map[n].points, 
              shared_node_map[n].points, ghost_node_map[n].points));     
        fa_wire_out_loc.write(wire_ptr, 
            find_location(out_ptr, private_node_map[n].points, 
              shared_node_map[n].points, ghost_node_map[n].points));
      }
    }
  }

  runtime->unmap_region(ctx, wires);
  runtime->unmap_region(ctx, nodes);
  runtime->unmap_region(ctx, locator);

  // Now we can create our partitions and update the circuit pieces

  // first create the privacy partition that splits all the nodes into either shared or private
  IndexPartition privacy_part = runtime->create_index_partition(ctx, ckt.all_nodes.get_index_space(), privacy_map, true/*disjoint*/);
  
  IndexSpace all_private = runtime->get_index_subspace(ctx, privacy_part, 0);
  IndexSpace all_shared  = runtime->get_index_subspace(ctx, privacy_part, 1);
  

  // Now create partitions for each of the subregions
  Partitions result;
  IndexPartition priv = runtime->create_index_partition(ctx, all_private, private_node_map, true/*disjoint*/);
  result.pvt_nodes = runtime->get_logical_partition_by_tree(ctx, priv, ckt.all_nodes.get_field_space(), ckt.all_nodes.get_tree_id());
  IndexPartition shared = runtime->create_index_partition(ctx, all_shared, shared_node_map, true/*disjoint*/);
  result.shr_nodes = runtime->get_logical_partition_by_tree(ctx, shared, ckt.all_nodes.get_field_space(), ckt.all_nodes.get_tree_id());
  IndexPartition ghost = runtime->create_index_partition(ctx, all_shared, ghost_node_map, false/*disjoint*/);
  result.ghost_nodes = runtime->get_logical_partition_by_tree(ctx, ghost, ckt.all_nodes.get_field_space(), ckt.all_nodes.get_tree_id());

  IndexPartition pvt_wires = runtime->create_index_partition(ctx, ckt.all_wires.get_index_space(), wire_owner_map, true/*disjoint*/);
  result.pvt_wires = runtime->get_logical_partition_by_tree(ctx, pvt_wires, ckt.all_wires.get_field_space(), ckt.all_wires.get_tree_id()); 

  IndexPartition locs = runtime->create_index_partition(ctx, ckt.node_locator.get_index_space(), locator_node_map, true/*disjoint*/);
  result.node_locations = runtime->get_logical_partition_by_tree(ctx, locs, ckt.node_locator.get_field_space(), ckt.node_locator.get_tree_id());

  // Build the pieces
  for (int n = 0; n < num_pieces; n++)
  {
    pieces[n].pvt_nodes = runtime->get_logical_subregion_by_color(ctx, result.pvt_nodes, n);
    pieces[n].shr_nodes = runtime->get_logical_subregion_by_color(ctx, result.shr_nodes, n);
    pieces[n].ghost_nodes = runtime->get_logical_subregion_by_color(ctx, result.ghost_nodes, n);
    pieces[n].pvt_wires = runtime->get_logical_subregion_by_color(ctx, result.pvt_wires, n);
    pieces[n].num_wires = wires_per_piece;
    pieces[n].first_wire = first_wires[n];
    pieces[n].num_nodes = nodes_per_piece;
    pieces[n].first_node = first_nodes[n];

    pieces[n].dt = DELTAT;
    pieces[n].steps = steps;
  }

  delete [] first_wires;
  delete [] first_nodes;

  log_circuit(LEVEL_PRINT,"Finished initializing simulation...");

  return result;
}

extern "C" void go_circuit_c(int argc, char ** argv) {
    go_circuit(argc, argv);
}
