.SUFFIXES:

# Docker targets (host only)
ifneq ($(wildcard /.dockerenv),/.dockerenv)
-include .env

DOCKER_IMAGE := mattfly/ps3-builder:latest
RPCS3_HDD0 := $(HOME)/Library/Application Support/rpcs3/dev_hdd0
RPCS3_USRDIR := $(RPCS3_HDD0)/game/ROMI00001/USRDIR

PS3_IP ?= 192.168.1.100
PS3_FTP_PORT ?= 21
PS3_FTP := ftp://$(PS3_IP):$(PS3_FTP_PORT)

.PHONY: docker-build docker-build-debug docker-clean docker-image rpcs3-build rpcs3-db rpcs3-deploy rpcs3-clean ps3-ensure-dir ps3-upload-pkg ps3-upload-config ps3-upload-config-remote ps3-deploy ps3-debug ps3-debug-remote-db ps3-clean

# ---- helper: pull image from Docker Hub or build locally ----
docker-image:
	@ if [ -z "$$(docker images -q $(DOCKER_IMAGE) 2>/dev/null)" ]; then \
	    echo "Docker image '$(DOCKER_IMAGE)' not found locally..."; \
	    echo "Trying to pull from Docker Hub..."; \
	    docker pull $(DOCKER_IMAGE) || \
	    (echo "Pull failed, building locally..." && docker build --platform linux/amd64 -t $(DOCKER_IMAGE) .); \
	  else \
	    echo "Docker image '$(DOCKER_IMAGE)' already exists → skipping."; \
	  fi

docker-build: docker-image
	@docker run --rm --platform linux/amd64 \
	  -v "$(CURDIR)":/src -w /src $(DOCKER_IMAGE) make pkg

docker-build-debug: docker-image
	@docker run --rm --platform linux/amd64 \
	  -v "$(CURDIR)":/src -w /src $(DOCKER_IMAGE) make pkg DEBUGLOG=1 $(if $(ENABLE_NTFS),ENABLE_NTFS=1,)

docker-clean: docker-image
	@docker run --rm --platform linux/amd64 \
	  -v "$(CURDIR)":/src -w /src $(DOCKER_IMAGE) make clean

rpcs3-clean:
	@echo "Cleaning old database files from RPCS3..."
	@rm -f "$(RPCS3_USRDIR)"/romi_*.tsv "$(RPCS3_USRDIR)/sources.txt" "$(RPCS3_USRDIR)/config.txt" "$(RPCS3_USRDIR)/romi_debug.log"

