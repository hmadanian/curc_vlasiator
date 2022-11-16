#!/bin/bash
# This file is part of Vlasiator.

#module load slurm/alpine
#acompile

module load gcc/11.2.0
module load openmpi/4.1.1
module load boost/1.78.0
module load eigen/3.4.0
module load papi/5.5.1



export VLASIATOR_ARCH=alpine
make clean
make -j 12

make clean 
make -j 12 tools

make clean
make testpackage -j 10

cd ./testpackage
small_test_alpine.sh



