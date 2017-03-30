/*
 * This file is part of Vlasiator.
 * Copyright 2010-2016 Finnish Meteorological Institute
 *
 * For details of usage, see the COPYING file and read the "Rules of the Road"
 * at http://vlasiator.fmi.fi/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <cstdlib>
#include <iostream>
#include <iomanip> // for setprecision()
#include <cmath>
#include <vector>
#include <sstream>
#include <ctime>
#include <array>
#include <sys/types.h>
#include <sys/stat.h>

#include "ioread.h"
#include "phiprof.hpp"
#include "parameters.h"
#include "logger.h"
#include "vlsv_reader_parallel.h"
#include "vlasovmover.h"
#include "object_wrapper.h"

using namespace std;
using namespace phiprof;

extern Logger logFile, diagnostic;

typedef Parameters P;

/*!
 * \brief Checks for command files written to the local directory.
 * If a file STOP was written and is readable, then a bailout with restart writing is initiated.
 * If a file KILL was written and is readable, then a bailout without a restart is initiated.
 * If a file SAVE was written and is readable, then restart writing without a bailout is initiated.
 * To avoid bailing out upfront on a new run the files are renamed with the date to keep a trace.
 * The function should only be called by MASTER_RANK. This ensures that resetting P::bailout_write_restart works.
 */
void checkExternalCommands() {
   struct stat tempStat;
   if (stat("STOP", &tempStat) == 0) {
      bailout(true, "Received an external STOP command. Setting bailout.write_restart to true.");
      P::bailout_write_restart = true;
      char newName[80];
      // Get the current time.
      const time_t rawTime = time(NULL);
      const struct tm * timeInfo = localtime(&rawTime);
      strftime(newName, 80, "STOP_%F_%H-%M-%S", timeInfo);
      rename("STOP", newName);
      return;
   }
   if(stat("KILL", &tempStat) == 0) {
      bailout(true, "Received an external KILL command. Setting bailout.write_restart to false.");
      P::bailout_write_restart = false;
      char newName[80];
      // Get the current time.
      const time_t rawTime = time(NULL);
      const struct tm * timeInfo = localtime(&rawTime);
      strftime(newName, 80, "KILL_%F_%H-%M-%S", timeInfo);
      rename("KILL", newName);
      return;
   }
   if(stat("SAVE", &tempStat) == 0) {
      cerr << "Received an external SAVE command. Writing a restart file." << endl;
      globalflags::writeRestart = true;
      char newName[80];
      // Get the current time.
      const time_t rawTime = time(NULL);
      const struct tm * timeInfo = localtime(&rawTime);
      strftime(newName, 80, "SAVE_%F_%H-%M-%S", timeInfo);
      rename("SAVE", newName);
      return;
   }
}

/*!
  \brief Collective exit on error functions

  If any process(es) have a false success values then the program will
  abort and write out the message to the logfile
  \param success This process' value for successful execution
  \param message Error message to be shown upon failure
  \param comm MPI comm
  \return Returns true if the operation was successful
*/
bool exitOnError(bool success,string message,MPI_Comm comm) {
   int successInt;
   int globalSuccessInt;
   if(success)
      successInt=1;
   else
      successInt=0;
   
   MPI_Allreduce(&successInt,&globalSuccessInt,1,MPI_INT,MPI_MIN,comm);
   
   if(globalSuccessInt==1) {
      return true;
   }
   else{
      logFile << message << endl<<write ;
      exit(1);
   }
}

/*!
 \brief Read cell ID's
 Read in cell ID's from file. Note: Uses the newer version of vlsv parallel reader
 \param file Some vlsv reader with a file open
 \param fileCells Vector in whic to store the cell ids
 \param masterRank The simulation's master rank id (Vlasiator uses 0, which should be the default)
 \param comm MPI comm (MPI_COMM_WORLD should be the default)
*/
bool readCellIds(vlsv::ParallelReader & file,
                 vector<CellID>& fileCells, const int masterRank,MPI_Comm comm){
   // Get info on array containing cell Ids:
   uint64_t arraySize = 0;
   uint64_t vectorSize;
   vlsv::datatype::type dataType;
   uint64_t byteSize;
   list<pair<string,string> > attribs;
   bool success=true;
   int rank;
   MPI_Comm_rank(comm,&rank);
   if (rank==masterRank) {
      const short int readFromFirstIndex = 0;
      //let's let master read cellId's, we anyway have at max ~1e6 cells
      attribs.push_back(make_pair("name","CellID"));
      attribs.push_back(make_pair("mesh","SpatialGrid"));
      if (file.getArrayInfoMaster("VARIABLE",attribs,arraySize,vectorSize,dataType,byteSize) == false) {
         logFile << "(RESTART) ERROR: Failed to read cell ID array info!" << endl << write;
         return false;
      }

      //Make a routine error check:
      if( vectorSize != 1 ) {
         logFile << "(RESTART) ERROR: Bad vectorsize at " << __FILE__ << " " << __LINE__ << endl << write;
         return false;
      }
      
      //   Read cell Ids:
      char* IDbuffer = new char[arraySize*vectorSize*byteSize];
      if (file.readArrayMaster("VARIABLE",attribs,readFromFirstIndex,arraySize,IDbuffer) == false) {
         logFile << "(RESTART) ERROR: Failed to read cell Ids!" << endl << write;
         success = false;
      }
   
   // Convert global Ids into our local DCCRG 64 bit uints
      const uint64_t& numberOfCells = arraySize;
      fileCells.resize(numberOfCells);
      if (dataType == vlsv::datatype::type::UINT && byteSize == 4) {
         uint32_t* ptr = reinterpret_cast<uint32_t*>(IDbuffer);
         //Input cell ids
         for (uint64_t i=0; i<numberOfCells; ++i) {
            const CellID cellID = ptr[i];
            fileCells[i] = cellID;
         }
      } else if (dataType == vlsv::datatype::type::UINT && byteSize == 8) {
         uint64_t* ptr = reinterpret_cast<uint64_t*>(IDbuffer);
         for (uint64_t i=0; i<numberOfCells; ++i) {
            const CellID cellID = ptr[i];
            fileCells[i] = cellID;
         }
      } else {
         logFile << "(RESTART) ERROR: ParallelReader returned an unsupported datatype for cell Ids!" << endl << write;
         success = false;
      }
      delete[] IDbuffer;
   }

   //broadcast cellId's to everybody
   MPI_Bcast(&arraySize,1,MPI_UINT64_T,masterRank,comm);   
   fileCells.resize(arraySize);
   MPI_Bcast(&(fileCells[0]),arraySize,MPI_UINT64_T,masterRank,comm);

   return success;
}

