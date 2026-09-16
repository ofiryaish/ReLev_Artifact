# ReLev_Artifact
A runtime-reconfigurable Levenshtein Automata Overlay for accelerating edit distance computations on an Alveo FPGA.
This repository contains all of the code you will need to duplicate our ReLev results and evaluate them against
Hyperscan.

# Some Prerequisites
This project targets the AMD Vitis Platform, and has been tested on the AMD Alveo U280. The other Alveo cards can also be used, but make sure that the HBM_connectivity.cfg file is updated with Global Memory mappings; the U280 has HBM2. Other cards do not, so target DDR instead.

This code can be ported to the new Versal platform and run on the V80, but this requires copying over the HLS components from Vitis to the Alveo Versal Example Design (AVED).

All experiments were performed on a Linux workstation with the following configuration:

## Hardware
- AMD Ryzen Threadripper PRO 5955WX 16-Cores (any x86 CPU should be fine)
- 512GB DRAM (64GB should be sufficient)
- AMD Alveo U280 FPGA

## Software
- Ubuntu 22.04.05 with the Linux 5.15 kernel
- GCC version 11.4
- Vitis HLS version 2023.2

# To reproduce Hyperscan Results:
1. Install Hyperscan from https://github.com/intel/hyperscan; IMPORTANT: this can only be done on an x86 machine; also make sure all dependencies are installed for hsbench. We will use Hyperscan's benchmarking program (hsbench) for comparison.
2. Update the location of Hyperscan in scripts/relev_experiments.sh to reflect your install location

# To reproduce FPGA Results:
1. Make sure the Makefile variables are correctly set, especially the DEVICE variable.
2. Unzip chr1.txt.gz - `cd input; gunzip chr1.txt.gz`
3. Unzip chr1.db.gz - `cd input; gunzip chr1.db.gz`
4. Compile the relev overlay - `make automata && make xclbin`
5. Compile the host executable - `make host`
6. Run the reproducibility script - `cd scripts; ./relev_experiments.sh`

# AutoFFinder JNI Integration

The `autoffinder-jni-integration` branch includes a JNI host interface for
running ReLev directly from AutoFFinder's Java post-processing stage. It also
resets the kernel's persistent position and configuration counters at the
start of every invocation, allowing one loaded system to process successive
chromosome and strand files.

## Hardware-specific requirements

The included FPGA design targets the **AMD Alveo U280**. Both of the following
files are U280-specific:

- `fpga/Makefile` selects the
  `xilinx_u280_gen3x16_xdma_1_202211_1` platform.
- `fpga/HBM_connectivity.cfg` connects the input and output ports to U280 HBM
  banks.

No prebuilt `.xclbin` is included on this branch. The kernel and bitstream must
be built from source so that they contain the repeated-invocation reset required
by the JNI pipeline. The original ReLev automaton already implements the NGG
PAM.

Even another U280 installation may expose its platform under a different
`.xpfm` path. Override `DEVICE` with the platform reported by `platforminfo`,
for example:

```bash
make DEVICE=/opt/xilinx/platforms/<installed-u280-platform>/<platform>.xpfm \
  automata xclbin
```

A non-U280 card cannot use the included bitstream or HBM mapping unchanged.
Its platform path and `HBM_connectivity.cfg` memory-bank assignments must be
adapted before rebuilding. A CPU connected to an arbitrary FPGA is therefore
not sufficient.

The JNI runner currently opens XRT device index `0`. If the U280 is exposed
under another XRT device index, update `fpga/host/relev_jni.cpp` and rebuild the
JNI library.

## Required rebuild

Load the Vitis and XRT environments for the installed U280 platform, then
build all three integration artifacts:

```bash
export JAVA_HOME=/path/to/jdk
cd fpga

make automata
make xclbin
make jni
```

The relevant outputs are:

- `fpga/automata.hw.xo`: compiled kernel object;
- `fpga/automata.hw.xclbin`: U280 FPGA image containing the counter reset; and
- `fpga/librelev_jni.so`: native bridge loaded by Java.

`make jni` alone does not rebuild the FPGA image.

## Fixed search configuration

The current FPGA design and JNI adapter have the following fixed constraints:

- exactly 128 guide records are required;
- edit-distance thresholds are limited to 0 through 6;
- only the first 20 symbols of each guide line are read by ReLev;
- the host stores a `TGG` suffix in the pattern buffer, but the FPGA automaton
  independently hard-codes the PAM as `[ACGT]GG`; and
- the JNI library and `.xclbin` paths supplied to Java must be absolute.

The PAM wildcard matches `A`, `C`, `G`, or `T`; a reference-genome `N` does not
satisfy that wildcard. The original automaton's mismatch predicates also accept
only `A`, `C`, `G`, or `T`, so a reference `N` is not treated as a mismatch.

Supporting fewer guide lanes, another PAM, or a different FPGA device requires
corresponding ReLev host/kernel changes and a new bitstream.

## Run from AutoFFinder

Pass the rebuilt JNI library and FPGA image to AutoFFinder:

```bash
java \
  -Dautoffinder.candidateSource=fpga \
  -Drelev.xclbin=/absolute/path/to/automata.hw.xclbin \
  -Drelev.nativeLibrary=/absolute/path/to/librelev_jni.so \
  -cp bin PostAutoFFinder.AutoOffTargetSearchAlign \
  genome.fa guides.txt results/run \
  6 6 4 2 32 false 50 NGG false unused
```

AutoFFinder splits the supplied FASTA and invokes the JNI host once for each
forward and reverse-complement chromosome file. No intermediate candidate file
is required in this mode.

## Current limitations

- The FPGA output allocation is derived from the chromosome input size. The
  kernel does not currently expose an explicit output-capacity guard, so an
  unusually dense match set can exceed that allocation.
- ReLev processes the zero padding added to align an input to the 64-byte
  transfer size. Candidate positions produced only from that trailing padding
  are not currently filtered by the JNI adapter.
- The JNI/FPGA path must be validated on the target U280 system; it cannot be
  exercised on a CPU-only machine.

# For additional debugging and development

The project is broken up in several key components:

## The Kernel ##

The FPGA kernel is implemented in the krnl_automata.cpp file. This file implements the dataflow architecture and the FIFOs that connect HLS functions.

## The Levenshtein Automaton ##

The Levenshtein Automaton is implemented in automata_single_file.cpp. This file implements the Levenshtein Automaton overlay in HLS.

## For Simulation ##

fpga/run_hls.tcl contains the TCL script for simulating the overlay on the full design, but for a small part of the first human chromosome for testing and debugging. To run:
1. Update the input_filename and pattern_filename in fpga/kernel_automata_test.cpp to reflect the location of the repository and input files.
2. Run the simulation; this will take several minutes - `cd fpga; vitis_hls -f run_hls.tcl`