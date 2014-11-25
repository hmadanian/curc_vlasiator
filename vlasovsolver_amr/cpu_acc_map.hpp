/*
This file is part of Vlasiator.
Copyright 2013, 2014 Finnish Meteorological Institute
*/

#ifndef CPU_ACC_MAP_H
#define CPU_ACC_MAP_H

#include  "vec4.h"
#include "algorithm"
#include "cmath"
#include "utility"
#include "common.h"
#include "spatial_cell.hpp"
#include "cpu_acc_sort_blocks.hpp"
#include "cpu_1d_pqm.hpp"
#include "cpu_1d_ppm.hpp"
#include "cpu_1d_plm.hpp"

#define MAX_BLOCKS_PER_DIM 100

//index in the temporary and padded column data values array. Each
//column has an empty block in ether end.
#define i_pcolumnv(num_k_blocks, k_block, j, k) ( (j) * WID * (num_k_blocks + 2) +  (k) + ( k_block + 1 ) * WID )

using namespace spatial_cell;

struct PropagParams {
   Real intersection;
   Real intersection_di;
   Real intersection_dj;
   Real intersection_dk;
   
   Real dv;
   Real v_min;
   Real v_max;
   Real inv_dv;

   int dimension;
   int i_mapped;
   int j_mapped;
   int k_mapped;
   int refMul;
   uint8_t refLevel;
   uint8_t maxRefLevel;
   vmesh::GlobalID Nx;
   vmesh::GlobalID Ny;
   vmesh::GlobalID k_cell_global_target_max;
};

void map_1d(SpatialCell* spatial_cell,PropagParams& params,
	    vmesh::VelocityMesh<vmesh::GlobalID,vmesh::LocalID>& vmesh,
	    vmesh::VelocityBlockContainer<vmesh::LocalID>& blockContainer);

void generateTargetMesh(SpatialCell* spatial_cell,const std::vector<vmesh::LocalID>& blocks,PropagParams& params,
			const uint8_t& targetRefLevel,const vmesh::VelocityMesh<vmesh::GlobalID,vmesh::LocalID>& vmesh);

/* 
   Here we map from the current time step grid, to a target grid which
   is the lagrangian departure grid (so th grid at timestep +dt,
   tracked backwards by -dt)

   TODO: parallelize with openMP over block-columns. If one also
   pre-creates new blocks in a separate loop first (serial operation),
   then the openmp parallization would scale well (better than over
   spatial cells), and would not need synchronization.
   
*/

