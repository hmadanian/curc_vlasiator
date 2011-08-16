#include <cstdlib>
#include <iostream>
#include <cmath>

#include "cell_spatial.h"
#include "common.h"
#include "project.h"
#include "parameters.h"

using namespace std;

bool cellParametersChanged(creal& t) {return false;}

Real calcPhaseSpaceDensity(creal& x,creal& y,creal& z,creal& dx,creal& dy,creal& dz,
			   creal& vx,creal& vy,creal& vz,creal& dvx,creal& dvy,creal& dvz) {
   if (vz >= 0.2 && vz < 0.2+dvz) {
      if (vx >= 0.2 && vx < 0.2+dvx) {
	 if (vy >= -0.2-dvy*1.001 && vy < -0.2) return 1.0;
	 if (vy >= 0.2 && vy < 0.2+dvy) return 1.0;
      }
      if (vx >= -0.2-dvx*1.001 && vx < -0.2) {
	 if (vy >= -0.2-dvy*1.001 && vy < -0.2) return 1.0;
	 if (vy >= 0.2 && vy < 0.2+dvy) return 1.0;
      }
   }
   if (vz >= -0.2-dvz*1.001 && vz < -0.2) {
      if (vx >= 0.2 && vx < 0.2+dvx) {
	 if (vy >= -0.2-dvy*1.001 && vy < -0.2) return 1.0;
	 if (vy >= 0.2 && vy < 0.2+dvy) return 1.0;
      }
      if (vx >= -0.2-dvx*1.001 && vx < -0.2) {
	 if (vy >= -0.2-dvy*1.001 && vy < -0.2) return 1.0;
	 if (vy >= 0.2 && vy < 0.2+dvy) return 1.0;
      }
   }
   return 0.0;
}

void calcBlockParameters(Real* blockParams) { }

void calcCellParameters(Real* cellParams,creal& t) {
   cellParams[CellParams::EX   ] = 0.0;
   cellParams[CellParams::EY   ] = 0.0;
   cellParams[CellParams::EZ   ] = 0.0;
   cellParams[CellParams::BX   ] = 0.0;
   cellParams[CellParams::BY   ] = 0.0;
   cellParams[CellParams::BZ   ] = 0.0;
}

// TODO use this instead: template <class Grid, class CellData> void calcSimParameters(Grid<CellData>& mpiGrid...
#ifndef PARGRID
void calcSimParameters(dccrg<SpatialCell>& mpiGrid, creal& t, Real& /*dt*/) {
   std::vector<uint64_t> cells = mpiGrid.get_cells();
#else
void calcSimParameters(ParGrid<SpatialCell>& mpiGrid, creal& t, Real& /*dt*/) {
   std::vector<ID::type> cells;
   mpiGrid.getCells(cells);
#endif
   for (uint i = 0; i < cells.size(); ++i) {
      calcCellParameters(mpiGrid[cells[i]]->cpu_cellParams, t);
   }
}
