#!/bin/bash  
nodes=1   #Has to be identical to above! $( qstat -f $PBS_JOBID | grep   Resource_List.nodes | gawk '{print $3}' )      

ht=1      #hyper threads per physical core
t=1       #threads per process                         
exec="../vlasiator"
#t=1                   #threads per process

#No idea how many cores we have available on travis. On my laptop I have 4.
cores_per_node=1
#Change PBS parameters above + the ones here
total_units=$(echo $nodes $cores_per_node $ht | gawk '{print $1*$2*$3}')
units_per_node=$(echo $cores_per_node $ht | gawk '{print $1*$2}')
tasks=$(echo $total_units $t  | gawk '{print $1/$2}')
tasks_per_node=$(echo $units_per_node $t  | gawk '{print $1/$2}')
export OMP_NUM_THREADS=$t

umask 007
# Launch the OpenMP job to the allocated compute node
echo "Running $exec on $tasks mpi tasks, with $t threads per task on $nodes nodes ($ht threads per physical core)"
#command for running stuff
run_command="srun -n $tasks $tasks_per_node -d $OMP_NUM_THREADS -j $ht"
small_run_command="srun -n 1"
run_command_tools="srun -n 1"

#If 1, the reference vlsv files are generated
# if 0 then we check the v1
create_verification_files=1

#folder for all reference data 
reference_dir="/projects/hama2717/vlasiator/testpackage/"
#compare agains which revision. This can be a proper version string, or "current", which should be a symlink to the
#proper most recent one
reference_revision="current"

# Define test
source small_test_definitions.sh
wait
# Run tests
source run_tests.sh