/* Read the total number of velocity blocks per spatial cell in the spatial mesh.
 * The returned value for each cell is a sum of the velocity blocks associated in each particle species. 
 * The value is used to calculate an initial load balance after restart.
 * @param file Some vlsv reader with a file open (can be old or new vlsv reader)
 * @param nBlocks Vector for holding information on cells and the number of blocks in them -- this function saves data here
 * @param masterRank The master rank of this process (Vlasiator uses masterRank = 0 and so it should be the default)
 * @param comm MPI comm
 * @return Returns true if the operation was successful
 @ @see exec_readGrid
*/
bool readNBlocks(vlsv::ParallelReader& file,const std::string& meshName,
                 std::vector<size_t>& nBlocks,int masterRank,MPI_Comm comm) {
   bool success = true;

   // Get info on array containing cell IDs:
   uint64_t arraySize;
   uint64_t vectorSize;
   vlsv::datatype::type dataType;
   uint64_t byteSize;

   // Read mesh bounding box to all processes, the info in bbox contains 
   // the number of spatial cells in the mesh.
   uint64_t bbox[6];
   uint64_t* bbox_ptr = bbox;
   list<pair<string,string> > attribs;
   attribs.push_back(make_pair("mesh",meshName));
   if (file.read("MESH_BBOX",attribs,0,6,bbox_ptr,false) == false) return false;

   // Resize the output vector and init to zero values
   const uint64_t N_spatialCells = bbox[0]*bbox[1]*bbox[2];
   nBlocks.resize(N_spatialCells);
   #pragma omp parallel for
   for (size_t i=0; i<nBlocks.size(); ++i) nBlocks[i] = 0;

   // Note: the input file contains N particle species, which are also 
   // defined in the configuration file. We need to read the BLOCKSPERCELL 
   // array for each species, and sum the values for each spatial cell.
   set<string> speciesNames;
   if (file.getUniqueAttributeValues("BLOCKSPERCELL","name",speciesNames) == false) return false;

   // Iterate over all particle species and read in BLOCKSPERCELL array 
   // to all processes, and add the values to nBlocks
   uint64_t* buffer = new uint64_t[N_spatialCells];
   for (set<string>::const_iterator s=speciesNames.begin(); s!=speciesNames.end(); ++s) {      
      attribs.clear();
      attribs.push_back(make_pair("mesh",meshName));
      attribs.push_back(make_pair("name",*s));
      if (file.getArrayInfo("BLOCKSPERCELL",attribs,arraySize,vectorSize,dataType,byteSize) == false) return false;

      if (file.read("BLOCKSPERCELL",attribs,0,arraySize,buffer) == false) {
         delete [] buffer; buffer = NULL;
         return false;
      }

      #pragma omp parallel for
      for (size_t i=0; i<N_spatialCells; ++i) {
         nBlocks[i] += buffer[i];
      }
   }
   delete [] buffer; buffer = NULL;
   return success;
}

//Outputs the velocity block indices of some given block into indices
//Input:
//[0] cellStruct -- some cell structure that has been constructed properly
//[1] block -- some velocity block id
//Output:
//[0] indices -- the array where to store the indices

/*! Outputs given block's velocity min coordinates (the corner of the block) in blockCoordinates
 \param block The block's id
 \param blockCoordinates An empty array where to store the block coordinates
 \sa readBlockData
 */
