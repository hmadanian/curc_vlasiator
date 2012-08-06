/*
This file is part of Vlasiator.

Copyright 2011, 2012 Finnish Meteorological Institute

Vlasiator is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License version 3
as published by the Free Software Foundation.

Vlasiator is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Vlasiator. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef FIELDBOUNDARY_H
#define FIELDBOUNDARY_H

#include "../fieldsolver.h"
#include "projects_common.h"

template<typename CELLID,typename UINT,typename REAL>
REAL fieldBoundaryCopyFromExistingFaceNbrBx(
	const CELLID& cellID,
	const UINT& existingCells,
	const UINT& nonExistingCells,
	const dccrg::Dccrg<SpatialCell>& mpiGrid
) { 
   namespace p = projects;
   switch ((nonExistingCells & p::FACE_NBR_BITMASK)) {
    case p::MISSING_ZNEG:
      // If +z neighbour exists, copy value from there:
      if (((existingCells >> p::ZP1_YCC_XCC) & 1) == 1) {
         const CELLID neighbor = getNeighbour(mpiGrid, cellID, 0, 0, +1);
         if (mpiGrid[neighbor] == NULL) {
            std::cerr << __FILE__ << ":" << __LINE__
               << " No data for neighbor " << neighbor
               << " while solving cell " << cellID
               << std::endl;
            abort();
         }
	 return mpiGrid[neighbor]->parameters[CellParams::BX]
	    + mpiGrid[neighbor]->parameters[CellParams::BX0];
      }
      break;
    case p::MISSING_YNEG:
      // If +y neighbour exists, copy value from there:
      if (((existingCells >> p::ZCC_YP1_XCC) & 1) == 1) {
         const CELLID neighbor = getNeighbour(mpiGrid, cellID, 0, +1, 0);
         if (mpiGrid[neighbor] == NULL) {
            std::cerr << __FILE__ << ":" << __LINE__
               << " No data for neighbor " << neighbor
               << " while solving cell " << cellID
               << std::endl;
            abort();
         }
	 return mpiGrid[neighbor]->parameters[CellParams::BX]
	    + mpiGrid[neighbor]->parameters[CellParams::BX0];
      }
      break;
    case p::MISSING_YPOS:
      // If -y neighbour exists, copy value from there:
      if (((existingCells >> p::ZCC_YM1_XCC) & 1) == 1) {
         const CELLID neighbor = getNeighbour(mpiGrid, cellID, 0, -1, 0);
         if (mpiGrid[neighbor] == NULL) {
            std::cerr << __FILE__ << ":" << __LINE__
               << " No data for neighbor " << neighbor
               << " while solving cell " << cellID
               << std::endl;
            abort();
         }
	 return mpiGrid[neighbor]->parameters[CellParams::BX]
	    + mpiGrid[neighbor]->parameters[CellParams::BX0];
      }
      break;
    case p::MISSING_ZPOS:
      // If -z neighbour exists, copy value from there:
      if (((existingCells >> p::ZM1_YCC_XCC) & 1) == 1) {
         const CELLID neighbor = getNeighbour(mpiGrid, cellID, 0, 0, -1);
         if (mpiGrid[neighbor] == NULL) {
            std::cerr << __FILE__ << ":" << __LINE__
               << " No data for neighbor " << neighbor
               << " while solving cell " << cellID
               << std::endl;
            abort();
         }
	 return mpiGrid[neighbor]->parameters[CellParams::BX]
	    + mpiGrid[neighbor]->parameters[CellParams::BX0];
      }
      break;
    default:
      return 0.0;
   }
   return 0.0;
}

template<typename CELLID,typename UINT,typename REAL>
REAL fieldBoundaryCopyFromExistingFaceNbrBy(
	const CELLID& cellID,
	const UINT& existingCells,
	const UINT& nonExistingCells,
	const dccrg::Dccrg<SpatialCell>& mpiGrid
) {
   namespace p = projects;
   switch ((nonExistingCells & p::FACE_NBR_BITMASK)) {
    case p::MISSING_ZNEG:
      // If +z neighbour exists, copy value from there:
      if (((existingCells >> p::ZP1_YCC_XCC) & 1) == 1) {
         const CELLID neighbor = getNeighbour(mpiGrid,cellID,0,0,+1);
         if (mpiGrid[neighbor] == NULL) {
            std::cerr << __FILE__ << ":" << __LINE__
               << " No data for neighbor " << neighbor
               << " while solving cell " << cellID
               << std::endl;
            abort();
         }

         if (mpiGrid[neighbor]->parameters == NULL) {
            std::cerr << __FILE__ << ":" << __LINE__
               << " No cell parameters for cell " << neighbor
               << " while solving cell " << cellID
               << std::endl;
            abort();
         }

	 return mpiGrid[neighbor]->parameters[CellParams::BY]
	    + mpiGrid[neighbor]->parameters[CellParams::BY0];
      }
      break;
    case p::MISSING_XNEG:
      // If +x neighbour exists, copy value from there:
      if (((existingCells >> p::ZCC_YCC_XP1) & 1) == 1) {
         const CELLID neighbor = getNeighbour(mpiGrid,cellID,+1,0,0);
         if (mpiGrid[neighbor] == NULL) {
            std::cerr << __FILE__ << ":" << __LINE__
               << " No data for neighbor " << neighbor
               << " while solving cell " << cellID
               << std::endl;
            abort();
         }

         if (mpiGrid[neighbor]->parameters == NULL) {
            std::cerr << __FILE__ << ":" << __LINE__
               << " No cell parameters for cell " << neighbor
               << " while solving cell " << cellID
               << std::endl;
            abort();
         }

	 return mpiGrid[neighbor]->parameters[CellParams::BY]
	    + mpiGrid[neighbor]->parameters[CellParams::BY0];
      }
      break;
    case p::MISSING_XPOS:
      // If -x neighbour exists, copy value from there:
      if (((existingCells >> p::ZCC_YCC_XM1) & 1) == 1) {
         const CELLID neighbor = getNeighbour(mpiGrid,cellID,-1,0,0);
         if (mpiGrid[neighbor] == NULL) {
            std::cerr << __FILE__ << ":" << __LINE__
               << " No data for neighbor " << neighbor
               << " while solving cell " << cellID
               << std::endl;
            abort();
         }

         if (mpiGrid[neighbor]->parameters == NULL) {
            std::cerr << __FILE__ << ":" << __LINE__
               << " No cell parameters for cell " << neighbor
               << " while solving cell " << cellID
               << std::endl;
            abort();
         }

	 return mpiGrid[neighbor]->parameters[CellParams::BY]
	    + mpiGrid[neighbor]->parameters[CellParams::BY0];
      }
      break;
    case p::MISSING_ZPOS:
      // If -z neighbour exists, copy value from there:
      if (((existingCells >> p::ZM1_YCC_XCC) & 1) == 1) {
         const CELLID neighbor = getNeighbour(mpiGrid,cellID,0,0,-1);
         if (mpiGrid[neighbor] == NULL) {
            std::cerr << __FILE__ << ":" << __LINE__
               << " No data for neighbor " << neighbor
               << " while solving cell " << cellID
               << std::endl;
            abort();
         }

         if (mpiGrid[neighbor]->parameters == NULL) {
            std::cerr << __FILE__ << ":" << __LINE__
               << " No cell parameters for cell " << neighbor
               << " while solving cell " << cellID
               << std::endl;
            abort();
         }

	 return mpiGrid[neighbor]->parameters[CellParams::BY]
	    + mpiGrid[neighbor]->parameters[CellParams::BY0];
      }
      break;
    default:
      return 0.0;
   }
   return 0.0;
}

template<typename CELLID,typename UINT,typename REAL>
REAL fieldBoundaryCopyFromExistingFaceNbrBz(const CELLID& cellID,const UINT& existingCells,const UINT& nonExistingCells,const dccrg::Dccrg<SpatialCell>& mpiGrid) {

    namespace p = projects;
   switch ((nonExistingCells & p::FACE_NBR_BITMASK)) {
    case p::MISSING_YNEG:
      // If +y neighbour exists, copy value from there:
      if (((existingCells >> p::ZCC_YP1_XCC) & 1) == 1) {
	 return mpiGrid[getNeighbour(mpiGrid,cellID,0,+1,0)]->parameters[CellParams::BZ]
	    + mpiGrid[getNeighbour(mpiGrid,cellID,0,+1,0)]->parameters[CellParams::BZ0];
      }
      break;
    case p::MISSING_XNEG:
      // If +x neighbour exists, copy value from there:
      if (((existingCells >> p::ZCC_YCC_XP1) & 1) == 1) {
	 return mpiGrid[getNeighbour(mpiGrid,cellID,+1,0,0)]->parameters[CellParams::BZ]
	    + mpiGrid[getNeighbour(mpiGrid,cellID,+1,0,0)]->parameters[CellParams::BZ0];
      }
      break;
    case p::MISSING_XPOS:
      // If -x neighbour exists, copy value from there:
      if (((existingCells >> p::ZCC_YCC_XM1) & 1) == 1) {
	 return mpiGrid[getNeighbour(mpiGrid,cellID,-1,0,0)]->parameters[CellParams::BZ]
	    + mpiGrid[getNeighbour(mpiGrid,cellID,-1,0,0)]->parameters[CellParams::BZ0];
      }
      break;
    case p::MISSING_YPOS:
      // If -y neighbour exists, copy value from there:
      if (((existingCells >> p::ZCC_YM1_XCC) & 1) == 1) {
	 return mpiGrid[getNeighbour(mpiGrid,cellID,0,-1,0)]->parameters[CellParams::BZ]
	    + mpiGrid[getNeighbour(mpiGrid,cellID,0,-1,0)]->parameters[CellParams::BZ0];
      }
      break;
    default:
      return 0.0;
   }
   return 0.0;
}

#endif
