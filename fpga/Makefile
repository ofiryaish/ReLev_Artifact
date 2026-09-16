VPP := $(XILINX_VITIS)/bin/v++
EMCONFIGUTIL := $(XILINX_VITIS)/bin/emconfigutil
TARGET:= hw
DEVICE:= /opt/xilinx/platforms/xilinx_u280_gen3x16_xdma_1_202211_1/xilinx_u280_gen3x16_xdma_1_202211_1.xpfm 

# kernel targets
AUTOMATA_XO := automata.$(TARGET).xo
XCLBIN := automata.$(TARGET).xclbin

# host target
HOST_EXE := host.exe
JNI_LIB := librelev_jni.so

# config files target
EMCONFIG_FILE := emconfig.json

VPP_OPTS := -s -t $(TARGET)  --platform $(DEVICE)
VPP_OPTS += --temp_dir ./all_builds/$(TARGET)
VPP_OPTS += --report_dir ./all_logs/$(TARGET)
VPP_OPTS += --log_dir ./all_logs/$(TARGET)
VPP_LINK_OPTS += --config HBM_connectivity.cfg

CFLAGS := -g -std=c++1y -I$(XILINX_XRT)/include -I${XILINX_HLS}/include
LFLAGS := -L$(XILINX_XRT)/lib -lOpenCL -lxrt_coreutil -lrt -luuid

# run time args
CMD_ARGS:= automata.${TARGET}.xclbin ../input/chr1.txt ../patterns/128guides.txt 0

# primary build targets
.PHONY: automata xclbin host jni all

automata: $(AUTOMATA_XO)
xclbin:  $(XCLBIN)
host: $(HOST_EXE) 
jni: $(JNI_LIB)

all: xclbin host 

clean:
	-$(RM) $(EMCONFIG_FILE) $(HOST_EXE) $(JNI_LIB) $(XCLBIN)

cleanall: clean	
	rm -rf all_* *.log *.xclbin *.compile_summary *.link_summary *.info *.xo *.hw.* *.hw_emu.*

# kernel rules
$(AUTOMATA_XO): krnl_automata.cpp automata_single_file.cpp
	$(RM) $@
	$(VPP) $(VPP_OPTS) -c --hls.clock 500000000:krnl_automata -k krnl_automata -I'$(<D)' -o $@ $+

$(XCLBIN): $(AUTOMATA_XO)
	$(VPP) $(VPP_OPTS) $(VPP_LINK_OPTS) -l -o $@ $+

# host rules
$(HOST_EXE): ./host/host.cpp ./host/util.cpp
	g++ $(CFLAGS) -o $@ $+ $(LFLAGS)
	@echo 'Compiled Host Executable: $(HOST_EXE)'

$(JNI_LIB): ./host/relev_jni.cpp ./host/util.cpp
	@test -n "$(JAVA_HOME)" || (echo "JAVA_HOME must point to a JDK to build $(JNI_LIB)" >&2; exit 1)
	g++ $(CFLAGS) -fPIC -shared -I$(JAVA_HOME)/include -I$(JAVA_HOME)/include/linux -o $@ $+ $(LFLAGS)
	@echo 'Compiled JNI Library: $(JNI_LIB)'

$(EMCONFIG_FILE):
	$(EMCONFIGUTIL) --nd 1 --od . --platform $(DEVICE)

run: $(XCLBIN) $(HOST_EXE) $(EMCONFIG_FILE)
	XCL_EMULATION_MODE=${TARGET} ./$(HOST_EXE) $(CMD_ARGS)