void getVelocityBlockCoordinates(const vmesh::GlobalID& block, std::array<Real, 3>& blockCoordinates ) {
#warning DEPRECATED
   cerr << "restart disabled" << endl;
   exit(1);
   /*
   //Get indices:
   std::array<vmesh::LocalID, 3> blockIndices;
   blockIndices[0] = block % P::vxblocks_ini;
   blockIndices[1] = (block / P::vxblocks_ini) % P::vyblocks_ini;
   blockIndices[2] = block / (P::vxblocks_ini * P::vyblocks_ini);
   //Store the coordinates:
   blockCoordinates[0] = P::vxmin + ((P::vxmax - P::vxmin) / P::vxblocks_ini) * blockIndices[0];
   blockCoordinates[1] = P::vymin + ((P::vymax - P::vymin) / P::vyblocks_ini) * blockIndices[1];
   blockCoordinates[2] = P::vzmin + ((P::vzmax - P::vzmin) / P::vzblocks_ini) * blockIndices[2];
   return;*/
}

/** Read velocity block mesh data and distribution function data belonging to this process 
 * for the given particle species. This function must be called simultaneously by all processes.
 * @param file VLSV reader with input file open.
 * @param spatMeshName Name of the spatial mesh.
 * @param fileCells List of all spatial cell IDs.
 * @param localCellStartOffset The offset from which to start reading cells.
 * @param localCells How many spatial cells after the offset to read.
 * @param blocksPerCell Number of velocity blocks for this particle species in each spatial cell belonging to this process.
 * @param localBlockStartOffset Offset into velocity block data arrays from which to start reading data.
 * @param localBlocks Number of velocity blocks for this species assigned to this process.
 * @param mpiGrid Parallel grid library.
 * @param popID ID of the particle species who's data is to be read.
 * @return If true, velocity block data was read successfully.*/
template <typename fileReal>
bool _readBlockData(
   vlsv::ParallelReader & file,
   const std::string& spatMeshName,
   const std::vector<uint64_t>& fileCells,
   const uint64_t localCellStartOffset,
   const uint64_t localCells,
   const vmesh::LocalID* blocksPerCell,
   const uint64_t localBlockStartOffset,
   const uint64_t localBlocks,
   dccrg::Dccrg<SpatialCell,dccrg::Cartesian_Geometry>& mpiGrid,
   const int& popID
) {   
   uint64_t arraySize;
   uint64_t avgVectorSize;
   uint64_t cellParamsVectorSize;
   vlsv::datatype::type dataType;
   uint64_t byteSize;
   list<pair<string,string> > avgAttribs;
   bool success=true;
   const string popName = getObjectWrapper().particleSpecies[popID].name;
   const string tagName = "BLOCKIDS";
   
   avgAttribs.push_back(make_pair("mesh",spatMeshName));
   avgAttribs.push_back(make_pair("name",popName));
   
    //Get block id array info and store them into blockIdAttribs, lockIdByteSize, blockIdDataType, blockIdVectorSize
  list<pair<string,string> > blockIdAttribs;
  uint64_t blockIdVectorSize, blockIdByteSize;
  vlsv::datatype::type blockIdDataType;
  blockIdAttribs.push_back( make_pair("mesh", spatMeshName));
  blockIdAttribs.push_back( make_pair("name", popName));
  if (file.getArrayInfo("BLOCKIDS",blockIdAttribs,arraySize,blockIdVectorSize,blockIdDataType,blockIdByteSize) == false ){
    logFile << "(RESTART) ERROR: Failed to read BLOCKCOORDINATES array info " << endl << write;
    return false;
  }
  if(file.getArrayInfo("BLOCKVARIABLE",avgAttribs,arraySize,avgVectorSize,dataType,byteSize) == false ){
    logFile << "(RESTART) ERROR: Failed to read BLOCKVARIABLE array info " << endl << write;
    return false;
  }

   //Some routine error checks:
   if( avgVectorSize!=WID3 ){
      logFile << "(RESTART) ERROR: Blocksize does not match in restart file " << endl << write;
      return false;
   }
   if( byteSize != sizeof(fileReal) ) {
      logFile << "(RESTART) ERROR: Bad avgs bytesize at " << __FILE__ << " " << __LINE__ << endl << write;
      return false;
   }
   
   if( blockIdByteSize != sizeof(vmesh::GlobalID)) {
      logFile << "(RESTART) ERROR: BlockID data size does not match " << __FILE__ << " " << __LINE__ << endl << write;
      return false;
   }

   fileReal* avgBuffer = new fileReal[avgVectorSize * localBlocks]; //avgs data for all cells
   vmesh::GlobalID * blockIdBuffer = new vmesh::GlobalID[blockIdVectorSize * localBlocks]; //blockids of all cells

   //Read block ids and data
   if (file.readArray("BLOCKIDS", blockIdAttribs, localBlockStartOffset, localBlocks, (char*)blockIdBuffer ) == false) {
      cerr << "ERROR, failed to read BLOCKIDS in " << __FILE__ << ":" << __LINE__ << endl;
      success = false;
   }
   if (file.readArray("BLOCKVARIABLE", avgAttribs, localBlockStartOffset, localBlocks, (char*)avgBuffer) == false) {
      cerr << "ERROR, failed to read BLOCKVARIABLE in " << __FILE__ << ":" << __LINE__ << endl;
      success = false;
   }
   
   uint64_t blockBufferOffset=0;
   //Go through all spatial cells     
   vector<vmesh::GlobalID> blockIdsInCell; //blockIds in a particular cell, temporary usage
   for(uint64_t i=0; i<localCells; i++) {
      CellID cell = fileCells[localCellStartOffset + i]; //spatial cell id 
      vmesh::LocalID nBlocksInCell = blocksPerCell[i];
      //copy blocks in this cell to vector blockIdsInCell, size of read in data has been checked earlier
      blockIdsInCell.reserve(nBlocksInCell);
      blockIdsInCell.assign(blockIdBuffer + blockBufferOffset, blockIdBuffer + blockBufferOffset + nBlocksInCell);
      mpiGrid[cell]->add_velocity_blocks(blockIdsInCell,popID); //allocate space for all blocks and create them
      //copy avgs data, here a conversion may happen between float and double
      Realf *cellBlockData=mpiGrid[cell]->get_data(popID);
      for(uint64_t i = 0; i< WID3 * nBlocksInCell ; i++){
         cellBlockData[i] =  avgBuffer[blockBufferOffset*WID3 + i];
      }
      blockBufferOffset += nBlocksInCell; //jump to location of next local cell
   }

   delete[] avgBuffer;
   delete[] blockIdBuffer;
   return success;
}