bool map_1d(SpatialCell* spatial_cell,Transform<Real,3,Affine>& fwd_transform,Transform<Real,3,Affine>& bwd_transform,int dimension,int propag) {
   //std::cerr << "mapping" << std::endl;
   /*amr_ref_criteria::Base* refCriterion = getObjectWrapper().amrVelRefCriteria.create(Parameters::amrVelRefCriterion);
   if (refCriterion != NULL) {
      refCriterion->initialize("");
      spatial_cell->coarsen_blocks(refCriterion);
      delete refCriterion;
   }*/
   
   // Move the old velocity mesh and data to the variables below (very fast)
   vmesh::VelocityMesh<vmesh::GlobalID,vmesh::LocalID> vmesh;
   vmesh::VelocityBlockContainer<vmesh::LocalID> blockContainer;
   spatial_cell->swap(vmesh,blockContainer);

   // Sort the blocks according to their refinement levels (very fast)
   const uint8_t maxRefLevel = vmesh::VelocityMesh<vmesh::GlobalID,vmesh::LocalID>::getMaxAllowedRefinementLevel();
   std::vector<std::vector<vmesh::LocalID> > blocks(maxRefLevel+1);
   for (vmesh::LocalID block=0; block<vmesh.size(); ++block) {
      uint8_t refLevel = vmesh::VelocityMesh<vmesh::GlobalID,vmesh::LocalID>::getRefinementLevel(vmesh.getGlobalID(block));
      blocks[refLevel].push_back(block);
   }

   // Computer intersections etc.
   PropagParams propagParams;
   propagParams.maxRefLevel = vmesh::VelocityMesh<vmesh::GlobalID,vmesh::LocalID>::getMaxAllowedRefinementLevel();
   propagParams.Nx = SpatialCell::get_velocity_grid_length(propagParams.maxRefLevel)[0];
   propagParams.Ny = SpatialCell::get_velocity_grid_length(propagParams.maxRefLevel)[1];
   propagParams.dimension = dimension;
   switch (propag) {
    case 0:
      compute_intersections_1st(spatial_cell, bwd_transform, fwd_transform, dimension, propagParams.maxRefLevel,
				propagParams.intersection,propagParams.intersection_di,propagParams.intersection_dj,propagParams.intersection_dk);
      break;
    case 1:
      compute_intersections_2nd(spatial_cell, bwd_transform, fwd_transform, dimension, propagParams.maxRefLevel,
				propagParams.intersection,propagParams.intersection_di,propagParams.intersection_dj,propagParams.intersection_dk);
      break;
    case 2:
      compute_intersections_3rd(spatial_cell, bwd_transform, fwd_transform, dimension, propagParams.maxRefLevel,
				propagParams.intersection,propagParams.intersection_di,propagParams.intersection_dj,propagParams.intersection_dk);
      break;
    default:
      std::cerr << "error in map1d" << std::endl; exit(1);
      break;
   }

   propagParams.dimension = dimension;
   propagParams.dv    = SpatialCell::get_velocity_grid_cell_size(propagParams.maxRefLevel)[dimension];
   propagParams.v_min = SpatialCell::get_velocity_grid_min_limits()[dimension];
   propagParams.v_max = SpatialCell::get_velocity_grid_max_limits()[dimension];
   propagParams.inv_dv = 1.0/propagParams.dv;
   propagParams.k_cell_global_target_max = SpatialCell::get_velocity_grid_length(propagParams.maxRefLevel)[dimension]*WID;
   switch (dimension) {
    case 0: {
      propagParams.i_mapped = 2;
      propagParams.j_mapped = 1;
      propagParams.k_mapped = 0;
      
      // Swap intersection di and dk
      const Real tmp=propagParams.intersection_di;
      propagParams.intersection_di=propagParams.intersection_dk;
      propagParams.intersection_dk=tmp;}
      break;
    case 1: {
      propagParams.i_mapped = 0;
      propagParams.j_mapped = 2;
      propagParams.k_mapped = 1;
      
      // Swap intersection dj and dk
      const Real tmp=propagParams.intersection_dj;
      propagParams.intersection_dj=propagParams.intersection_dk;
      propagParams.intersection_dk=tmp;}
      break;
    case 2:
      propagParams.i_mapped = 0;
      propagParams.j_mapped = 1;
      propagParams.k_mapped = 2;
      break;
    default:
      std::cerr << "error in map1d" << std::endl; exit(1);
      break;
   }
      
   bool rvalue = true;

//   std::cerr << "target mesh generation" << std::endl;
   phiprof::start("mesh generation");
   for (uint8_t r=0; r<blocks.size(); ++r) {
      for (uint8_t rr=r; rr<blocks.size(); ++rr) {
         propagParams.refLevel = rr;
         generateTargetMesh(spatial_cell,blocks[rr],propagParams,r,vmesh);
      }
   }
   
   /*
   for (uint8_t r=0; r<blocks.size(); ++r) {
      propagParams.refLevel = r;
      generateTargetMesh(spatial_cell,blocks[r],propagParams,0,vmesh);
   }
   for (uint8_t r=1; r<blocks.size(); ++r) {
      propagParams.refLevel = r;
      generateTargetMesh(spatial_cell,blocks[r],propagParams,1,vmesh);
   }*/
   phiprof::stop("mesh generation");
//   std::cerr << "\t mesh DONE" << std::endl;

   phiprof::start("mapping");
   map_1d(spatial_cell,propagParams,vmesh,blockContainer);
   phiprof::stop("mapping");

   // Merge values from coarse blocks to refined blocks wherever the same domain 
   // is covered by overlapping blocks (at different refinement levels)
   //spatial_cell->merge_values();
   
   //if (spatial_cell->checkMesh() == false) {
      //std::cerr << "error(s) in mesh, exiting" << std::endl; exit(1);
   //}

//   std::cerr << "cell has " << spatial_cell->get_number_of_velocity_blocks() << " blocks after map" << std::endl;
/*
   amr_ref_criteria::Base* refCriterion = getObjectWrapper().amrVelRefCriteria.create(Parameters::amrVelRefCriterion);
   if (refCriterion != NULL) {
      refCriterion->initialize("");
      spatial_cell->coarsen_blocks(refCriterion);
      delete refCriterion;
   }
*/   
   //rvalue = false;
   return true;
}

