#ifndef VLASOVMOVER_H
#define VLASOVMOVER_H

#include <vector>

#include "definitions.h"
#include "cell_spatial.h"

bool finalizeMover();

#ifdef PARGRID 

#include "pargrid.h"

bool initializeMover(ParGrid<SpatialCell>& mpiGrid);
void calculateSimParameters(ParGrid<SpatialCell>& mpiGrid,creal& t,Real& dt);
void calculateCellParameters(ParGrid<SpatialCell>& mpiGrid,creal& t,ID::type cell);
void calculateAcceleration(ParGrid<SpatialCell>& mpiGrid);
void calculateSpatialDerivatives(ParGrid<SpatialCell>& mpiGrid);
void calculateSpatialFluxes(ParGrid<SpatialCell>& mpiGrid);
void calculateSpatialPropagation(ParGrid<SpatialCell>& mpiGrid,const bool& secondStep,const bool& transferAvgs);
void initialLoadBalance(ParGrid<SpatialCell>& mpiGrid);
void calculateVelocityMoments(ParGrid<SpatialCell>& mpiGrid);

#else // DCCRG

#include <stdint.h>
#include <dccrg.hpp>

namespace Main {
   std::vector<uint64_t> cells;
}

void initializeMover(ParGrid<SpatialCell>& mpiGrid);
void calculateSimParameters(dccrg<SpatialCell>& mpiGrid,creal& t,Real& dt);
void calculateCellParameters(dccrg<SpatialCell>& mpiGrid,creal& t,uint64_t& cell);
void calculateAcceleration(dccrg<SpatialCell>& mpiGrid);
void calculateSpatialDerivatives(dccrg<SpatialCell>& mpiGrid);
void calculateSpatialFluxes(dccrg<SpatialCell>& mpiGrid);
void calculateSpatialPropagation(dccrg<SpatialCell>& mpiGrid,const bool& secondStep,const bool& transferAvgs);
void initialLoadBalance(dccrg<SpatialCell>& mpiGrid);
void calculateVelocityMoments(dccrg<SpatialCell>& mpiGrid);

#endif

#endif