/** Read velocity block data of all existing particle species.
 * @param file VLSV reader.
 * @param meshName Name of the spatial mesh.
 * @param fileCells Vector containing spatial cell IDs.
 * @param localCellStartOffset Offset into fileCells, determines where the cells belonging 
 * to this process start.
 * @param localCells Number of spatial cells assigned to this process.
 * @param mpiGrid Parallel grid library.
 * @return If true, velocity block data was read successfully.*/
bool readBlockData(
        vlsv::ParallelReader& file,
        const string& meshName,
        const vector<CellID>& fileCells,
        const uint64_t localCellStartOffset,
        const uint64_t localCells,
        dccrg::Dccrg<SpatialCell,dccrg::Cartesian_Geometry>& mpiGrid
   ) {
   bool success = true;

   const uint64_t bytesReadStart = file.getBytesRead();
   int N_processes;
   MPI_Comm_size(MPI_COMM_WORLD,&N_processes);

   uint64_t arraySize;
   uint64_t vectorSize;
   vlsv::datatype::type dataType;
   uint64_t byteSize;
   uint64_t* offsetArray = new uint64_t[N_processes];

   for (int popID=0; popID<getObjectWrapper().particleSpecies.size(); ++popID) {
      const string& popName = getObjectWrapper().particleSpecies[popID].name;

      list<pair<string,string> > attribs;
      attribs.push_back(make_pair("mesh",meshName));
      attribs.push_back(make_pair("name",popName));      

      // In restart files each spatial cell has an entry in CELLSWITHBLOCKS. 
      // Each process calculates how many velocity blocks it has for this species.
      vmesh::LocalID* blocksPerCell = NULL;
      
      if (file.read("BLOCKSPERCELL",attribs,localCellStartOffset,localCells,blocksPerCell,true) == false) {
         logFile << "(RESTART) ERROR: Failed to read BLOCKSPERCELL at " << __FILE__ << ":" << __LINE__ << endl << write;
         success = false;
      }

      // Count how many velocity blocks this process gets
      uint64_t blockSum = 0;
      for (uint64_t i=0; i<localCells; ++i){
         blockSum += blocksPerCell[i];
      }
      
      // Gather all block sums to master process who will them broadcast 
      // the values to everyone
      MPI_Allgather(&blockSum,1,MPI_Type<uint64_t>(),offsetArray,1,MPI_Type<uint64_t>(),MPI_COMM_WORLD);      
      
      // Calculate the offset from which this process starts reading block data
      uint64_t myOffset = 0;
      for (uint64_t i=0; i<mpiGrid.get_rank(); ++i) myOffset += offsetArray[i];
      
      if (file.getArrayInfo("BLOCKVARIABLE",attribs,arraySize,vectorSize,dataType,byteSize) == false) {
         logFile << "(RESTART)  ERROR: Failed to read BLOCKVARIABLE INFO" << endl << write;
         return false;
      }

      // Call _readBlockData
      if (dataType == vlsv::datatype::type::FLOAT) {
         switch (byteSize) {
            case sizeof(double):
               if (_readBlockData<double>(file,meshName,fileCells,localCellStartOffset,localCells,blocksPerCell,
                                          myOffset,blockSum,mpiGrid,popID) == false) success = false;
               break;
            case sizeof(float):
               if (_readBlockData<float>(file,meshName,fileCells,localCellStartOffset,localCells,blocksPerCell,
                                         myOffset,blockSum,mpiGrid,popID) == false) success = false;
               break;
         }
      } else if (dataType == vlsv::datatype::type::UINT) {
         switch (byteSize) {
            case sizeof(uint32_t):
               if (_readBlockData<uint32_t>(file,meshName,fileCells,localCellStartOffset,localCells,blocksPerCell,
                                            myOffset,blockSum,mpiGrid,popID) == false) success = false;
               break;
            case sizeof(uint64_t):
               if (_readBlockData<uint64_t>(file,meshName,fileCells,localCellStartOffset,localCells,blocksPerCell,
                                            myOffset,blockSum,mpiGrid,popID) == false) success = false;
               break;
         }
      } else if (dataType == vlsv::datatype::type::INT) {
         switch (byteSize) {
            case sizeof(int32_t):
               if (_readBlockData<int32_t>(file,meshName,fileCells,localCellStartOffset,localCells,blocksPerCell,
                                           myOffset,blockSum,mpiGrid,popID) == false) success = false;
               break;
            case sizeof(int64_t):
               if (_readBlockData<int64_t>(file,meshName,fileCells,localCellStartOffset,localCells,blocksPerCell,
                                           myOffset,blockSum,mpiGrid,popID) == false) success = false;
               break;
         }
      } else {
         logFile << "(RESTART) ERROR: Failed to read data type at readCellParamsVariable" << endl << write;
         success = false;
      }
      delete [] blocksPerCell; blocksPerCell = NULL;
   } // for-loop over particle species

   delete [] offsetArray; offsetArray = NULL;
   
   const uint64_t bytesReadEnd = file.getBytesRead() - bytesReadStart;
   logFile << "Velocity meshes and data read, approximate data rate is ";
   logFile << vlsv::printDataRate(bytesReadEnd,file.getReadTime()) << endl << write;

   return success;
}

