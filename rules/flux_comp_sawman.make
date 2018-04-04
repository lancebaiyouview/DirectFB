FLUX_SAWMAN_ARGS_BASE = -i --call-mode --static-args-bytes=FLUXED_ARGS_BYTES
FLUX_SAWMAN_ARGS = $(FLUX_SAWMAN_ARGS_BASE) --dispatch-error-abort

$(builddir)/%.cpp $(builddir)/%.h: $(srcdir)/%.flux
	$(FLUXCOMP) $(FLUX_SAWMAN_ARGS) $<

$(builddir)/SaWManManager.cpp: FLUX_SAWMAN_ARGS=$(FLUX_SAWMAN_ARGS_BASE)
