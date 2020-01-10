# Process Tree File System
## Install the kernel
- `cd ROOT_OF_THIS_REPOSITORY/linux`
- `make w4118_defconfig`
- `make -j4`
- `sudo make modules_install && sudo make install`
- `reboot`
## Mount the Process Tree File System
- `cd /`
- `sudo mkdir ptreefs`
- `sudo mount -t ptreefs ptreefs /ptreefs`
## Output the process hierarchy
- `cd /ptreefs`
- `ls -R`
## Test the Process Tree File System
- `cd ROOT_OF_THIS_REPOSITORY/test`
- `make`
- `./ptreeps`
- This test program will 1. Execute `ls -R`. 2. Create 20 child processes and execute `ls -R`. You will find these processes in the output. 3. Terminate these 20 child processes and execute `ls -R`. You will find these processes are not in the output anymore.