/*! Reads cell parameters from the file and saves them in the right place in mpiGrid
 \param file Some parallel vlsv reader with a file open
 \param fileCells List of all cell ids
 \param localCellStartOffset Offset in the fileCells list for this process ( calculated so that the amount of blocks is distributed somewhat evenly between processes)
 \param localCells The amount of cells to read in this process after localCellStartOffset
 \param cellParamsIndex The parameter of the cell index e.g. CellParams::RHOM
 \param expectedVectorSize The amount of elements in the parameter (parameter can be a scalar or a vector of size N)
 \param mpiGrid Vlasiator's grid (the parameters are saved here)
 \return Returns true if the operation is successful
 */
template <typename fileReal>
static bool _readCellParamsVariable(
                                    vlsv::ParallelReader& file,
                                    const vector<uint64_t>& fileCells,
                                    const uint64_t localCellStartOffset,
                                    const uint64_t localCells,
                                    const string& variableName,
                                    const size_t cellParamsIndex,
                                    const size_t expectedVectorSize,
                                    dccrg::Dccrg<SpatialCell,dccrg::Cartesian_Geometry>& mpiGrid
                                   ) {
   uint64_t arraySize;
   uint64_t vectorSize;
   vlsv::datatype::type dataType;
   uint64_t byteSize;
   list<pair<string,string> > attribs;
   fileReal *buffer;
   bool success=true;
   
   attribs.push_back(make_pair("name",variableName));
   attribs.push_back(make_pair("mesh","SpatialGrid"));
   
   if (file.getArrayInfo("VARIABLE",attribs,arraySize,vectorSize,dataType,byteSize) == false) {
      logFile << "(RESTART)  ERROR: Failed to read " << endl << write;
      return false;
   }

   if(vectorSize!=expectedVectorSize){
      logFile << "(RESTART)  vectorsize wrong " << endl << write;
      return false;
   }
   
   buffer=new fileReal[vectorSize*localCells];
   if(file.readArray("VARIABLE",attribs,localCellStartOffset,localCells,(char *)buffer) == false ) {
      logFile << "(RESTART)  ERROR: Failed to read " << variableName << endl << write;
      return false;
   }
   
   for(uint i=0;i<localCells;i++){
     uint cell=fileCells[localCellStartOffset+i];
     for(uint j=0;j<vectorSize;j++){
        mpiGrid[cell]->parameters[cellParamsIndex+j]=buffer[i*vectorSize+j];
     }
   }
   
   delete[] buffer;
   return success;
}

/*! Reads cell parameters from the file and saves them in the right place in mpiGrid
 \param file Some parallel vlsv reader with a file open
 \param fileCells List of all cell ids
 \param localCellStartOffset Offset in the fileCells list for this process ( calculated so that the amount of blocks is distributed somewhat evenly between processes)
 \param localCells The amount of cells to read in this process after localCellStartOffset
 \param cellParamsIndex The parameter of the cell index e.g. CellParams::RHOM
 \param expectedVectorSize The amount of elements in the parameter (parameter can be a scalar or a vector of size N)
 \param mpiGrid Vlasiator's grid (the parameters are saved here)
 \return Returns true if the operation is successful
 */
