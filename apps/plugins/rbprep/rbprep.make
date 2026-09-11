# Rekordpod is a multi-file plugin so performance-critical and persistent
# subsystems can be compiled and tested independently.

RBPREPSRCDIR := $(APPSDIR)/plugins/rbprep
RBPREPBUILDDIR := $(BUILDDIR)/apps/plugins/rbprep

ROCKS += $(RBPREPBUILDDIR)/rbprep.rock

RBPREP_SRC := $(call preprocess, $(RBPREPSRCDIR)/SOURCES)
RBPREP_OBJ := $(call c2obj, $(RBPREP_SRC))

OTHER_SRC += $(RBPREP_SRC)

$(RBPREPBUILDDIR)/rbprep.rock: $(RBPREP_OBJ)

$(RBPREPBUILDDIR)/%.o: $(RBPREPSRCDIR)/%.c $(RBPREPSRCDIR)/rbprep.make
	$(SILENT)mkdir -p $(dir $@)
	$(call PRINTS,CC $(subst $(ROOTDIR)/,,$<))$(CC) -I$(RBPREPSRCDIR) \
		$(PLUGINFLAGS) -c $< -o $@
