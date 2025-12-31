# 20251230

# export TOP_DIR =$(shell pwd)
# export ARCH = arm64
# export OUTPUT_DIR =$(TOP_DIR)/../bin
LD_FLAGS= -lm -lpthread -ldl -lX11 -lxcb -lXau -lXdmcp -ldrm -lXfixes -lXext -lXdamage -lexpat -lEGL -lGLESv2 -lavformat -lavcodec -lavutil -lswscale
LIB=-L$(MALI_LIBS)
INC=-I$(MALI_INCLUDE) -I$(DRM_INCLUDE)
PROJ_INCLUDES=-I../Coreframework

srcfiles := $(shell find ../Coreframework -maxdepth 1 -name "*.cpp")
objects  := $(patsubst %.cpp, %.o, $(srcfiles))

all: udc $(srcfiles)

udc $(objects) : udc.cpp $(srcfiles)
	$(ARM_CROSS_COMPILER_PATH) -g udc.cpp $(srcfiles) $(CXXFLAGS) $(LD_FLAGS) -o udc $(INC) $(PROJ_INCLUDES) $(LIB)
# 		cp -rf Supportingfiles bin
# 		cp -rf udc bin

# The dependency file names.
DEPS := $(objects:.o=.d)

bin=
bin_FBDEV=bin_FBDEV
bin_X11=bin_X11

ifeq ($(CXXFLAGS),-DENABLE_FBDEV)
        bin=$(bin_FBDEV)
else
        bin=$(bin_X11)
endif


# install: tiling
# 	mkdir -p $(TOP_DIR)/../bin/$(bin)/udc
# 	cp -rf udc  $(TOP_DIR)/../bin/$(bin)/udc
# 	cp -rf Supportingfiles  $(TOP_DIR)/../bin/$(bin)/udc


depend: .depend

.depend: $(srcfiles)
	rm -f ./.depend
	$(CXX) $(CXXFLAGS) -MM $^>>./.depend;

clean:
	rm -f $(objects)
# 	-rm -rf bin
	-rm -rf udc
	

dist-clean: clean
	rm -f *~ .depend


include .depend

