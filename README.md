# High-Performance TLB Covert Channels
This repo contains the source code from my [bachelor's thesis](thesis.pdf) written at the
Karlsruhe Institute of Technology (KIT) with the [ITEC Operating Systems Group](https://os.itec.kit.edu/).
The presentation slides can be found [here](slides.pdf).
If you have further questions, feel free to hit me up.

#### Disclaimer
The code is provided "as is" for educational purposes only.
I am not responsible for any damage or legal issues caused through use of this code.

## Abstract
The ongoing global trend towards large-scale cloud virtualization raises concerns on how secure these systems are. Previous work has shown how shared hardware resources (e.g., caches) can be exploited to break isolation between processes and virtual machines. With [TLBleed](https://www.usenix.org/conference/usenixsecurity18/presentation/gras), the Translation Lookaside Buffer (TLB) was identified as a new attack vector which is immune to state-of-the-art security mechanisms such as Intel’s Cache Allocation Technology (CAT).

Given the general feasibility of TLB-based covert channels, we aim to considerably increase the performance of TLB-based covert channels in terms of channel bit rate and reliability, thereby demonstrating that holistic techniques for microarchitectural resource partitioning are needed. Therefore, we design a two-layer communication protocol capable of dealing with the issues of synchronization and noise due to concurrently running processes. Furthermore, we present a novel approach to monitor TLB entries by leveraging the accessed bit in page table entries. We are able to achieve error-free transmissions at bit rates of up to 200 kB/s in a Linux KVM environment running on current Intel hardware.

## Instructions
0. Install build dependencies: [Argp](https://www.gnu.org/software/libc/manual/html_node/Argp.html), [libfec](https://github.com/quiet/libfec).
1. Prepare two core co-resident virtual machines (e.g., pin them to adjacent hyperthreads via the `cpuset` attribute).
2. Compile (`make`) and load the pteaccess kernel module on the attacker VM.
3. Compile (`make CFLAGS="-D..."`) the receiver and sender program with the desired [flags](src/Makefile).
4. Execute the receiver program on the attacker VM. Use `./receiver --help` to list all available cli args.
5. Execute the sender program on the victim VM. Use `./sender --help` to list all available cli args.
6. Think about solutions to this security issue and write another bachelor's thesis.