bool readCellParamsVariable(
   vlsv::ParallelReader& file,
   const vector<CellID>& fileCells,
   const uint64_t localCellStartOffset,
   const uint64_t localCells,
   const string& variableName,
   const size_t cellParamsIndex,
   const size_t expectedVectorSize,
   dccrg::Dccrg<SpatialCell,dccrg::Cartesian_Geometry>& mpiGrid
) {
   uint64_t arraySize;
   uint64_t vectorSize;
   vlsv::datatype::type dataType;
   uint64_t byteSize;
   list<pair<string,string> > attribs;
   
   attribs.push_back(make_pair("name",variableName));
   attribs.push_back(make_pair("mesh","SpatialGrid"));
   
   if (file.getArrayInfo("VARIABLE",attribs,arraySize,vectorSize,dataType,byteSize) == false) {
      logFile << "(RESTART)  ERROR: Failed to read " << endl << write;
      return false;
   }

   // Call _readCellParamsVariable
   if( dataType == vlsv::datatype::type::FLOAT ) {
      switch (byteSize) {
         case sizeof(double):
            return _readCellParamsVariable<double>( file, fileCells, localCellStartOffset, localCells, variableName, cellParamsIndex, expectedVectorSize, mpiGrid );
            break;
         case sizeof(float):
            return _readCellParamsVariable<float>( file, fileCells, localCellStartOffset, localCells, variableName, cellParamsIndex, expectedVectorSize, mpiGrid );
            break;
      }
   } else if( dataType == vlsv::datatype::type::UINT ) {
      switch (byteSize) {

         case sizeof(uint32_t):
            return _readCellParamsVariable<uint32_t>( file, fileCells, localCellStartOffset, localCells, variableName, cellParamsIndex, expectedVectorSize, mpiGrid );
            break;
         case sizeof(uint64_t):
            return _readCellParamsVariable<uint64_t>( file, fileCells, localCellStartOffset, localCells, variableName, cellParamsIndex, expectedVectorSize, mpiGrid );
            break;
      }
   } else if( dataType == vlsv::datatype::type::INT ) {
      switch (byteSize) {
         case sizeof(int32_t):
            return _readCellParamsVariable<int32_t>( file, fileCells, localCellStartOffset, localCells, variableName, cellParamsIndex, expectedVectorSize, mpiGrid );
            break;
         case sizeof(int64_t):
            return _readCellParamsVariable<int64_t>( file, fileCells, localCellStartOffset, localCells, variableName, cellParamsIndex, expectedVectorSize, mpiGrid );
            break;
      }
   } else {
      logFile << "(RESTART)  ERROR: Failed to read data type at readCellParamsVariable" << endl << write;
      return false;
   }
}

/*! A function for reading parameters, e.g., 'timestep'.
 \param file VLSV parallel reader with a file open.
 \param name Name of the parameter.
 \param value Variable in which to store the scalar variable (double, float, int .. ).
 \param masterRank The master process' id (Vlasiator uses 0 so this should equal 0 by default).
 \param comm MPI comm (MPI_COMM_WORLD should be the default).
 \return Returns true if the operation is successful. */
template <typename T>
bool readScalarParameter(vlsv::ParallelReader& file,string name,T& value,int masterRank,MPI_Comm comm) {
   if (file.readParameter(name,value) == false) {
      logFile << "(RESTART) ERROR: Failed to read parameter '" << name << "' value in ";
      logFile << __FILE__ << ":" << __LINE__ << endl << write;
      return false;
   }
   return true;
}

/*! A function for checking the scalar parameter
 \param file Some parallel vlsv reader with a file open
 \param name Name of the parameter
 \param correctValue The correct value of the parameter to compare to
 \param masterRank The master process' id (Vlasiator uses 0 so this should be 0 by default)
 \param comm MPI comm (Default should be MPI_COMM_WORLD)
 \return Returns true if the operation is successful
 */
template <typename T>
bool checkScalarParameter(vlsv::ParallelReader& file,const string& name,T correctValue,int masterRank,MPI_Comm comm) {
   T value;
   readScalarParameter(file,name,value,masterRank,comm);
   if (value != correctValue){
      ostringstream s;
      s << "(RESTART) Parameter " << name << " has mismatching value.";
      s << " CFG value = " << correctValue;
      s << " Restart file value = " << value;
      exitOnError(false,s.str(),MPI_COMM_WORLD);
      return false;
   }
   else{
      exitOnError(true,"",MPI_COMM_WORLD);
      return true;
   }
}

/*!
\brief Read in state from a vlsv file in order to restart simulations
\param mpiGrid Vlasiator's grid
\param name Name of the restart file e.g. "restart.00052.vlsv"
 \return Returns true if the operation was successful
 \sa readGrid
 */