void generateTargetMesh(SpatialCell* spatial_cell,const std::vector<vmesh::LocalID>& blocks,PropagParams& params,
                        const uint8_t& targetRefLevel,const vmesh::VelocityMesh<vmesh::GlobalID,vmesh::LocalID>& vmesh) {
   params.refMul = pow(2,(params.maxRefLevel-params.refLevel));
   int baseMul = pow(2,params.maxRefLevel-targetRefLevel);

   for (size_t b=0; b<blocks.size(); ++b) {
      const vmesh::LocalID blockLID = blocks[b];
      const vmesh::GlobalID blockGID = vmesh.getGlobalID(blockLID);
      vmesh::LocalID sourceIndex[3];
      vmesh.getIndices(blockGID,params.refLevel,sourceIndex[0],sourceIndex[1],sourceIndex[2]);
      
      switch (params.dimension) {
       case 0: {
          const vmesh::LocalID temp=sourceIndex[2];
          sourceIndex[2]=sourceIndex[0];
          sourceIndex[0]=temp;}
         break;
       case 1: {
          vmesh::LocalID temp=sourceIndex[2];
          sourceIndex[2]=sourceIndex[1];
          sourceIndex[1]=temp;}
         break;
       case 2:
         break;
      }
      sourceIndex[0] *= WID*params.refMul;
      sourceIndex[1] *= WID*params.refMul;
      sourceIndex[2] *= WID*params.refMul;
      
      //Realf compression=0.0;
      vmesh::GlobalID k_trgt_min = std::numeric_limits<vmesh::GlobalID>::max();
      vmesh::GlobalID k_trgt_max = 0;
      for (int k=0; k<2; ++k) for (int j=0; j<2; ++j) for (int i=0; i<2; ++i) {
         const int i_cell = i*WID*params.refMul;
         const int j_cell = j*WID*params.refMul;
         const int k_cell = k*WID*params.refMul;
         
         Realf intersection_min = params.intersection +
           (sourceIndex[0]+i_cell)*params.intersection_di +
           (sourceIndex[1]+j_cell)*params.intersection_dj;
	 
         Realf v_top = params.v_min + (sourceIndex[2]+k_cell)*params.dv;
         
         // Global (mapped) k-index of the target cell.
         // Note that v_top may be outside of the velocity box in either direction (+ or -).
         vmesh::GlobalID lagrangian_gk_top = static_cast<vmesh::GlobalID>((v_top-intersection_min)/params.intersection_dk);
         if (lagrangian_gk_top >= params.k_cell_global_target_max) continue;

         //Realf v_k_min = (v_top-intersection_min)/params.intersection_dk;
         //Realf v_k_max = (v_top+params.dv*params.refMul-intersection_min)/params.intersection_dk;
         //Realf c = (v_k_max-v_k_min)/(params.refMul);
         //if (c > compression) compression=c;
         
         if (lagrangian_gk_top < k_trgt_min) k_trgt_min = lagrangian_gk_top;
         if (lagrangian_gk_top > k_trgt_max) k_trgt_max = lagrangian_gk_top;

         /*
         // DEBUG
         if (k_trgt_min > params.Nx*WID-1 || k_trgt_max > params.Nx*WID-1) {
            std::cerr << "v_top " << v_top << std::endl;
            std::cerr << "src " << sourceIndex[0]+i_cell << ' ' << sourceIndex[1]+j_cell << ' ' << sourceIndex[2]+k_cell << std::endl;
            std::cerr << "k_min,max: " << k_trgt_min << ' ' << k_trgt_max << " max " << params.Nx*WID << std::endl;
            exit(1);
         }
         // END DEBUG
         */
      }

      if (targetRefLevel == 0) {
         vmesh::GlobalID targetBlockIndex[3];
         targetBlockIndex[params.i_mapped] = sourceIndex[0] / (WID*baseMul);
         targetBlockIndex[params.j_mapped] = sourceIndex[1] / (WID*baseMul);
         k_trgt_min /= (WID*baseMul);
         k_trgt_max /= (WID*baseMul);
         
         for (vmesh::GlobalID k=k_trgt_min; k<=k_trgt_max; ++k) {
            targetBlockIndex[params.k_mapped] = k;
            vmesh::GlobalID targetBlock = vmesh.getGlobalID(targetRefLevel,targetBlockIndex);
            spatial_cell->add_velocity_block(targetBlock);
         }
      } else {
         //uint8_t r_target = params.refLevel;
         //bool dynamicRefinement = true;
         //if (dynamicRefinement == true) {
         //   if (compression < 0.75) ++r_target;
         //   r_target = std::min(r_target,vmesh.getMaxAllowedRefinementLevel());
         //}
         
         uint8_t r=targetRefLevel;
         //while (r <= r_target) {
         //baseMul = pow(2,params.maxRefLevel-r);
         vmesh::GlobalID targetBlockIndex[3];
         targetBlockIndex[params.i_mapped] = sourceIndex[0] / (WID*baseMul);
         targetBlockIndex[params.j_mapped] = sourceIndex[1] / (WID*baseMul);
         vmesh::GlobalID k_min = k_trgt_min / (WID*baseMul);
         vmesh::GlobalID k_max = k_trgt_max / (WID*baseMul);
         for (vmesh::GlobalID k=k_min; k<=k_max; ++k) {
            targetBlockIndex[params.k_mapped] = k;
            vmesh::GlobalID targetBlock = vmesh.getGlobalID(r,targetBlockIndex);
            targetBlock = vmesh.getParent(targetBlock);
            std::map<vmesh::GlobalID,vmesh::LocalID> insertedBlocks;
            spatial_cell->refine_block(targetBlock,insertedBlocks);
         }
         //   ++r;
         //}
      }
      /*
      vmesh::GlobalID targetBlockIndex[3];
      targetBlockIndex[params.i_mapped] = sourceIndex[0] / (WID*baseMul);
      targetBlockIndex[params.j_mapped] = sourceIndex[1] / (WID*baseMul);
      k_trgt_min /= (WID*baseMul);
      k_trgt_max /= (WID*baseMul);
      for (vmesh::GlobalID k=k_trgt_min; k<=k_trgt_max; ++k) {
       targetBlockIndex[params.k_mapped] = k;
       vmesh::GlobalID targetBlock = vmesh.getGlobalID(targetRefLevel,targetBlockIndex);

       if (targetRefLevel == 0) {
	    spatial_cell->add_velocity_block(targetBlock);
       } else {
	    targetBlock = vmesh.getParent(targetBlock);
	    std::map<vmesh::GlobalID,vmesh::LocalID> insertedBlocks;
	    spatial_cell->refine_block(targetBlock,insertedBlocks);
       }
       }
       */
   }
}

