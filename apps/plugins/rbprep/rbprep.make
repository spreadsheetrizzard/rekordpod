# SPDX-License-Identifier: GPL-2.0-or-later

# Rekordpod is a multi-file plugin so performance-critical and persistent
# subsystems can be compiled and tested independently.

RBPREPSRCDIR := $(APPSDIR)/plugins/rbprep
RBPREPBUILDDIR := $(BUILDDIR)/apps/plugins/rbprep

# Release builds keep the private framebuffer capture shortcut out of the
# binary. Set REKORDPOD_PRIVATE_SCREENSHOTS=1 when invoking make to include it.
REKORDPOD_PRIVATE_SCREENSHOTS ?= 0
RBPREP_FEATURE_FLAGS := \
	-DREKORDPOD_ENABLE_SCREENSHOTS=$(REKORDPOD_PRIVATE_SCREENSHOTS)

ROCKS += $(RBPREPBUILDDIR)/rekordpod.rock

RBPREP_SRC := $(call preprocess, $(RBPREPSRCDIR)/SOURCES)
RBPREP_OBJ := $(call c2obj, $(RBPREP_SRC))

OTHER_SRC += $(RBPREP_SRC)

$(RBPREPBUILDDIR)/rekordpod.rock: $(RBPREP_OBJ)

$(RBPREPBUILDDIR)/%.o: $(RBPREPSRCDIR)/%.c $(RBPREPSRCDIR)/rbprep.make
	$(SILENT)mkdir -p $(dir $@)
	$(call PRINTS,CC $(subst $(ROOTDIR)/,,$<))$(CC) -I$(RBPREPSRCDIR) \
		$(PLUGINFLAGS) $(RBPREP_FEATURE_FLAGS) -c $< -o $@