rpcs3-db: rpcs3-clean
	# @mkdir -p "$(RPCS3_USRDIR)"
	@cp -v tools/databases/*.tsv "$(RPCS3_USRDIR)/" 2>/dev/null || true
	@cp -v tools/sources.txt "$(RPCS3_USRDIR)/"

rpcs3-build: docker-image
	@docker run --rm --platform linux/amd64 \
	  -v "$(CURDIR)":/src -w /src $(DOCKER_IMAGE) make pkg DEBUGLOG=1 CFLAGS_EXTRA="-DROMI_FILE_LOGGING"

rpcs3-deploy: rpcs3-build rpcs3-db
	@mkdir -p "$(RPCS3_USRDIR)/LANG"
	@cp -v pkgfiles/USRDIR/LANG/*.yts "$(RPCS3_USRDIR)/LANG/"
	@echo "Package and databases deployed to RPCS3"

rpcs3-deploy-remote: rpcs3-clean
	@echo "Deploying RPCS3 with remote database configuration..."
	@mkdir -p "$(RPCS3_USRDIR)/LANG"
	@cp -v pkgfiles/USRDIR/LANG/*.yts "$(RPCS3_USRDIR)/LANG/"
	@cp -v tools/sources.txt "$(RPCS3_USRDIR)/"
	@echo "url https://matheusfillipe.github.io/ROMi/romi_db.tsv" > "$(RPCS3_USRDIR)/config.txt"
	@if [ -n "$(PROXY_URL)" ]; then \
		echo "proxy_url $(PROXY_URL)" >> "$(RPCS3_USRDIR)/config.txt"; \
		[ -n "$(PROXY_USER)" ] && echo "proxy_user $(PROXY_USER)" >> "$(RPCS3_USRDIR)/config.txt" || true; \
		[ -n "$(PROXY_PASS)" ] && echo "proxy_pass $(PROXY_PASS)" >> "$(RPCS3_USRDIR)/config.txt" || true; \
	fi
	@echo ""
	@echo "✓ RPCS3 remote database mode configured!"
	@echo "  Database URL: https://matheusfillipe.github.io/ROMi/romi_db.tsv"
	@echo "  Sources file: tools/sources.txt"
	@echo "  Language files: *.yts"
	@echo "  Package deployed to: $(RPCS3_USRDIR)"

ps3-ensure-dir:
	@echo "Ensuring ROMi directories exist on PS3..."
	@curl -s -Q "MKD /dev_hdd0/game/ROMI00001" "$(PS3_FTP)/" 2>/dev/null || true
	@curl -s -Q "MKD /dev_hdd0/game/ROMI00001/USRDIR" "$(PS3_FTP)/" 2>/dev/null || true
	@curl -s -Q "MKD /dev_hdd0/game/ROMI00001/USRDIR/LANG" "$(PS3_FTP)/" 2>/dev/null || true
	@echo "✓ Directory structure ready"

ps3-clean:
	@echo "Cleaning old database files from PS3..."
	@curl -s -Q "DELE /dev_hdd0/game/ROMI00001/USRDIR/romi_db.tsv" "$(PS3_FTP)/" >/dev/null 2>&1 || true
	@curl -s -Q "DELE /dev_hdd0/game/ROMI00001/USRDIR/sources.txt" "$(PS3_FTP)/" >/dev/null 2>&1 || true
	@curl -s -Q "DELE /dev_hdd0/game/ROMI00001/USRDIR/config.txt" "$(PS3_FTP)/" >/dev/null 2>&1 || true
	@for plat in GB GBC GBA NES SNES Genesis SMS PSX PS2 PS3 N64 Arcade; do \
		curl -s -Q "DELE /dev_hdd0/game/ROMI00001/USRDIR/romi_$${plat}.tsv" "$(PS3_FTP)/" >/dev/null 2>&1 || true; \
	done

ps3-upload-pkg:
	@echo "Uploading PKG to PS3..."
	@curl -T src.pkg "$(PS3_FTP)/dev_hdd0/packages/romi.pkg"
	@echo "✓ PKG uploaded to /dev_hdd0/packages/romi.pkg"
	@echo ""
	@echo "Next steps:"
	@echo "  1. Go to Package Manager on your PS3"
	@echo "  2. Install 'romi.pkg'"
	@echo "  3. Run 'make ps3-upload-config' to upload database files"

ps3-upload-config: ps3-ensure-dir ps3-clean
	@echo "Uploading database files to PS3..."
	@curl -T tools/sources.txt "$(PS3_FTP)/dev_hdd0/game/ROMI00001/USRDIR/sources.txt"
	@for f in tools/databases/*.tsv; do \
	  curl -T "$$f" "$(PS3_FTP)/dev_hdd0/game/ROMI00001/USRDIR/$$(basename $$f)"; \
	done
	@echo "Uploading translations to PS3..."
	@for f in pkgfiles/USRDIR/LANG/*.yts; do \
	  curl -T "$$f" "$(PS3_FTP)/dev_hdd0/game/ROMI00001/USRDIR/LANG/$$(basename $$f)"; \
	done
	@echo "✓ Database files and translations uploaded"

ps3-deploy: ps3-upload-pkg ps3-upload-config
	@echo "✓ Full deployment complete"

ps3-debug: docker-clean docker-build-debug ps3-deploy
	@echo "Waiting for debug logs (multicast 239.255.0.100:30000)..."
	@socat udp4-recv:30000,ip-add-membership=239.255.0.100:0.0.0.0 -

ps3-upload-config-remote: ps3-ensure-dir ps3-clean
	@echo "Uploading remote database configuration to PS3..."
	@echo "url https://matheusfillipe.github.io/ROMi/romi_db.tsv" > /tmp/romi_config.txt
	@if [ -n "$(PROXY_URL)" ]; then \
		echo "proxy_url $(PROXY_URL)" >> /tmp/romi_config.txt; \
		[ -n "$(PROXY_USER)" ] && echo "proxy_user $(PROXY_USER)" >> /tmp/romi_config.txt || true; \
		[ -n "$(PROXY_PASS)" ] && echo "proxy_pass $(PROXY_PASS)" >> /tmp/romi_config.txt || true; \
	fi
	@curl -T /tmp/romi_config.txt "$(PS3_FTP)/dev_hdd0/game/ROMI00001/USRDIR/config.txt"
	@rm /tmp/romi_config.txt
	@curl -T tools/sources.txt "$(PS3_FTP)/dev_hdd0/game/ROMI00001/USRDIR/sources.txt"
	@echo "Uploading translations to PS3..."
	@for f in pkgfiles/USRDIR/LANG/*.yts; do \
	  curl -T "$$f" "$(PS3_FTP)/dev_hdd0/game/ROMI00001/USRDIR/LANG/$$(basename $$f)"; \
	done
	@echo "✓ Remote database configuration and translations uploaded"

ps3-debug-remote-db: docker-clean docker-build-debug ps3-upload-pkg ps3-upload-config-remote
	@echo ""
	@echo "✓ Remote database mode configured!"
	@echo "  Database URL: https://matheusfillipe.github.io/ROMi/romi_db.tsv"
	@echo "  PKG uploaded to: /dev_hdd0/packages/romi.pkg"
	@echo ""
	@echo "Next steps:"
	@echo "  1. Install the package on your PS3 (Package Manager → romi.pkg)"
	@echo "  2. Run ROMi and press 'Refresh' to download database"
	@echo ""
	@echo "Waiting for debug logs (multicast 239.255.0.100:30000)..."
	@socat udp4-recv:30000,ip-add-membership=239.255.0.100:0.0.0.0 -
endif

# (rest of your original Makefile as before)

DOCKER_TARGETS := docker-image docker-build docker-build-debug docker-clean rpcs3-db rpcs3-deploy rpcs3-deploy-remote rpcs3-clean ps3-ensure-dir ps3-upload-pkg ps3-upload-config ps3-upload-config-remote ps3-deploy ps3-debug ps3-debug-remote-db ps3-clean
ifneq ($(filter $(DOCKER_TARGETS),$(MAKECMDGOALS)),)
  PSL1GHT_SKIP := 1
endif

ifndef PSL1GHT_SKIP
ifeq ($(strip $(PSL1GHT)),)
$(error "Please set PSL1GHT in your environment. export PSL1GHT=<path>")
endif
endif

ifdef PSL1GHT_SKIP
.DEFAULT_GOAL := docker-build
else

#---------------------------------------------------------------------------------
#  TITLE, APPID, CONTENTID, ICON0 SFOXML before ppu_rules.
#---------------------------------------------------------------------------------
TITLE		:=	ROMi PS3
APPID		:=	NP00ROMI3
CONTENTID	:=	UP0001-$(APPID)_00-0000000000000000
ICON0		:=	pkgfiles/ICON0.PNG
SFOXML		:=	sfo.xml

include $(PSL1GHT)/ppu_rules

# aditional scetool flags (--self-ctrl-flags, --self-cap-flags...)
SCETOOL_FLAGS	+=	

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
#---------------------------------------------------------------------------------
TARGET		:=	$(notdir $(CURDIR))
BUILD		:=	build
SOURCES		:=	source
DATA		:=	data
SHADERS		:=	shaders
INCLUDES	:=	include
PKGFILES	:=	pkgfiles

#---------------------------------------------------------------------------------
# any extra libraries we wish to link with the project
#---------------------------------------------------------------------------------
LIBS		:=	-lcurl -lxml2 -lya2d -lfont3d -ltiny3d -lsimdmath -lgcm_sys -lio -lsysutil -lrt -llv2 -lpngdec -lsysmodule -lm -lsysfs  -ljpgdec \
				-lnet -lfreetype -lz -lmikmod -laudio -lpolarssl -lmini18n -ljson-c


#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------

CFLAGS		=	-O2 -Wall -mcpu=cell -std=gnu99 $(MACHDEP) $(INCLUDE)
CXXFLAGS	=	$(CFLAGS)

LDFLAGS		=	$(MACHDEP) -Wl,-Map,$(notdir $@).map

ifdef DEBUGLOG
CFLAGS		+=	-DROMI_ENABLE_LOGGING
LIBS		+=	-ldbglogger
endif

ifdef CFLAGS_EXTRA
CFLAGS		+=	$(CFLAGS_EXTRA)
endif

# Enable NTFS support by default (disable with ENABLE_NTFS=0)
ifndef DISABLE_NTFS
CFLAGS		+=	-DHAS_NTFS_SUPPORT
endif
#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:=

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
					$(foreach dir,$(SHADERS),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

export BUILDDIR	:=	$(CURDIR)/$(BUILD)

#---------------------------------------------------------------------------------
# automatically build a list of object files for our project
#---------------------------------------------------------------------------------
CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES	:=  $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.bin)))
PNGFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.png)))
JPGFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.jpg)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES	:=	$(addsuffix .o,$(BINFILES)) \
					$(addsuffix .o,$(PNGFILES)) \
					$(addsuffix .o,$(JPGFILES)) \
					$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) \
					$(sFILES:.s=.o) $(SFILES:.S=.o)

#---------------------------------------------------------------------------------
# build a list of include paths
#---------------------------------------------------------------------------------
export INCLUDE	:=	$(foreach dir,$(INCLUDES), -I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					$(LIBPSL1GHT_INC) \
					-I$(PORTLIBS)/include/freetype2 \
					-I$(PORTLIBS)/include/libxml2 \
					-I$(CURDIR)/$(BUILD) -I$(PORTLIBS)/include

#---------------------------------------------------------------------------------
# build a list of library paths
#---------------------------------------------------------------------------------
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
					$(LIBPSL1GHT_LIB) -L$(PORTLIBS)/lib

export OUTPUT	:=	$(CURDIR)/$(TARGET)
.PHONY: $(BUILD) clean compile-translations


#---------------------------------------------------------------------------------
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).self  EBOOT.BIN

#---------------------------------------------------------------------------------
run:
	ps3load $(OUTPUT).self

#---------------------------------------------------------------------------------
# Compile translations from .po to .yts (ALWAYS runs before packaging)
#---------------------------------------------------------------------------------
compile-translations:
	@echo "Compiling translations..."
	@cd $(PKGFILES)/USRDIR/LANG && for po in *.po; do \
		python3 $(CURDIR)/tools/po_to_yts.py "$$po" "$${po%.po}.yts" || true; \
	done
	@echo "Translations compiled successfully"

#---------------------------------------------------------------------------------
pkg:	$(BUILD) compile-translations $(OUTPUT).pkg

#---------------------------------------------------------------------------------

npdrm: $(BUILD)
	@$(SELF_NPDRM) $(SCETOOL_FLAGS) --np-content-id=$(CONTENTID) --encrypt $(BUILDDIR)/$(basename $(notdir $(OUTPUT))).elf $(BUILDDIR)/../EBOOT.BIN

#---------------------------------------------------------------------------------
quickpkg:
	$(VERB) cd $(PKGFILES)/USRDIR/LANG && for po in *.po; do python3 $(CURDIR)/tools/po_to_yts.py "$$po" "$${po%.po}.yts" || true; done
	$(VERB) if [ -n "$(PKGFILES)" -a -d "$(PKGFILES)" ]; then cp -rf $(PKGFILES)/* $(BUILDDIR)/pkg/; fi
	$(VERB) $(PKG) --contentid $(CONTENTID) $(BUILDDIR)/pkg/ $(TARGET).pkg >> /dev/null
	$(VERB) cp $(TARGET).pkg $(TARGET).gnpdrm.pkg
	$(VERB) $(PACKAGE_FINALIZE) $(TARGET).gnpdrm.pkg

else

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf:	$(OFILES)

#---------------------------------------------------------------------------------
# This rule links in binary data with the .bin extension
#---------------------------------------------------------------------------------
%.bin.o	:	%.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
%.it.o	:	%.it
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
%.png.o	:	%.png
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
%.jpg.o	:	%.jpg
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------

endif