/**
 * @param spatial_cell Propagated spatial cell, contains a valid (unsorted) target mesh.
 * @param params Parameters needed in the propagation.
 * @param vmesh The source mesh.
 * @param blockContainer Source mesh data.*/
void map_1d(SpatialCell* spatial_cell,PropagParams& params,
            vmesh::VelocityMesh<vmesh::GlobalID,vmesh::LocalID>& vmesh,
            vmesh::VelocityBlockContainer<vmesh::LocalID>& blockContainer) {
   
   std::vector<vmesh::GlobalID> removeList;

   // TEST
   const int PAD=3;
   Realf* array1 = new Realf[(WID+2*PAD)*(WID+2*PAD)*(WID+2*PAD)];
   Realf* array2 = new Realf[(WID+2*PAD)*(WID+2*PAD)*(WID+2*PAD)];   

   int transp[3];
   switch (params.dimension) {
    case 0:
       transp[0] = (WID+2*PAD)*(WID+2*PAD);
       transp[1] = WID+2*PAD;
       transp[2] = 1;
      break;
    case 1:
       transp[0] = 1;
       transp[1] = (WID+2*PAD)*(WID+2*PAD);
       transp[2] = WID+2*PAD;
      break;
    case 2:
      transp[0] = 1;
      transp[1] = WID+2*PAD;
      transp[2] = (WID+2*PAD)*(WID+2*PAD);
      break;
   }
   // END TEST

   for (vmesh::LocalID targetLID=0; targetLID<spatial_cell->get_number_of_velocity_blocks(); ++targetLID) {
      //std::cerr << "lid " << targetLID << std::endl;
      vmesh::GlobalID targetGID = spatial_cell->get_velocity_block_global_id(targetLID);
      params.refLevel = vmesh.getRefinementLevel(targetGID);
      params.refMul = std::pow(2,params.maxRefLevel-params.refLevel);
      vmesh::LocalID targetIndex[3];
      vmesh.getIndices(targetGID,params.refLevel,targetIndex[0],targetIndex[1],targetIndex[2]);

      // Pointer to target data
      Realf* data_trgt = spatial_cell->get_data(targetLID);
      
      switch (params.dimension) {
       case 0: {
          const vmesh::LocalID temp=targetIndex[2];
          targetIndex[2]=targetIndex[0];
          targetIndex[0]=temp;
       }
         break;
       case 1: {
          vmesh::LocalID temp=targetIndex[2];
          targetIndex[2]=targetIndex[1];
          targetIndex[1]=temp;
       }
         break;
       case 2:
         break;
      }
      targetIndex[0] *= WID*params.refMul;
      targetIndex[1] *= WID*params.refMul;
      targetIndex[2] *= WID*params.refMul;
      
      Real accum=0;

      // Note: the i,j,k indices below are transposed indices:
      for (int k=0; k<WID; ++k) {
         vmesh::GlobalID k_cell_src_min_global = std::numeric_limits<vmesh::GlobalID>::max();
         vmesh::GlobalID k_cell_src_max_global = 0;
         Real v_src_bots[WID*WID];
         Real v_src_tops[WID*WID];
         
         // Calculate source cell (transposed) k-index integration bounding box.
         // First map each i/j target cell backwards. Then k_cell_src_min_global is the 
         // smallest value of all source k-indices, and k_cell_src_max_global is the 
         // largest value.
         // 
         // The bounding box integration trick doubled the speed of the AMR algorithm
         for (int j=0; j<WID; ++j) for (int i=0; i<WID; ++i) {
            // Target cell global i/j/k indices (transposed):
            vmesh::GlobalID i_cell_trgt = targetIndex[0] + i*params.refMul;
            vmesh::GlobalID j_cell_trgt = targetIndex[1] + j*params.refMul;
            vmesh::GlobalID k_cell_trgt = targetIndex[2] + k*params.refMul;
            
            Real intersection_min = params.intersection + i_cell_trgt*params.intersection_di + j_cell_trgt*params.intersection_dj;
            
            // Velocities which the bottom and top z-faces of the target cell maps to.
            // Note that these may be outside of the velocity mesh
            Real v_src_bot = k_cell_trgt*params.intersection_dk + intersection_min;
            Real v_src_top = (k_cell_trgt+params.refMul)*params.intersection_dk + intersection_min;
            if (v_src_top < v_src_bot) {
               Real tmp = v_src_top;
               v_src_top = v_src_bot;
               v_src_bot = tmp;
            }

            // Convert velocities to k-indices
            Real v_src_bot_l = (v_src_bot-params.v_min)/params.dv;
            Real v_src_top_l = (v_src_top-params.v_min)/params.dv;

            // Source cell bottom and top (transposed) k-indices
            // Velocities v_src_bot_l,v_src_top_l can be outside the box in either (+ or -) direction.
            // If v < params.v_min then there will be an integer underflow and the k-index is close 
            // to std::numeric_limits<vmesh::GlobalID>::max() value. The std::min functions below 
            // force both indices to be valid in such a way that the mapping/integration returns 
            // the correct value.
            vmesh::GlobalID k_cell_src_top = std::min(params.Nx*WID-1,static_cast<vmesh::GlobalID>(v_src_top_l));
            vmesh::GlobalID k_cell_src_bot = std::min(k_cell_src_top ,static_cast<vmesh::GlobalID>(v_src_bot_l));
            k_cell_src_min_global = std::min(k_cell_src_min_global,k_cell_src_bot);
            k_cell_src_max_global = std::max(k_cell_src_max_global,k_cell_src_top);
            v_src_bots[j*WID+i] = v_src_bot_l;
            v_src_tops[j*WID+i] = v_src_top_l;

            // DEBUG
            int trgtCellIndex[3];
            trgtCellIndex[params.i_mapped] = i;
            trgtCellIndex[params.j_mapped] = j;
            trgtCellIndex[params.k_mapped] = k;
            const int trgtCell = vblock::index(trgtCellIndex[0],trgtCellIndex[1],trgtCellIndex[2]);
            spatial_cell->get_fx(targetLID)[trgtCell] = v_src_bot;
            // END DEBUG
         } // for (int j=0; j<WID; ++j) for (int i=0; i<WID; ++i)

         // Iterate over the source cell bounding box
         vmesh::GlobalID k_cell_src = k_cell_src_min_global;
         while (k_cell_src <= k_cell_src_max_global) {
            // Source cell(s) can map to several blocks. Each source block contains a 
            // range of source k-indices, however, so we can save time by caching the 
            // source block
            uint8_t srcRefLevel;
            int srcRefMul = 1;
            int factor;
            vmesh::GlobalID k_cell_src_max = 0;
            vmesh::GlobalID i_block_src,j_block_src,k_block_src;
            Realf* data_src = NULL;
            vmesh::GlobalID srcIndex[3];
            if (k_cell_src >= k_cell_src_max) {
               // Find the source block
               phiprof::start("source block search");
               srcIndex[params.i_mapped] = targetIndex[0];
               srcIndex[params.j_mapped] = targetIndex[1];
               srcIndex[params.k_mapped] = k_cell_src;
               srcRefLevel = params.refLevel;
               vmesh::GlobalID sourceGID = vmesh.findBlockDown(srcRefLevel,srcIndex);
               srcRefMul = std::pow(2,params.maxRefLevel-srcRefLevel);
               factor = std::pow(2,params.refLevel-srcRefLevel);
               
               if (sourceGID == vmesh.invalidGlobalID()) {
                  ++k_cell_src;
                  phiprof::stop("source block search");
                  continue;
               }

               // Existing source block found, grab pointer to source data
               data_src = blockContainer.getData(vmesh.getLocalID(sourceGID));
               
               // TEST
               //std::cerr << "starting to load" << std::endl;
               spatial_cell->fetch_data<PAD>(sourceGID,vmesh,blockContainer.getData(),array1);
               //for (int kk=0; kk<WID+2*PAD; ++kk) for (int jj=0; jj<WID+2*PAD; ++jj) for (int ii=0; ii<WID+2*PAD; ++ii) {
               //   array2[kk*transp[2]+jj*transp[1]+ii*transp[0]] = array1[vblock::padIndex<PAD>(ii,jj,kk)];
               //}
               //std::cerr << "loaded" << std::endl;
               // END TEST

               // Compute (transposed) i/j/k indices of the source block at the source block refinement level
               i_block_src = srcIndex[params.i_mapped] / (WID*srcRefMul);
               j_block_src = srcIndex[params.j_mapped] / (WID*srcRefMul);
               k_block_src = srcIndex[params.k_mapped] / (WID*srcRefMul);
               
               // Maximum k-index this source block contains
               k_cell_src_max = std::min(k_cell_src_max_global,k_block_src*(WID*srcRefMul) + WID*srcRefMul - 1);
               phiprof::stop("source block search");
            }
            
            // Iterate over all source k-cells in this source block
            while (k_cell_src <= k_cell_src_max) {
               // Source cell (transposed) k index within the source block, i.e., its value is in range 0-(WID-1)
               const int k_cell_bot = (k_cell_src - k_block_src*(WID*srcRefMul)) / srcRefMul;

               for (int j=0; j<WID; ++j) for (int i=0; i<WID; ++i) {
                  // Source cell global i/j/k indices (untransposed):
                  srcIndex[params.i_mapped] = targetIndex[0] + i*params.refMul;
                  srcIndex[params.j_mapped] = targetIndex[1] + j*params.refMul;
                  
                  phiprof::start("index computations");
                  Real v_top = std::min(v_src_tops[j*WID+i],(Real)(k_block_src*(WID*srcRefMul)+(k_cell_bot+1)*srcRefMul));
                  Real v_bot = std::max(v_src_bots[j*WID+i],(Real)(k_block_src*(WID*srcRefMul)+(k_cell_bot  )*srcRefMul));
                  
                  Real v_int_max = (v_top - (k_block_src*(WID*srcRefMul)+k_cell_bot*srcRefMul)) / srcRefMul;
                  Real v_int_min = (v_bot - (k_block_src*(WID*srcRefMul)+k_cell_bot*srcRefMul)) / srcRefMul;
                  v_int_min = std::min(v_int_max,v_int_min);
                  
                  // Source cell (transposed) indices within the source block, i.e., 
                  // each index is limited to range 0-(WID-1)
                  int srcCellIndex[3];
                  //srcCellIndex[params.i_mapped] = (srcIndex[params.i_mapped] - i_block_src*(WID*srcRefMul)) / srcRefMul;
                  //srcCellIndex[params.j_mapped] = (srcIndex[params.j_mapped] - j_block_src*(WID*srcRefMul)) / srcRefMul;
                  //srcCellIndex[params.k_mapped] = k_cell_bot;
                  srcCellIndex[params.i_mapped] = PAD + (srcIndex[params.i_mapped] - i_block_src*(WID*srcRefMul)) / srcRefMul;
                  srcCellIndex[params.j_mapped] = PAD + (srcIndex[params.j_mapped] - j_block_src*(WID*srcRefMul)) / srcRefMul;
                  srcCellIndex[params.k_mapped] = PAD + k_cell_bot;

                  // DEBUG
                  bool ok = true;
                  if (srcCellIndex[0] < PAD || srcCellIndex[0] > WID+PAD-1) ok = false;
                  if (srcCellIndex[1] < PAD || srcCellIndex[1] > WID+PAD-1) ok = false;
                  if (srcCellIndex[2] < PAD || srcCellIndex[2] > WID+PAD-1) ok = false;
                  if (ok == false) {
                     std::cerr << "indices" << std::endl;
                  }
                  // END DEBUG
                  
                  // Target cell (untransposed) indices
                  int trgtCellIndex[3];
                  trgtCellIndex[params.i_mapped] = i;
                  trgtCellIndex[params.j_mapped] = j;
                  trgtCellIndex[params.k_mapped] = k;
                  const int trgtCell = vblock::index(trgtCellIndex[0],trgtCellIndex[1],trgtCellIndex[2]);
                  phiprof::stop("index computations");

                  //data_trgt[trgtCell] += data_src[vblock::index(srcCellIndex[0],srcCellIndex[1],srcCellIndex[2])] * (v_int_max-v_int_min) * factor;
                  data_trgt[trgtCell] += array1[vblock::padIndex<PAD>(srcCellIndex[0],srcCellIndex[1],srcCellIndex[2])] * (v_int_max-v_int_min) * factor;

                  //accum += data_src[vblock::index(srcCellIndex[0],srcCellIndex[1],srcCellIndex[2])] * (v_int_max-v_int_min) * factor;
                  accum += array1[vblock::padIndex<PAD>(srcCellIndex[0],srcCellIndex[1],srcCellIndex[2])] * (v_int_max-v_int_min) * factor;
               }
               
               // Increase k_cell_src to point to the next source cell, taking refinement
               // level into account.
               k_cell_src = k_block_src*(WID*srcRefMul)+(k_cell_bot+1)*srcRefMul;
               
            } // while (k_cell_src <= k_cell_src_max)
         } // while (k_cell_src <= k_cell_src_max_global)
      } // for (int k=0; k<WID; ++k) 

      if (accum < Parameters::sparseMinValue) {
         removeList.push_back(targetGID);
      }
   } // for-loop over velocity blocks
   
   delete [] array1; delete [] array2;
   for (size_t b=0; b<removeList.size(); ++b) spatial_cell->remove_velocity_block(removeList[b]);
}

#endif   