bool exec_readGrid(dccrg::Dccrg<SpatialCell,dccrg::Cartesian_Geometry>& mpiGrid,
                   const std::string& name) {
   vector<CellID> fileCells; /*< CellIds for all cells in file*/
   vector<size_t> nBlocks;/*< Number of blocks for all cells in file*/
   bool success=true;
   int myRank,processes;

#warning Spatial grid name hard-coded here
   const string meshName = "SpatialGrid";
   
   // Attempt to open VLSV file for reading:
   MPI_Comm_rank(MPI_COMM_WORLD,&myRank);
   MPI_Comm_size(MPI_COMM_WORLD,&processes);

   phiprof::start("readGrid");

   vlsv::ParallelReader file;
   MPI_Info mpiInfo = MPI_INFO_NULL;

   if (file.open(name,MPI_COMM_WORLD,MASTER_RANK,mpiInfo) == false) {
      success=false;
   }
   exitOnError(success,"(RESTART) Could not open file",MPI_COMM_WORLD);

   // Around May 2015 time was renamed from "t" to "time", we try to read both, 
   // new way is read first
   if (readScalarParameter(file,"time",P::t,MASTER_RANK,MPI_COMM_WORLD) == false)
     if (readScalarParameter(file,"t", P::t,MASTER_RANK,MPI_COMM_WORLD) == false) 
       success=false;
   P::t_min=P::t;

   // Around May 2015 timestep was renamed from "tstep" to "timestep", we to read
   // both, new way is read first
   if (readScalarParameter(file,"timestep",P::tstep,MASTER_RANK,MPI_COMM_WORLD) == false)
     if (readScalarParameter(file,"tstep", P::tstep,MASTER_RANK,MPI_COMM_WORLD) ==false) 
       success = false;
   P::tstep_min=P::tstep;

   if(readScalarParameter(file,"dt",P::dt,MASTER_RANK,MPI_COMM_WORLD) ==false) success=false;

   if(readScalarParameter(file,"fieldSolverSubcycles",P::fieldSolverSubcycles,MASTER_RANK,MPI_COMM_WORLD) ==false) {
      // Legacy restarts do not have this field, it "should" be safe for one or two steps...
      P::fieldSolverSubcycles = 1.0;
      cout << " No P::fieldSolverSubcycles found in restart, setting 1." << endl;
   }
   MPI_Bcast(&(P::fieldSolverSubcycles),1,MPI_Type<Real>(),MASTER_RANK,MPI_COMM_WORLD);
   



   checkScalarParameter(file,"xmin",P::xmin,MASTER_RANK,MPI_COMM_WORLD);
   checkScalarParameter(file,"ymin",P::ymin,MASTER_RANK,MPI_COMM_WORLD);
   checkScalarParameter(file,"zmin",P::zmin,MASTER_RANK,MPI_COMM_WORLD);
   checkScalarParameter(file,"xmax",P::xmax,MASTER_RANK,MPI_COMM_WORLD);
   checkScalarParameter(file,"ymax",P::ymax,MASTER_RANK,MPI_COMM_WORLD);
   checkScalarParameter(file,"zmax",P::zmax,MASTER_RANK,MPI_COMM_WORLD);
   checkScalarParameter(file,"xcells_ini",P::xcells_ini,MASTER_RANK,MPI_COMM_WORLD);
   checkScalarParameter(file,"ycells_ini",P::ycells_ini,MASTER_RANK,MPI_COMM_WORLD);
   checkScalarParameter(file,"zcells_ini",P::zcells_ini,MASTER_RANK,MPI_COMM_WORLD);

   phiprof::start("readDatalayout");
   if (success == true) success = readCellIds(file,fileCells,MASTER_RANK,MPI_COMM_WORLD);

   // Check that the cellID lists are identical in file and grid
   if (myRank==0){
      vector<CellID> allGridCells=mpiGrid.get_all_cells();
      if (fileCells.size() != allGridCells.size()){
         success=false;
      }
   }
   
   exitOnError(success,"(RESTART) Wrong number of cells in restart file",MPI_COMM_WORLD);

   // Read the total number of velocity blocks in each spatial cell.
   // Note that this is a sum over all existing particle species.
   if (success == true) {
      success = readNBlocks(file,meshName,nBlocks,MASTER_RANK,MPI_COMM_WORLD);
   }

   //make sure all cells are empty, we will anyway overwrite everything and 
   // in that case moving cells is easier...
     {
        const vector<CellID>& gridCells = getLocalCells();
        for (size_t i=0; i<gridCells.size(); i++) {
           for (int popID=0; popID<getObjectWrapper().particleSpecies.size(); ++popID)
             mpiGrid[gridCells[i]]->clear(popID);
        }
     }

   uint64_t totalNumberOfBlocks=0;
   unsigned int numberOfBlocksPerProcess;
   for(uint i=0; i<nBlocks.size(); ++i){
      totalNumberOfBlocks += nBlocks[i];
   }
   numberOfBlocksPerProcess= 1 + totalNumberOfBlocks/processes;

   uint64_t localCellStartOffset=0; // This is where local cells start in file-list after migration.
   uint64_t localCells=0;
   uint64_t numberOfBlocksCount=0;
   
   // Pin local cells to remote processes, we try to balance number of blocks so that 
   // each process has the same amount of blocks, more or less.
   for (size_t i=0; i<fileCells.size(); ++i) {
      numberOfBlocksCount += nBlocks[i];
      int newCellProcess = numberOfBlocksCount/numberOfBlocksPerProcess;
      if (newCellProcess == myRank) {
         if (localCells == 0)
            localCellStartOffset=i; //here local cells start
         ++localCells;
      }
      if (mpiGrid.is_local(fileCells[i])) {
         mpiGrid.pin(fileCells[i],newCellProcess);
      }
   }
   
   //Do initial load balance based on pins. Need to transfer at least sysboundaryflags
   SpatialCell::set_mpi_transfer_type(Transfer::ALL_SPATIAL_DATA);
   mpiGrid.balance_load(false);

   //update list of local gridcells
   recalculateLocalCellsCache();
   //getObjectWrapper().meshData.reallocate();

   //get new list of local gridcells
   const vector<CellID>& gridCells = getLocalCells();

   // Unpin cells, otherwise we will never change this initial bad balance
   for (size_t i=0; i<gridCells.size(); ++i) {
      mpiGrid.unpin(gridCells[i]);
   }

   // Check for errors, has migration succeeded
   if (localCells != gridCells.size() ) {
      success=false;
   }
   if (success == true) {
      for (uint64_t i=localCellStartOffset; i<localCellStartOffset+localCells; ++i) {
         if(mpiGrid.is_local(fileCells[i]) == false) success = false;
      }
   }

   exitOnError(success,"(RESTART) Cell migration failed",MPI_COMM_WORLD);

   // Set cell coordinates based on cfg (mpigrid) information
   for (size_t i=0; i<gridCells.size(); ++i) {
      array<double, 3> cell_min = mpiGrid.geometry.get_min(gridCells[i]);
      array<double, 3> cell_length = mpiGrid.geometry.get_length(gridCells[i]);

      mpiGrid[gridCells[i]]->parameters[CellParams::XCRD] = cell_min[0];
      mpiGrid[gridCells[i]]->parameters[CellParams::YCRD] = cell_min[1];
      mpiGrid[gridCells[i]]->parameters[CellParams::ZCRD] = cell_min[2];
      mpiGrid[gridCells[i]]->parameters[CellParams::DX  ] = cell_length[0];
      mpiGrid[gridCells[i]]->parameters[CellParams::DY  ] = cell_length[1];
      mpiGrid[gridCells[i]]->parameters[CellParams::DZ  ] = cell_length[2];
   }

   // Where local data start in the blocklists
   //uint64_t localBlocks=0;
   //for(uint64_t i=localCellStartOffset; i<localCellStartOffset+localCells; ++i) {
   //  localBlocks += nBlocks[i];
   //}
   phiprof::stop("readDatalayout");

   //todo, check file datatype, and do not just use double
   phiprof::start("readCellParameters");
   if(success) { success=readCellParamsVariable(file,fileCells,localCellStartOffset,localCells,"perturbed_B",CellParams::PERBX,3,mpiGrid); }
// Backround B has to be set, there are also the derivatives that should be written/read if we wanted to only read in background field
   if(success) { success=readCellParamsVariable(file,fileCells,localCellStartOffset,localCells,"moments",CellParams::RHOM,4,mpiGrid); }
   if(success) { success=readCellParamsVariable(file,fileCells,localCellStartOffset,localCells,"moments_dt2",CellParams::RHOM_DT2,4,mpiGrid); }
   if(success) { success=readCellParamsVariable(file,fileCells,localCellStartOffset,localCells,"moments_r",CellParams::RHOM_R,4,mpiGrid); }
   if(success) { success=readCellParamsVariable(file,fileCells,localCellStartOffset,localCells,"moments_v",CellParams::RHOM_V,4,mpiGrid); }
   if(success) { success=readCellParamsVariable(file,fileCells,localCellStartOffset,localCells,"pressure",CellParams::P_11,3,mpiGrid); }
   if(success) { success=readCellParamsVariable(file,fileCells,localCellStartOffset,localCells,"pressure_dt2",CellParams::P_11_DT2,3,mpiGrid); }
   if(success) { success=readCellParamsVariable(file,fileCells,localCellStartOffset,localCells,"pressure_r",CellParams::P_11_R,3,mpiGrid); }
   if(success) { success=readCellParamsVariable(file,fileCells,localCellStartOffset,localCells,"pressure_v",CellParams::P_11_V,3,mpiGrid); }
   if(success) { success=readCellParamsVariable(file,fileCells,localCellStartOffset,localCells,"LB_weight",CellParams::LBWEIGHTCOUNTER,1,mpiGrid); }
   if(success) { success=readCellParamsVariable(file,fileCells,localCellStartOffset,localCells,"max_v_dt",CellParams::MAXVDT,1,mpiGrid); }
   if(success) { success=readCellParamsVariable(file,fileCells,localCellStartOffset,localCells,"max_r_dt",CellParams::MAXRDT,1,mpiGrid); }
   if(success) { success=readCellParamsVariable(file,fileCells,localCellStartOffset,localCells,"max_fields_dt",CellParams::MAXFDT,1,mpiGrid); }
// Backround B has to be set, there are also the derivatives that should be written/read if we wanted to only read in background field
   phiprof::stop("readCellParameters");

   phiprof::start("readBlockData");
   if (success == true) {
      success = readBlockData(file,meshName,fileCells,localCellStartOffset,localCells,mpiGrid); 
   }
   phiprof::stop("readBlockData");

   success = file.close();
   phiprof::stop("readGrid");

   exitOnError(success,"(RESTART) Other failure",MPI_COMM_WORLD);
   return success;
}

//FIXME, readGrid has no support for checking or converting endianness
/*!
\brief Read in state from a vlsv file in order to restart simulations
\param mpiGrid Vlasiator's grid
\param name Name of the restart file e.g. "restart.00052.vlsv"
*/
bool readGrid(dccrg::Dccrg<SpatialCell,dccrg::Cartesian_Geometry>& mpiGrid,
              const std::string& name){
   //Check the vlsv version from the file:
   return exec_readGrid(mpiGrid,name);
}
